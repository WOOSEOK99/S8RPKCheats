#pragma once

namespace DX11Base {
  // 단기접전 재발생 쿨타임(SAN8RPK.exe+03BB0214)을 설정/복구합니다.
  // enable=true일 때 days를 적용하고, false일 때 활성화 전 값을 복구합니다.
  bool SetShortBattleCooldown(bool enable, int days);
  bool IsShortBattleCooldownApplied();
} // namespace DX11Base
