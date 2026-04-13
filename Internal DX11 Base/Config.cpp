#include "Config.h"
#include "BattleMonitor.h"
#include "Cheats.h"
#include "Cheats/MonthCapture.h"
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
#include "Cheats/BangmokCity.h"
#include "Cheats/Battleunitcapture.h"
#include "Cheats/Bigcityconvert.h"
#include "Cheats/Catapult.h"
#include "Cheats/Celestia.h"
#include "Cheats/Defatkboost.h"
#include "Cheats/Defbuildingboost.h"
#include "Cheats/Dongto.h"
#include "Cheats/FactionLordBonus.h"
#include "Cheats/Fastrelationship.h"
#include "Cheats/Infinitegift.h"
#include "Cheats/Infinitetalk.h"
#include "Cheats/InstantLoveCave.h"
#include "Cheats/Loyaltycave.h"
#include "Cheats/NonggyeongCity.h"
#include "Cheats/Resonancecave.h"
#include "Cheats/Roadblock.h"
#include "Cheats/SangeopCity.h"
#include "Cheats/Selfheal.h"
#include "Cheats/SkillCondition.h"
#include "Cheats/StartSetting.h"
#include "Cheats/SystemMonth.h"
#include "Cheats/TechZero.h"
#include "Cheats/Techpointcave.h"
#include "Cheats/TengiCave.h"
#include "Cheats/Terrainignore.h"
#include "Cheats/DomesticsMult.h"
#include "Cheats/SpeedHack.h"

namespace DX11Base {
  extern HMODULE g_hModule;
  extern bool marriageApplied;
  void SetInstantAttitude(bool enable);
  void SetMarriageCondition(bool enable);

