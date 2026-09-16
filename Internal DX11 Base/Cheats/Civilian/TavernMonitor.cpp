/*
 * TavernMonitor.cpp
 * -----------------
 * 주점 청부(Request) 무한 유지 모니터
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
 * 안전성 원칙:
 *   1. 백업은 같은 도시/같은 슬롯에만 복구합니다.
 *   2. 다른 도시의 청부 포인터를 빈 슬롯에 복사하지 않습니다.
 *   3. 월이 변경되면 이전 달의 포인터 백업은 즉시 폐기합니다.
 *   4. 세이브 로드 등으로 도시 배열 베이스가 바뀌면 백업을 모두 폐기합니다.
 *   5. 이름/데이터 포인터가 둘 다 유효하고 읽을 수 있을 때만 백업/복구합니다.
 *   6. 슬롯 두 포인터는 16바이트 단위로 함께 쓰고 실패 시 원래 값으로 되돌립니다.
 *
 * 이 방식은 이전 구현처럼 이전 달의 만료된 청부를 억지로 되살리지는 않습니다.
 * 대신 현재 달 안에서 유효한 청부가 일시적으로 비워졌을 때만 자기 슬롯으로 복구하여
 * 런타임 포인터 오염 가능성을 낮춥니다.
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

        static TavernRequest s_backups[kCityCount][kSlotCount] = {};
        static uint8_t s_lastMonth = 0xFF;
        static ULONGLONG s_monthChangeTime = 0;
        static bool s_waitingStabilize = false;
        static bool s_wasEnabled = false;
        static uintptr_t s_lastCityArrayBase = 0;

        static void ClearBackups() {
            std::memset(s_backups, 0, sizeof(s_backups));
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
            return req.name > 0x10000 && req.data > 0x10000 &&
                   IsReadableAddress(req.name, 1) && IsReadableAddress(req.data, 1);
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

            // 현재 구조상 두 포인터는 연속 16바이트입니다. 예상 구조가 바뀌면 쓰지 않습니다.
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

        static void CaptureCurrentRequests(uintptr_t cityArrayBase) {
            ClearBackups();

            for (int c = 0; c < kCityCount; ++c) {
                uintptr_t cityAddr = cityArrayBase + (uintptr_t)c * kCityStride;
                for (int s = 0; s < kSlotCount; ++s) {
                    TavernRequest req{};
                    if (ReadRequestPair(cityAddr, s, &req) && IsUsableRequest(req)) {
                        s_backups[c][s] = req;
                    }
                }
            }
        }

        static void ResetMonitorState() {
            ClearBackups();
            s_lastMonth = 0xFF;
            s_monthChangeTime = 0;
            s_waitingStabilize = false;
            s_lastCityArrayBase = 0;
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

        // 세이브 로드/시나리오 전환 등으로 도시 배열이 재배치되면 이전 세션 포인터는 절대 재사용하지 않습니다.
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

        // 최초 활성화/세션 진입 시 현재 달의 유효한 청부만 백업합니다.
        if (s_lastMonth == 0xFF) {
            s_lastMonth = currentMonth;
            CaptureCurrentRequests(cityArrayBase);
            return;
        }

        // 월 변경 시 이전 달 백업은 즉시 폐기합니다.
        // 게임이 만료 청부 삭제/신규 청부 생성 작업을 끝낼 때까지는 어떤 포인터도 복구하지 않습니다.
        if (currentMonth != s_lastMonth) {
            s_lastMonth = currentMonth;
            s_monthChangeTime = GetTickCount64();
            s_waitingStabilize = true;
            ClearBackups();
            return;
        }

        if (s_waitingStabilize) {
            if (GetTickCount64() - s_monthChangeTime < kMonthStabilizeMs)
                return;

            s_waitingStabilize = false;
            CaptureCurrentRequests(cityArrayBase);
            return;
        }

        // 같은 달 안에서만 자기 도시/자기 슬롯의 마지막 유효 청부를 유지합니다.
        for (int c = 0; c < kCityCount; ++c) {
            uintptr_t cityAddr = cityArrayBase + (uintptr_t)c * kCityStride;

            for (int s = 0; s < kSlotCount; ++s) {
                TavernRequest current{};
                if (!ReadRequestPair(cityAddr, s, &current)) {
                    s_backups[c][s] = {};
                    continue;
                }

                const bool bothEmpty = (current.name == 0 && current.data == 0);
                const bool bothPresent = (current.name > 0x10000 && current.data > 0x10000);

                if (bothPresent) {
                    if (IsUsableRequest(current)) {
                        // 게임이 정상적으로 새 청부를 넣었으면 항상 최신 값으로 교체합니다.
                        s_backups[c][s] = current;
                    } else {
                        // 주소 숫자만 남아 있고 대상 메모리가 이미 유효하지 않다면 복구 후보에서 제외합니다.
                        s_backups[c][s] = {};
                    }
                    continue;
                }

                if (bothEmpty) {
                    TavernRequest backup = s_backups[c][s];
                    if (IsUsableRequest(backup)) {
                        // 다른 도시의 포인터를 빌려오지 않고 오직 같은 슬롯의 백업만 복구합니다.
                        if (!WriteRequestPair(cityAddr, s, backup)) {
                            s_backups[c][s] = {};
                        }
                    } else {
                        s_backups[c][s] = {};
                    }
                    continue;
                }

                // name/data 중 하나만 남은 비정상 상태에서는 억지로 덮어쓰지 않습니다.
                // 저장/월전환 도중의 과도 상태일 수 있으므로 백업도 폐기합니다.
                s_backups[c][s] = {};
            }
        }
    }

} // namespace DX11Base
