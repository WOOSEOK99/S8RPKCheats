#pragma once

namespace DX11Base {
  // CT의 "부대 공방 계산식 - 오리지널 시절때로" 분기 패치를 적용/복구합니다.
  bool SetTroopCountCombatScaling(bool enable);
  bool IsTroopCountCombatScalingApplied();
} // namespace DX11Base
