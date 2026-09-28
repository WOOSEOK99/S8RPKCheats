#pragma once

namespace DX11Base {
  // 사용자 설정에 저장되는 5번 책략 활성화 희망 상태.
  // 실제 전투 적용 상태는 StratagemSlotProbe.cpp의 lifecycle gate가 관리합니다.
  inline bool bStratagemFiveEnabled = false;

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

  // Explicit game/save generation reset. Keeps the user's desired checkbox
  // state, abandons all previous battle/UI pointers, and marks the newly loaded
  // generation unsafe for ID5 injection until a stable non-battle phase is seen.
  void ResetStratagemFiveSessionRuntime(uintptr_t oldP1, uintptr_t newP1);

  // Battle lifecycle gate. During an active battle the effective state is frozen:
  // checkbox changes are remembered but are applied only after safeNonBattle=true.
  // This prevents save-loaded/mid-battle UI generations from being modified.
  void UpdateStratagemFiveBattleLifecycle(bool battleActive,
                                          bool safeNonBattle);

  // True when the current battle generation must not receive any ID5 data/UI
  // mutation. BattleMonitor uses this to suppress data refresh and UI probes.
  bool ShouldSkipStratagemFiveCurrentBattle();

  // Keeps the requested ID5 count/metadata attached to the current battle
  // generation. Old pointers are abandoned across save/load transitions.
  void RefreshStratagemFiveBattleRuntime();

  // Runtime read-only UI probe. A build-guarded ResetBtnPos hook captures the
  // live TrickCommandDialogLayout pointer; this logs it once per layout.
  void UpdateStratagemFiveUiRuntimeProbe();
}
