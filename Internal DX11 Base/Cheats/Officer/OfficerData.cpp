#include "OfficerData.h"
#include "OfficerRosterResolve.h"
#include "../System/MonthCapture.h"
#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"
#include "../../EmbeddedJsonResources.h"
#include "pch.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <psapi.h>
#include <iostream>
#include <winnls.h>
#include <algorithm>
#include <map>

namespace DX11Base {

    // Unicode Normalization Form C (NFC) 로 변환하는 유틸리티
    std::string NormalizeUtf8(const std::string& str) {
        if (str.empty()) return "";
        
        // UTF-8 -> Wide
        int wlen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        if (wlen <= 0) return str;
        std::wstring wstr(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], wlen);

        // Normalize to NFC
        int nlen = NormalizeString(NormalizationC, wstr.c_str(), -1, NULL, 0);
        if (nlen <= 0) return str;
        std::wstring nstr(nlen, 0);
        NormalizeString(NormalizationC, wstr.c_str(), -1, &nstr[0], nlen);

        // Wide -> UTF-8
        int u8len = WideCharToMultiByte(CP_UTF8, 0, nstr.c_str(), -1, NULL, 0, NULL, NULL);
        if (u8len <= 0) return str;
        std::string u8str(u8len, 0);
        WideCharToMultiByte(CP_UTF8, 0, nstr.c_str(), -1, &u8str[0], u8len, NULL, NULL);
        
