#include "../../pch.h"
#include "InstantLoveCave.h"
#include "../../Cheats.h"
#include "../../showlog.h"
#include "../../MemoryUtils.h"

#include <vector>
#include <string>
#include <psapi.h>

namespace DX11Base { 
    void AddLog(const char* fmt, ...);
    uintptr_t FindPattern(uintptr_t start, uintptr_t end, const std::string& pattern);
    extern bool bShowDebug;

    uintptr_t g_capturedLoveAddr = 0;

    // 디버그 모드에서 즉시 경애 필터 적용 전의 원본 관계값을 캡처한다.
    // 체크 ON 시점이 아니라 실제 hook1 실행(대화/관계 판정) 시점의 값을 본다.
    static volatile uintptr_t g_loveDebugCapturedAddr = 0;
    static volatile uint32_t g_loveDebugRawValue = 0;
    static volatile uint8_t g_loveDebugCaptureReady = 0;
    static uintptr_t g_loveDebugLastLoggedAddr = 0;

    void FlushLoveDebugCaptureLog() {
        if (!g_loveDebugCaptureReady)
            return;

        const uintptr_t addr = g_loveDebugCapturedAddr;
        const uint32_t raw = g_loveDebugRawValue & 0xFFu;
        g_loveDebugCaptureReady = 0;

        // 같은 관계 주소가 반복 실행되는 동안에는 한 번만 출력한다.
        // 다른 장수와 대화해 주소가 바뀌면 새 값을 다시 출력한다.
        if (addr == 0 || addr == g_loveDebugLastLoggedAddr)
            return;
        g_loveDebugLastLoggedAddr = addr;

        const bool over8 = raw > 8u;
        const bool bit08 = (raw & 0x08u) != 0;
        const bool normalPass = over8 && bit08;
        const bool hateIgnorePass = raw >= 0x63u;

        AddLog(u8"[LoveDebug] addr=%p raw=%u (0x%02X) >8=%s bit08=%s Normal=%s HateIgnore=%s",
               (void*)addr,
               (unsigned)raw,
               (unsigned)raw,
               over8 ? "YES" : "NO",
               bit08 ? "YES" : "NO",
               normalPass ? "PASS" : "BLOCK",
               hateIgnorePass ? "PASS" : "BLOCK");
    }

    // 상태 추가
    bool g_initThreadRunning = false;
    uint32_t g_dynOffset = 0;
    static LoveMode g_loveMode = LoveMode::Normal;

    // ───────────────────────────────────────────────
    //  경애 Cave 훅 전역 상태
    // ───────────────────────────────────────────────

    // [블록1] 친밀도 수치 훅 (rax+rbx+0xF0) - 화면 표시용
     uintptr_t g_hook1Addr = 0;
    static uint8_t   g_hook1Original[8] = {};
    static uintptr_t g_cave1Addr = 0;
     bool      g_cave1Applied = false;

    // [블록2] 관계 플래그 훅 (r8+rdi+0x68E0) - 저장 데이터 반영용
    static uintptr_t g_hook2Addr = 0;
    static uint8_t   g_hook2Original[11] = {};
    static uintptr_t g_cave2Addr = 0;
     bool      g_cave2Applied = false;

    // ───────────────────────────────────────────────
    //  블록1 Cave 설치
    //  원본: movzx eax, byte ptr [rax+rbx+F0]  (8바이트)
    // ───────────────────────────────────────────────

