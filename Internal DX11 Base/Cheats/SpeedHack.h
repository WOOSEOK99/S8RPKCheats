#pragma once
#include <windows.h>

namespace DX11Base {

  extern float g_speedMultiplier;
  extern bool bSpeedHack;

  void SpeedHack_Init();
  void SpeedHack_Update();
  void SpeedHack_Sleep_Install();

} // namespace DX11Base