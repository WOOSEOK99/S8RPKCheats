/*
 * TavernMonitor.cpp
 * -----------------
 * 주점 청부(Request) 4개 유지 모니터
 *
 * 실제 게임 주기에 맞춘 동작:
 *   1. 청부의 새 갱신 시점은 월 변경이 아니라 평정(0x05) -> 내정(0x07) 전환입니다.
 *   2. 내정 기간(약 3개월) 동안 완료되어 사라진 청부는 기존 백업으로 복원해 계속 유지합니다.
 *   3. 평정 -> 내정 전환 후 게임의 새 청부 생성이 끝날 때까지 2.5초 기다립니다.
 *   4. 새 갱신 결과가 4개 미만이면 다른 도시의 현재 유효한 청부를 우선 가져와 4개까지 채웁니다.
 *   5. 월이 바뀌어도 백업/주입 상태를 폐기하지 않습니다.
 *
 * 안전성:
 *   - 다른 도시에서 가져올 때 현재 원본 슬롯에 실제로 살아 있는 청부만 사용합니다.
 *   - 우리가 주입한 슬롯을 다시 원본 후보로 연쇄 사용하지 않습니다.
 *   - 세이브 로드/시나리오 전환 등으로 도시 배열 베이스가 바뀌면 런타임 포인터 백업을 폐기합니다.
 *   - name/data 포인터는 모두 읽기 가능한 경우에만 사용합니다.
 *   - name/data 두 포인터는 한 보호구간에서 함께 쓰고 실패 시 원래 값으로 롤백합니다.
 */

#include "TavernMonitor.h"
#include "../../Cheats.h"
#include "../../MenuState.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <windows.h>

namespace DX11Base {

    namespace {
        constexpr int kCityCount = 51;
        constexpr int kSlotCount = 4;
        constexpr uintptr_t kCityStride = 0x2A0;
        constexpr ULONGLONG kPhaseStabilizeMs = 2500;
        constexpr uint8_t kStateCouncil = 0x05;
        constexpr uint8_t kStateDomestic = 0x07;

        constexpr uintptr_t kNameOff[kSlotCount] = {0x178, 0x1A0, 0x1C8, 0x1F0};
        constexpr uintptr_t kDataOff[kSlotCount] = {0x180, 0x1A8, 0x1D0, 0x1F8};

        struct TavernRequest {
            uintptr_t name = 0;
            uintptr_t data = 0;
        };

        struct BackupEntry {
            TavernRequest req{};
            bool borrowed = false;
            int sourceCity = -1;
            int sourceSlot = -1;
        };

        struct LiveSource {
            int city = -1;
            int slot = -1;
            TavernRequest req{};
        };

        static BackupEntry s_backups[kCityCount][kSlotCount] = {};
        static TavernRequest s_lastInjected[kCityCount][kSlotCount] = {};
        static uint8_t s_lastRelevantGameState = 0;
        static ULONGLONG s_phaseChangeTime = 0;
        static bool s_waitingStabilize = false;
        static bool s_wasEnabled = false;
        static uintptr_t s_lastCityArrayBase = 0;

        static bool SameRequest(const TavernRequest& a, const TavernRequest& b) {
            return a.name == b.name && a.data == b.data;
        }

        static bool HasRequest(const TavernRequest& req) {
            return req.name > 0x10000 && req.data > 0x10000;
        }

        static void ClearGenerationState() {
            std::memset(s_backups, 0, sizeof(s_backups));
            std::memset(s_lastInjected, 0, sizeof(s_lastInjected));
        }

        static void ResetMonitorState() {
            ClearGenerationState();
            s_lastRelevantGameState = 0;
            s_phaseChangeTime = 0;
            s_waitingStabilize = false;
            s_lastCityArrayBase = 0;
        }

        static bool IsReadableAddress(uintptr_t addr, SIZE_T size = 1) {
            if (addr <= 0x10000 || size == 0)
                return false;

            uintptr_t end = addr + size - 1;
            if (end < addr)
                return false;

            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi))
                return false;
            if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
                return false;

            uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
            uintptr_t regionEnd = regionStart + mbi.RegionSize;
            if (regionEnd < regionStart)
                return false;

