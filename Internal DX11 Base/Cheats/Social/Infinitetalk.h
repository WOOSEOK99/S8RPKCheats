#pragma once

namespace DX11Base {
  void SetInfiniteTalk(bool enable);
  void ScanDuelDebateFlagCandidates();
  void LogDuelDebateFocusedCandidates();
  void LogInteractionUsageFlags(uintptr_t officerBase);

} // namespace DX11Base