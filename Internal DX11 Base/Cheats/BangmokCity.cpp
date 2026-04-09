#include "pch.h"
#include "BangmokCity.h"
#include "Cheats.h"
#include "showlog.h"

#include <psapi.h>

namespace DX11Base {

    // ───────────────────────────────────────────────
    //  방목도시 황폐화 패치
    //  (대도시 전환 코드와 동일한 구조)
    // ───────────────────────────────────────────────

    static bool g_bangmokApplied = false;
    std::atomic_bool g_bangmokThreadRunning{false};

    static const uintptr_t k_panCityOffsets[] = {
        0x10, 0x550, 0x1510, 0x3490, 0x3730, 0x3C70, 0x41B0, 0x4ED0,
        0x5410, 0x5BF0, 0x63D0, 0x6910, 0x7390, 0x7B70, 0x7E10, 0x80B0
    };

    static uintptr_t ResolveBangmokPtr() {
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

    void SetBangmokCity(bool enable) {
        if (g_bangmokThreadRunning.exchange(true)) return;

        HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
            bool enable = *(bool*)param;
            delete (bool*)param;

            uintptr_t pCity = ResolveBangmokPtr();
            if (!pCity) {
                AddLog(u8"[방목도시황폐화] 포인터 해석 실패");
                g_bangmokThreadRunning.store(false);
                return 0;
            }

            DWORD old, tmp;

            if (enable) {
                // 1단계: 도시 타입 128 쓰기
                Sleep(200);
                VirtualProtect((LPVOID)pCity, 0x80000, PAGE_READWRITE, &old);
                for (uintptr_t off : k_panCityOffsets)
                    *(uint8_t*)(pCity + off) = 128;
                VirtualProtect((LPVOID)pCity, 0x80000, old, &tmp);

                // 2단계: 208 고정 + 수치 한도 저하 (PCN/PCS)
                Sleep(200);
                VirtualProtect((LPVOID)pCity, 0x80000, PAGE_READWRITE, &old);
                for (uintptr_t off : k_panCityOffsets) {
                    // 도시 타입 208로 고정
                    *(uint8_t*)(pCity + off) = 208;

                    // 도시 베이스 = 도시 타입 주소 - 0x10
                    uintptr_t cityBase = (pCity + off) - 0x10;
                    if (!IsValidPtr(cityBase, 0xDD)) continue;

                    // 수치 한도 저하 (1500, 3000, 4500...)
                    *(uint16_t*)(cityBase + 0xD0) = 1500;  // 개발
                    *(uint16_t*)(cityBase + 0xD4) = 3000;  // 상업
                    *(uint16_t*)(cityBase + 0xD8) = 4500;  // 방어
                    *(uint16_t*)(cityBase + 0xDC) = 1500;  // 기술
                }
                VirtualProtect((LPVOID)pCity, 0x80000, old, &tmp);

                g_bangmokApplied = true;
                AddLog(u8"[방목도시황폐화] 황폐화 적용 + 수치 한도 저하 완료");

            } else {
                VirtualProtect((LPVOID)pCity, 0x80000, PAGE_READWRITE, &old);
                for (uintptr_t off : k_panCityOffsets) {
                    // 도시 타입 복구 (88 = 상업도시)
                    *(uint8_t*)(pCity + off) = 88;

                    // 수치 한도 복구
                    uintptr_t cityBase = (pCity + off) - 0x10;
                    if (!IsValidPtr(cityBase, 0xDD)) continue;

                    *(uint16_t*)(cityBase + 0xD0) = 2000;  // 개발
                    *(uint16_t*)(cityBase + 0xD4) = 3500;  // 상업
                    *(uint16_t*)(cityBase + 0xD8) = 5000;  // 방어
                    *(uint16_t*)(cityBase + 0xDC) = 2000;  // 기술
                }
                VirtualProtect((LPVOID)pCity, 0x80000, old, &tmp);

                g_bangmokApplied = false;
                AddLog(u8"[방목도시황폐화] 복구 완료");
            }

            g_bangmokThreadRunning.store(false);
            return 0;
        }, new bool(enable), 0, nullptr);

        if (hThread) CloseHandle(hThread);
        else g_bangmokThreadRunning.store(false);
    }

} // namespace DX11Base
