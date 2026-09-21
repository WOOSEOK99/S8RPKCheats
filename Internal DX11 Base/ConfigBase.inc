#include "Config.h"
#include "BattleMonitor.h"
#include "Cheats.h"
#include "Cheats/System/MonthCapture.h"
#include "MenuState.h"
#include "NotificationManager.h"
#include "pch.h"
#include "showlog.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>


// 치트 기능 헤더들
#include "Cheats/Civilian/CityInfoWindow.h"
#include "Cheats/Civilian/BangmokCity.h"
#include "Cheats/War/Battleunitcapture.h"
#include "Cheats/Civilian/Bigcityconvert.h"
#include "Cheats/War/Catapult.h"
#include "Cheats/War/Celestia.h"
#include "Cheats/War/Defbuildingboost.h"
#include "Cheats/War/Dongto.h"
#include "Cheats/War/FactionLordBonus.h"
#include "Cheats/Social/Fastrelationship.h"
#include "Cheats/Social/Infinitegift.h"
#include "Cheats/Social/Infinitetalk.h"
#include "Cheats/Social/InstantLoveCave.h"
#include "Cheats/Social/Loyaltycave.h"
#include "Cheats/Civilian/NonggyeongCity.h"
#include "Cheats/Social/Resonancecave.h"
#include "Cheats/War/Roadblock.h"
#include "Cheats/Civilian/SangeopCity.h"
#include "Cheats/War/Selfheal.h"
#include "Cheats/System/SkillCondition.h"
#include "Cheats/System/StartSetting.h"
#include "Cheats/System/SystemMonth.h"
#include "Cheats/Civilian/Techzero.h"
#include "Cheats/Civilian/Techpointcave.h"
#include "Cheats/System/TengiCave.h"
#include "Cheats/War/Terrainignore.h"
#include "Cheats/War/SiegeWarfare.h"
#include "Cheats/Civilian/DomesticsMult.h"
#include "Cheats/Officer/OfficerData.h"
#include "Cheats/System/SpeedHack.h"

namespace DX11Base {
  extern HMODULE g_hModule;
  extern bool marriageApplied;
  void SetInstantAttitude(bool enable);
  void SetMarriageCondition(bool enable);

  // 람다 대용 래퍼 함수들
  void ApplyLoveHate(bool e) { SetInstantLoveCave(e, LoveMode::HateIgnore); }

  struct ConfigEntry {
    const char *key;
    const char *displayName; // 로그 출력용 이름
    bool *flag;
    bool *appliedState; // 지연 적용 상태 추적용
    void (*applyFunc)(bool);
    bool isWar;           // 전쟁 관련 기능 여부 (지연 활성화용)
    bool requiresP1;      // false: gameBase 안정화 후 P1 없이 자동 적용
  };

  // 적용 상태 플래그들
  static bool s_appBigCity = false;
  static bool s_appBangmokCity = false;
  static bool s_appNonggyeongCity = false;
  static bool s_appSangeopCity = false;
  static bool s_appAttitude = false;
  static bool s_appLoveHate = false;
  static bool s_appLoyalty = false;
  static bool s_appResonance = false;
  static bool s_appGift = false;
  static bool s_appTalk = false;
  static bool s_appDuel = false;
  static bool s_appDebate = false;
  static bool s_appRelation = false;
  static bool s_appMarriage = false;
  static bool s_appSelfHeal = false;
  static bool s_appDongto = false;
  static bool s_appTerrain = false;
  static bool s_appDefBuild = false;
  static bool s_appCatapult = false;
  static bool s_appCelestial = false;
  static bool s_appSiege = false;
  static bool s_appSiege2 = false;
  static bool s_appBattleUnit = false;
  static bool s_appRoadBlock = false;
  static bool s_appRoadBlock2 = false;
  static bool s_appStartSetting = false;
  static bool s_appTechZero = false;
  static bool s_appSkillCond = false;
  static bool s_appYumokCond = false;
  static bool s_appSangbyeongCond = false;
  static bool s_appFactionLordBonus = false;
  static bool s_appDomestics = false;
  static bool s_appUndiscovered = false;
  static uint64_t s_firstGameBaseTime = 0; // gameBase 감지 시점
  static uint64_t s_firstP1Time = 0;       // p1 감지 시점 기록용
  static bool s_isReset = true;            // 리셋 완료 상태 기록

