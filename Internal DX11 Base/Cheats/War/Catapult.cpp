#include "../../pch.h"

#include "Catapult.h"
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
  // 투석기 데이터 구조체 (지형 무시 스타일)
  struct CatapultEntry {
    uintptr_t offset;
    int enableVal;
    int disableVal;
    bool isWord; // true=uint16_t, false=uint8_t
  };

  // [데이터 테이블] 유저님이 분석하신 14B2 시작점 기준 오프셋 (기본값 복구용)
  static const CatapultEntry k_catapultBaseTable[] = {
      {0x0B, 5, 2, false},    // 최소 사거리
      {0x0C, 5, 2, false},    // 최대 사거리
      {0x12, 100, 20, true},  // 부대 위력 (2 Bytes)
      {0x18, 11, 9, false}    // 범위 (09:투석 -> 0B:광역)
  };

  static uintptr_t ResolveCatapultPtr() {
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

    // 안전 범위 검사
    if (!IsValidPtr(p, 0x1500))
      return 0;

    // 1A0AA37D4E0 지점을 정확히 가리키도록 14A0 리턴
    return p + 0x14A0;
  }


  void SetCatapultCheat(bool enable) {
    uintptr_t target = ResolveCatapultPtr();
    if (!target) {
      AddLog(u8"[투석기] 포인터 해석 실패 - 전투 중이 아닙니까?");
      return;
    }

    DWORD old, tmp;
    VirtualProtect((LPVOID)target, 0x40, PAGE_READWRITE, &old);

    if (enable) {
      *(uint8_t *)(target + 0x0B) = (uint8_t)v_Catapult_MinRange;
      *(uint8_t *)(target + 0x0C) = (uint8_t)v_Catapult_MaxRange;
      *(uint16_t *)(target + 0x12) = (uint16_t)v_Catapult_Amount;
      *(uint8_t *)(target + 0x18) = (uint8_t)v_Catapult_Range;
      AddLog(u8"[투석기] 개조 설정 적용 완료!");
    } else {
      // 기본값 복구
      for (const auto &e : k_catapultBaseTable) {
        if (e.isWord) *(uint16_t *)(target + e.offset) = (uint16_t)e.disableVal;
        else *(uint8_t *)(target + e.offset) = (uint8_t)e.disableVal;
      }
      AddLog(u8"[투석기] 원본 복구 완료");
    }

    VirtualProtect((LPVOID)target, 0x40, old, &tmp);
  }
} // namespace DX11Base
