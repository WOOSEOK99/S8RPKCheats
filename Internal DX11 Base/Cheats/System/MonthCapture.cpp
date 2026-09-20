#include "../../pch.h"
#include "MonthCapture.h"
#include "../../Cheats.h"
#include "../../showlog.h"
#include "../../MemoryUtils.h"
#include <psapi.h>
#include <string>

namespace DX11Base {

    namespace {
        // 게임 모듈 내 인스턴스 포인터(고정) → 실제 데이터 블록
        constexpr uintptr_t kScenarioInstanceStaticOffset = 0x2E98BC8;
        constexpr uintptr_t kScenarioYearOffset = 0x72D0;
        // 월 후킹 패턴 mov [rsi+0x72D2], al 과 동일 오프셋
        constexpr uintptr_t kScenarioMonthOffset = 0x72D2;

        // 연·월 필드만 검사 (넓은 범위 IsValidPtr는 VirtualQuery 비용이 큼)
        uintptr_t ResolveScenarioDataCenter() {
            uintptr_t moduleBase = (uintptr_t)GetModuleHandle(NULL);
            if (!moduleBase)
                return 0;
            uintptr_t ptrAddr = moduleBase + kScenarioInstanceStaticOffset;
            if (!IsValidPtr(ptrAddr, sizeof(uintptr_t)))
                return 0;
            uintptr_t inst = *(uintptr_t *)ptrAddr;
            if (!inst || inst < 0x10000)
                return 0;
            uintptr_t yearAddr = inst + kScenarioYearOffset;
            uintptr_t monthAddr = inst + kScenarioMonthOffset;
            if (!IsValidPtr(yearAddr, 2) || !IsValidPtr(monthAddr, 1))
                return 0;
            return inst;
        }
    } // namespace

    uintptr_t GetScenarioDataCenterAddress() {
        return ResolveScenarioDataCenter();
    }

    bool TickInfiniteBanquet() {
        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;

        // 실게임 검증: 연회 실행 전/후 +0x71E4가 0x15 -> 0x17로 변하며
        // bit 1(0x02)만 연회 사용 완료 상태를 나타냄.
        constexpr uintptr_t kBanquetUsedFlagOffset = 0x71E4;
        const uintptr_t addr = dataCenter + kBanquetUsedFlagOffset;
        if (!IsValidPtr(addr, 1))
            return false;

        const uint8_t before = *(const uint8_t*)addr;
        if ((before & 0x02u) == 0)
            return true;

        DWORD oldProt = 0;
        if (!VirtualProtect((LPVOID)addr, 1, PAGE_READWRITE, &oldProt))
            return false;

        *(uint8_t*)addr = (uint8_t)(before & (uint8_t)~0x02u);

        DWORD tmp = 0;
        VirtualProtect((LPVOID)addr, 1, oldProt, &tmp);

        return ((*(const uint8_t*)addr & 0x02u) == 0);
    }

    bool ClearMediationUsedBitForTest() {
        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter) {
            AddLog(u8"[중개TEST] ScenarioDataCenter를 찾지 못했습니다.");
            return false;
        }

        // 실게임 진단:
        // 중개 전/후 GameBase(=ScenarioDataCenter) +0x71E5가
        // 0x00 -> 0x40으로 변함. 단일 0->1 bit 후보는 bit6 하나뿐.
        constexpr uintptr_t kMediationUsedFlagOffset = 0x71E5;
        constexpr uint8_t kMediationUsedBit = 0x40u;

        const uintptr_t addr = dataCenter + kMediationUsedFlagOffset;
        if (!IsValidPtr(addr, 1)) {
            AddLog(u8"[중개TEST] +0x71E5 주소가 유효하지 않습니다: %p",
                   (void*)addr);
            return false;
        }

        const uint8_t before = *(const uint8_t*)addr;
        const uint8_t after =
            (uint8_t)(before & (uint8_t)~kMediationUsedBit);

        DWORD oldProt = 0;
        if (!VirtualProtect((LPVOID)addr, 1, PAGE_READWRITE, &oldProt)) {
            AddLog(u8"[중개TEST] +0x71E5 쓰기 보호 해제 실패.");
            return false;
        }

        *(uint8_t*)addr = after;

        DWORD tmp = 0;
        VirtualProtect((LPVOID)addr, 1, oldProt, &tmp);

        const uint8_t readback = *(const uint8_t*)addr;
        AddLog(
            u8"[중개TEST] +0x71E5 bit6 해제: 0x%02X -> 0x%02X / readback=0x%02X",
            (unsigned int)before,
            (unsigned int)after,
            (unsigned int)readback);

        return readback == after;
    }

    void UpdateYear(unsigned short targetYear) {
        uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return;
        uintptr_t yearAddr = dataCenter + kScenarioYearOffset;
        DWORD oldProt, tmp;
        if (!VirtualProtect((LPVOID)yearAddr, 2, PAGE_READWRITE, &oldProt))
            return;
        *(unsigned short *)yearAddr = targetYear;
        VirtualProtect((LPVOID)yearAddr, 2, oldProt, &tmp);
    }

    void UpdateMonth(uint8_t targetMonth) {
        if (targetMonth < 1 || targetMonth > 12)
            return;
        uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return;
        uintptr_t monthAddr = dataCenter + kScenarioMonthOffset;
        DWORD oldProt, tmp;
        if (!VirtualProtect((LPVOID)monthAddr, 1, PAGE_READWRITE, &oldProt))
            return;
        *(uint8_t *)monthAddr = targetMonth;
        VirtualProtect((LPVOID)monthAddr, 1, oldProt, &tmp);
    }

    bool ReadScenarioYear(unsigned short *outYear) {
        if (!outYear)
            return false;
        uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;
        *outYear = *(unsigned short *)(dataCenter + kScenarioYearOffset);
        return true;
    }

    bool ReadScenarioMonth(uint8_t *outMonth) {
        if (!outMonth)
            return false;
        uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;
        *outMonth = *(uint8_t *)(dataCenter + kScenarioMonthOffset);
        return true;
    }

    bool ReadScenarioDate(unsigned short *outYear, uint8_t *outMonth) {
        if (!outYear && !outMonth)
            return false;
        uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter)
            return false;
        if (outYear)
            *outYear = *(unsigned short *)(dataCenter + kScenarioYearOffset);
        if (outMonth)
            *outMonth = *(uint8_t *)(dataCenter + kScenarioMonthOffset);
        return true;
    }

    // ---------------------------------------------------------------------------
    // 글로벌 상태 및 설정
    // ---------------------------------------------------------------------------
    bool bMonthCapture = true;       // 상설 기능화 (기본값 true)
    bool s_appMonthCapture = false;  // 현재 적용 여부

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
