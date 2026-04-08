#include "pch.h"
#include "FactionLordBonus.h"
#include "Cheats.h"
#include "showlog.h"
#include <psapi.h>
#include <unordered_map>

namespace DX11Base {

    // ───────────────────────────────────────────────
    //  세력 군주 보너스 자동 배정
    //  5초마다 실행
    //  관작 등급에 따라 보너스 포인터 배정
    // ───────────────────────────────────────────────

    bool   g_factionLordBonusEnabled = false;
    static bool   g_factionLordBonusRunning = false;
    static HANDLE g_factionLordBonusThread  = nullptr;

    static std::unordered_map<uintptr_t, bool> g_prevAssigned;
    static bool g_titleNamesApplied = false;

    // 상수
    static const int      FACTION_COUNT = 120;
    static const uintptr_t FACTION_SIZE = 0x998;

    static const uintptr_t OFF_FACTION_LORD_PTR = 0xC6908;
    static const uintptr_t OFF_FACTION_EXIST    = 0xC690D;
    static const uintptr_t OFF_FACTION_TITLEPTR = 0xC6A20;
    static const uintptr_t OFF_FACTION_WANDER   = 0xC6A28;

    static const uintptr_t OFF_LORD_BONUS  = 0x330;
    static const uintptr_t OFF_TITLE_RANK  = 0x0E;

    static const uintptr_t OFF_EMPEROR = 0x4EC6D8;
    static const uintptr_t OFF_KING    = 0x4EC6F0;
    static const uintptr_t OFF_GONG    = 0x4EC708;
    static const uintptr_t OFF_JUMOK   = 0x4EC720;
    static const uintptr_t OFF_GUNJU   = 0x4EC738;

    static const uintptr_t OFF_TITLE_NAME_PATCH = 0x197A774;

    static uintptr_t ResolveRoot() {
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
        if (!IsValidPtr(p, 8)) return 0;

        return p;
    }

    // forward decls (used by title name patch helpers)
    static void WriteBytes(uintptr_t addr, const uint8_t* data, size_t size);

    static void WriteBytes(uintptr_t addr, const uint8_t* data, size_t size) {
        if (!IsValidPtr(addr, size)) return;
        DWORD old, tmp;
        VirtualProtect((LPVOID)addr, size, PAGE_READWRITE, &old);
        memcpy((void*)addr, data, size);
        VirtualProtect((LPVOID)addr, size, old, &tmp);
    }

    static void WriteQword(uintptr_t addr, uint64_t val) {
        if (!IsValidPtr(addr, 8)) return;
        DWORD old, tmp;
        VirtualProtect((LPVOID)addr, 8, PAGE_READWRITE, &old);
        *(uint64_t*)addr = val;
        VirtualProtect((LPVOID)addr, 8, old, &tmp);
    }

    static void ApplyTitleNames(uintptr_t root) {
        // 스크립트의 writeBytes(root+197A774, ...) 동일
        static const uint8_t kNames[48] = {
            0x69,0xD6,0x1C,0xC8,0x00,0x00,0x00,0x00,
            0x55,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,
            0xF5,0xAC,0x00,0x00,0x00,0x00,0x00,0x00,
            0xFC,0xC8,0xA9,0xBA,0x00,0x00,0x00,0x00,
            0x70,0xAD,0xFC,0xC8,0x00,0x00,0x00,0x00,
            0x9C,0xCC,0xF5,0xAC,0xA5,0xC7,0x70,0xAD
        };
        WriteBytes(root + OFF_TITLE_NAME_PATCH, kNames, sizeof(kNames));
    }

    static void ClearTitleNames(uintptr_t root) {
        static const uint8_t kZeros[48] = {};
        WriteBytes(root + OFF_TITLE_NAME_PATCH, kZeros, sizeof(kZeros));
    }

    static void ApplyBonusTable(uintptr_t root) {
        uintptr_t base   = root + OFF_EMPEROR;
        uintptr_t stride = 24;

        static const uint8_t patches[5][15] = {
            // 치트엔진 스크립트 patches[] 동일 (OFF_EMPEROR 기준 +9에 15바이트)
            {0x54,0x00,0x14,0x88,0x13,0x05,0x05,0x05,0x05,0x05,0x00,0x01,0x03,0x01,0x00},
            {0x56,0x00,0x14,0xB8,0x0B,0x04,0x04,0x04,0x04,0x04,0x00,0x01,0x03,0x01,0x00},
            {0x58,0x00,0x14,0xD0,0x07,0x03,0x03,0x03,0x03,0x03,0x00,0x01,0x03,0x01,0x00},
            {0x5A,0x00,0x14,0xE8,0x03,0x02,0x02,0x02,0x02,0x02,0x00,0x01,0x03,0x01,0x00},
            {0x5C,0x00,0x14,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x00,0x01,0x03,0x01,0x00},
        };

        for (int i = 0; i < 5; i++)
            WriteBytes(base + i * stride + 9, patches[i], 15);
    }

    static void ClearBonusTable(uintptr_t root) {
        uintptr_t base   = root + OFF_EMPEROR;
        uintptr_t stride = 24;
        static const uint8_t zeros[15] = {};

        for (int i = 0; i < 5; i++)
            WriteBytes(base + i * stride + 9, zeros, 15);
    }

