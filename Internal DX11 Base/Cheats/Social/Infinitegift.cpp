#include "pch.h"

#include "Cheats.h"
#include "showlog.h"

#include <psapi.h>

namespace DX11Base {
  // ───────────────────────────────────────────────
  // 무한 기증
  // 원본: or [rsi+0x320], 0x400
  // 패치: and [rsi+0x320], 0xFFFFFBFF
  // ───────────────────────────────────────────────

  static uintptr_t g_giftHookAddr = 0;
  static bool g_giftApplied = false;

  namespace {
    static const uint8_t kGiftOriginalBytes[10] = {
        0x81, 0x8E, 0x20, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00};
    static const uint8_t kGiftPatchedBytes[10] = {
        0x81, 0xA6, 0x20, 0x03, 0x00, 0x00, 0xFF, 0xFB, 0xFF, 0xFF};

    static uintptr_t ResolveGiftFlagCode(uintptr_t exeBase) {
      uintptr_t found = FindPattern(
          exeBase, exeBase + 0x3000000,
          "81 8E 20 03 00 00 00 04 00 00");
      if (!found) {
        found = FindPattern(
            exeBase, exeBase + 0x3000000,
            "81 A6 20 03 00 00 FF FB FF FF");
      }
      return found;
    }

    static bool WriteGiftCode(const uint8_t* bytes) {
      if (!g_giftHookAddr || !bytes)
        return false;

      DWORD old = 0, tmp = 0;
      if (!VirtualProtect(
              (LPVOID)g_giftHookAddr, 10, PAGE_EXECUTE_READWRITE, &old))
        return false;

      memcpy((void*)g_giftHookAddr, bytes, 10);
      FlushInstructionCache(
          GetCurrentProcess(), (LPCVOID)g_giftHookAddr, 10);
      VirtualProtect((LPVOID)g_giftHookAddr, 10, old, &tmp);
      return true;
    }
  }

  void SetInfiniteGift(bool enable) {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return;

    if (!g_giftHookAddr)
      g_giftHookAddr = ResolveGiftFlagCode(exeBase);

    if (!g_giftHookAddr)
      return;

    const bool isOriginal =
        memcmp((const void*)g_giftHookAddr, kGiftOriginalBytes, 10) == 0;
    const bool isPatched =
        memcmp((const void*)g_giftHookAddr, kGiftPatchedBytes, 10) == 0;

    if (enable) {
      if (isPatched) {
        g_giftApplied = true;
        return;
      }

      if (!isOriginal) {
        AddLog(u8"[기증] 예상하지 못한 코드 상태라 무제한 패치를 적용하지 않았습니다.");
        return;
      }

      if (WriteGiftCode(kGiftPatchedBytes))
        g_giftApplied = true;

    } else {
      // 실제 코드가 패치 상태라면 내부 상태와 상관없이 원본으로 복구.
      if (isPatched) {
        if (WriteGiftCode(kGiftOriginalBytes))
          AddLog(u8"[기증] 무제한 코드 원본 복구 완료.");
      }
      g_giftApplied = false;
    }
  }
} // namespace DX11Base
