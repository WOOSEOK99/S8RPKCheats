#pragma once

#include <cstdint>

namespace DX11Base {

  // Step 1 진단:
  // Viewer에서 확인된 AI 포로 결과 메시지 분기 두 곳에 소프트웨어
  // 브레이크포인트를 설치하여 원래 분기 동작은 유지한 채 레지스터/장수 후보만 기록합니다.
  bool SetAIBattleResultMonitor(bool enable);
  bool IsAIBattleResultMonitorApplied();

  // Menu::Loops() 백그라운드 틱에서 호출합니다.
  // 메시지 지점에서 수집한 스냅샷을 해석하고 0x05 -> 0x07 전환 때
  // 기존 RoninMonitor 알림창에 진단 묶음을 전달합니다.
  void AIBattleResultMonitor_Tick(uintptr_t gameBase);

} // namespace DX11Base
