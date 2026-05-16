#pragma once
#include "pch.h"

namespace DX11Base {
  void MonitorBattleStatus();
  bool IsInBattle();
  void MonitorTechStatus();
  int GetBattleDay();
  int GetFinalDay();
} // namespace DX11Base

// [2026-04-12] 신규 포착 정보: gameBase+0xD0 (00: 시작메뉴, 05:평정, 07:내정)
// [2026-04-15] 신규 포착 정보: gameBase+0xC0~c2 (시나리오 선택 주소)