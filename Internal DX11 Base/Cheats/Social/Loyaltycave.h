#pragma once
#include <atomic>

namespace DX11Base {
  void SetInstantLoyalty(bool enable);
  extern std::atomic_bool g_loyaltyThreadRunning;

} // namespace DX11Base