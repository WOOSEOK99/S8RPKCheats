#include "../../pch.h"
#include "Infinitetalk.h"
#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"

#include <psapi.h>
#include <vector>

namespace DX11Base {
    // ───────────────────────────────────────────────
    // 무한 담화
    // 원본: or dword ptr [rdi+0x320], 0x04
    // 패치: and dword ptr [rdi+0x320], 0xFFFFFFFB
    // ───────────────────────────────────────────────
    static uintptr_t g_talkHookAddr = 0;
    static uint8_t g_talkOriginal[7] = {};
    static uintptr_t g_talkCaveAddr = 0;
    static bool g_talkApplied = false;

    namespace {
        struct InteractionBitPatch {
            uintptr_t addr = 0;
            uint8_t originalModRm = 0;
        };

        static std::vector<InteractionBitPatch> g_duelPatches;
        static std::vector<InteractionBitPatch> g_debatePatches;
        static bool g_duelApplied = false;
        static bool g_debateApplied = false;

        static bool Has320ReadAndWriteNearby(
            const uint8_t* code, size_t len, size_t center) {
            const size_t begin = (center > 40) ? center - 40 : 0;
            const size_t end = (center + 48 < len) ? center + 48 : len;

            bool hasRead = false;
            bool hasWrite = false;

            for (size_t j = begin; j + 6 <= end; ++j) {
                // mov r32,[reg+0x320]
                if (code[j] == 0x8B &&
                    (code[j + 1] & 0xC0) == 0x80 &&
                    code[j + 2] == 0x20 && code[j + 3] == 0x03 &&
                    code[j + 4] == 0x00 && code[j + 5] == 0x00) {
                    hasRead = true;
                }

                // mov [reg+0x320],r32
                if (code[j] == 0x89 &&
                    (code[j + 1] & 0xC0) == 0x80 &&
                    code[j + 2] == 0x20 && code[j + 3] == 0x03 &&
                    code[j + 4] == 0x00 && code[j + 5] == 0x00) {
                    hasWrite = true;
                }
            }

            return hasRead && hasWrite;
        }

        static bool WriteOneByte(uintptr_t addr, uint8_t value) {
            DWORD old = 0, tmp = 0;
            if (!VirtualProtect((LPVOID)addr, 1, PAGE_EXECUTE_READWRITE, &old))
                return false;

            *(uint8_t*)addr = value;
            FlushInstructionCache(GetCurrentProcess(), (LPCVOID)addr, 1);
            VirtualProtect((LPVOID)addr, 1, old, &tmp);
            return true;
        }

        static size_t ApplyInteractionBitPatch(
            uint8_t bitIndex,
            std::vector<InteractionBitPatch>& patches) {
            uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
            if (!exeBase)
                return 0;

            MODULEINFO mi{};
            if (!GetModuleInformation(
                    GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi)))
                return 0;

            const uint8_t* code = (const uint8_t*)exeBase;
            const size_t len = (size_t)mi.SizeOfImage;

            patches.clear();

            for (size_t i = 0; i + 4 <= len; ++i) {
                // BTS r32, imm8. 옛 CT와 현재 코드 모두
                // +0x320을 읽고/쓰는 setter 내부에서 bit8/bit9를 BTS로 세운다.
                if (code[i] == 0x0F && code[i + 1] == 0xBA &&
                    (code[i + 2] & 0xC0) == 0xC0 &&
                    (code[i + 2] & 0x38) == 0x28 &&
                    code[i + 3] == bitIndex &&
                    Has320ReadAndWriteNearby(code, len, i)) {

                    const uintptr_t modrmAddr = exeBase + i + 2;
                    const uint8_t original = code[i + 2];
                    const uint8_t patched =
                        (uint8_t)((original & 0xC7u) | 0x30u); // BTS(/5) -> BTR(/6)

                    if (WriteOneByte(modrmAddr, patched)) {
                        patches.push_back({modrmAddr, original});
                    }
                    continue;
                }

                // BTS dword ptr [reg+0x320], imm8 직접형.
                if (i + 8 <= len &&
                    code[i] == 0x0F && code[i + 1] == 0xBA &&
                    (code[i + 2] & 0xC0) == 0x80 &&
                    (code[i + 2] & 0x38) == 0x28 &&
                    (code[i + 2] & 0x07) != 0x04 && // SIB형 제외
                    code[i + 3] == 0x20 && code[i + 4] == 0x03 &&
                    code[i + 5] == 0x00 && code[i + 6] == 0x00 &&
                    code[i + 7] == bitIndex) {

                    const uintptr_t modrmAddr = exeBase + i + 2;
                    const uint8_t original = code[i + 2];
                    const uint8_t patched =
                        (uint8_t)((original & 0xC7u) | 0x30u);

                    if (WriteOneByte(modrmAddr, patched)) {
                        patches.push_back({modrmAddr, original});
                    }
                }
            }

            return patches.size();
        }

        static void RestoreInteractionBitPatches(
            std::vector<InteractionBitPatch>& patches) {
            for (const auto& p : patches) {
                if (p.addr)
                    WriteOneByte(p.addr, p.originalModRm);
            }
            patches.clear();
        }
    }

    void SetInfiniteDuel(bool enable) {
        if (enable) {
            if (g_duelApplied)
                return;

            const size_t count = ApplyInteractionBitPatch(0x08, g_duelPatches);
            g_duelApplied = (count > 0);
            AddLog(u8"[대련] +0x320 bit8 무제한 패치 %s (적용 지점 %llu개)",
                   g_duelApplied ? u8"적용" : u8"실패",
                   (unsigned long long)count);
        } else {
            RestoreInteractionBitPatches(g_duelPatches);
            g_duelApplied = false;
        }
    }

    void SetInfiniteDebate(bool enable) {
        if (enable) {
            if (g_debateApplied)
                return;

            const size_t count = ApplyInteractionBitPatch(0x09, g_debatePatches);
            g_debateApplied = (count > 0);
            AddLog(u8"[토론] +0x320 bit9 무제한 패치 %s (적용 지점 %llu개)",
                   g_debateApplied ? u8"적용" : u8"실패",
                   (unsigned long long)count);
        } else {
            RestoreInteractionBitPatches(g_debatePatches);
            g_debateApplied = false;
        }
    }

    void SetInfiniteTalk(bool enable) {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase)
            return;

        if (!g_talkHookAddr) {
            uintptr_t found = FindPattern(
                exeBase, exeBase + 0x3000000,
                "83 8F 20 03 00 00 04");
            if (found)
                g_talkHookAddr = found;
        }
        if (!g_talkHookAddr)
            return;

        if (!g_talkApplied && enable) {
            memcpy(g_talkOriginal, (void*)g_talkHookAddr, 7);

            g_talkCaveAddr = AllocNear(g_talkHookAddr, 128);
            if (!g_talkCaveAddr)
                return;

            uint8_t* cave = (uint8_t*)g_talkCaveAddr;
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

            uintptr_t retAddr = g_talkHookAddr + 7;
            cave[idx++] = 0xFF;
            cave[idx++] = 0x25;
            cave[idx++] = 0x00;
            cave[idx++] = 0x00;
            cave[idx++] = 0x00;
            cave[idx++] = 0x00;
            *(uintptr_t*)&cave[idx] = retAddr;
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
