#include "../../pch.h"
#include "DomesticsMult.h"
#include "../../Cheats.h"
#include "../../showlog.h"
#include "../../MemoryUtils.h"
#include "../../PerformanceDiagnostics.h"
#include <psapi.h>
#include <string>

namespace DX11Base {

    void AddLog(const char* fmt, ...);
    uintptr_t FindPattern(uintptr_t start, uintptr_t end, const std::string& pattern);
    // uintptr_t AllocNear(uintptr_t target, size_t size); // Now from MemoryUtils.h
    // bool IsValidPtr(uintptr_t addr, SIZE_T size); // Now from Cheats.h

    // ───────────────────────────────────────────────
    //  내정 배율 cave
    //  원본: cmovle r14d,edi / movzx ebx,byte ptr [rsp+60]  (9바이트)
    //  플레이어: 2.0배 / 같은 세력: 1.25배
    // ───────────────────────────────────────────────

    bool      g_domesticsEnabled = false;
    bool      g_domesticsRunning = false;

    // 플레이어/세력 주소 (cave에서 채워짐)
    static uintptr_t g_myPlayer = 0;
    static uintptr_t g_myForce  = 0;

    // 배율 (float)
    static float g_playerMult = 2.0f;
    static float g_forceMult  = 1.25f;

    static uintptr_t g_domesticsHookAddr    = 0;
    static uint8_t   g_domesticsOriginal[9] = {};
    static uintptr_t g_domesticsCaveAddr    = 0;
    static bool      g_domesticsApplied     = false;
    static bool InstallDomesticsCave(uintptr_t hookAddr) {
        g_domesticsCaveAddr = AllocNear(hookAddr, 1024);
        if (!g_domesticsCaveAddr) return false;

        uint8_t* cave = (uint8_t*)g_domesticsCaveAddr;
        int idx = 0;

        // 원본 instruction 1: cmovle r14d, edi
        cave[idx++] = 0x44; cave[idx++] = 0x0F;
        cave[idx++] = 0x4E; cave[idx++] = 0xF7;

        // 레지스터 및 플래그 보존
        cave[idx++] = 0x9C; // pushfq
        cave[idx++] = 0x50; // push rax
        cave[idx++] = 0x53; // push rbx

        // S03 diagnostics: count actual cave entries without a function call.
        // rax and flags are already preserved by this cave.
        cave[idx++] = 0x48; cave[idx++] = 0xB8; // mov rax, imm64
        *(uintptr_t*)&cave[idx] = PerfDiagnosticsGateAddress(); idx += 8;
        cave[idx++] = 0x83; cave[idx++] = 0x38; cave[idx++] = 0x00; // cmp dword ptr [rax], 0
        cave[idx++] = 0x74; int pPerfSkip = idx; cave[idx++] = 0x00; // je perf_skip
        cave[idx++] = 0x48; cave[idx++] = 0xB8; // mov rax, imm64
        *(uintptr_t*)&cave[idx] = PerfRawHookCallCounterAddress(PerfMetric::DomesticsHook); idx += 8;
        cave[idx++] = 0xF0; cave[idx++] = 0x48; cave[idx++] = 0xFF; cave[idx++] = 0x00; // lock inc qword ptr [rax]
        cave[pPerfSkip] = static_cast<uint8_t>(idx - pPerfSkip - 1);

        // ── 0. R12 Null check ──
        cave[idx++] = 0x4D; cave[idx++] = 0x85; cave[idx++] = 0xE4; // test r12, r12
        cave[idx++] = 0x74; int pCodeNull = idx; cave[idx++] = 0x00; // je code

        // ── 1. MyPlayer가 0이면 현재 무장 저장 ──
        // mov rax, &g_myPlayer
        cave[idx++] = 0x48; cave[idx++] = 0xB8;
        *(uintptr_t*)&cave[idx] = (uintptr_t)&g_myPlayer; idx += 8;
        // cmp qword ptr [rax], 0
        cave[idx++] = 0x48; cave[idx++] = 0x83; cave[idx++] = 0x38; cave[idx++] = 0x00;
        // jne check_identity
        cave[idx++] = 0x75; int pCheckIdent = idx; cave[idx++] = 0x00;

        // mov [rax], r12
        cave[idx++] = 0x4C; cave[idx++] = 0x89; cave[idx++] = 0x20;

        // mov rax, [r12+0x18] (Force pointer)
        cave[idx++] = 0x49; cave[idx++] = 0x8B; cave[idx++] = 0x44; cave[idx++] = 0x24; cave[idx++] = 0x18;
        // mov rbx, &g_myForce
        cave[idx++] = 0x48; cave[idx++] = 0xBB;
        *(uintptr_t*)&cave[idx] = (uintptr_t)&g_myForce; idx += 8;
        // mov [rbx], rax
        cave[idx++] = 0x48; cave[idx++] = 0x89; cave[idx++] = 0x03;

        // check_identity:
        cave[pCheckIdent] = (uint8_t)(idx - pCheckIdent - 1);

        // ── 2. Identity Check ──
        // mov rax, &g_myPlayer
        cave[idx++] = 0x48; cave[idx++] = 0xB8;
        *(uintptr_t*)&cave[idx] = (uintptr_t)&g_myPlayer; idx += 8;
        // rax = *g_myPlayer
        cave[idx++] = 0x48; cave[idx++] = 0x8B; cave[idx++] = 0x00;
        // cmp r12, rax
        cave[idx++] = 0x49; cave[idx++] = 0x39; cave[idx++] = 0xC4;
        // je ApplyPlayer
        cave[idx++] = 0x74; int pApplyPlayer = idx; cave[idx++] = 0x00;

        // mov rax, [r12+0x18] (Current Force)
        cave[idx++] = 0x49; cave[idx++] = 0x8B; cave[idx++] = 0x44; cave[idx++] = 0x24; cave[idx++] = 0x18;
        // mov rbx, &g_myForce
        cave[idx++] = 0x48; cave[idx++] = 0xBB;
        *(uintptr_t*)&cave[idx] = (uintptr_t)&g_myForce; idx += 8;
        // rbx = *g_myForce
        cave[idx++] = 0x48; cave[idx++] = 0x8B; cave[idx++] = 0x1B;
        // test rbx, rbx
        cave[idx++] = 0x48; cave[idx++] = 0x85; cave[idx++] = 0xDB;
        // je code
        cave[idx++] = 0x74; int pCodeFromForce = idx; cave[idx++] = 0x00;
        // cmp rax, rbx
        cave[idx++] = 0x48; cave[idx++] = 0x39; cave[idx++] = 0xD8;
        // je ApplyForce
        cave[idx++] = 0x74; int pApplyForce = idx; cave[idx++] = 0x00;
        // jmp code
        cave[idx++] = 0xEB; int pCodeSkip = idx; cave[idx++] = 0x00;

        // ── 3. ApplyPlayer ──
        cave[pApplyPlayer] = (uint8_t)(idx - pApplyPlayer - 1);
        // sub rsp, 0x10
        cave[idx++] = 0x48; cave[idx++] = 0x83; cave[idx++] = 0xEC; cave[idx++] = 0x10;
        // movdqu [rsp], xmm0
        cave[idx++] = 0xF3; cave[idx++] = 0x0F; cave[idx++] = 0x7F; cave[idx++] = 0x04; cave[idx++] = 0x24;
        // cvtsi2ss xmm0, r14d
        cave[idx++] = 0xF3; cave[idx++] = 0x41; cave[idx++] = 0x0F; cave[idx++] = 0x2A; cave[idx++] = 0xC6;
        // mov rax, &g_playerMult
        cave[idx++] = 0x48; cave[idx++] = 0xB8;
        *(uintptr_t*)&cave[idx] = (uintptr_t)&g_playerMult; idx += 8;
        // mulss xmm0, [rax]
        cave[idx++] = 0xF3; cave[idx++] = 0x0F; cave[idx++] = 0x59; cave[idx++] = 0x00;
        // cvttss2si r14d, xmm0
        cave[idx++] = 0xF3; cave[idx++] = 0x44; cave[idx++] = 0x0F; cave[idx++] = 0x2C; cave[idx++] = 0xF0;
        // movdqu xmm0, [rsp]
        cave[idx++] = 0xF3; cave[idx++] = 0x0F; cave[idx++] = 0x6F; cave[idx++] = 0x04; cave[idx++] = 0x24;
        // add rsp, 0x10
        cave[idx++] = 0x48; cave[idx++] = 0x83; cave[idx++] = 0xC4; cave[idx++] = 0x10;
        // jmp code
        cave[idx++] = 0xEB; int pCodeFromPlayer = idx; cave[idx++] = 0x00;

        // ── 4. ApplyForce ──
        cave[pApplyForce] = (uint8_t)(idx - pApplyForce - 1);
        // sub rsp, 0x10
        cave[idx++] = 0x48; cave[idx++] = 0x83; cave[idx++] = 0xEC; cave[idx++] = 0x10;
        // movdqu [rsp], xmm0
        cave[idx++] = 0xF3; cave[idx++] = 0x0F; cave[idx++] = 0x7F; cave[idx++] = 0x04; cave[idx++] = 0x24;
        // cvtsi2ss xmm0, r14d
        cave[idx++] = 0xF3; cave[idx++] = 0x41; cave[idx++] = 0x0F; cave[idx++] = 0x2A; cave[idx++] = 0xC6;
        // mov rax, &g_forceMult
        cave[idx++] = 0x48; cave[idx++] = 0xB8;
        *(uintptr_t*)&cave[idx] = (uintptr_t)&g_forceMult; idx += 8;
        // mulss xmm0, [rax]
        cave[idx++] = 0xF3; cave[idx++] = 0x0F; cave[idx++] = 0x59; cave[idx++] = 0x00;
        // cvttss2si r14d, xmm0
        cave[idx++] = 0xF3; cave[idx++] = 0x44; cave[idx++] = 0x0F; cave[idx++] = 0x2C; cave[idx++] = 0xF0;
        // movdqu xmm0, [rsp]
        cave[idx++] = 0xF3; cave[idx++] = 0x0F; cave[idx++] = 0x6F; cave[idx++] = 0x04; cave[idx++] = 0x24;
        // add rsp, 0x10
        cave[idx++] = 0x48; cave[idx++] = 0x83; cave[idx++] = 0xC4; cave[idx++] = 0x10;

        // ── code Label ──
        cave[pCodeNull] = (uint8_t)(idx - pCodeNull - 1);
        cave[pCodeFromForce] = (uint8_t)(idx - pCodeFromForce - 1);
        cave[pCodeSkip] = (uint8_t)(idx - pCodeSkip - 1);
        cave[pCodeFromPlayer] = (uint8_t)(idx - pCodeFromPlayer - 1);

        cave[idx++] = 0x5B; // pop rbx
        cave[idx++] = 0x58; // pop rax
        cave[idx++] = 0x9D; // popfq

        // 원본 instruction 2: movzx ebx, byte ptr [rsp+60]
        cave[idx++] = 0x0F; cave[idx++] = 0xB6;
        cave[idx++] = 0x5C; cave[idx++] = 0x24; cave[idx++] = 0x60;

        // 복귀 점프 (Absolute)
        uintptr_t retAddr = hookAddr + 9;
        cave[idx++] = 0xFF; cave[idx++] = 0x25;
        cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00;
        *(uintptr_t*)&cave[idx] = retAddr; idx += 8;

        return ApplyJmp(hookAddr, g_domesticsCaveAddr, 9);
    }

