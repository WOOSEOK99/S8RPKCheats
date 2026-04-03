
#include "pch.h"

#include "Cheats.h"
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
  //  전투 시 격류/낙석 지형 무시 패치
  //  포인터 체인: SAN8RPK.exe+02ED7A10 → +110 → +120 → +40 → +168
  //  활성화: 격류/낙석 데이터 초기화
  //  비활성화: 원래값으로 복구
  // ───────────────────────────────────────────────

  static bool g_terrainApplied = false;

  struct TerrainEntry {
    uintptr_t offset;
    int enableVal;
    int disableVal;
    bool isWord; // true=uint16_t, false=uint8_t
  };

  // 격류(water) / 낙석(stone) 오프셋 테이블
  static const TerrainEntry k_terrainTable[] = {
      // water1
      {0xCAE, 11, 9, false}, // 11로 하면 비오는 조건 필요함.
      {0xCB2, 60, 120, true},
      {0xCBC, 9, 0, false},
      {0xCBE, 1, 0, false},
      {0xCC0, 60, 0, true},
      {0xCC2, 3, 0, false},
      {0xCC6, 5, 0, false},
      {0xCC7, 100, 0, false},
      // water2
      {0xCD0, 11, 9, false},
      {0xCD4, 70, 140, true},
      {0xCDE, 9, 0, false},
      {0xCE0, 1, 0, false},
      {0xCE2, 70, 0, true},
      {0xCE4, 3, 0, false},
      {0xCE8, 5, 0, false},
      {0xCE9, 100, 0, false},
      // water3
      {0xCF2, 11, 9, false},
      {0xCF6, 85, 170, true},
      {0xD00, 9, 0, false},
      {0xD02, 1, 0, false},
      {0xD04, 85, 0, true},
      {0xD06, 3, 0, false},
      {0xD0A, 5, 0, false},
      {0xD0B, 100, 0, false},
      // stone1
      {0xD2E, 11, 27, false},
      {0xD32, 60, 120, true},
      {0xD3C, 27, 0, false},
      {0xD3E, 1, 0, false},
      {0xD40, 60, 0, true},
      {0xD42, 3, 0, false},
      {0xD46, 4, 0, false},
      {0xD47, 100, 0, false},
      // stone2
      {0xD50, 11, 27, false},
      {0xD54, 70, 140, true},
      {0xD5E, 27, 0, false},
      {0xD60, 1, 0, false},
      {0xD62, 70, 0, true},
      {0xD64, 3, 0, false},
      {0xD68, 4, 0, false},
      {0xD69, 100, 0, false},
      // stone3
      {0xD72, 11, 27, false},
      {0xD76, 85, 170, true},
      {0xD80, 27, 0, false},
      {0xD82, 1, 0, false},
      {0xD84, 85, 0, true},
      {0xD86, 3, 0, false},
      {0xD8A, 4, 0, false},
      {0xD8B, 100, 0, false},
  };

  static uintptr_t ResolveTerrainPtr() {
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
    if (!IsValidPtr(p, 0xD8C))
      return 0;

    return p;
  }

  void SetTerrainIgnore(bool enable) {
    uintptr_t p = ResolveTerrainPtr();
    if (!p) {
      AddLog(u8"[지형무시] 포인터 해석 실패 - 전투 중이 아닐 수 있습니다.");
      return;
    }

    DWORD old, tmp;
    VirtualProtect((LPVOID)p, 0xD8C, PAGE_READWRITE, &old);

    for (const auto &e : k_terrainTable) {
      int val = enable ? e.enableVal : e.disableVal;
      if (e.isWord)
        *(uint16_t *)(p + e.offset) = (uint16_t)val;
      else
        *(uint8_t *)(p + e.offset) = (uint8_t)val;
    }

    VirtualProtect((LPVOID)p, 0xD8C, old, &tmp);
    g_terrainApplied = enable;
  }
} // namespace DX11Base