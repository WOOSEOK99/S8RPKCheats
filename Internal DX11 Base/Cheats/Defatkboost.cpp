
#include "pch.h"

#include "Defatkboost.h"
#include "Cheats.h"
#include "Defbuildingboost.h"
#include "Fastrelationship.h"
#include "Infinitetalk.h"
#include "InstantLoveCave.h"
#include "Loyaltycave.h"
#include "Resonancecave.h"
#include "Selfheal.h"
#include "Terrainignore.h"
#include "showlog.h"

#include <psapi.h>
#include <string>
#include <vector>

namespace DX11Base {
  // ───────────────────────────────────────────────
  //  방어 건물 공격력 강화 패치
  //  포인터 체인: SAN8RPK.exe+034C8630 → +BB8 → +0 → +170 → +10
  //  (ResolveDefBuildingPtr() 재사용)
  // ───────────────────────────────────────────────

  static bool g_defAtkApplied = false;

  struct DefAtkEntry {
    uintptr_t offset;
    uint16_t enableVal;
    uint16_t disableVal;
  };

  static const DefAtkEntry k_defAtkTable[] = {
      {0x20AD4, 16, 8}, // 도시
      {0x20AF4, 16, 8}, // 성문
      {0x20B54, 20, 9}, // 투석기
      {0x20B34, 12, 6}, // 망루
  };

  void SetDefAtkBoost(bool enable) {
    uintptr_t p = ResolveDefBuildingPtr();
    if (!p) {
      AddLog(u8"[방어건물공격력] 포인터 해석 실패 - 전투 중이 아닐 수 있습니다.");
      return;
    }

    DWORD old, tmp;
    VirtualProtect((LPVOID)p, 0x20BA0, PAGE_READWRITE, &old);

    for (const auto &e : k_defAtkTable) {
      *(uint16_t *)(p + e.offset) = enable ? e.enableVal : e.disableVal;
    }

    VirtualProtect((LPVOID)p, 0x20BA0, old, &tmp);
    g_defAtkApplied = enable;
    AddLog(u8"[방어건물공격력] %s", enable ? u8"활성화" : u8"비활성화");
  }
}