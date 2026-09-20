#pragma once

namespace DX11Base {
  void SetInfiniteTalk(bool enable);
  void ScanDuelDebateFlagCandidates();
  void LogDuelDebateFocusedCandidates();
  void LogInteractionUsageFlags(uintptr_t officerBase);
  bool StartDuelDebateCapture();
  void LogDuelDebateCaptureResult();
  void StopDuelDebateCapture();

} // namespace DX11Base