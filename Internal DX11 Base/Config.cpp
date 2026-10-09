#define SaveConfig SaveConfigBase
#define LoadConfig LoadConfigBase
#include "ConfigBase.inc"
#undef LoadConfig
#undef SaveConfig

#include "Cheats/Officer/TraitViewerFeature.h"
#include "Cheats/Officer/AffinityDisplayVisibilityFix.h"
#include "Cheats/Civilian/JewelSettings.h"
#include "Cheats/Civilian/MissionCpuHeroExclusion.h"
#include "Cheats/Civilian/TechCityEditorVisibility.h"
#include "Cheats/System/StartSetting.h"
#include "Cheats/War/CouncilContinueAfterMove.h"
#include "Cheats/War/CouncilExecuteFreeOfficers.h"
#include "Cheats/War/ShortBattleCooldown.h"
#include "Cheats/War/TotalWarCycleShortening.h"
#include "Cheats/War/TroopCountCombatScaling.h"
#include "Cheats/War/GovernorPrisonerDisposal.h"
#include "Cheats/War/IsolatedTerritoryMovementFeature.h"
#include "Cheats/War/PrisonerCaptureManagement.h"
#include "Cheats/War/ReinforcementArrivalAction.h"
#include "Cheats/War/ReinforcementDefenderPlacement.h"
#include "Cheats/War/StratagemGaugeMax.h"
#include "Cheats/War/StratagemSlotProbe.h"
#include "Cheats/War/Spell5HealProbe.h"

namespace DX11Base {
  static void UpsertBoolConfigValue(const char *name, bool value) {
    std::ifstream in(GetConfigPath(), std::ios::binary);
    if (!in.is_open()) {
      ReportConfigSaveFailure("추가 설정 파일 읽기", name);
      return;
    }

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
    if (configEnd == std::string::npos) {
      ReportConfigSaveFailure("설정 파일 형식 확인", name);
      return;
    }

    size_t insertPos = data.rfind('\n', configEnd);
    insertPos = (insertPos == std::string::npos) ? configEnd : insertPos + 1;

    const std::string line = std::string("  \"") + name + "\": " +
                             (value ? "true" : "false") + ",\n";
    data.insert(insertPos, line);

    std::ofstream out(GetConfigPath(), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      ReportConfigSaveFailure("추가 설정 파일 열기", name);
      return;
    }

    out << data;
    out.flush();
    const bool writeOk = out.good();
    out.close();
    if (!writeOk || out.fail())
      ReportConfigSaveFailure("추가 설정 파일 쓰기", name);
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
    if (!in.is_open()) {
      ReportConfigSaveFailure("추가 설정 파일 읽기", name);
      return;
    }

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
    if (configEnd == std::string::npos) {
      ReportConfigSaveFailure("설정 파일 형식 확인", name);
      return;
    }

    size_t insertPos = data.rfind('\n', configEnd);
    insertPos = (insertPos == std::string::npos) ? configEnd : insertPos + 1;

    const std::string line =
        std::string("  \"") + name + "\": " + std::to_string(value) + ",\n";
    data.insert(insertPos, line);

    std::ofstream out(GetConfigPath(), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      ReportConfigSaveFailure("추가 설정 파일 열기", name);
      return;
    }

    out << data;
    out.flush();
    const bool writeOk = out.good();
    out.close();
    if (!writeOk || out.fail())
      ReportConfigSaveFailure("추가 설정 파일 쓰기", name);
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

  static void UpsertStringConfigValue(const char *name, const std::string &value) {
    std::ifstream in(GetConfigPath(), std::ios::binary);
    if (!in.is_open()) {
      ReportConfigSaveFailure("추가 설정 파일 읽기", name);
      return;
    }

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
    if (configEnd == std::string::npos) {
      ReportConfigSaveFailure("설정 파일 형식 확인", name);
      return;
    }

    size_t insertPos = data.rfind('\n', configEnd);
    insertPos = (insertPos == std::string::npos) ? configEnd : insertPos + 1;
    const std::string line = std::string("  \"") + name + "\": \"" + value + "\",\n";
    data.insert(insertPos, line);

    std::ofstream out(GetConfigPath(), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      ReportConfigSaveFailure("추가 설정 파일 열기", name);
      return;
    }
    out << data;
    out.flush();
    const bool writeOk = out.good();
    out.close();
    if (!writeOk || out.fail())
      ReportConfigSaveFailure("추가 설정 파일 쓰기", name);
  }

  static bool LoadStringConfigValue(const char *name, std::string &value) {
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
      const size_t quoteStart = line.find('"', colonPos + 1);
      if (quoteStart == std::string::npos)
        return false;
      const size_t quoteEnd = line.find('"', quoteStart + 1);
      if (quoteEnd == std::string::npos)
        return false;
      value = line.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
      return true;
    }
    return false;
  }