        size_t actualLen = strlen(u8str.c_str());
        u8str.resize(actualLen);
        return u8str;
    }

    extern HMODULE g_hModule;

    std::unordered_map<int, std::string> g_officerNames;
    bool g_namesLoaded = false;
    std::unordered_map<int, EffectDef> g_effectDefs;
    bool g_effectsLoaded = false;

    bool g_autoAffinityGrowthEnabled = false;

    namespace {
        uintptr_t g_autoAffinityLastDataCenter = 0;
        uintptr_t g_autoAffinityLastProtagonist = 0;
        unsigned short g_autoAffinityLastYear = 0;
        uint8_t g_autoAffinityLastMonth = 0;
    }

    RosterStats SafeReadRosterStats(uintptr_t targetBase) {
        RosterStats stats = { false };
        __try {
            stats.id_08 = *(unsigned short*)(targetBase + 0x08);
            stats.id_2e = *(unsigned short*)(targetBase + 0x2E);
            stats.gold = *(unsigned int*)(targetBase + 0xE8);
            stats.merit = *(unsigned short*)(targetBase + 0x100);
            stats.repI = *(unsigned short*)(targetBase + 0x108);
            stats.sp = *(unsigned char*)(targetBase + 0xED);
            stats.valid = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            stats.valid = false;
        }
        return stats;
    }


    namespace {
        constexpr uintptr_t kCurrentSynergeticPtrOffset = 0x547330;
        constexpr uintptr_t kCurrentRelationshipPtrOffset = 0x57AE38;
        constexpr uintptr_t kLegacySynergeticPtrOffset = 0x433210;
        constexpr uintptr_t kLegacyRelationshipPtrOffset = 0x462BA8;

        bool SafeRelRead8(uintptr_t addr, uint8_t* out) {
            if (!out) return false;
            __try {
                *out = *(uint8_t*)addr;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                *out = 0;
                return false;
            }
        }

        bool SafeRelRead16(uintptr_t addr, uint16_t* out) {
            if (!out) return false;
            __try {
                *out = *(uint16_t*)addr;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                *out = 0;
                return false;
            }
        }

        bool SafeRelReadPtr(uintptr_t addr, uintptr_t* out) {
            if (!out) return false;
            __try {
                *out = *(uintptr_t*)addr;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                *out = 0;
                return false;
            }
        }

        bool IsRosterOfficerPtr(uintptr_t ptr, uintptr_t rosterBase) {
            if (ptr < rosterBase)
                return false;
            const uintptr_t delta = ptr - rosterBase;
            if (delta >= (uintptr_t)5102 * 0x3D0)
                return false;
            return (delta % 0x3D0) == 0;
        }

        bool ReadOfficerIdFromRosterPtr(uintptr_t ptr, uintptr_t rosterBase, uint16_t* outId) {
            if (!outId || !IsRosterOfficerPtr(ptr, rosterBase))
                return false;
            uint16_t id = 0;
            if (!SafeRelRead16(ptr + 0x08, &id) || id < 1 || id > 5102)
                return false;
            *outId = id;
            return true;
        }

        int ScoreSynergeticTable(uintptr_t base, uintptr_t rosterBase) {
            if (base <= 0x10000 || rosterBase <= 0x10000)
                return -1;

            int valid = 0;
            int invalid = 0;
            for (int i = 0; i < 600; ++i) {
                const uintptr_t slot = base + (uintptr_t)i * 0x20;
                uint8_t relation = 0;
                if (!SafeRelRead8(slot + 0x18, &relation))
                    return -1;
                if (relation == 0)
                    continue;
                if (relation != 1 && relation != 2) {
                    if (++invalid > 3)
                        return -1;
                    continue;
                }

                uintptr_t p1 = 0, p2 = 0;
                if (!SafeRelReadPtr(slot + 0x08, &p1) ||
                    !SafeRelReadPtr(slot + 0x10, &p2) ||
                    !IsRosterOfficerPtr(p1, rosterBase) ||
                    !IsRosterOfficerPtr(p2, rosterBase)) {
                    if (++invalid > 3)
                        return -1;
                    continue;
                }
                ++valid;
            }
            return valid;
        }

        int ScoreRelationshipTable(uintptr_t base, uintptr_t rosterBase) {
            if (base <= 0x10000 || rosterBase <= 0x10000)
                return -1;

            int valid = 0;
            int invalid = 0;
            for (int i = 0; i < 128; ++i) {
                const uintptr_t slot = base + (uintptr_t)i * 0x40;
                uint8_t relation = 0;
                if (!SafeRelRead8(slot + 0x08, &relation))
                    return -1;
                if (relation == 0)
                    continue;
                if (relation < 1 || relation > 4) {
                    if (++invalid > 3)
                        return -1;
                    continue;
                }

                uintptr_t first = 0, second = 0;
                if (!SafeRelReadPtr(slot + 0x10, &first) ||
                    !SafeRelReadPtr(slot + 0x18, &second) ||
                    !IsRosterOfficerPtr(first, rosterBase) ||
                    !IsRosterOfficerPtr(second, rosterBase)) {
                    if (++invalid > 3)
                        return -1;
                    continue;
                }
                ++valid;
            }
            return valid;
        }

        bool IsReadableRelationshipScanRegion(const MEMORY_BASIC_INFORMATION& mbi) {
            if (mbi.State != MEM_COMMIT)
                return false;
            if (mbi.Protect & PAGE_GUARD)
                return false;
            return (mbi.Protect & 0xFF) != PAGE_NOACCESS;
        }

        bool TryResolveSynergeticTable(uintptr_t gameBase, uintptr_t rosterBase, uintptr_t* outBase) {
            if (!outBase) return false;
            *outBase = 0;

            static uintptr_t s_cachedGameBase = 0;
            static uintptr_t s_cachedRosterBase = 0;
            static uintptr_t s_cachedTableBase = 0;
            if (s_cachedGameBase == gameBase &&
                s_cachedRosterBase == rosterBase &&
                s_cachedTableBase > 0x10000 &&
                ScoreSynergeticTable(s_cachedTableBase, rosterBase) >= 1) {
                *outBase = s_cachedTableBase;
                return true;
            }

            const uintptr_t offsets[] = {
                kCurrentSynergeticPtrOffset,
                kLegacySynergeticPtrOffset
            };
            for (uintptr_t off : offsets) {
                uintptr_t ptr = 0;
                if (!SafeRelReadPtr(gameBase + off, &ptr) || ptr <= 0x10000)
                    continue;
                const uintptr_t candidate = ptr + 0xA0;
                if (ScoreSynergeticTable(candidate, rosterBase) >= 1) {
                    s_cachedGameBase = gameBase;
                    s_cachedRosterBase = rosterBase;
                    s_cachedTableBase = candidate;
                    *outBase = candidate;
                    return true;
                }
            }

            // 고정 오프셋이 바뀐 빌드/상태에서는 디버그에서 검증된 구조 스캔 방식으로 찾는다.
            const uintptr_t scanEnd = gameBase + 0x800000;
            uintptr_t cursor = gameBase;
            while (cursor < scanEnd) {
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery((LPCVOID)cursor, &mbi, sizeof(mbi)) != sizeof(mbi))
                    break;

                uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
                uintptr_t regionEnd = regionStart + mbi.RegionSize;
                if (regionEnd <= cursor)
                    break;
                if (regionStart < gameBase)
                    regionStart = gameBase;
                if (regionEnd > scanEnd)
                    regionEnd = scanEnd;

                if (IsReadableRelationshipScanRegion(mbi)) {
                    uintptr_t addr = (regionStart + 7) & ~(uintptr_t)7;
                    for (; addr + 8 <= regionEnd; addr += 8) {
                        uintptr_t ptr = 0;
                        if (!SafeRelReadPtr(addr, &ptr) || ptr <= 0x10000)
                            continue;

                        const uintptr_t candidate = ptr + 0xA0;
                        if (ScoreSynergeticTable(candidate, rosterBase) < 1)
                            continue;

                        s_cachedGameBase = gameBase;
                        s_cachedRosterBase = rosterBase;
                        s_cachedTableBase = candidate;
                        *outBase = candidate;
                        return true;
                    }
                }
                cursor = regionEnd;
            }
            return false;
        }

        bool TryResolveRelationshipTable(uintptr_t gameBase, uintptr_t rosterBase, uintptr_t* outBase) {
            if (!outBase) return false;
            *outBase = 0;

            const uintptr_t offsets[] = {
                kCurrentRelationshipPtrOffset,
                kLegacyRelationshipPtrOffset
            };
            for (uintptr_t off : offsets) {
                uintptr_t ptr = 0;
                if (!SafeRelReadPtr(gameBase + off, &ptr) || ptr <= 0x10080)
                    continue;

                const uintptr_t biases[] = {0x80, 0xC0};
                for (uintptr_t bias : biases) {
                    const uintptr_t candidate = ptr - bias;
                    if (ScoreRelationshipTable(candidate, rosterBase) >= 1) {
                        *outBase = candidate;
                        return true;
                    }
                }
            }
            return false;
        }

        void AddUniqueRelationshipId(std::vector<uint16_t>& values, uint16_t id) {
            if (id == 0)
                return;
            if (std::find(values.begin(), values.end(), id) == values.end())
                values.push_back(id);
        }

        bool ContainsRelationshipId(const OfficerRelationshipInfo& info, uint16_t id) {
            const std::vector<uint16_t>* groups[] = {
                &info.swornBrothers,
                &info.spouses,
                &info.enemies,
                &info.rivals
            };
            for (const auto* group : groups) {
                if (std::find(group->begin(), group->end(), id) != group->end())
                    return true;
            }
            return false;
        }

        constexpr uintptr_t kCurrentAffinityBaseOffset = 0x24206;
        constexpr int kAffinityOfficerCount = 1650;

        static uintptr_t g_affinityWriteHookAddr = 0;
        static uintptr_t g_affinityWriteCaveAddr = 0;
        static uint8_t g_affinityWriteOriginal[8] = {};
        static bool g_affinityWriteHookInstalled = false;
        static volatile uintptr_t g_affinityExpectedWriteAddr = 0;
        static volatile uintptr_t g_affinityCapturedWriteAddr = 0;
        static volatile uint8_t g_affinityCapturedWriteValue = 0;
        static volatile uint8_t g_affinityWriteCaptured = 0;

        void EmitAffinityAbsoluteReturn(
            uint8_t* cave, int& cur,
            uintptr_t returnAddr) {
            cave[cur++] = 0xFF;
            cave[cur++] = 0x25;
            *(uint32_t*)&cave[cur] = 0;
            cur += 4;
            *(uintptr_t*)&cave[cur] = returnAddr;
            cur += 8;
        }

        bool InstallAffinityWriteProbeHook() {
            if (g_affinityWriteHookInstalled)
                return true;

            const uintptr_t moduleBase =
                (uintptr_t)GetModuleHandle(nullptr);
            if (!moduleBase) {
                AddLog(u8"[친밀쓰기DBG] 모듈 베이스 확인 실패");
                return false;
            }

            MODULEINFO mi{};
            if (!GetModuleInformation(
                    GetCurrentProcess(),
                    (HMODULE)moduleBase,
                    &mi, sizeof(mi))) {
                AddLog(u8"[친밀쓰기DBG] 모듈 범위 확인 실패");
                return false;
            }

            const uintptr_t moduleEnd =
                moduleBase + mi.SizeOfImage;

            // 현재 SAN8RPK.CT ID 300이 실제로 사용하는 AOB를 기준점으로 잡는다.
            // CT 하단의 디스어셈블리 주석은 빌드에 따라 달라질 수 있으므로
            // 전체 주변 바이트를 고정 패턴으로 사용하지 않는다.
            const uintptr_t ct300Anchor =
                FindPattern(
                    moduleBase, moduleEnd,
                    "41 03 C0 41 3B C6 41");

            uintptr_t hookAddr = 0;
            uint32_t affinityWriteDisp = 0;

            // ID300 기준점에서 가까운 범위의
            // mov [rdx+r9+disp32],cl (42 88 8C 0A xx xx xx xx)를 찾는다.
            if (ct300Anchor) {
                const uintptr_t localEnd =
                    (std::min)(
                        moduleEnd,
                        ct300Anchor + (uintptr_t)0x100);
                for (uintptr_t p = ct300Anchor;
                     p + 8 <= localEnd; ++p) {
                    const uint8_t* code =
                        (const uint8_t*)p;
                    if (code[0] == 0x42 &&
                        code[1] == 0x88 &&
                        code[2] == 0x8C &&
                        code[3] == 0x0A) {
                        hookAddr = p;
                        memcpy(
                            &affinityWriteDisp,
                            code + 4,
                            sizeof(affinityWriteDisp));
                        break;
                    }
                }
            }

            // CT ID300이 외부에서 이미 활성화되어 기준 AOB가 JMP로 바뀐 경우나
            // 함수 주변 명령 배치가 달라진 경우를 위한 폴백.
            if (!hookAddr) {
                const uintptr_t fallback =
                    FindPattern(
                        moduleBase, moduleEnd,
                        "42 88 8C 0A ? ? ? ?");
                if (fallback) {
                    hookAddr = fallback;
                    memcpy(
                        &affinityWriteDisp,
                        (const void*)(fallback + 4),
                        sizeof(affinityWriteDisp));
                }
            }

            if (!hookAddr) {
                AddLog(
                    u8"[친밀쓰기DBG] 현재 CT ID300 친밀 저장 명령을 찾지 못했습니다. anchor=%s",
                    ct300Anchor ? "FOUND" : "NOT_FOUND");
                return false;
            }

            static const uint8_t kWritePrefix[4] = {
                0x42, 0x88, 0x8C, 0x0A
            };
            if (memcmp(
                    (const void*)hookAddr,
                    kWritePrefix,
                    sizeof(kWritePrefix)) != 0) {
                AddLog(
                    u8"[친밀쓰기DBG] 저장 명령 검증 실패: hook=0x%llX",
                    (unsigned long long)hookAddr);
                return false;
            }

            AddLog(
                u8"[친밀쓰기DBG] CT300 저장 명령 확인: anchor=0x%llX / hook=0x%llX / disp=0x%X",
                (unsigned long long)ct300Anchor,
                (unsigned long long)hookAddr,
                (unsigned int)affinityWriteDisp);

            g_affinityWriteCaveAddr =
                AllocNear(hookAddr, 256);
            if (!g_affinityWriteCaveAddr) {
                AddLog(u8"[친밀쓰기DBG] 저장 명령 cave 할당 실패");
                return false;
            }

            memcpy(
                g_affinityWriteOriginal,
                (const void*)hookAddr,
                sizeof(g_affinityWriteOriginal));

            uint8_t* cave =
                (uint8_t*)g_affinityWriteCaveAddr;
            int cur = 0;

            // 원본 mov는 flags를 건드리지 않으므로 비교용 flags와
            // 임시 레지스터를 모두 보존한다.
            cave[cur++] = 0x9C;                         // pushfq
            cave[cur++] = 0x50;                         // push rax
            cave[cur++] = 0x41; cave[cur++] = 0x53;   // push r11

            // lea rax,[rdx+r9+disp32]
            cave[cur++] = 0x4A;
            cave[cur++] = 0x8D;
            cave[cur++] = 0x84;
            cave[cur++] = 0x0A;
            *(uint32_t*)&cave[cur] =
                affinityWriteDisp;
            cur += 4;

            // r11 = &g_affinityExpectedWriteAddr
            cave[cur++] = 0x49;
            cave[cur++] = 0xBB;
            *(uintptr_t*)&cave[cur] =
                (uintptr_t)&g_affinityExpectedWriteAddr;
            cur += 8;

            // cmp rax,[r11]
            cave[cur++] = 0x49;
            cave[cur++] = 0x3B;
            cave[cur++] = 0x03;

            // jne skipCapture
            cave[cur++] = 0x0F;
            cave[cur++] = 0x85;
            const int jneSkipPos = cur;
            cur += 4;

            // capturedAddress = rax
            cave[cur++] = 0x48;
            cave[cur++] = 0xA3;
            *(uintptr_t*)&cave[cur] =
                (uintptr_t)&g_affinityCapturedWriteAddr;
            cur += 8;

            // capturedValue = cl
            cave[cur++] = 0x8A;
            cave[cur++] = 0xC1; // mov al,cl
            cave[cur++] = 0xA2;
            *(uintptr_t*)&cave[cur] =
                (uintptr_t)&g_affinityCapturedWriteValue;
            cur += 8;

            // captured flag = 1
            cave[cur++] = 0x49;
            cave[cur++] = 0xBB;
            *(uintptr_t*)&cave[cur] =
                (uintptr_t)&g_affinityWriteCaptured;
            cur += 8;
            cave[cur++] = 0x41;
            cave[cur++] = 0xC6;
            cave[cur++] = 0x03;
            cave[cur++] = 0x01;

            // 한 번 잡으면 자동 disarm.
            cave[cur++] = 0x49;
            cave[cur++] = 0xBB;
            *(uintptr_t*)&cave[cur] =
                (uintptr_t)&g_affinityExpectedWriteAddr;
            cur += 8;
            cave[cur++] = 0x49;
            cave[cur++] = 0xC7;
            cave[cur++] = 0x03;
            *(uint32_t*)&cave[cur] = 0;
            cur += 4;

            const int skipCapture = cur;
            *(int32_t*)&cave[jneSkipPos] =
                (int32_t)(
                    skipCapture -
                    (jneSkipPos + 4));

            cave[cur++] = 0x41; cave[cur++] = 0x5B;   // pop r11
            cave[cur++] = 0x58;                         // pop rax
            cave[cur++] = 0x9D;                         // popfq

            // 원본 mov [rdx+r9+disp32],cl 8바이트를 그대로 실행한다.
            memcpy(
                &cave[cur],
                g_affinityWriteOriginal,
                sizeof(g_affinityWriteOriginal));
            cur += (int)sizeof(g_affinityWriteOriginal);

            EmitAffinityAbsoluteReturn(
                cave, cur,
                hookAddr +
                    sizeof(g_affinityWriteOriginal));

            FlushInstructionCache(
                GetCurrentProcess(),
                (LPCVOID)g_affinityWriteCaveAddr,
                cur);

            if (!ApplyJmp(
                    hookAddr,
                    g_affinityWriteCaveAddr,
                    sizeof(g_affinityWriteOriginal))) {
                AddLog(
                    u8"[친밀쓰기DBG] 저장 명령 후킹 설치 실패: hook=0x%llX",
                    (unsigned long long)hookAddr);
                VirtualFree(
                    (LPVOID)g_affinityWriteCaveAddr,
                    0, MEM_RELEASE);
                g_affinityWriteCaveAddr = 0;
                memset(
                    g_affinityWriteOriginal, 0,
                    sizeof(g_affinityWriteOriginal));
                return false;
            }

            g_affinityWriteHookAddr = hookAddr;
            g_affinityWriteHookInstalled = true;
            AddLog(
                u8"[친밀쓰기DBG] 저장 명령 후킹 설치 완료: hook=0x%llX",
                (unsigned long long)hookAddr);
            return true;
        }

        int GetAffinityCompressedIndex(uint16_t id) {
            if (id >= 1 && id <= 1000)
                return (int)id;
            if (id >= 2001 && id <= 2100)
                return (int)id - 1000;
            if (id >= 4001 && id <= 4200)
                return (int)id - 2900;
            if (id >= 5001 && id <= 5100)
                return (int)id - 3700;
            if (id >= 3001 && id <= 3150)
                return (int)id - 1600;
            if (id >= 5101 && id <= 5200)
                return (int)id - 3550;
            return -1;
        }

        bool CalcAffinityPairOffset(
            int index1, int index2, uintptr_t* outOffset) {
            if (!outOffset ||
                index1 < 1 || index2 < 1 ||
                index1 > kAffinityOfficerCount ||
                index2 > kAffinityOfficerCount ||
                index1 == index2)
                return false;

            const int leftIndex = (std::min)(index1, index2);
            const int rightIndex = (std::max)(index1, index2);
            const uint64_t left = (uint64_t)(leftIndex - 1);
            const uint64_t width =
                (uint64_t)(kAffinityOfficerCount - 1);
            const uint64_t rowStart =
                left * (2ull * width - (left - 1ull)) / 2ull;
            *outOffset =
                (uintptr_t)(rowStart +
                (uint64_t)(rightIndex - leftIndex - 1));
            return true;
        }
    } // namespace


    bool GetOfficerAffinityAddress(
        uint16_t officerId1,
        uint16_t officerId2,
        uintptr_t& outAddress,
        uint8_t* outCurrentValue) {
        outAddress = 0;
        if (outCurrentValue)
            *outCurrentValue = 0;

        if (officerId1 == 0 || officerId2 == 0 ||
            officerId1 == officerId2)
            return false;

        const int index1 =
            GetAffinityCompressedIndex(officerId1);
        const int index2 =
            GetAffinityCompressedIndex(officerId2);
        uintptr_t pairOffset = 0;
        if (!CalcAffinityPairOffset(
                index1, index2, &pairOffset))
            return false;

        const uintptr_t dataCenter =
            GetScenarioDataCenterAddress();
        if (dataCenter <= 0x10000)
            return false;

        const uintptr_t address =
            dataCenter +
            kCurrentAffinityBaseOffset +
            pairOffset;

        uint8_t value = 0;
        if (!SafeRelRead8(address, &value) ||
            value > 100)
            return false;

        outAddress = address;
        if (outCurrentValue)
            *outCurrentValue = value;
        return true;
    }


    bool TestOfficerAffinityWriteRoundTrip(
        uint16_t officerId1,
        uint16_t officerId2,
        uintptr_t& outAddress,
        uint8_t& outOriginal,
        uint8_t& outTestValue,
        uint8_t& outRestored) {
        outAddress = 0;
        outOriginal = 0;
        outTestValue = 0;
        outRestored = 0;

        uint8_t current = 0;
        uintptr_t address = 0;
        if (!GetOfficerAffinityAddress(
                officerId1, officerId2,
                address, &current))
            return false;

        // 100이면 +1이 불가능하므로 99로 내리지 않고 테스트하지 않는다.
        if (current >= 100)
            return false;

        const uint8_t testValue =
            (uint8_t)(current + 1);

        DWORD oldProtect = 0;
        if (!VirtualProtect(
                (LPVOID)address, 1,
                PAGE_READWRITE, &oldProtect))
            return false;

        bool writeTestOk = false;
        bool restoreOk = false;
        uint8_t verifyTest = 0;
        uint8_t verifyRestore = 0;

        __try {
            *(volatile uint8_t*)address =
                testValue;
            verifyTest =
                *(volatile uint8_t*)address;
            writeTestOk =
                (verifyTest == testValue);

            // 테스트 성공 여부와 관계없이 원래 값으로 되돌린다.
            *(volatile uint8_t*)address =
                current;
            verifyRestore =
                *(volatile uint8_t*)address;
            restoreOk =
                (verifyRestore == current);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            writeTestOk = false;
            restoreOk = false;
        }

        DWORD dummy = 0;
        VirtualProtect(
            (LPVOID)address, 1,
            oldProtect, &dummy);

        outAddress = address;
        outOriginal = current;
        outTestValue = verifyTest;
        outRestored = verifyRestore;

        return writeTestOk && restoreOk;
    }

    bool GetOfficerAffinity(
        uint16_t officerId1,
        uint16_t officerId2,
        uint8_t& outAffinity) {
        uintptr_t address = 0;
        return GetOfficerAffinityAddress(
            officerId1, officerId2,
            address, &outAffinity);
    }


    bool SetOfficerAffinity(
        uint16_t officerId1,
        uint16_t officerId2,
        uint8_t value,
        uint8_t* outPreviousValue) {
        if (value > 100)
            value = 100;

        uintptr_t address = 0;
        uint8_t previous = 0;
        if (!GetOfficerAffinityAddress(
                officerId1, officerId2,
                address, &previous))
            return false;

        DWORD oldProtect = 0;
        if (!VirtualProtect(
                (LPVOID)address, 1,
                PAGE_READWRITE, &oldProtect))
            return false;

        bool ok = false;
        __try {
            *(volatile uint8_t*)address = value;
            ok = (*(volatile uint8_t*)address == value);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = false;
        }

        DWORD dummy = 0;
        VirtualProtect(
            (LPVOID)address, 1,
            oldProtect, &dummy);

        if (ok && outPreviousValue)
            *outPreviousValue = previous;
        return ok;
    }

    bool IncreaseOfficerAffinity(
        uint16_t officerId1,
        uint16_t officerId2,
        uint8_t amount,
        uint8_t* outPreviousValue,
        uint8_t* outNewValue) {
        uint8_t current = 0;
        if (!GetOfficerAffinity(
                officerId1, officerId2, current))
            return false;

        const int increased =
            (std::min)(100, (int)current + (int)amount);
        const uint8_t next =
            (uint8_t)increased;

        if (current == next) {
            if (outPreviousValue)
                *outPreviousValue = current;
            if (outNewValue)
                *outNewValue = current;
            return true;
        }

        uint8_t previous = 0;
        if (!SetOfficerAffinity(
                officerId1, officerId2,
                next, &previous))
            return false;

        if (outPreviousValue)
            *outPreviousValue = previous;
        if (outNewValue)
            *outNewValue = next;
        return true;
    }

    bool ArmOfficerAffinityWriteProbe(
        uint16_t officerId1,
        uint16_t officerId2) {
        uintptr_t expectedAddress = 0;
        uint8_t currentValue = 0;
        if (!GetOfficerAffinityAddress(
                officerId1, officerId2,
                expectedAddress, &currentValue))
            return false;

        if (!InstallAffinityWriteProbeHook())
            return false;

        g_affinityCapturedWriteAddr = 0;
        g_affinityCapturedWriteValue = 0;
        g_affinityWriteCaptured = 0;
        g_affinityExpectedWriteAddr =
            expectedAddress;

        return true;
    }

    bool ConsumeOfficerAffinityWriteProbe(
        uintptr_t& outExpectedAddress,
        uintptr_t& outCapturedAddress,
        uint8_t& outWrittenValue) {
        outExpectedAddress =
            g_affinityExpectedWriteAddr;
        outCapturedAddress = 0;
        outWrittenValue = 0;

        if (!g_affinityWriteCaptured)
            return false;

        outCapturedAddress =
            g_affinityCapturedWriteAddr;
        outWrittenValue =
            g_affinityCapturedWriteValue;

        // 캡처 시 cave에서 expected는 0으로 자동 disarm되므로,
        // 결과 반환 시에는 captured 주소를 expected로 사용한다.
        outExpectedAddress =
            g_affinityCapturedWriteAddr;
        g_affinityWriteCaptured = 0;
        return true;
    }

    void DisarmOfficerAffinityWriteProbe() {
        g_affinityExpectedWriteAddr = 0;
        g_affinityWriteCaptured = 0;
        g_affinityCapturedWriteAddr = 0;
        g_affinityCapturedWriteValue = 0;
    }

    bool GetOfficerRelationshipInfoBatch(
        const std::vector<uintptr_t>& officerBases,
        std::vector<OfficerRelationshipInfo>& outInfos) {
        outInfos.assign(officerBases.size(), OfficerRelationshipInfo{});

        if (officerBases.empty())
            return false;

        const uintptr_t exe = (uintptr_t)GetModuleHandle(NULL);
        const uintptr_t gameBase = GetGameBase();
        uintptr_t rosterBase = 0;
        if (!exe || gameBase <= 0x10000 ||
            !TryResolveOfficerRosterArrayBase(exe, &rosterBase) ||
            rosterBase <= 0x10000)
            return false;

        // 요청된 무장 ID -> 결과 인덱스. 관계 테이블은 한 번만 훑고,
        // 슬롯에 요청 무장이 들어 있을 때 해당 결과에 바로 누적한다.
        std::unordered_map<uint16_t, size_t> selectedById;
        selectedById.reserve(officerBases.size());
        for (size_t i = 0; i < officerBases.size(); ++i) {
            const uintptr_t officerBase = officerBases[i];
            uint16_t id = 0;
            if (officerBase <= 0x10000 ||
                !SafeRelRead16(officerBase + 0x08, &id) ||
                id < 1 || id > 5102)
                continue;
            selectedById[id] = i;
        }

        if (selectedById.empty())
            return false;

        uintptr_t synerBase = 0;
        uintptr_t relationBase = 0;
        const bool hasSyner =
            TryResolveSynergeticTable(gameBase, rosterBase, &synerBase);
        const bool hasRelation =
            TryResolveRelationshipTable(gameBase, rosterBase, &relationBase);

        if (!hasSyner && !hasRelation)
            return false;

        // 직접 관계(의형제/배우자/혐오/경쟁)를 테이블 전체에서 한 번 수집한다.
        // 기존 단일 조회와 동일하게 한 슬롯의 최대 5명 관계를 서로 연결한다.
        if (hasRelation) {
            for (int i = 0; i < 3000; ++i) {
                const uintptr_t slot = relationBase + (uintptr_t)i * 0x40;
                uint8_t relation = 0;
                if (!SafeRelRead8(slot + 0x08, &relation))
                    break;
                if (relation < 1 || relation > 4)
                    continue;

                uint16_t memberIds[5]{};
                bool readOk = true;
                for (int j = 0; j < 5; ++j) {
                    uintptr_t memberPtr = 0;
                    if (!SafeRelReadPtr(
                            slot + 0x10 + (uintptr_t)j * 8,
                            &memberPtr)) {
                        readOk = false;
                        break;
                    }
                    if (memberPtr == 0)
                        continue;

                    uint16_t id = 0;
                    if (ReadOfficerIdFromRosterPtr(
                            memberPtr, rosterBase, &id))
                        memberIds[j] = id;
                }

                if (!readOk)
                    break;

                for (int source = 0; source < 5; ++source) {
                    const uint16_t selectedId = memberIds[source];
                    if (selectedId == 0)
                        continue;

                    const auto selectedIt =
                        selectedById.find(selectedId);
                    if (selectedIt == selectedById.end())
                        continue;

                    OfficerRelationshipInfo& info =
                        outInfos[selectedIt->second];

                    for (int target = 0; target < 5; ++target) {
                        const uint16_t memberId = memberIds[target];
                        if (memberId == 0 || memberId == selectedId)
                            continue;

                        switch (relation) {
                        case 1:
                            AddUniqueRelationshipId(
                                info.swornBrothers, memberId);
                            break;
                        case 2:
                            AddUniqueRelationshipId(
                                info.spouses, memberId);
                            break;
                        case 3:
                            AddUniqueRelationshipId(
                                info.enemies, memberId);
                            break;
                        case 4:
                            AddUniqueRelationshipId(
                                info.rivals, memberId);
                            break;
                        }
                    }
                }
            }
        }

        // 숙명 관계도 전체 테이블을 한 번만 훑는다.
        // 직접 관계와 중복되는 항목은 기존 단일 조회와 동일하게 제외한다.
        if (hasSyner) {
            for (int i = 0; i < 5000; ++i) {
                const uintptr_t slot = synerBase + (uintptr_t)i * 0x20;
                uintptr_t p1 = 0, p2 = 0;
                uint8_t relation = 0, occurred = 0;
                if (!SafeRelReadPtr(slot + 0x08, &p1) ||
                    !SafeRelReadPtr(slot + 0x10, &p2) ||
                    !SafeRelRead8(slot + 0x18, &relation) ||
                    !SafeRelRead8(slot + 0x19, &occurred))
                    break;

                if ((relation != 1 && relation != 2) ||
                    occurred != 0)
                    continue;

                uint16_t id1 = 0, id2 = 0;
                const bool valid1 =
                    ReadOfficerIdFromRosterPtr(p1, rosterBase, &id1);
                const bool valid2 =
                    ReadOfficerIdFromRosterPtr(p2, rosterBase, &id2);
                if (!valid1 || !valid2)
                    continue;

                auto addSynerRelationship =
                    [&](uint16_t selectedId, uint16_t otherId) {
                    if (selectedId == 0 || otherId == 0 ||
                        selectedId == otherId)
                        return;

                    const auto selectedIt =
                        selectedById.find(selectedId);
                    if (selectedIt == selectedById.end())
                        return;

                    OfficerRelationshipInfo& info =
                        outInfos[selectedIt->second];
                    if (ContainsRelationshipId(info, otherId))
                        return;

                    if (relation == 1)
                        AddUniqueRelationshipId(
                            info.antipathetic, otherId);
                    else
                        AddUniqueRelationshipId(
                            info.synergetic, otherId);
                };

                addSynerRelationship(id1, id2);
                addSynerRelationship(id2, id1);
            }
        }

        for (const auto& entry : selectedById)
            outInfos[entry.second].valid = true;

        return true;
    }

    bool GetOfficerRelationshipInfo(
        uintptr_t officerBase,
        OfficerRelationshipInfo& outInfo) {
        std::vector<uintptr_t> officerBases = { officerBase };
        std::vector<OfficerRelationshipInfo> infos;
        if (!GetOfficerRelationshipInfoBatch(
                officerBases, infos) ||
            infos.empty() || !infos[0].valid) {
            outInfo = OfficerRelationshipInfo{};
            return false;
        }

        outInfo = infos[0];
        return true;
    }



    void ResetAutoAffinityGrowthState() {
        g_autoAffinityLastDataCenter = 0;
        g_autoAffinityLastProtagonist = 0;
        g_autoAffinityLastYear = 0;
        g_autoAffinityLastMonth = 0;
    }

    namespace {
        constexpr uintptr_t kOfficerCompatibilityOffset = 0x5D;
        constexpr uintptr_t kOfficerInterestOffset = 0x83;
        constexpr uintptr_t kOfficerFavoredReputationOffset = 0xA4;

        bool IsAutoAffinityOfficerStatus(uint8_t status) {
            return status == 0x18 || status == 0x28 ||
                   status == 0x38 || status == 0x48 ||
                   status == 0xC8 || status == 0xD8 ||
                   status == 0xE8;
        }

        bool HasId(
            const std::vector<uint16_t>& values,
            uint16_t id) {
            return std::find(
                       values.begin(),
                       values.end(),
                       id) != values.end();
        }

        bool ShouldSkipAutoAffinityPair(
            const OfficerRelationshipInfo& info,
            uint16_t otherId) {
            if (HasId(info.swornBrothers, otherId) ||
                HasId(info.spouses, otherId) ||
                HasId(info.synergetic, otherId))
                return true;

            return HasId(info.antipathetic, otherId) ||
                   HasId(info.enemies, otherId) ||
                   HasId(info.rivals, otherId);
        }

        std::string AutoAffinityName(uint16_t id) {
            auto it = g_officerNames.find((int)id);
            if (it != g_officerNames.end() &&
                !it->second.empty())
                return it->second;
            return u8"ID " + std::to_string((int)id);
        }

        int CalcCompatibilityDistance(
            uint8_t a, uint8_t b) {
            const int diff =
                std::abs((int)a - (int)b);
            return (std::min)(diff, 150 - diff);
        }

        int CalcCompatibilityBonus(
            uint8_t a, uint8_t b) {
            const int distance =
                CalcCompatibilityDistance(a, b);

            // 참고 DLL(AffinityM) 실측:
            // 거리 0 => +15
            // 1~5 => +14, 6~10 => +13, ... , 66~70 => +1, 71~75 => +0
            if (distance == 0)
                return 15;

            const int bonus =
                15 - ((distance + 4) / 5);
            return (std::max)(0, bonus);
        }

        int CalcInterestAndReputationBonus(
            uint8_t interestA,
            uint8_t interestB,
            uint8_t reputationA,
            uint8_t reputationB) {
            int bonus = 0;

            // 참고 DLL(AffinityM) 실측:
            // +0x83을 2비트 x 4필드로 비교한다.
            // 같은 값이어도 0(없음)이면 보너스 없음.
            for (int shift = 0; shift <= 6; shift += 2) {
                const uint8_t a =
                    (uint8_t)((interestA >> shift) & 0x03);
                const uint8_t b =
                    (uint8_t)((interestB >> shift) & 0x03);
                if (a != 0 && a == b)
                    bonus += 3;
            }

            // +0xA4 중시 유형도 같은 비영(非0) 값일 때 +3.
            if (reputationA != 0 &&
                reputationA == reputationB)
                bonus += 3;

            return bonus;
        }

        bool IsAutoAffinityCouncilMonth(uint8_t month) {
            return month == 1 || month == 4 ||
                   month == 7 || month == 10;
        }

        // SEH는 std::vector/string 등의 소멸자가 있는 TickAutoAffinityGrowth
        // 본문에서 사용할 수 없으므로 POD 전용 헬퍼로 분리한다.
        bool TryWriteAffinityByte(
            uintptr_t address,
            uint8_t value) {
            __try {
                *(volatile uint8_t*)address =
                    value;
                return
                    *(volatile uint8_t*)address ==
                    value;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }
    }

    void TickAutoAffinityGrowth(
        uintptr_t protagonistBase) {
        static ULONGLONG s_lastPollMs = 0;
        const ULONGLONG now =
            GetTickCount64();
        if (now - s_lastPollMs < 250)
            return;
        s_lastPollMs = now;

        if (!g_autoAffinityGrowthEnabled) {
            ResetAutoAffinityGrowthState();
            return;
        }

        const uintptr_t dataCenter =
            GetScenarioDataCenterAddress();
        if (dataCenter <= 0x10000 ||
            protagonistBase <= 0x10000)
            return;

        if (g_autoAffinityLastDataCenter != dataCenter ||
            g_autoAffinityLastProtagonist != protagonistBase) {
            g_autoAffinityLastDataCenter = dataCenter;
            g_autoAffinityLastProtagonist = protagonistBase;
            g_autoAffinityLastYear = 0;
            g_autoAffinityLastMonth = 0;
        }

        uint16_t protagonistId = 0;
        if (!SafeRelRead16(
                protagonistBase + 0x08,
                &protagonistId) ||
            protagonistId < 1 ||
            protagonistId > 5102)
            return;

        unsigned short currentYear = 0;
        uint8_t currentMonth = 0;
        if (!ReadScenarioDate(
                &currentYear, &currentMonth) ||
            currentYear == 0 ||
            currentMonth < 1 ||
            currentMonth > 12)
            return;

        // 참고 DLL(AffinityM)과 동일하게 "분기 평정월로 넘어가는 순간" 1회 실행.
        // 활성화 시 이미 해당 월이면 기준만 잡고 소급 실행하지 않는다.
        if (g_autoAffinityLastYear == 0 ||
            g_autoAffinityLastMonth == 0) {
            g_autoAffinityLastYear = currentYear;
            g_autoAffinityLastMonth = currentMonth;
            return;
        }

        if (g_autoAffinityLastYear == currentYear &&
            g_autoAffinityLastMonth == currentMonth)
            return;

        const unsigned int previousSerial =
            (unsigned int)g_autoAffinityLastYear * 12u +
            (unsigned int)g_autoAffinityLastMonth;
        const unsigned int currentSerial =
            (unsigned int)currentYear * 12u +
            (unsigned int)currentMonth;

        g_autoAffinityLastYear = currentYear;
        g_autoAffinityLastMonth = currentMonth;

        // 저장 불러오기 등으로 날짜가 뒤로 간 경우에는 실행하지 않는다.
        if (currentSerial <= previousSerial)
            return;

        if (!IsAutoAffinityCouncilMonth(currentMonth))
            return;

        AddLog(
            u8"[친밀자동] %u년 %u월 평정 시작 감지 -> AI 친밀도 가속 실행",
            (unsigned int)currentYear,
            (unsigned int)currentMonth);

        const uintptr_t exe =
            (uintptr_t)GetModuleHandle(nullptr);
        uintptr_t rosterBase = 0;
        if (!exe ||
            !TryResolveOfficerRosterArrayBase(
                exe, &rosterBase) ||
            rosterBase <= 0x10000)
            return;

        struct AutoAffinityOfficer {
            uintptr_t base = 0;
            uintptr_t force = 0;
            uintptr_t city = 0;
            uint16_t id = 0;
            uint8_t compatibility = 0;
            uint8_t interest = 0;
            uint8_t favoredReputation = 0;
            size_t relationIndex = 0;
        };

        std::vector<AutoAffinityOfficer> officers;
        std::vector<uintptr_t> officerBases;
        officers.reserve(1600);
        officerBases.reserve(1600);

        int invalidMetaCount = 0;
        bool seenOfficerIds[5103] = {};

        for (int i = 0; i < 5102; ++i) {
            const uintptr_t base =
                rosterBase + (uintptr_t)i * 0x3D0;

            const RosterStats rosterStats =
                SafeReadRosterStats(base);
            if (!rosterStats.valid ||
                rosterStats.id_08 < 1 ||
                rosterStats.id_08 > 5102)
                continue;

            const uint16_t id =
                rosterStats.id_08;
            if (seenOfficerIds[id])
                continue;
            seenOfficerIds[id] = true;

            uint8_t status = 0;
            uintptr_t force = 0;
            uintptr_t city = 0;
            uint8_t compatibility = 0;
            uint8_t interest = 0;
            uint8_t favoredReputation = 0;

            if (id == protagonistId ||
                !SafeRelRead8(base + 0x10, &status) ||
                !IsAutoAffinityOfficerStatus(status) ||
                !SafeRelReadPtr(base + 0x18, &force) ||
                !SafeRelReadPtr(base + 0x20, &city) ||
                force <= 0x10000 ||
                city <= 0x10000)
                continue;

            const bool metaReadOk =
                SafeRelRead8(
                    base + kOfficerCompatibilityOffset,
                    &compatibility) &&
                SafeRelRead8(
                    base + kOfficerInterestOffset,
                    &interest) &&
                SafeRelRead8(
                    base + kOfficerFavoredReputationOffset,
                    &favoredReputation);

            if (!metaReadOk ||
                compatibility > 149 ||
                favoredReputation < 1 ||
                favoredReputation > 6) {
                ++invalidMetaCount;
                continue;
            }

            AutoAffinityOfficer officer;
            officer.base = base;
            officer.force = force;
            officer.city = city;
            officer.id = id;
            officer.compatibility = compatibility;
            officer.interest = interest;
            officer.favoredReputation =
                favoredReputation;
            officer.relationIndex =
                officerBases.size();
            officers.push_back(officer);
            officerBases.push_back(base);
        }

        if (officers.size() < 2) {
            AddLog(
                u8"[친밀자동] 분기 평정 시작: 유효 AI 무장 부족 / 메타데이터 제외 %d명",
                invalidMetaCount);
            return;
        }

        std::vector<OfficerRelationshipInfo>
            relationships;
        if (!GetOfficerRelationshipInfoBatch(
                officerBases, relationships) ||
            relationships.size() != officerBases.size()) {
            AddLog(
                u8"[친밀자동] 분기 평정 시작: 관계 테이블 읽기 실패");
            return;
        }

        std::sort(
            officers.begin(), officers.end(),
            [](const AutoAffinityOfficer& a,
               const AutoAffinityOfficer& b) {
                if (a.force != b.force)
                    return a.force < b.force;
                if (a.city != b.city)
                    return a.city < b.city;
                return a.id < b.id;
            });

        int candidateCount = 0;
        int increasedCount = 0;
        int zeroGainCount = 0;
        int reachedHundredCount = 0;
        int failedCount = 0;
        int negativeAffinityCount = 0;

        const uintptr_t affinityArrayBase =
            dataCenter + kCurrentAffinityBaseOffset;
        const size_t affinityArraySize =
            (size_t)kAffinityOfficerCount *
            (size_t)(kAffinityOfficerCount - 1) / 2u;

        DWORD affinityOldProtect = 0;
        if (!VirtualProtect(
                (LPVOID)affinityArrayBase,
                affinityArraySize,
                PAGE_READWRITE,
                &affinityOldProtect)) {
            AddLog(
                u8"[친밀자동] 분기 평정 시작: 친밀도 배열 쓰기 권한 확보 실패");
            return;
        }

        const ULONGLONG affinityStartMs =
            GetTickCount64();

        size_t groupBegin = 0;
        while (groupBegin < officers.size()) {
            size_t groupEnd =
                groupBegin + 1;
            while (groupEnd < officers.size() &&
                   officers[groupEnd].force ==
                       officers[groupBegin].force &&
                   officers[groupEnd].city ==
                       officers[groupBegin].city) {
                ++groupEnd;
            }

            for (size_t a = groupBegin;
                 a < groupEnd; ++a) {
                const AutoAffinityOfficer& left =
                    officers[a];
                const OfficerRelationshipInfo& leftRel =
                    relationships[left.relationIndex];

                for (size_t b = a + 1;
                     b < groupEnd; ++b) {
                    const AutoAffinityOfficer& right =
                        officers[b];

                    if (ShouldSkipAutoAffinityPair(
                            leftRel, right.id))
                        continue;

                    const int leftAffinityIndex =
                        GetAffinityCompressedIndex(left.id);
                    const int rightAffinityIndex =
                        GetAffinityCompressedIndex(right.id);
                    uintptr_t pairOffset = 0;
                    if (!CalcAffinityPairOffset(
                            leftAffinityIndex,
                            rightAffinityIndex,
                            &pairOffset))
                        continue;

                    const uintptr_t affinityAddress =
                        dataCenter +
                        kCurrentAffinityBaseOffset +
                        pairOffset;

                    uint8_t rawAffinity = 0;
                    if (!SafeRelRead8(
                            affinityAddress,
                            &rawAffinity))
                        continue;

                    const int8_t signedAffinity =
                        (int8_t)rawAffinity;
                    if (signedAffinity <= -1) {
                        ++negativeAffinityCount;
                        continue;
                    }
                    if (rawAffinity >= 100)
                        continue;

                    const int compatibilityBonus =
                        CalcCompatibilityBonus(
                            left.compatibility,
                            right.compatibility);
                    const int interestBonus =
                        CalcInterestAndReputationBonus(
                            left.interest,
                            right.interest,
                            left.favoredReputation,
                            right.favoredReputation);
                    const int gain =
                        compatibilityBonus +
                        interestBonus;

                    ++candidateCount;

                    if (gain <= 0) {
                        ++zeroGainCount;
                        continue;
                    }

                    const int nextInt =
                        (std::min)(
                            100,
                            (int)rawAffinity + gain);
                    const uint8_t next =
                        (uint8_t)nextInt;

                    if (!TryWriteAffinityByte(
                            affinityAddress,
                            next)) {
                        ++failedCount;
                        continue;
                    }

                    ++increasedCount;

                    if (rawAffinity < 100 &&
                        next == 100) {
                        ++reachedHundredCount;
                        AddLog(
                            u8"[친밀자동] 100 도달: %s(ID %u) <-> %s(ID %u), %u -> 100 / 상성 +%d / 흥미·중시 +%d",
                            AutoAffinityName(left.id).c_str(),
                            (unsigned int)left.id,
                            AutoAffinityName(right.id).c_str(),
                            (unsigned int)right.id,
                            (unsigned int)rawAffinity,
                            compatibilityBonus,
                            interestBonus);

                        // 이 기능은 친밀도만 가속하며 관계 테이블은 직접 수정하지 않는다.
                    }
                }
            }

            groupBegin = groupEnd;
        }

        DWORD affinityDummyProtect = 0;
        VirtualProtect(
            (LPVOID)affinityArrayBase,
            affinityArraySize,
            affinityOldProtect,
            &affinityDummyProtect);

        const ULONGLONG affinityElapsedMs =
            GetTickCount64() - affinityStartMs;

        AddLog(
            u8"[친밀자동] 분기 평정 처리 완료: AI %d명 / 같은 세력·도시 후보 %d쌍 / 증가 %d / 증가0 %d / 음수친밀 제외 %d / 100 도달 %d / 실패 %d / 메타데이터 제외 %d명",
            (int)officers.size(),
            candidateCount,
            increasedCount,
            zeroGainCount,
            negativeAffinityCount,
            reachedHundredCount,
            failedCount,
            invalidMetaCount);
        AddLog(
            u8"[친밀자동] 처리시간 %llums (친밀 배열 쓰기 권한 1회 설정)",
            (unsigned long long)affinityElapsedMs);
    }

    bool GetOfficerTalentDetailed(uintptr_t officerBase, int slot, TalentInfo& outInfo) {
        if (officerBase < 0x10000) return false;
        memset(&outInfo, 0, sizeof(TalentInfo));
        __try {
            uintptr_t pointerArrayAddr = officerBase + 0x88 + (slot * 8);
            uintptr_t realDataAddr = *(uintptr_t*)pointerArrayAddr;
            if (realDataAddr == 0 || realDataAddr < 0x10000) return false;

            outInfo.id = *(uint16_t*)(realDataAddr + 0x08);
            for (int i = 0; i < 6; i++) {
                uintptr_t effectAddr = realDataAddr + 0x0A + (i * 6);
                uint16_t eId = *(uint16_t*)(effectAddr);
                if (eId == 0) break;
                outInfo.effects[i].effectId = eId;
                outInfo.effects[i].val1 = *(uint16_t*)(effectAddr + 2);
                outInfo.effects[i].val2 = *(uint16_t*)(effectAddr + 4);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool IsUtf8(const std::string& str) {
        int i = 0;
        int n = (int)str.length();
        while (i < n) {
            unsigned char c = (unsigned char)str[i];
            if (c <= 0x7F) i++;
            else if ((c & 0xE0) == 0xC0) {
                if (i + 1 >= n || (str[i + 1] & 0xC0) != 0x80) return false;
                i += 2;
            }
            else if ((c & 0xF0) == 0xE0) {
                if (i + 2 >= n || (str[i + 1] & 0xC0) != 0x80 || (str[i + 2] & 0xC0) != 0x80) return false;
                i += 3;
            }
            else if ((c & 0xF8) == 0xF0) {
                if (i + 3 >= n || (str[i + 1] & 0xC0) != 0x80 || (str[i + 2] & 0xC0) != 0x80 || (str[i + 3] & 0xC0) != 0x80) return false;
                i += 4;
            }
            else return false;
        }
        return true;
    }

    std::string AnsiToUtf8(const std::string& str) {
        if (str.empty()) return "";
        // [수정] 이미 UTF-8이면 변환하지 않음 (깨짐 방지)
        if (IsUtf8(str)) return str;

        int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
        if (len <= 0) return str;
        std::wstring wstr(len, 0);
        MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &wstr[0], len);
        int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        if (u8len <= 0) return str;
        std::string u8str(u8len, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &u8str[0], u8len, NULL, NULL);
        size_t actualLen = strlen(u8str.c_str());
        u8str.resize(actualLen);
        return u8str;
    }

    void LoadOfficerNames() {
        if (g_namesLoaded) return;
        g_namesLoaded = true;

        std::string jsonText;
        bool loaded = false;
        char path[MAX_PATH] = {};
        if (GetModuleFileNameA(g_hModule, path, MAX_PATH)) {
            const std::filesystem::path jsonPath =
                std::filesystem::path(path).parent_path() / "S8RPK_cheat_char.json";
            loaded = ReadUtf8TextFile(jsonPath, jsonText);
        }
        if (!loaded)
            loaded = LoadEmbeddedJsonResource(IDR_JSON_CHEAT_CHAR, jsonText);
        if (!loaded)
            return;

        std::istringstream file(jsonText);
        std::string line;
        int currentId = -1;
        std::string currentName = "";
        std::string currentJa = "";
        while (std::getline(file, line)) {
            if (line.find("{") != std::string::npos) {
                currentId = -1;
                currentName = "";
                currentJa = "";
            }
            size_t idPos = line.find("\"id\"");
            if (idPos != std::string::npos) {
                size_t colon = line.find(":", idPos);
                if (colon != std::string::npos) {
                    try { currentId = std::stoi(line.substr(colon + 1)); }
                    catch (...) { currentId = -1; }
                }
            }
            size_t namePos = line.find("\"name\"");
            if (namePos != std::string::npos) {
                size_t colon = line.find(":", namePos);
                if (colon != std::string::npos) {
                    size_t firstQuote = line.find("\"", colon);
                    if (firstQuote != std::string::npos) {
                        size_t secondQuote = line.find("\"", firstQuote + 1);
                        if (secondQuote != std::string::npos)
                            currentName = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                    }
                }
            }
            size_t jaPos = line.find("\"ja\"");
            if (jaPos != std::string::npos) {
                size_t colon = line.find(":", jaPos);
                if (colon != std::string::npos) {
                    size_t firstQuote = line.find("\"", colon);
                    if (firstQuote != std::string::npos) {
                        size_t secondQuote = line.find("\"", firstQuote + 1);
                        if (secondQuote != std::string::npos)
                            currentJa = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                    }
                }
            }
            if (line.find("}") != std::string::npos && currentId != -1) {
                if (!currentName.empty()) {
                    std::string u8Name = NormalizeUtf8(AnsiToUtf8(currentName));
                    std::string u8Ja = NormalizeUtf8(AnsiToUtf8(currentJa));
                    if (!u8Ja.empty())
                        g_officerNames[currentId] = u8Name + u8"(" + u8Ja + u8")";
                    else
                        g_officerNames[currentId] = u8Name;
                }
            }
        }
    }

    namespace {

    struct CharJsonEntry {
        int id = 0;
        std::string name;
        std::string ja;
    };

    std::string JsonEscapeQuoted(const std::string &s) {
        std::string r;
        r.reserve(s.size() + 16);
        for (unsigned char c : s) {
            switch (c) {
            case '"':
                r += "\\\"";
                break;
            case '\\':
                r += "\\\\";
                break;
            case '\n':
                r += "\\n";
                break;
            case '\r':
                r += "\\r";
                break;
            case '\t':
                r += "\\t";
                break;
            default:
                r += static_cast<char>(c);
                break;
            }
        }
        return r;
    }

    bool ParseCharJsonFileToMap(const std::string &jsonPath, std::map<int, CharJsonEntry> &out) {
        std::ifstream file(jsonPath);
        if (!file.is_open())
            return false;
        std::string line;
        int currentId = -1;
        std::string currentName = "";
        std::string currentJa = "";
        while (std::getline(file, line)) {
            if (line.find("{") != std::string::npos) {
                currentId = -1;
                currentName = "";
                currentJa = "";
            }
            size_t idPos = line.find("\"id\"");
            if (idPos != std::string::npos) {
                size_t colon = line.find(":", idPos);
                if (colon != std::string::npos) {
                    try {
                        currentId = std::stoi(line.substr(colon + 1));
                    } catch (...) {
                        currentId = -1;
                    }
                }
            }
            size_t namePos = line.find("\"name\"");
            if (namePos != std::string::npos) {
                size_t colon = line.find(":", namePos);
                if (colon != std::string::npos) {
                    size_t firstQuote = line.find("\"", colon);
                    if (firstQuote != std::string::npos) {
                        size_t secondQuote = line.find("\"", firstQuote + 1);
                        if (secondQuote != std::string::npos)
                            currentName = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                    }
                }
            }
            size_t jaPos = line.find("\"ja\"");
            if (jaPos != std::string::npos) {
                size_t colon = line.find(":", jaPos);
                if (colon != std::string::npos) {
                    size_t firstQuote = line.find("\"", colon);
                    if (firstQuote != std::string::npos) {
                        size_t secondQuote = line.find("\"", firstQuote + 1);
                        if (secondQuote != std::string::npos)
                            currentJa = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                    }
                }
            }
            if (line.find("}") != std::string::npos && currentId != -1) {
                CharJsonEntry e;
                e.id = currentId;
                e.name = NormalizeUtf8(AnsiToUtf8(currentName));
                e.ja = NormalizeUtf8(AnsiToUtf8(currentJa));
                out[currentId] = std::move(e);
            }
        }
        file.close();
        return true;
    }

    bool WriteCharJsonMap(const std::string &jsonPath, const std::map<int, CharJsonEntry> &byId) {
        std::ofstream o(jsonPath, std::ios::binary | std::ios::trunc);
        if (!o.is_open())
            return false;
        o << "[\n";
        size_t i = 0;
        const size_t n = byId.size();
        for (const auto &kv : byId) {
            const CharJsonEntry &e = kv.second;
            o << "  {\n";
            o << "    \"id\": " << e.id << ",\n";
            o << "    \"name\": \"" << JsonEscapeQuoted(e.name) << "\",\n";
            o << "    \"ja\": \"" << JsonEscapeQuoted(e.ja) << "\"\n";
            o << "  }";
            if (++i < n)
                o << ",";
            o << "\n";
        }
        o << "]\n";
        return true;
    }

    } // namespace

    bool SaveOfficerNameToJson(int id, const std::string &nameUtf8) {
        char path[MAX_PATH];
        if (!GetModuleFileNameA(g_hModule, path, MAX_PATH))
            return false;
        std::string jsonPath = std::filesystem::path(path).parent_path().append("S8RPK_cheat_char.json").string();

        std::map<int, CharJsonEntry> byId;
        bool parsed = ParseCharJsonFileToMap(jsonPath, byId);
        if (!parsed && std::filesystem::exists(jsonPath))
            return false;

        // 외부 파일이 아직 없으면 내장 기본 명단을 기준으로 새 override 파일을 만듭니다.
        // 한 명만 수정했다고 나머지 기본 명단이 사라지는 것을 방지합니다.
        if (!parsed) {
            std::string embedded;
            if (LoadEmbeddedJsonResource(IDR_JSON_CHEAT_CHAR, embedded)) {
                std::istringstream file(embedded);
                std::string line;
                int currentId = -1;
                std::string currentName;
                std::string currentJa;
                while (std::getline(file, line)) {
                    if (line.find("{") != std::string::npos) {
                        currentId = -1;
                        currentName.clear();
                        currentJa.clear();
                    }
                    size_t idPos = line.find("\"id\"");
                    if (idPos != std::string::npos) {
                        size_t colon = line.find(":", idPos);
                        if (colon != std::string::npos) {
                            try { currentId = std::stoi(line.substr(colon + 1)); }
                            catch (...) { currentId = -1; }
                        }
                    }
                    size_t namePos = line.find("\"name\"");
                    if (namePos != std::string::npos) {
                        size_t colon = line.find(":", namePos);
                        if (colon != std::string::npos) {
                            size_t q1 = line.find("\"", colon);
                            size_t q2 = q1 == std::string::npos ? std::string::npos : line.find("\"", q1 + 1);
                            if (q1 != std::string::npos && q2 != std::string::npos)
                                currentName = line.substr(q1 + 1, q2 - q1 - 1);
                        }
                    }
                    size_t jaPos = line.find("\"ja\"");
                    if (jaPos != std::string::npos) {
                        size_t colon = line.find(":", jaPos);
                        if (colon != std::string::npos) {
                            size_t q1 = line.find("\"", colon);
                            size_t q2 = q1 == std::string::npos ? std::string::npos : line.find("\"", q1 + 1);
                            if (q1 != std::string::npos && q2 != std::string::npos)
                                currentJa = line.substr(q1 + 1, q2 - q1 - 1);
                        }
                    }
                    if (line.find("}") != std::string::npos && currentId != -1) {
                        CharJsonEntry e;
                        e.id = currentId;
                        e.name = NormalizeUtf8(AnsiToUtf8(currentName));
                        e.ja = NormalizeUtf8(AnsiToUtf8(currentJa));
                        byId[currentId] = std::move(e);
                    }
                }
            }
        }

        std::string normalized = NormalizeUtf8(nameUtf8);
        auto it = byId.find(id);
        if (it == byId.end()) {
            CharJsonEntry e;
            e.id = id;
            e.name = normalized;
            e.ja = "";
            byId[id] = std::move(e);
        } else {
            it->second.name = normalized;
        }

        if (!WriteCharJsonMap(jsonPath, byId))
            return false;

        auto it2 = byId.find(id);
        if (it2 == byId.end() || it2->second.name.empty())
            g_officerNames.erase(id);
        else if (!it2->second.ja.empty())
            g_officerNames[id] = it2->second.name + u8"(" + it2->second.ja + u8")";
        else
            g_officerNames[id] = it2->second.name;
        return true;
    }

    void LoadEffectDefinitions() {
        if (g_effectsLoaded) return;
        g_effectsLoaded = true;

        std::string jsonText;
        bool loaded = false;
        char path[MAX_PATH] = {};
        if (GetModuleFileNameA(g_hModule, path, MAX_PATH)) {
            const std::filesystem::path jsonPath =
                std::filesystem::path(path).parent_path() / "effect_definitions.json";
            loaded = ReadUtf8TextFile(jsonPath, jsonText);
        }
        if (!loaded)
            loaded = LoadEmbeddedJsonResource(IDR_JSON_EFFECT_DEFINITIONS, jsonText);
        if (!loaded)
            return;

        std::istringstream file(jsonText);
        std::string line;
        EffectDef currentDef = { -1 };
        std::string currentKey = "";
        while (std::getline(file, line)) {
            if (line.find("{") != std::string::npos && line.find(":") == std::string::npos) {
                currentDef = { -1 };
                currentKey = "";
            }
            size_t typePos = line.find("\"type\"");
            if (typePos != std::string::npos) {
                size_t colon = line.find(":", typePos);
                if (colon != std::string::npos) {
                    try {
                        std::string valStr = line.substr(colon + 1);
                        size_t comma = valStr.find(",");
                        if (comma != std::string::npos) valStr = valStr.substr(0, comma);
                        currentDef.type = std::stoi(valStr);
                    } catch (...) {}
                }
            }
            size_t tempPos = line.find("\"template\"");
            if (tempPos != std::string::npos) {
                size_t colon = line.find(":", tempPos);
                if (colon != std::string::npos) {
                    size_t firstQuote = line.find("\"", colon);
                    if (firstQuote != std::string::npos) {
                        size_t secondQuote = line.find("\"", firstQuote + 1);
                        if (secondQuote != std::string::npos)
                            currentDef.templateStr = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                    }
                }
            }
            if (line.find("\"valueMap\"") != std::string::npos) currentKey = "v";
            else if (line.find("\"paramMap\"") != std::string::npos) currentKey = "p";
            if (currentKey != "") {
                size_t firstQuote = line.find("\""), colon = line.find(":");
                if (firstQuote != std::string::npos && colon != std::string::npos && firstQuote < colon) {
                    size_t secondQuote = line.find("\"", firstQuote + 1);
                    if (secondQuote != std::string::npos && secondQuote < colon) {
                        std::string keyStr = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                        size_t valFirstQuote = line.find("\"", colon);
                        if (valFirstQuote != std::string::npos) {
                            size_t valSecondQuote = line.find("\"", valFirstQuote + 1);
                            if (valSecondQuote != std::string::npos) {
                                std::string valStr = line.substr(valFirstQuote + 1, valSecondQuote - valFirstQuote - 1);
                                try {
                                    int k = std::stoi(keyStr);
                                    if (currentKey == "v") currentDef.valueMap[k] = valStr;
                                    else currentDef.paramMap[k] = valStr;
                                } catch (...) {}
                            }
                        }
                    }
                }
            }
            if (line.find("}") != std::string::npos) {
                if (currentKey != "") currentKey = "";
                else if (currentDef.type != -1) {
                    g_effectDefs[currentDef.type] = currentDef;
                    currentDef.type = -1;
                }
            }
        }
    }

    const char* GetTalentName(uint16_t id) {
        static const std::unordered_map<uint16_t, const char*> traitsMap = {
            { 1, u8"대덕" }, { 2, u8"의협" }, { 3, u8"만인적" }, { 4, u8"일신시담" }, { 5, u8"금마초" },
            { 6, u8"노당익장" }, { 7, u8"복룡" }, { 8, u8"봉추" }, { 9, u8"기린아" }, { 10, u8"초세지걸" },
            { 11, u8"왕좌" }, { 12, u8"불요불굴" }, { 13, u8"낭고" }, { 14, u8"병귀신속" }, { 15, u8"료래료래" },
            { 16, u8"금강불괴" }, { 17, u8"위서심공" }, { 18, u8"산도강습" }, { 19, u8"강동맹호" }, { 20, u8"소패왕" },
            { 21, u8"용재" }, { 22, u8"화신" }, { 23, u8"냉염" }, { 24, u8"괄목" }, { 25, u8"방울감녕" },
            { 26, u8"원모심려" }, { 27, u8"천하무쌍" }, { 28, u8"수화폐월" }, { 29, u8"명가위광" }, { 30, u8"악역무도" },
            { 31, u8"구심" }, { 32, u8"황천" }, { 33, u8"전장의꽃" }, { 34, u8"호위" }, { 35, u8"기습병" },
            { 36, u8"간파" }, { 37, u8"군규" }, { 38, u8"냉정" }, { 39, u8"기략" }, { 40, u8"진법" },
            { 41, u8"궤계" }, { 42, u8"맹공" }, { 43, u8"견수" }, { 44, u8"불굴" }, { 45, u8"산전" },
            { 46, u8"삼전" }, { 47, u8"강창" }, { 48, u8"조기통제" }, { 49, u8"북방마술" }, { 50, u8"질주궁" },
            { 51, u8"수신" }, { 52, u8"상조교" }, { 53, u8"재녀" }, { 54, u8"교화" }, { 55, u8"호랑지심" },
            { 56, u8"부가" }, { 57, u8"능리" }, { 58, u8"근면" }, { 59, u8"시재" }, { 60, u8"유심" },
            { 61, u8"경성" }, { 62, u8"직정" }, { 63, u8"자만" }, { 64, u8"반감" }, { 65, u8"소심" },
            { 66, u8"조급" }, { 67, u8"성걸" }, { 68, u8"이기적" }, { 69, u8"병약" }, { 70, u8"거만" },
            { 201, u8"괴물" }, { 202, u8"잔병첩보" }
        };
        auto it = traitsMap.find(id);
        return (it != traitsMap.end()) ? it->second : "Unknown";
    }

    const char* GetOffsetLabel(int offset) {
        switch (offset) {
            case 0x02C: return u8"소재지/좌표 (short)";
            case 0x02E: return u8"얼굴 번호 (short)";
            case 0x020: return u8"소속 군단 주소 (uintptr)";
            case 0x032: return u8"등장년도 (short)";
            case 0x034: return u8"생년 (short)";
            case 0x036: return u8"몰년 (short)";
            case 0x05C: return u8"소속 도시 (byte)";
            case 0x060: return u8"소재 도시 (byte)";
            case 0x88:  return u8"기재 포인터 배열 (uintptr*)";
            case 0xAA: return u8"통솔 (byte)";
            case 0xAB: return u8"무력 (byte)";
            case 0xAC: return u8"지력 (byte)";
            case 0xAD: return u8"정치 (byte)";
            case 0xAE: return u8"매력 (byte)";
            case 0xA5: return u8"모델 번호 (byte)";
            case 0xA6: return u8"모델 색상 (byte)";
            case 0xE8: return u8"봉록 (int?)";
            case 0xEC: return u8"충성 (byte)";
            case 0xED: return u8"전략포인트 (byte)";
            case 0xEE: return u8"행동력 (byte)";
            case 0x100: return u8"공적 (short)";
            case 0x104: return u8"문명 (short)";
            case 0x106: return u8"무명 (short)";
            case 0x108: return u8"악명 (short)";
            case 0x374: return u8"사망 플래그 (int)";
            default: return nullptr;
        }
    }

    std::string GetFormattedEffectDescription(const TalentEffect& effect) {
        LoadEffectDefinitions();
        auto it = g_effectDefs.find(effect.effectId);
        if (it == g_effectDefs.end()) return "";
        const EffectDef& def = it->second;
        std::string result = def.templateStr;
        size_t valPos = result.find("{value}");
        if (valPos != std::string::npos) {
            auto vIt = def.valueMap.find(effect.val1);
            result.replace(valPos, 7, (vIt != def.valueMap.end()) ? vIt->second : std::to_string(effect.val1));
        }
        size_t paramPos = result.find("{param}");
        if (paramPos != std::string::npos) {
            auto pIt = def.paramMap.find(effect.val2);
            result.replace(paramPos, 7, (pIt != def.paramMap.end()) ? pIt->second : std::to_string(effect.val2));
        }
        return result;
    }

}
