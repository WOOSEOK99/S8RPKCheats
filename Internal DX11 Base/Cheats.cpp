#include "Cheats.h"
#include "Cheats\Social\InstantLoveCave.h"
#include "pch.h"
#include <psapi.h>
#include <string>
#include <vector>

#include "Cheats/System/SkillCountManager.h"

namespace DX11Base {

    static uintptr_t s_gameBasePtrAddr = 0;
    static uint32_t s_dynamicTraitsOffset = 0;
    extern bool bLoveCave;
    void AddLog(const char *fmt, ...);
    // ───────────────────────────────────────────────
    //  유틸리티
    // ───────────────────────────────────────────────

    bool IsValidPtr(uintptr_t addr, SIZE_T size) {
        if (!addr || size == 0)
            return false;

        // 오버플로 방지. 잘못된 범위가 wrap-around 되면 유효 주소처럼 보일 수 있습니다.
        const uintptr_t endAddr = addr + size - 1;
        if (endAddr < addr)
            return false;

        MEMORY_BASIC_INFORMATION mbiStart{};
        if (VirtualQuery((LPCVOID)addr, &mbiStart, sizeof(mbiStart)) != sizeof(mbiStart))
            return false;
        if (mbiStart.State != MEM_COMMIT || (mbiStart.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;

        // 대부분의 런타임 검사는 1~8바이트이며 같은 메모리 영역 안에 있습니다.
        // 첫 VirtualQuery가 알려준 영역 안에 끝 주소까지 포함되면 추가 시스템 호출을 생략합니다.
        const uintptr_t regionStart = (uintptr_t)mbiStart.BaseAddress;
        const SIZE_T regionSize = mbiStart.RegionSize;
        const uintptr_t regionEnd = regionStart + regionSize - 1;
        if (regionEnd >= regionStart && endAddr <= regionEnd)
            return true;

        // 실제로 다른 메모리 영역까지 걸치는 범위만 끝 주소를 한 번 더 검사합니다.
        MEMORY_BASIC_INFORMATION mbiEnd{};
        if (VirtualQuery((LPCVOID)endAddr, &mbiEnd, sizeof(mbiEnd)) != sizeof(mbiEnd))
            return false;
        if (mbiEnd.State != MEM_COMMIT || (mbiEnd.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;

        return true;
    }

    uintptr_t ResolveRelAddr(uintptr_t instAddr, int opOffset, int instSize) {
        if (!instAddr)
            return 0;
        int32_t rel = *(int32_t *)(instAddr + opOffset);
        return instAddr + rel + instSize;
    }

    uintptr_t FindPattern(uintptr_t start, uintptr_t end, const std::string &pattern) {
        std::vector<uint8_t> bytes;
        std::vector<bool> mask;
        for (size_t i = 0; i < pattern.size(); ++i) {
            if (pattern[i] == ' ')
                continue;
            if (pattern[i] == '?') {
                bytes.push_back(0);
                mask.push_back(false);
                if (i + 1 < pattern.size() && pattern[i + 1] == '?')
                    i++;
            } else {
                bytes.push_back((uint8_t)strtoul(pattern.substr(i, 2).c_str(), nullptr, 16));
                mask.push_back(true);
                i++;
            }
        }
        uint8_t *pStart = (uint8_t *)start;
        size_t searchLen = (size_t)(end - start) - bytes.size();
        for (size_t i = 0; i < searchLen; ++i) {
            bool found = true;
            for (size_t j = 0; j < bytes.size(); ++j) {
                if (mask[j] && pStart[i + j] != bytes[j]) {
                    found = false;
                    break;
                }
            }
            if (found)
                return (uintptr_t)(pStart + i);
        }
        return 0;
    }

    uintptr_t g_HeroAddr = 0;

    typedef void(__fastcall *tGetHeroStats)(void *);
    tGetHeroStats oGetHeroStats = nullptr;

    extern "C" void HookHandler();

    void __fastcall hkHeroLogic(void *rcx) {
        return oGetHeroStats(rcx);
    }

    uintptr_t GetGameBase() {
        if (!s_gameBasePtrAddr)
            return 0;

        static uintptr_t s_cachedGameBase = 0;
        static uint64_t s_lastGameBaseProbeMs = 0;

        const uint64_t now = GetTickCount64();
        uintptr_t base = *(uintptr_t *)s_gameBasePtrAddr;

        if (base != 0 && base == s_cachedGameBase && (now - s_lastGameBaseProbeMs) < 300ull)
            return base;

        s_lastGameBaseProbeMs = now;

        if (!IsValidPtr(s_gameBasePtrAddr, 8)) {
            s_cachedGameBase = 0;
            return 0;
        }
        base = *(uintptr_t *)s_gameBasePtrAddr;
        if (!base || !IsValidPtr(base, 8)) {
            s_cachedGameBase = 0;
            return 0;
        }
        s_cachedGameBase = base;
        return base;
    }

    bool InitCheats() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase, &mi, sizeof(mi)))
            return false;
        uintptr_t imgEnd = exeBase + mi.SizeOfImage;

        const std::string gameBasePat = "4C 8B 05 ? ? ? ? 41 0F B7";
        uintptr_t foundAddr = FindPattern(exeBase, imgEnd, gameBasePat);
        if (foundAddr)
            s_gameBasePtrAddr = ResolveRelAddr(foundAddr, 3, 7);

        LoadSkillCounts();

        return (s_gameBasePtrAddr != 0);
    }

    void ModifyStat(uintptr_t targetBase, uintptr_t offset, int value, int size) {
      if (!targetBase)
        return;
      uintptr_t targetAddr = targetBase + offset;
      __try {
        if (IsValidPtr(targetAddr, size)) {
          DWORD oldProt;
          if (VirtualProtect((LPVOID)targetAddr, size, PAGE_READWRITE, &oldProt)) {
            ModifyStatFast(targetAddr, value, size);
            VirtualProtect((LPVOID)targetAddr, size, oldProt, &oldProt);
          } else {
            AddLog(u8"[ERROR] VirtualProtect 실패 (Addr:%p, Size:%d)", (void *)targetAddr, size);
          }
        } else {
          AddLog(u8"[ERROR] IsValidPtr 실패 (Addr:%p, Size:%d)", (void *)targetAddr, size);
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        AddLog(u8"[CRITICAL] ModifyStat 예외 발생 (Addr:%p)", (void *)targetAddr);
      }
    }

    void ModifyStatFast(uintptr_t targetAddr, int value, int size) {
      __try {
        if (size == 1)
          *(unsigned char *)targetAddr = (unsigned char)value;
        else if (size == 2)
          *(short *)targetAddr = (short)value;
        else if (size == 4)
          *(int *)targetAddr = value;
        else if (size == 8)
          *(uint64_t *)targetAddr = (uint64_t)value;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
      }
    }

    void MaximizeHeroStats() {
        if (!g_HeroAddr || !IsValidPtr(g_HeroAddr, 0x150)) {
            AddLog("[FAIL] 주인공 주소를 아직 찾지 못했습니다. (정보창을 한번 열어주세요)");
            return;
        }

        struct Stats {
            unsigned char lead;
            unsigned char war;
            unsigned char intel;
            unsigned char pol;
            unsigned char cha;
        };

        Stats *s = (Stats *)(g_HeroAddr + 0xAA);

        DWORD old;
        if (VirtualProtect(s, sizeof(Stats), PAGE_READWRITE, &old)) {
            s->lead = 100;
            s->war = 100;
            s->intel = 100;
            s->pol = 100;
            s->cha = 100;
            VirtualProtect(s, sizeof(Stats), old, &old);
            AddLog("[SUCCESS] 주인공(%p) 모든 능력치 100 완료!", (void *)g_HeroAddr);
        }
    }

    void SetInstantAttitude(bool enable) {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase)
            return;

        static uintptr_t targetAddr = 0;
        if (targetAddr == 0)
            targetAddr = FindPattern(exeBase, exeBase + 0x2000000, "44 88 44 07 70");
        if (!targetAddr)
            return;

        DWORD oldProtect;
        if (VirtualProtect((LPVOID)targetAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            if (enable) {
                unsigned char patch[] = {0xC6, 0x44, 0x07, 0x70, 0x64};
                memcpy((void *)targetAddr, patch, 5);
            } else {
                unsigned char original[] = {0x44, 0x88, 0x44, 0x07, 0x70};
                memcpy((void *)targetAddr, original, 5);
            }
            VirtualProtect((LPVOID)targetAddr, 5, oldProtect, &oldProtect);
        }
    }
    static uintptr_t addr = 0;
    static BYTE original[8];
    static bool saved = false;
    static bool applied = false;

    void SetInstantLove() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase)
            return;

        if (!addr)
            addr = FindPattern(exeBase, exeBase + 0x3000000, "0F B6 84 18 F0 00 00 00");
        if (!addr)
            return;

        if (!saved) {
            memcpy(original, (void *)addr, 8);
            saved = true;
        }

        if (!applied) {
            DWORD old;
            VirtualProtect((LPVOID)addr, 8, PAGE_EXECUTE_READWRITE, &old);
            BYTE patch[8] = {0xB8, 0x64, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90};
            memcpy((void *)addr, patch, 8);

            VirtualProtect((LPVOID)addr, 8, old, &old);

            applied = true;
        }
    }

