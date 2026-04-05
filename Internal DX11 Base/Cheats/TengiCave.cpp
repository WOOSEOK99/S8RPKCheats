#include "pch.h"
#include "TengiCave.h"
#include "Cheats.h"
#include "MemoryUtils.h"
#include "showlog.h"
#include <psapi.h>
#include <string>

namespace DX11Base {
    // ───────────────────────────────────────────────
    //  전기 주소 캡처 cave
    //  원본: mov [r15+0x1E4B18], al  (7바이트)
    //  r15+0x1E4B18 = 전기 주소를 g_tengiAddr에 저장
    // ───────────────────────────────────────────────

    uintptr_t g_tengiAddr       = 0;
    bool      g_tengiRunning    = false;

    static uintptr_t g_tengiHookAddr    = 0;
    static uint8_t   g_tengiOriginal[7] = {};
    static uintptr_t g_tengiCaveAddr    = 0;
    static bool      g_tengiApplied     = false;

    static bool InstallTengiCave(uintptr_t hookAddr) {
        g_tengiCaveAddr = AllocNear(hookAddr, 128);
        if (!g_tengiCaveAddr) return false;

        uint8_t* cave = (uint8_t*)g_tengiCaveAddr;
        int idx = 0;

        // push rax
        cave[idx++] = 0x50;

        // lea rax, [r15+0x1E4B18]
        // REX.W + REX.B = 0x49, ModRM = 0x87
        cave[idx++] = 0x49; cave[idx++] = 0x8D; cave[idx++] = 0x87;
        cave[idx++] = 0x18; cave[idx++] = 0x4B; cave[idx++] = 0x1E; cave[idx++] = 0x00;

        // mov [g_tengiAddr], rax
        cave[idx++] = 0x48; cave[idx++] = 0xA3;
        *(uintptr_t*)&cave[idx] = (uintptr_t)&g_tengiAddr; idx += 8;

        // pop rax
        cave[idx++] = 0x58;

        // 원본: mov [r15+0x1E4B18], al
        uint8_t orig[] = { 0x41, 0x88, 0x87, 0x18, 0x4B, 0x1E, 0x00 };
        memcpy(&cave[idx], orig, 7); idx += 7;

        // 복귀 점프
        uintptr_t retAddr = hookAddr + 7;
        cave[idx++] = 0xFF; cave[idx++] = 0x25;
        cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00;
        *(uintptr_t*)&cave[idx] = retAddr; idx += 8;

        return ApplyJmp(hookAddr, g_tengiCaveAddr, 7);
    }

    void SetTengiCapture(bool enable) {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) return;

        if (enable) {
            if (g_tengiApplied) return;
            if (g_tengiRunning) return;

            g_tengiRunning = true;

            HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
                MODULEINFO mi;
                GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi));
                uintptr_t searchEnd = exeBase + mi.SizeOfImage;

                if (!g_tengiHookAddr)
                    g_tengiHookAddr = FindPattern(exeBase, searchEnd,
                        "41 88 87 18 4B 1E 00");

                AddLog("[DEBUG] tengiHook: %p", (void*)g_tengiHookAddr);

                if (g_tengiHookAddr && !g_tengiApplied) {
                    memcpy(g_tengiOriginal, (void*)g_tengiHookAddr, 7);
                    if (InstallTengiCave(g_tengiHookAddr)) {
                        g_tengiApplied = true;
                        AddLog("[DEBUG] Tengi hook installed at: %p", (void*)g_tengiHookAddr);
                    }
                }

                AddLog("[DEBUG] tengiCave applied: %d", g_tengiApplied);
                g_tengiRunning = false;
                return 0;
            }, nullptr, 0, nullptr);

            if (hThread) CloseHandle(hThread);

        } else {
            g_tengiAddr = 0;

            if (g_tengiApplied) {
                RestoreBytes(g_tengiHookAddr, g_tengiOriginal, 7);
                VirtualFree((LPVOID)g_tengiCaveAddr, 0, MEM_RELEASE);
                g_tengiCaveAddr  = 0;
                g_tengiApplied   = false;
                g_tengiHookAddr  = 0;
            }
        }
    }

    uintptr_t GetTengiHookAddr() { return g_tengiHookAddr; }
    uintptr_t GetTengiHookOffset() { 
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        return g_tengiHookAddr ? (g_tengiHookAddr - exeBase) : 0; 
    }
    uintptr_t GetCapturedTengiAddr() { return g_tengiAddr; }

    // 전기 읽기
    uint8_t GetTengi() {
        if (!g_tengiAddr || !IsValidPtr(g_tengiAddr, 1)) return 0;
        return *(uint8_t*)g_tengiAddr;
    }

    // 전기 쓰기
    void SetTengi(uint8_t value) {
        if (!g_tengiAddr || !IsValidPtr(g_tengiAddr, 1)) return;
        
        // 전기 게이지 값 쓰기
        *(uint8_t*)g_tengiAddr = value;

        // 전기 발생 플래그 동기화 (오프셋 +0x18 -> xxA8)
        uintptr_t flagAddr = g_tengiAddr + 0x18;
        if (IsValidPtr(flagAddr, 1)) {
            if (value >= 100) {
                // [2026-04-05] 크래시 방지: +0x08과 +0x10에 이벤트 포인터가 들어왔을 때만 1 셋팅
                uintptr_t ptr1 = *(uintptr_t*)(g_tengiAddr + 0x08);
                uintptr_t ptr2 = *(uintptr_t*)(g_tengiAddr + 0x10);
                if (ptr1 != 0 && ptr2 != 0) {
                    *(uint8_t*)flagAddr = 1;
                }
            }
        }
    }

    // 전기 취소 (플래그 0 설정 및 할당된 이벤트 포인터 주소 초기화)
    void CancelTengi() {
        if (!g_tengiAddr || !IsValidPtr(g_tengiAddr + 0x18, 1)) return;

        // 1. 발생 플래그(+0x18)를 0으로 초기화
        uintptr_t flagAddr = g_tengiAddr + 0x18;
        *(uint8_t*)flagAddr = 0;

        // 2. 전기 이벤트 포인터 1 (+0x08): 유저 요청대로 포인터 0으로 초기화 (64비트 크기인 8바이트를 0으로 밀어 6바이트 모두 0 처리)
        if (IsValidPtr(g_tengiAddr + 0x08, 8)) {
            *(uint64_t*)(g_tengiAddr + 0x08) = 0;
        }

        // 3. 전기 이벤트 포인터 2 (+0x10): 유저 요청대로 포인터 0으로 초기화
        if (IsValidPtr(g_tengiAddr + 0x10, 8)) {
            *(uint64_t*)(g_tengiAddr + 0x10) = 0;
        }
    }

} // namespace DX11Base
