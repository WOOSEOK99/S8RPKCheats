/*
 * TavernMonitor.cpp
 * -----------------
 * 주점 청부(Request) 4개 유지 모니터
 *
 * 목표 동작:
 *   1. 각 도시의 청부 슬롯을 최대치인 4개로 유지합니다.
 *   2. 새로운 청부 갱신 전까지는 현재 달의 기존 청부를 유지합니다.
 *   3. 새 청부 갱신 후 어떤 도시가 4개 미만이면, 다른 도시의 현재 유효한 청부를 가져와 4개로 채웁니다.
 *
 * 각 도시 객체(크기 0x2A0) 내 청부 슬롯 구조:
 *   도시 베이스 + 0x178 : 청부1 의뢰명 포인터 (8바이트)
 *   도시 베이스 + 0x180 : 청부1 데이터 포인터 (8바이트)
 *   도시 베이스 + 0x1A0 : 청부2 의뢰명 포인터
 *   도시 베이스 + 0x1A8 : 청부2 데이터 포인터
 *   도시 베이스 + 0x1C8 : 청부3 의뢰명 포인터
 *   도시 베이스 + 0x1D0 : 청부3 데이터 포인터
 *   도시 베이스 + 0x1F0 : 청부4 의뢰명 포인터
 *   도시 베이스 + 0x1F8 : 청부4 데이터 포인터
 *
 * 안전성 보강:
 *   - 백업 포인터는 같은 달 안에서만 사용합니다.
 *   - 월이 바뀌면 이전 달 백업/주입 기록을 모두 폐기하고 게임의 새 청부 생성을 2.5초 기다립니다.
 *   - 다른 도시에서 보충할 때는 과거 백업이 아니라 현재 그 원본 슬롯에 실제 존재하는 청부만 사용합니다.
 *   - 다른 도시에서 빌려온 청부는 원본 도시/슬롯을 추적합니다.
 *   - 원본 슬롯이 더 이상 같은 청부를 갖고 있지 않으면, 가능한 경우 다른 현재 유효 청부로 교체합니다.
 *   - 세이브 로드/시나리오 전환 등으로 도시 배열 베이스가 바뀌면 모든 런타임 포인터 백업을 폐기합니다.
 *   - name/data가 모두 존재하고 실제 읽기 가능한 포인터일 때만 백업/복구/공유합니다.
 *   - name/data 두 포인터는 한 번의 보호구간 안에서 함께 쓰고 실패 시 원래 값으로 롤백합니다.
 *
 * 포인터 체인 (명품 창과 동일):
 *   exe + 0x34C8630 -> p1
 *   p1              -> p2
 *   p2              -> cityArrayBase
 */

#include "TavernMonitor.h"
#include "../../MenuState.h"
#include "../System/SystemMonth.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <windows.h>

namespace DX11Base {

    namespace {
        constexpr int kCityCount = 51;
        constexpr int kSlotCount = 4;
        constexpr uintptr_t kCityStride = 0x2A0;
        constexpr ULONGLONG kMonthStabilizeMs = 2500;

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
        static uint8_t s_lastMonth = 0xFF;
        static ULONGLONG s_monthChangeTime = 0;
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
            s_lastMonth = 0xFF;
            s_monthChangeTime = 0;
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

            // 현재 구조에서는 name/data가 연속된 두 포인터입니다.
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

            // 우리가 채워 넣은 슬롯을 다시 원본 후보로 연쇄 사용하지 않습니다.
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

            // 다른 도시에서 빌려온 청부는 원본 슬롯이 아직 실제 원본으로 살아 있을 때만 재사용합니다.
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

