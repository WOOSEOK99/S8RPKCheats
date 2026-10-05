#include "../../pch.h"

#include "Battleunitcapture.h"
#include "Catapult.h"
#include "Celestia.h"
#include "../../Cheats.h"
#include "../Social/Fastrelationship.h"
#include "../Social/Infinitetalk.h"
#include "../Social/InstantLoveCave.h"
#include "../Social/Loyaltycave.h"
#include "../Social/Resonancecave.h"
#include "Selfheal.h"
#include "../../showlog.h"

#include <psapi.h>
#include <string>
#include <vector>

namespace DX11Base {
    // ───────────────────────────────────────────────
    //  전투 선택 유닛 베이스 주소 캡처
    //  원본: movzx eax, byte ptr [rax+0x80]  (7바이트)
    //  훅 위치: INJECT_BATT_SELECTED_UNIT+01
    //  패턴: 08 0F B6 80 80 00 00 00 (+1 위치에서 훅)
    // ───────────────────────────────────────────────

    // 캡처된 유닛 베이스 주소 (전역)
    uintptr_t g_battleUnitAddr1 = 0;
    uintptr_t g_battleUnitAddr2 = 0;

    static uintptr_t g_battUnitHookAddr = 0;
    static uint8_t g_battUnitOriginal[7] = {};
    static uintptr_t g_battUnitCaveAddr = 0;
    static bool g_battUnitApplied = false;
    std::atomic_bool g_battUnitThreadRunning{false};

    namespace {
        constexpr uintptr_t kBattUnitV0860Rva = 0x1E3A5D4;
        constexpr uint8_t kBattUnitV0860Bytes[7] = {0x0F, 0xB6, 0x80, 0x80, 0x00, 0x00, 0x00};

        bool ResolveKnownBattUnitHook(uintptr_t exeBase, uintptr_t searchEnd, uintptr_t* outHook) {
            if (!outHook || searchEnd <= exeBase)
                return false;
            const uintptr_t imageSize = searchEnd - exeBase;
            if (imageSize < kBattUnitV0860Rva + sizeof(kBattUnitV0860Bytes))
                return false;

            const uintptr_t candidate = exeBase + kBattUnitV0860Rva;
            if (!IsValidPtr(candidate, sizeof(kBattUnitV0860Bytes)))
                return false;
            if (memcmp((const void*)candidate, kBattUnitV0860Bytes,
                       sizeof(kBattUnitV0860Bytes)) != 0)
                return false;

            *outHook = candidate;
            return true;
        }
    }

