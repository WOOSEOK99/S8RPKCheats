#define SaveConfig SaveConfigBase
#define LoadConfig LoadConfigBase
#include "ConfigBase.inc"
#undef LoadConfig
#undef SaveConfig

#include "Cheats/Officer/TraitViewerFeature.h"
#include "Cheats/Civilian/JewelSettings.h"
#include "Cheats/War/ShortBattleCooldown.h"
#include "Cheats/War/TroopCountCombatScaling.h"
#include "Cheats/War/GovernorPrisonerDisposal.h"
#include "Cheats/War/ReinforcementArrivalAction.h"
#include "Cheats/War/ReinforcementDefenderPlacement.h"

namespace DX11Base {
  static void UpsertBoolConfigValue(const char *name, bool value) {
    std::ifstream in(GetConfigPath(), std::ios::binary);
    if (!in.is_open())
      return;

    std::ostringstream ss;
    ss << in.rdbuf();
    in.close();
    std::string data = ss.str();

    const std::string key = std::string("\"") + name + "\"";
    size_t existing = data.find(key);
    if (existing != std::string::npos) {
      size_t lineStart = data.rfind('\n', existing);
      lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
      size_t lineEnd = data.find('\n', existing);
      if (lineEnd == std::string::npos)
        lineEnd = data.size();
      else
        ++lineEnd;
      data.erase(lineStart, lineEnd - lineStart);
    }

    const size_t configEnd = data.find("\"config_end\"");
    if (configEnd == std::string::npos)
      return;

    size_t insertPos = data.rfind('\n', configEnd);
    insertPos = (insertPos == std::string::npos) ? configEnd : insertPos + 1;

    const std::string line = std::string("  \"") + name + "\": " +
                             (value ? "true" : "false") + ",\n";
    data.insert(insertPos, line);

    std::ofstream out(GetConfigPath(), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
      return;
    out << data;
  }

  static bool LoadBoolConfigValue(const char *name, bool &value) {
    std::ifstream file(GetConfigPath());
    if (!file.is_open())
      return false;

    const std::string key = std::string("\"") + name + "\"";
    std::string line;
    while (std::getline(file, line)) {
      if (line.find(key) != std::string::npos) {
        value = (line.find("true") != std::string::npos);
        return true;
      }
    }
    return false;
  }

  static void UpsertIntConfigValue(const char *name, int value) {
    std::ifstream in(GetConfigPath(), std::ios::binary);
    if (!in.is_open())
      return;

    std::ostringstream ss;
    ss << in.rdbuf();
    in.close();
    std::string data = ss.str();

    const std::string key = std::string("\"") + name + "\"";
    size_t existing = data.find(key);
    if (existing != std::string::npos) {
      size_t lineStart = data.rfind('\n', existing);
      lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
      size_t lineEnd = data.find('\n', existing);
      if (lineEnd == std::string::npos)
        lineEnd = data.size();
      else
        ++lineEnd;
      data.erase(lineStart, lineEnd - lineStart);
    }

    const size_t configEnd = data.find("\"config_end\"");
    if (configEnd == std::string::npos)
      return;

    size_t insertPos = data.rfind('\n', configEnd);
    insertPos = (insertPos == std::string::npos) ? configEnd : insertPos + 1;

    const std::string line =
        std::string("  \"") + name + "\": " + std::to_string(value) + ",\n";
    data.insert(insertPos, line);

    std::ofstream out(GetConfigPath(), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
      return;
    out << data;
  }

  static bool LoadIntConfigValue(const char *name, int &value) {
    std::ifstream file(GetConfigPath());
    if (!file.is_open())
      return false;

    const std::string key = std::string("\"") + name + "\"";
    std::string line;
    while (std::getline(file, line)) {
      if (line.find(key) == std::string::npos)
        continue;

      const size_t colonPos = line.find(':');
      if (colonPos == std::string::npos)
        return false;

      try {
        value = std::stoi(line.substr(colonPos + 1));
        return true;
      } catch (...) {
        return false;
      }
    }
    return false;
  }

  void SaveConfig() {
    SaveConfigBase();
    UpsertBoolConfigValue("bAIWarImprove", bAIWarImprove);
    UpsertBoolConfigValue("bShortBattleCooldownEnabled", bShortBattleCooldownEnabled);
    UpsertIntConfigValue("iShortBattleCooldownDays", iShortBattleCooldownDays);
    UpsertBoolConfigValue("bTroopCountCombatScaling", bTroopCountCombatScaling);
    UpsertBoolConfigValue("bGovernorPrisonerDisposal", bGovernorPrisonerDisposal);
    UpsertBoolConfigValue("bGovernorPrisonerConsumePrivilege", bGovernorPrisonerConsumePrivilege);
    UpsertBoolConfigValue("bReinforcementArrivalAction", bReinforcementArrivalAction);
    UpsertBoolConfigValue("bReinforcementDefenderPlacement", bReinforcementDefenderPlacement);
    UpsertBoolConfigValue("bTraitViewer", bTraitViewer);
    UpsertBoolConfigValue("bAllJewelsOpen", IsAllJewelsOpenPreferred());
    UpsertBoolConfigValue("bAllSecondaryJewels", IsAllSecondaryJewelsEnabled());
  }

  void LoadConfig() {
    LoadConfigBase();

    bool savedAIWarImprove = false;
    if (LoadBoolConfigValue("bAIWarImprove", savedAIWarImprove)) {
      bAIWarImprove = savedAIWarImprove;
      SetAIWarImprove(savedAIWarImprove);
      AddLog(u8"[Config] AI 전투 개선 설정 로드: %s", savedAIWarImprove ? "ON" : "OFF");
    }

    bool savedGovernorPrisonerConsumePrivilege = false;
    if (LoadBoolConfigValue("bGovernorPrisonerConsumePrivilege",
                            savedGovernorPrisonerConsumePrivilege)) {
      bGovernorPrisonerConsumePrivilege = savedGovernorPrisonerConsumePrivilege;
    }

    bool savedGovernorPrisonerDisposal = false;
    if (LoadBoolConfigValue("bGovernorPrisonerDisposal",
                            savedGovernorPrisonerDisposal)) {
      bGovernorPrisonerDisposal = savedGovernorPrisonerDisposal;
      if (!SetGovernorPrisonerDisposal(savedGovernorPrisonerDisposal))
        bGovernorPrisonerDisposal = IsGovernorPrisonerDisposalApplied();

      AddLog(u8"[Config] 도독 포로 직접 처분 설정 로드: %s / 특권소비=%s",
             bGovernorPrisonerDisposal ? "ON" : "OFF",
             bGovernorPrisonerConsumePrivilege ? "ON" : "OFF");
    }

    bool savedReinforcementArrivalAction = false;
    if (LoadBoolConfigValue("bReinforcementArrivalAction",
                            savedReinforcementArrivalAction)) {
      bReinforcementArrivalAction = savedReinforcementArrivalAction;
      if (!SetReinforcementArrivalAction(savedReinforcementArrivalAction))
        bReinforcementArrivalAction = IsReinforcementArrivalActionApplied();

      AddLog(u8"[Config] 원군 도착 턴 즉시 행동 설정 로드: %s",
             bReinforcementArrivalAction ? "ON" : "OFF");
    }

    bool savedReinforcementDefenderPlacement = false;
    if (LoadBoolConfigValue("bReinforcementDefenderPlacement",
                            savedReinforcementDefenderPlacement)) {
      bReinforcementDefenderPlacement = savedReinforcementDefenderPlacement;
      if (!SetReinforcementDefenderPlacement(savedReinforcementDefenderPlacement))
        bReinforcementDefenderPlacement =
            IsReinforcementDefenderPlacementApplied();

      AddLog(u8"[Config] 수비측 원군 총대장 근처 배치 설정 로드: %s",
             bReinforcementDefenderPlacement ? "ON" : "OFF");
    }

    int savedShortBattleCooldownDays = 3;
    if (LoadIntConfigValue("iShortBattleCooldownDays", savedShortBattleCooldownDays))
      iShortBattleCooldownDays = savedShortBattleCooldownDays;

    bool savedShortBattleCooldownEnabled = false;
    if (LoadBoolConfigValue("bShortBattleCooldownEnabled", savedShortBattleCooldownEnabled)) {
      bShortBattleCooldownEnabled = savedShortBattleCooldownEnabled;
      if (!SetShortBattleCooldown(savedShortBattleCooldownEnabled, iShortBattleCooldownDays))
        bShortBattleCooldownEnabled = IsShortBattleCooldownApplied();

      AddLog(u8"[Config] 단기접전 쿨타임 설정 로드: %s / %d일",
             bShortBattleCooldownEnabled ? "ON" : "OFF",
             iShortBattleCooldownDays);
    }

    bool savedTroopCountCombatScaling = false;
    if (LoadBoolConfigValue("bTroopCountCombatScaling",
                            savedTroopCountCombatScaling)) {
      bTroopCountCombatScaling = savedTroopCountCombatScaling;
      if (!SetTroopCountCombatScaling(savedTroopCountCombatScaling))
        bTroopCountCombatScaling = IsTroopCountCombatScalingApplied();

      AddLog(u8"[Config] 병력수 공방 반영 설정 로드: %s",
             bTroopCountCombatScaling ? "ON" : "OFF");
    }

    // 이전 설정 파일에 키가 없으면 기본값(true)으로 실제 패치까지 적용합니다.
    bool savedTraitViewer = true;
    const bool hasTraitViewerSetting = LoadBoolConfigValue("bTraitViewer", savedTraitViewer);
    bTraitViewer = savedTraitViewer;
    if (!SetTraitViewerFeature(bTraitViewer))
      bTraitViewer = IsTraitViewerFeatureApplied();
    AddLog(u8"[Config] 기재 화면 보이기 설정 로드%s: %s",
           hasTraitViewerSetting ? "" : "(기본값)", bTraitViewer ? "ON" : "OFF");

    bool savedAllJewelsOpen = false;
    const bool hasAllJewelOpenSetting =
        LoadBoolConfigValue("bAllJewelsOpen", savedAllJewelsOpen);
    SetAllJewelsOpenPreference(savedAllJewelsOpen);
    AddLog(u8"[Config] 보주 전체 개방 설정 로드%s: %s",
           hasAllJewelOpenSetting ? "" : "(기본값)",
           savedAllJewelsOpen ? "ON" : "OFF");

    bool savedAllSecondaryJewels = false;
    const bool hasAllSecondaryJewelSetting =
        LoadBoolConfigValue("bAllSecondaryJewels", savedAllSecondaryJewels);
    if (!SetAllSecondaryJewelsEnabled(savedAllSecondaryJewels)) {
      savedAllSecondaryJewels = IsAllSecondaryJewelsEnabled();
    }
    AddLog(u8"[Config] 보조 보주 전체 사용 설정 로드%s: %s",
           hasAllSecondaryJewelSetting ? "" : "(기본값)",
           savedAllSecondaryJewels ? "ON" : "OFF");

  }
} // namespace DX11Base