    void RestoreInstantLove() {
        if (addr && saved && applied) {

            DWORD old;
            VirtualProtect((LPVOID)addr, 8, PAGE_EXECUTE_READWRITE, &old);

            memcpy((void *)addr, original, 8);

            VirtualProtect((LPVOID)addr, 8, old, &old);

            applied = false;
        }
    }

    uintptr_t marriageAddr = 0;
    BYTE marriageOriginal[2];
    bool marriageSaved = false;
    bool marriageApplied = false;

    void ToggleMarriageCondition() {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase)
            return;

        if (!marriageAddr) {
            uintptr_t searchAddr = FindPattern(exeBase, exeBase + 0x3000000, "48 8B 47 10 4C 3B F0");

            if (searchAddr) {
                marriageAddr = searchAddr - 2;
                AddLog("[DEBUG] [marriageAddr]: %02X", *(uint8_t *)(marriageAddr));
            }
        }

        if (!marriageAddr)
            return;

        if (!marriageSaved) {
            memcpy(marriageOriginal, (void *)marriageAddr, 2);
            marriageSaved = true;
        }

        DWORD old;
        VirtualProtect((LPVOID)marriageAddr, 2, PAGE_EXECUTE_READWRITE, &old);

        if (!marriageApplied) {
            *(BYTE *)marriageAddr = 0xEB;
            marriageApplied = true;
        } else {
            memcpy((void *)marriageAddr, marriageOriginal, 2);
            marriageApplied = false;
        }