  void SaveConfig() {
    if (IsConfigFileReadOnly()) {
      ReportConfigSaveFailure("설정 파일 읽기 전용");
      return;
    }

    SaveConfigBase();
    UpsertBoolConfigValue("bAIWarImprove", bAIWarImprove);
    UpsertBoolConfigValue("bMissionCpuHeroExclusion", bMissionCpuHeroExclusion);
    UpsertBoolConfigValue("bTechCityEditorVisible", bTechCityEditorVisible);
    UpsertBoolConfigValue("bTotalWarCycleShortening", bTotalWarCycleShortening);
    for (int i = 0; i < kTengiListEventCount; ++i) {
      const std::string suffix = std::to_string(i);
      UpsertBoolConfigValue(("tengiListAllow" + suffix).c_str(), g_tengiListAllowed[i]);
      UpsertIntConfigValue(("tengiListMonths" + suffix).c_str(), g_tengiListDurationMonths[i]);
      UpsertIntConfigValue(("tengiListCooldownYears" + suffix).c_str(), GetTengiListCooldownYears(i));
    }
    UpsertBoolConfigValue("bCouncilContinueAfterMove", bCouncilContinueAfterMove);
    UpsertBoolConfigValue("bCouncilExecuteFreeOfficers", bCouncilExecuteFreeOfficers);
    UpsertBoolConfigValue("bStratagemFiveEnabled", bStratagemFiveEnabled);
    UpsertBoolConfigValue("bShortBattleCooldownEnabled", bShortBattleCooldownEnabled);
    UpsertIntConfigValue("iShortBattleCooldownDays", iShortBattleCooldownDays);
    UpsertBoolConfigValue("bTroopCountCombatScaling", bTroopCountCombatScaling);
    UpsertBoolConfigValue("bPrisonerCaptureManagement", bPrisonerCaptureManagement);
    UpsertBoolConfigValue("bIsolatedTerritoryMovement", bIsolatedTerritoryMovement);
    UpsertBoolConfigValue("bGovernorPrisonerDisposal", bGovernorPrisonerDisposal);
    UpsertBoolConfigValue("bGovernorPrisonerConsumePrivilege", bGovernorPrisonerConsumePrivilege);
    UpsertBoolConfigValue("bReinforcementArrivalAction", bReinforcementArrivalAction);
    UpsertBoolConfigValue("bReinforcementDefenderPlacement", bReinforcementDefenderPlacement);
    UpsertBoolConfigValue("bMaxAttackStratagemGauge", bMaxAttackStratagemGauge);
    UpsertBoolConfigValue("bMaxDefenseStratagemGauge", bMaxDefenseStratagemGauge);
    {
      const Spell5CustomSettings s = GetSpell5CustomSettings();
      UpsertIntConfigValue("iStratagem5Target", s.target);
      UpsertIntConfigValue("iStratagem5Effect1", s.effect1);
      UpsertIntConfigValue("iStratagem5Power1", s.power1);
      UpsertIntConfigValue("iStratagem5Duration1", s.duration1);
      UpsertIntConfigValue("iStratagem5Effect2", s.effect2);
      UpsertIntConfigValue("iStratagem5Power2", s.power2);
      UpsertIntConfigValue("iStratagem5Duration2", s.duration2);
      UpsertIntConfigValue("iStratagem5Range", s.range);
      UpsertIntConfigValue("iStratagem5HealAmount", s.healAmount);
    }
    UpsertBoolConfigValue("bOfficerChangeNotify", bOfficerChangeNotify);
    UpsertBoolConfigValue("bAffinityDisplay", bAffinityDisplay);
    UpsertBoolConfigValue("bAnnualSpecialAbilityAutoAssign", bAnnualSpecialAbilityAutoAssign);
    UpsertBoolConfigValue("bAIOfficerAutoGrowth", bAIOfficerAutoGrowth);
    UpsertIntConfigValue("iAIOfficerGrowthSpeed", iAIOfficerGrowthSpeed);
    UpsertBoolConfigValue("bAIOfficerGrowthRestoreNone", bAIOfficerGrowthRestoreNone);
    UpsertBoolConfigValue("bTraitViewer", bTraitViewer);
    UpsertBoolConfigValue("bAllJewelsOpen", IsAllJewelsOpenPreferred());
    UpsertBoolConfigValue("bAllSecondaryJewels", IsAllSecondaryJewelsEnabled());
    UpsertStringConfigValue("secondaryJewelForceMask", GetSecondaryJewelForceMaskHex());
  }