  // 람다 대용 래퍼 함수들
  void ApplyLoveNormal(bool e) { SetInstantLoveCave(e, LoveMode::Normal); }
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
  static bool s_appLoveNormal = false;
  static bool s_appLoveHate = false;
  static bool s_appLoyalty = false;
  static bool s_appResonance = false;
  static bool s_appGift = false;
  static bool s_appTalk = false;
  static bool s_appRelation = false;
  static bool s_appMarriage = false;
  static bool s_appSelfHeal = false;
  static bool s_appDongto = false;
  static bool s_appTerrain = false;
  static bool s_appDefBuild = false;
  static bool s_appDefAtk = false;
  static bool s_appCatapult = false;
  static bool s_appCelestial = false;
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
      {"bBigCity", u8"대도시 전환", &bBigCity, &s_appBigCity, SetBigCityConvert, false, true},
      {"bBangmokCity", u8"방목도시 황폐화", &bBangmokCity, &s_appBangmokCity, SetBangmokCity, false, true},
      {"bNonggyeongCity", u8"농경도시 버프", &bNonggyeongCity, &s_appNonggyeongCity, SetNonggyeongCity, false, true},
      {"bSangeopCity", u8"상업도시 버프", &bSangeopCity, &s_appSangeopCity, SetSangeopCity, false, true},
      {"bAttitudeHack", u8"견문시 민심최대", &bAttitudeHack, &s_appAttitude, SetInstantAttitude, false, true},
      {"bLoveCave", u8"경애/의형제 조건 완화", &bLoveCave, &s_appLoveNormal, ApplyLoveNormal, false, true},
      {"bHateCave", u8"상극 무시/동지 조건 완화", &bHateCave, &s_appLoveHate, ApplyLoveHate, false, true},
      {"bLoyalty", u8"충성도 변경", &bLoyalty, &s_appLoyalty, SetInstantLoyalty, false, true},
      {"bResonance", u8"공명 변경", &bResonance, &s_appResonance, SetInstantResonance, false, true},
      {"bInfiniteGift", u8"증정 무한", &bInfiniteGift, &s_appGift, SetInfiniteGift, false, true},
      {"bInfiniteTalk", u8"담화 무한", &bInfiniteTalk, &s_appTalk, SetInfiniteTalk, false, true},
      {"bFastRelationship", u8"경애시 무조건 공명", &bFastRelationship, &s_appRelation, SetFastRelationship, false, true},
      {"marriageApplied", u8"결혼 무제한", &marriageApplied, &s_appMarriage, SetMarriageCondition, false, true},
      {"bSelfHeal", u8"전쟁: 자가 회복", &bSelfHeal, &s_appSelfHeal, SetSelfHeal, true, true},
      {"bDongto", u8"전쟁: 동토(금/군량 무한)", &bDongto, &s_appDongto, SetDongto, true, true},
      {"bTerrainIgnore", u8"전쟁: 격류낙석 지형 무시", &bTerrainIgnore, &s_appTerrain, SetTerrainIgnore, true, true},
      {"bDefBuilding", u8"전쟁: 방어건물 강화", &bDefBuilding, &s_appDefBuild, SetDefBuildingBoost, true, true},
      {"bDefAtk", u8"전쟁: 공격/방어 부스트", &bDefAtk, &s_appDefAtk, SetDefAtkBoost, true, true},
      {"bCatapult", u8"전쟁: 투석기 강화", &bCatapult, &s_appCatapult, SetCatapultCheat, true, true},
      {"bCelestial", u8"전쟁: 천계 강화", &bCelestial, &s_appCelestial, SetCelestialMod, true, true},
      {"bAllAggressive", u8"전쟁: 모든 무장 성향 적극", &bAllAggressive, nullptr, nullptr, false, true},
      {"bBattleUnit", u8"전쟁: 유닛 정보 캡처", &bBattleUnit, &s_appBattleUnit, SetBattleUnitCapture, false, true},
      {"bBattleMapShuffle", u8"기타: 평정 시 전투맵 셔플", &bBattleMapShuffle, nullptr, nullptr, false, true},
      {"bRoadBlock", u8"전쟁: 진로 방해 무시 (건녕↔교지)", &bRoadBlock, &s_appRoadBlock, SetRoadBlock, false, true},
      {"bRoadBlock2", u8"전쟁: 진로 방해 무시 (교지↔회계)", &bRoadBlock2, &s_appRoadBlock2, SetRoadBlock2, false, true},
      {"bStartSetting", u8"시나리오 수정", &bStartSetting, &s_appStartSetting, SetStartSetting, false, false},
      {"bTechZero", u8"모든 세력 기술 초기화", &bTechZero, &s_appTechZero, SetTechZero, false, false},
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
      {"bUndiscoveredToRonin", u8"모든 미발견 무장 재야로 변경", &bUndiscoveredToRonin, &s_appUndiscovered, SetUndiscoveredToRonin, false, false}};

  std::string GetConfigPath() {
    char path[MAX_PATH];
    GetModuleFileNameA(DX11Base::g_hModule, path, MAX_PATH);
    return std::filesystem::path(path).parent_path().append("S8RPK_cheat_config.json").string();
  }

  void SaveConfig() {
    std::ofstream file(GetConfigPath());
    if (!file.is_open())
      return;

    file << "{\n";
    for (size_t i = 0; i < (sizeof(g_Entries) / sizeof(g_Entries[0])); ++i) {
      file << "  \"" << g_Entries[i].key << "\": " << (*g_Entries[i].flag ? "true" : "false");
      file << ",\n";
    }
    file << "  \"g_speedMultiplier\": " << g_speedMultiplier << ",\n";
    file << "  \"g_notificationSpeed\": " << g_notificationSpeed << ",\n";
    file << "  \"fDomesticsPlayer\": " << fDomesticsPlayer << ",\n";
    file << "  \"fDomesticsForce\": " << fDomesticsForce << "\n";
    file << "}";
    file.close();
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

      for (auto &entry : g_Entries) {
        if (line.find(entry.key) != std::string::npos) {
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