    void SetDomesticsMult(bool enable) {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) return;

        if (enable) {
            if (g_domesticsApplied) return;
            if (g_domesticsRunning) return;

            g_domesticsRunning = true;
            g_myPlayer = 0;
            g_myForce  = 0;

            HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
                MODULEINFO mi;
                GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi));
                uintptr_t searchEnd = exeBase + mi.SizeOfImage;

                if (!g_domesticsHookAddr)
                    g_domesticsHookAddr = FindPattern(exeBase, searchEnd,
                        "44 0F 4E F7 0F B6 5C 24 60");

                AddLog("[DEBUG] domesticsHook: %p", (void*)g_domesticsHookAddr);

                if (g_domesticsHookAddr && !g_domesticsApplied) {
                    memcpy(g_domesticsOriginal, (void*)g_domesticsHookAddr, 9);
                    if (InstallDomesticsCave(g_domesticsHookAddr))
                        g_domesticsApplied = true;
                }

                AddLog("[DEBUG] domesticsCave applied: %d", g_domesticsApplied);
                g_domesticsRunning = false;
                return 0;
            }, nullptr, 0, nullptr);

            if (hThread) CloseHandle(hThread);

        } else {
            g_myPlayer = 0;
            g_myForce  = 0;

            if (g_domesticsApplied) {
                RestoreBytes(g_domesticsHookAddr, g_domesticsOriginal, 9);
                VirtualFree((LPVOID)g_domesticsCaveAddr, 0, MEM_RELEASE);
                g_domesticsCaveAddr  = 0;
                g_domesticsApplied   = false;
                g_domesticsHookAddr  = 0;
            }
        }
    }

    void SetDomesticsMultiplier(float playerMult, float forceMult) {
        g_playerMult = playerMult;
        g_forceMult  = forceMult;
        AddLog(u8"[내정배율] 플레이어: %.2f배, 세력: %.2f배", playerMult, forceMult);
    }

    void ResetDomesticsPlayer() {
        g_myPlayer = 0;
        g_myForce  = 0;
        AddLog(u8"[내정배율] 플레이어 주소 초기화");
    }

} // namespace DX11Base
