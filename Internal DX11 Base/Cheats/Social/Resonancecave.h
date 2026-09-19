#include <atomic>

namespace DX11Base {
  void SetInstantResonance(bool enable);
  void SetDialogueResonanceFour(bool enable);
  extern std::atomic<bool> g_resonanceThreadRunning;
  extern std::atomic<bool> g_resonanceFourThreadRunning;
}
