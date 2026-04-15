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

  // 천계(Celestial) 데이터 구조체 (offset 0 = 0x64 기준 보정)
  static const celestialEntry k_celestialTable[] = {
      {0x06, 10, 100, false}, // 전의 // 테스트용도

      // 레벨 1
      {0x1C, 20, 0, false},   // 효과2 치료
      {0x1E, 6, 0, false},    // 대상2 아군전체
      {0x20, 2000, 1, true},  // 위력2 2000 (0x20, 0x21 2바이트 사용)
      {0x22, 99, 0, false},   // 특수2 99
      {0x26, 11, 0, false},   // 범위2 11 (시람+1칸)
      {0x27, 100, 0, false},  // 확률2 100
      // 레벨 2
      {0x3E, 20, 0, false},   // 효과2 치료
      {0x40, 6, 0, false},    // 대상2 아군전체
      {0x42, 3500, 1, true},  // 위력2 3500
      {0x44, 99, 0, false},   // 특수2 99
      {0x48, 11, 0, false},   // 범위2 11 (시람+1칸)
      {0x49, 100, 0, false},  // 확률2 100
      // 레벨 3
      {0x60, 20, 0, false},   // 효과2 치료
      {0x62, 6, 0, false},    // 대상2 아군전체
      {0x64, 7000, 1, true},  // 위력2 7000
      {0x66, 99, 0, false},   // 특수2 99
      {0x6A, 11, 0, false},   // 범위2 11 (시람+1칸)
      {0x6B, 100, 0, false},  // 확률2 100
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

    // --- [추가된 검증 로그] ---
    if (!IsBadReadPtr((void *)target, 4)) {
      unsigned int header = *(unsigned int *)target;
      // 정상이라면 로그에 00260026 이 찍혀야 합니다.
      // AddLog(u8"[디버그] Target: %llX, Data: %08X", target, header);
    }
    // --------------------------

    DWORD old, tmp;
    VirtualProtect((LPVOID)target, 0x40, PAGE_READWRITE, &old);

    for (const auto &e : k_celestialTable) {
      int val = enable ? e.enableVal : e.disableVal;
      if (e.isWord)
        *(uint16_t *)(target + e.offset) = (uint16_t)val;
      else
        *(uint8_t *)(target + e.offset) = (uint8_t)val;
    }

    VirtualProtect((LPVOID)target, 0x40, old, &tmp);
    AddLog(enable ? u8"[천계] 광역 치료 활성화!" : u8"[천계] 원본 복구");
  }

} // namespace DX11Base