#pragma once
#include <cstdint>

namespace DX11Base {
  // Experimental probe:
  // code #5 uses a known-safe ally morale effect as the primary effect and
  // keeps effect #20 only as a secondary healing candidate.
  bool SetSpell5HealProbe(bool enable);

  // Read-only target diagnostic. While the probe is enabled, watches battle
  // units and logs units whose morale changes exactly like the +40 probe
  // (including cap-at-100 cases). This does not modify unit data.
  void UpdateSpell5TargetDiagnostics(int unitCount, uintptr_t unitListBase);
}
