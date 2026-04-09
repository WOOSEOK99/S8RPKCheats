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
#include "Cheats/Resonancecave.h"
#include "Cheats/Roadblock.h"
#include "Cheats/Selfheal.h"
#include "Cheats/SkillCondition.h"
#include "Cheats/StartSetting.h"
#include "Cheats/SystemMonth.h"
#include "Cheats/Techpointcave.h"
#include "Cheats/TengiCave.h"
#include "Cheats/Terrainignore.h"


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
    bool isWar; // 전쟁 관련 기능 여부 (지연 활성화용)
  };

  // 적용 상태 플래그들
  static bool s_appBigCity = false;
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
  static bool s_appSkillCond = false;
  static bool s_appYumokCond = false;
  static bool s_appSangbyeongCond = false;
  static bool s_appFactionLordBonus = false;
  static uint64_t s_firstP1Time = 0; // p1 감지 시점 기록용
  static bool s_isReset = true;      // 리셋 완료 상태 기록

  static ConfigEntry g_Entries[] = {
      {"bInfiniteAP", u8"행동력 무한", &bInfiniteAP, nullptr, nullptr, false},
      {"bFastJewel", u8"보주교체 무제한", &bFastJewel, nullptr, nullptr, false},
      {"bBigCity", u8"대도시 전환", &bBigCity, &s_appBigCity, SetBigCityConvert, false},
      {"bAttitudeHack", u8"견문시 민심최대", &bAttitudeHack, &s_appAttitude, SetInstantAttitude, false},
      {"bLoveCave", u8"경애/의형제 조건 완화", &bLoveCave, &s_appLoveNormal, ApplyLoveNormal, false},
      {"bHateCave", u8"상극 무시/동지 조건 완화", &bHateCave, &s_appLoveHate, ApplyLoveHate, false},
      {"bLoyalty", u8"충성도 변경", &bLoyalty, &s_appLoyalty, SetInstantLoyalty, false},
      {"bResonance", u8"공명 변경", &bResonance, &s_appResonance, SetInstantResonance, false},
      {"bInfiniteGift", u8"증정 무한", &bInfiniteGift, &s_appGift, SetInfiniteGift, false},
      {"bInfiniteTalk", u8"담화 무한", &bInfiniteTalk, &s_appTalk, SetInfiniteTalk, false},
      {"bFastRelationship", u8"경애시 무조건 공명", &bFastRelationship, &s_appRelation, SetFastRelationship, false},
      {"marriageApplied", u8"결혼 무제한", &marriageApplied, &s_appMarriage, SetMarriageCondition, false},
      {"bSelfHeal", u8"전쟁: 자가 회복", &bSelfHeal, &s_appSelfHeal, SetSelfHeal, true},
      {"bDongto", u8"전쟁: 동토(금/군량 무한)", &bDongto, &s_appDongto, SetDongto, true},
      {"bTerrainIgnore", u8"전쟁: 격류낙석 지형 무시", &bTerrainIgnore, &s_appTerrain, SetTerrainIgnore, true},
      {"bDefBuilding", u8"전쟁: 방어건물 강화", &bDefBuilding, &s_appDefBuild, SetDefBuildingBoost, true},
      {"bDefAtk", u8"전쟁: 공격/방어 부스트", &bDefAtk, &s_appDefAtk, SetDefAtkBoost, true},
      {"bCatapult", u8"전쟁: 투석기 강화", &bCatapult, &s_appCatapult, SetCatapultCheat, true},
      {"bCelestial", u8"전쟁: 천계 강화", &bCelestial, &s_appCelestial, SetCelestialMod, true},
      {"bBattleUnit", u8"전쟁: 유닛 정보 캡처", &bBattleUnit, &s_appBattleUnit, SetBattleUnitCapture, false},
      {"bBattleMapShuffle", u8"기타: 평정 시 전투맵 셔플", &bBattleMapShuffle, nullptr, nullptr, false},
      {"bRoadBlock", u8"전쟁: 진로 방해 무시 (건녕↔교지)", &bRoadBlock, &s_appRoadBlock, SetRoadBlock, false},
      {"bRoadBlock2", u8"전쟁: 진로 방해 무시 (교지↔회계)", &bRoadBlock2, &s_appRoadBlock2, SetRoadBlock2, false},
      {"bStartSetting", u8"시나리오 수정", &bStartSetting, &s_appStartSetting, SetStartSetting, false},
      {"bSkillCondition", u8"만병 습득 조건 해제", &bSkillCondition, &s_appSkillCond, ApplySkillCondition, false},
      {"bYumokCondition", u8"유목기병 습득 조건 해제", &bYumokCondition, &s_appYumokCond, ApplyYumokCondition, false},
      {"bSangbyeongCondition", u8"상병 습득 조건 해제", &bSangbyeongCondition, &s_appSangbyeongCond,
       ApplySangbyeongCondition, false},
      {"bFactionLordBonus", u8"세력 군주 보너스 자동 배정", &bFactionLordBonus, &s_appFactionLordBonus,
       SetFactionLordBonus, false},
      {"bMonitorRonin", u8"재야장수 감시", &bMonitorRonin, nullptr, nullptr, false},
      {"bAutoStatUp99", u8"능력치 한계돌파", &bAutoStatUp99, nullptr, nullptr, false},
      {"bAutoLoadMenu", u8"시작 시 설정 로드", &bAutoLoadMenu, nullptr, nullptr, false},
      {"bZeroInfamy", u8"매턴 악명 0", &bZeroInfamy, nullptr, nullptr, false},
      {"bSpeedHack", u8"배속 기능", &bSpeedHack, nullptr, nullptr, false},
      {"bCancelCastleEvent", u8"중지 성성 취소", &bCancelCastleEvent, nullptr, nullptr, false},
      {"bShowWidgetTengi", u8"위젯: 전기발생 취소", &bShowWidgetTengi, nullptr, nullptr, false},
      {"bShowWidgetNotif", u8"위젯: 알림확인", &bShowWidgetNotif, nullptr, nullptr, false},
      {"bShowWidgetHero", u8"위젯: 주인공", &bShowWidgetHero, nullptr, nullptr, false},
      {"bShowWidgetAllOfficers", u8"위젯: 모든무장", &bShowWidgetAllOfficers, nullptr, nullptr, false}};

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
    file << "  \"g_notificationSpeed\": " << g_notificationSpeed << "\n";
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

  void ResetAppliedStates() {
    if (s_isReset)
      return; // 이미 리셋된 상태면 중복 실행 방지

    for (auto &entry : g_Entries) {
      if (entry.appliedState) {
        *entry.appliedState = false;
      }
    }
    s_firstP1Time = 0;
    SetMonthCapture(false);
    s_appMonthCapture = false;
    AddLog(u8"[Config] 모든 적용 상태 초기화");
    s_isReset = true;
  }

  void ApplyStoredConfigs(uintptr_t p1, uintptr_t gameBase) {
    static uint64_t s_lastLoop = 0;
    uintptr_t now = GetTickCount64();

    // gameBase와 p1이 모두 유효할 때만 적용 시도
    if (gameBase == 0 || p1 == 0) {
      if (s_firstP1Time != 0)
        ResetAppliedStates();
      return;
    }

    // 1000ms마다 한 번만 루프 체크 (CPU 부하 최소화)
    if (now - s_lastLoop < 1000)
      return;
    s_lastLoop = now;

    // 처음 감지된 순간 시간 기록
    if (s_firstP1Time == 0) {
      s_firstP1Time = now;
      s_isReset = false; // 이제 데이터가 들어왔으므로 나중에 0이 되면 리셋 가능하게 함
      AddLog(u8"[Config] 무장 데이터 감지됨. 안정화 대기 시작...");
      return;
    }

    // 안전 지연 시간 (3초) 체크: 로딩 중 성급한 접근 방지
    if (now - s_firstP1Time < 3000)
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

    // 추가적인 안전장치: p1이 가리키는 메모리가 최소한의 유효성을 가지는지 확인
    if (!IsValidPtr(p1, 0x100))
      return;

    // [안정화] 3초 대기 후 설정 적용 및 실시간 중지 처리
    for (auto &entry : g_Entries) {
      if (entry.appliedState && entry.applyFunc) {
        // [수정] 전쟁 관련 기능은 '전투 중이 아닐 때'만 지연 활성화함
        if (entry.isWar && !IsInBattle()) {
          continue;
        }

        // 1. 켜기 (flag == true && appliedState == false)
        if (*entry.flag && !*entry.appliedState) {
          *entry.appliedState = true;
          entry.applyFunc(true);
          AddLog(u8"[Config] 자동 활성화: %s", entry.displayName);
        }
        // 2. 끄기 (flag == false && appliedState == true)
        else if (!*entry.flag && *entry.appliedState) {
          *entry.appliedState = false;
          entry.applyFunc(false);
          AddLog(u8"[Config] 실시간 중지: %s", entry.displayName);
        }
      }
    }
  }

  bool IsConfigReady() {
    if (s_firstP1Time == 0)
      return false;
    return (GetTickCount64() - s_firstP1Time >= 3000);
  }
} // namespace DX11Base
