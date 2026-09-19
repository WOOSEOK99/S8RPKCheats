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
    std::atomic<bool> g_resonanceThreeThreadRunning(false);
    static uintptr_t g_resonanceHookAddr = 0;
    static uintptr_t g_resonanceCaveAddr = 0;
    static bool g_resonanceCaveApplied = false;
    static uint8_t g_resonanceOriginal[8] = { 0 };

    static uintptr_t g_resonanceThreeSelectHookAddr = 0;
    static uintptr_t g_resonanceThreeSelectCaveAddr = 0;
    static uint8_t g_resonanceThreeSelectOriginal[6] = { 0 };

    static uintptr_t g_resonanceThreeGetHookAddr = 0;
    static uintptr_t g_resonanceThreeGetCaveAddr = 0;
    static uint8_t g_resonanceThreeGetOriginal[9] = { 0 };

    static bool g_resonanceThreeCaveApplied = false;
    static volatile uint16_t g_resonanceThreeTargetId = 0;
    static volatile uint16_t g_resonanceThreeLockedTargetId = 0;

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
            if (g_resonanceThreadRunning || g_resonanceThreeThreadRunning) return;

            // 같은 훅을 쓰므로 3개 강제 모드와 동시 적용하지 않습니다.
            if (g_resonanceThreeCaveApplied) {
                SetDialogueResonanceThree(false);
                bResonanceThree = false;
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

    static void EmitAbsoluteReturn(uint8_t* cave, int& cur, uintptr_t returnAddr) {
        cave[cur++] = 0xFF; cave[cur++] = 0x25;
        *(uint32_t*)&cave[cur] = 0; cur += 4;
        *(uintptr_t*)&cave[cur] = returnAddr; cur += 8;
    }

    // CT296: 교류 화면에서 같은 장수 ID가 연속으로 들어오는 순간을
    // 실제 선택 대상으로 확정해 별도 LockedTargetId에 보관한다.
    static bool InstallResonanceThreeSelectCapture(uintptr_t hookAddr) {
        g_resonanceThreeSelectCaveAddr = AllocNear(hookAddr, 192);
        if (!g_resonanceThreeSelectCaveAddr)
            return false;

        uint8_t* cave = (uint8_t*)g_resonanceThreeSelectCaveAddr;
        int cur = 0;

        // cmp를 사용하므로 원본 flags를 보존한다.
        cave[cur++] = 0x9C; // pushfq
        cave[cur++] = 0x50; // push rax
        cave[cur++] = 0x51; // push rcx
        cave[cur++] = 0x52; // push rdx

        // new ID -> ecx
        cave[cur++] = 0x0F; cave[cur++] = 0xB7; cave[cur++] = 0x4E; cave[cur++] = 0x08;

        // old TargetId -> edx
        cave[cur++] = 0x48; cave[cur++] = 0xB8;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeTargetId; cur += 8;
        cave[cur++] = 0x0F; cave[cur++] = 0xB7; cave[cur++] = 0x10;

        // TargetId = new ID
        cave[cur++] = 0x66; cave[cur++] = 0x89; cave[cur++] = 0x08;

        // 같은 ID가 연속으로 들어왔을 때만 LockedTargetId 확정.
        cave[cur++] = 0x66; cave[cur++] = 0x3B; cave[cur++] = 0xCA; // cmp cx,dx
        cave[cur++] = 0x0F; cave[cur++] = 0x85;                    // jne rel32
        const int jneSkipLockDispPos = cur; cur += 4;

        cave[cur++] = 0x48; cave[cur++] = 0xB8;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeLockedTargetId; cur += 8;
        cave[cur++] = 0x66; cave[cur++] = 0x89; cave[cur++] = 0x08; // mov [rax],cx

        const int skipLockPos = cur;
        *(int32_t*)&cave[jneSkipLockDispPos] =
            (int32_t)(skipLockPos - (jneSkipLockDispPos + 4));

        cave[cur++] = 0x5A; // pop rdx
        cave[cur++] = 0x59; // pop rcx
        cave[cur++] = 0x58; // pop rax
        cave[cur++] = 0x9D; // popfq

        // 원본: mov eax,[rsi+00000320]
        memcpy(&cave[cur], g_resonanceThreeSelectOriginal,
               sizeof(g_resonanceThreeSelectOriginal));
        cur += (int)sizeof(g_resonanceThreeSelectOriginal);

        EmitAbsoluteReturn(cave, cur,
                           hookAddr + sizeof(g_resonanceThreeSelectOriginal));
        FlushInstructionCache(GetCurrentProcess(),
                              (LPCVOID)g_resonanceThreeSelectCaveAddr, cur);

        return ApplyJmp(hookAddr, g_resonanceThreeSelectCaveAddr,
                        sizeof(g_resonanceThreeSelectOriginal));
    }

    // CT369: 게임이 읽으려는 장수 ID가 LockedTargetId와 일치하면
    // 해당 공명값을 최소 3으로 맞춘 뒤 원본 getter를 계속 실행한다.
    static bool InstallResonanceThreeGetter(uintptr_t hookAddr) {
        g_resonanceThreeGetCaveAddr = AllocNear(hookAddr, 256);
        if (!g_resonanceThreeGetCaveAddr)
            return false;

        const uint32_t dynOffset = *(uint32_t*)(hookAddr + 5);
        uint8_t* cave = (uint8_t*)g_resonanceThreeGetCaveAddr;
        int cur = 0;

        // 원본 movzx는 flags를 변경하지 않으므로 비교에 사용한 상태를 보존한다.
        cave[cur++] = 0x9C;                         // pushfq
        cave[cur++] = 0x41; cave[cur++] = 0x53;   // push r11
        cave[cur++] = 0x50;                         // push rax

        // LockedTargetId가 0이면 적용하지 않는다.
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeLockedTargetId; cur += 8;
        cave[cur++] = 0x66; cave[cur++] = 0x41; cave[cur++] = 0x83; cave[cur++] = 0x3B; cave[cur++] = 0x00;
        cave[cur++] = 0x0F; cave[cur++] = 0x84; // je rel32
        const int jeNoLockDispPos = cur; cur += 4;

        // getter ID(dx)와 확정 선택 ID가 다르면 적용하지 않는다.
        cave[cur++] = 0x66; cave[cur++] = 0x41; cave[cur++] = 0x3B; cave[cur++] = 0x13;
        cave[cur++] = 0x0F; cave[cur++] = 0x85; // jne rel32
        const int jneIdDispPos = cur; cur += 4;

        // 현재 공명값 읽기.
        cave[cur++] = 0x41; cave[cur++] = 0x0F; cave[cur++] = 0xB6; cave[cur++] = 0x84; cave[cur++] = 0x08;
        *(uint32_t*)&cave[cur] = dynOffset; cur += 4;

        // 이미 3 이상이면 유지, 3 미만이면 3으로 설정.
        cave[cur++] = 0x3C; cave[cur++] = 0x03; // cmp al,3
        cave[cur++] = 0x73;                     // jae rel8
        const int jaeNoWriteDispPos = cur++;

        cave[cur++] = 0x41; cave[cur++] = 0xC6; cave[cur++] = 0x84; cave[cur++] = 0x08;
        *(uint32_t*)&cave[cur] = dynOffset; cur += 4;
        cave[cur++] = 0x03;

        const int noWritePos = cur;
        cave[jaeNoWriteDispPos] =
            (uint8_t)(noWritePos - (jaeNoWriteDispPos + 1));

        // 한 번 실제 대상 getter와 매칭되면 다음 교류를 위해 lock 해제.
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeLockedTargetId; cur += 8;
        cave[cur++] = 0x66; cave[cur++] = 0x41; cave[cur++] = 0xC7; cave[cur++] = 0x03;
        cave[cur++] = 0x00; cave[cur++] = 0x00;

        const int skipApplyPos = cur;
        *(int32_t*)&cave[jeNoLockDispPos] =
            (int32_t)(skipApplyPos - (jeNoLockDispPos + 4));
        *(int32_t*)&cave[jneIdDispPos] =
            (int32_t)(skipApplyPos - (jneIdDispPos + 4));

        cave[cur++] = 0x58;                         // pop rax
        cave[cur++] = 0x41; cave[cur++] = 0x5B;   // pop r11
        cave[cur++] = 0x9D;                         // popfq

        // 원본 CT369:
        // movzx eax,byte ptr [r8+rcx+00005BDA]
        memcpy(&cave[cur], g_resonanceThreeGetOriginal,
               sizeof(g_resonanceThreeGetOriginal));
        cur += (int)sizeof(g_resonanceThreeGetOriginal);

        EmitAbsoluteReturn(cave, cur,
                           hookAddr + sizeof(g_resonanceThreeGetOriginal));
        FlushInstructionCache(GetCurrentProcess(),
                              (LPCVOID)g_resonanceThreeGetCaveAddr, cur);

        return ApplyJmp(hookAddr, g_resonanceThreeGetCaveAddr,
                        sizeof(g_resonanceThreeGetOriginal));
    }

    static void RemoveResonanceThreeHooks() {
        if (g_resonanceThreeSelectHookAddr && g_resonanceThreeSelectOriginal[0] != 0) {
            RestoreBytes(g_resonanceThreeSelectHookAddr,
                         g_resonanceThreeSelectOriginal,
                         sizeof(g_resonanceThreeSelectOriginal));
        }

        if (g_resonanceThreeGetHookAddr && g_resonanceThreeGetOriginal[0] != 0) {
            RestoreBytes(g_resonanceThreeGetHookAddr,
                         g_resonanceThreeGetOriginal,
                         sizeof(g_resonanceThreeGetOriginal));
        }

        if (g_resonanceThreeSelectCaveAddr) {
            VirtualFree((LPVOID)g_resonanceThreeSelectCaveAddr, 0, MEM_RELEASE);
            g_resonanceThreeSelectCaveAddr = 0;
        }

        if (g_resonanceThreeGetCaveAddr) {
            VirtualFree((LPVOID)g_resonanceThreeGetCaveAddr, 0, MEM_RELEASE);
            g_resonanceThreeGetCaveAddr = 0;
        }

        g_resonanceThreeCaveApplied = false;
        g_resonanceThreeTargetId = 0;
        g_resonanceThreeLockedTargetId = 0;
    }

    void SetDialogueResonanceThree(bool enable) {
        if (enable) {
            if (g_resonanceThreeCaveApplied)
                return;
            if (g_resonanceThreeThreadRunning || g_resonanceThreadRunning)
                return;

            if (g_resonanceCaveApplied) {
                SetInstantResonance(false);
                bResonance = false;
            }

            g_resonanceThreeThreadRunning = true;
            std::thread([]() {
                uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
                if (exeBase) {
                    MODULEINFO mi{};
                    if (GetModuleInformation(GetCurrentProcess(),
                                             (HMODULE)exeBase,
                                             &mi, sizeof(mi))) {
                        const uintptr_t searchEnd = exeBase + mi.SizeOfImage;

                        // CT296: 교류 화면의 무장 레코드 흐름.
                        const char* selectPat =
                            "8B 86 20 03 00 00 C1 E8 12 A8 01 74 0A";
                        g_resonanceThreeSelectHookAddr =
                            FindPattern(exeBase, searchEnd, selectPat);

                        // CT369: 공명값 조회 지점.
                        const char* getterPat =
                            "41 0F B6 84 08 DA 5B 00 00";
                        g_resonanceThreeGetHookAddr =
                            FindPattern(exeBase, searchEnd, getterPat);

                        if (g_resonanceThreeSelectHookAddr &&
                            g_resonanceThreeGetHookAddr) {
                            memcpy(g_resonanceThreeSelectOriginal,
                                   (void*)g_resonanceThreeSelectHookAddr,
                                   sizeof(g_resonanceThreeSelectOriginal));
                            memcpy(g_resonanceThreeGetOriginal,
                                   (void*)g_resonanceThreeGetHookAddr,
                                   sizeof(g_resonanceThreeGetOriginal));

                            const bool selectOk =
                                InstallResonanceThreeSelectCapture(
                                    g_resonanceThreeSelectHookAddr);
                            const bool getterOk =
                                selectOk && InstallResonanceThreeGetter(
                                    g_resonanceThreeGetHookAddr);

                            if (selectOk && getterOk) {
                                g_resonanceThreeCaveApplied = true;
                                AddLog(u8"[Resonance3] 대화 상대 공명 3개 패치 적용");
                            } else {
                                RemoveResonanceThreeHooks();
                                AddLog(u8"[Resonance3] 대화 상대 공명 3개 패치 적용 실패");
                            }
                        } else {
                            AddLog(u8"[Resonance3] 필요한 패턴을 찾지 못했습니다. select=%p getter=%p",
                                   (void*)g_resonanceThreeSelectHookAddr,
                                   (void*)g_resonanceThreeGetHookAddr);
                        }
                    }
                }

                g_resonanceThreeThreadRunning = false;
            }).detach();
        } else {
            if (g_resonanceThreeCaveApplied ||
                g_resonanceThreeSelectCaveAddr ||
                g_resonanceThreeGetCaveAddr) {
                RemoveResonanceThreeHooks();
                AddLog(u8"[Resonance3] 대화 상대 공명 3개 패치 해제");
            }
        }
    }

} // namespace DX11Base
