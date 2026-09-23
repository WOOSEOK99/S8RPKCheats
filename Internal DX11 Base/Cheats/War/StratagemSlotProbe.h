#pragma once

namespace DX11Base {
  // Captures the battle-side object used by the old CT and dumps the
  // stratagem count fields. Pre-battle these may still be zero; re-run after
  // the battle actually starts.
  void ScanStratagemFiveSlotCandidates();

  // Guarded write test for ID5 count. Only writes when IDs1~4 look like
  // plausible in-battle counts and the selected side object is valid.
  bool SetStratagemFiveCountTest(bool enable);
}
