#include "pch.h"
#include "TraitViewerFeature.h"

#include "TraitViewerWidths.h"
#include "TraitViewerRoster.h"
#include "TraitViewerBattlePrep.h"
#include "TraitViewerBattleMap.h"
#include "TraitViewerBattleInfo.h"
#include "TraitViewerOfficerInfo.h"
#include "../../showlog.h"

namespace DX11Base {
namespace {

bool RollbackEnable(bool officerInfo, bool battleInfo, bool battleMap,
                    bool battlePrep, bool widths, bool roster) {
  bool ok = true;
  if (officerInfo) ok = SetTraitViewerOfficerInfo(false) && ok;
  if (battleInfo) ok = SetTraitViewerBattleInfo(false) && ok;
  if (battleMap) ok = SetTraitViewerBattleMap(false) && ok;
  if (battlePrep) ok = SetTraitViewerBattlePrep(false) && ok;
  if (widths) ok = SetTraitViewerWidths(false) && ok;
  if (roster) ok = SetTraitViewerRoster(false) && ok;
  return ok;
}

} // namespace

bool SetTraitViewerFeature(bool enable) {
  if (enable) {
    bool roster = false;
    bool widths = false;
    bool battlePrep = false;
    bool battleMap = false;
    bool battleInfo = false;
    bool officerInfo = false;

    if (!SetTraitViewerRoster(true))
      return false;
    roster = true;

    if (!SetTraitViewerWidths(true)) {
      RollbackEnable(false, false, false, false, false, roster);
      return false;
    }
    widths = true;

    if (!SetTraitViewerBattlePrep(true)) {
      RollbackEnable(false, false, false, false, widths, roster);
      return false;
    }
    battlePrep = true;

    if (!SetTraitViewerBattleMap(true)) {
      RollbackEnable(false, false, false, battlePrep, widths, roster);
      return false;
    }
    battleMap = true;

    if (!SetTraitViewerBattleInfo(true)) {
      RollbackEnable(false, false, battleMap, battlePrep, widths, roster);
      return false;
    }
    battleInfo = true;

    if (!SetTraitViewerOfficerInfo(true)) {
      RollbackEnable(false, battleInfo, battleMap, battlePrep, widths, roster);
      return false;
    }
    officerInfo = true;

    AddLog(u8"[기재 화면] 전체 기능 적용 완료");
    return true;
  }

  // 활성 UI 객체를 참조하는 코드케이브부터 역순으로 해제합니다.
  if (!SetTraitViewerOfficerInfo(false))
    return false;
  if (!SetTraitViewerBattleInfo(false)) {
    SetTraitViewerOfficerInfo(true);
    return false;
  }
  if (!SetTraitViewerBattleMap(false)) {
    SetTraitViewerBattleInfo(true);
    SetTraitViewerOfficerInfo(true);
    return false;
  }
  if (!SetTraitViewerBattlePrep(false)) {
    SetTraitViewerBattleMap(true);
    SetTraitViewerBattleInfo(true);
    SetTraitViewerOfficerInfo(true);
    return false;
  }
  if (!SetTraitViewerWidths(false)) {
    SetTraitViewerBattlePrep(true);
    SetTraitViewerBattleMap(true);
    SetTraitViewerBattleInfo(true);
    SetTraitViewerOfficerInfo(true);
    return false;
  }
  if (!SetTraitViewerRoster(false)) {
    SetTraitViewerWidths(true);
    SetTraitViewerBattlePrep(true);
    SetTraitViewerBattleMap(true);
    SetTraitViewerBattleInfo(true);
    SetTraitViewerOfficerInfo(true);
    return false;
  }

  AddLog(u8"[기재 화면] 전체 기능 해제 완료");
  return true;
}

bool IsTraitViewerFeatureApplied() {
  return IsTraitViewerRosterApplied() &&
         IsTraitViewerWidthsApplied() &&
         IsTraitViewerBattlePrepApplied() &&
         IsTraitViewerBattleMapApplied() &&
         IsTraitViewerBattleInfoApplied() &&
         IsTraitViewerOfficerInfoApplied();
}

} // namespace DX11Base
