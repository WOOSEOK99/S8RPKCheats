#pragma once

namespace DX11Base {
  // Called from the DLL worker before its startup delay, independently of battle
  // data/toggles. Retry only during that bounded delay if runtime code is not ready.
  bool PrepareStratagemFiveUiBridge(bool reportFailure = true);

  // Unified ID5 experiment switch. ON raises every persistent request together
  // and lets the ordered runtime state machine advance DATA->COUNT->CAMP->MODEL->UI.
  // OFF unwinds the battle/model bindings and then restores the ID5 data record.
  bool SetStratagemFiveFeature(bool enable);

  // Captures the battle-side object used by the old CT and dumps the
  // stratagem count fields. Pre-battle these may still be zero; re-run after
  // the battle actually starts.
  void ScanStratagemFiveSlotCandidates();

  // Guarded write test for ID5 count. Only writes when IDs1~4 look like
  // plausible in-battle counts and the selected side object is valid.
  bool SetStratagemFiveCountTest(bool enable);

  // Read-only code diagnostic: scan only SAN8RPK.exe for code that both
  // references the stratagem-count layout (+10C/+13C/+14C area) and has a
  // nearby hard-coded compare against 4. Used to locate the UI/enumeration cap.
  void ScanStratagemFourLimitCodeCandidates();

  // Candidate #34 from the runtime diagnostic:
  // a 0x20-stride loop with "cmp r12d,4 / jb ..." near the stratagem fields.
  // Experimental: change only the loop bound immediate 4 -> 5, reversible.
  bool SetStratagemFiveLoopTest(bool enable);

  // Second stratagem table used by the old CT:
  // [[gameBase]+troopTypePointerOffset]+0x9D30, stride 0x20.
  // Reversible test: clone a valid row into row 5 and set ID/name/desc IDs to 5.
  bool SetStratagemFiveMetadataTest(bool enable);

  // Battle-generation reset. Keeps user-requested ON state and process hooks,
  // but abandons Camp/UI/model/sidecar pointers from the battle that just ended.
  void ResetStratagemFiveBattleRuntime();

  // Explicit game/save generation reset. Keeps user-requested ON state but
  // abandons all battle/UI object pointers from the previous p1 generation.
  void ResetStratagemFiveSessionRuntime(uintptr_t oldP1, uintptr_t newP1);

  // Keeps the requested ID5 count/metadata attached to the current battle
  // generation. Old pointers are abandoned across save/load transitions.
  void RefreshStratagemFiveBattleRuntime();

  // Runtime read-only UI probe. A build-guarded ResetBtnPos hook captures the
  // live TrickCommandDialogLayout pointer; this logs it once per layout.
  void UpdateStratagemFiveUiRuntimeProbe();
}
