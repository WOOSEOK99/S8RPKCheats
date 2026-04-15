#include "pch.h"

#include "../../pch.h"
#include "Defbuildingboost.h"
#include "../../Cheats.h"
#include "../Social/Fastrelationship.h"
#include "../Social/Infinitetalk.h"
#include "../Social/InstantLoveCave.h"
#include "../Social/Loyaltycave.h"
#include "../Social/Resonancecave.h"
#include "Selfheal.h"
#include "Terrainignore.h"
#include "../../showlog.h"

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
      // 사거리 (망루/투석기 재활성화)
      {0x20AD2, 4, 3}, // 도시
      {0x20AF2, 4, 3}, // 관문
      {0x20B32, 4, 3}, // 망루
      {0x20B52, 5, 4}, // 투석기
      // {0x20B72, 6, 5}, // 봉화대 (잠정 제외)
      
      // 시야 (망루/투석기 재활성화)
      {0x20ADC, 6, 5}, // 도시
      {0x20AFC, 4, 3}, // 관문
      {0x20B3C, 4, 3}, // 망루
      {0x20B5C, 5, 4}, // 투석기
      // {0x20B7C, 4, 3}, // 봉화대 (잠정 제외)
  };

  uintptr_t ResolveDefBuildingPtr() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return 0;

    // 1단계: exeBase + 0x034C8630
    uintptr_t p1_addr = exeBase + 0x034C8630;
    if (!IsValidPtr(p1_addr, 8)) return 0;
    uintptr_t p1 = *(uintptr_t *)p1_addr;
    if (!IsValidPtr(p1, 8)) return 0;

    // 2단계: p1 + 0xBB8
    uintptr_t p2_addr = p1 + 0xBB8;
    if (!IsValidPtr(p2_addr, 8)) return 0;
    uintptr_t p2 = *(uintptr_t *)p2_addr;
    if (!IsValidPtr(p2, 8)) return 0;

    // 3단계: p2 + 0x0
    uintptr_t p3_addr = p2 + 0x0;
    if (!IsValidPtr(p3_addr, 8)) return 0;
    uintptr_t p3 = *(uintptr_t *)p3_addr;
    if (!IsValidPtr(p3, 8)) return 0;

    // 4단계: p3 + 0x170
    uintptr_t p4_addr = p3 + 0x170;
    if (!IsValidPtr(p4_addr, 8)) return 0;
    uintptr_t p4 = *(uintptr_t *)p4_addr;
    if (!IsValidPtr(p4, 8)) return 0;

    // 5단계: p4 + 0x10
    uintptr_t p5_addr = p4 + 0x10;
    if (!IsValidPtr(p5_addr, 8)) return 0;
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
    // Defatkboost.cpp에서 검증된 방식(넓은 범위 한꺼번에 권한 변경)으로 롤백하여 프리징 해결 시도
    if (VirtualProtect((LPVOID)p, 0x20BA0, PAGE_READWRITE, &old)) {
      for (const auto &e : k_defBuildingTable) {
        // 혹시 모를 범위 초과 방지 (Pyre Tower 등 확인되지 않은 오프셋 보호)
        if (e.offset + sizeof(uint16_t) > 0x20BA0) continue;

        *(uint16_t *)(p + e.offset) = enable ? e.enableVal : e.disableVal;
      }
      VirtualProtect((LPVOID)p, 0x20BA0, old, &tmp);

      g_defBuildingApplied = enable;
      AddLog(u8"[방어건물강화] %s", enable ? u8"활성화" : u8"비활성화");
    } else {
      AddLog(u8"[방어건물강화] 메모리 보호 해제 실패 (error: %d)", GetLastError());
    }
  }
}