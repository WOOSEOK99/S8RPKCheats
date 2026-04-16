#include "pch.h"

#include "../../Cheats.h"
#include "../../pch.h"
#include "../../showlog.h"
#include "../../MenuState.h"
#include "../Social/Fastrelationship.h"
#include "../Social/Infinitetalk.h"
#include "../Social/InstantLoveCave.h"
#include "../Social/Loyaltycave.h"
#include "../Social/Resonancecave.h"
#include "Defbuildingboost.h"
#include "Selfheal.h"
#include "Terrainignore.h"

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
      // 수비 건물
      {0x20ACE, 6000, 3000}, // 공격건물 : 도시 내구
      {0x20AEE, 4000, 2000}, // 공격건물 : 관문 내구
      {0x20B2E, 1400, 700},  // 공격건물 : 망루 내구
      {0x20B4E, 2000, 1000}, // 공격건물 : 투석기 내구

      {0x20AD2, 4, 3}, // 공격건물 : 도시 사거리
      {0x20AF2, 4, 3}, // 공격건물 : 관문 사거리
      {0x20B32, 4, 3}, // 공격건물 : 망루 사거리
      {0x20B52, 5, 4}, // 공격건물 : 투석기 사거리

      {0x20AD4, 16, 8}, // 공격건물 : 도시 공격력
      {0x20AF4, 16, 8}, // 공격건물 : 성문 공격력
      {0x20B54, 20, 9}, // 공격건물 : 투석기 공격력
      {0x20B34, 12, 6}, // 공격건물 : 망루 공격력

      // 시야 (망루/투석기 재활성화)
      {0x20ADC, 6, 5}, // 공격건물 : 도시 시야
      {0x20AFC, 4, 3}, // 공격건물 : 관문 시야
      {0x20B3C, 4, 3}, // 공격건물 : 망루 시야
      {0x20B5C, 5, 4}, // 공격건물 : 투석기 시야

      // 사기증가 건물
      {0x20B6E, 1400, 700}, // 사기증가 : 봉화대 내구
      {0x20B72, 10, 5},     // 사기증가 : 봉화대 전의
      {0x20B7C, 4, 3},      // 사기증가 : 봉화대 시야

  };

  uintptr_t ResolveDefBuildingPtr() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return 0;

    // 1단계: exeBase + 0x034C8630
    uintptr_t p1_addr = exeBase + 0x034C8630;
    if (!IsValidPtr(p1_addr, 8))
      return 0;
    uintptr_t p1 = *(uintptr_t *)p1_addr;
    if (!IsValidPtr(p1, 8))
      return 0;

    // 2단계: p1 + 0xBB8
    uintptr_t p2_addr = p1 + 0xBB8;
    if (!IsValidPtr(p2_addr, 8))
      return 0;
    uintptr_t p2 = *(uintptr_t *)p2_addr;
    if (!IsValidPtr(p2, 8))
      return 0;

    // 3단계: p2 + 0x0
    uintptr_t p3_addr = p2 + 0x0;
    if (!IsValidPtr(p3_addr, 8))
      return 0;
    uintptr_t p3 = *(uintptr_t *)p3_addr;
    if (!IsValidPtr(p3, 8))
      return 0;

    // 4단계: p3 + 0x170
    uintptr_t p4_addr = p3 + 0x170;
    if (!IsValidPtr(p4_addr, 8))
      return 0;
    uintptr_t p4 = *(uintptr_t *)p4_addr;
    if (!IsValidPtr(p4, 8))
      return 0;

    // 5단계: p4 + 0x10
    uintptr_t p5_addr = p4 + 0x10;
    if (!IsValidPtr(p5_addr, 8))
      return 0;
    uintptr_t p5 = *(uintptr_t *)p5_addr;

    // 최종 데이터 영역(0x20BA0바이트 이상) 유효성 검사
    if (!IsValidPtr(p5, 0x20BA0))
      return 0;

    return p5;
  }

  void SetDefBuildingBoost(bool enable) {
    uintptr_t p = ResolveDefBuildingPtr();
    if (!p) {
      AddLog(u8"[방어건물강화] 포인터 해석 실패 - 전투 중이 아닐 수 있습니다.");
      return;
    }

    DWORD old, tmp;
    if (VirtualProtect((LPVOID)p, 0x20BA0, PAGE_READWRITE, &old)) {
      if (enable) {
        // 도시
        *(uint16_t *)(p + 0x20ACE) = (uint16_t)v_City_Dur;
        *(uint16_t *)(p + 0x20AD2) = (uint16_t)v_City_Range;
        *(uint16_t *)(p + 0x20AD4) = (uint16_t)v_City_Atk;
        *(uint16_t *)(p + 0x20ADC) = (uint16_t)v_City_Sight;

        // 관문
        *(uint16_t *)(p + 0x20AEE) = (uint16_t)v_Gate_Dur;
        *(uint16_t *)(p + 0x20AF2) = (uint16_t)v_Gate_Range;
        *(uint16_t *)(p + 0x20AF4) = (uint16_t)v_Gate_Atk;
        *(uint16_t *)(p + 0x20AFC) = (uint16_t)v_Gate_Sight;

        // 망루
        *(uint16_t *)(p + 0x20B2E) = (uint16_t)v_Tower_Dur;
        *(uint16_t *)(p + 0x20B32) = (uint16_t)v_Tower_Range;
        *(uint16_t *)(p + 0x20B34) = (uint16_t)v_Tower_Atk;
        *(uint16_t *)(p + 0x20B3C) = (uint16_t)v_Tower_Sight;

        // 투석기 (건물용)
        *(uint16_t *)(p + 0x20B4E) = (uint16_t)v_WallCatapult_Dur;
        *(uint16_t *)(p + 0x20B52) = (uint16_t)v_WallCatapult_Range;
        *(uint16_t *)(p + 0x20B54) = (uint16_t)v_WallCatapult_Atk;
        *(uint16_t *)(p + 0x20B5C) = (uint16_t)v_WallCatapult_Sight;

        // 봉화대
        *(uint16_t *)(p + 0x20B6E) = (uint16_t)v_Signal_Dur;
        *(uint16_t *)(p + 0x20B72) = (uint16_t)v_Signal_Spirit;
        *(uint16_t *)(p + 0x20B7C) = (uint16_t)v_Signal_Sight;
      } else {
        // 복구 값 ( disableVal )
        *(uint16_t *)(p + 0x20ACE) = 3000;
        *(uint16_t *)(p + 0x20AD2) = 3;
        *(uint16_t *)(p + 0x20AD4) = 8;
        *(uint16_t *)(p + 0x20ADC) = 5;

        *(uint16_t *)(p + 0x20AEE) = 2000;
        *(uint16_t *)(p + 0x20AF2) = 3;
        *(uint16_t *)(p + 0x20AF4) = 8;
        *(uint16_t *)(p + 0x20AFC) = 3;

        *(uint16_t *)(p + 0x20B2E) = 700;
        *(uint16_t *)(p + 0x20B32) = 3;
        *(uint16_t *)(p + 0x20B34) = 6;
        *(uint16_t *)(p + 0x20B3C) = 3;

        *(uint16_t *)(p + 0x20B4E) = 1000;
        *(uint16_t *)(p + 0x20B52) = 4;
        *(uint16_t *)(p + 0x20B54) = 9;
        *(uint16_t *)(p + 0x20B5C) = 4;

        *(uint16_t *)(p + 0x20B6E) = 700;
        *(uint16_t *)(p + 0x20B72) = 5;
        *(uint16_t *)(p + 0x20B7C) = 3;
      }

      VirtualProtect((LPVOID)p, 0x20BA0, old, &tmp);
      g_defBuildingApplied = enable;
      AddLog(u8"[방어건물강화] %s", enable ? u8"활성화 (커스텀)" : u8"비활성화 (복구)");
    } else {
      AddLog(u8"[방어건물강화] 메모리 보호 해제 실패 (error: %d)", GetLastError());
    }
  }
} // namespace DX11Base