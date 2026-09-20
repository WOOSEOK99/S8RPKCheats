#include "../../pch.h"
#include "Infinitetalk.h"
#include "../../Cheats.h"
#include "InstantLoveCave.h"
#include "Loyaltycave.h"
#include "Resonancecave.h"
#include "../../showlog.h"

#include <psapi.h>
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

    void ScanDuelDebateFlagCandidates() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) {
            AddLog(u8"[교류DBG] SAN8R.exe 베이스를 찾지 못했습니다.");
            return;
        }

        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi))) {
            AddLog(u8"[교류DBG] 모듈 정보를 읽지 못했습니다.");
            return;
        }

        const uintptr_t exeEnd = exeBase + mi.SizeOfImage;
        const uint8_t* b = (const uint8_t*)exeBase;
        const size_t len = (size_t)mi.SizeOfImage;

        unsigned int duelHits = 0;
        unsigned int debateHits = 0;

        auto logHit = [&](const char* name, const char* kind, size_t i) {
            const uintptr_t addr = exeBase + i;
            const size_t remain = len - i;
            const size_t n = remain < 16 ? remain : 16;
            char bytes[16 * 3 + 1] = {};
            size_t p = 0;
            for (size_t k = 0; k < n && p + 3 < sizeof(bytes); ++k) {
                p += (size_t)snprintf(bytes + p, sizeof(bytes) - p,
                                      "%02X%s", b[i + k], (k + 1 < n) ? " " : "");
            }
            AddLog(u8"[교류DBG] %s 후보 %s RVA:+0x%llX bytes=%s",
                   name, kind,
                   (unsigned long long)(addr - exeBase),
                   bytes);
        };

        for (size_t i = 0; i + 10 <= len; ++i) {
            // OR dword ptr [reg+0x320], imm32
            if (b[i] == 0x81 &&
                (b[i + 1] & 0xF8) == 0x88 &&
                b[i + 2] == 0x20 && b[i + 3] == 0x03 &&
                b[i + 4] == 0x00 && b[i + 5] == 0x00) {

                const uint32_t imm = *(const uint32_t*)&b[i + 6];
                if (imm == 0x00000100u) {
                    ++duelHits;
                    logHit("대련(bit8)", "OR [reg+320],100", i);
                } else if (imm == 0x00000200u) {
                    ++debateHits;
                    logHit("토론(bit9)", "OR [reg+320],200", i);
                }
            }

            // BTS dword ptr [reg+0x320], imm8
            if (b[i] == 0x0F && b[i + 1] == 0xBA &&
                (b[i + 2] & 0xF8) == 0xA8 &&
                b[i + 3] == 0x20 && b[i + 4] == 0x03 &&
                b[i + 5] == 0x00 && b[i + 6] == 0x00) {

                if (b[i + 7] == 0x08) {
                    ++duelHits;
                    logHit("대련(bit8)", "BTS [reg+320],8", i);
                } else if (b[i + 7] == 0x09) {
                    ++debateHits;
                    logHit("토론(bit9)", "BTS [reg+320],9", i);
                }
            }

            // 레지스터에서 bit를 세운 뒤 메모리에 저장하는 형태도 보조 탐색.
            if (b[i] == 0x0F && b[i + 1] == 0xBA &&
                (b[i + 2] & 0xF8) == 0xE8) {
                if (b[i + 3] == 0x08) {
                    ++duelHits;
                    logHit("대련(bit8)", "BTS reg,8", i);
                } else if (b[i + 3] == 0x09) {
                    ++debateHits;
                    logHit("토론(bit9)", "BTS reg,9", i);
                }
            }
        }

        AddLog(u8"[교류DBG] 검색 완료. 대련(bit8) 후보=%u / 토론(bit9) 후보=%u / EXE size=0x%llX",
               duelHits, debateHits, (unsigned long long)(exeEnd - exeBase));
    }

    void LogDuelDebateFocusedCandidates() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) {
            AddLog(u8"[교류집중DBG] SAN8R.exe 베이스를 찾지 못했습니다.");
            return;
        }

        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi))) {
            AddLog(u8"[교류집중DBG] 모듈 정보를 읽지 못했습니다.");
            return;
        }
        const uintptr_t exeEnd = exeBase + mi.SizeOfImage;

        struct Candidate {
            const char* name;
            uintptr_t rva;
        };
        const Candidate candidates[] = {
            {"토론A", 0x16F584F},
            {"대련A", 0x16F587F},
            {"토론B", 0x16F5F0F},
            {"대련B", 0x16F5F5F},
        };

        for (const auto& c : candidates) {
            const uintptr_t center = exeBase + c.rva;
            if (center < exeBase + 0x20 || center + 0x40 >= exeEnd)
                continue;

            const uintptr_t start = center - 0x20;
            const uint8_t* p = (const uint8_t*)start;

            char line[3 * 96 + 1] = {};
            size_t out = 0;
            for (size_t i = 0; i < 96 && out + 4 < sizeof(line); ++i) {
                out += (size_t)snprintf(line + out, sizeof(line) - out,
                                        "%02X%s", p[i], (i + 1 < 96) ? " " : "");
            }

            AddLog(u8"[교류집중DBG] %s center=RVA:+0x%llX range=+0x%llX~+0x%llX",
                   c.name,
                   (unsigned long long)c.rva,
                   (unsigned long long)(c.rva - 0x20),
                   (unsigned long long)(c.rva + 0x3F));
            AddLog("[교류집중DBG] %s bytes=%s", c.name, line);
        }
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