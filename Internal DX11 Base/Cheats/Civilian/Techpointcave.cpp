#include "../../pch.h"
#include "../War/Battleunitcapture.h"
#include "../War/Catapult.h"
#include "../War/Celestia.h"
#include "../../Cheats.h"
#include "../Social/Fastrelationship.h"
#include "../Social/Infinitetalk.h"
#include "../Social/InstantLoveCave.h"
#include "../Social/Loyaltycave.h"
#include "../Social/Resonancecave.h"
#include "../War/Selfheal.h"
#include "../../showlog.h"

#include <psapi.h>
#include <string>
#include <vector>

#include "Techpointcave.h"

namespace DX11Base {
    
        // ───────────────────────────────────────────────
        //  기술 포인트 베이스 캡처 cave
        //  원본: movzx eax, byte ptr [rbx+0x24A]  (7바이트)
        //  클릭 시 rbx(기술 베이스) 캡처
        // ───────────────────────────────────────────────
    
        uintptr_t g_capturedTechPAddr      = 0;
        std::atomic_bool g_techPThreadRunning{false};
    
        static uintptr_t g_techPHookAddr    = 0;
        static uint8_t   g_techPOriginal[7] = {};
        static uintptr_t g_techPCaveAddr    = 0;
        static bool      g_techPApplied     = false;
    
        static bool InstallTechPCave(uintptr_t hookAddr) {
            g_techPCaveAddr = AllocNear(hookAddr, 128);
            if (!g_techPCaveAddr) return false;
    
            uint8_t* cave = (uint8_t*)g_techPCaveAddr;
            int idx = 0;
    
            // mov rax, &g_capturedTechPAddr
            cave[idx++] = 0x48; cave[idx++] = 0xB8;
            *(uintptr_t*)&cave[idx] = (uintptr_t)&g_capturedTechPAddr; idx += 8;
            // mov [rax], rbx
            cave[idx++] = 0x48; cave[idx++] = 0x89; cave[idx++] = 0x18;
    
            // 원본: movzx eax, byte ptr [rbx+0x24A]
            uint8_t orig[] = { 0x0F, 0xB6, 0x83, 0x4A, 0x02, 0x00, 0x00 };
            memcpy(&cave[idx], orig, 7); idx += 7;
    
            // 복귀 점프
            uintptr_t retAddr = hookAddr + 7;
            cave[idx++] = 0xFF; cave[idx++] = 0x25;
            cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00;
            *(uintptr_t*)&cave[idx] = retAddr; idx += 8;
    
            return ApplyJmp(hookAddr, g_techPCaveAddr, 7);
        }
    
        void SetTechPCapture(bool enable) {
            uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
            if (!exeBase) return;
    
            if (enable) {
                if (g_techPApplied) return;
                if (g_techPThreadRunning.exchange(true)) return;
    
                HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
                    MODULEINFO mi;
                    GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi));
                    uintptr_t searchEnd = exeBase + mi.SizeOfImage;
    
                    if (!g_techPHookAddr) {
                        uintptr_t found = FindPattern(exeBase, searchEnd,
                            "0F B6 83 4A 02 00 00 38 44");
                        if (found) g_techPHookAddr = found;
                    }
    
                    AddLog("[DEBUG] techPHook: %p", (void*)g_techPHookAddr);
    
                    if (g_techPHookAddr && !g_techPApplied) {
                        memcpy(g_techPOriginal, (void*)g_techPHookAddr, 7);
                        if (InstallTechPCave(g_techPHookAddr))
                            g_techPApplied = true;
                    }
    
                    AddLog("[DEBUG] techPCave applied: %d", g_techPApplied);
                    g_techPThreadRunning.store(false);
                    return 0;
                }, nullptr, 0, nullptr);
    
                if (hThread) CloseHandle(hThread);
                else g_techPThreadRunning.store(false);
    
            } else {
                g_capturedTechPAddr = 0;
    
                if (g_techPApplied) {
                    RestoreBytes(g_techPHookAddr, g_techPOriginal, 7);
                    VirtualFree((LPVOID)g_techPCaveAddr, 0, MEM_RELEASE);
                    g_techPCaveAddr = 0;
                    g_techPApplied  = false;
                }
            }
        }
    
    } // namespace DX11Base