            return end < regionEnd;
        }

        static bool IsUsableRequest(const TavernRequest& req) {
            return HasRequest(req) && IsReadableAddress(req.name, 1) && IsReadableAddress(req.data, 1);
        }

        static bool ReadPtr(uintptr_t addr, uintptr_t* out) {
            if (!out)
                return false;

            __try {
                *out = *(uintptr_t*)addr;
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                *out = 0;
                return false;
            }
        }

        static bool ReadRequestPair(uintptr_t cityAddr, int slot, TavernRequest* out) {
            if (!out || slot < 0 || slot >= kSlotCount)
                return false;

            TavernRequest req{};
            if (!ReadPtr(cityAddr + kNameOff[slot], &req.name))
                return false;
            if (!ReadPtr(cityAddr + kDataOff[slot], &req.data))
                return false;

            *out = req;
            return true;
        }

        static bool WriteRequestPair(uintptr_t cityAddr, int slot, const TavernRequest& req) {
            if (slot < 0 || slot >= kSlotCount || !IsUsableRequest(req))
                return false;

            uintptr_t nameAddr = cityAddr + kNameOff[slot];
            uintptr_t dataAddr = cityAddr + kDataOff[slot];
            if (dataAddr != nameAddr + sizeof(uintptr_t))
                return false;
            if (!IsReadableAddress(nameAddr, sizeof(uintptr_t) * 2))
                return false;

            DWORD oldProtect = 0;
            if (!VirtualProtect((LPVOID)nameAddr, sizeof(uintptr_t) * 2, PAGE_READWRITE, &oldProtect))
                return false;

            bool ok = false;
            uintptr_t oldName = 0;
            uintptr_t oldData = 0;

            __try {
                oldName = *(uintptr_t*)nameAddr;
                oldData = *(uintptr_t*)dataAddr;

                *(uintptr_t*)nameAddr = req.name;
                *(uintptr_t*)dataAddr = req.data;

                ok = (*(uintptr_t*)nameAddr == req.name && *(uintptr_t*)dataAddr == req.data);
                if (!ok) {
                    *(uintptr_t*)nameAddr = oldName;
                    *(uintptr_t*)dataAddr = oldData;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                __try {
                    *(uintptr_t*)nameAddr = oldName;
                    *(uintptr_t*)dataAddr = oldData;
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                }
                ok = false;
            }

            DWORD dummy = 0;
            VirtualProtect((LPVOID)nameAddr, sizeof(uintptr_t) * 2, oldProtect, &dummy);
            return ok;
        }

        static uintptr_t ResolveCityBase() {
            uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
            if (exeBase <= 0x10000)
                return 0;

            uintptr_t p1 = 0, p2 = 0, cityBase = 0;
            if (!ReadPtr(exeBase + 0x34C8630, &p1) || p1 <= 0x10000)
                return 0;
            if (!ReadPtr(p1, &p2) || p2 <= 0x10000)
                return 0;
            if (!ReadPtr(p2, &cityBase) || cityBase <= 0x10000)
                return 0;
            return cityBase;
        }

        static uint8_t ReadRelevantGameState() {
            uintptr_t gameBase = GetGameBase();
            if (gameBase <= 0x10000 || !IsValidPtr(gameBase + 0xD0, 1))
                return 0;

            uint8_t state = 0;
            __try {
                state = *(uint8_t*)(gameBase + 0xD0);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return 0;
            }

            return (state == kStateCouncil || state == kStateDomestic) ? state : 0;
        }

        static bool SourceStillNativeAndLive(uintptr_t cityArrayBase, int sourceCity, int sourceSlot,
                                             const TavernRequest& expected) {
            if (sourceCity < 0 || sourceCity >= kCityCount || sourceSlot < 0 || sourceSlot >= kSlotCount)
                return false;

            uintptr_t sourceAddr = cityArrayBase + (uintptr_t)sourceCity * kCityStride;
            TavernRequest current{};
            if (!ReadRequestPair(sourceAddr, sourceSlot, &current) || !SameRequest(current, expected) ||
                !IsUsableRequest(current)) {
                return false;
            }

            if (HasRequest(s_lastInjected[sourceCity][sourceSlot]) &&
                SameRequest(current, s_lastInjected[sourceCity][sourceSlot])) {
                return false;
            }

            return true;
        }

        static bool DataAlreadyInCity(const TavernRequest current[kSlotCount], int ignoreSlot, uintptr_t dataPtr) {
            if (dataPtr <= 0x10000)
                return true;

            for (int s = 0; s < kSlotCount; ++s) {
                if (s == ignoreSlot)
                    continue;
                if (current[s].data == dataPtr)
                    return true;
            }
            return false;
        }

        static bool FindLiveCandidate(uintptr_t cityArrayBase, const std::vector<LiveSource>& sources,
                                      int targetCity, int targetSlot, const TavernRequest current[kSlotCount],
                                      LiveSource* out) {
            if (!out)
                return false;

            for (const auto& source : sources) {
                if (source.city == targetCity)
                    continue;
                if (DataAlreadyInCity(current, targetSlot, source.req.data))
                    continue;
                if (!SourceStillNativeAndLive(cityArrayBase, source.city, source.slot, source.req))
                    continue;

                *out = source;
                return true;
            }
            return false;
        }

        static bool CanRestoreBackup(uintptr_t cityArrayBase, const BackupEntry& backup) {
            if (!IsUsableRequest(backup.req))
                return false;

            if (!backup.borrowed)
                return true;

            return SourceStillNativeAndLive(cityArrayBase, backup.sourceCity, backup.sourceSlot, backup.req);
        }

        static void SetNativeBackup(int city, int slot, const TavernRequest& req) {
            s_backups[city][slot].req = req;
            s_backups[city][slot].borrowed = false;
            s_backups[city][slot].sourceCity = -1;
            s_backups[city][slot].sourceSlot = -1;
        }

        static void SetBorrowedBackup(int city, int slot, const LiveSource& source) {
            s_backups[city][slot].req = source.req;
            s_backups[city][slot].borrowed = true;
            s_backups[city][slot].sourceCity = source.city;
            s_backups[city][slot].sourceSlot = source.slot;
        }

        static bool TryFillFromLiveSource(uintptr_t cityArrayBase, const std::vector<LiveSource>& nativeSources,
                                          int city, int slot, uintptr_t cityAddr,
                                          TavernRequest current[kSlotCount]) {
            LiveSource source{};
            if (!FindLiveCandidate(cityArrayBase, nativeSources, city, slot, current, &source))
                return false;
            if (!WriteRequestPair(cityAddr, slot, source.req))
                return false;

            current[slot] = source.req;
            s_lastInjected[city][slot] = source.req;
            SetBorrowedBackup(city, slot, source);
            return true;
        }

        static void MaintainFourRequests(uintptr_t cityArrayBase, bool preferCurrentGenerationSources = false) {
            TavernRequest current[kCityCount][kSlotCount] = {};
            bool pairRead[kCityCount][kSlotCount] = {};
            std::vector<LiveSource> nativeSources;
            nativeSources.reserve(kCityCount * kSlotCount);

            // 1차 패스: 현재 게임이 실제로 가진 청부를 수집하고 같은 슬롯 백업을 최신화합니다.
            for (int c = 0; c < kCityCount; ++c) {
                uintptr_t cityAddr = cityArrayBase + (uintptr_t)c * kCityStride;

                for (int s = 0; s < kSlotCount; ++s) {
                    TavernRequest req{};
                    if (!ReadRequestPair(cityAddr, s, &req))
                        continue;

                    pairRead[c][s] = true;
                    current[c][s] = req;

                    const bool bothEmpty = (req.name == 0 && req.data == 0);
                    const bool bothPresent = HasRequest(req);

                    if (bothPresent && IsUsableRequest(req)) {
                        const bool isOurInjected = HasRequest(s_lastInjected[c][s]) &&
                                                   SameRequest(req, s_lastInjected[c][s]);

                        if (!isOurInjected) {
                            SetNativeBackup(c, s, req);
                            s_lastInjected[c][s] = {};
                            nativeSources.push_back({c, s, req});
                        }
                    } else if (bothEmpty) {
                        s_lastInjected[c][s] = {};
                    }
                }
            }

            // 2차 패스: 빈 슬롯을 복원/보충합니다.
            for (int c = 0; c < kCityCount; ++c) {
                uintptr_t cityAddr = cityArrayBase + (uintptr_t)c * kCityStride;

                for (int s = 0; s < kSlotCount; ++s) {
                    if (!pairRead[c][s])
                        continue;

                    TavernRequest& slot = current[c][s];
                    const bool bothEmpty = (slot.name == 0 && slot.data == 0);
                    const bool bothPresent = HasRequest(slot);
                    const bool isOurInjected = bothPresent && HasRequest(s_lastInjected[c][s]) &&
                                               SameRequest(slot, s_lastInjected[c][s]);

                    if (bothEmpty) {
                        // 평정 -> 내정 직후에는 이번 갱신에서 실제로 생성된 다른 도시 청부를 먼저 사용합니다.
                        if (preferCurrentGenerationSources &&
                            TryFillFromLiveSource(cityArrayBase, nativeSources, c, s, cityAddr, current[c])) {
                            continue;
                        }

                        const BackupEntry backup = s_backups[c][s];
                        if (CanRestoreBackup(cityArrayBase, backup) &&
                            !DataAlreadyInCity(current[c], s, backup.req.data) &&
                            WriteRequestPair(cityAddr, s, backup.req)) {
                            slot = backup.req;
                            s_lastInjected[c][s] = backup.req;
                            continue;
                        }

                        // 평상시 또는 백업 복원이 불가능한 경우 현재 다른 도시의 실제 청부로 보충합니다.
                        TryFillFromLiveSource(cityArrayBase, nativeSources, c, s, cityAddr, current[c]);
                        continue;
                    }

                    if (!bothPresent || !IsUsableRequest(slot))
                        continue;

                    // 빌려온 청부의 원본이 사라졌다면 다른 현재 유효 청부로 교체합니다.
                    if (isOurInjected && s_backups[c][s].borrowed &&
                        !SourceStillNativeAndLive(cityArrayBase, s_backups[c][s].sourceCity,
                                                  s_backups[c][s].sourceSlot, s_backups[c][s].req)) {
                        TryFillFromLiveSource(cityArrayBase, nativeSources, c, s, cityAddr, current[c]);
                    }
                }
            }
        }
    } // namespace

    void UpdateTavernRequests() {
        if (!bInfiniteTavernRequests) {
            if (s_wasEnabled) {
                ResetMonitorState();
                s_wasEnabled = false;
            }
            return;
        }

        if (!s_wasEnabled) {
            ResetMonitorState();
            s_wasEnabled = true;
        }

        uintptr_t cityArrayBase = ResolveCityBase();
        if (cityArrayBase <= 0x10000)
            return;

        // 세이브 로드/시나리오 전환 등으로 도시 배열이 바뀌면 이전 세션 포인터를 폐기합니다.
        if (s_lastCityArrayBase != 0 && cityArrayBase != s_lastCityArrayBase) {
            ResetMonitorState();
            s_lastCityArrayBase = cityArrayBase;
            return;
        }
        if (s_lastCityArrayBase == 0)
            s_lastCityArrayBase = cityArrayBase;

        uint8_t gameState = ReadRelevantGameState();
        if (gameState == 0)
            return;

        // 첫 유효 상태는 기준만 잡습니다. 내정 중 처음 켠 경우에는 현재 청부를 즉시 유지 대상으로 잡습니다.
        if (s_lastRelevantGameState == 0) {
            s_lastRelevantGameState = gameState;
            if (gameState == kStateDomestic)
                MaintainFourRequests(cityArrayBase, true);
            return;
        }

        // 중간 상태값은 ReadRelevantGameState()에서 무시하므로 05/07의 실제 방향만 비교됩니다.
        if (gameState != s_lastRelevantGameState) {
            const uint8_t prevState = s_lastRelevantGameState;
            s_lastRelevantGameState = gameState;

            // 평정 -> 내정: 게임이 새 청부를 만들 시간을 준 뒤 새 현재 청부를 우선으로 4개 구성합니다.
            // 기존 백업은 여기서 지우지 않습니다. 새 청부가 부족/미생성일 때 안전한 fallback으로 남겨둡니다.
            if (prevState == kStateCouncil && gameState == kStateDomestic) {
                s_phaseChangeTime = GetTickCount64();
                s_waitingStabilize = true;
                return;
            }

            // 내정 -> 평정: 평정 중에는 청부 메모리를 건드리지 않고 다음 내정 전환을 기다립니다.
            if (gameState == kStateCouncil) {
                s_waitingStabilize = false;
                return;
            }
        }

        if (gameState != kStateDomestic)
            return;

        if (s_waitingStabilize) {
            if (GetTickCount64() - s_phaseChangeTime < kPhaseStabilizeMs)
                return;

            s_waitingStabilize = false;
            MaintainFourRequests(cityArrayBase, true);
            return;
        }

        // 내정 1~3개월 동안 월 변경과 무관하게 동일 청부 세대를 유지합니다.
        MaintainFourRequests(cityArrayBase, false);
    }

} // namespace DX11Base
