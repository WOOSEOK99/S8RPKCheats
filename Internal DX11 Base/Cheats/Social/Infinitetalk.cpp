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

    void LogInteractionUsageFlags(uintptr_t officerBase) {
        if (!officerBase || !IsValidPtr(officerBase + 0x320, sizeof(uint32_t))) {
            AddLog(u8"[교류상태DBG] 무장 주소 또는 +0x320이 유효하지 않습니다. base=%p",
                   (void*)officerBase);
            return;
        }

        const uint32_t raw = *(const uint32_t*)(officerBase + 0x320);
        AddLog(u8"[교류상태DBG] base=%p +0x320 raw=0x%08X / 담화(bit2)=%u 대련(bit8)=%u 토론(bit9)=%u 기증(bit10)=%u",
               (void*)officerBase,
               raw,
               (raw & 0x00000004u) ? 1u : 0u,
               (raw & 0x00000100u) ? 1u : 0u,
               (raw & 0x00000200u) ? 1u : 0u,
               (raw & 0x00000400u) ? 1u : 0u);
    }

    namespace {
        struct InteractionCaptureSlot {
            uintptr_t hookAddr = 0;
            uintptr_t caveAddr = 0;
            uint8_t original[7] = {};
            uintptr_t capturedRcx = 0;
            uint8_t capturedDl = 0;
        };

        static InteractionCaptureSlot s_interactionCapture[4];
        static bool s_interactionCaptureInstalled = false;

        static bool InstallInteractionCaptureSlot(
            int index, uintptr_t exeBase, uintptr_t functionRva) {
            if (index < 0 || index >= 4)
                return false;

            auto& slot = s_interactionCapture[index];
            slot.hookAddr = exeBase + functionRva;

            const uint8_t expected[7] = {0x44, 0x8B, 0x81, 0x20, 0x03, 0x00, 0x00};
            if (memcmp((const void*)slot.hookAddr, expected, sizeof(expected)) != 0) {
                AddLog(u8"[교류캡처DBG] 후보%d 원본 바이트 불일치. RVA:+0x%llX",
                       index, (unsigned long long)functionRva);
                slot.hookAddr = 0;
                return false;
            }

            memcpy(slot.original, (const void*)slot.hookAddr, 7);

            slot.caveAddr = AllocNear(slot.hookAddr, 128);
            if (!slot.caveAddr) {
                AddLog(u8"[교류캡처DBG] 후보%d cave 할당 실패.", index);
                slot.hookAddr = 0;
                return false;
            }

            uint8_t* cave = (uint8_t*)slot.caveAddr;
            int p = 0;

            // push rax
            cave[p++] = 0x50;

            // mov rax, &slot.capturedRcx
            cave[p++] = 0x48;
            cave[p++] = 0xB8;
            *(uintptr_t*)&cave[p] = (uintptr_t)&slot.capturedRcx;
            p += 8;

            // mov [rax], rcx
            cave[p++] = 0x48;
            cave[p++] = 0x89;
            cave[p++] = 0x08;

            // mov rax, &slot.capturedDl
            cave[p++] = 0x48;
            cave[p++] = 0xB8;
            *(uintptr_t*)&cave[p] = (uintptr_t)&slot.capturedDl;
            p += 8;

            // mov [rax], dl
            cave[p++] = 0x88;
            cave[p++] = 0x10;

            // pop rax
            cave[p++] = 0x58;

            // 원본 첫 명령: mov r8d,[rcx+0x320]
            memcpy(&cave[p], slot.original, 7);
            p += 7;

            // 절대 복귀 점프
            const uintptr_t retAddr = slot.hookAddr + 7;
            cave[p++] = 0xFF;
            cave[p++] = 0x25;
            cave[p++] = 0x00;
            cave[p++] = 0x00;
            cave[p++] = 0x00;
            cave[p++] = 0x00;
            *(uintptr_t*)&cave[p] = retAddr;
            p += 8;

            if (!ApplyJmp(slot.hookAddr, slot.caveAddr, 7)) {
                VirtualFree((LPVOID)slot.caveAddr, 0, MEM_RELEASE);
                slot.caveAddr = 0;
                slot.hookAddr = 0;
                return false;
            }
            return true;
        }

        static void RemoveInteractionCaptureHooks() {
            for (auto& slot : s_interactionCapture) {
                if (slot.hookAddr && slot.caveAddr) {
                    RestoreBytes(slot.hookAddr, slot.original, 7);
                    VirtualFree((LPVOID)slot.caveAddr, 0, MEM_RELEASE);
                }
                slot.hookAddr = 0;
                slot.caveAddr = 0;
                slot.capturedRcx = 0;
                slot.capturedDl = 0;
            }
            s_interactionCaptureInstalled = false;
        }
    }

    bool StartDuelDebateCapture() {
        for (auto& slot : s_interactionCapture) {
            slot.capturedRcx = 0;
            slot.capturedDl = 0;
        }

        if (s_interactionCaptureInstalled) {
            AddLog(u8"[교류캡처DBG] 캡처값 초기화 완료. 이제 대련 또는 토론을 실행하세요.");
            return true;
        }

        const uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase)
            return false;

        // 앞선 바이트 검증으로 확인된 setter 함수 시작 RVA.
        const uintptr_t rvas[4] = {
            0x16F5840, // 토론 A
            0x16F5870, // 대련 A
            0x16F5F00, // 토론 B
            0x16F5F50  // 대련 B
        };

        for (int i = 0; i < 4; ++i) {
            if (!InstallInteractionCaptureSlot(i, exeBase, rvas[i])) {
                RemoveInteractionCaptureHooks();
                AddLog(u8"[교류캡처DBG] 캡처 훅 설치 실패. 원상 복구했습니다.");
                return false;
            }
        }

        s_interactionCaptureInstalled = true;
        AddLog(u8"[교류캡처DBG] 캡처 시작. 대련 또는 토론을 1회 실행한 뒤 결과 확인을 누르세요.");
        return true;
    }

    void LogDuelDebateCaptureResult() {
        static const char* names[4] = {
            u8"토론A", u8"대련A", u8"토론B", u8"대련B"
        };

        if (!s_interactionCaptureInstalled) {
            AddLog(u8"[교류캡처DBG] 먼저 캡처 시작/초기화를 눌러주세요.");
            return;
        }

        for (int i = 0; i < 4; ++i) {
            const auto& slot = s_interactionCapture[i];
            if (!slot.capturedRcx) {
                AddLog(u8"[교류캡처DBG] %s 호출없음", names[i]);
                continue;
            }

            uint32_t raw = 0;
            bool valid = IsValidPtr(slot.capturedRcx + 0x320, sizeof(uint32_t));
            if (valid)
                raw = *(const uint32_t*)(slot.capturedRcx + 0x320);

            AddLog(u8"[교류캡처DBG] %s rcx=%p dl=%u +0x320=%s0x%08X / bit8=%u bit9=%u",
                   names[i],
                   (void*)slot.capturedRcx,
                   (unsigned int)slot.capturedDl,
                   valid ? "" : "INVALID ",
                   raw,
                   (raw & 0x00000100u) ? 1u : 0u,
                   (raw & 0x00000200u) ? 1u : 0u);
        }
    }

    void StopDuelDebateCapture() {
        if (!s_interactionCaptureInstalled)
            return;
        RemoveInteractionCaptureHooks();
        AddLog(u8"[교류캡처DBG] 캡처 훅 해제 완료.");
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