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
        // 옛 CT의 실제 "무제한 <연회>" 코드가 +0x6EA4 bit 1을 사용했고,
        // 같은 CT의 현재년도 +0x6F78과의 차이는 0xD4였습니다.
        // 현재년도 +0x72D0에 같은 상대차이를 적용한 읽기 전용 후보입니다.
        constexpr uintptr_t kBanquetFlagCandidateOffset = kScenarioYearOffset - 0xD4; // 0x71FC
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

    namespace {
        constexpr uintptr_t kBanquetDiffStart = 0x6000;
        constexpr uintptr_t kBanquetDiffEnd   = 0x8000;
        constexpr size_t kBanquetDiffSize =
            (size_t)(kBanquetDiffEnd - kBanquetDiffStart);

        static uint8_t s_banquetBefore[kBanquetDiffSize] = {};
        static uintptr_t s_banquetBeforeBase = 0;
        static bool s_banquetBeforeCaptured = false;
    }

    bool CaptureBanquetDiffBaseline() {
        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter) {
            AddLog(u8"[연회DIFF] 시나리오 데이터 베이스를 찾지 못했습니다.");
            return false;
        }

        const uintptr_t start = dataCenter + kBanquetDiffStart;
        if (!IsValidPtr(start, kBanquetDiffSize)) {
            AddLog(u8"[연회DIFF] 비교 범위가 유효하지 않습니다. base=%p range=+0x%llX~+0x%llX",
                   (void*)dataCenter,
                   (unsigned long long)kBanquetDiffStart,
                   (unsigned long long)kBanquetDiffEnd);
            return false;
        }

        memcpy(s_banquetBefore, (const void*)start, kBanquetDiffSize);
        s_banquetBeforeBase = dataCenter;
        s_banquetBeforeCaptured = true;

        AddLog(u8"[연회DIFF] 연회 전 저장 완료. base=%p range=+0x%llX~+0x%llX size=0x%llX",
               (void*)dataCenter,
               (unsigned long long)kBanquetDiffStart,
               (unsigned long long)kBanquetDiffEnd,
               (unsigned long long)kBanquetDiffSize);
        return true;
    }

    bool ClearBanquetUsedBitForTest() {
        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter) {
            AddLog(u8"[연회TEST] 시나리오 데이터 베이스를 찾지 못했습니다.");
            return false;
        }

        constexpr uintptr_t kVerifiedBanquetFlagOffset = 0x71E4;
        const uintptr_t addr = dataCenter + kVerifiedBanquetFlagOffset;
        if (!IsValidPtr(addr, 1)) {
            AddLog(u8"[연회TEST] 연회 플래그 주소가 유효하지 않습니다. addr=%p",
                   (void*)addr);
            return false;
        }

        const uint8_t before = *(const uint8_t*)addr;
        const uint8_t after = (uint8_t)(before & (uint8_t)~0x02u);

        DWORD oldProt = 0;
        if (!VirtualProtect((LPVOID)addr, 1, PAGE_READWRITE, &oldProt)) {
            AddLog(u8"[연회TEST] VirtualProtect 실패. addr=%p", (void*)addr);
            return false;
        }

        *(uint8_t*)addr = after;

        DWORD tmp = 0;
        VirtualProtect((LPVOID)addr, 1, oldProt, &tmp);

        const uint8_t readback = *(const uint8_t*)addr;
        if (readback != after) {
            AddLog(u8"[연회TEST] 쓰기 검증 실패. +0x71E4 %02X -> 목표 %02X / 실제 %02X",
                   (unsigned int)before,
                   (unsigned int)after,
                   (unsigned int)readback);
            return false;
        }

        AddLog(u8"[연회TEST] +0x71E4 bit1 해제 완료: %02X -> %02X (다른 비트 유지)",
               (unsigned int)before,
               (unsigned int)readback);
        return true;
    }

    bool CompareBanquetDiffAfter() {
        if (!s_banquetBeforeCaptured) {
            AddLog(u8"[연회DIFF] 먼저 '연회 전 저장'을 눌러주세요.");
            return false;
        }

        const uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter) {
            AddLog(u8"[연회DIFF] 시나리오 데이터 베이스를 찾지 못했습니다.");
            return false;
        }
        if (dataCenter != s_banquetBeforeBase) {
            AddLog(u8"[연회DIFF] 베이스가 바뀌었습니다. 전=%p 후=%p. 다시 저장해주세요.",
                   (void*)s_banquetBeforeBase, (void*)dataCenter);
            return false;
        }

        const uintptr_t start = dataCenter + kBanquetDiffStart;
        if (!IsValidPtr(start, kBanquetDiffSize)) {
            AddLog(u8"[연회DIFF] 비교 범위가 유효하지 않습니다.");
            return false;
        }

        const uint8_t* now = (const uint8_t*)start;
        unsigned int changed = 0;
        unsigned int bit1Candidates = 0;
        unsigned int loggedChanged = 0;

        for (size_t i = 0; i < kBanquetDiffSize; ++i) {
            const uint8_t before = s_banquetBefore[i];
            const uint8_t after = now[i];
            if (before == after)
                continue;

            ++changed;
            const uintptr_t off = kBanquetDiffStart + i;

            if ((before & 0x02u) == 0 && (after & 0x02u) != 0) {
                ++bit1Candidates;
                AddLog(u8"[연회DIFF] bit1 후보 +0x%llX : %02X -> %02X",
                       (unsigned long long)off,
                       (unsigned int)before,
                       (unsigned int)after);
            } else if (loggedChanged < 40) {
                AddLog(u8"[연회DIFF] 변경 +0x%llX : %02X -> %02X",
                       (unsigned long long)off,
                       (unsigned int)before,
                       (unsigned int)after);
                ++loggedChanged;
            }
        }

        AddLog(u8"[연회DIFF] 비교 완료. 변경 바이트=%u / bit1 0->1 후보=%u",
               changed, bit1Candidates);
        return true;
    }

    bool ScanBanquetCodeCandidates() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) {
            AddLog(u8"[연회코드DBG] SAN8R.exe 베이스를 찾지 못했습니다.");
            return false;
        }

        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi))) {
            AddLog(u8"[연회코드DBG] 모듈 정보를 읽지 못했습니다.");
            return false;
        }
        const uintptr_t exeEnd = exeBase + mi.SizeOfImage;

        // 현재 프로젝트에서 실제 사용하는 담화/기증 원본 패턴.
        uintptr_t talk = FindPattern(exeBase, exeEnd, "83 8F 20 03 00 00 04");
        uintptr_t gift = FindPattern(exeBase, exeEnd, "81 8E 20 03 00 00 00 04 00 00");
        if (!gift)
            gift = FindPattern(exeBase, exeEnd, "81 A6 20 03 00 00 FF FB FF FF");

        // 옛 CT RVA:
        // banquet 0x1BEE365 / talk 0x1BF6DD5 / gift 0x1BF76A6
        // 따라서 banquet = talk - 0x8A70 = gift - 0x9341.
        uintptr_t predTalk = 0;
        uintptr_t predGift = 0;
        if (talk >= exeBase + 0x8A70)
            predTalk = talk - 0x8A70;
        if (gift >= exeBase + 0x9341)
            predGift = gift - 0x9341;

        AddLog(u8"[연회코드DBG] exe=%p size=0x%llX talk=%p(RVA:+0x%llX) gift=%p(RVA:+0x%llX)",
               (void*)exeBase,
               (unsigned long long)mi.SizeOfImage,
               (void*)talk,
               (unsigned long long)(talk ? talk - exeBase : 0),
               (void*)gift,
               (unsigned long long)(gift ? gift - exeBase : 0));
        AddLog(u8"[연회코드DBG] predicted talk기준=%p(RVA:+0x%llX) gift기준=%p(RVA:+0x%llX) 차이=0x%llX",
               (void*)predTalk,
               (unsigned long long)(predTalk ? predTalk - exeBase : 0),
               (void*)predGift,
               (unsigned long long)(predGift ? predGift - exeBase : 0),
               (unsigned long long)((predTalk && predGift)
                   ? (predTalk > predGift ? predTalk - predGift : predGift - predTalk)
                   : 0));

        if (!predTalk && !predGift) {
            AddLog(u8"[연회코드DBG] 담화/기증 기준점을 찾지 못해 상대거리 검색을 중단합니다.");
            return false;
        }

        uintptr_t centerLo = predTalk ? predTalk : predGift;
        uintptr_t centerHi = predGift ? predGift : predTalk;
        if (centerLo > centerHi) {
            const uintptr_t t = centerLo;
            centerLo = centerHi;
            centerHi = t;
        }

        // 예측 지점 주변만 좁게 검사. 읽기 전용.
        constexpr uintptr_t kWindow = 0x6000;
        uintptr_t scanStart = (centerLo > exeBase + kWindow) ? centerLo - kWindow : exeBase;
        uintptr_t scanEnd = centerHi + kWindow;
        if (scanEnd > exeEnd)
            scanEnd = exeEnd;

        auto logBytes = [&](const char* label, uintptr_t addr) {
            if (!addr || addr < exeBase || addr + 16 > exeEnd)
                return;
            const uint8_t* p = (const uint8_t*)addr;
            AddLog("[연회코드DBG] %s RVA:+0x%llX bytes=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                   label,
                   (unsigned long long)(addr - exeBase),
                   p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],
                   p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]);
        };
        logBytes("talk예측", predTalk);
        if (predGift != predTalk)
            logBytes("gift예측", predGift);

        unsigned int hitCount = 0;
        const uint8_t* b = (const uint8_t*)scanStart;
        const size_t len = (size_t)(scanEnd - scanStart);

        for (size_t i = 0; i + 10 <= len && hitCount < 40; ++i) {
            uintptr_t a = scanStart + i;

            // or dword ptr [reg+disp32], 02  : 83 /1 ... 02
            if (b[i] == 0x83 &&
                (b[i + 1] & 0xF8) == 0x88 &&
                b[i + 6] == 0x02) {
                const uint32_t disp = *(const uint32_t*)&b[i + 2];
                AddLog(u8"[연회코드DBG] OR-mem02 후보 RVA:+0x%llX disp=0x%X",
                       (unsigned long long)(a - exeBase), disp);
                ++hitCount;
                continue;
            }

            // or dword ptr [reg+disp32], 00000002 : 81 /1 ... 02 00 00 00
            if (b[i] == 0x81 &&
                (b[i + 1] & 0xF8) == 0x88 &&
                b[i + 6] == 0x02 && b[i + 7] == 0x00 &&
                b[i + 8] == 0x00 && b[i + 9] == 0x00) {
                const uint32_t disp = *(const uint32_t*)&b[i + 2];
                AddLog(u8"[연회코드DBG] OR-mem32-02 후보 RVA:+0x%llX disp=0x%X",
                       (unsigned long long)(a - exeBase), disp);
                ++hitCount;
                continue;
            }

            // and dword ptr [reg+disp32], FD : 83 /4 ... FD
            if (b[i] == 0x83 &&
                (b[i + 1] & 0xF8) == 0xA0 &&
                b[i + 6] == 0xFD) {
                const uint32_t disp = *(const uint32_t*)&b[i + 2];
                AddLog(u8"[연회코드DBG] AND-memFD 후보 RVA:+0x%llX disp=0x%X",
                       (unsigned long long)(a - exeBase), disp);
                ++hitCount;
                continue;
            }

            // 옛 setter 형태의 핵심: or eax,02 / and eax,FD.
            if (b[i] == 0x83 && b[i + 1] == 0xC8 && b[i + 2] == 0x02) {
                bool paired = false;
                const size_t pairEnd = (i + 0x100 < len) ? i + 0x100 : len - 2;
                for (size_t j = i + 3; j < pairEnd; ++j) {
                    if (b[j] == 0x83 && b[j + 1] == 0xE0 && b[j + 2] == 0xFD) {
                        paired = true;
                        break;
                    }
                }
                AddLog(u8"[연회코드DBG] OR-eax02 후보 RVA:+0x%llX paired_AND_FD=%u",
                       (unsigned long long)(a - exeBase), paired ? 1u : 0u);
                ++hitCount;
            }
        }

        AddLog(u8"[연회코드DBG] 검색범위 RVA:+0x%llX~+0x%llX / 후보 %u개",
               (unsigned long long)(scanStart - exeBase),
               (unsigned long long)(scanEnd - exeBase),
               hitCount);
        return true;
    }

    bool LogBanquetFlagCandidate() {
        uintptr_t dataCenter = ResolveScenarioDataCenter();
        if (!dataCenter) {
            AddLog(u8"[연회DBG] 시나리오 데이터 베이스를 찾지 못했습니다.");
            return false;
        }

        const uintptr_t candidateAddr = dataCenter + kBanquetFlagCandidateOffset;
        if (!IsValidPtr(candidateAddr, sizeof(uint32_t))) {
            AddLog(u8"[연회DBG] 후보 주소가 유효하지 않습니다. base=%p offset=+0x%llX addr=%p",
                   (void*)dataCenter,
                   (unsigned long long)kBanquetFlagCandidateOffset,
                   (void*)candidateAddr);
            return false;
        }

        const uint32_t raw = *(const uint32_t*)candidateAddr;
        const unsigned int banquetBit = (raw & 0x02u) ? 1u : 0u;

        AddLog(u8"[연회DBG] base=%p year=+0x%llX candidate=+0x%llX addr=%p raw=0x%08X bit1=%u",
               (void*)dataCenter,
               (unsigned long long)kScenarioYearOffset,
               (unsigned long long)kBanquetFlagCandidateOffset,
               (void*)candidateAddr,
               raw,
               banquetBit);
        return true;
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
