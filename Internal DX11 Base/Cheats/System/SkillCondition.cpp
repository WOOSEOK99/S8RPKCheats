#include "SkillCondition.h"
#include "../../pch.h"
#include "../../showlog.h"
#include "../../Cheats.h"
#include <windows.h>

namespace DX11Base {
    int8_t GetManbyeong() {
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
        if (!IsValidPtr(p, 0x4BC733)) return 0;

        return *(int8_t*)(p + 0x4BC732);
    }

    void SetManbyeong(int8_t value) {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) return;

        uintptr_t p = *(uintptr_t*)(exeBase + 0x034C8630);
        if (!IsValidPtr(p, 8)) return;
        p = *(uintptr_t*)(p + 0x0);
        if (!IsValidPtr(p, 8)) return;
        p = *(uintptr_t*)(p + 0x8);
        if (!IsValidPtr(p, 8)) return;
        p = *(uintptr_t*)(p + 0x10);
        if (!IsValidPtr(p, 8)) return;
        p = *(uintptr_t*)(p + 0x0);
        if (!IsValidPtr(p, 0x4BC733)) return;

        DWORD old, tmp;
        VirtualProtect((LPVOID)(p + 0x4BC732), 1, PAGE_READWRITE, &old);
        *(int8_t*)(p + 0x4BC732) = value;
        VirtualProtect((LPVOID)(p + 0x4BC732), 1, old, &tmp);

        AddLog(u8"[만병조건] %d 로 설정", value);
    }

    void ApplySkillCondition(bool enable) {
        if (enable) {
            SetManbyeong(0);
        } else {
            SetManbyeong(2);
        }
    }

    void SetYumokgibeong(int8_t value) {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) return;

        uintptr_t p = *(uintptr_t*)(exeBase + 0x034C8630);
        if (!IsValidPtr(p, 8)) return;
        p = *(uintptr_t*)(p + 0x0);
        if (!IsValidPtr(p, 8)) return;
        p = *(uintptr_t*)(p + 0x8);
        if (!IsValidPtr(p, 8)) return;
        p = *(uintptr_t*)(p + 0x10);
        if (!IsValidPtr(p, 8)) return;
        p = *(uintptr_t*)(p + 0x0);
        if (!IsValidPtr(p, 0x4BC7DB)) return;

        DWORD old, tmp;
        VirtualProtect((LPVOID)(p + 0x4BC7DA), 1, PAGE_READWRITE, &old);
        *(int8_t*)(p + 0x4BC7DA) = value;
        VirtualProtect((LPVOID)(p + 0x4BC7DA), 1, old, &tmp);

        AddLog(u8"[유목기병조건] %d 로 설정", value);
    }

    void ApplyYumokCondition(bool enable) {
        if (enable) {
            SetYumokgibeong(0);
        } else {
            SetYumokgibeong(2);
        }
    }

    void SetSangbyeong(int8_t value) {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) return;

        uintptr_t p = *(uintptr_t*)(exeBase + 0x034C8630);
        if (!IsValidPtr(p, 8)) return;
        p = *(uintptr_t*)(p + 0x0);
        if (!IsValidPtr(p, 8)) return;
        p = *(uintptr_t*)(p + 0x8);
        if (!IsValidPtr(p, 8)) return;
        p = *(uintptr_t*)(p + 0x10);
        if (!IsValidPtr(p, 8)) return;
        p = *(uintptr_t*)(p + 0x0);
        if (!IsValidPtr(p, 0x4BC95F)) return;

        DWORD old, tmp;
        VirtualProtect((LPVOID)(p + 0x4BC95E), 1, PAGE_READWRITE, &old);
        *(int8_t*)(p + 0x4BC95E) = value;
        VirtualProtect((LPVOID)(p + 0x4BC95E), 1, old, &tmp);

        AddLog(u8"[상병조건] %d 로 설정", value);
    }

    void ApplySangbyeongCondition(bool enable) {
        if (enable) {
            SetSangbyeong(0);
        } else {
            SetSangbyeong(1);
        }
    }
}