     static bool InstallCave1(uintptr_t hookAddr, LoveMode mode) {
         AddLog(u8"[DEBUG] Cave1 시작");

         g_cave1Addr = AllocNear(hookAddr, 512);
         AddLog(u8"[DEBUG] Cave1 AllocNear: %p", (void*)g_cave1Addr);
         if (!g_cave1Addr) return false;

         uint8_t* cave = (uint8_t*)g_cave1Addr;
         int idx = 0;
         int p1 = 0, p2 = 0;

         // 1. r10에 rcx 백업
         cave[idx++] = 0x4C; cave[idx++] = 0x8B; cave[idx++] = 0xD1;

         // 2. movzx ecx, byte ptr [rax+rbx+F0]
         cave[idx++] = 0x0F; cave[idx++] = 0xB6; cave[idx++] = 0x8C; cave[idx++] = 0x18;
         cave[idx++] = 0xF0; cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00;

         // 3. 디버그 모드일 때 현재 hook1 실행의 원본 관계값을 캡처한다.
         // 실제 대화/관계 판정 시점의 값을 보기 위해 매 실행마다 최신값으로 갱신한다.
         // 기존 게임 레지스터를 건드리지 않도록 r9/r11은 push/pop으로 보존한다.
         cave[idx++] = 0x41; cave[idx++] = 0x51; // push r9
         cave[idx++] = 0x41; cave[idx++] = 0x53; // push r11

         // mov r9, &bShowDebug
         cave[idx++] = 0x49; cave[idx++] = 0xB9;
         *(uintptr_t*)&cave[idx] = (uintptr_t)&bShowDebug; idx += 8;
         // cmp byte ptr [r9], 0 / je debug_skip
         cave[idx++] = 0x41; cave[idx++] = 0x80; cave[idx++] = 0x39; cave[idx++] = 0x00;
         cave[idx++] = 0x74; int pDebugSkip = idx; cave[idx++] = 0x00;

         // lea r11, [rax+rbx+F0]
         cave[idx++] = 0x4C; cave[idx++] = 0x8D; cave[idx++] = 0x9C; cave[idx++] = 0x18;
         cave[idx++] = 0xF0; cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00;

         // mov r9, &g_loveDebugCapturedAddr / mov [r9], r11
         cave[idx++] = 0x49; cave[idx++] = 0xB9;
         *(uintptr_t*)&cave[idx] = (uintptr_t)&g_loveDebugCapturedAddr; idx += 8;
         cave[idx++] = 0x4D; cave[idx++] = 0x89; cave[idx++] = 0x19;

         // mov r9, &g_loveDebugRawValue / mov [r9], ecx
         cave[idx++] = 0x49; cave[idx++] = 0xB9;
         *(uintptr_t*)&cave[idx] = (uintptr_t)&g_loveDebugRawValue; idx += 8;
         cave[idx++] = 0x41; cave[idx++] = 0x89; cave[idx++] = 0x09;

         // ready = 1
         cave[idx++] = 0x49; cave[idx++] = 0xB9;
         *(uintptr_t*)&cave[idx] = (uintptr_t)&g_loveDebugCaptureReady; idx += 8;
         cave[idx++] = 0x41; cave[idx++] = 0xC6; cave[idx++] = 0x01; cave[idx++] = 0x01;

         // debug_skip
         cave[pDebugSkip] = (uint8_t)(idx - pDebugSkip - 1);
         cave[idx++] = 0x41; cave[idx++] = 0x5B; // pop r11
         cave[idx++] = 0x41; cave[idx++] = 0x59; // pop r9

         // 4. 기존 캡처 — 필터 전에 먼저
         // lea r11, [rax+rbx+F0]
         cave[idx++] = 0x4C; cave[idx++] = 0x8D; cave[idx++] = 0x9C; cave[idx++] = 0x18;
         cave[idx++] = 0xF0; cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00;
         // mov [g_capturedLoveAddr], r11
         cave[idx++] = 0x49; cave[idx++] = 0xBB;
         *(uintptr_t*)&cave[idx] = (uintptr_t)&g_capturedLoveAddr; idx += 8;
         cave[idx++] = 0x4D; cave[idx++] = 0x89; cave[idx++] = 0x1B;


		 // 5. cmp ecx, 8 / jbe end
         cave[idx++] = 0x83; cave[idx++] = 0xF9; cave[idx++] = 0x08;
         cave[idx++] = 0x76; p1 = idx; cave[idx++] = 0x00;

         if (mode == LoveMode::Normal) {
             // and ecx, 8 필터
             cave[idx++] = 0x83; cave[idx++] = 0xE1; cave[idx++] = 0x08;
             cave[idx++] = 0x74; p2 = idx; cave[idx++] = 0x00;
             AddLog(u8"[DEBUG] Lovemode : normal");
         }
         else {
             // cmp ecx, 0x63 / jb 필터
             cave[idx++] = 0x83; cave[idx++] = 0xF9; cave[idx++] = 0x63;
             cave[idx++] = 0x72; p2 = idx; cave[idx++] = 0x00;
             AddLog(u8"[DEBUG] Lovemode : inhate");
         }

         // 6. mov ecx, 0x64
         cave[idx++] = 0xB9; cave[idx++] = 0x64; cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00;

         // 7. mov byte ptr [rax+rbx+F0], cl
         cave[idx++] = 0x88; cave[idx++] = 0x8C; cave[idx++] = 0x18;
         cave[idx++] = 0xF0; cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00;

         // 8. 저장용 플래그: mov byte ptr [rax+rbx+6AF48], cl
         cave[idx++] = 0x88; cave[idx++] = 0x8C; cave[idx++] = 0x18;
         *(uint32_t*)&cave[idx] = g_dynOffset; idx += 4;

         // 9. end 레이블
         cave[p1] = (uint8_t)(idx - p1 - 1);
         cave[p2] = (uint8_t)(idx - p2 - 1);

         // 10. 원본처럼 메모리에서 다시 읽어서 eax 반환
         cave[idx++] = 0x0F; cave[idx++] = 0xB6; cave[idx++] = 0x84; cave[idx++] = 0x18;
         cave[idx++] = 0xF0; cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00;

         // 11. rcx 복구
         cave[idx++] = 0x4C; cave[idx++] = 0x8B; cave[idx++] = 0xCA;

         // 12. 복귀 점프
         uintptr_t retAddr = hookAddr + 8;
         cave[idx++] = 0xFF; cave[idx++] = 0x25;
         cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00;
         *(uintptr_t*)&cave[idx] = retAddr; idx += 8;

         bool result = ApplyJmp(hookAddr, g_cave1Addr, 8);
         AddLog(u8"[DEBUG] Cave1 ApplyJmp: %d", result);
         return result;
     }

