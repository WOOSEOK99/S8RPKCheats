#include <atomic>

namespace DX11Base {
  void SetInstantResonance(bool enable);
  void SetDialogueResonanceThree(bool enable);
  void RunResonanceDebugPoll();
  extern std::atomic<bool> g_resonanceThreadRunning;
  extern std::atomic<bool> g_resonanceThreeThreadRunning;
}