    static void RunOnce() {
        uintptr_t root = ResolveRoot();
        if (!root) return;

        // 관작 이름 패치 1회 적용 (root가 준비된 시점)
        if (!g_titleNamesApplied) {
            ApplyTitleNames(root);
            g_titleNamesApplied = true;
        }

        // 1. 보너스 테이블 적용
        ApplyBonusTable(root);

        uintptr_t ptrEmperor = root + OFF_EMPEROR;
        uintptr_t ptrKing    = root + OFF_KING;
        uintptr_t ptrGong    = root + OFF_GONG;
        uintptr_t ptrJumok   = root + OFF_JUMOK;
        uintptr_t ptrGunju   = root + OFF_GUNJU;

        std::unordered_map<uintptr_t, bool> bonusSet = {
            {ptrEmperor, true},
            {ptrKing,    true},
            {ptrGong,    true},
            {ptrJumok,   true},
            {ptrGunju,   true},
        };

        std::unordered_map<uintptr_t, bool> currentAssigned;

        // 2. 세력 순회
        for (int i = 0; i < FACTION_COUNT; i++) {
            uintptr_t existAddr    = root + OFF_FACTION_EXIST    + i * FACTION_SIZE;
            uintptr_t wanderAddr   = root + OFF_FACTION_WANDER   + i * FACTION_SIZE;
            uintptr_t lordPtrAddr  = root + OFF_FACTION_LORD_PTR + i * FACTION_SIZE;
            uintptr_t titlePtrAddr = root + OFF_FACTION_TITLEPTR + i * FACTION_SIZE;

            if (!IsValidPtr(existAddr, 1)) continue;

            uint8_t  exist   = *(uint8_t*)existAddr;
            uint8_t  wander  = IsValidPtr(wanderAddr, 1) ? *(uint8_t*)wanderAddr : 1;
            uint64_t lordPtr = IsValidPtr(lordPtrAddr, 8) ? *(uint64_t*)lordPtrAddr : 0;

            if (exist < 1 || wander != 0 || lordPtr == 0) continue;
            if (!IsValidPtr(lordPtr, OFF_LORD_BONUS + 8)) continue;

            uintptr_t bonusAddr = lordPtr + OFF_LORD_BONUS;
            uint64_t  titlePtr  = IsValidPtr(titlePtrAddr, 8) ? *(uint64_t*)titlePtrAddr : 0;

            uintptr_t target = ptrGunju;

            if (titlePtr && IsValidPtr(titlePtr + OFF_TITLE_RANK, 1)) {
                uint8_t rank = *(uint8_t*)(titlePtr + OFF_TITLE_RANK);
                if      (rank == 1) target = ptrEmperor;
                else if (rank == 2) target = ptrKing;
                else if (rank == 3) target = ptrGong;
                else if (rank == 4) target = ptrJumok;
                else                target = ptrGunju;
            }

            currentAssigned[bonusAddr] = true;

            uint64_t current = IsValidPtr(bonusAddr, 8) ? *(uint64_t*)bonusAddr : 0;
            if (current != target)
                WriteQword(bonusAddr, target);
        }

        // 3. 이전에 배정됐지만 지금은 빠진 대상 정리
        for (auto& kv : g_prevAssigned) {
            uintptr_t bonusAddr = kv.first;
            if (currentAssigned.count(bonusAddr)) continue;
            if (!IsValidPtr(bonusAddr, 8)) continue;
            uint64_t current = *(uint64_t*)bonusAddr;
            if (bonusSet.count(current))
                WriteQword(bonusAddr, 0);
        }

        g_prevAssigned = currentAssigned;
    }

    static void ClearAll(uintptr_t root) {
        uintptr_t ptrEmperor = root + OFF_EMPEROR;
        uintptr_t ptrKing    = root + OFF_KING;
        uintptr_t ptrGong    = root + OFF_GONG;
        uintptr_t ptrJumok   = root + OFF_JUMOK;
        uintptr_t ptrGunju   = root + OFF_GUNJU;

        std::unordered_map<uintptr_t, bool> bonusSet = {
            {ptrEmperor, true}, {ptrKing, true}, {ptrGong, true},
            {ptrJumok,   true}, {ptrGunju, true},
        };

        for (auto& kv : g_prevAssigned) {
            uintptr_t bonusAddr = kv.first;
            if (!IsValidPtr(bonusAddr, 8)) continue;
            uint64_t current = *(uint64_t*)bonusAddr;
            if (bonusSet.count(current))
                WriteQword(bonusAddr, 0);
        }

        g_prevAssigned.clear();
        ClearBonusTable(root);
        ClearTitleNames(root);
        g_titleNamesApplied = false;
    }

    static DWORD WINAPI FactionLordBonusThread(LPVOID) {
        // 즉시 1회 실행
        RunOnce();

        while (g_factionLordBonusEnabled) {
            Sleep(5000);
            if (!g_factionLordBonusEnabled) break;
            RunOnce();
        }

        g_factionLordBonusRunning = false;
        return 0;
    }

    void SetFactionLordBonus(bool enable) {
        if (enable) {
            if (g_factionLordBonusEnabled) return;

            g_factionLordBonusEnabled = true;
            g_factionLordBonusRunning = true;
            g_titleNamesApplied = false;

            g_factionLordBonusThread = CreateThread(nullptr, 0,
                FactionLordBonusThread, nullptr, 0, nullptr);

            if (!g_factionLordBonusThread) {
                g_factionLordBonusEnabled = false;
                g_factionLordBonusRunning = false;
                AddLog(u8"[군주보너스] 스레드 생성 실패");
            } else {
                AddLog(u8"[군주보너스] 활성화");
            }

        } else {
            g_factionLordBonusEnabled = false;

            if (g_factionLordBonusThread) {
                WaitForSingleObject(g_factionLordBonusThread, 6000);
                CloseHandle(g_factionLordBonusThread);
                g_factionLordBonusThread = nullptr;
            }

            uintptr_t root = ResolveRoot();
            if (root) ClearAll(root);
            else g_titleNamesApplied = false;

            AddLog(u8"[군주보너스] 비활성화");
        }
    }

} // namespace DX11Base