        static void MaintainFourRequests(uintptr_t cityArrayBase) {
            TavernRequest current[kCityCount][kSlotCount] = {};
            bool pairRead[kCityCount][kSlotCount] = {};
            std::vector<LiveSource> nativeSources;
            nativeSources.reserve(kCityCount * kSlotCount);

            // 1차 패스: 현재 게임이 실제로 가진 청부를 수집하고 백업을 최신화합니다.
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
                            // 게임이 만든/갱신한 실제 청부. 다른 도시 보충용 원본 후보로 사용할 수 있습니다.
                            SetNativeBackup(c, s, req);
                            s_lastInjected[c][s] = {};
                            nativeSources.push_back({c, s, req});
                        }
                    } else if (bothEmpty) {
                        // 우리가 넣었던 값도 게임이 지운 상태이므로 주입 표식만 해제합니다.
                        s_lastInjected[c][s] = {};
                    }
                    // name/data 중 하나만 남은 과도 상태는 이번 틱에 건드리지 않습니다.
                }
            }

            // 2차 패스: 빈 슬롯 복구 및 다른 도시 청부로 4개 채우기.
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
                        const BackupEntry backup = s_backups[c][s];

                        // 우선 같은 달의 자기 슬롯 기존 청부를 유지합니다.
                        if (CanRestoreBackup(cityArrayBase, backup) &&
                            !DataAlreadyInCity(current[c], s, backup.req.data) &&
                            WriteRequestPair(cityAddr, s, backup.req)) {
                            slot = backup.req;
                            s_lastInjected[c][s] = backup.req;
                            continue;
                        }

                        // 자기 백업을 안전하게 쓸 수 없으면 현재 다른 도시의 실제 청부에서 보충합니다.
                        LiveSource source{};
                        if (FindLiveCandidate(cityArrayBase, nativeSources, c, s, current[c], &source) &&
                            WriteRequestPair(cityAddr, s, source.req)) {
                            slot = source.req;
                            s_lastInjected[c][s] = source.req;
                            SetBorrowedBackup(c, s, source);
                        }
                        continue;
                    }

                    if (!bothPresent || !IsUsableRequest(slot))
                        continue;

                    // 다른 도시에서 빌려온 슬롯은 원본이 사라졌다면 가능한 즉시 다른 현재 유효 청부로 교체합니다.
                    if (isOurInjected && s_backups[c][s].borrowed &&
                        !SourceStillNativeAndLive(cityArrayBase, s_backups[c][s].sourceCity,
                                                  s_backups[c][s].sourceSlot, s_backups[c][s].req)) {
                        LiveSource replacement{};
                        if (FindLiveCandidate(cityArrayBase, nativeSources, c, s, current[c], &replacement) &&
                            WriteRequestPair(cityAddr, s, replacement.req)) {
                            slot = replacement.req;
                            s_lastInjected[c][s] = replacement.req;
                            SetBorrowedBackup(c, s, replacement);
                        }
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

        // 세이브 로드/시나리오 전환 등으로 도시 배열이 바뀌면 이전 세션의 포인터는 폐기합니다.
        if (s_lastCityArrayBase != 0 && cityArrayBase != s_lastCityArrayBase) {
            ResetMonitorState();
            s_lastCityArrayBase = cityArrayBase;
            return;
        }
        if (s_lastCityArrayBase == 0)
            s_lastCityArrayBase = cityArrayBase;

        uint8_t currentMonth = GetSystemMonthValue();
        if (currentMonth < 1 || currentMonth > 12)
            return;

        // 최초 활성화 시 현재 청부를 기준으로 즉시 4개 유지 로직을 시작합니다.
        if (s_lastMonth == 0xFF) {
            s_lastMonth = currentMonth;
            ClearGenerationState();
            MaintainFourRequests(cityArrayBase);
            return;
        }

        // 월 변경 = 새 청부 갱신 시작으로 간주합니다.
        // 이전 달 포인터를 다음 달에 재사용하지 않고, 게임의 삭제/신규 생성이 끝날 때까지 기다립니다.
        if (currentMonth != s_lastMonth) {
            s_lastMonth = currentMonth;
            s_monthChangeTime = GetTickCount64();
            s_waitingStabilize = true;
            ClearGenerationState();
            return;
        }

        if (s_waitingStabilize) {
            if (GetTickCount64() - s_monthChangeTime < kMonthStabilizeMs)
                return;

            s_waitingStabilize = false;
            ClearGenerationState();
        }

        MaintainFourRequests(cityArrayBase);
    }

} // namespace DX11Base
