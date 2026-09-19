#include "Resonancecave.h"
#include "../../pch.h"
#include "../../showlog.h"
#include "../../MemoryUtils.h"
#include <thread>
#include <atomic>
#include <psapi.h>

namespace DX11Base {
    std::atomic<bool> g_resonanceThreadRunning(false);

    static uintptr_t g_resonanceSelectHookAddr = 0;
    static uintptr_t g_resonanceSelectCaveAddr = 0;
    static uint8_t g_resonanceSelectOriginal[6] = { 0 };

    static uintptr_t g_resonanceGetHookAddr = 0;
    static uintptr_t g_resonanceGetCaveAddr = 0;
    static uint8_t g_resonanceGetOriginal[9] = { 0 };

    static bool g_resonanceCaveApplied = false;
    static volatile uint16_t g_resonanceTargetId = 0;
    static volatile uint16_t g_resonanceLockedTargetId = 0;

    static void EmitAbsoluteReturn(uint8_t* cave, int& cur, uintptr_t returnAddr) {
        cave[cur++] = 0xFF; cave[cur++] = 0x25;
        *(uint32_t*)&cave[cur] = 0; cur += 4;
        *(uintptr_t*)&cave[cur] = returnAddr; cur += 8;
    }

    // CT296: 교류 화면에서 같은 장수 ID가 연속으로 들어오는 순간을
    // 실제 선택 대상으로 확정하여 LockedTargetId에 보관한다.
    static bool InstallResonanceSelectCapture(uintptr_t hookAddr) {
        g_resonanceSelectCaveAddr = AllocNear(hookAddr, 192);
        if (!g_resonanceSelectCaveAddr)
            return false;

        uint8_t* cave = (uint8_t*)g_resonanceSelectCaveAddr;
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
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceTargetId; cur += 8;
        cave[cur++] = 0x0F; cave[cur++] = 0xB7; cave[cur++] = 0x10;

        // TargetId = new ID
        cave[cur++] = 0x66; cave[cur++] = 0x89; cave[cur++] = 0x08;

        // 같은 ID가 연속으로 들어왔을 때만 LockedTargetId 확정.
        cave[cur++] = 0x66; cave[cur++] = 0x3B; cave[cur++] = 0xCA; // cmp cx,dx
        cave[cur++] = 0x0F; cave[cur++] = 0x85;                    // jne rel32
        const int jneSkipLockDispPos = cur; cur += 4;

        cave[cur++] = 0x48; cave[cur++] = 0xB8;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceLockedTargetId; cur += 8;
        cave[cur++] = 0x66; cave[cur++] = 0x89; cave[cur++] = 0x08; // mov [rax],cx

        const int skipLockPos = cur;
        *(int32_t*)&cave[jneSkipLockDispPos] =
            (int32_t)(skipLockPos - (jneSkipLockDispPos + 4));

        cave[cur++] = 0x5A; // pop rdx
        cave[cur++] = 0x59; // pop rcx
        cave[cur++] = 0x58; // pop rax
        cave[cur++] = 0x9D; // popfq

        // 원본: mov eax,[rsi+00000320]
        memcpy(&cave[cur], g_resonanceSelectOriginal,
               sizeof(g_resonanceSelectOriginal));
        cur += (int)sizeof(g_resonanceSelectOriginal);

        EmitAbsoluteReturn(cave, cur,
                           hookAddr + sizeof(g_resonanceSelectOriginal));
        FlushInstructionCache(GetCurrentProcess(),
                              (LPCVOID)g_resonanceSelectCaveAddr, cur);

        return ApplyJmp(hookAddr, g_resonanceSelectCaveAddr,
                        sizeof(g_resonanceSelectOriginal));
    }

