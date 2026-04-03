#include "Config.h"
#include "Cheats.h"
#include "MenuState.h"
#include "pch.h"
#include "showlog.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

// 치트 기능 헤더들
#include "Cheats/Battleunitcapture.h"
#include "Cheats/Bigcityconvert.h"
#include "Cheats/Catapult.h"
#include "Cheats/Celestia.h"
#include "Cheats/Defatkboost.h"
#include "Cheats/Defbuildingboost.h"
#include "Cheats/Dongto.h"
#include "Cheats/Fastrelationship.h"
#include "Cheats/Infinitegift.h"
#include "Cheats/Infinitetalk.h"
#include "Cheats/InstantLoveCave.h"
#include "Cheats/Loyaltycave.h"
#include "Cheats/Resonancecave.h"
#include "Cheats/Roadblock.h"
#include "Cheats/Selfheal.h"
#include "Cheats/Techpointcave.h"
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
    bool *flag;
    bool *appliedState; // 지연 적용 상태 추적용
    void (*applyFunc)(bool);
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
  static uint64_t s_firstP1Time = 0; // p1 감지 시점 기록용

  static ConfigEntry g_Entries[] = {{"bInfiniteAP", &bInfiniteAP, nullptr, nullptr},
                                    {"bFastJewel", &bFastJewel, nullptr, nullptr},
                                    {"bBigCity", &bBigCity, &s_appBigCity, SetBigCityConvert},
                                    {"bAttitudeHack", &bAttitudeHack, &s_appAttitude, SetInstantAttitude},
                                    {"bLoveCave", &bLoveCave, &s_appLoveNormal, ApplyLoveNormal},
                                    {"bHateCave", &bHateCave, &s_appLoveHate, ApplyLoveHate},
                                    {"bLoyalty", &bLoyalty, &s_appLoyalty, SetInstantLoyalty},
                                    {"bResonance", &bResonance, &s_appResonance, SetInstantResonance},
                                    {"bInfiniteGift", &bInfiniteGift, &s_appGift, SetInfiniteGift},
                                    {"bInfiniteTalk", &bInfiniteTalk, &s_appTalk, SetInfiniteTalk},
                                    {"bFastRelationship", &bFastRelationship, &s_appRelation, SetFastRelationship},
                                    {"marriageApplied", &marriageApplied, &s_appMarriage, SetMarriageCondition},
                                    {"bSelfHeal", &bSelfHeal, &s_appSelfHeal, SetSelfHeal},
                                    {"bDongto", &bDongto, &s_appDongto, SetDongto},
                                    {"bTerrainIgnore", &bTerrainIgnore, &s_appTerrain, SetTerrainIgnore},
                                    {"bDefBuilding", &bDefBuilding, &s_appDefBuild, SetDefBuildingBoost},
                                    {"bDefAtk", &bDefAtk, &s_appDefAtk, SetDefAtkBoost},
                                    {"bCatapult", &bCatapult, &s_appCatapult, SetCatapultCheat},
                                    {"bCelestial", &bCelestial, &s_appCelestial, SetCelestialMod},
                                    {"bBattleUnit", &bBattleUnit, &s_appBattleUnit, SetBattleUnitCapture},
                                    {"bRoadBlock", &bRoadBlock, &s_appRoadBlock, SetRoadBlock},
                                    {"bAutoLoadMenu", &bAutoLoadMenu, nullptr, nullptr},
                                    {"bZeroInfamy", &bZeroInfamy, nullptr, nullptr}};

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
      if (i < (sizeof(g_Entries) / sizeof(g_Entries[0])) - 1)
        file << ",";
      file << "\n";
    }
    file << "}";
    file.close();
  }

  void LoadConfig() {
    std::ifstream file(GetConfigPath());
    if (!file.is_open())
      return;

    std::string line;
    while (std::getline(file, line)) {
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
    for (auto &entry : g_Entries) {
      if (entry.appliedState) {
        *entry.appliedState = false;
      }
    }
    s_firstP1Time = 0;
    AddLog(u8"[Config] 모든 적용 상태 초기화 (메인 메뉴)");
  }

  void ApplyStoredConfigs(uintptr_t p1, uintptr_t gameBase) {
    // gameBase와 p1이 모두 유효할 때만 적용 시도
    if (gameBase == 0 || p1 == 0) {
      if (s_firstP1Time != 0)
        ResetAppliedStates(); // 주소가 사라지면 리셋
      return;
    }

    // 처음 감지된 순간 시간 기록
    if (s_firstP1Time == 0) {
      s_firstP1Time = GetTickCount64();
      AddLog(u8"[Config] 무장 데이터 감지됨. 안정화를 위해 3초 대기...");
      return;
    }

    // 안전 지연 시간 (3초) 체크: 로딩 중 성급한 접근 방지
    if (GetTickCount64() - s_firstP1Time < 3000)
      return;

    // 추가적인 안전장치: p1이 가리키는 메모리가 최소한의 유효성을 가지는지 확인
    if (!IsValidPtr(p1, 0x100))
      return;

    // [추가 검증] 금(Gold)이나 다른 데이터가 0이 아닌 유의미한 상태인지 확인 (선택 사항)
    // if (*(unsigned int*)(p1 + 0xE8) == 0) return;

    for (auto &entry : g_Entries) {
      if (*entry.flag && entry.appliedState && !*entry.appliedState) {
        if (entry.applyFunc) {
          *entry.appliedState = true;
          entry.applyFunc(true);
          AddLog(u8"[Config] 자동 활성화: %s", entry.key);

          // [핵심] 한꺼번에 수많은 스레드가 생성되는 것을 방지하기 위해 약간의 지연시간 추가
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
      }
    }
  }
} // namespace DX11Base
