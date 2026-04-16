#include "pch.h"

#include "../../pch.h"
#include "Celestia.h"
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

  struct celestialEntry {
    uintptr_t offset;
    int enableVal;
    int disableVal;
    bool isWord; // true=uint16_t, false=uint8_t
  };

  // 전역 변수 또는 정적 변수
  uintptr_t g_LastCelestialAddr = 0;

  // 천계(Celestial) 데이터 구조체 (기본 효과/대상 자동 수정용)
  static const celestialEntry k_celestialBaseTable[] = {
      {0x06, 10, 100, false}, // 전의

      // 레벨 1 기본값 (효과/대상)
      {0x1C, 20, 0, false},   // 효과2 치료
      {0x1E, 6, 0, false},    // 대상2 아군전체
      {0x22, 99, 0, false},   // 특수2 99

      // 레벨 2 기본값
      {0x3E, 20, 0, false},
      {0x40, 6, 0, false},
      {0x44, 99, 0, false},

      // 레벨 3 기본값
      {0x60, 20, 0, false},
      {0x62, 6, 0, false},
      {0x66, 99, 0, false},
  };

  static uintptr_t ResolveCelestialPtr() {
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

    // 1A0AA37D4E0 지점을 정확히 가리키도록 10A0 리턴
    return p + 0x10A0;
  }

  void SetCelestialMod(bool enable) {
    uintptr_t target = ResolveCelestialPtr();
    if (!target)
      return;

    DWORD old, tmp;
    VirtualProtect((LPVOID)target, 0x100, PAGE_READWRITE, &old);

    if (enable) {
      // 1. 공통/기본 효과 수동 패치
      for (const auto &e : k_celestialBaseTable) {
        if (e.isWord) *(uint16_t *)(target + e.offset) = (uint16_t)e.enableVal;
        else *(uint8_t *)(target + e.offset) = (uint8_t)e.enableVal;
      }

      // 2. 가변 수치 패치
      // [Lv.1]
      *(uint16_t *)(target + 0x20) = (uint16_t)v_CelestiaLv1_Amount;
      *(uint8_t *)(target + 0x26) = (uint8_t)v_CelestiaLv1_Range;
      *(uint8_t *)(target + 0x27) = (uint8_t)v_CelestiaLv1_Prob;

      // [Lv.2]
      *(uint16_t *)(target + 0x42) = (uint16_t)v_CelestiaLv2_Amount;
      *(uint8_t *)(target + 0x48) = (uint8_t)v_CelestiaLv2_Range;
      *(uint8_t *)(target + 0x49) = (uint8_t)v_CelestiaLv2_Prob;

      // [Lv.3]
      *(uint16_t *)(target + 0x64) = (uint16_t)v_CelestiaLv3_Amount;
      *(uint8_t *)(target + 0x6A) = (uint8_t)v_CelestiaLv3_Range;
      *(uint8_t *)(target + 0x6B) = (uint8_t)v_CelestiaLv3_Prob;

      AddLog(u8"[천계] 광역 치료 설정 적용 완료!");
    } else {
      // 기본값 복구 (k_celestialBaseTable 활용 및 수동 복구)
      for (const auto &e : k_celestialBaseTable) {
        if (e.isWord) *(uint16_t *)(target + e.offset) = (uint16_t)e.disableVal;
        else *(uint8_t *)(target + e.offset) = (uint8_t)e.disableVal;
      }
      *(uint16_t *)(target + 0x20) = 1;
      *(uint8_t *)(target + 0x26) = 1;
      *(uint8_t *)(target + 0x27) = 10;

      *(uint16_t *)(target + 0x42) = 1;
      *(uint8_t *)(target + 0x48) = 1;
      *(uint8_t *)(target + 0x49) = 10;

      *(uint16_t *)(target + 0x64) = 1;
      *(uint8_t *)(target + 0x6A) = 1;
      *(uint8_t *)(target + 0x6B) = 10;

      AddLog(u8"[천계] 원본 데이터 복구 완료");
    }

    VirtualProtect((LPVOID)target, 0x100, old, &tmp);
  }

} // namespace DX11Base