    // CT369: 게임이 읽으려는 장수 ID가 LockedTargetId와 일치하면
    // 해당 공명값을 최소 4로 맞춘 뒤 원본 getter를 계속 실행한다.
    static bool InstallResonanceGetter(uintptr_t hookAddr) {
        g_resonanceGetCaveAddr = AllocNear(hookAddr, 256);
        if (!g_resonanceGetCaveAddr)
            return false;

        const uint32_t dynOffset = *(uint32_t*)(hookAddr + 5);
        uint8_t* cave = (uint8_t*)g_resonanceGetCaveAddr;
        int cur = 0;

        // 원본 movzx는 flags를 변경하지 않으므로 비교에 사용한 상태를 보존한다.
        cave[cur++] = 0x9C;                         // pushfq
        cave[cur++] = 0x41; cave[cur++] = 0x53;   // push r11
        cave[cur++] = 0x50;                         // push rax

        // LockedTargetId가 0이면 적용하지 않는다.
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceLockedTargetId; cur += 8;
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

        // 이미 4 이상이면 유지, 4 미만이면 4로 설정.
        cave[cur++] = 0x3C; cave[cur++] = 0x04; // cmp al,4
        cave[cur++] = 0x73;                     // jae rel8
        const int jaeNoWriteDispPos = cur++;

        cave[cur++] = 0x41; cave[cur++] = 0xC6; cave[cur++] = 0x84; cave[cur++] = 0x08;
        *(uint32_t*)&cave[cur] = dynOffset; cur += 4;
        cave[cur++] = 0x04;

        const int noWritePos = cur;
        cave[jaeNoWriteDispPos] =
            (uint8_t)(noWritePos - (jaeNoWriteDispPos + 1));

        // 실제 대상 getter와 매칭되면 다음 교류를 위해 lock 해제.
        cave[cur++] = 0x49; cave[cur++] = 0xBB;
        *(uintptr_t*)&cave[cur] = (uintptr_t)&g_resonanceLockedTargetId; cur += 8;
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

        // 원본 CT369: movzx eax,byte ptr [r8+rcx+00005BDA]
        memcpy(&cave[cur], g_resonanceGetOriginal,
               sizeof(g_resonanceGetOriginal));
        cur += (int)sizeof(g_resonanceGetOriginal);

        EmitAbsoluteReturn(cave, cur,
                           hookAddr + sizeof(g_resonanceGetOriginal));
        FlushInstructionCache(GetCurrentProcess(),
                              (LPCVOID)g_resonanceGetCaveAddr, cur);

        return ApplyJmp(hookAddr, g_resonanceGetCaveAddr,
                        sizeof(g_resonanceGetOriginal));
    }

    static void RemoveResonanceHooks() {
        if (g_resonanceSelectHookAddr && g_resonanceSelectOriginal[0] != 0) {
            RestoreBytes(g_resonanceSelectHookAddr,
                         g_resonanceSelectOriginal,
                         sizeof(g_resonanceSelectOriginal));
        }

        if (g_resonanceGetHookAddr && g_resonanceGetOriginal[0] != 0) {
            RestoreBytes(g_resonanceGetHookAddr,
                         g_resonanceGetOriginal,
                         sizeof(g_resonanceGetOriginal));
        }

        if (g_resonanceSelectCaveAddr) {
            VirtualFree((LPVOID)g_resonanceSelectCaveAddr, 0, MEM_RELEASE);
            g_resonanceSelectCaveAddr = 0;
        }

        if (g_resonanceGetCaveAddr) {
            VirtualFree((LPVOID)g_resonanceGetCaveAddr, 0, MEM_RELEASE);
            g_resonanceGetCaveAddr = 0;
        }

        g_resonanceCaveApplied = false;
        g_resonanceTargetId = 0;
        g_resonanceLockedTargetId = 0;
    }

    // 기존 "무조건 공명 발생" 기능을 대체하는 최종 공명 기능.
    // 선택 상대의 공명값을 4로 맞춰 다음 담화에서 상생이 발생하는 흐름을 만든다.
    void SetInstantResonance(bool enable) {
        if (enable) {
            if (g_resonanceCaveApplied)
                return;
            if (g_resonanceThreadRunning)
                return;

            g_resonanceThreadRunning = true;
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
                        g_resonanceSelectHookAddr =
                            FindPattern(exeBase, searchEnd, selectPat);

                        // CT369: 공명값 조회 지점.
                        const char* getterPat =
                            "41 0F B6 84 08 DA 5B 00 00";
                        g_resonanceGetHookAddr =
                            FindPattern(exeBase, searchEnd, getterPat);

                        if (g_resonanceSelectHookAddr &&
                            g_resonanceGetHookAddr) {
                            memcpy(g_resonanceSelectOriginal,
                                   (void*)g_resonanceSelectHookAddr,
                                   sizeof(g_resonanceSelectOriginal));
                            memcpy(g_resonanceGetOriginal,
                                   (void*)g_resonanceGetHookAddr,
                                   sizeof(g_resonanceGetOriginal));

                            const bool selectOk =
                                InstallResonanceSelectCapture(
                                    g_resonanceSelectHookAddr);
                            const bool getterOk =
                                selectOk && InstallResonanceGetter(
                                    g_resonanceGetHookAddr);

                            if (selectOk && getterOk) {
                                g_resonanceCaveApplied = true;
                                AddLog(u8"[Resonance] 공명 4개 고정 패치 적용");
                            } else {
                                RemoveResonanceHooks();
                                AddLog(u8"[Resonance] 공명 4개 고정 패치 적용 실패");
                            }
                        } else {
                            AddLog(u8"[Resonance] 필요한 패턴을 찾지 못했습니다. select=%p getter=%p",
                                   (void*)g_resonanceSelectHookAddr,
                                   (void*)g_resonanceGetHookAddr);
                        }
                    }
                }

                g_resonanceThreadRunning = false;
            }).detach();
        } else {
            if (g_resonanceCaveApplied ||
                g_resonanceSelectCaveAddr ||
                g_resonanceGetCaveAddr) {
                RemoveResonanceHooks();
                AddLog(u8"[Resonance] 공명 4개 고정 패치 해제");
            }
        }
    }

} // namespace DX11Base
