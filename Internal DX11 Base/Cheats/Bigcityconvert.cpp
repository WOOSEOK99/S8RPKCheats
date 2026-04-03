#include "pch.h"
#include "Bigcityconvert.h"
#include "Cheats.h"
#include "showlog.h"

#include <psapi.h>

namespace DX11Base {

    // ───────────────────────────────────────────────
    //  대도시 → 기술도시 전환 패치
    //  포인터 체인: SAN8RPK.exe+034C8630 → +0 → +8 → +10 → +0
    //
    //  도시 타입 오프셋 기준:
    //  - 도시 타입 주소 - 0x10 = 도시 베이스
    //  - 도시 베이스 + 0xD0 = 개발 최대치
    //  - 도시 베이스 + 0xD4 = 상업 최대치
    //  - 도시 베이스 + 0xD8 = 방어 최대치
    //  - 도시 베이스 + 0xDC = 기술 최대치
    // ───────────────────────────────────────────────

    static bool g_bigCityApplied       = false;
    bool        g_bigCityThreadRunning = false;

    // 기술도시로 전환할 도시 타입 오프셋 (ResolveBigCityPtr 기준)
    static const uintptr_t k_bigCityOffsets[] = {
        0xA90, 0x24D0, 0x2CB0, 0x31F0, 0x4450, 0x56B0, 0x70F0
    };

    static uintptr_t ResolveBigCityPtr() {
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

    void SetBigCityConvert(bool enable) {
        if (g_bigCityThreadRunning) return;
        g_bigCityThreadRunning = true;

        HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
            bool enable = *(bool*)param;
            delete (bool*)param;

            uintptr_t pCity = ResolveBigCityPtr();
            if (!pCity) {
                AddLog(u8"[대도시전환] 포인터 해석 실패");
                g_bigCityThreadRunning = false;
                return 0;
            }

            DWORD old, tmp;

            if (enable) {
                // 1단계: 도시 타입 168 쓰기
                Sleep(200);
                VirtualProtect((LPVOID)pCity, 0x80000, PAGE_READWRITE, &old);
                for (uintptr_t off : k_bigCityOffsets)
                    *(uint8_t*)(pCity + off) = 168;
                VirtualProtect((LPVOID)pCity, 0x80000, old, &tmp);

                // 2단계: 128 고정 + 개발/상업/방어/기술 수치 수정
                Sleep(200);
                VirtualProtect((LPVOID)pCity, 0x80000, PAGE_READWRITE, &old);
                for (uintptr_t off : k_bigCityOffsets) {
                    // 도시 타입 128로 고정
                    *(uint8_t*)(pCity + off) = 128;

                    // 도시 베이스 = 도시 타입 주소 - 0x10
                    uintptr_t cityBase = (pCity + off) - 0x10;
                    if (!IsValidPtr(cityBase, 0xDD)) continue;

                    *(uint16_t*)(cityBase + 0xD0) = 9000;  // 개발
                    *(uint16_t*)(cityBase + 0xD4) = 12000;  // 상업
                    *(uint16_t*)(cityBase + 0xD8) = 9000;  // 방어
                    *(uint16_t*)(cityBase + 0xDC) = 4000;  // 기술
                }
                VirtualProtect((LPVOID)pCity, 0x80000, old, &tmp);

                g_bigCityApplied = true;
                AddLog(u8"[대도시전환] 기술도시 전환 + 수치 최대화 완료");

            } else {
                VirtualProtect((LPVOID)pCity, 0x80000, PAGE_READWRITE, &old);
                for (uintptr_t off : k_bigCityOffsets) {
                    // 도시 타입 복구 (88 = 상업도시)
                    *(uint8_t*)(pCity + off) = 88;

                    // 수치 복구
                    uintptr_t cityBase = (pCity + off) - 0x10;
                    if (!IsValidPtr(cityBase, 0xDD)) continue;

                    *(uint16_t*)(cityBase + 0xD0) = 4500;  // 개발
                    *(uint16_t*)(cityBase + 0xD4) = 6000;  // 상업
                    *(uint16_t*)(cityBase + 0xD8) = 4500;  // 방어
                    *(uint16_t*)(cityBase + 0xDC) = 2000;  // 기술
                }
                VirtualProtect((LPVOID)pCity, 0x80000, old, &tmp);

                g_bigCityApplied = false;
                AddLog(u8"[대도시전환] 복구 완료");
            }

            g_bigCityThreadRunning = false;
            return 0;
        }, new bool(enable), 0, nullptr);

        if (hThread) CloseHandle(hThread);
    }

} // namespace DX11Base