    // ───────────────────────────────────────────────
    //  블록2 Cave 설치
    //  원본: cmp byte ptr [r8+rdi+68E0], 64  (9바이트)
    //  저장 파일에 반영되는 관계 플래그를 수정
    // ───────────────────────────────────────────────

    static bool InstallCave2(uintptr_t hookAddr) {
        g_cave2Addr = AllocNear(hookAddr, 256);
        if (!g_cave2Addr) return false;

        // hookAddr+4 위치의 오프셋 4바이트를 동적으로 읽음
        //g_dynOffset = *(uint32_t*)(hookAddr + 3);

        //AddLog("[DEBUG] Cave2 dynOffset: %08X", g_dynOffset);

            // 바이트 덤프
        uint8_t* p = (uint8_t*)hookAddr;
        AddLog("[DEBUG] hook2 bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
            p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10]);

        //g_dynOffset = *(uint32_t*)(hookAddr + 3);
        g_dynOffset = *(uint32_t*)(hookAddr + 4);
        AddLog("[DEBUG] dynOffset from +3: %08X", g_dynOffset);


        uint8_t* cave = (uint8_t*)g_cave2Addr;
        int idx = 0;

        // cmp 9바이트 전체 복사
        memcpy(&cave[idx], (void*)hookAddr, 9); idx += 9;

        // jne → je로 교체
        cave[idx++] = 0x74; int pEnd = idx; cave[idx++] = 0x00;

        // 경애 아니면 0x64 써버림
        cave[idx++] = 0x42; cave[idx++] = 0xC6;
        cave[idx++] = 0x84; cave[idx++] = 0x07;
        *(uint32_t*)&cave[idx] = g_dynOffset; idx += 4;
        cave[idx++] = 0x64;

        // end2 레이블
        cave[pEnd] = (uint8_t)(idx - pEnd - 1);

