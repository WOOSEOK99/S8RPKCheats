// ======================================================
// 전투 환경 및 조건 설정 (Battle Environment Settings)
// ======================================================
#pragma once
#include <cstdint>

namespace DX11Base {

  // 매 프레임 혹은 전투 루프(하트비트)마다 호출되어
  // 날씨, 일자, 지형, 공성전(여울) 관련 메모리 조작을 수행합니다.
  void UpdateBattleEnvironment(uintptr_t exeBase, uintptr_t dayBaseAddr, uintptr_t unitListBase);

} // namespace DX11Base
