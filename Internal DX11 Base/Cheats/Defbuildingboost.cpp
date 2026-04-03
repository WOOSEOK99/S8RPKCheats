
#include "pch.h"

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
  //  방어 건물 사거리/시야 강화 패치
  //  포인터 체인: SAN8RPK.exe+034C8630 → +BB8 → +0 → +170 → +10
  //  활성화: 사거리/시야 증가
  //  비활성화: 원래값으로 복구
  // ───────────────────────────────────────────────

  static bool g_defBuildingApplied = false;

  struct DefBuildingEntry {
    uintptr_t offset;
    uint16_t enableVal;
    uint16_t disableVal;
  };

  static const DefBuildingEntry k_defBuildingTable[] = {
      // 사거리
      {0x20AD2, 4, 3}, // 도시
      {0x20AF2, 4, 3}, // 관문
      {0x20B32, 4, 3}, // 망루
      {0x20B52, 5, 4}, // 투석기
      {0x20B72, 6, 5}, // 봉화대 전의 증가량
      // 시야
      {0x20ADC, 6, 5}, // 도시
      {0x20AFC, 4, 3}, // 관문
      {0x20B3C, 4, 3}, // 망루
      {0x20B5C, 5, 4}, // 투석기
      {0x20B7C, 4, 3}, // 봉화대

      // {0x20AD2, 4, 3}, // 도시
      // {0x20AF2, 4, 3}, // 관문
      // {0x20B32, 10, 3}, // 망루
      // {0x20B52, 10, 4}, // 투석기
      // {0x20B72, 6, 5}, // 봉화대 전의 증가량
      // // 시야
      // {0x20ADC, 6, 5}, // 도시
      // {0x20AFC, 4, 3}, // 관문
      // {0x20B3C, 10, 3}, // 망루
      // {0x20B5C, 10, 4}, // 투석기
      // {0x20B7C, 4, 3}, // 봉화대
  };

  uintptr_t ResolveDefBuildingPtr() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return 0;

    uintptr_t p = *(uintptr_t *)(exeBase + 0x034C8630);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0xBB8);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x0);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x170);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x10);
    if (!IsValidPtr(p, 0x20BA0))
      return 0;

    return p;
  }

  void SetDefBuildingBoost(bool enable) {
    uintptr_t p = ResolveDefBuildingPtr();
    if (!p) {
      AddLog(u8"[방어건물강화] 포인터 해석 실패 - 전투 중이 아닐 수 있습니다.");
      return;
    }

    DWORD old, tmp;
    VirtualProtect((LPVOID)p, 0x20BA0, PAGE_READWRITE, &old);

    for (const auto &e : k_defBuildingTable) {
      *(uint16_t *)(p + e.offset) = enable ? e.enableVal : e.disableVal;
    }

    VirtualProtect((LPVOID)p, 0x20BA0, old, &tmp);
    g_defBuildingApplied = enable;
    AddLog(u8"[방어건물강화] %s", enable ? u8"활성화" : u8"비활성화");
  }
}