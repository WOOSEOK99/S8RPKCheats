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
      if (!VirtualProtect((LPVOID)g_giftHookAddr, 10, PAGE_EXECUTE_READWRITE, &old))
        return false;

      memcpy((void*)g_giftHookAddr, bytes, 10);
      FlushInstructionCache(GetCurrentProcess(), (LPCVOID)g_giftHookAddr, 10);
      VirtualProtect((LPVOID)g_giftHookAddr, 10, old, &tmp);
      return true;
    }
  }

  void LogDuelDebateNearGiftCandidates() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase) {
      AddLog(u8"[교류근처DBG] SAN8R.exe 베이스를 찾지 못했습니다.");
      return;
    }

    uintptr_t gift = ResolveGiftFlagCode(exeBase);
    if (!gift) {
      AddLog(u8"[교류근처DBG] 현재 기증 +0x320 코드를 찾지 못했습니다.");
      return;
    }

    // 옛 CT에서 같은 +0x320 플래그 묶음:
    // 토론 = 기증 - 0x210, 대련 = 기증 - 0x180
    const uintptr_t debate = gift - 0x210;
    const uintptr_t duel = gift - 0x180;

    auto logWindow = [&](const char* name, uintptr_t center) {
      constexpr size_t kBefore = 0x30;
      constexpr size_t kSize = 0x70;
      if (center <= exeBase + kBefore || !IsValidPtr(center - kBefore, kSize)) {
        AddLog(u8"[교류근처DBG] %s 범위가 유효하지 않습니다.", name);
        return;
      }

      const uint8_t* p = (const uint8_t*)(center - kBefore);
      char bytes[kSize * 3 + 1] = {};
      size_t out = 0;
      for (size_t i = 0; i < kSize && out + 4 < sizeof(bytes); ++i) {
        out += (size_t)snprintf(
            bytes + out, sizeof(bytes) - out,
            "%02X%s", p[i], (i + 1 < kSize) ? " " : "");
      }

      AddLog(u8"[교류근처DBG] %s 예상 RVA:+0x%llX range=+0x%llX~+0x%llX",
             name,
             (unsigned long long)(center - exeBase),
             (unsigned long long)(center - kBefore - exeBase),
             (unsigned long long)(center - kBefore + kSize - 1 - exeBase));
      AddLog("[교류근처DBG] %s bytes=%s", name, bytes);
    };

    AddLog(u8"[교류근처DBG] 기증 기준 RVA:+0x%llX / 토론 예상=-0x210 / 대련 예상=-0x180",
           (unsigned long long)(gift - exeBase));
    logWindow(u8"토론", debate);
    logWindow(u8"대련", duel);
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

      memcpy(g_giftOriginal, kGiftOriginalBytes, 10);
      if (WriteGiftCode(kGiftPatchedBytes))
        g_giftApplied = true;

    } else {
      // 디버그 훅/상태 플래그와 무관하게 실제 코드가 AND 패치로 남아 있으면
      // 항상 원본 OR 명령으로 복구한다.
      if (isPatched) {
        if (WriteGiftCode(kGiftOriginalBytes)) {
          AddLog(u8"[기증] 무제한 코드 원본 복구 완료.");
        }
      }
      g_giftApplied = false;
    }
  }} // namespace DX11Base