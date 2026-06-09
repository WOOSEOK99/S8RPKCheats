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
 * 동작 방식:
 *   1. 달이 바뀌면 2.5초 대기 (게임이 만료 청부 삭제 및 신규 청부 생성할 시간)
 *   2. 대기 후, 도시별 청부 슬롯 검사:
 *      - 유효한 포인터(>0x10000)가 있으면 -> 백업 갱신
 *      - 슬롯이 0이면 -> 기존 백업이 있다면 복구 (덮어쓰기)
 *   3. 달이 바뀌었는데 신규 청부가 없으면 -> 기존 백업으로 계속 복구
 *
 * 포인터 체인 (명품 창과 동일):
 *   exe + 0x34C8630 -> p1
 *   p1              -> p2
 *   p2              -> cityArrayBase
 */

#include "TavernMonitor.h"
#include "../../MenuState.h"
#include "../System/SystemMonth.h"
#include <windows.h>
#include <cstdint>

namespace DX11Base {

    // -- 안전한 메모리 접근 헬퍼 (SEH 기반) --------------------------
    static bool TM_ReadPtr(uintptr_t addr, uintptr_t* out) {
        __try { *out = *(uintptr_t*)addr; return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool TM_WritePtrSafe(uintptr_t addr, uintptr_t value) {
        DWORD old = 0;
        if (!VirtualProtect((LPVOID)addr, sizeof(uintptr_t), PAGE_READWRITE, &old)) return false;
        bool ok = false;
        __try { *(uintptr_t*)addr = value; ok = true; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        DWORD dummy = 0;
        VirtualProtect((LPVOID)addr, sizeof(uintptr_t), old, &dummy);
        return ok;
    }
    // -----------------------------------------------------------------

    struct TavernRequest {
        uintptr_t name; // 의뢰명 포인터
        uintptr_t data; // 청부 데이터 포인터
    };

    // 각 도시(최대 51개)의 4개 슬롯 백업
    static TavernRequest g_TavernBackups[51][4] = {};

    // 월 변경 감지 및 안정화 대기용
    static uint8_t   s_lastMonth        = 0xFF;
    static ULONGLONG s_monthChangeTime  = 0;
    static bool      s_waitingStabilize = false;

    // 도시 배열 베이스 주소 반환
    static uintptr_t ResolveCityBase() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (exeBase <= 0x10000) return 0;
        uintptr_t p1 = 0, p2 = 0, cityBase = 0;
        if (!TM_ReadPtr(exeBase + 0x34C8630, &p1) || p1 <= 0x10000) return 0;
        if (!TM_ReadPtr(p1, &p2) || p2 <= 0x10000)                  return 0;
        if (!TM_ReadPtr(p2, &cityBase) || cityBase <= 0x10000)       return 0;
        return cityBase;
    }

    void UpdateTavernRequests() {
        if (!bInfiniteTavernRequests) return;

        uintptr_t cityArrayBase = ResolveCityBase();
        if (cityArrayBase <= 0x10000) return;

        uint8_t currentMonth = GetSystemMonthValue();

        // --- 1. 달 변경 감지 -----------------------------------------
        if (currentMonth >= 1 && currentMonth <= 12 && currentMonth != s_lastMonth) {
            s_lastMonth        = currentMonth;
            s_monthChangeTime  = GetTickCount64();
            s_waitingStabilize = true;
            return;
        }

        // --- 2. 안정화 대기 (2.5초) ----------------------------------
        if (s_waitingStabilize) {
            if (GetTickCount64() - s_monthChangeTime < 2500) return;
            s_waitingStabilize = false;

            const uintptr_t nameOff[4] = {0x178, 0x1A0, 0x1C8, 0x1F0};
            const uintptr_t dataOff[4] = {0x180, 0x1A8, 0x1D0, 0x1F8};

            for (int c = 0; c < 51; c++) {
                uintptr_t cityAddr = cityArrayBase + (uintptr_t)c * 0x2A0;
                for (int s = 0; s < 4; s++) {
                    uintptr_t curName = 0, curData = 0;
                    TM_ReadPtr(cityAddr + nameOff[s], &curName);
                    TM_ReadPtr(cityAddr + dataOff[s], &curData);
                    if (curName > 0x10000 && curData > 0x10000) {
                        // 신규 유효 청부 -> 백업 갱신
                        g_TavernBackups[c][s].name = curName;
                        g_TavernBackups[c][s].data = curData;
                    }
                    // 슬롯이 0이면 기존 백업 유지 (아래 루프에서 복구)
                }
            }
        }

        // --- 3. 일반 복구 루프 ----------------------------------------
        const uintptr_t nameOff[4] = {0x178, 0x1A0, 0x1C8, 0x1F0};
        const uintptr_t dataOff[4] = {0x180, 0x1A8, 0x1D0, 0x1F8};

        for (int c = 0; c < 51; c++) {
            uintptr_t cityAddr = cityArrayBase + (uintptr_t)c * 0x2A0;
            for (int s = 0; s < 4; s++) {
                uintptr_t curName = 0, curData = 0;
                TM_ReadPtr(cityAddr + nameOff[s], &curName);
                TM_ReadPtr(cityAddr + dataOff[s], &curData);

                if (curName > 0x10000 && curData > 0x10000) {
                    // 유효한 청부 -> 최신 백업 유지
                    g_TavernBackups[c][s].name = curName;
                    g_TavernBackups[c][s].data = curData;
                } else if (curName == 0 && curData == 0) {
                    // 비어있으면 백업으로 복구
                    if (g_TavernBackups[c][s].name > 0x10000 &&
                        g_TavernBackups[c][s].data > 0x10000) {
                        TM_WritePtrSafe(cityAddr + nameOff[s], g_TavernBackups[c][s].name);
                        TM_WritePtrSafe(cityAddr + dataOff[s], g_TavernBackups[c][s].data);
                    }
                }
            }
        }
    }

} // namespace DX11Base