  static ConfigEntry g_Entries[] = {
      {"bInfiniteAP", u8"행동력 무한", &bInfiniteAP, nullptr, nullptr, false, true},
      {"bFastJewel", u8"보주교체 무제한", &bFastJewel, nullptr, nullptr, false, true},
      {"bAutoFillSpecialties", u8"평정 시 남는 명품 자동 배분", &bAutoFillSpecialties, nullptr, nullptr, false, true},
      {"bInfiniteTavernRequests", u8"주점 청부 무한 유지", &bInfiniteTavernRequests, nullptr, nullptr, false, true},
      {"bBigCity", u8"대도시 전환", &bBigCity, &s_appBigCity, SetBigCityConvert, false, true},
      {"bBangmokCity", u8"방목도시 황폐화", &bBangmokCity, &s_appBangmokCity, SetBangmokCity, false, true},
      {"bNonggyeongCity", u8"농경도시 버프", &bNonggyeongCity, &s_appNonggyeongCity, SetNonggyeongCity, false, true},
      {"bSangeopCity", u8"상업도시 버프", &bSangeopCity, &s_appSangeopCity, SetSangeopCity, false, true},
      {"bAttitudeHack", u8"견문시 민심최대", &bAttitudeHack, &s_appAttitude, SetInstantAttitude, false, true},
      {"bInfiniteBanquet", u8"연회 무제한", &bInfiniteBanquet, nullptr, nullptr, false, true},
      {"bHateCave", u8"상극 무시/동지 조건 완화", &bHateCave, &s_appLoveHate, ApplyLoveHate, false, true},
      {"bLoyalty", u8"충성도 변경", &bLoyalty, &s_appLoyalty, SetInstantLoyalty, false, true},
      {"bResonance", u8"무조건 공명 발생", &bResonance, &s_appResonance, SetInstantResonance, false, true},
      {"bInfiniteGift", u8"증정 무한", &bInfiniteGift, &s_appGift, SetInfiniteGift, false, true},
      {"bInfiniteTalk", u8"담화 무한", &bInfiniteTalk, &s_appTalk, SetInfiniteTalk, false, true},
      {"bInfiniteDuel", u8"대련 무한", &bInfiniteDuel, &s_appDuel, SetInfiniteDuel, false, true},
      {"bInfiniteDebate", u8"토론 무한", &bInfiniteDebate, &s_appDebate, SetInfiniteDebate, false, true},
      {"bInfiniteMediation", u8"중개 무제한", &bInfiniteMediation, nullptr, nullptr, false, true},
      {"bFastRelationship", u8"즉시 경애 맺기", &bFastRelationship, &s_appRelation, SetFastRelationship, false, true},
      {"bAutoAffinityGrowth", u8"AI 친밀도 가속", &g_autoAffinityGrowthEnabled, nullptr, nullptr, false, true},
      {"marriageApplied", u8"결혼 무제한", &marriageApplied, &s_appMarriage, SetMarriageCondition, false, true},
      {"bSelfHeal", u8"전쟁: 자가 회복", &bSelfHeal, &s_appSelfHeal, SetSelfHeal, true, true},
      {"bDongto", u8"전쟁: 동토(금/군량 무한)", &bDongto, &s_appDongto, SetDongto, true, true},
      {"bTerrainIgnore", u8"전쟁: 격류낙석 지형 무시", &bTerrainIgnore, &s_appTerrain, SetTerrainIgnore, true, true},
      {"bDefBuilding", u8"전쟁: 방어건물 강화", &bDefBuilding, &s_appDefBuild, SetDefBuildingBoost, true, true},
      {"bCatapult", u8"전쟁: 투석기 강화", &bCatapult, &s_appCatapult, SetCatapultCheat, true, true},
      {"bCelestial", u8"전쟁: 천계 강화", &bCelestial, &s_appCelestial, SetCelestialMod, true, true},
      {"bSiegeWarfare", u8"전쟁: 공성전(성주변 얕은여울 및 회복)", &bSiegeWarfare, &s_appSiege, SetSiegeWarfare, true, true},
      {"bSiegeWarfare2", u8"전쟁: 공성전(성주변 2칸여울 및 회복)", &bSiegeWarfare2, &s_appSiege2, SetSiegeWarfare2, true, true},
      {"bWeatherSkillSimple", u8"전쟁: 날씨 간단 변경", &bWeatherSkillSimple, nullptr, nullptr, false, true},
      {"bWeatherSkillComplex", u8"전쟁: 날씨 대폭 변경", &bWeatherSkillComplex, nullptr, nullptr, false, true},
      {"bDateAlways15", u8"전쟁: 전투 일자 15일 고정", &bDateAlways15, nullptr, nullptr, false, true},
      {"bDateDynamic", u8"전쟁: 전투 일자 유동 조절", &bDateDynamic, nullptr, nullptr, false, true},
      {"bSkipDaysEnabled", u8"전쟁: 시작 경과일 스킵 활성화", &bSkipDaysEnabled, nullptr, nullptr, false, true},
      {"bTerrainAbilityAtkDef", u8"전쟁: 지형 능력 공방만", &bTerrainAbilityAtkDef, nullptr, nullptr, false, true},
      {"bTerrainAbilityAll", u8"전쟁: 지형 능력 전체", &bTerrainAbilityAll, nullptr, nullptr, false, true},
      {"bAllAggressive", u8"전쟁: 모든 무장 성향 적극", &bAllAggressive, nullptr, nullptr, false, true},
      {"bBattleUnit", u8"전쟁: 유닛 정보 캡처", &bBattleUnit, &s_appBattleUnit, SetBattleUnitCapture, false, true},
      {"bBattleMapShuffle", u8"기타: 평정 시 전투맵 셔플", &bBattleMapShuffle, nullptr, nullptr, false, true},
      {"bRoadBlock", u8"전쟁: 진로 방해 무시 (건녕↔교지)", &bRoadBlock, &s_appRoadBlock, SetRoadBlock, false, true},
      {"bRoadBlock2", u8"전쟁: 진로 방해 무시 (교지↔회계)", &bRoadBlock2, &s_appRoadBlock2, SetRoadBlock2, false, true},
      {"bStartSetting", u8"시나리오 수정", &bStartSetting, &s_appStartSetting, SetStartSetting, false, false},
      {"bTechZero", u8"모든 세력 기술 초기화", &bTechZero, &s_appTechZero, SetTechZero, false, false},
      {"bFileLog", u8"파일 로그출력", &bFileLog, nullptr, nullptr, false, false},
      {"bSkillCondition", u8"만병 습득 조건 해제", &bSkillCondition, &s_appSkillCond, ApplySkillCondition, false, true},
      {"bYumokCondition", u8"유목기병 습득 조건 해제", &bYumokCondition, &s_appYumokCond, ApplyYumokCondition, false, true},
      {"bSangbyeongCondition", u8"상병 습득 조건 해제", &bSangbyeongCondition, &s_appSangbyeongCond,
       ApplySangbyeongCondition, false, true},
      {"bFactionLordBonus", u8"세력 군주 보너스 자동 배정", &bFactionLordBonus, &s_appFactionLordBonus,
       SetFactionLordBonus, false, true},
      {"bMonitorRonin", u8"재야장수 감시", &bMonitorRonin, nullptr, nullptr, false, true},
      {"bAutoStatUp99", u8"능력치 한계돌파", &bAutoStatUp99, nullptr, nullptr, false, true},
      {"bAutoLoadMenu", u8"시작 시 설정 로드", &bAutoLoadMenu, nullptr, nullptr, false, true},
      {"bZeroInfamy", u8"매턴 악명 0", &bZeroInfamy, nullptr, nullptr, false, true},
      {"bSpeedHack", u8"배속 기능", &bSpeedHack, nullptr, nullptr, false, true},
      {"bCancelCastleEvent", u8"중지 성성 취소", &bCancelCastleEvent, nullptr, nullptr, false, true},
      {"bShowWidgetTengi", u8"위젯: 전기발생 취소", &bShowWidgetTengi, nullptr, nullptr, false, true},
      {"bShowWidgetNotif", u8"위젯: 알림확인", &bShowWidgetNotif, nullptr, nullptr, false, true},
      {"bShowWidgetHero", u8"위젯: 주인공", &bShowWidgetHero, nullptr, nullptr, false, true},
      {"bShowWidgetAllOfficers", u8"위젯: 모든무장", &bShowWidgetAllOfficers, nullptr, nullptr, false, true},
      {"bDomestics", u8"내정 배율", &bDomestics, &s_appDomestics, SetDomesticsMult, false, true},
      {"bUndiscoveredToRonin", u8"모든 미발견 무장 재야로 변경", &bUndiscoveredToRonin, &s_appUndiscovered, SetUndiscoveredToRonin, false, false},
      {"bHealLv1_Self", u8"치료Lv1 자기치료", &bHealLv1_Self, nullptr, nullptr, false, true},
      {"bHealLv2_Self", u8"치료Lv2 자기치료", &bHealLv2_Self, nullptr, nullptr, false, true},
      {"bHealLv3_Self", u8"치료Lv3 자기치료", &bHealLv3_Self, nullptr, nullptr, false, true}};

