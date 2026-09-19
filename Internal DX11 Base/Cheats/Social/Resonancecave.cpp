#include "Resonancecave.h"
#include "../../MenuState.h"
#include "../../pch.h"
#include "../../showlog.h"
#include "../../MemoryUtils.h"
#include "../../Cheats.h"
#include <thread>
#include <atomic>
#include <psapi.h>

namespace DX11Base {
    std::atomic<bool> g_resonanceThreadRunning(false);
    std::atomic<bool> g_resonanceFourThreadRunning(false);
    static uintptr_t g_resonanceHookAddr = 0;
    static uintptr_t g_resonanceCaveAddr = 0;
    static bool g_resonanceCaveApplied = false;
    static uint8_t g_resonanceOriginal[8] = { 0 };

    static uintptr_t g_resonanceFourCaveAddr = 0;
    static bool g_resonanceFourCaveApplied = false;
    static uint8_t g_resonanceFourOriginal[8] = { 0 };

    static bool InstallResonanceCave(uintptr_t hookAddr) {
        // 코드 케이브 할당 (8.0 버전에 맞춰 AllocNear 사용)
        g_resonanceCaveAddr = AllocNear(hookAddr, 128);
        if (!g_resonanceCaveAddr) return false;

        uint32_t dynOffset = *(uint32_t*)(hookAddr + 4);
        uint8_t* cave = (uint8_t*)g_resonanceCaveAddr;
        int cur = 0;

        // --- Cave Code ---
        // 1. 오리지널 실행: movzx ecx, [rdx+rsi+dynOffset] (8바이트)
        cave[cur++] = 0x0F; cave[cur++] = 0xB6; cave[cur++] = 0x8C; cave[cur++] = 0x32;
        *(uint32_t*)&cave[cur] = dynOffset; cur += 4;

        // 2. 조건 확인: cmp ecx, 0
        cave[cur++] = 0x83; cave[cur++] = 0xF9; cave[cur++] = 0x00;

        // 3. 0이면 리턴 지점으로 점프 (jz -> 64비트 점프 위치로)
        cave[cur++] = 0x74; cave[cur++] = 0x0D; // 뒤의 mov 2개를 건너뜀 (5 + 8 = 13 바이트)

        // 4. 포인트가 있으면 3으로 고정: mov ecx, 3 (5바이트)
        cave[cur++] = 0xB9; *(uint32_t*)&cave[cur] = 3; cur += 4;

        // 5. 메모리에도 3 쓰기: mov byte ptr [rdx+rsi+dynOffset], 3 (8바이트)
        cave[cur++] = 0xC6; cave[cur++] = 0x84; cave[cur++] = 0x32;
        *(uint32_t*)&cave[cur] = dynOffset; cur += 4;
        cave[cur++] = 0x03;

        // 6. 복귀: 64비트 절대 주소 점프
        uintptr_t returnAddr = hookAddr + 8;
        cave[cur++] = 0xFF; cave[cur++] = 0x25; // jmp [rip+0]
        *(uint32_t*)&cave[cur] = 0; cur += 4;
        *(uintptr_t*)&cave[cur] = returnAddr; cur += 8;

        return ApplyJmp(hookAddr, g_resonanceCaveAddr, 8);
    }

