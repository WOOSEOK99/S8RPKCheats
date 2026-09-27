#pragma once

#include "Cheats.h"
#include "Cheats/Civilian/CityInfoWindow.h"
#include "MenuState.h"
#include <windows.h>

namespace DX11Base {

inline void RunT04RenderMaintenance() {
  static ULONGLONG s_lastMaintenanceMs = 0;
  const ULONGLONG now = GetTickCount64();
  if (s_lastMaintenanceMs != 0 && now - s_lastMaintenanceMs < 200)
    return;

  s_lastMaintenanceMs = now;

  // 악명 0 유지도 렌더 FPS가 아니라 실시간 200ms cadence로 처리합니다.
  // 기존 동작처럼 값이 이미 0이면 쓰지 않습니다.
  if (bZeroInfamy) {
    const uintptr_t gameBase = GetGameBase();
    if (gameBase > 0x10000 && IsValidPtr(gameBase + 0xE0, sizeof(uintptr_t))) {
      const uintptr_t p1 = *(uintptr_t *)(gameBase + 0xE0);
      if (p1 > 0x10000 && IsValidPtr(p1 + 0x108, sizeof(uint16_t))) {
        uint16_t *pInfamy = (uint16_t *)(p1 + 0x108);
        if (*pInfamy != 0)
          *pInfamy = 0;
      }
    }
  }

  RunCityRevoltAlwaysZero();
}

} // namespace DX11Base
