#include "../../pch.h"
#include "Infinitetalk.h"
#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "InstantLoveCave.h"
#include "Loyaltycave.h"
#include "Resonancecave.h"
#include "../../showlog.h"

#include <psapi.h>
#include <TlHelp32.h>
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

    // ───────────────────────────────────────────────
    // 대련/토론 실제 +0x320 쓰기 추적 (디버그 전용)
    // 1) 검증된 기증 원본 명령에서 RSI(교류 상태 객체)를 캡처
    // 2) base+0x320에 x64 HW write breakpoint(DR3)를 설치
    // 3) 실제 쓰기 발생 시 다음 RIP와 변경된 raw 값을 기록
    // 게임 데이터 값 자체는 변경하지 않는다.
    // ───────────────────────────────────────────────
    namespace {
        static uintptr_t g_interactionBase = 0;
        static uintptr_t g_interactionWatchAddr = 0;

        static uintptr_t g_captureHookAddr = 0;
        static uintptr_t g_captureCaveAddr = 0;
        static uint8_t g_captureOriginal[10] = {};
        static bool g_captureApplied = false;

        static PVOID g_watchVeh = nullptr;
        static volatile LONG g_watchActive = 0;
        static volatile LONG g_watchReady = 0;
        static volatile LONG g_watchConfiguredThreads = 0;
        static volatile LONG g_watchHitCount = 0;

        struct InteractionWatchHit {
            DWORD threadId = 0;
            uintptr_t nextRip = 0;
            uint32_t raw = 0;
            uintptr_t rcx = 0;
            uintptr_t rdx = 0;
            uintptr_t rsi = 0;
            uintptr_t rdi = 0;
        };
        static InteractionWatchHit g_watchHits[16] = {};

        static const uint8_t kGiftOriginalBytes[10] = {
            0x81, 0x8E, 0x20, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00};

        static void RestoreInteractionCapture(bool clearBase) {
            if (g_captureApplied && g_captureHookAddr) {
                RestoreBytes(g_captureHookAddr, g_captureOriginal, 10);
                FlushInstructionCache(
                    GetCurrentProcess(), (LPCVOID)g_captureHookAddr, 10);
            }

            if (g_captureCaveAddr) {
                VirtualFree((LPVOID)g_captureCaveAddr, 0, MEM_RELEASE);
                g_captureCaveAddr = 0;
            }

            g_captureApplied = false;
            g_captureHookAddr = 0;

            if (clearBase) {
                g_interactionBase = 0;
                g_interactionWatchAddr = 0;
            }
        }

        static LONG CALLBACK InteractionWatchVeh(PEXCEPTION_POINTERS ep) {
#ifdef _M_X64
            if (!ep || !ep->ExceptionRecord || !ep->ContextRecord)
                return EXCEPTION_CONTINUE_SEARCH;

            if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
                return EXCEPTION_CONTINUE_SEARCH;

            CONTEXT* ctx = ep->ContextRecord;

            // DR3(B3)에서 발생했고 우리가 설치한 주소일 때만 처리.
            if ((ctx->Dr6 & 0x8ull) == 0 ||
                !g_interactionWatchAddr ||
                (uintptr_t)ctx->Dr3 != g_interactionWatchAddr) {
                return EXCEPTION_CONTINUE_SEARCH;
            }

            const LONG slot = InterlockedIncrement(&g_watchHitCount) - 1;
            if (slot >= 0 && slot < 16) {
                InteractionWatchHit& hit = g_watchHits[slot];
                hit.threadId = GetCurrentThreadId();
                hit.nextRip = (uintptr_t)ctx->Rip;
                hit.raw = *(volatile uint32_t*)g_interactionWatchAddr;
                hit.rcx = (uintptr_t)ctx->Rcx;
                hit.rdx = (uintptr_t)ctx->Rdx;
                hit.rsi = (uintptr_t)ctx->Rsi;
                hit.rdi = (uintptr_t)ctx->Rdi;
            }

            // 디버그 상태 비트만 정리. DR3/DR7 감시는 계속 유지된다.
            ctx->Dr6 &= ~0x8ull;
            return EXCEPTION_CONTINUE_EXECUTION;
#else
            return EXCEPTION_CONTINUE_SEARCH;
#endif
        }

        static bool ApplyDr3WatchToThread(DWORD threadId, bool enable) {
#ifdef _M_X64
            HANDLE hThread = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                    THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                FALSE,
                threadId);
            if (!hThread)
                return false;

            if (SuspendThread(hThread) == (DWORD)-1) {
                CloseHandle(hThread);
                return false;
            }

            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            bool ok = GetThreadContext(hThread, &ctx) != FALSE;

            if (ok) {
                if (enable) {
                    // 다른 도구/디버거가 이미 DR3을 사용 중이면 건드리지 않는다.
                    if ((ctx.Dr7 & ((1ull << 6) | (1ull << 7))) != 0) {
                        ok = false;
                    } else {
                        ctx.Dr3 = (DWORD64)g_interactionWatchAddr;

                        // L3=1, G3=0
                        ctx.Dr7 &= ~((1ull << 6) | (1ull << 7));
                        ctx.Dr7 |= (1ull << 6);

                        // RW3=01(write), LEN3=11(4 bytes)
                        ctx.Dr7 &= ~(0xFull << 28);
                        ctx.Dr7 |= (0xDull << 28);
                        ctx.Dr6 &= ~0x8ull;
                    }
                } else {
                    // 우리가 사용한 DR3 슬롯만 정리.
                    ctx.Dr7 &= ~((1ull << 6) | (1ull << 7));
                    ctx.Dr7 &= ~(0xFull << 28);
                    ctx.Dr3 = 0;
                    ctx.Dr6 &= ~0x8ull;
                }

                if (ok)
                    ok = SetThreadContext(hThread, &ctx) != FALSE;
            }

            ResumeThread(hThread);
            CloseHandle(hThread);
            return ok;
#else
            (void)threadId;
            (void)enable;
            return false;
#endif
        }

        struct WatchWorkerArgs {
            bool enable;
        };

        static DWORD WINAPI ConfigureInteractionWatchWorker(LPVOID param) {
            WatchWorkerArgs* args = (WatchWorkerArgs*)param;
            const bool enable = args ? args->enable : false;
            delete args;

            const DWORD pid = GetCurrentProcessId();
            const DWORD selfTid = GetCurrentThreadId();
            LONG configured = 0;

            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snap != INVALID_HANDLE_VALUE) {
                THREADENTRY32 te{};
                te.dwSize = sizeof(te);

                if (Thread32First(snap, &te)) {
                    do {
                        if (te.th32OwnerProcessID != pid ||
                            te.th32ThreadID == selfTid) {
                            continue;
                        }

                        if (ApplyDr3WatchToThread(te.th32ThreadID, enable))
                            ++configured;
                    } while (Thread32Next(snap, &te));
                }
                CloseHandle(snap);
            }

            if (enable) {
                InterlockedExchange(
                    &g_watchConfiguredThreads, configured);
                InterlockedExchange(&g_watchReady, 1);
            } else {
                InterlockedExchange(&g_watchReady, 0);
                InterlockedExchange(&g_watchConfiguredThreads, 0);

                if (g_watchVeh) {
                    RemoveVectoredExceptionHandler(g_watchVeh);
                    g_watchVeh = nullptr;
                }
            }

            return 0;
        }

        static void StartWatchWorker(bool enable) {
            WatchWorkerArgs* args = new WatchWorkerArgs{enable};
            HANDLE h = CreateThread(
                nullptr, 0, ConfigureInteractionWatchWorker, args, 0, nullptr);
            if (h) {
                CloseHandle(h);
            } else {
                delete args;
            }
        }
    }

    bool StartInteractionStateCaptureForWatch() {
        // 이전 진단 상태가 있으면 정리하고 새로 시작.
        InterlockedExchange(&g_watchActive, 0);
        InterlockedExchange(&g_watchHitCount, 0);
        ZeroMemory(g_watchHits, sizeof(g_watchHits));
        RestoreInteractionCapture(true);

        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase)
            return false;

        // 기증 무제한 패치와 충돌하지 않도록 정상 원본에서만 캡처한다.
        g_captureHookAddr = FindPattern(
            exeBase,
            exeBase + 0x3000000,
            "81 8E 20 03 00 00 00 04 00 00");

        if (!g_captureHookAddr) {
            const uintptr_t patched = FindPattern(
                exeBase,
                exeBase + 0x3000000,
                "81 A6 20 03 00 00 FF FB FF FF");

            if (patched) {
                AddLog(
                    u8"[쓰기감시DBG] '선물 기증 무제한'을 OFF한 뒤 다시 시작해주세요.");
            } else {
                AddLog(
                    u8"[쓰기감시DBG] 기증 원본 +0x320 코드를 찾지 못했습니다.");
            }
            g_captureHookAddr = 0;
            return false;
        }

        memcpy(g_captureOriginal, kGiftOriginalBytes, 10);

        g_captureCaveAddr = AllocNear(g_captureHookAddr, 128);
        if (!g_captureCaveAddr) {
            AddLog(u8"[쓰기감시DBG] 주소 캡처 cave 할당 실패.");
            g_captureHookAddr = 0;
            return false;
        }

        uint8_t* cave = (uint8_t*)g_captureCaveAddr;
        int i = 0;

        // push rax
        cave[i++] = 0x50;

        // mov rax, &g_interactionBase
        cave[i++] = 0x48;
        cave[i++] = 0xB8;
        *(uintptr_t*)&cave[i] = (uintptr_t)&g_interactionBase;
        i += 8;

        // mov [rax], rsi
        cave[i++] = 0x48;
        cave[i++] = 0x89;
        cave[i++] = 0x30;

        // pop rax
        cave[i++] = 0x58;

        // 기증 원본 명령은 그대로 수행.
        memcpy(&cave[i], kGiftOriginalBytes, 10);
        i += 10;

        const uintptr_t retAddr = g_captureHookAddr + 10;
        cave[i++] = 0xFF;
        cave[i++] = 0x25;
        cave[i++] = 0x00;
        cave[i++] = 0x00;
        cave[i++] = 0x00;
        cave[i++] = 0x00;
        *(uintptr_t*)&cave[i] = retAddr;
        i += 8;

        if (!ApplyJmp(g_captureHookAddr, g_captureCaveAddr, 10)) {
            VirtualFree((LPVOID)g_captureCaveAddr, 0, MEM_RELEASE);
            g_captureCaveAddr = 0;
            g_captureHookAddr = 0;
            AddLog(u8"[쓰기감시DBG] 주소 캡처 훅 설치 실패.");
            return false;
        }

        g_captureApplied = true;
        AddLog(
            u8"[쓰기감시DBG] 1단계 시작: 기증을 1회 실행한 뒤 '2. +0x320 쓰기 감시 시작'을 누르세요.");
        return true;
    }

    bool StartInteractionWriteWatch() {
        if (!g_interactionBase) {
            AddLog(
                u8"[쓰기감시DBG] 아직 교류 상태 객체가 캡처되지 않았습니다. 기증을 1회 실행해주세요.");
            return false;
        }

        // 기증 캡처 훅은 여기서 즉시 정상 원본으로 복구.
        RestoreInteractionCapture(false);

        g_interactionWatchAddr = g_interactionBase + 0x320;
        if (!IsValidPtr(g_interactionWatchAddr, sizeof(uint32_t))) {
            AddLog(
                u8"[쓰기감시DBG] 캡처한 base+0x320 주소가 유효하지 않습니다: %p",
                (void*)g_interactionWatchAddr);
            return false;
        }

        if (g_watchVeh) {
            RemoveVectoredExceptionHandler(g_watchVeh);
            g_watchVeh = nullptr;
        }

        g_watchVeh = AddVectoredExceptionHandler(1, InteractionWatchVeh);
        if (!g_watchVeh) {
            AddLog(u8"[쓰기감시DBG] VEH 설치 실패.");
            return false;
        }

        InterlockedExchange(&g_watchHitCount, 0);
        ZeroMemory(g_watchHits, sizeof(g_watchHits));
        InterlockedExchange(&g_watchReady, 0);
        InterlockedExchange(&g_watchActive, 1);

        StartWatchWorker(true);

        const uint32_t raw = *(const uint32_t*)g_interactionWatchAddr;
        AddLog(
            u8"[쓰기감시DBG] 감시 설치 중: base=%p addr=%p raw=0x%08X",
            (void*)g_interactionBase,
            (void*)g_interactionWatchAddr,
            raw);
        AddLog(
            u8"[쓰기감시DBG] 잠시 후 대련 1회, 토론 1회를 실행한 뒤 결과 확인을 누르세요.");
        return true;
    }

    void LogInteractionWriteWatchResults() {
        AddLog(
            u8"[쓰기감시DBG] ready=%ld / 감시 스레드=%ld / hit=%ld",
            g_watchReady,
            g_watchConfiguredThreads,
            g_watchHitCount);

        if (g_interactionWatchAddr &&
            IsValidPtr(g_interactionWatchAddr, sizeof(uint32_t))) {
            AddLog(
                u8"[쓰기감시DBG] 현재 +0x320 raw=0x%08X",
                *(const uint32_t*)g_interactionWatchAddr);
        }

        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        MODULEINFO mi{};
        uintptr_t exeEnd = 0;
        if (exeBase &&
            GetModuleInformation(
                GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi))) {
            exeEnd = exeBase + mi.SizeOfImage;
        }

        LONG count = g_watchHitCount;
        if (count > 16)
            count = 16;

        if (count <= 0) {
            AddLog(u8"[쓰기감시DBG] 아직 +0x320 쓰기 감지 없음.");
            return;
        }

        for (LONG n = 0; n < count; ++n) {
            const InteractionWatchHit& hit = g_watchHits[n];

            if (exeBase && hit.nextRip >= exeBase && hit.nextRip < exeEnd) {
                AddLog(
                    u8"[쓰기감시DBG] hit#%ld TID=%lu nextRIP=RVA:+0x%llX raw=0x%08X RCX=%p RDX=%p RSI=%p RDI=%p",
                    n + 1,
                    (unsigned long)hit.threadId,
                    (unsigned long long)(hit.nextRip - exeBase),
                    hit.raw,
                    (void*)hit.rcx,
                    (void*)hit.rdx,
                    (void*)hit.rsi,
                    (void*)hit.rdi);

                const uintptr_t start =
                    (hit.nextRip >= exeBase + 24)
                        ? hit.nextRip - 24
                        : exeBase;
                const size_t bytesLen = 48;

                if (IsValidPtr(start, bytesLen)) {
                    const uint8_t* pBytes = (const uint8_t*)start;
                    char line[48 * 3 + 1] = {};
                    size_t out = 0;
                    for (size_t k = 0;
                         k < bytesLen && out + 4 < sizeof(line);
                         ++k) {
                        out += (size_t)snprintf(
                            line + out,
                            sizeof(line) - out,
                            "%02X%s",
                            pBytes[k],
                            (k + 1 < bytesLen) ? " " : "");
                    }

                    AddLog(
                        u8"[쓰기감시DBG] hit#%ld bytes RVA:+0x%llX~+0x%llX (nextRIP는 시작+0x18) = %s",
                        n + 1,
                        (unsigned long long)(start - exeBase),
                        (unsigned long long)(start - exeBase + bytesLen - 1),
                        line);
                }
            } else {
                AddLog(
                    u8"[쓰기감시DBG] hit#%ld TID=%lu nextRIP=%p raw=0x%08X",
                    n + 1,
                    (unsigned long)hit.threadId,
                    (void*)hit.nextRip,
                    hit.raw);
            }
        }
    }

    void StopInteractionWriteWatch() {
        InterlockedExchange(&g_watchActive, 0);
        RestoreInteractionCapture(true);

        if (g_watchVeh) {
            // DR3 정리는 별도 worker에서 모든 기존 게임 스레드에 적용.
            StartWatchWorker(false);
        }

        AddLog(u8"[쓰기감시DBG] 쓰기 감시 종료/초기화 요청.");
    }

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
} // namespace DX11Base