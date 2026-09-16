#include "pch.h"
#include "TraitViewerFeature.h"

#include "TraitViewer.h"
#include "TraitViewerBattleMap.h"

namespace DX11Base {

bool SetTraitViewerFeature(bool enable) {
  if (enable) {
    if (!SetTraitViewer(true))
      return false;
    if (!SetTraitViewerBattleMap(true)) {
      SetTraitViewer(false);
      return false;
    }
    return true;
  }

  // 전투맵 패치는 활성 UI 객체가 남아 있으면 해제를 거부하므로 먼저 처리합니다.
  if (!SetTraitViewerBattleMap(false))
    return false;
  if (!SetTraitViewer(false)) {
    // 코어 해제가 비정상적으로 실패하면 가능한 범위에서 전투맵을 다시 활성화합니다.
    SetTraitViewerBattleMap(true);
    return false;
  }
  return true;
}

bool IsTraitViewerFeatureApplied() {
  return IsTraitViewerApplied() && IsTraitViewerBattleMapApplied();
}

} // namespace DX11Base