  std::string GetConfigPath() {
    char path[MAX_PATH];
    GetModuleFileNameA(DX11Base::g_hModule, path, MAX_PATH);
    return std::filesystem::path(path).parent_path().append("S8RPK_cheat_config.json").string();
  }

  // 설정 파일 저장 실패는 조용히 무시하지 않고 로그와 화면 알림으로 알려줍니다.
  // SaveConfig() 한 번에 여러 보조 항목이 연속 갱신되므로 알림은 3초간 중복 표시를 막습니다.
  static void ReportConfigSaveFailure(const char *stage, const char *key = nullptr) {
    const std::string path = GetConfigPath();
    AddLog(u8"[Config] 설정 저장 실패: 단계=%s / 항목=%s / 경로=%s",
           stage ? stage : "unknown",
           key ? key : "-",
           path.c_str());

    static ULONGLONG s_lastNotifyTick = 0;
    const ULONGLONG now = GetTickCount64();
    if (s_lastNotifyTick == 0 || now - s_lastNotifyTick >= 3000) {
      AddErrorNotification(
          u8"설정 저장 실패 - S8RPK_cheat_config.json의 쓰기 권한/읽기 전용 상태를 확인하세요.");
      s_lastNotifyTick = now;
    }
  }

  // [신규] D3D Hook 이전, 모듈 로드 극초기에 로그출력 여부만 가장 먼저 파악
  void LoadEarlyLogConfig() {
    std::ifstream file(GetConfigPath());
    if (!file.is_open())
      return;

    std::string line;
    while (std::getline(file, line)) {
      if (line.find("\"bFileLog\"") != std::string::npos && line.find("true") != std::string::npos) {
        bFileLog = true;
        break;
      }
    }
    file.close();
  }

