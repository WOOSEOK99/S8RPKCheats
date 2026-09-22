#pragma once

namespace DX11Base {
  // Experimental probe:
  // temporarily converts the 4th active spell record into code 5 and tests
  // whether strategy effect code 20 is handled as native troop healing.
  bool SetSpell5HealProbe(bool enable);
}
