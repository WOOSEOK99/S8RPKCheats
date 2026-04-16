#include "../../pch.h"
#include "Dongto.h"
#include "../../Cheats.h"
#include "../Social/Fastrelationship.h"
#include "../Social/Infinitetalk.h"
#include "../Social/InstantLoveCave.h"
#include "../Social/Loyaltycave.h"
#include "../Social/Resonancecave.h"
#include "Selfheal.h"
#include "../../showlog.h"
#include "../../MenuState.h"

#include <psapi.h>
#include <string>
#include <vector>

namespace DX11Base {
  // ───────────────────────────────────────────────
  //  전투 시 동토(Frozen Land) 패치
  //  포인터 체인: SAN8RPK.exe+02ED7A10 → +110 → +120 → +40 → +168
  //  (Selfheal과 동일한 구조, 주소만 -0x200 차이)
  // ───────────────────────────────────────────────

  static bool g_dongtoApplied = false;

  static uintptr_t ResolveDongtoPtr() {
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
    if (!IsValidPtr(p, 0x108B)) // Selfheal과 동일한 범위 체크
      return 0;

    return p;
  }

  void SetDongto(bool enable) {
    uintptr_t p = ResolveDongtoPtr();
    if (!p) {
      AddLog(u8"[동토] 포인터 해석 실패 - 전투 중이 아닐 수 있습니다.");
      return;
    }

    // Dongto 주소는 Selfheal - 0x200
    uintptr_t dp = p - 0x200;

    DWORD old, tmp;
    VirtualProtect((LPVOID)dp, 0x108B, PAGE_READWRITE, &old);

    if (enable) {
      // [Lv.1]
      *(uint16_t *)(dp + 0x1029) = (uint16_t)v_DongtoLv1_Prob;
      *(uint16_t *)(dp + 0x1047) = (uint16_t)v_DongtoLv1_StateProb;

      // [Lv.2]
      *(uint16_t *)(dp + 0x104B) = (uint16_t)v_DongtoLv2_Prob;
      *(uint8_t *)(dp + 0x1069) = (uint8_t)v_DongtoLv2_StateProb;

      // [Lv.3]
      *(uint16_t *)(dp + 0x106D) = (uint16_t)v_DongtoLv3_Prob;
      *(uint8_t *)(dp + 0x108B) = (uint8_t)v_DongtoLv3_StateProb;

      g_dongtoApplied = true;
    } else {
      // 기본값 복구 (추정치)
      *(uint16_t *)(dp + 0x1029) = 10;
      *(uint16_t *)(dp + 0x104B) = 10;
      *(uint16_t *)(dp + 0x106D) = 10;
      *(uint8_t *)(dp + 0x1069) = 10;
      *(uint8_t *)(dp + 0x108B) = 10;

      g_dongtoApplied = false;
    }

    VirtualProtect((LPVOID)dp, 0x108B, old, &tmp);
  }
} // namespace DX11Base
