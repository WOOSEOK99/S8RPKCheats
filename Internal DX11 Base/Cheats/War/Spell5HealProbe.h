#pragma once
#include <cstdint>

namespace DX11Base {
  // Experimental probe:
  // code #5 uses a known-safe ally morale effect as the primary effect and
  // keeps effect #20 only as a secondary healing candidate.
  bool SetSpell5HealProbe(bool enable);

  // True only when the current game generation actually contains the verified
  // ID5 TrickData record. A persistent ON request by itself is not "ready".
  bool IsSpell5HealProbeReady();

  // Event-driven fast path used by the battle dialog. metadataTable points to
  // TrickData object row1 (vptr at +00, payload at +08), so the SpellRecord
  // payload table is metadataTable+8. This avoids waiting for gameBase.
  bool SetSpell5HealProbeFromMetadataTable(uintptr_t metadataTable);

  // Battle end reset. TrickData itself is game-generation data, so keep the
  // applied record but clear per-battle target/heal diagnostics.
  void ResetSpell5HealProbeBattleRuntime();

  // Save/load generation reset. Keeps the user's requested ON state but drops
  // any TrickData address that belonged to the previous game generation.
  void ResetSpell5HealProbeSession(uintptr_t oldP1, uintptr_t newP1);

  // Reapplies the requested ID5 data to the current TrickData table when a
  // new battle/game generation is ready.
  void RefreshSpell5HealProbe();

  // Read-only target diagnostic. While the probe is enabled, watches battle
  // units and logs units whose morale changes exactly like the +40 probe
  // (including cap-at-100 cases). This does not modify unit data.
  void UpdateSpell5TargetDiagnostics(int unitCount, uintptr_t unitListBase);
}
