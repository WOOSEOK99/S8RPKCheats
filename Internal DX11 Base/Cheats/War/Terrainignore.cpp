#include "Terrainignore.h"
#include "../../Cheats.h"
#include "../../pch.h"
#include "../../showlog.h"
#include "../../MenuState.h"
#include "../Social/Fastrelationship.h"
#include "../Social/Infinitetalk.h"
#include "../Social/InstantLoveCave.h"
#include "../Social/Loyaltycave.h"
#include "../Social/Resonancecave.h"
#include "Selfheal.h"


#include <cstdint>
#include <cstdio>
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
      {0xCAE, 11, 9, false},  // 효과1(조건), 디폴트 9 , 11로 하면 비오는 조건 필요함. (고정값)
      {0xCB2, 60, 120, true}, // 위력1   의미없는듯.(위력2와 같은값)
      {0xCBC, 9, 0, false},   // 효과2,  9 격류 (고정값)
      {0xCBE, 1, 0, false},   // 대상2,  1 적부대 (고정값)
      {0xCC0, 60, 0, true},   // 위력2,  실제 데미지값은 여기서
      {0xCC2, 2, 0, false},   // 특수2,  DESC_TABLE 효과랑 같은거 쓰는듯. (고정값)
      {0xCC6, 5, 0, false},   // 범위2,
      {0xCC7, 100, 0, false}, // 확률2,  (고정값)
      // water2
      {0xCD0, 11, 9, false},
      {0xCD4, 70, 140, true},
      {0xCDE, 9, 0, false},
      {0xCE0, 1, 0, false},
      {0xCE2, 70, 0, true},
      {0xCE4, 2, 0, false},
      {0xCE8, 5, 0, false},
      {0xCE9, 100, 0, false},
      // water3
      {0xCF2, 11, 9, false},
      {0xCF6, 85, 170, true},
      {0xD00, 9, 0, false},
      {0xD02, 1, 0, false},
      {0xD04, 85, 0, true},
      {0xD06, 2, 0, false},
      {0xD0A, 5, 0, false},
      {0xD0B, 100, 0, false},
      // stone1
      {0xD2E, 28, 27, false}, // 효과1(조건), 디폴트 28 , 27로 하면 강풍 조건 필요함. (고정값)
      {0xD32, 60, 120, true}, // 위력1   의미없는듯. (위력2와 같은값)
      {0xD3C, 27, 0, false},  // 효과2,  27 낙석 (고정값)
      {0xD3E, 1, 0, false},   // 대상2,  1 적부대 (고정값)
      {0xD40, 60, 0, true},   // 위력2,  실제 데미지값은 여기서
      {0xD42, 2, 0, false},   // 특수2,  DESC_TABLE 효과랑 같은거 쓰는듯.
      {0xD46, 4, 0, false},   // 범위2,
      {0xD47, 100, 0, false}, // 확률2,  (고정값)
      // stone2
      {0xD50, 11, 27, false},
      {0xD54, 70, 140, true},
      {0xD5E, 27, 0, false},
      {0xD60, 1, 0, false},
      {0xD62, 70, 0, true},
      {0xD64, 2, 0, false},
      {0xD68, 4, 0, false},
      {0xD69, 100, 0, false},
      // stone3
      {0xD72, 11, 27, false},
      {0xD76, 85, 170, true},
      {0xD80, 27, 0, false},
      {0xD82, 1, 0, false},
      {0xD84, 85, 0, true},
      {0xD86, 2, 0, false},
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

    // Water
    // Level 1
    *(uint8_t *)(p + 0xCAE) = (uint8_t)(enable ? 11 : 9); // Condition
    *(uint16_t *)(p + 0xCB2) = (uint16_t)(enable ? v_WaterLv1_Amount : 120); // Amount1
    *(uint16_t *)(p + 0xCC0) = (uint16_t)(enable ? v_WaterLv1_Amount : 0); // Amount2
    *(uint8_t *)(p + 0xCC6) = (uint8_t)(enable ? v_WaterLv1_Range : 0); // Range2
    if (enable) {
        *(uint8_t *)(p + 0xCBC) = 9; // Effect2
        *(uint8_t *)(p + 0xCBE) = 1; // Target2
        *(uint8_t *)(p + 0xCC2) = 3; // Special2
        *(uint8_t *)(p + 0xCC7) = 100; // Prob2
    }

    // Level 2
    *(uint8_t *)(p + 0xCD0) = (uint8_t)(enable ? 11 : 9);
    *(uint16_t *)(p + 0xCD4) = (uint16_t)(enable ? v_WaterLv2_Amount : 140);
    *(uint16_t *)(p + 0xCE2) = (uint16_t)(enable ? v_WaterLv2_Amount : 0);
    *(uint8_t *)(p + 0xCE8) = (uint8_t)(enable ? v_WaterLv2_Range : 0);
    if (enable) {
        *(uint8_t *)(p + 0xCDE) = 9;
        *(uint8_t *)(p + 0xCE0) = 1;
        *(uint8_t *)(p + 0xCE4) = 3;
        *(uint8_t *)(p + 0xCE9) = 100;
    }

    // Level 3
    *(uint8_t *)(p + 0xCF2) = (uint8_t)(enable ? 11 : 9);
    *(uint16_t *)(p + 0xCF6) = (uint16_t)(enable ? v_WaterLv3_Amount : 170);
    *(uint16_t *)(p + 0xD04) = (uint16_t)(enable ? v_WaterLv3_Amount : 0);
    *(uint8_t *)(p + 0xD0A) = (uint8_t)(enable ? v_WaterLv3_Range : 0);
    if (enable) {
        *(uint8_t *)(p + 0xD00) = 9;
        *(uint8_t *)(p + 0xD02) = 1;
        *(uint8_t *)(p + 0xD06) = 3;
        *(uint8_t *)(p + 0xD0B) = 100;
    }

    // Stone
    // Level 1
    *(uint8_t *)(p + 0xD2E) = (uint8_t)(enable ? 11 : 27); // Condition
    *(uint16_t *)(p + 0xD32) = (uint16_t)(enable ? v_StoneLv1_Amount : 120);
    *(uint16_t *)(p + 0xD40) = (uint16_t)(enable ? v_StoneLv1_Amount : 0);
    *(uint8_t *)(p + 0xD46) = (uint8_t)(enable ? v_StoneLv1_Range : 0);
    if (enable) {
        *(uint8_t *)(p + 0xD3C) = 27; // Effect2
        *(uint8_t *)(p + 0xD3E) = 1; // Target2
        *(uint8_t *)(p + 0xD42) = 3; // Special2
        *(uint8_t *)(p + 0xD47) = 100; // Prob2
    }

    // Level 2
    *(uint8_t *)(p + 0xD50) = (uint8_t)(enable ? 11 : 27);
    *(uint16_t *)(p + 0xD54) = (uint16_t)(enable ? v_StoneLv2_Amount : 140);
    *(uint16_t *)(p + 0xD62) = (uint16_t)(enable ? v_StoneLv2_Amount : 0);
    *(uint8_t *)(p + 0xD68) = (uint8_t)(enable ? v_StoneLv2_Range : 0);
    if (enable) {
        *(uint8_t *)(p + 0xD5E) = 27;
        *(uint8_t *)(p + 0xD60) = 1;
        *(uint8_t *)(p + 0xD64) = 3;
        *(uint8_t *)(p + 0xD69) = 100;
    }

    // Level 3
    *(uint8_t *)(p + 0xD72) = (uint8_t)(enable ? 11 : 27);
    *(uint16_t *)(p + 0xD76) = (uint16_t)(enable ? v_StoneLv3_Amount : 170);
    *(uint16_t *)(p + 0xD84) = (uint16_t)(enable ? v_StoneLv3_Amount : 0);
    *(uint8_t *)(p + 0xD8A) = (uint8_t)(enable ? v_StoneLv3_Range : 0);
    if (enable) {
        *(uint8_t *)(p + 0xD80) = 27;
        *(uint8_t *)(p + 0xD82) = 1;
        *(uint8_t *)(p + 0xD86) = 3;
        *(uint8_t *)(p + 0xD8B) = 100;
    }

    VirtualProtect((LPVOID)p, 0xD8C, old, &tmp);
    g_terrainApplied = enable;
  }
} // namespace DX11Base