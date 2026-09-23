#pragma once

namespace DX11Base {
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
}
