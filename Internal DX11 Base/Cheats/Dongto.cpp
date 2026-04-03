#include "pch.h"

#include "Cheats.h"
#include "Dongto.h"
#include "showlog.h"

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
      // Selfheal 로직을 그대로 사용하되 dp (base - 0x200) 기준으로 적용
      *(uint16_t *)(dp + 0x1029) = 100; // level1 통토 확률 100%로 조정
      *(uint16_t *)(dp + 0x104B) = 100; // levle2 통토 확률 100%로 조정
      *(uint16_t *)(dp + 0x106D) = 100; // levle3 통토 확률 100%로 조정
      //
      *(uint8_t *)(dp + 0x1069) = 50;  // level2 상태 이상 확률 50%
      *(uint8_t *)(dp + 0x108B) = 100; // level3 상태 이상 확률 100%
      g_dongtoApplied = true;
    } else {
      *(uint16_t *)(dp + 0x1029) = 10;
      *(uint16_t *)(dp + 0x104B) = 10;
      *(uint16_t *)(dp + 0x106D) = 10;
      //
      *(uint8_t *)(dp + 0x1069) = 10;
      *(uint8_t *)(dp + 0x108B) = 10;

      g_dongtoApplied = false;
    }

    VirtualProtect((LPVOID)dp, 0x108B, old, &tmp);
  }
} // namespace DX11Base
