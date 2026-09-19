#include <atomic>

namespace DX11Base {
  void SetInstantResonance(bool enable);
  extern std::atomic<bool> g_resonanceThreadRunning;
}
