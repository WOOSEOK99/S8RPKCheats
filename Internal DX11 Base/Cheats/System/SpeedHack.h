#pragma once
#include <windows.h>

namespace DX11Base {

  extern float g_speedMultiplier;
  extern bool bSpeedHack;

  void SpeedHack_Init();
  void SpeedHack_Update(uintptr_t p1);
  void SpeedHack_Sleep_Install();

  // SpeedHack의 가짜 시간과 분리된 실제 시간 기준 UI DeltaTime.
  // ImGui/알림 애니메이션이 배속 ON/OFF 전환의 영향을 받지 않도록 사용합니다.
  float SpeedHack_GetRealDeltaTime();

} // namespace DX11Base