  void SaveConfig() {
    std::ofstream file(GetConfigPath());
    if (!file.is_open()) {
      ReportConfigSaveFailure("기본 설정 파일 열기");
      return;
    }

    file << "{\n";
    for (size_t i = 0; i < (sizeof(g_Entries) / sizeof(g_Entries[0])); ++i) {
      file << "  \"" << g_Entries[i].key << "\": " << (*g_Entries[i].flag ? "true" : "false");
      file << ",\n";
    }
    file << "  \"g_speedMultiplier\": " << g_speedMultiplier << ",\n";
    file << "  \"g_notificationSpeed\": " << g_notificationSpeed << ",\n";
    file << "  \"fDomesticsPlayer\": " << fDomesticsPlayer << ",\n";
    file << "  \"fDomesticsForce\": " << fDomesticsForce << ",\n";
    file << "  \"g_cityMaxGrainLimit\": " << g_cityMaxGrainLimit << ",\n";
    file << "  \"g_cityKeepGrain\": " << g_cityKeepGrain << ",\n";
    file << "  \"g_cityExchangeRate\": " << g_cityExchangeRate << ",\n";
    file << "  \"g_cityAutoExchangeEnabled\": " << (g_cityAutoExchangeEnabled ? "true" : "false") << ",\n";
    file << "  \"g_cityRevoltAlwaysZero\": " << (g_cityRevoltAlwaysZero ? "true" : "false") << ",\n";
    file << "  \"nSkipDays\": " << nSkipDays << ",\n";
    file << "  \"v_HealLv1_Amount\": " << v_HealLv1_Amount << ",\n";
    file << "  \"v_HealLv2_Amount\": " << v_HealLv2_Amount << ",\n";
    file << "  \"v_HealLv2_Range\": " << v_HealLv2_Range << ",\n";
    file << "  \"v_HealLv3_Amount\": " << v_HealLv3_Amount << ",\n";
    file << "  \"v_HealLv3_Range\": " << v_HealLv3_Range << ",\n";
    file << "  \"v_DongtoLv3_Prob\": " << v_DongtoLv3_Prob << ",\n";
    file << "  \"v_DongtoLv3_StateProb\": " << v_DongtoLv3_StateProb << ",\n";
    file << "  \"v_CelestiaLv1_Prob\": " << v_CelestiaLv1_Prob << ",\n";
    file << "  \"v_CelestiaLv1_Amount\": " << v_CelestiaLv1_Amount << ",\n";
    file << "  \"v_CelestiaLv1_Range\": " << v_CelestiaLv1_Range << ",\n";
    file << "  \"v_CelestiaLv2_Prob\": " << v_CelestiaLv2_Prob << ",\n";
    file << "  \"v_CelestiaLv2_Amount\": " << v_CelestiaLv2_Amount << ",\n";
    file << "  \"v_CelestiaLv2_Range\": " << v_CelestiaLv2_Range << ",\n";
    file << "  \"v_CelestiaLv3_Prob\": " << v_CelestiaLv3_Prob << ",\n";
    file << "  \"v_CelestiaLv3_Amount\": " << v_CelestiaLv3_Amount << ",\n";
    file << "  \"v_CelestiaLv3_Range\": " << v_CelestiaLv3_Range << ",\n";
    file << "  \"v_Catapult_MinRange\": " << v_Catapult_MinRange << ",\n";
    file << "  \"v_Catapult_MaxRange\": " << v_Catapult_MaxRange << ",\n";
    file << "  \"v_Catapult_Amount\": " << v_Catapult_Amount << ",\n";
    file << "  \"v_Catapult_Range\": " << v_Catapult_Range << ",\n";
    file << "  \"v_WaterLv1_Amount\": " << v_WaterLv1_Amount << ",\n";
    file << "  \"v_WaterLv1_Range\": " << v_WaterLv1_Range << ",\n";
    file << "  \"v_WaterLv2_Amount\": " << v_WaterLv2_Amount << ",\n";
    file << "  \"v_WaterLv2_Range\": " << v_WaterLv2_Range << ",\n";
    file << "  \"v_WaterLv3_Amount\": " << v_WaterLv3_Amount << ",\n";
    file << "  \"v_WaterLv3_Range\": " << v_WaterLv3_Range << ",\n";
    file << "  \"v_StoneLv1_Amount\": " << v_StoneLv1_Amount << ",\n";
    file << "  \"v_StoneLv1_Range\": " << v_StoneLv1_Range << ",\n";
    file << "  \"v_StoneLv2_Amount\": " << v_StoneLv2_Amount << ",\n";
    file << "  \"v_StoneLv2_Range\": " << v_StoneLv2_Range << ",\n";
    file << "  \"v_StoneLv3_Amount\": " << v_StoneLv3_Amount << ",\n";
    file << "  \"v_StoneLv3_Range\": " << v_StoneLv3_Range << ",\n";
    file << "  \"v_City_Dur\": " << v_City_Dur << ",\n";
    file << "  \"v_City_Range\": " << v_City_Range << ",\n";
    file << "  \"v_City_Atk\": " << v_City_Atk << ",\n";
    file << "  \"v_City_Sight\": " << v_City_Sight << ",\n";
    file << "  \"v_Gate_Dur\": " << v_Gate_Dur << ",\n";
    file << "  \"v_Gate_Range\": " << v_Gate_Range << ",\n";
    file << "  \"v_Gate_Atk\": " << v_Gate_Atk << ",\n";
    file << "  \"v_Gate_Sight\": " << v_Gate_Sight << ",\n";
    file << "  \"v_Tower_Dur\": " << v_Tower_Dur << ",\n";
    file << "  \"v_Tower_Range\": " << v_Tower_Range << ",\n";
    file << "  \"v_Tower_Atk\": " << v_Tower_Atk << ",\n";
    file << "  \"v_Tower_Sight\": " << v_Tower_Sight << ",\n";
    file << "  \"v_WallCatapult_Dur\": " << v_WallCatapult_Dur << ",\n";
    file << "  \"v_WallCatapult_Range\": " << v_WallCatapult_Range << ",\n";
    file << "  \"v_WallCatapult_Atk\": " << v_WallCatapult_Atk << ",\n";
    file << "  \"v_WallCatapult_Sight\": " << v_WallCatapult_Sight << ",\n";
    file << "  \"v_Signal_Dur\": " << v_Signal_Dur << ",\n";
    file << "  \"v_Signal_Spirit\": " << v_Signal_Spirit << ",\n";
    file << "  \"v_Signal_Sight\": " << v_Signal_Sight << ",\n";
    file << "  \"iSiegeHealRate\": " << iSiegeHealRate << ",\n";

    // 지형 보너스 테이블 저장
    static const int terrainIndices[] = { 1, 2, 3, 4, 5, 6, 7, 10, 15, 16, 17 };
    for (int i = 1; i <= 12; i++) {
        for (int j = 0; j < 11; j++) {
            int tidx = terrainIndices[j];
            file << "  \"TB_" << i << "_" << tidx << "\": " << g_TerrainBonusTable[i][tidx] << ",\n";
        }
    }
    file << "  \"config_end\": true\n";
    file << "}";

    file.flush();
    const bool writeOk = file.good();
    file.close();
    if (!writeOk || file.fail())
      ReportConfigSaveFailure("기본 설정 파일 쓰기");
  }