    static bool InstallBattUnitCave(uintptr_t hookAddr) {
        g_battUnitCaveAddr = AllocNear(hookAddr, 256);
        if (!g_battUnitCaveAddr)
            return false;

        uint8_t *cave = (uint8_t *)g_battUnitCaveAddr;
        int idx = 0;

        // push r15  (41 57)
        cave[idx++] = 0x41;
        cave[idx++] = 0x57;

        // cmp qword ptr [rax+60], 0  (48 83 78 60 00)
        cave[idx++] = 0x48;
        cave[idx++] = 0x83;
        cave[idx++] = 0x78;
        cave[idx++] = 0x60;
        cave[idx++] = 0x00;

        // je endp
        cave[idx++] = 0x74;
        int pEnd1 = idx;
        cave[idx++] = 0x00;

        // mov r15, [rax+60]  (4C 8B 78 60)
        cave[idx++] = 0x4C;
        cave[idx++] = 0x8B;
        cave[idx++] = 0x78;
        cave[idx++] = 0x60;

        // test r15, r15  (4D 85 FF)
        cave[idx++] = 0x4D;
        cave[idx++] = 0x85;
        cave[idx++] = 0xFF;

        // jz endp
        cave[idx++] = 0x74;
        int pEnd2 = idx;
        cave[idx++] = 0x00;

        // cmp qword ptr [r15+18], 0  (49 83 7F 18 00)
        cave[idx++] = 0x49;
        cave[idx++] = 0x83;
        cave[idx++] = 0x7F;
        cave[idx++] = 0x18;
        cave[idx++] = 0x00;

        // je endp
        cave[idx++] = 0x74;
        int pEnd3 = idx;
        cave[idx++] = 0x00;

        // mov r15, [r15+18]  (4D 8B 7F 18)
        cave[idx++] = 0x4D;
        cave[idx++] = 0x8B;
        cave[idx++] = 0x7F;
        cave[idx++] = 0x18;

        // cmp [g_battleUnitAddr1], r15  (49 3B 3D + rel32)
        // mov r11, &g_battleUnitAddr1
        cave[idx++] = 0x49;
        cave[idx++] = 0xBB;
        *(uintptr_t *)&cave[idx] = (uintptr_t)&g_battleUnitAddr1;
        idx += 8;
        // cmp [r11], r15  (4D 3B 3B)
        cave[idx++] = 0x4D;
        cave[idx++] = 0x3B;
        cave[idx++] = 0x3B;
        // je endp
        cave[idx++] = 0x74;
        int pEnd4 = idx;
        cave[idx++] = 0x00;

        // cmp [g_battleUnitAddr2], r15
        cave[idx++] = 0x49;
        cave[idx++] = 0xBB;
        *(uintptr_t *)&cave[idx] = (uintptr_t)&g_battleUnitAddr2;
        idx += 8;
        // cmp [r11], r15
        cave[idx++] = 0x4D;
        cave[idx++] = 0x3B;
        cave[idx++] = 0x3B;
        // je endp
        cave[idx++] = 0x74;
        int pEnd5 = idx;
        cave[idx++] = 0x00;

        // addr1 비었는지 체크
        cave[idx++] = 0x49;
        cave[idx++] = 0xBB;
        *(uintptr_t *)&cave[idx] = (uintptr_t)&g_battleUnitAddr1;
        idx += 8;
        // cmp [r11], 0
        cave[idx++] = 0x49;
        cave[idx++] = 0x83;
        cave[idx++] = 0x3B;
        cave[idx++] = 0x00;
        // je write_1
        cave[idx++] = 0x74;
        int pWrite1 = idx;
        cave[idx++] = 0x00;

        // addr2 비었는지 체크
        cave[idx++] = 0x49;
        cave[idx++] = 0xBB;
        *(uintptr_t *)&cave[idx] = (uintptr_t)&g_battleUnitAddr2;
        idx += 8;
        // cmp [r11], 0
        cave[idx++] = 0x49;
        cave[idx++] = 0x83;
        cave[idx++] = 0x3B;
        cave[idx++] = 0x00;
        // je write_2
        cave[idx++] = 0x74;
        int pWrite2 = idx;
        cave[idx++] = 0x00;

        // jmp endp
        cave[idx++] = 0xEB;
        int pJmpEnd = idx;
        cave[idx++] = 0x00;

        // write_1:
        cave[pWrite1] = (uint8_t)(idx - pWrite1 - 1);
        cave[idx++] = 0x49;
        cave[idx++] = 0xBB;
        *(uintptr_t *)&cave[idx] = (uintptr_t)&g_battleUnitAddr1;
        idx += 8;
        // mov [r11], r15
        cave[idx++] = 0x4D;
        cave[idx++] = 0x89;
        cave[idx++] = 0x3B;
        // jmp endp
        cave[idx++] = 0xEB;
        int pJmpEnd2 = idx;
        cave[idx++] = 0x00;

        // write_2:
        cave[pWrite2] = (uint8_t)(idx - pWrite2 - 1);
        cave[idx++] = 0x49;
        cave[idx++] = 0xBB;
        *(uintptr_t *)&cave[idx] = (uintptr_t)&g_battleUnitAddr2;
        idx += 8;
        // mov [r11], r15
        cave[idx++] = 0x4D;
        cave[idx++] = 0x89;
        cave[idx++] = 0x3B;

        // endp 레이블
        cave[pEnd1] = (uint8_t)(idx - pEnd1 - 1);
        cave[pEnd2] = (uint8_t)(idx - pEnd2 - 1);
        cave[pEnd3] = (uint8_t)(idx - pEnd3 - 1);
        cave[pEnd4] = (uint8_t)(idx - pEnd4 - 1);
        cave[pEnd5] = (uint8_t)(idx - pEnd5 - 1);
        cave[pJmpEnd] = (uint8_t)(idx - pJmpEnd - 1);
        cave[pJmpEnd2] = (uint8_t)(idx - pJmpEnd2 - 1);

        // pop r15  (41 5F)
        cave[idx++] = 0x41;
        cave[idx++] = 0x5F;

        // 원본: movzx eax, byte ptr [rax+0x80]
        uint8_t orig[] = {0x0F, 0xB6, 0x80, 0x80, 0x00, 0x00, 0x00};
        memcpy(&cave[idx], orig, 7);
        idx += 7;

        // 복귀 점프
        uintptr_t retAddr = hookAddr + 7;
        cave[idx++] = 0xFF;
        cave[idx++] = 0x25;
        cave[idx++] = 0x00;
        cave[idx++] = 0x00;
        cave[idx++] = 0x00;
        cave[idx++] = 0x00;
        *(uintptr_t *)&cave[idx] = retAddr;
        idx += 8;

        return ApplyJmp(hookAddr, g_battUnitCaveAddr, 7);
    }

