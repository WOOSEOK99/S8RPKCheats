#include "../pch.h"
#include "SystemMonth.h"
#include "../Cheats.h"
#include "../showlog.h"
#include <windows.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>

namespace DX11Base {
    // ---------------------------------------------------------------------------
    // 글로벌 상태
    // ---------------------------------------------------------------------------
    static uintptr_t g_pRealMonthAddr = 0;   // 가로챈 월 값의 메모리 주소
    static uintptr_t g_returnAddr = 0;      // 훌쩍 뛰어넘어 돌아올 지점

    // ---------------------------------------------------------------------------
    // GetSystemMonthValue: 기존의 5단계 포인터 체인 방식 (변경 없음)
    // ---------------------------------------------------------------------------
    uint8_t GetSystemMonthValue() {
        static uintptr_t exeBase = 0;
        if (exeBase == 0) exeBase = (uintptr_t)GetModuleHandleA("SAN8RPK.exe");
        if (!exeBase) return 0;

        uintptr_t p1_ptr = exeBase + 0x034C8630;
        if (!IsValidPtr(p1_ptr, sizeof(uintptr_t))) return 0;
        uintptr_t p1 = *(uintptr_t*)p1_ptr;

        if (!IsValidPtr(p1 + 0, sizeof(uintptr_t))) return 0;
        uintptr_t p2 = *(uintptr_t*)(p1 + 0);

        if (!IsValidPtr(p2 + 8, sizeof(uintptr_t))) return 0;
        uintptr_t p3 = *(uintptr_t*)(p2 + 8);

        if (!IsValidPtr(p3 + 0x10, sizeof(uintptr_t))) return 0;
        uintptr_t p4 = *(uintptr_t*)(p3 + 0x10);

        if (!IsValidPtr(p4 + 0, sizeof(uintptr_t))) return 0;
        uintptr_t p5 = *(uintptr_t*)(p4 + 0);

        uintptr_t finalAddr = p5 + 0x49EC94;
        if (!IsValidPtr(finalAddr, sizeof(uint8_t))) return 0;

        return *(uint8_t*)finalAddr;
    }

    // ---------------------------------------------------------------------------
    // GetRealMonthValue: 가로챈 실시간 주소에서 월 값 읽기
    // ---------------------------------------------------------------------------
    uint8_t GetRealMonthValue() {
        if (!g_pRealMonthAddr || !IsValidPtr(g_pRealMonthAddr, 1)) return 0;
        return *(uint8_t*)g_pRealMonthAddr;
    }

