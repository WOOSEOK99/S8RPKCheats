#include "MenuState.h"
#include "Engine.h"
#include "pch.h"
#include "debug.h"

// 글로벌 변수 정의 (Flags)
ImGuiWindowFlags Flags = ImGuiWindowFlags_AlwaysAutoResize;

namespace DX11Base {
  // 1. 내정/자원 초기화
  int v_Gold = 30000, v_AP = 200, v_Token = 0;
  int v_SP = 255, v_Merit = 50000, v_Priv = 1;
  int v_RepM = 1000, v_RepL = 1000, v_RepI = 0;
  int v_Brave = 600;
  int v_JewelTime = 0;

  bool bInfiniteAP = false;
  bool bFastJewel = false;
  bool bAutoFillSpecialties = false;
  bool bInfiniteTavernRequests = false;
  bool bBigCity = false;
  bool bBangmokCity = false;
  bool bNonggyeongCity = false;
  bool bSangeopCity = false;
  bool bAttitudeHack = false;
  bool bDomestics = false;
  float fDomesticsPlayer = 2.0f;
  float fDomesticsForce = 1.25f;

  // 2. 인연/관계 초기화
  bool bLoveCave = false;
  bool bHateCave = false;
  bool bLoyalty = false;
  bool bResonance = false;
  bool bResonanceFour = false;
  bool bInfiniteGift = false;
  bool bInfiniteTalk = false;
  bool bFastRelationship = false;
  bool bShowChildManagerWin = false;

  // 3. 전쟁/전투 초기화
  bool bSelfHeal = false;
  bool bDongto = false;
  bool bTerrainIgnore = false;
  bool bDefBuilding = false;
  bool bCatapult = false;
  bool bCelestial = false;
  bool bSiegeWarfare = false;
  bool bAllAggressive = false;
  bool bBattleUnit = false;
  bool bBattleMapShuffle = false;

  // 전투 환경 / 조건 설정
  bool bWeatherSkillSimple = false;
  bool bWeatherSkillComplex = false;
  bool bDateAlways15 = false;
  bool bDateDynamic = false;
  bool bSkipDaysEnabled = false;
  int nSkipDays = 0;
  bool bTerrainAbilityAtkDef = false;
  bool bTerrainAbilityAll = false;
  bool bSiegeWarfare2 = false;
  int iSiegeHealRate = 10;
  bool bShowBattleEnvWin = false;

  // 특수 능력 (SpecialAbility)
  bool bGunakdae        = false; // 군악대
  bool bMusangBomyeong  = false; // 무쌍 보명
  bool bFlameKnight     = false; // 불꽃기병
  bool bRangedArcher    = false; // 원격 궁병
  bool bRattanArmor     = false; // 등갑군
  bool bGeneralissimo   = false; // 총사령관
  bool bAmbushUnit      = false; // 기습부대
  bool bGrandStrategist = false; // 대군사

  // 4. 기타 UI 상태 초기화 (DX11Base 네임스페이스)
  std::string currentLabelValue = ""; // (사용되지 않을 수도 있음)
  std::string currentLabel = "";
  int *pSelectedVar = nullptr;

  bool bTechZero = false;
  bool bRoadBlock = false;
  bool bRoadBlock2 = false;
  bool bStartSetting = false;
  bool bMonitorRonin = false;
  bool bUndiscoveredToRonin = false;
  bool bAutoStatUp99 = false;
  bool bInfTengi = false;            // 2026-04-05 무한 전기
  bool bCancelCastleEvent = false;   // 2026-04-05 중지 성성 취소
  bool bSkillCondition = false;      // 만병 습득 조건 해제
  bool bYumokCondition = false;      // 유목기병 습득 조건 해제
  bool bSangbyeongCondition = false; // 상병 습득 조건 해제
  bool bFactionLordBonus = false;    // 세력 군주 보너스 자동 배정
  bool bCancelTengi = false;         // 2026-04-05 전기발생 취소
  bool bShowCityInfoWin = false;
  bool bShowSelectedOfficerWin = false;
  bool bShowOfficerListWin = false;
  bool bShowSpouseListWin = false;
  bool bShowSpecialtyInfoWin = false;
  bool bTraitViewer = true;          // 기재 화면 보이기: 기본 ON
  bool bShowMemoryNotepadWin = false;
  bool bOfficerCapture = false;
  uintptr_t g_capturedOfficerBase = 0;
  uintptr_t g_officerInlineReadPtr = 0;
  bool bAllowGameClick = false;
  bool bBlockClickInOfficerList = true;
  bool bBlockClickInMemoryEditor = true;
  bool bIsMenuCollapsed = false;
  bool bAutoLoadMenu = false;

  bool bShowWidgetTengi = false;
  // bool bShowWidgetNotif = false; // Already defined in NotificationManager.cpp
  bool bShowWidgetHero = false;
  bool bShowWidgetAllOfficers = false;

  bool bForceCenterOfficerDetail = false;
  bool bForceCenterSelectedOfficer = false;
  bool bForceCenterOfficerList = false;

  // 기타 특수 토글
  bool bZeroInfamy = false;

  // UI 렌더링 루프 상단 어딘가
  bool bSelectOfficerFirstInit = true;

  bool bToggleMenuCollapseRequest = false;
  bool bShowPasswordPopup = false;
  // bool bShowNotificationLog = false; // Already defined in NotificationManager.cpp
  bool bShowTacticsEditWin = false;
  bool bShowBatchOfficerEditWin = false;
  bool bShowFactionTechEditor = false;

  BatchUnitSetting g_batchTraits[24] = {};
  BatchUnitSetting g_batchTactics[35] = {};

