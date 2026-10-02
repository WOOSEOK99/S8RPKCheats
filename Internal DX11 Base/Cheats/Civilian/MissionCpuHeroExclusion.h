#pragma once

namespace DX11Base {
  // 부하 주인공이 임무에 지원하지 않았을 때 CPU의 강제 후보 배정에서 제외합니다.
  extern bool bMissionCpuHeroExclusion;

  bool SetMissionCpuHeroExclusion(bool enable);
  bool IsMissionCpuHeroExclusionApplied();
} // namespace DX11Base
