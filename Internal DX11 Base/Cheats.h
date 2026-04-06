#pragma once
#include "pch.h"

namespace DX11Base {

  // Initialize cheat subsystem (resolve pointers / offsets)
  bool InitCheats();

  // Return resolved gameBase address (二쇱씤怨??뺣낫 踰좎씠??
  uintptr_t GetGameBase();

  // Return resolved base address for traits (0 if not found)
  uintptr_t GetTraitsBase();

  // Set gold amount (safely)
  void SetGold(int amount);
  void ModifyStat(uintptr_t targetBase, uintptr_t offset, int value, int size);
  void ModifyStatFast(uintptr_t targetAddr, int value, int size);
    bool InstallHeroHook();
    void MaximizeHeroStats();
    void SetFactionLordBonus(bool enable);

    extern uintptr_t g_HeroAddr;
    extern bool g_isHeroHookInstalled;
    struct OfficerInfo {
      uintptr_t address;
      int id;
      int lead, war, intel, pol, cha;
    };

    uintptr_t FindPattern(uintptr_t start, uintptr_t end, const std::string &pattern);
    bool IsValidPtr(uintptr_t addr, SIZE_T size);
  } // namespace DX11Base