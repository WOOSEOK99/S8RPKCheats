#include "../../pch.h"
#include "Infinitetalk.h"
#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "InstantLoveCave.h"
#include "Loyaltycave.h"
#include "Resonancecave.h"
#include "../../showlog.h"

#include <psapi.h>
#include <string>
#include <vector>

namespace DX11Base {
    // ───────────────────────────────────────────────
    //  무한 담화 패치 (cave 방식)
    //  원본: or dword ptr [rdi+0x320], 0x04  (7바이트)
    //  패치: and dword ptr [rdi+0x320], 0xFFFFFFFB
    // ───────────────────────────────────────────────

    static uintptr_t g_talkHookAddr = 0;
    static uint8_t g_talkOriginal[7] = {};
    static uintptr_t g_talkCaveAddr = 0;
    static bool g_talkApplied = false;

    void SetInfiniteTalk(bool enable) {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase)
            return;

        if (!g_talkHookAddr) {
            uintptr_t found = FindPattern(exeBase, exeBase + 0x3000000, "83 8F 20 03 00 00 04");
            if (found) g_talkHookAddr = found;
        }
        if (!g_talkHookAddr)
            return;

        if (!g_talkApplied && enable) {
            memcpy(g_talkOriginal, (void *)g_talkHookAddr, 7);

            g_talkCaveAddr = AllocNear(g_talkHookAddr, 128);
            if (!g_talkCaveAddr)
                return;

            uint8_t *cave = (uint8_t *)g_talkCaveAddr;
            int idx = 0;

            // and dword ptr [rdi+0x320], 0xFFFFFFFB
            cave[idx++] = 0x81;
            cave[idx++] = 0xA7;
            cave[idx++] = 0x20;
            cave[idx++] = 0x03;
            cave[idx++] = 0x00;
            cave[idx++] = 0x00;
            cave[idx++] = 0xFB;
            cave[idx++] = 0xFF;
            cave[idx++] = 0xFF;
            cave[idx++] = 0xFF;

            // 복귀 점프
            uintptr_t retAddr = g_talkHookAddr + 7;
            cave[idx++] = 0xFF;
            cave[idx++] = 0x25;
            cave[idx++] = 0x00;
            cave[idx++] = 0x00;
            cave[idx++] = 0x00;
            cave[idx++] = 0x00;
            *(uintptr_t *)&cave[idx] = retAddr;
            idx += 8;

            if (ApplyJmp(g_talkHookAddr, g_talkCaveAddr, 7))
                g_talkApplied = true;

        } else if (g_talkApplied && !enable) {
            RestoreBytes(g_talkHookAddr, g_talkOriginal, 7);
            VirtualFree((LPVOID)g_talkCaveAddr, 0, MEM_RELEASE);
            g_talkCaveAddr = 0;
            g_talkApplied = false;
            g_talkHookAddr = 0;
        }
    }

    // ───────────────────────────────────────────────
    // 무한 대련 / 토론
    //
    // 실제 게임에서 확인된 공통 사용 플래그 쓰기:
    //   movzx eax,bpl
    //   shl    eax,8
    //   add    eax,100h
    //   or     dword ptr [rdi+320h],eax
    //
    // BPL=0 -> EAX=0x100 (대련 bit8)
    // BPL=1 -> EAX=0x200 (토론 bit9)
    //
    // 정확한 마지막 OR 한 지점만 cave로 우회하고,
    // 활성화된 항목의 bit만 EAX에서 제거한 뒤 원래 OR을 실행한다.
    // ───────────────────────────────────────────────
    namespace {
        static uintptr_t g_duelDebateHookAddr = 0;
        static uintptr_t g_duelDebateCaveAddr = 0;
        static uint8_t g_duelDebateOriginal[6] = {};
        static bool g_duelDebateApplied = false;

        static volatile uint8_t g_infiniteDuelEnabled = 0;
        static volatile uint8_t g_infiniteDebateEnabled = 0;

        static bool InstallDuelDebateHook() {
            if (g_duelDebateApplied)
                return true;

            uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
            if (!exeBase)
                return false;

            // RVA +0x18569D9에서 실제 확인한 전체 계산 + 쓰기 패턴.
            uintptr_t found = FindPattern(
                exeBase,
                exeBase + 0x3000000,
                "40 0F B6 C5 C1 E0 08 05 00 01 00 00 09 87 20 03 00 00");

            if (!found) {
                AddLog(u8"[대련/토론] 검증된 +0x320 공통 쓰기 패턴을 찾지 못했습니다.");
                return false;
            }

            g_duelDebateHookAddr = found + 12;

            static const uint8_t expected[6] = {
                0x09, 0x87, 0x20, 0x03, 0x00, 0x00
            };

            if (memcmp((const void*)g_duelDebateHookAddr, expected, 6) != 0) {
                AddLog(u8"[대련/토론] 공통 쓰기 명령 원본 바이트가 예상과 다릅니다.");
                g_duelDebateHookAddr = 0;
                return false;
            }

            memcpy(g_duelDebateOriginal, expected, 6);

            g_duelDebateCaveAddr = AllocNear(g_duelDebateHookAddr, 160);
            if (!g_duelDebateCaveAddr) {
                AddLog(u8"[대련/토론] code cave 할당 실패.");
                g_duelDebateHookAddr = 0;
                return false;
            }

            uint8_t* cave = (uint8_t*)g_duelDebateCaveAddr;
            int i = 0;

            // 원본 OR은 EAX/RDI를 보존하고 flags만 변경한다.
            // 아래 보조 처리에서 RAX/R11을 사용하므로 둘 다 보존한다.
            cave[i++] = 0x50;             // push rax
            cave[i++] = 0x41;             // push r11
            cave[i++] = 0x53;

            // mov r11, &g_infiniteDuelEnabled
            cave[i++] = 0x49;
            cave[i++] = 0xBB;
            *(uintptr_t*)&cave[i] =
                (uintptr_t)&g_infiniteDuelEnabled;
            i += 8;

            // cmp byte ptr [r11],0
            cave[i++] = 0x41;
            cave[i++] = 0x80;
            cave[i++] = 0x3B;
            cave[i++] = 0x00;

            // je +5
            cave[i++] = 0x74;
            cave[i++] = 0x05;

            // and eax,0xFFFFFEFF  (대련 bit8 제거)
            cave[i++] = 0x25;
            cave[i++] = 0xFF;
            cave[i++] = 0xFE;
            cave[i++] = 0xFF;
            cave[i++] = 0xFF;

            // mov r11, &g_infiniteDebateEnabled
            cave[i++] = 0x49;
            cave[i++] = 0xBB;
            *(uintptr_t*)&cave[i] =
                (uintptr_t)&g_infiniteDebateEnabled;
            i += 8;

            // cmp byte ptr [r11],0
            cave[i++] = 0x41;
            cave[i++] = 0x80;
            cave[i++] = 0x3B;
            cave[i++] = 0x00;

            // je +5
            cave[i++] = 0x74;
            cave[i++] = 0x05;

            // and eax,0xFFFFFDFF  (토론 bit9 제거)
            cave[i++] = 0x25;
            cave[i++] = 0xFF;
            cave[i++] = 0xFD;
            cave[i++] = 0xFF;
            cave[i++] = 0xFF;

            // 원래 쓰기 명령.
            // 이 OR이 cave의 마지막 flags 변경 명령이므로 원본 flags 의미도 유지된다.
            cave[i++] = 0x09;
            cave[i++] = 0x87;
            cave[i++] = 0x20;
            cave[i++] = 0x03;
            cave[i++] = 0x00;
            cave[i++] = 0x00;

            cave[i++] = 0x41;             // pop r11
            cave[i++] = 0x5B;
            cave[i++] = 0x58;             // pop rax

            const uintptr_t retAddr = g_duelDebateHookAddr + 6;

            // jmp qword ptr [rip+0]
            cave[i++] = 0xFF;
            cave[i++] = 0x25;
            cave[i++] = 0x00;
            cave[i++] = 0x00;
            cave[i++] = 0x00;
            cave[i++] = 0x00;
            *(uintptr_t*)&cave[i] = retAddr;
            i += 8;

            if (!ApplyJmp(
                    g_duelDebateHookAddr,
                    g_duelDebateCaveAddr,
                    6)) {
                VirtualFree(
                    (LPVOID)g_duelDebateCaveAddr,
                    0,
                    MEM_RELEASE);
                g_duelDebateCaveAddr = 0;
                g_duelDebateHookAddr = 0;
                AddLog(u8"[대련/토론] 공통 쓰기 훅 설치 실패.");
                return false;
            }

            g_duelDebateApplied = true;
            AddLog(
                u8"[대련/토론] 검증된 공통 +0x320 쓰기 훅 적용: RVA:+0x%llX",
                (unsigned long long)(g_duelDebateHookAddr - exeBase));
            return true;
        }

        static void RemoveDuelDebateHookIfUnused() {
            if (g_infiniteDuelEnabled ||
                g_infiniteDebateEnabled ||
                !g_duelDebateApplied) {
                return;
            }

            RestoreBytes(
                g_duelDebateHookAddr,
                g_duelDebateOriginal,
                6);
            FlushInstructionCache(
                GetCurrentProcess(),
                (LPCVOID)g_duelDebateHookAddr,
                6);

            if (g_duelDebateCaveAddr) {
                VirtualFree(
                    (LPVOID)g_duelDebateCaveAddr,
                    0,
                    MEM_RELEASE);
            }

            g_duelDebateCaveAddr = 0;
            g_duelDebateHookAddr = 0;
            g_duelDebateApplied = false;

            AddLog(u8"[대련/토론] 공통 쓰기 훅 원본 복구 완료.");
        }
    }

    void SetInfiniteDuel(bool enable) {
        g_infiniteDuelEnabled = enable ? 1 : 0;

        if (enable) {
            if (!InstallDuelDebateHook()) {
                g_infiniteDuelEnabled = 0;
                return;
            }
        } else {
            RemoveDuelDebateHookIfUnused();
        }
    }

    void SetInfiniteDebate(bool enable) {
        g_infiniteDebateEnabled = enable ? 1 : 0;

        if (enable) {
            if (!InstallDuelDebateHook()) {
                g_infiniteDebateEnabled = 0;
                return;
            }
        } else {
            RemoveDuelDebateHookIfUnused();
        }
    }

} // namespace DX11Base