        VirtualProtect((LPVOID)marriageAddr, 2, old, &old);
    }

    void SetMarriageCondition(bool enable) {
        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        if (!exeBase)
            return;

        if (!marriageAddr) {
            uintptr_t searchAddr = FindPattern(exeBase, exeBase + 0x3000000, "48 8B 47 10 4C 3B F0");
            if (searchAddr)
                marriageAddr = searchAddr - 2;
        }

        if (!marriageAddr)
            return;

        if (!marriageSaved) {
            memcpy(marriageOriginal, (void *)marriageAddr, 2);
            marriageSaved = true;
        }

        if (marriageApplied == enable)
            return;

        DWORD old;
        if (VirtualProtect((LPVOID)marriageAddr, 2, PAGE_EXECUTE_READWRITE, &old)) {
            if (enable) {
                *(BYTE *)marriageAddr = 0xEB;
            } else {
                memcpy((void *)marriageAddr, marriageOriginal, 2);
            }

            marriageApplied = enable;
            VirtualProtect((LPVOID)marriageAddr, 2, old, &old);
        }
    }

    bool g_isHeroHookInstalled = false;

    bool InstallHeroHook() {
        if (g_isHeroHookInstalled)
            return true;

        uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
        uintptr_t targetAddr = FindPattern(exeBase, exeBase + 0x3000000, "41 0F B6 B7 AE 00 00 00");

        if (targetAddr && MH_Initialize() == MH_OK) {
            if (MH_CreateHook((LPVOID)targetAddr, &hkHeroLogic, (LPVOID *)&oGetHeroStats) == MH_OK) {
                if (MH_EnableHook((LPVOID)targetAddr) == MH_OK) {
                    g_isHeroHookInstalled = true;
                    AddLog(u8"[SUCCESS] 주인공 추적 후킹 설치 완료!");
                    return true;
                }
            }
        }
        AddLog(u8"[FAIL] 후킹 설치 실패 (패턴을 찾지 못함)");
        return false;
    }

    OfficerInfo GetHeroInfo() {
        OfficerInfo info = {0,};
        uintptr_t gameBase = GetGameBase();
        if (!gameBase)
            return info;

        uintptr_t *pContainer = (uintptr_t *)(gameBase + 0x3B8);
        if (IsBadReadPtr(pContainer, 8) || !*pContainer)
            return info;

        uintptr_t container = *pContainer;
        uintptr_t *pOfficerData = (uintptr_t *)(container);
        if (IsBadReadPtr(pOfficerData, 8) || !*pOfficerData)
            return info;

        uintptr_t dataAddr = *pOfficerData;
        info.address = dataAddr;
        info.id = *(unsigned short *)(dataAddr + 0x08);
        info.lead = *(unsigned char *)(dataAddr + 0xAA);
        info.war = *(unsigned char *)(dataAddr + 0xAB);
        info.intel = *(unsigned char *)(dataAddr + 0xAC);
        info.pol = *(unsigned char *)(dataAddr + 0xAD);
        info.cha = *(unsigned char *)(dataAddr + 0xAE);

        return info;
    }

} // namespace DX11Base
