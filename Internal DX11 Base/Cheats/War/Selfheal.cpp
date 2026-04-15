#include "../../pch.h"

#include "../../Cheats.h"
#include "../Social/Fastrelationship.h"
#include "../Social/Infinitetalk.h"
#include "../Social/InstantLoveCave.h"
#include "../Social/Loyaltycave.h"
#include "../Social/Resonancecave.h"
#include "Selfheal.h"
#include "../../showlog.h"

#include <psapi.h>
#include <string>
#include <vector>

namespace DX11Base {
  // ───────────────────────────────────────────────
  //  전투 시 자가 치료 패치
  //  포인터 체인: SAN8RPK.exe+02ED7A10 → +110 → +120 → +40 → +168
  //  활성화: 특정 오프셋에 0/9 설정
  //  비활성화: 원래값(1)으로 복구
  // ───────────────────────────────────────────────

  static bool g_selfHealApplied = false;

  static uintptr_t ResolveSelfHealPtr() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return 0;

    uintptr_t p = *(uintptr_t *)(exeBase + 0x02ED7A10);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x110);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x120);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x40);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x168);
    if (!IsValidPtr(p, 0x108B))
      return 0;

    return p;
  }

  void SetSelfHeal(bool enable) {
    uintptr_t p = ResolveSelfHealPtr();
    if (!p) {
      AddLog(u8"[자가치료] 포인터 해석 실패 - 전투 중이 아닐 수 있습니다.");
      return;
    }

    DWORD old, tmp;
    VirtualProtect((LPVOID)p, 0x108B, PAGE_READWRITE, &old);

    if (enable) {
      *(uint16_t *)(p + 0x102B) = 0x0100; // 최소 사거리 0
      // [Lv.2] 자가 치료 가능, 범위 확장 (범위 2~3)
      *(uint16_t *)(p + 0x104D) = 0x0100; // 최소 사거리 0  //
      *(uint8_t *)(p + 0x105A) = 5;       // (주변 1칸)
      *(uint8_t *)(p + 0x1068) = 5;       // 범위 3 (주변 2칸)
      // [Lv.3] 자가 치료 가능, 화면 전체 치료 (범위 9)
      *(uint16_t *)(p + 0x106F) = 0x0100; // 최소 사거리 0
      *(uint8_t *)(p + 0x107C) = 9;       // (주변 2칸)
      *(uint8_t *)(p + 0x108A) = 9;       // 범위 9 (전체)
      g_selfHealApplied = true;
    } else {
      *(uint16_t *)(p + 0x102B) = 0x0101;
      *(uint16_t *)(p + 0x104D) = 0x0101;
      *(uint16_t *)(p + 0x106F) = 0x0101;
      *(uint8_t *)(p + 0x105A) = 1;
      *(uint8_t *)(p + 0x1068) = 1;
      *(uint8_t *)(p + 0x107C) = 1;
      *(uint8_t *)(p + 0x108A) = 1;

      g_selfHealApplied = false;
    }

    VirtualProtect((LPVOID)p, 0x108B, old, &tmp);
  }
} // namespace DX11Base