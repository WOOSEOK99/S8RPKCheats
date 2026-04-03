#pragma once
#include "pch.h"


namespace DX11Base {
  void SetBattleUnitCapture(bool enable);
  extern uintptr_t g_battleUnitAddr1;
  extern uintptr_t g_battleUnitAddr2;
  extern bool g_battUnitThreadRunning;
}