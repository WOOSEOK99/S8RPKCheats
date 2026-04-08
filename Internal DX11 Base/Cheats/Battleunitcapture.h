#pragma once
#include "pch.h"
#include <atomic>


namespace DX11Base {
  void SetBattleUnitCapture(bool enable);
  extern uintptr_t g_battleUnitAddr1;
  extern uintptr_t g_battleUnitAddr2;
  extern std::atomic_bool g_battUnitThreadRunning;
}