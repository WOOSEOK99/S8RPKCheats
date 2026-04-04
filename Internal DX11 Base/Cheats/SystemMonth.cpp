#include "../pch.h"
#include "SystemMonth.h"
#include "../Cheats.h"
#include "../showlog.h"
#include "../MemoryUtils.h"
#include <windows.h>
#include <psapi.h>
#include <string>

namespace DX11Base {
    // ---------------------------------------------------------------------------
    // 글로벌 상태
    // ---------------------------------------------------------------------------
    static uintptr_t g_monthUIAddr         = 0;
    static bool      g_monthUICaptureRunning = false;
    static uintptr_t g_monthUIHookAddr     = 0;
    static uint8_t   g_monthUIOriginal[10] = {};
    static uintptr_t g_monthUICaveAddr     = 0;
    static bool      g_monthUIApplied      = false;

    // ---------------------------------------------------------------------------
    // InstallSystemMonthHook: 신규 AOB(10바이트 패턴) 기반 후킹 설치
    // ---------------------------------------------------------------------------
    static bool InstallMonthUICave(uintptr_t hookAddr) {
        g_monthUICaveAddr = AllocNear(hookAddr, 128);
        if (!g_monthUICaveAddr) return false;

        uint8_t* cave = (uint8_t*)g_monthUICaveAddr;
        int idx = 0;

        // push rbx
        cave[idx++] = 0x53;

        // lea rbx, [rax+0x6C]
        cave[idx++] = 0x48; cave[idx++] = 0x8D; cave[idx++] = 0x58; cave[idx++] = 0x6C;

        // g_monthUIAddr 저장 (절대주소 방식)
        // push rax
        cave[idx++] = 0x50;
        // mov rax, &g_monthUIAddr
        cave[idx++] = 0x48; cave[idx++] = 0xB8;
        *(uintptr_t*)&cave[idx] = (uintptr_t)&g_monthUIAddr; idx += 8;
        // mov [rax], rbx
        cave[idx++] = 0x48; cave[idx++] = 0x89; cave[idx++] = 0x18;
        // pop rax
        cave[idx++] = 0x58;

        // pop rbx
        cave[idx++] = 0x5B;

        // 원본 명령어 실행: mov [rax+0x6C], cl (88 48 6C)
        cave[idx++] = 0x88; cave[idx++] = 0x48; cave[idx++] = 0x6C;

        // 원본 명령어 실행: mov [r14], 0x00000001 (41 C7 06 01 00 00 00)
        cave[idx++] = 0x41; cave[idx++] = 0xC7; cave[idx++] = 0x06;
        cave[idx++] = 0x01; cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00;

        // 복귀 점프
        uintptr_t retAddr = hookAddr + 10;
        cave[idx++] = 0xFF; cave[idx++] = 0x25;
        cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00;
        *(uintptr_t*)&cave[idx] = retAddr; idx += 8;

        return ApplyJmp(hookAddr, g_monthUICaveAddr, 10);
    }

    void InstallSystemMonthHook() {
        if (g_monthUIApplied || g_monthUICaptureRunning) return;
        g_monthUICaptureRunning = true;

        HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
            MODULEINFO mi;
            GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi));
            uintptr_t searchEnd = exeBase + mi.SizeOfImage;

            if (!g_monthUIHookAddr)
                g_monthUIHookAddr = FindPattern(exeBase, searchEnd, "88 48 6C 41 C7 06 01 00 00 00");

            if (g_monthUIHookAddr && !g_monthUIApplied) {
                memcpy(g_monthUIOriginal, (void*)g_monthUIHookAddr, 10);
                if (InstallMonthUICave(g_monthUIHookAddr)) {
                    g_monthUIApplied = true;
                    AddLog(u8"[SystemMonth] 신규 AOB 월 감시 후크 설치 완료.");
                }
            }
            g_monthUICaptureRunning = false;
            return 0;
        }, nullptr, 0, nullptr);

        if (hThread) CloseHandle(hThread);
    }

    // ---------------------------------------------------------------------------
    // GetSystemMonthValue: 신규 AOB로 캡처된 주소에서 월 값 읽기
    // ---------------------------------------------------------------------------
    uint8_t GetSystemMonthValue() {
        if (!g_monthUIAddr || !IsValidPtr(g_monthUIAddr, 1)) return 0;
        return *(uint8_t*)g_monthUIAddr;
    }

} // namespace DX11Base
