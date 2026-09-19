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
    static volatile uint16_t g_resonanceThreePrevTargetId = 0;
    static volatile uint16_t g_resonanceThreeLockedTargetId = 0;
    static volatile uint8_t g_resonanceThreeArmed = 0;

    // CT369 실제 getter가 선택 상대에 대해 호출됐는지 확인하는 진단값.
    static volatile uint32_t g_resonanceThreeGetterSeq = 0;
    static volatile uint16_t g_resonanceThreeGetterId = 0;
    static volatile int32_t g_resonanceThreeGetterSlot = -1;
    static volatile uint8_t g_resonanceThreeGetterBefore = 0;
    static volatile uint8_t g_resonanceThreeGetterAfter = 0;

    // CT369 getter의 모든 호출을 TargetID 필터 없이 기록.
    static volatile uint32_t g_resonanceThreeRawGetterSeq = 0;
    static volatile uint16_t g_resonanceThreeRawGetterId = 0;
    static volatile int32_t g_resonanceThreeRawGetterSlot = -1;
    static volatile uint8_t g_resonanceThreeRawGetterValue = 0;

    // CT298 실제 담화 실행 경로에서 rdi+0x08을 확인하는 진단용.
    static uintptr_t g_resonanceThreeTalkHookAddr = 0;
    static uintptr_t g_resonanceThreeTalkCaveAddr = 0;
    static uint8_t g_resonanceThreeTalkOriginal[7] = { 0 };
    static volatile uint32_t g_resonanceThreeTalkSeq = 0;
    static volatile uint16_t g_resonanceThreeTalkId = 0;

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

    static bool InstallResonanceThreeSelectCapture(uintptr_t hookAddr) {
        g_resonanceThreeSelectCaveAddr = AllocNear(hookAddr, 192);
        if (!g_resonanceThreeSelectCaveAddr)
            return false;

        uint8_t* cave = (uint8_t*)g_resonanceThreeSelectCaveAddr;
        int cur = 0;

        // CT296은 실제 선택 장수 A 뒤에 화면 전환용 다른 레코드들도 지나간다.
        // 관찰상 실제 선택 장수는 같은 ID가 연속 2번 들어온다.
        // 같은 ID가 연속으로 들어온 순간 LockedTargetId에 확정 저장한다.
        // 이후 Target/Prev가 942/967 등으로 바뀌어도 LockedTargetId는 유지한다.
        //
        // 원본 mov eax,[rsi+320]은 flags를 바꾸지 않으므로,
        // cmp를 추가하는 이 훅은 flags까지 보존/복원한다.
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

        // PrevTargetId = old TargetId
        cave[cur++] = 0x48; cave[cur++] = 0xB8;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreePrevTargetId; cur += 8;
        cave[cur++] = 0x66; cave[cur++] = 0x89; cave[cur++] = 0x10;

        // TargetId = new ID
        cave[cur++] = 0x48; cave[cur++] = 0xB8;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeTargetId; cur += 8;
        cave[cur++] = 0x66; cave[cur++] = 0x89; cave[cur++] = 0x08;

        // if (new ID != old TargetId) skip lock
        cave[cur++] = 0x66; cave[cur++] = 0x3B; cave[cur++] = 0xCA; // cmp cx,dx
        cave[cur++] = 0x0F; cave[cur++] = 0x85;                    // jne rel32
        const int jneSkipLockDispPos = cur; cur += 4;

        // LockedTargetId = new ID
        cave[cur++] = 0x48; cave[cur++] = 0xB8;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeLockedTargetId; cur += 8;
        cave[cur++] = 0x66; cave[cur++] = 0x89; cave[cur++] = 0x08;

        const int skipLockPos = cur;
        *(int32_t*)&cave[jneSkipLockDispPos] =
            (int32_t)(skipLockPos - (jneSkipLockDispPos + 4));

        // armed = 1 (진단용/기존 로직 호환)
        cave[cur++] = 0x48; cave[cur++] = 0xB8;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeArmed; cur += 8;
        cave[cur++] = 0xC6; cave[cur++] = 0x00; cave[cur++] = 0x01;

        cave[cur++] = 0x5A; // pop rdx
        cave[cur++] = 0x59; // pop rcx
        cave[cur++] = 0x58; // pop rax
        cave[cur++] = 0x9D; // popfq

        // 원본: mov eax,[rsi+00000320]
        memcpy(&cave[cur], g_resonanceThreeSelectOriginal, sizeof(g_resonanceThreeSelectOriginal));
        cur += (int)sizeof(g_resonanceThreeSelectOriginal);

        EmitAbsoluteReturn(cave, cur, hookAddr + sizeof(g_resonanceThreeSelectOriginal));
        FlushInstructionCache(GetCurrentProcess(), (LPCVOID)g_resonanceThreeSelectCaveAddr, cur);

        return ApplyJmp(hookAddr, g_resonanceThreeSelectCaveAddr,
                        sizeof(g_resonanceThreeSelectOriginal));
    }

    static bool InstallResonanceThreeGetter(uintptr_t hookAddr) {
        g_resonanceThreeGetCaveAddr = AllocNear(hookAddr, 512);
        if (!g_resonanceThreeGetCaveAddr)
            return false;

        const uint32_t dynOffset = *(uint32_t*)(hookAddr + 5);
        uint8_t* cave = (uint8_t*)g_resonanceThreeGetCaveAddr;
        int cur = 0;

        // 원본 movzx는 flags를 건드리지 않으므로 비교에 사용한 flags/register를 보존.
        cave[cur++] = 0x9C;             // pushfq
        cave[cur++] = 0x41; cave[cur++] = 0x53; // push r11
        cave[cur++] = 0x50;             // push rax

        // ---- RAW CT369 진단: TargetID와 상관없이 모든 getter 호출 기록 ----
        // ID = dx
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeRawGetterId; cur += 8;
        cave[cur++] = 0x66; cave[cur++] = 0x41; cave[cur++] = 0x89; cave[cur++] = 0x13;

        // Slot = r8d
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeRawGetterSlot; cur += 8;
        cave[cur++] = 0x45; cave[cur++] = 0x89; cave[cur++] = 0x03;

        // Value = [r8+rcx+dynOffset]
        cave[cur++] = 0x41; cave[cur++] = 0x0F; cave[cur++] = 0xB6; cave[cur++] = 0x84; cave[cur++] = 0x08;
        *(uint32_t*)&cave[cur] = dynOffset; cur += 4;
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeRawGetterValue; cur += 8;
        cave[cur++] = 0x41; cave[cur++] = 0x88; cave[cur++] = 0x03;

        // seq++
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeRawGetterSeq; cur += 8;
        cave[cur++] = 0x41; cave[cur++] = 0xFF; cave[cur++] = 0x03;

        // armed != 1 이면 skipApply.
        // 적용 코드가 길기 때문에 rel32(0F 85 dword)를 사용한다.
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeArmed; cur += 8;
        cave[cur++] = 0x41; cave[cur++] = 0x80; cave[cur++] = 0x3B; cave[cur++] = 0x01;
        cave[cur++] = 0x0F; cave[cur++] = 0x85;
        const int jneArmedDispPos = cur; cur += 4;

        // getter ID(dx) == 확정 선택 LockedTargetId ?
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeLockedTargetId; cur += 8;
        cave[cur++] = 0x66; cave[cur++] = 0x41; cave[cur++] = 0x3B; cave[cur++] = 0x13;

        // 불일치면 skipApply (rel32)
        cave[cur++] = 0x0F; cave[cur++] = 0x85;
        const int jneIdDispPos = cur; cur += 4;

        // CT369 실제 getter까지 들어온 선택 상대만 진단합니다.
        // r8d = 장수ID -> 공명 슬롯 매핑 결과, [r8+rcx+dynOffset] = 실제 공명 카운트.
        // mov r11, &g_resonanceThreeGetterId
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeGetterId; cur += 8;
        // mov word ptr [r11], dx
        cave[cur++] = 0x66; cave[cur++] = 0x41; cave[cur++] = 0x89; cave[cur++] = 0x13;

        // mov r11, &g_resonanceThreeGetterSlot
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeGetterSlot; cur += 8;
        // mov dword ptr [r11], r8d
        cave[cur++] = 0x45; cave[cur++] = 0x89; cave[cur++] = 0x03;

        // movzx eax, byte ptr [r8+rcx+dynOffset]
        cave[cur++] = 0x41; cave[cur++] = 0x0F; cave[cur++] = 0xB6; cave[cur++] = 0x84; cave[cur++] = 0x08;
        *(uint32_t*)&cave[cur] = dynOffset; cur += 4;

        // mov r11, &g_resonanceThreeGetterBefore / mov [r11], al
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeGetterBefore; cur += 8;
        cave[cur++] = 0x41; cave[cur++] = 0x88; cave[cur++] = 0x03;

        // 공명은 "최소 3"만 보장합니다. 이미 3 이상이면 내리지 않습니다.
        // cmp al, 3 / jae noWrite
        cave[cur++] = 0x3C; cave[cur++] = 0x03;
        cave[cur++] = 0x73;
        const int jaeNoWriteDispPos = cur++;

        // mov byte ptr [r8+rcx+dynOffset], 3
        cave[cur++] = 0x41; cave[cur++] = 0xC6; cave[cur++] = 0x84; cave[cur++] = 0x08;
        *(uint32_t*)&cave[cur] = dynOffset; cur += 4;
        cave[cur++] = 0x03;

        const int noWritePos = cur;
        cave[jaeNoWriteDispPos] = (uint8_t)(noWritePos - (jaeNoWriteDispPos + 1));

        // 변경 후 값 기록
        cave[cur++] = 0x41; cave[cur++] = 0x0F; cave[cur++] = 0xB6; cave[cur++] = 0x84; cave[cur++] = 0x08;
        *(uint32_t*)&cave[cur] = dynOffset; cur += 4;
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeGetterAfter; cur += 8;
        cave[cur++] = 0x41; cave[cur++] = 0x88; cave[cur++] = 0x03;

        // seq++ (폴링 로그용)
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeGetterSeq; cur += 8;
        cave[cur++] = 0x41; cave[cur++] = 0xFF; cave[cur++] = 0x03;

        // 실제 확정 대상 getter가 매칭되어 3 적용을 끝냈으면 lock을 해제한다.
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeLockedTargetId; cur += 8;
        cave[cur++] = 0x66; cave[cur++] = 0x41; cave[cur++] = 0xC7; cave[cur++] = 0x03;
        cave[cur++] = 0x00; cave[cur++] = 0x00;

        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeArmed; cur += 8;
        cave[cur++] = 0x41; cave[cur++] = 0xC6; cave[cur++] = 0x03; cave[cur++] = 0x00;

        const int skipApplyPos = cur;
        *(int32_t*)&cave[jneArmedDispPos] =
            (int32_t)(skipApplyPos - (jneArmedDispPos + 4));
        *(int32_t*)&cave[jneIdDispPos] =
            (int32_t)(skipApplyPos - (jneIdDispPos + 4));

        cave[cur++] = 0x58;                         // pop rax
        cave[cur++] = 0x41; cave[cur++] = 0x5B;   // pop r11
        cave[cur++] = 0x9D;                         // popfq

        // 원본 CT ID 369: movzx eax,byte ptr [r8+rcx+00005BDA]
        memcpy(&cave[cur], g_resonanceThreeGetOriginal, sizeof(g_resonanceThreeGetOriginal));
        cur += (int)sizeof(g_resonanceThreeGetOriginal);

        EmitAbsoluteReturn(cave, cur, hookAddr + sizeof(g_resonanceThreeGetOriginal));
        FlushInstructionCache(GetCurrentProcess(), (LPCVOID)g_resonanceThreeGetCaveAddr, cur);

        return ApplyJmp(hookAddr, g_resonanceThreeGetCaveAddr,
                        sizeof(g_resonanceThreeGetOriginal));
    }

    static bool InstallResonanceThreeTalkDiagnostic(uintptr_t hookAddr) {
        g_resonanceThreeTalkCaveAddr = AllocNear(hookAddr, 128);
        if (!g_resonanceThreeTalkCaveAddr)
            return false;

        uint8_t* cave = (uint8_t*)g_resonanceThreeTalkCaveAddr;
        int cur = 0;

        // CT298의 OR [rdi+0x320],04 바로 다음 명령.
        // 이 시점의 rdi가 실제 담화 대상 레코드인지 확인한다.
        cave[cur++] = 0x9C;                         // pushfq
        cave[cur++] = 0x41; cave[cur++] = 0x53;   // push r11

        // movzx eax, word ptr [rdi+08]
        cave[cur++] = 0x0F; cave[cur++] = 0xB7; cave[cur++] = 0x47; cave[cur++] = 0x08;

        // mov r11, &g_resonanceThreeTalkId
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeTalkId; cur += 8;
        // mov word ptr [r11], ax
        cave[cur++] = 0x66; cave[cur++] = 0x41; cave[cur++] = 0x89; cave[cur++] = 0x03;

        // mov r11, &g_resonanceThreeTalkSeq / inc dword ptr [r11]
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceThreeTalkSeq; cur += 8;
        cave[cur++] = 0x41; cave[cur++] = 0xFF; cave[cur++] = 0x03;

        cave[cur++] = 0x41; cave[cur++] = 0x5B;   // pop r11
        cave[cur++] = 0x9D;                         // popfq

        // 원본: movzx eax,word ptr [rcx+000072C0]
        memcpy(&cave[cur], g_resonanceThreeTalkOriginal,
               sizeof(g_resonanceThreeTalkOriginal));
        cur += (int)sizeof(g_resonanceThreeTalkOriginal);

        EmitAbsoluteReturn(cave, cur,
                           hookAddr + sizeof(g_resonanceThreeTalkOriginal));
        FlushInstructionCache(GetCurrentProcess(),
                              (LPCVOID)g_resonanceThreeTalkCaveAddr, cur);

        return ApplyJmp(hookAddr, g_resonanceThreeTalkCaveAddr,
                        sizeof(g_resonanceThreeTalkOriginal));
    }

    static void RemoveResonanceThreeHooks() {
        if (g_resonanceThreeSelectHookAddr && g_resonanceThreeSelectOriginal[0] != 0)
            RestoreBytes(g_resonanceThreeSelectHookAddr, g_resonanceThreeSelectOriginal,
                         sizeof(g_resonanceThreeSelectOriginal));
        if (g_resonanceThreeGetHookAddr && g_resonanceThreeGetOriginal[0] != 0)
            RestoreBytes(g_resonanceThreeGetHookAddr, g_resonanceThreeGetOriginal,
                         sizeof(g_resonanceThreeGetOriginal));
        if (g_resonanceThreeTalkHookAddr && g_resonanceThreeTalkOriginal[0] != 0)
            RestoreBytes(g_resonanceThreeTalkHookAddr, g_resonanceThreeTalkOriginal,
                         sizeof(g_resonanceThreeTalkOriginal));

        if (g_resonanceThreeSelectCaveAddr) {
            VirtualFree((LPVOID)g_resonanceThreeSelectCaveAddr, 0, MEM_RELEASE);
            g_resonanceThreeSelectCaveAddr = 0;
        }
        if (g_resonanceThreeGetCaveAddr) {
            VirtualFree((LPVOID)g_resonanceThreeGetCaveAddr, 0, MEM_RELEASE);
            g_resonanceThreeGetCaveAddr = 0;
        }
        if (g_resonanceThreeTalkCaveAddr) {
            VirtualFree((LPVOID)g_resonanceThreeTalkCaveAddr, 0, MEM_RELEASE);
            g_resonanceThreeTalkCaveAddr = 0;
        }

        g_resonanceThreeCaveApplied = false;
        g_resonanceThreeTargetId = 0;
        g_resonanceThreeArmed = 0;
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
                    if (GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi))) {
                        const uintptr_t searchEnd = exeBase + mi.SizeOfImage;

                        // CT ID 296: 교류 화면에서 현재 선택한 상대 무장 레코드(RSI) 캡처.
                        const char* selectPat =
                            "8B 86 20 03 00 00 C1 E8 12 A8 01 74 0A";
                        g_resonanceThreeSelectHookAddr =
                            FindPattern(exeBase, searchEnd, selectPat);

                        // CT ID 369: 공명 getter. EDX=대상 무장 ID,
                        // [r8+rcx+disp32]가 실제 공명 저장 바이트.
                        const char* getterPat =
                            "41 0F B6 84 08 DA 5B 00 00";
                        g_resonanceThreeGetHookAddr =
                            FindPattern(exeBase, searchEnd, getterPat);

                        // CT298의 담화 사용 플래그 기록 직후 명령.
                        // 기존 InfiniteTalk 훅(83 8F 20 03...)과 겹치지 않는다.
                        const char* talkPat =
                            "0F B7 81 C0 72 00 00 66 89 87 1A 03 00 00";
                        g_resonanceThreeTalkHookAddr =
                            FindPattern(exeBase, searchEnd, talkPat);

                        if (g_resonanceThreeSelectHookAddr && g_resonanceThreeGetHookAddr) {
                            memcpy(g_resonanceThreeSelectOriginal,
                                   (void*)g_resonanceThreeSelectHookAddr,
                                   sizeof(g_resonanceThreeSelectOriginal));
                            memcpy(g_resonanceThreeGetOriginal,
                                   (void*)g_resonanceThreeGetHookAddr,
                                   sizeof(g_resonanceThreeGetOriginal));

                            const bool selectOk =
                                InstallResonanceThreeSelectCapture(g_resonanceThreeSelectHookAddr);
                            const bool getterOk =
                                selectOk && InstallResonanceThreeGetter(g_resonanceThreeGetHookAddr);

                            bool talkDiagOk = false;
                            if (getterOk && g_resonanceThreeTalkHookAddr) {
                                memcpy(g_resonanceThreeTalkOriginal,
                                       (void*)g_resonanceThreeTalkHookAddr,
                                       sizeof(g_resonanceThreeTalkOriginal));
                                talkDiagOk =
                                    InstallResonanceThreeTalkDiagnostic(g_resonanceThreeTalkHookAddr);
                            }

                            if (selectOk && getterOk) {
                                g_resonanceThreeCaveApplied = true;
                                AddLog(u8"[Resonance3] 교류 상대 캡처 + 공명 getter 필터 패치 성공 (TalkDiag=%d)",
                                       talkDiagOk ? 1 : 0);
                            } else {
                                RemoveResonanceThreeHooks();
                                AddLog(u8"[Resonance3] 교류 상대/공명 getter 패치 적용 실패");
                            }
                        } else {
                            AddLog(u8"[Resonance3] 필요한 CT296/CT369 패턴을 찾지 못했습니다. select=%p getter=%p",
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
                g_resonanceThreeGetCaveAddr ||
                g_resonanceThreeTalkCaveAddr) {
                RemoveResonanceThreeHooks();
                AddLog(u8"[Resonance3] 교류 상대 공명 3개 패치 해제");
            }
        }
    }

    void RunResonanceDebugPoll() {
        static uint16_t s_lastLoggedTargetId = 0;
        static uint32_t s_lastGetterSeq = 0;
        static uint32_t s_lastRawGetterSeq = 0;
        static uint32_t s_lastTalkSeq = 0;

        if (!bResonanceThree)
            return;

        const uint16_t targetId = g_resonanceThreeTargetId;
        if (targetId != 0 && targetId != s_lastLoggedTargetId) {
            s_lastLoggedTargetId = targetId;
            AddLog(u8"[Resonance3Debug] TargetID=%u PrevID=%u LockedID=%u Armed=%u",
                   targetId,
                   (unsigned)g_resonanceThreePrevTargetId,
                   (unsigned)g_resonanceThreeLockedTargetId,
                   (unsigned)g_resonanceThreeArmed);
        }

        const uint32_t talkSeq = g_resonanceThreeTalkSeq;
        if (talkSeq != s_lastTalkSeq) {
            s_lastTalkSeq = talkSeq;
            AddLog(u8"[Resonance3Talk] Hit#%u ActualTalkID=%u (SelectTarget=%u)",
                   talkSeq,
                   (unsigned)g_resonanceThreeTalkId,
                   (unsigned)g_resonanceThreeTargetId);
        }

        const uint32_t rawSeq = g_resonanceThreeRawGetterSeq;
        if (rawSeq != s_lastRawGetterSeq) {
            s_lastRawGetterSeq = rawSeq;
            AddLog(u8"[Resonance3Raw] Hit#%u ID=%u Slot=%d Value=%u (SelectTarget=%u)",
                   rawSeq,
                   (unsigned)g_resonanceThreeRawGetterId,
                   (int)g_resonanceThreeRawGetterSlot,
                   (unsigned)g_resonanceThreeRawGetterValue,
                   (unsigned)g_resonanceThreeTargetId);
        }

        const uint32_t seq = g_resonanceThreeGetterSeq;
        if (seq != s_lastGetterSeq) {
            s_lastGetterSeq = seq;
            AddLog(u8"[Resonance3Getter] Hit#%u ID=%u Slot=%d Resonance=%u -> %u",
                   seq,
                   (unsigned)g_resonanceThreeGetterId,
                   (int)g_resonanceThreeGetterSlot,
                   (unsigned)g_resonanceThreeGetterBefore,
                   (unsigned)g_resonanceThreeGetterAfter);
        }
    }

}