        // 복귀 점프 → jne 다음인 hookAddr+11로
        uintptr_t retAddr = hookAddr + 11;  // 9(cmp) + 2(jne)

        cave[idx++] = 0xFF; cave[idx++] = 0x25;
        cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00;
        *(uintptr_t*)&cave[idx] = retAddr; idx += 8;

        return ApplyJmp(hookAddr, g_cave2Addr, 11);
    }


    // ───────────────────────────────────────────────
    //  SetInstantLoveCave (공개 API)
    // ───────────────────────────────────────────────


    void SetInstantLoveCave(bool enable, LoveMode mode) {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) return;

        if (enable) {
            if (g_cave1Applied && g_cave2Applied) return; // 이미 설치됨
            if (g_initThreadRunning) return;              // 이미 초기화 중

            // 새 활성화 세션에서는 첫 대화 대상부터 다시 로그가 나오도록 초기화한다.
            g_loveDebugCapturedAddr = 0;
            g_loveDebugRawValue = 0;
            g_loveDebugCaptureReady = 0;
            g_loveDebugLastLoggedAddr = 0;

            g_initThreadRunning = true;
            g_loveMode = mode;

            // 별도 스레드에서 패턴 스캔 + 훅 설치
            HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
                
				//모듈 정보 얻기
                MODULEINFO mi;
                GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi));
                uintptr_t searchEnd = exeBase + mi.SizeOfImage;

                AddLog(u8"[DEBUG] 스레드 시작");

                if (!g_hook2Addr)
                    g_hook2Addr = FindPattern(exeBase, searchEnd,
                        "41 80 ?? ?? ?? ?? ?? ?? ?? 75 ?? 48 8B ?? ?? ?? E8");

                AddLog(u8"[DEBUG] hook2: %p", (void*)g_hook2Addr);

                if (g_hook2Addr && !g_cave2Applied) {
                    memcpy(g_hook2Original, (void*)g_hook2Addr, 11);
                    if (InstallCave2(g_hook2Addr))
                        g_cave2Applied = true;
                }

                AddLog(u8"[DEBUG] cave2 applied: %d", g_cave2Applied);


                if (!g_hook1Addr)
                    g_hook1Addr = FindPattern(exeBase, exeBase + 0x3000000,
                        "0F B6 84 18 F0 00 00 00");

                AddLog(u8"[DEBUG] hook1: %p", (void*)g_hook1Addr);

                if (g_hook1Addr && !g_cave1Applied) {
                    memcpy(g_hook1Original, (void*)g_hook1Addr, 8);
                    if (InstallCave1(g_hook1Addr, g_loveMode))
                        g_cave1Applied = true;
                }

                AddLog(u8"[DEBUG] cave1 applied: %d", g_cave1Applied);
                AddLog(u8"[DEBUG] 스레드 종료");

                g_initThreadRunning = false;
                return 0;
                }, nullptr, 0, nullptr);

            if (hThread) CloseHandle(hThread);

        }
        else {
            g_loveDebugCaptureReady = 0;
            g_loveDebugLastLoggedAddr = 0;

            // 해제는 빠르므로 스레드 불필요
            if (g_cave1Applied) {
                RestoreBytes(g_hook1Addr, g_hook1Original, 8);
                VirtualFree((LPVOID)g_cave1Addr, 0, MEM_RELEASE);
                g_cave1Addr = 0;
                g_cave1Applied = false;
                g_hook1Addr = 0;  // ← 추가
                AddLog(u8"[DEBUG] cave1 해제됨");

            }
            if (g_cave2Applied) {
                RestoreBytes(g_hook2Addr, g_hook2Original, 11);
                VirtualFree((LPVOID)g_cave2Addr, 0, MEM_RELEASE);
                g_cave2Addr = 0;
                g_cave2Applied = false;
                AddLog(u8"[DEBUG] cave2 해제됨");

            }
        }
    }

    uintptr_t GetCapturedLoveAddr() {
        return g_capturedLoveAddr;
    }
    

}