    void SetBattleUnitCapture(bool enable) {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase)
            return;

        if (enable) {
            if (g_battUnitApplied)
                return;
            if (g_battUnitThreadRunning.exchange(true))
                return;

            HANDLE hThread = CreateThread(
                nullptr, 0,
                [](LPVOID) -> DWORD {
                    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
                    MODULEINFO mi;
                    GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi));
                    uintptr_t searchEnd = exeBase + mi.SizeOfImage;

                    if (!ResolveKnownBattUnitHook(exeBase, searchEnd, &g_battUnitHookAddr)) {
                        // 패턴: 08 0F B6 80 80 00 00 00
                        // 훅 위치는 +1 (08 다음)
                        uintptr_t found = FindPattern(exeBase, searchEnd, "08 0F B6 80 80 00 00 00");

                        if (found)
                            g_battUnitHookAddr = found + 1; // +1 위치에 훅
                    } else {
                        AddLog(u8"[BattleUnitCapture] V0.860 고정 RVA 검증 성공: +0x%llX",
                               (unsigned long long)kBattUnitV0860Rva);
                    }

                    AddLog("[DEBUG] battUnitHook: %p", (void *)g_battUnitHookAddr);

                    if (g_battUnitHookAddr && !g_battUnitApplied) {
                        memcpy(g_battUnitOriginal, (void *)g_battUnitHookAddr, 7);
                        if (InstallBattUnitCave(g_battUnitHookAddr))
                            g_battUnitApplied = true;
                    }

                    AddLog("[DEBUG] battUnit cave applied: %d", g_battUnitApplied);
                    g_battUnitThreadRunning.store(false);
                    return 0;
                },
                nullptr, 0, nullptr);

            if (hThread)
                CloseHandle(hThread);
            else
                g_battUnitThreadRunning.store(false);

        } else {
            // 유닛 주소 초기화
            g_battleUnitAddr1 = 0;
            g_battleUnitAddr2 = 0;

            if (g_battUnitApplied) {
                RestoreBytes(g_battUnitHookAddr, g_battUnitOriginal, 7);
                VirtualFree((LPVOID)g_battUnitCaveAddr, 0, MEM_RELEASE);
                g_battUnitCaveAddr = 0;
                g_battUnitApplied = false;
            }
        }
    }

} // namespace DX11Base