    // ---------------------------------------------------------------------------
    // 주변 메모리 할당 (x64 RelJmp 2GB 제한 해결용)
    // ---------------------------------------------------------------------------
    static void* AllocateNearMemory(uintptr_t targetAddr, size_t size) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);

        uintptr_t minAddr = (targetAddr > 0x7FFFFFFF) ? (targetAddr - 0x7FFFF000) : (uintptr_t)si.lpMinimumApplicationAddress;
        uintptr_t maxAddr = (targetAddr < (0xFFFFFFFFFFFFFFFF - 0x7FFFFFFF)) ? (targetAddr + 0x7FFFF000) : (uintptr_t)si.lpMaximumApplicationAddress;

        // 타겟 주소로부터 위아래로 64KB씩 건너뛰며 빈 공간 탐색
        uintptr_t startAddr = targetAddr & ~0xFFFF; // 64KB 정렬

        for (uintptr_t offset = 0; offset < 0x7FFFF000; offset += 0x10000) {
            // 아래 방향 탐색
            uintptr_t tryAddr = startAddr - offset;
            if (tryAddr >= minAddr) {
                void* ptr = VirtualAlloc((LPVOID)tryAddr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (ptr) return ptr;
            }

            // 위 방향 탐색
            tryAddr = startAddr + offset;
            if (tryAddr <= maxAddr) {
                void* ptr = VirtualAlloc((LPVOID)tryAddr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
                if (ptr) return ptr;
            }
        }
        return nullptr;
    }

    // ---------------------------------------------------------------------------
    // InstallRealMonthHook: AOB 스캔 후 인라인 후킹 설치
    // ---------------------------------------------------------------------------
    void InstallRealMonthHook() {
        static bool s_installed = false;
        if (s_installed) return;

        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi))) return;
        uintptr_t imgEnd = exeBase + mi.SizeOfImage;

        // 1. AOB 스캔 (패턴: 88 86 D2 72 00 00 -> mov [rsi+72D2], al)
        const std::string pat = "88 86 D2 72 00 00";
        uintptr_t targetAddr = FindPattern(exeBase, imgEnd, pat);
        if (!targetAddr) {
            AddLog(u8"[Error] Real Month 패턴 스캔 실패!");
            return;
        }

        g_returnAddr = targetAddr + 6; // 명령어 크기(6)만큼 건너뜀

        // 2. 쉘코드 스튜브 할당 (타겟 주소 근처로 할당하여 2GB 제한 해결)
        uint8_t* stub = (uint8_t*)AllocateNearMemory(targetAddr, 128);
        if (!stub) {
            AddLog(u8"[Error] 타겟 주소 주변에 메모리 할당 실패!");
            return;
        }

        /*
        [x64 Shellcode Logic]
        50                                  | push rax
        48 8D 86 D2 72 00 00                | lea rax, [rsi + 72D2]
        48 A3 <AddressOf(g_pRealMonthAddr)> | mov [g_pRealMonthAddr], rax
        58                                  | pop rax
        88 86 D2 72 00 00                   | mov [rsi + 72D2], al (Original)
        FF 25 00 00 00 00                   | jmp [AbsoluteReturnAddr]
        <8-byte ReturnAddr>
        */

        size_t pos = 0;
        stub[pos++] = 0x50; // push rax
        
        // lea rax, [rsi + 72D2]
        stub[pos++] = 0x48; stub[pos++] = 0x8D; stub[pos++] = 0x86;
        stub[pos++] = 0xD2; stub[pos++] = 0x72; stub[pos++] = 0x00; stub[pos++] = 0x00;

        // mov abs [addr], rax (48 A3 ...)
        stub[pos++] = 0x48; stub[pos++] = 0xA3;
        uintptr_t targetPointerAddr = (uintptr_t)&g_pRealMonthAddr;
        memcpy(&stub[pos], &targetPointerAddr, 8);
        pos += 8;

        stub[pos++] = 0x58; // pop rax

        // 원본 명령어 복사 (mov [rsi+72D2], al)
        stub[pos++] = 0x88; stub[pos++] = 0x86; stub[pos++] = 0xD2;
        stub[pos++] = 0x72; stub[pos++] = 0x00; stub[pos++] = 0x00;

        // jmp [rip + 0]
        stub[pos++] = 0xFF; stub[pos++] = 0x25;
        stub[pos++] = 0x00; stub[pos++] = 0x00; stub[pos++] = 0x00; stub[pos++] = 0x00;
        memcpy(&stub[pos], &g_returnAddr, 8);
        pos += 8;

        // 3. 대상 지점에 JMP 패치 (5바이트 JMP + 1바이트 NOP)
        // 주의: x64에서 5바이트 JMP는 +-2GB 범위 내에서만 가능함. 
        // VirtualAlloc은 보통 가까운 곳에 할당되지만, 멀리 있다면 14바이트 JMP 사용 필요.
        int64_t relativeOffset = (int64_t)stub - (int64_t)targetAddr - 5;
        
        if (relativeOffset > 2147483647LL || relativeOffset < -2147483648LL) {
            // 거리가 너무 멂 -> 14바이트 패치가 필요하지만 원본이 6바이트라 공간 부족
            // 이 경우 MinHook을 쓰는 것이 더 안전함. 하지만 여기선 일단 실행.
            AddLog(u8"[Error] 후킹 거리가 너무 멉니다 (x64 RelJmp Limit)");
            VirtualFree(stub, 0, MEM_RELEASE);
            return;
        }

        DWORD oldProt;
        VirtualProtect((LPVOID)targetAddr, 6, PAGE_EXECUTE_READWRITE, &oldProt);
        
        *(uint8_t*)targetAddr = 0xE9; // JMP rel32
        *(int32_t*)(targetAddr + 1) = (int32_t)relativeOffset;
        *(uint8_t*)(targetAddr + 5) = 0x90; // NOP
        
        VirtualProtect((LPVOID)targetAddr, 6, oldProt, &oldProt);

        s_installed = true;
        AddLog(u8"[SystemMonth] 실시간 월 감시 후크 설치 완료 (AOB 스캔 성공)");
    }
}