  void LoadConfig() {
    std::ifstream file(GetConfigPath());
    if (!file.is_open())
      return;

    std::string line;
    while (std::getline(file, line)) {
      if (line.find("g_speedMultiplier") != std::string::npos) {
        size_t colonPos = line.find(":");
        if (colonPos != std::string::npos) {
          try {
            g_speedMultiplier = std::stof(line.substr(colonPos + 1));
          } catch (...) {
          }
        }
        continue;
      }
      if (line.find("g_notificationSpeed") != std::string::npos) {
        size_t colonPos = line.find(":");
        if (colonPos != std::string::npos) {
          try {
            g_notificationSpeed = std::stof(line.substr(colonPos + 1));
          } catch (...) {
          }
        }
        continue;
      }
      if (line.find("fDomesticsPlayer") != std::string::npos) {
        size_t colonPos = line.find(":");
        if (colonPos != std::string::npos) {
          try {
            fDomesticsPlayer = std::stof(line.substr(colonPos + 1));
            SetDomesticsMultiplier(fDomesticsPlayer, fDomesticsForce);
          } catch (...) {
          }
        }
        continue;
      }
      if (line.find("fDomesticsForce") != std::string::npos) {
        size_t colonPos = line.find(":");
        if (colonPos != std::string::npos) {
          try {
            fDomesticsForce = std::stof(line.substr(colonPos + 1));
            SetDomesticsMultiplier(fDomesticsPlayer, fDomesticsForce);
          } catch (...) {
          }
        }
        continue;
      }
      if (line.find("g_cityMaxGrainLimit") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { g_cityMaxGrainLimit = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("g_cityKeepGrain") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { g_cityKeepGrain = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("g_cityExchangeRate") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { g_cityExchangeRate = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("\"g_cityAutoExchangeEnabled\"") != std::string::npos) {
          g_cityAutoExchangeEnabled = (line.find("true") != std::string::npos);
          continue;
      }
      if (line.find("\"g_cityRevoltAlwaysZero\"") != std::string::npos) {
          g_cityRevoltAlwaysZero = (line.find("true") != std::string::npos);
          continue;
      }
      if (line.find("nSkipDays") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { nSkipDays = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("iSiegeHealRate") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { iSiegeHealRate = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }

      if (line.find("v_HealLv1_Amount") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_HealLv1_Amount = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_HealLv2_Amount") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_HealLv2_Amount = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_HealLv2_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_HealLv2_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_HealLv3_Amount") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_HealLv3_Amount = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_HealLv3_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_HealLv3_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_DongtoLv1_Prob") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_DongtoLv1_Prob = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_DongtoLv1_StateProb") != std::string::npos) {
        size_t colonPos = line.find(":");
        if (colonPos != std::string::npos) {
          try {
            v_DongtoLv1_StateProb = std::stoi(line.substr(colonPos + 1));
          } catch (...) {
          }
        }
        continue;
      }
      if (line.find("v_DongtoLv2_Prob") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_DongtoLv2_Prob = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_DongtoLv2_StateProb") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_DongtoLv2_StateProb = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_DongtoLv3_Prob") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_DongtoLv3_Prob = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_DongtoLv3_StateProb") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_DongtoLv3_StateProb = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_CelestiaLv1_Prob") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_CelestiaLv1_Prob = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_CelestiaLv1_Amount") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_CelestiaLv1_Amount = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_CelestiaLv1_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_CelestiaLv1_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_CelestiaLv2_Prob") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_CelestiaLv2_Prob = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_CelestiaLv2_Amount") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_CelestiaLv2_Amount = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_CelestiaLv2_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_CelestiaLv2_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_CelestiaLv3_Prob") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_CelestiaLv3_Prob = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_CelestiaLv3_Amount") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_CelestiaLv3_Amount = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_CelestiaLv3_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_CelestiaLv3_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Catapult_MinRange") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Catapult_MinRange = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Catapult_MaxRange") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Catapult_MaxRange = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Catapult_Amount") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Catapult_Amount = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Catapult_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Catapult_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_WaterLv1_Amount") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_WaterLv1_Amount = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_WaterLv1_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_WaterLv1_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_WaterLv2_Amount") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_WaterLv2_Amount = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_WaterLv2_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_WaterLv2_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_WaterLv3_Amount") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_WaterLv3_Amount = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_WaterLv3_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_WaterLv3_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_StoneLv1_Amount") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_StoneLv1_Amount = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_StoneLv1_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_StoneLv1_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_StoneLv2_Amount") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_StoneLv2_Amount = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_StoneLv2_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_StoneLv2_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_StoneLv3_Amount") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_StoneLv3_Amount = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_StoneLv3_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_StoneLv3_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_City_Dur") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_City_Dur = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_City_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_City_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_City_Atk") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_City_Atk = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_City_Sight") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_City_Sight = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Gate_Dur") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Gate_Dur = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Gate_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Gate_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Gate_Atk") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Gate_Atk = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Gate_Sight") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Gate_Sight = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Tower_Dur") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Tower_Dur = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Tower_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Tower_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Tower_Atk") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Tower_Atk = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Tower_Sight") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Tower_Sight = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_WallCatapult_Dur") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_WallCatapult_Dur = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_WallCatapult_Range") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_WallCatapult_Range = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_WallCatapult_Atk") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_WallCatapult_Atk = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_WallCatapult_Sight") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_WallCatapult_Sight = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Signal_Dur") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Signal_Dur = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Signal_Spirit") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Signal_Spirit = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }
      if (line.find("v_Signal_Sight") != std::string::npos) {
          size_t colonPos = line.find(":");
          if (colonPos != std::string::npos) { try { v_Signal_Sight = std::stoi(line.substr(colonPos + 1)); } catch (...) {} }
          continue;
      }

      // 지형 보너스 테이블 로드 (TB_병종_지형)
      if (line.find("\"TB_") != std::string::npos) {
          size_t firstUnderscore = line.find("_");
          size_t secondUnderscore = line.find("_", firstUnderscore + 1);
          size_t colonPos = line.find(":");
          if (firstUnderscore != std::string::npos && secondUnderscore != std::string::npos && colonPos != std::string::npos) {
              try {
                  int tr = std::stoi(line.substr(firstUnderscore + 1, secondUnderscore - firstUnderscore - 1));
                  int gr = std::stoi(line.substr(secondUnderscore + 1, colonPos - secondUnderscore - 1));
                  int val = std::stoi(line.substr(colonPos + 1));
                  if (tr >= 1 && tr <= 12 && gr >= 1 && gr <= 17) {
                      g_TerrainBonusTable[tr][gr] = (short)val;
                  }
              } catch (...) {}
          }
          continue;
      }

      for (auto &entry : g_Entries) {
        std::string quotedKey = "\"" + std::string(entry.key) + "\"";
        if (line.find(quotedKey) != std::string::npos) {
          *entry.flag = (line.find("true") != std::string::npos);
          break;
        }
      }
    }
    file.close();
    AddLog(u8"[Config] 설정 파일 로드 완료");
  }

  static void ResetP1DependentAppliedStates() {
    for (auto &entry : g_Entries) {
      if (!entry.requiresP1 || !entry.appliedState)
        continue;
      *entry.appliedState = false;
    }
    SetMonthCapture(false);
    s_appMonthCapture = false;
    ResetCorpsAutoDeploymentSession();
    AddLog(u8"[Config] P1 의존 적용 상태 초기화");
  }

  void ResetAppliedStates() {
    if (s_isReset)
      return; // 이미 리셋된 상태면 중복 실행 방지

    for (auto &entry : g_Entries) {
      if (entry.appliedState) {
        *entry.appliedState = false;
      }
    }
    s_firstGameBaseTime = 0;
    s_firstP1Time = 0;
    SetMonthCapture(false);
    s_appMonthCapture = false;
    ResetCorpsAutoDeploymentSession();
    AddLog(u8"[Config] 모든 적용 상태 초기화");
    s_isReset = true;
  }

  void ApplyStoredConfigs(uintptr_t p1, uintptr_t gameBase) {
    static uint64_t s_lastLoop = 0;
    uintptr_t now = GetTickCount64();

    if (gameBase == 0) {
      if (s_firstGameBaseTime != 0 || s_firstP1Time != 0) {
        ResetAppliedStates();
        s_firstGameBaseTime = 0;
        s_firstP1Time = 0;
      }
      return;
    }

    // 1000ms마다 한 번만 루프 체크 (CPU 부하 최소화)
    if (now - s_lastLoop < 1000)
      return;
    s_lastLoop = now;

    if (s_firstGameBaseTime == 0) {
      s_firstGameBaseTime = now;
      s_isReset = false;
      AddLog(u8"[Config] 게임 베이스 감지. 안정화 대기...");
      return;
    }

    const bool p1Ok = (p1 > 0x10000) && IsValidPtr(p1, 0x100);

    if (p1Ok) {
      if (s_firstP1Time == 0) {
        s_firstP1Time = now;
        s_isReset = false;
        AddLog(u8"[Config] 무장 데이터 감지됨. 안정화 대기 시작...");
      }
    } else {
      if (s_firstP1Time != 0) {
        ResetP1DependentAppliedStates();
        s_firstP1Time = 0;
      }
    }

    const bool gameBaseStable = (now - s_firstGameBaseTime >= 3000);
    const bool p1Stable = p1Ok && s_firstP1Time != 0 && (now - s_firstP1Time >= 3000);

    auto processEntries = [&](bool noP1Only) {
      for (auto &entry : g_Entries) {
        if (!entry.appliedState || !entry.applyFunc)
          continue;
        if (noP1Only) {
          if (entry.requiresP1)
            continue;
        } else {
          if (!entry.requiresP1)
            continue;
        }
        if (entry.isWar && !IsInBattle()) {
          continue;
        }

        if (*entry.flag && !*entry.appliedState) {
          *entry.appliedState = true;
          entry.applyFunc(true);
          AddLog(u8"[Config] 자동 활성화: %s", entry.displayName);
        } else if (!*entry.flag && *entry.appliedState) {
          *entry.appliedState = false;
          entry.applyFunc(false);
          AddLog(u8"[Config] 실시간 중지: %s", entry.displayName);
        }
      }
    };

    if (gameBaseStable)
      processEntries(true);

    if (!p1Stable)
      return;

    // 월 캡처 시작 (설정이 켜져 있을 때만)
    if (bMonthCapture && !s_appMonthCapture) {
      SetMonthCapture(true);
      InstallSystemMonthHook(); // 신규 AOB 방식 시스템 월 후킹
      s_appMonthCapture = true;
    }

    // 전기 발생 캡처 (상시 활성화, 지연 로딩)
    static bool s_appTengiCapture = false;
    if (!s_appTengiCapture) {
      SetTengiCapture(true);
      s_appTengiCapture = true;
    }

    processEntries(false);
  }

  bool IsConfigReady() {
    if (s_firstP1Time == 0)
      return false;
    return (GetTickCount64() - s_firstP1Time >= 3000);
  }
} // namespace DX11Base
