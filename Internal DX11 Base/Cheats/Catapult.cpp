#include "pch.h"

#include "Catapult.h"
#include "Cheats.h"
#include "Fastrelationship.h"
#include "Infinitetalk.h"
#include "InstantLoveCave.h"
#include "Loyaltycave.h"
#include "Resonancecave.h"
#include "Selfheal.h"
#include "showlog.h"

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

  // [데이터 테이블] 유저님이 분석하신 14B2 시작점 기준 오프셋
  static const CatapultEntry k_catapultTable[] = {
      {0x0B, 5, 2, false},    // 최소 사거리
      {0x0C, 5, 2, false},    // 최대 사거리
      {0x12, 100, 20, true},  // 부대 위력 (2 Bytes)
      //{0x16, 250, 250, true}, // 공성 위력 (2 Bytes)
      {0x18, 11, 9, false}    // 범위 (09:투석 -> 0B:광역)
  };

  // [최종 교정본] 14A0 오프셋 적용
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

    //// --- [추가된 검증 로그] ---
    //if (!IsBadReadPtr((void *)target, 4)) {
    //  unsigned int header = *(unsigned int *)target;
    //  // 정상이라면 로그에 00260026 이 찍혀야 합니다.
    //  AddLog(u8"[디버그] Target: %llX, Data: %08X", target, header);

    //  if (header != 0x00260026) {
    //    AddLog(u8"[경고] 시작 지점이 26 00 26 00이 아닙니다! 오프셋 확인 필요.");
    //  }
    //}
    //// --------------------------

    DWORD old, tmp;
    VirtualProtect((LPVOID)target, 0x20, PAGE_READWRITE, &old);

    for (const auto &e : k_catapultTable) {
      int val = enable ? e.enableVal : e.disableVal;
      if (e.isWord)
        *(uint16_t *)(target + e.offset) = (uint16_t)val;
      else
        *(uint8_t *)(target + e.offset) = (uint8_t)val;
    }

    VirtualProtect((LPVOID)target, 0x20, old, &tmp);
    AddLog(enable ? u8"[투석기] 개조 활성화!" : u8"[투석기] 원본 복구 완료");
  }
} // namespace DX11Base
