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

    void ScanExactDuelDebateWriteCandidates() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase) {
            AddLog(u8"[대련토론DBG] SAN8R.exe 베이스를 찾지 못했습니다.");
            return;
        }

        MODULEINFO mi{};
        if (!GetModuleInformation(
                GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi))) {
            AddLog(u8"[대련토론DBG] 모듈 정보를 읽지 못했습니다.");
            return;
        }

        const uint8_t* b = (const uint8_t*)exeBase;
        const size_t len = (size_t)mi.SizeOfImage;
        unsigned int duelHits = 0;
        unsigned int debateHits = 0;

        auto logHit = [&](const char* action, const char* kind, size_t i) {
            const size_t before = (i >= 16) ? 16 : i;
            const size_t start = i - before;
            const size_t maxN = 48;
            const size_t n = (start + maxN <= len) ? maxN : (len - start);

            char bytes[maxN * 3 + 1] = {};
            size_t out = 0;
            for (size_t k = 0; k < n && out + 4 < sizeof(bytes); ++k) {
                out += (size_t)snprintf(
                    bytes + out, sizeof(bytes) - out,
                    "%02X%s", b[start + k], (k + 1 < n) ? " " : "");
            }

            AddLog(
                u8"[대련토론DBG] %s %s RVA:+0x%llX bytes[%llX~%llX]=%s",
                action,
                kind,
                (unsigned long long)i,
                (unsigned long long)start,
                (unsigned long long)(start + n - 1),
                bytes);
        };

        auto countHit = [&](bool duel, const char* kind, size_t i) {
            if (duel) {
                ++duelHits;
                logHit(u8"대련(bit8)", kind, i);
            } else {
                ++debateHits;
                logHit(u8"토론(bit9)", kind, i);
            }
        };

        for (size_t i = 0; i + 8 <= len; ++i) {
            // 1) OR byte ptr [reg+0x321], 01/02
            //    bit8/bit9는 +0x321 바이트의 bit0/bit1과 동일.
            if (b[i] == 0x80 &&
                (b[i + 1] & 0xF8) == 0x88) {
                const uint8_t rm = b[i + 1] & 0x07;
                if (rm != 0x04) {
                    if (b[i + 2] == 0x21 && b[i + 3] == 0x03 &&
                        b[i + 4] == 0x00 && b[i + 5] == 0x00) {
                        if (b[i + 6] == 0x01)
                            countHit(true, "OR byte [reg+321],01", i);
                        else if (b[i + 6] == 0x02)
                            countHit(false, "OR byte [reg+321],02", i);
                    }
                } else if (i + 8 <= len) {
                    // SIB형: 80 /1 modrm sib disp32 imm8
                    if (b[i + 3] == 0x21 && b[i + 4] == 0x03 &&
                        b[i + 5] == 0x00 && b[i + 6] == 0x00) {
                        if (b[i + 7] == 0x01)
                            countHit(true, "OR byte [reg+321],01 (SIB)", i);
                        else if (b[i + 7] == 0x02)
                            countHit(false, "OR byte [reg+321],02 (SIB)", i);
                    }
                }
            }

            // 2) OR dword ptr [reg+0x320], 00000100/00000200
            if (i + 10 <= len &&
                b[i] == 0x81 &&
                (b[i + 1] & 0xF8) == 0x88) {
                const uint8_t rm = b[i + 1] & 0x07;
                if (rm != 0x04 &&
                    b[i + 2] == 0x20 && b[i + 3] == 0x03 &&
                    b[i + 4] == 0x00 && b[i + 5] == 0x00) {
                    const uint32_t imm = *(const uint32_t*)&b[i + 6];
                    if (imm == 0x00000100u)
                        countHit(true, "OR dword [reg+320],100", i);
                    else if (imm == 0x00000200u)
                        countHit(false, "OR dword [reg+320],200", i);
                } else if (rm == 0x04 && i + 11 <= len &&
                           b[i + 3] == 0x20 && b[i + 4] == 0x03 &&
                           b[i + 5] == 0x00 && b[i + 6] == 0x00) {
                    const uint32_t imm = *(const uint32_t*)&b[i + 7];
                    if (imm == 0x00000100u)
                        countHit(true, "OR dword [reg+320],100 (SIB)", i);
                    else if (imm == 0x00000200u)
                        countHit(false, "OR dword [reg+320],200 (SIB)", i);
                }
            }

            // 3) OR word ptr [reg+0x320], 0100/0200
            if (i + 9 <= len &&
                b[i] == 0x66 && b[i + 1] == 0x81 &&
                (b[i + 2] & 0xF8) == 0x88) {
                const uint8_t rm = b[i + 2] & 0x07;
                if (rm != 0x04 &&
                    b[i + 3] == 0x20 && b[i + 4] == 0x03 &&
                    b[i + 5] == 0x00 && b[i + 6] == 0x00) {
                    const uint16_t imm = *(const uint16_t*)&b[i + 7];
                    if (imm == 0x0100u)
                        countHit(true, "OR word [reg+320],0100", i);
                    else if (imm == 0x0200u)
                        countHit(false, "OR word [reg+320],0200", i);
                }
            }

            // 4) BTS dword ptr [reg+0x320], 8/9
            if (b[i] == 0x0F && b[i + 1] == 0xBA &&
                (b[i + 2] & 0xF8) == 0xA8) {
                const uint8_t rm = b[i + 2] & 0x07;
                if (rm != 0x04 &&
                    b[i + 3] == 0x20 && b[i + 4] == 0x03 &&
                    b[i + 5] == 0x00 && b[i + 6] == 0x00) {
                    if (b[i + 7] == 0x08)
                        countHit(true, "BTS dword [reg+320],8", i);
                    else if (b[i + 7] == 0x09)
                        countHit(false, "BTS dword [reg+320],9", i);
                }
            }
        }

        AddLog(
            u8"[대련토론DBG] 읽기 전용 검색 완료: 대련 후보=%u / 토론 후보=%u",
            duelHits, debateHits);
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