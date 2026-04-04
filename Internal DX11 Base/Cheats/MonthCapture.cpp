#include "../pch.h"
#include "MonthCapture.h"
#include "../Cheats.h"
#include "../showlog.h"
#include "../MemoryUtils.h"
#include <psapi.h>
#include <string>

namespace DX11Base {

    // ---------------------------------------------------------------------------
    // 글로벌 상태 및 설정
    // ---------------------------------------------------------------------------
    bool bMonthCapture = true;       // 상설 기능화 (기본값 true)
    bool s_appMonthCapture = false;  // 현재 적용 여부

    // ---------------------------------------------------------------------------
    // GetSystemMonthValue: 기존의 5단계 포인터 체인 방식 (안전성 강화)
    // ---------------------------------------------------------------------------
    uint8_t GetSystemMonthValue() {
        static uintptr_t exeBase = 0;
        if (exeBase == 0) exeBase = (uintptr_t)GetModuleHandleA("SAN8RPK.exe");
        if (!exeBase) return 0;

        // 1단계: p1
        uintptr_t p1_ptr = exeBase + 0x034C8630;
        if (!IsValidPtr(p1_ptr, sizeof(uintptr_t))) return 0;
        uintptr_t p1 = *(uintptr_t*)p1_ptr;
        if (p1 < 0x10000) return 0;

        // 2단계: [p1 + 0]
        if (!IsValidPtr(p1 + 0, sizeof(uintptr_t))) return 0;
        uintptr_t p2 = *(uintptr_t*)(p1 + 0);
        if (p2 < 0x10000) return 0;

        // 3단계: [p2 + 8]
        if (!IsValidPtr(p2 + 8, sizeof(uintptr_t))) return 0;
        uintptr_t p3 = *(uintptr_t*)(p2 + 8);
        if (p3 < 0x10000) return 0;

        // 4단계: [p3 + 0x10]
        if (!IsValidPtr(p3 + 0x10, sizeof(uintptr_t))) return 0;
        uintptr_t p4 = *(uintptr_t*)(p3 + 0x10);
        if (p4 < 0x10000) return 0;

        // 5단계: [p4 + 0]
        if (!IsValidPtr(p4 + 0, sizeof(uintptr_t))) return 0;
        uintptr_t p5 = *(uintptr_t*)(p4 + 0);
        if (p5 < 0x10000) return 0;

        // 최종: [p5 + 0x49EC94]
        uintptr_t finalAddr = p5 + 0x49EC94;
        if (!IsValidPtr(finalAddr, sizeof(uint8_t))) return 0;

        return *(uint8_t*)finalAddr;
    }

    // ───────────────────────────────────────────────
    //  사용자 제공 로직: 월 주소 실시간 캡처
    // ───────────────────────────────────────────────

    uintptr_t g_realMonthAddr      = 0;  // 실제 월 데이터 주소
    static uintptr_t g_monthHookAddr    = 0;
    static uint8_t   g_monthOriginal[6] = { 0 };
    static uintptr_t g_monthCaveAddr    = 0;
    static bool      g_monthApplied     = false;
    static bool      g_monthCaptureRunning = false;
    static volatile bool g_stopScan     = false; // 스캔 중단 플래그

    static bool InstallMonthCave(uintptr_t hookAddr, uint32_t offset) {
        g_monthCaveAddr = AllocNear(hookAddr, 128);
        if (!g_monthCaveAddr) return false;

        uint8_t* cave = (uint8_t*)g_monthCaveAddr;
        int idx = 0;

        // push rax
        cave[idx++] = 0x50;

        // lea rax, [rsi + dynamic_offset] (48 8D 86 [4-byte offset])
        cave[idx++] = 0x48; cave[idx++] = 0x8D; cave[idx++] = 0x86;
        *(uint32_t*)&cave[idx] = offset; idx += 4;

        // mov [g_realMonthAddr], rax
        cave[idx++] = 0x48; cave[idx++] = 0xA3;
        *(uintptr_t*)&cave[idx] = (uintptr_t)&g_realMonthAddr; idx += 8;

        // pop rax
        cave[idx++] = 0x58;

        // 원본: mov [rsi + dynamic_offset], al (88 86 [4-byte offset])
        cave[idx++] = 0x88; cave[idx++] = 0x86;
        *(uint32_t*)&cave[idx] = offset; idx += 4;

        // 복귀 점프
        uintptr_t retAddr = hookAddr + 6;
        cave[idx++] = 0xFF; cave[idx++] = 0x25;
        cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00; cave[idx++] = 0x00;
        *(uintptr_t*)&cave[idx] = retAddr; idx += 8;

        return ApplyJmp(hookAddr, g_monthCaveAddr, 6);
    }

    void SetMonthCapture(bool enable) {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) return;

        if (enable) {
            if (g_monthCaptureRunning) return;
            g_monthCaptureRunning = true;
            g_stopScan = false;

            HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
                MODULEINFO mi;
                GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi));
                uintptr_t searchEnd = exeBase + mi.SizeOfImage;

                AddLog(u8"[MonthCapture] 월 캡처 검색 시작... (%p ~ %p)", (void*)exeBase, (void*)searchEnd);

                // 1. 정확한 패턴 검색 우선
                g_monthHookAddr = FindPattern(exeBase, searchEnd, "88 86 D2 72 00 00");

                // 2. 다중 매칭 순회 및 중단 체크
                if (!g_monthHookAddr) {
                    uintptr_t currentStart = exeBase;
                    while (currentStart < searchEnd && !g_stopScan) {
                        uintptr_t found = FindPattern(currentStart, searchEnd, "88 86 ? ? 00 00");
                        if (!found) break;

                        uint32_t offset = *(uint32_t*)(found + 2);
                        if (offset != 0x593) {
                            g_monthHookAddr = found;
                            break;
                        }
                        currentStart = found + 1;
                    }
                }

                if (g_monthHookAddr && !g_monthApplied && !g_stopScan) {
                    uint32_t detectedOffset = *(uint32_t*)(g_monthHookAddr + 2);
                    AddLog("[DEBUG] monthHook matched: %p (Offset: 0x%X)", (void*)g_monthHookAddr, detectedOffset);

                    memcpy(g_monthOriginal, (void*)g_monthHookAddr, 6);
                    if (InstallMonthCave(g_monthHookAddr, detectedOffset)) {
                        g_monthApplied = true;
                        AddLog(u8"[MonthCapture] 실시간 월 캡처 설치 완료.");
                    }
                }

                if (!g_monthApplied && !g_stopScan) {
                    AddLog(u8"[Error] 월 캡처 지점을 끝내 찾지 못했습니다.");
                }

                g_monthCaptureRunning = false;
                g_stopScan = false;
                return 0;
            }, nullptr, 0, nullptr);

            if (hThread) CloseHandle(hThread);

        } else {
            g_stopScan = true; // 스캔 중인 스레드가 있다면 중단 요청
            g_realMonthAddr = 0;

            if (g_monthApplied) {
                RestoreBytes(g_monthHookAddr, g_monthOriginal, 6);
                VirtualFree((LPVOID)g_monthCaveAddr, 0, MEM_RELEASE);
                g_monthCaveAddr  = 0;
                g_monthApplied   = false;
                g_monthHookAddr  = 0;
            }
        }
    }

    // 현재 월 값 읽기
    uint8_t GetCurrentMonth() {
        if (!g_realMonthAddr || !IsValidPtr(g_realMonthAddr, 1)) return 0;
        return *(uint8_t*)g_realMonthAddr;
    }

} // namespace DX11Base
