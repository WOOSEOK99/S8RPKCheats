#pragma once

#include "Cheats.h"
#include "Cheats/Civilian/CityInfoWindow.h"
#include "MenuState.h"
#include <windows.h>

namespace DX11Base {

// SelectOfficercapture.h는 UI 매크로 래퍼도 정의하므로 여기서는 함수만 전방 선언합니다.
void TickTraitChangeAsync();
void TickBatchRandomTraitJobT05();

inline void RunT04RenderMaintenance() {
  static ULONGLONG s_lastMaintenanceMs = 0;
  const ULONGLONG now = GetTickCount64();
  if (s_lastMaintenanceMs != 0 && now - s_lastMaintenanceMs < 50)
    return;

  s_lastMaintenanceMs = now;

  // T05: 기재 작업은 FPS가 아니라 실제 시간 cadence에서 처리합니다.
  TickTraitChangeAsync();
  TickBatchRandomTraitJobT05();

  // 아래 가벼운 유지 작업은 200ms 주기를 유지합니다.
  static ULONGLONG s_lastLightMaintenanceMs = 0;
  if (s_lastLightMaintenanceMs != 0 && now - s_lastLightMaintenanceMs < 200)
    return;
  s_lastLightMaintenanceMs = now;

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
