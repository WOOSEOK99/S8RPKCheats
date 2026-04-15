#include "../../pch.h"
#include "SangeopCity.h"
#include "../../Cheats.h"
#include "../../showlog.h"

#include <psapi.h>

namespace DX11Base {

    // ───────────────────────────────────────────────
    //  상업도시 버프 패치
    //  (대도시 전환 코드와 동일한 구조)
    // ───────────────────────────────────────────────

    static bool g_sagCityApplied = false;
    std::atomic_bool g_sagCityThreadRunning{false};

    static const uintptr_t k_sagCityOffsets[] = {
        0xA90, 0x24D0, 0x2CB0, 0x31F0, 0x4450, 0x56B0, 0x70F0, 
        0x1F90, 0x2230, 0x2770, 0x5170, 0x6BB0, 0x6E50, 0x7630, 0x8350
    };

    static uintptr_t ResolveSagCityPtr() {
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

    void SetSangeopCity(bool enable) {
        if (g_sagCityThreadRunning.exchange(true)) return;

        HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
            bool enable = *(bool*)param;
            delete (bool*)param;

            uintptr_t pCity = ResolveSagCityPtr();
            if (!pCity) {
                AddLog(u8"[상업도시버프] 포인터 해석 실패");
                g_sagCityThreadRunning.store(false);
                return 0;
            }

            DWORD old, tmp;

            if (enable) {
                // 1단계: 도시 타입 168 쓰기 (기술도시(128)인 경우 제외)
                Sleep(200);
                VirtualProtect((LPVOID)pCity, 0x80000, PAGE_READWRITE, &old);
                for (uintptr_t off : k_sagCityOffsets) {
                    uint8_t cur = *(uint8_t*)(pCity + off);
                    if (cur != 128) {
                        *(uint8_t*)(pCity + off) = 168;
                    }
                }
                VirtualProtect((LPVOID)pCity, 0x80000, old, &tmp);

                // 2단계: 88 고정 + 수치 보정 (SCN/SCS)
                Sleep(200);
                VirtualProtect((LPVOID)pCity, 0x80000, PAGE_READWRITE, &old);
                for (uintptr_t off : k_sagCityOffsets) {
                    uint8_t cur = *(uint8_t*)(pCity + off);
                    // 168로 변환된 도시(또는 원래 상업도시)만 88로 고정
                    if (cur == 168 || cur == 88) {
                        *(uint8_t*)(pCity + off) = 88;
                    }

                    // 도시 베이스 = 도시 타입 주소 - 0x10
                    uintptr_t cityBase = (pCity + off) - 0x10;
                    if (!IsValidPtr(cityBase, 0xDD)) continue;

                    // 수치 한도 상승 (SCN/SCS)
                    *(uint16_t*)(cityBase + 0xD0) = 5000;  // 농촌(PN/SCN) 한도 상향
                    *(uint16_t*)(cityBase + 0xD4) = 6500;  // 상가(PS/SCS) 한도 상향
                }
                VirtualProtect((LPVOID)pCity, 0x80000, old, &tmp);

                g_sagCityApplied = true;
                AddLog(u8"[상업도시버프] 상업도시 버프 + 수치 보정 완료");

            } else {
                VirtualProtect((LPVOID)pCity, 0x80000, PAGE_READWRITE, &old);
                for (uintptr_t off : k_sagCityOffsets) {
                    // 수치 복구
                    uintptr_t cityBase = (pCity + off) - 0x10;
                    if (!IsValidPtr(cityBase, 0xDD)) continue;

                    *(uint16_t*)(cityBase + 0xD0) = 4500;  // 농촌 원복
                    *(uint16_t*)(cityBase + 0xD4) = 6000;  // 상가 원복
                }
                VirtualProtect((LPVOID)pCity, 0x80000, old, &tmp);

                g_sagCityApplied = false;
                AddLog(u8"[상업도시버프] 복구 완료");
            }

            g_sagCityThreadRunning.store(false);
            return 0;
        }, new bool(enable), 0, nullptr);

        if (hThread) CloseHandle(hThread);
        else g_sagCityThreadRunning.store(false);
    }

} // namespace DX11Base