  void LoadConfig() {
    LoadConfigBase();
    // 기존 '중지 성성 취소'는 별도 메뉴에서 제거했으므로 숨겨진 설정으로
    // 자동 취소가 지속되지 않게 한다. 전기 목록 관리 실제 적용은 후속 구현이다.
    bCancelCastleEvent = false;
    for (int i = 0; i < kTengiListEventCount; ++i) {
      const std::string suffix = std::to_string(i);
      bool allowed = g_tengiListAllowed[i];
      int months = g_tengiListDurationMonths[i];
      if (LoadBoolConfigValue(("tengiListAllow" + suffix).c_str(), allowed))
        g_tengiListAllowed[i] = allowed;
      if (LoadIntConfigValue(("tengiListMonths" + suffix).c_str(), months))
        g_tengiListDurationMonths[i] = months < 0 ? 0 : (months > 120 ? 120 : months);
      int cooldownYears = -1;
      LoadIntConfigValue(("tengiListCooldownYears" + suffix).c_str(), cooldownYears);
      SetTengiListCooldownYears(i, cooldownYears);
    }

    if (bUndiscoveredToRonin) {
      SetUndiscoveredToRonin(true);
      AddLog(u8"[Config] 미발견 무장 재야 변경 watcher 조기 시작");
    }

    bool savedAIWarImprove = false;
    if (LoadBoolConfigValue("bAIWarImprove", savedAIWarImprove)) {
      bAIWarImprove = savedAIWarImprove;
      SetAIWarImprove(savedAIWarImprove);
      AddLog(u8"[Config] AI 전투 개선 설정 로드: %s", savedAIWarImprove ? "ON" : "OFF");
    }

    bool savedMissionCpuHeroExclusion = false;
    if (LoadBoolConfigValue("bMissionCpuHeroExclusion", savedMissionCpuHeroExclusion)) {
      bMissionCpuHeroExclusion = savedMissionCpuHeroExclusion;
      if (!SetMissionCpuHeroExclusion(savedMissionCpuHeroExclusion))
        bMissionCpuHeroExclusion = IsMissionCpuHeroExclusionApplied();
      AddLog(u8"[Config] 임무 미지원 CPU 강제 배정 제외 설정 로드: %s",
             bMissionCpuHeroExclusion ? "ON" : "OFF");
    }

    bool savedTechCityEditorVisible = false;
    if (LoadBoolConfigValue("bTechCityEditorVisible", savedTechCityEditorVisible)) {
      bTechCityEditorVisible = savedTechCityEditorVisible;
      if (!SetTechCityEditorVisible(savedTechCityEditorVisible))
        bTechCityEditorVisible = IsTechCityEditorVisibleApplied();
      AddLog(u8"[Config] 게임 내 도시 편집기 기술도시 표시 설정 로드: %s",
             bTechCityEditorVisible ? "ON" : "OFF");
    }

    bool savedTotalWarCycleShortening = bTotalWarCycleShortening;
    if (LoadBoolConfigValue("bTotalWarCycleShortening", savedTotalWarCycleShortening))
      bTotalWarCycleShortening = savedTotalWarCycleShortening;

    if (!SetTotalWarCycleShortening(bTotalWarCycleShortening))
      bTotalWarCycleShortening = IsTotalWarCycleShorteningApplied();

    AddLog(u8"[Config] 결전 발생 주기 단축 설정 로드: %s",
           bTotalWarCycleShortening ? "ON" : "OFF");

    bool savedCouncilContinueAfterMove = false;
    LoadBoolConfigValue("bCouncilContinueAfterMove", savedCouncilContinueAfterMove);
    bCouncilContinueAfterMove = savedCouncilContinueAfterMove;
    if (!SetCouncilContinueAfterMove(bCouncilContinueAfterMove))
      bCouncilContinueAfterMove = IsCouncilContinueAfterMoveApplied();
    AddLog(u8"[Config] 도시 이동 후 평정 지속 설정 로드: %s",
           bCouncilContinueAfterMove ? "ON" : "OFF");

    bool savedCouncilExecuteFreeOfficers = false;
    LoadBoolConfigValue("bCouncilExecuteFreeOfficers", savedCouncilExecuteFreeOfficers);
    bCouncilExecuteFreeOfficers = savedCouncilExecuteFreeOfficers;
    if (!SetCouncilExecuteFreeOfficers(bCouncilExecuteFreeOfficers))
      bCouncilExecuteFreeOfficers = IsCouncilExecuteFreeOfficersApplied();
    AddLog(u8"[Config] 세력 도시 재야 무장 처단 설정 로드: %s",
           bCouncilExecuteFreeOfficers ? "ON" : "OFF");

    bool savedStratagemFiveEnabled = false;
    LoadBoolConfigValue("bStratagemFiveEnabled", savedStratagemFiveEnabled);
    bStratagemFiveEnabled = savedStratagemFiveEnabled;
    if (!SetStratagemFiveFeature(bStratagemFiveEnabled))
      bStratagemFiveEnabled = false;
    AddLog(u8"[Config] 5번 책략 활성화 설정 로드: %s",
           bStratagemFiveEnabled ? "ON" : "OFF");

    bool savedIsolatedTerritoryMovement = true;
    const bool hasIsolatedTerritoryMovementSetting =
        LoadBoolConfigValue("bIsolatedTerritoryMovement",
                            savedIsolatedTerritoryMovement);
    bIsolatedTerritoryMovement = savedIsolatedTerritoryMovement;
    if (!SetIsolatedTerritoryMovementFeature(bIsolatedTerritoryMovement))
      bIsolatedTerritoryMovement = IsIsolatedTerritoryMovementFeatureApplied();
    AddLog(u8"[Config] 단절 영토 무장 이동 제한 설정 로드%s: %s",
           hasIsolatedTerritoryMovementSetting ? "" : "(기본값)",
           bIsolatedTerritoryMovement ? "ON" : "OFF");

    bool savedPrisonerCaptureManagement = bPrisonerCaptureManagement;
    LoadBoolConfigValue("bPrisonerCaptureManagement",
                        savedPrisonerCaptureManagement);
    bPrisonerCaptureManagement = savedPrisonerCaptureManagement;
    if (!SetPrisonerCaptureManagement(bPrisonerCaptureManagement))
      bPrisonerCaptureManagement = IsPrisonerCaptureManagementApplied();

    AddLog(u8"[Config] 포로 관리 설정 로드: %s",
           bPrisonerCaptureManagement ? "ON" : "OFF");

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

    bool savedMaxAttackStratagemGauge = false;
    LoadBoolConfigValue("bMaxAttackStratagemGauge", savedMaxAttackStratagemGauge);
    bMaxAttackStratagemGauge = savedMaxAttackStratagemGauge;

    bool savedMaxDefenseStratagemGauge = false;
    LoadBoolConfigValue("bMaxDefenseStratagemGauge", savedMaxDefenseStratagemGauge);
    bMaxDefenseStratagemGauge = savedMaxDefenseStratagemGauge;

    {
      Spell5CustomSettings s = GetSpell5CustomSettings();
      LoadIntConfigValue("iStratagem5Target", s.target);
      LoadIntConfigValue("iStratagem5Effect1", s.effect1);
      LoadIntConfigValue("iStratagem5Power1", s.power1);
      LoadIntConfigValue("iStratagem5Duration1", s.duration1);
      LoadIntConfigValue("iStratagem5Effect2", s.effect2);
      LoadIntConfigValue("iStratagem5Power2", s.power2);
      LoadIntConfigValue("iStratagem5Duration2", s.duration2);
      LoadIntConfigValue("iStratagem5Range", s.range);
      LoadIntConfigValue("iStratagem5HealAmount", s.healAmount);
      SetSpell5CustomSettings(s);
      const Spell5CustomSettings applied = GetSpell5CustomSettings();
      AddLog(u8"[Config] 5번 책략 설정 로드: 대상=%d 효과1=%d/%d/%d 효과2=%d/%d/%d 범위=%d 추가병력=%d",
             applied.target,
             applied.effect1, applied.power1, applied.duration1,
             applied.effect2, applied.power2, applied.duration2,
             applied.range, applied.healAmount);
    }

    if (bMaxAttackStratagemGauge || bMaxDefenseStratagemGauge) {
      if (!SetStratagemGaugeCapture(true)) {
        bMaxAttackStratagemGauge = false;
        bMaxDefenseStratagemGauge = false;
        AddLog(u8"[Config] 책략 게이지 최대 설정 적용 실패 - 두 설정을 OFF 처리");
      } else {
        AddLog(u8"[Config] 책략 게이지 최대 설정 로드: 공격=%s / 수비=%s",
               bMaxAttackStratagemGauge ? "ON" : "OFF",
               bMaxDefenseStratagemGauge ? "ON" : "OFF");
      }
    }

    bool savedOfficerChangeNotify = false;
    if (LoadBoolConfigValue("bOfficerChangeNotify", savedOfficerChangeNotify)) {
      bOfficerChangeNotify = savedOfficerChangeNotify;
      AddLog(u8"[Config] 사망장수 및 등용장수 알림 설정 로드: %s",
             bOfficerChangeNotify ? "ON" : "OFF");
    }

    bool savedAffinityDisplay = false;
    if (LoadBoolConfigValue("bAffinityDisplay", savedAffinityDisplay)) {
      bAffinityDisplay = savedAffinityDisplay;
      if (!SetAffinityDisplayWithVisibilityFix(bAffinityDisplay))
        bAffinityDisplay = IsAffinityDisplayApplied();
      AddLog(u8"[Config] 상성 인게임 표시 설정 로드: %s",
             bAffinityDisplay ? "ON" : "OFF");
    }

    bool savedAnnualSpecialAbilityAutoAssign = false;
    if (LoadBoolConfigValue("bAnnualSpecialAbilityAutoAssign",
                            savedAnnualSpecialAbilityAutoAssign)) {
      bAnnualSpecialAbilityAutoAssign = savedAnnualSpecialAbilityAutoAssign;
      AddLog(u8"[Config] 자동 특수능력 부여 설정 로드: %s",
             bAnnualSpecialAbilityAutoAssign ? "ON" : "OFF");
    }

    bool savedAIOfficerAutoGrowth = false;
    if (LoadBoolConfigValue("bAIOfficerAutoGrowth", savedAIOfficerAutoGrowth))
      bAIOfficerAutoGrowth = savedAIOfficerAutoGrowth;

    int savedAIOfficerGrowthSpeed = 2;
    if (LoadIntConfigValue("iAIOfficerGrowthSpeed", savedAIOfficerGrowthSpeed)) {
      if (savedAIOfficerGrowthSpeed < 1) savedAIOfficerGrowthSpeed = 1;
      if (savedAIOfficerGrowthSpeed > 3) savedAIOfficerGrowthSpeed = 3;
      iAIOfficerGrowthSpeed = savedAIOfficerGrowthSpeed;
    }

    bool savedAIOfficerGrowthRestoreNone = false;
    if (LoadBoolConfigValue("bAIOfficerGrowthRestoreNone",
                            savedAIOfficerGrowthRestoreNone)) {
      bAIOfficerGrowthRestoreNone = savedAIOfficerGrowthRestoreNone;
    }

    if (!bAIOfficerAutoGrowth && bAnnualSpecialAbilityAutoAssign) {
      bAnnualSpecialAbilityAutoAssign = false;
      AddLog(u8"[Config] AI 자동성장 OFF이므로 자동 특수능력 부여도 OFF 처리");
    }

    AddLog(u8"[Config] AI 무장 자동성장 설정 로드: %s / 속도=%d / 없음복귀=%s",
           bAIOfficerAutoGrowth ? "ON" : "OFF",
           iAIOfficerGrowthSpeed,
           bAIOfficerGrowthRestoreNone ? "YES" : "NO");

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

    std::string savedSecondaryJewelMask;
    const bool hasSecondaryJewelMask =
        LoadStringConfigValue("secondaryJewelForceMask", savedSecondaryJewelMask);
    if (hasSecondaryJewelMask) {
      if (!LoadSecondaryJewelForceMaskHex(savedSecondaryJewelMask)) {
        SetAllSecondaryJewelsEnabled(false);
        AddLog(u8"[Config] 보조 보주 선택 마스크가 유효하지 않아 전체 해제 처리");
      } else {
        AddLog(u8"[Config] 보조 보주 개별 강제 사용 선택 마스크 로드");
      }
    } else {
      bool savedAllSecondaryJewels = false;
      const bool hasAllSecondaryJewelSetting =
          LoadBoolConfigValue("bAllSecondaryJewels", savedAllSecondaryJewels);
      if (!SetAllSecondaryJewelsEnabled(savedAllSecondaryJewels))
        savedAllSecondaryJewels = IsAllSecondaryJewelsEnabled();
      AddLog(u8"[Config] 보조 보주 구버전 전체 사용 설정 마이그레이션%s: %s",
             hasAllSecondaryJewelSetting ? "" : "(기본값)",
             savedAllSecondaryJewels ? "ON" : "OFF");
    }

  }
} // namespace DX11Base