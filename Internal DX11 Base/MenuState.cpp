#include "MenuState.h"
#include "pch.h"

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
  bool bInfiniteGift = false;
  bool bInfiniteTalk = false;
  bool bFastRelationship = false;

  // 3. 전쟁/전투 초기화
  bool bSelfHeal = false;
  bool bDongto = false;
  bool bTerrainIgnore = false;
  bool bDefBuilding = false;
  bool bDefAtk = false;
  bool bCatapult = false;
  bool bCelestial = false;
  bool bAllAggressive = false;
  bool bBattleUnit = false;
  bool bBattleMapShuffle = false;

  // 4. 기타 UI 상태 초기화 (DX11Base 네임스페이스)
  std::string currentLabelValue = ""; // (사용되지 않을 수도 있음)
  std::string currentLabel = "";
  int *pSelectedVar = nullptr;

  bool bTechZero = false;
  bool bRoadBlock = false;
  bool bRoadBlock2 = false;
  bool bStartSetting = false;
  bool bMonitorRonin = false;
  bool bAutoStatUp99 = false;
  bool bInfTengi = false;         // 2026-04-05 무한 전기
  bool bCancelCastleEvent = false;// 2026-04-05 중지 성성 취소
  bool bSkillCondition = false;   // 만병 습득 조건 해제
  bool bYumokCondition = false;   // 유목기병 습득 조건 해제
  bool bSangbyeongCondition = false; // 상병 습득 조건 해제
  bool bFactionLordBonus = false; // 세력 군주 보너스 자동 배정
  bool bCancelTengi = false;      // 2026-04-05 전기발생 취소
  bool bSpeedHack = false;       // 2026-04-04 배속
  float g_speedMultiplier = 2.0f; // 2026-04-04 기본 2배속
  bool bShowSelectedOfficerWin = false;
  bool bShowOfficerListWin = false;
  bool bShowSpouseListWin = false;
  bool bShowSpecialtyInfoWin = false;
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

  uintptr_t g_savedHeroAddr = 0;
} // namespace DX11Base
