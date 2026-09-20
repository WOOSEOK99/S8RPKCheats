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
        struct InteractionCodePatch {
            uintptr_t addr = 0;
            uint8_t original[10] = {};
            uint8_t size = 0;
        };

        static std::vector<InteractionCodePatch> g_duelPatches;
        static std::vector<InteractionCodePatch> g_debatePatches;
        static bool g_duelApplied = false;
        static bool g_debateApplied = false;

        static bool IsExecutableAddress(uintptr_t addr) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)))
                return false;

            const DWORD p = mbi.Protect & 0xFFu;
            return p == PAGE_EXECUTE ||
                   p == PAGE_EXECUTE_READ ||
                   p == PAGE_EXECUTE_READWRITE ||
                   p == PAGE_EXECUTE_WRITECOPY;
        }

        static bool WriteCodeBytes(
            uintptr_t addr, const uint8_t* bytes, size_t size) {
            if (!addr || !bytes || size == 0)
                return false;

            DWORD old = 0, tmp = 0;
            if (!VirtualProtect(
                    (LPVOID)addr, size, PAGE_EXECUTE_READWRITE, &old))
                return false;

            memcpy((void*)addr, bytes, size);
            FlushInstructionCache(
                GetCurrentProcess(), (LPCVOID)addr, size);
            VirtualProtect((LPVOID)addr, size, old, &tmp);
            return true;
        }

        static void SavePatch(
            std::vector<InteractionCodePatch>& patches,
            uintptr_t addr,
            const uint8_t* original,
            size_t size) {
            InteractionCodePatch p{};
            p.addr = addr;
            p.size = (uint8_t)size;
            memcpy(p.original, original, size);
            patches.push_back(p);
        }

        static size_t ApplyInteractionBitPatch(
            uint8_t bitIndex,
            uint32_t bitMask,
            std::vector<InteractionCodePatch>& patches,
            const char* label) {
            uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
            if (!exeBase)
                return 0;

            MODULEINFO mi{};
            if (!GetModuleInformation(
                    GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi)))
                return 0;

            const uint8_t* code = (const uint8_t*)exeBase;
            const size_t len = (size_t)mi.SizeOfImage;
            const uint32_t clearMask = ~bitMask;

            patches.clear();
            size_t directOrCount = 0;
            size_t directBtsCount = 0;

            for (size_t i = 0; i + 10 <= len; ++i) {
                const uintptr_t addr = exeBase + i;
                if (!IsExecutableAddress(addr))
                    continue;

                // 기증과 동일한 형태:
                //   or dword ptr [reg+0x320], imm32
                // 대련 imm32=0x100, 토론 imm32=0x200
                if (code[i] == 0x81 &&
                    (code[i + 1] & 0xF8) == 0x88 &&
                    (code[i + 1] & 0x07) != 0x04 &&
                    code[i + 2] == 0x20 && code[i + 3] == 0x03 &&
                    code[i + 4] == 0x00 && code[i + 5] == 0x00 &&
                    *(const uint32_t*)&code[i + 6] == bitMask) {

                    uint8_t patched[10];
                    memcpy(patched, &code[i], 10);

                    // OR(/1) -> AND(/4), displacement/register는 그대로 유지.
                    patched[1] =
                        (uint8_t)((patched[1] & 0xC7u) | 0x20u);
                    *(uint32_t*)&patched[6] = clearMask;

                    if (WriteCodeBytes(addr, patched, 10)) {
                        SavePatch(patches, addr, &code[i], 10);
                        ++directOrCount;
                        AddLog(
                            u8"[%s] OR->AND 패치 RVA:+0x%llX",
                            label,
                            (unsigned long long)i);
                    }
                    continue;
                }

                // 옛 CT와 같은 직접 BTS 형태:
                //   bts dword ptr [reg+0x320], bit
                if (i + 8 <= len &&
                    code[i] == 0x0F && code[i + 1] == 0xBA &&
                    (code[i + 2] & 0xF8) == 0xA8 &&
                    (code[i + 2] & 0x07) != 0x04 &&
                    code[i + 3] == 0x20 && code[i + 4] == 0x03 &&
                    code[i + 5] == 0x00 && code[i + 6] == 0x00 &&
                    code[i + 7] == bitIndex) {

                    uint8_t patched[8];
                    memcpy(patched, &code[i], 8);

                    // BTS(/5) -> BTR(/6)
                    patched[2] =
                        (uint8_t)((patched[2] & 0xC7u) | 0x30u);

                    if (WriteCodeBytes(addr, patched, 8)) {
                        SavePatch(patches, addr, &code[i], 8);
                        ++directBtsCount;
                        AddLog(
                            u8"[%s] BTS->BTR 패치 RVA:+0x%llX",
                            label,
                            (unsigned long long)i);
                    }
                }
            }

            AddLog(
                u8"[%s] +0x320 bit%d 패치 완료: OR형=%llu / BTS형=%llu",
                label,
                (int)bitIndex,
                (unsigned long long)directOrCount,
                (unsigned long long)directBtsCount);

            return patches.size();
        }

        static void RestoreInteractionBitPatches(
            std::vector<InteractionCodePatch>& patches) {
            for (const auto& p : patches) {
                if (p.addr && p.size)
                    WriteCodeBytes(p.addr, p.original, p.size);
            }
            patches.clear();
        }
    }

    void SetInfiniteDuel(bool enable) {
        if (enable) {
            if (g_duelApplied)
                return;

            const size_t count = ApplyInteractionBitPatch(
                0x08, 0x00000100u, g_duelPatches, u8"대련");
            g_duelApplied = (count > 0);

            if (!g_duelApplied)
                AddLog(u8"[대련] +0x320 bit8 실제 사용 코드 패치를 찾지 못했습니다.");
        } else {
            RestoreInteractionBitPatches(g_duelPatches);
            g_duelApplied = false;
        }
    }

    void SetInfiniteDebate(bool enable) {
        if (enable) {
            if (g_debateApplied)
                return;

            const size_t count = ApplyInteractionBitPatch(
                0x09, 0x00000200u, g_debatePatches, u8"토론");
            g_debateApplied = (count > 0);

            if (!g_debateApplied)
                AddLog(u8"[토론] +0x320 bit9 실제 사용 코드 패치를 찾지 못했습니다.");
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
