#pragma once

namespace DX11Base {
  // Installs/removes a small read-only capture hook for the battle-side info object.
  // The old CT identifies side at +0x18 and stratagem gauge (uint16) at +0x154.
  bool SetStratagemGaugeCapture(bool enable);

  // Called from BattleMonitor while combat is active.
  // Only writes the selected side(s), and only after validating the captured object.
  void UpdateStratagemGaugeMax();
}