  // 전법 수정 세부 설정 (치료)
  bool bHealLv1_Self = true;
  int v_HealLv1_Amount = 2000;
  bool bHealLv2_Self = true;
  int v_HealLv2_Range = 9;
  int v_HealLv2_Amount = 3500;
  bool bHealLv3_Self = true;
  int v_HealLv3_Range = 11;
  int v_HealLv3_Amount = 7000;

  // 전법 수정 세부 설정 (동토)
  int v_DongtoLv1_Prob = 100;
  int v_DongtoLv1_StateProb = 20;
  int v_DongtoLv2_Prob = 100;
  int v_DongtoLv2_StateProb = 50;
  int v_DongtoLv3_Prob = 100;
  int v_DongtoLv3_StateProb = 100;

  // 전법 수정 세부 설정 (천계)
  int v_CelestiaLv1_Prob = 100;
  int v_CelestiaLv1_Amount = 2000;
  int v_CelestiaLv1_Range = 11;
  int v_CelestiaLv2_Prob = 100;
  int v_CelestiaLv2_Amount = 3500;
  int v_CelestiaLv2_Range = 11;
  int v_CelestiaLv3_Prob = 100;
  int v_CelestiaLv3_Amount = 7000;
  int v_CelestiaLv3_Range = 11;

  // 전법 수정 세부 설정 (투석기)
  int v_Catapult_MinRange = 5;
  int v_Catapult_MaxRange = 5;
  int v_Catapult_Amount = 100;
  int v_Catapult_Range = 11;

  // 전법 수정 세부 설정 (격류/낙석) default values
  int v_WaterLv1_Amount = 60, v_WaterLv1_Range = 5;
  int v_WaterLv2_Amount = 70, v_WaterLv2_Range = 5;
  int v_WaterLv3_Amount = 85, v_WaterLv3_Range = 5;
  int v_StoneLv1_Amount = 60, v_StoneLv1_Range = 4;
  int v_StoneLv2_Amount = 70, v_StoneLv2_Range = 4;
  int v_StoneLv3_Amount = 85, v_StoneLv3_Range = 4;

  // 방어 건물강화 세부 설정 default values
  int v_City_Dur = 6000, v_City_Range = 4, v_City_Atk = 16, v_City_Sight = 6;
  int v_Gate_Dur = 4000, v_Gate_Range = 4, v_Gate_Atk = 16, v_Gate_Sight = 4;
  int v_Tower_Dur = 1400, v_Tower_Range = 4, v_Tower_Atk = 12, v_Tower_Sight = 4;
  int v_WallCatapult_Dur = 2000, v_WallCatapult_Range = 5, v_WallCatapult_Atk = 20, v_WallCatapult_Sight = 5;
  int v_Signal_Dur = 1400, v_Signal_Spirit = 10, v_Signal_Sight = 4;

  uintptr_t g_savedHeroAddr = 0;
  bool bFileLog = false;

  // 지형 보너스 테이블 초기값 (Lua 원본 스크립트 기반)
  short g_TerrainBonusTable[13][18] = {
      {0}, // 0 (사용 안 함)
      {0, 0, 0, 10, 0, 10, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 10}, // 1: 경보병
      {0, 0, 0, 10, 0, 10, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 10}, // 2: 중보병
      {0, 0, 0, 10, 0, 10, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 10}, // 3: 정예보병
      {0, 0, 0, 10, 0, 20, 10, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 10}, // 4: 만병
      {0, 10, 0, 0, 10, -10, -20, -20, 0, 0, 0, 0, 0, 0, 0, 10, 0, -10}, // 5: 경기병
      {0, 10, 0, 0, 10, -10, -20, -20, 0, 0, 0, 0, 0, 0, 0, 10, 0, -10}, // 6: 중기병
      {0, 10, 0, 0, 10, -10, -20, -20, 0, 0, 0, 0, 0, 0, 0, 10, 0, -10}, // 7: 정예기병
      {0, 10, 10, 0, 10, -10, -20, -20, 0, 0, 0, 0, 0, 0, 0, 10, 0, -10}, // 8: 유목기병
      {0, 0, 0, 0, 0, -10, -10, 10, 0, 0, 20, 0, 0, 0, 0, 0, 0, 10}, // 9: 궁병
      {0, 0, 0, 0, 0, -10, -10, 10, 0, 0, 20, 0, 0, 0, 0, 0, 0, 10}, // 10: 노병
      {0, 0, 0, 0, 0, -10, -10, 10, 0, 0, 20, 0, 0, 0, 0, 0, 10}, // 11: 정예궁병
      {0, 0, 0, 0, 0, 0, -10, 20, 0, 0, 20, 0, 0, 0, 0, 0, 0, 10}  // 12: 연노병
  };
  bool bShowTerrainBonusWin = false;

  void *g_RangeTextures[12] = {nullptr};

  bool IsAnyUIOpen() {
    return (g_Engine && g_Engine->bShowMenu) ||
           bShowOfficerDetail ||
           bShowSelectedOfficerWin ||
           bShowOfficerListWin ||
           bShowSpouseListWin ||
           bShowSpecialtyInfoWin ||
           bShowCityInfoWin ||
           bShowBatchOfficerEditWin ||
           bShowFactionTechEditor ||
           bShowMemoryNotepadWin ||
           bShowNotificationLog ||
           bShowTacticsEditWin ||
           bShowPasswordPopup ||
           bShowTerrainBonusWin ||
           bShowBattleEnvWin ||
           bShowChildManagerWin ||
           bShowDebug ||
           bShowMemoryEditor;
  }
} // namespace DX11Base
