#pragma once

#include "Cheats/Civilian/CityInfoWindow.h"
#include <windows.h>

namespace DX11Base {

inline void RunT04RenderMaintenance() {
  static ULONGLONG s_lastMaintenanceMs = 0;
  const ULONGLONG now = GetTickCount64();
  if (s_lastMaintenanceMs != 0 && now - s_lastMaintenanceMs < 200)
    return;

  s_lastMaintenanceMs = now;
  RunCityRevoltAlwaysZero();
}

} // namespace DX11Base
