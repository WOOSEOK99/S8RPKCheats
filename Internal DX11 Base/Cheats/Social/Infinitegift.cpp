#include "pch.h"

#include "Cheats.h"
#include "InstantLoveCave.h"
#include "Loyaltycave.h"
#include "Resonancecave.h"
#include "showlog.h"

#include <psapi.h>
#include <string>
#include <vector>

namespace DX11Base {
  // ───────────────────────────────────────────────
  //  무한 기증 패치
  //  원본: or [rsi+0x320], 0x400   (10바이트) - 기증 완료 플래그 세팅
  //  패치: and [rsi+0x320], 0xFFFFFBFF        - 플래그 클리어 (항상 기증 가능)
  // ───────────────────────────────────────────────

  static uintptr_t g_giftHookAddr = 0;
  static uint8_t g_giftOriginal[10] = {};
  static bool g_giftApplied = false;

  void SetInfiniteGift(bool enable) {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return;

    if (!g_giftHookAddr) {
      uintptr_t found = FindPattern(exeBase, exeBase + 0x3000000, "81 8E 20 03 00 00 00 04 00 00");
      if (found) g_giftHookAddr = found;
    }

    AddLog("[DEBUG] giftHookAddr: %p", (void *)g_giftHookAddr);

    if (!g_giftHookAddr)
      return;

    if (!g_giftApplied && enable) {
      memcpy(g_giftOriginal, (void *)g_giftHookAddr, 10);

      DWORD old, tmp;
      VirtualProtect((LPVOID)g_giftHookAddr, 10, PAGE_EXECUTE_READWRITE, &old);

      // and [rsi+0x320], 0xFFFFFBFF
      // 81 A6 20 03 00 00 FF FB FF FF
      uint8_t patch[] = {0x81, 0xA6, 0x20, 0x03, 0x00, 0x00, 0xFF, 0xFB, 0xFF, 0xFF};
      memcpy((void *)g_giftHookAddr, patch, 10);

      VirtualProtect((LPVOID)g_giftHookAddr, 10, old, &tmp);
      g_giftApplied = true;

    } else if (g_giftApplied && !enable) {
      DWORD old, tmp;
      VirtualProtect((LPVOID)g_giftHookAddr, 10, PAGE_EXECUTE_READWRITE, &old);
      memcpy((void *)g_giftHookAddr, g_giftOriginal, 10);
      VirtualProtect((LPVOID)g_giftHookAddr, 10, old, &tmp);
      g_giftApplied = false;
    }
  }
} // namespace DX11Base