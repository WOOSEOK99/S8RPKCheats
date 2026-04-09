#include "pch.h"
#include "NonggyeongCity.h"
#include "Cheats.h"
#include "showlog.h"

#include <psapi.h>

namespace DX11Base {

    // ───────────────────────────────────────────────
    //  농경도시 버프 패치
    //  (대도시 전환 코드와 동일한 구조)
    // ───────────────────────────────────────────────

    static bool g_nongCityApplied = false;
    std::atomic_bool g_nongCityThreadRunning{false};

    static const uintptr_t k_nongCityOffsets[] = {
        0x7F0, 0xD30, 0xFD0, 0x1270, 0x1A50, 0x1CF0, 0x2A10, 0x39D0, 0x4C30, 0x5E90, 0x6130, 0x6670
    };

    static uintptr_t ResolveNongCityPtr() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) return 0;

        uintptr_t p = *(uintptr_t*)(exeBase + 0x034C8630);
        if (!IsValidPtr(p, 8)) return 0;
        p = *(uintptr_t*)(p + 0x0);
        if (!IsValidPtr(p, 8)) return 0;
        p = *(uintptr_t*)(p + 0x8);
        if (!IsValidPtr(p, 8)) return 0;
        p = *(uintptr_t*)(p + 0x10);
        if (!IsValidPtr(p, 8)) return 0;
        p = *(uintptr_t*)(p + 0x0);
        if (!IsValidPtr(p, 0x80000)) return 0;

        return p;
    }

    void SetNonggyeongCity(bool enable) {
        if (g_nongCityThreadRunning.exchange(true)) return;

        HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
            bool enable = *(bool*)param;
            delete (bool*)param;

            uintptr_t pCity = ResolveNongCityPtr();
            if (!pCity) {
                AddLog(u8"[농경도시버프] 포인터 해석 실패");
                g_nongCityThreadRunning.store(false);
                return 0;
            }

            DWORD old, tmp;

            if (enable) {
                // 1단계: 도시 타입 128 쓰기
                Sleep(200);
                VirtualProtect((LPVOID)pCity, 0x80000, PAGE_READWRITE, &old);
                for (uintptr_t off : k_nongCityOffsets)
                    *(uint8_t*)(pCity + off) = 128;
                VirtualProtect((LPVOID)pCity, 0x80000, old, &tmp);

                // 2단계: 48 고정 + 수치 보정 (NCN/NCS)
                Sleep(200);
                VirtualProtect((LPVOID)pCity, 0x80000, PAGE_READWRITE, &old);
                for (uintptr_t off : k_nongCityOffsets) {
                    // 도시 타입 48로 고정
                    *(uint8_t*)(pCity + off) = 48;

                    // 도시 베이스 = 도시 타입 주소 - 0x10
                    uintptr_t cityBase = (pCity + off) - 0x10;
                    if (!IsValidPtr(cityBase, 0xDD)) continue;

                    // 수치 한도 상승 (최고 단계 기준)
                    *(uint16_t*)(cityBase + 0xD0) = 6500;  // 농촌(PN) 한도 상향
                    *(uint16_t*)(cityBase + 0xD4) = 5000;  // 상가(PS) 한도 상향
                }
                VirtualProtect((LPVOID)pCity, 0x80000, old, &tmp);

                g_nongCityApplied = true;
                AddLog(u8"[농경도시버프] 농경도시 버프 + 수치 보정 완료");

            } else {
                VirtualProtect((LPVOID)pCity, 0x80000, PAGE_READWRITE, &old);
                for (uintptr_t off : k_nongCityOffsets) {
                    // 도시 타입 복구 (88 = 상업도시)
                    *(uint8_t*)(pCity + off) = 88;

                    // 수치 복구
                    uintptr_t cityBase = (pCity + off) - 0x10;
                    if (!IsValidPtr(cityBase, 0xDD)) continue;

                    *(uint16_t*)(cityBase + 0xD0) = 6000;  // 농촌 원복
                    *(uint16_t*)(cityBase + 0xD4) = 4500;  // 상가 원복
                }
                VirtualProtect((LPVOID)pCity, 0x80000, old, &tmp);

                g_nongCityApplied = false;
                AddLog(u8"[농경도시버프] 복구 완료");
            }

            g_nongCityThreadRunning.store(false);
            return 0;
        }, new bool(enable), 0, nullptr);

        if (hThread) CloseHandle(hThread);
        else g_nongCityThreadRunning.store(false);
    }

} // namespace DX11Base