    void SetInstantResonance(bool enable) {
        if (enable) {
            if (g_resonanceCaveApplied) return;
            if (g_resonanceThreadRunning || g_resonanceFourThreadRunning) return;

            // 같은 훅을 쓰므로 4개 강제 모드와 동시 적용하지 않습니다.
            if (g_resonanceFourCaveApplied) {
                SetDialogueResonanceFour(false);
                bResonanceFour = false;
            }

            g_resonanceThreadRunning = true;
            std::thread([]() {
                uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
                if (exeBase) {
                    MODULEINFO mi;
                    if (GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi))) {
                        uintptr_t searchEnd = exeBase + mi.SizeOfImage;

                        if (!g_resonanceHookAddr) {
                            const char* pat = "0F B7 C9 48 63 94 88 ?? ?? ?? ?? 83 FA FF 74 ?? 0F B6 ?? 32 ?? ?? ?? ?? EB";
                            uintptr_t found = FindPattern(exeBase, searchEnd, pat);
                            if (found) {
                                g_resonanceHookAddr = found + 16;
                            }
                        }

                        if (g_resonanceHookAddr && !g_resonanceCaveApplied) {
                            memcpy(g_resonanceOriginal, (void*)g_resonanceHookAddr, 8);
                            if (InstallResonanceCave(g_resonanceHookAddr)) {
                                g_resonanceCaveApplied = true;
                                AddLog(u8"[Resonance] 공명 고정 패치 성공 (Cave)");
                            } else {
                                AddLog(u8"[Resonance] Cave 할당 실패");
                            }
                        }
                    }
                }
                g_resonanceThreadRunning = false;
            }).detach();
        }
        else {
            if (g_resonanceCaveApplied) {
                RestoreBytes(g_resonanceHookAddr, g_resonanceOriginal, 8);
                VirtualFree((LPVOID)g_resonanceCaveAddr, 0, MEM_RELEASE);
                g_resonanceCaveAddr = 0;
                g_resonanceCaveApplied = false;
                AddLog(u8"[Resonance] 공명 패치 해제 완료");
            }
        }
    }

    static bool InstallResonanceFourCave(uintptr_t hookAddr) {
        g_resonanceFourCaveAddr = AllocNear(hookAddr, 96);
        if (!g_resonanceFourCaveAddr)
            return false;

        const uint32_t dynOffset = *(uint32_t*)(hookAddr + 4);
        uint8_t* cave = (uint8_t*)g_resonanceFourCaveAddr;
        int cur = 0;

        // 현재 이 루틴에서 읽는 상대 장수의 공명값을 무조건 4로 설정.
        // mov byte ptr [rdx+rsi+dynOffset], 4
        cave[cur++] = 0xC6; cave[cur++] = 0x84; cave[cur++] = 0x32;
        *(uint32_t*)&cave[cur] = dynOffset; cur += 4;
        cave[cur++] = 0x04;

        // 원본: movzx ecx, byte ptr [rdx+rsi+dynOffset]
        memcpy(&cave[cur], g_resonanceFourOriginal, 8);
        cur += 8;

        // 원래 코드로 복귀
        const uintptr_t returnAddr = hookAddr + 8;
        cave[cur++] = 0xFF; cave[cur++] = 0x25;
        *(uint32_t*)&cave[cur] = 0; cur += 4;
        *(uintptr_t*)&cave[cur] = returnAddr; cur += 8;

        FlushInstructionCache(GetCurrentProcess(), (LPCVOID)g_resonanceFourCaveAddr, cur);
        return ApplyJmp(hookAddr, g_resonanceFourCaveAddr, 8);
    }

    void SetDialogueResonanceFour(bool enable) {
        if (enable) {
            if (g_resonanceFourCaveApplied)
                return;
            if (g_resonanceFourThreadRunning || g_resonanceThreadRunning)
                return;

            // 기존 1개 이상 -> 3 기능과 같은 훅을 공유하므로 먼저 해제.
            if (g_resonanceCaveApplied) {
                SetInstantResonance(false);
                bResonance = false;
            }

            g_resonanceFourThreadRunning = true;
            std::thread([]() {
                uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
                if (exeBase) {
                    MODULEINFO mi{};
                    if (GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi))) {
                        const uintptr_t searchEnd = exeBase + mi.SizeOfImage;

                        if (!g_resonanceHookAddr) {
                            // CT ID 368 계열: 상대 무장 ID를 해석한 뒤 공명값을 읽는 지점.
                            const char* pat =
                                "0F B7 C9 48 63 94 88 ?? ?? ?? ?? 83 FA FF 74 ?? "
                                "0F B6 ?? 32 ?? ?? ?? ?? EB";
                            uintptr_t found = FindPattern(exeBase, searchEnd, pat);
                            if (found)
                                g_resonanceHookAddr = found + 16;
                        }

                        if (g_resonanceHookAddr && !g_resonanceFourCaveApplied) {
                            memcpy(g_resonanceFourOriginal, (void*)g_resonanceHookAddr, 8);

                            if (InstallResonanceFourCave(g_resonanceHookAddr)) {
                                g_resonanceFourCaveApplied = true;
                                AddLog(u8"[Resonance4] 상대 공명 4개 강제 패치 성공");
                            } else {
                                AddLog(u8"[Resonance4] Cave 할당/적용 실패");
                            }
                        } else if (!g_resonanceHookAddr) {
                            AddLog(u8"[Resonance4] 공명 훅 패턴을 찾지 못했습니다.");
                        }
                    }
                }

                g_resonanceFourThreadRunning = false;
            }).detach();
        } else {
            if (g_resonanceFourCaveApplied) {
                RestoreBytes(g_resonanceHookAddr, g_resonanceFourOriginal, 8);
                VirtualFree((LPVOID)g_resonanceFourCaveAddr, 0, MEM_RELEASE);
                g_resonanceFourCaveAddr = 0;
                g_resonanceFourCaveApplied = false;
                AddLog(u8"[Resonance4] 상대 공명 4개 강제 패치 해제");
            }
        }
    }
}
