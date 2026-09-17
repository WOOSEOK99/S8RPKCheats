#include "pch.h"
#include "TraitViewerFeature.h"

#include "TraitViewerWidths.h"
#include "TraitViewerRoster.h"
#include "TraitViewerBattlePrep.h"
#include "TraitViewerBattleMap.h"
#include "TraitViewerBattleInfo.h"
#include "TraitViewerOfficerInfo.h"
#include "TraitViewerNativeEditors.h"
#include "../../showlog.h"

namespace DX11Base {
namespace {

void RollbackEnable(bool baseEditor, bool inProgressEditor, bool kirase,
                    bool officerInfo, bool battleInfo, bool battleMap,
                    bool battlePrep, bool widths, bool roster) {
  if (baseEditor) SetTraitViewerBaseEditor(false);
  if (inProgressEditor) SetTraitViewerInProgressEditor(false);
  if (kirase) SetTraitViewerKirase(false);
  if (officerInfo) SetTraitViewerOfficerInfo(false);
  if (battleInfo) SetTraitViewerBattleInfo(false);
  if (battleMap) SetTraitViewerBattleMap(false);
  if (battlePrep) SetTraitViewerBattlePrep(false);
  if (widths) SetTraitViewerWidths(false);
  if (roster) SetTraitViewerRoster(false);
}

} // namespace

bool SetTraitViewerFeature(bool enable) {
  if (enable) {
    bool roster = false, widths = false, battlePrep = false, battleMap = false;
    bool battleInfo = false, officerInfo = false, kirase = false;
    bool inProgressEditor = false, baseEditor = false;

    if (!SetTraitViewerRoster(true)) return false;
    roster = true;

    if (!SetTraitViewerWidths(true)) {
      RollbackEnable(false,false,false,false,false,false,false,false,roster);
      return false;
    }
    widths = true;

    if (!SetTraitViewerBattlePrep(true)) {
      RollbackEnable(false,false,false,false,false,false,false,widths,roster);
      return false;
    }
    battlePrep = true;

    if (!SetTraitViewerBattleMap(true)) {
      RollbackEnable(false,false,false,false,false,false,battlePrep,widths,roster);
      return false;
    }
    battleMap = true;

    if (!SetTraitViewerBattleInfo(true)) {
      RollbackEnable(false,false,false,false,false,battleMap,battlePrep,widths,roster);
      return false;
    }
    battleInfo = true;

    if (!SetTraitViewerOfficerInfo(true)) {
      RollbackEnable(false,false,false,false,battleInfo,battleMap,battlePrep,widths,roster);
      return false;
    }
    officerInfo = true;

    // CT 99000 후반부 순서: 99002 -> 91512 -> 91602.
    // 91620~91622는 TraitViewerRoster에서 같은 기재3 열 정의를 사용해 적용됩니다.
    if (!SetTraitViewerKirase(true)) {
      RollbackEnable(false,false,false,officerInfo,battleInfo,battleMap,battlePrep,widths,roster);
      return false;
    }
    kirase = true;

    if (!SetTraitViewerInProgressEditor(true)) {
      RollbackEnable(false,false,kirase,officerInfo,battleInfo,battleMap,battlePrep,widths,roster);
      return false;
    }
    inProgressEditor = true;

    if (!SetTraitViewerBaseEditor(true)) {
      RollbackEnable(false,inProgressEditor,kirase,officerInfo,battleInfo,battleMap,battlePrep,widths,roster);
      return false;
    }
    baseEditor = true;

    AddLog(u8"[기재 화면] CT 99000 전체 기능 적용 완료");
    return true;
  }

  // CT 후반 코드케이브부터 역순으로 복원합니다.
  if (!SetTraitViewerBaseEditor(false)) return false;
  if (!SetTraitViewerInProgressEditor(false)) {
    SetTraitViewerBaseEditor(true);
    return false;
  }
  if (!SetTraitViewerKirase(false)) {
    SetTraitViewerInProgressEditor(true);
    SetTraitViewerBaseEditor(true);
    return false;
  }
  if (!SetTraitViewerOfficerInfo(false)) {
    SetTraitViewerKirase(true);
    SetTraitViewerInProgressEditor(true);
    SetTraitViewerBaseEditor(true);
    return false;
  }
  if (!SetTraitViewerBattleInfo(false)) {
    SetTraitViewerOfficerInfo(true);
    SetTraitViewerKirase(true);
    SetTraitViewerInProgressEditor(true);
    SetTraitViewerBaseEditor(true);
    return false;
  }
  if (!SetTraitViewerBattleMap(false)) {
    SetTraitViewerBattleInfo(true);
    SetTraitViewerOfficerInfo(true);
    SetTraitViewerKirase(true);
    SetTraitViewerInProgressEditor(true);
    SetTraitViewerBaseEditor(true);
    return false;
  }
  if (!SetTraitViewerBattlePrep(false)) {
    SetTraitViewerBattleMap(true);
    SetTraitViewerBattleInfo(true);
    SetTraitViewerOfficerInfo(true);
    SetTraitViewerKirase(true);
    SetTraitViewerInProgressEditor(true);
    SetTraitViewerBaseEditor(true);
    return false;
  }
  if (!SetTraitViewerWidths(false)) {
    SetTraitViewerBattlePrep(true);
    SetTraitViewerBattleMap(true);
    SetTraitViewerBattleInfo(true);
    SetTraitViewerOfficerInfo(true);
    SetTraitViewerKirase(true);
    SetTraitViewerInProgressEditor(true);
    SetTraitViewerBaseEditor(true);
    return false;
  }
  if (!SetTraitViewerRoster(false)) {
    SetTraitViewerWidths(true);
    SetTraitViewerBattlePrep(true);
    SetTraitViewerBattleMap(true);
    SetTraitViewerBattleInfo(true);
    SetTraitViewerOfficerInfo(true);
    SetTraitViewerKirase(true);
    SetTraitViewerInProgressEditor(true);
    SetTraitViewerBaseEditor(true);
    return false;
  }

  AddLog(u8"[기재 화면] CT 99000 전체 기능 해제 완료");
  return true;
}

bool IsTraitViewerFeatureApplied() {
  return IsTraitViewerRosterApplied() &&
         IsTraitViewerWidthsApplied() &&
         IsTraitViewerBattlePrepApplied() &&
         IsTraitViewerBattleMapApplied() &&
         IsTraitViewerBattleInfoApplied() &&
         IsTraitViewerOfficerInfoApplied() &&
         IsTraitViewerKiraseApplied() &&
         IsTraitViewerInProgressEditorApplied() &&
         IsTraitViewerBaseEditorApplied();
}

} // namespace DX11Base
