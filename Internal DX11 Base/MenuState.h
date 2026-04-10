#pragma once
#define SAM8_CHEAT_VERSION "V0.72"
#include "Framework/imgui.h"
#include <string>
#include <vector>

// Flags를 글로벌 네임스페이스에 선언 (OfficerDetail.cpp 등과의 호환성)
extern ImGuiWindowFlags Flags;

namespace DX11Base {
  // 섹션별 상태 변수들
  // 1. 내정/자원
  extern int v_Gold, v_AP, v_Token;
  extern int v_SP, v_Merit, v_Priv;
  extern int v_RepM, v_RepL, v_RepI;
  extern int v_Brave;
  extern int v_JewelTime;

  extern bool bInfiniteAP;
  extern bool bFastJewel;
  extern bool bBigCity;
  extern bool bBangmokCity;
  extern bool bNonggyeongCity;
  extern bool bSangeopCity;
  extern bool bAttitudeHack;
  extern bool bZeroInfamy;
  extern bool bSelectOfficerFirstInit;
  extern bool bDomestics;          // 내정 배율
  extern float fDomesticsPlayer;   // 플레이어 배율
  extern float fDomesticsForce;    // 세력 배율

  // 2. 인연/관계
  extern bool bLoveCave;
  extern bool bHateCave;
  extern bool bLoyalty;
  extern bool bResonance;
  extern bool bInfiniteGift;
  extern bool bInfiniteTalk;
  extern bool bFastRelationship;

  // 3. 전쟁/전투
  extern bool bSelfHeal;
  extern bool bDongto;
  extern bool bTerrainIgnore;
  extern bool bDefBuilding;
  extern bool bDefAtk;
  extern bool bCatapult;
  extern bool bCelestial;
  extern bool bBattleUnit;

  // 4. 기타 UI 상태
  extern std::string currentLabel;
  extern int *pSelectedVar;

  // bShowOfficerDetail은 OfficerDetail.cpp에 이미 정의되어 있으므로 extern으로만 선언
  extern bool bShowOfficerDetail;
  extern uintptr_t g_capturedOfficerBase;
  // 선택 무장 상세 UI: 인라인 필드(0x3D0) 표시용 스냅샷 주소. 0이면 게임 메모리에서 직접 읽음.
  extern uintptr_t g_officerInlineReadPtr;

  extern bool bBattleUnit;
  extern bool bBattleMapShuffle;
  extern bool bTechZero;
  extern bool bRoadBlock;
  extern bool bRoadBlock2;
  extern bool bStartSetting;
  extern bool bMonitorRonin;
  extern bool bAutoStatUp99;        // 능력치 99 -> 100 자동 보정
  extern bool bInfTengi;            // 2026-04-05 무한 전기
  extern bool bCancelCastleEvent;   // 2026-04-05 중지 성성 취소
  extern bool bSkillCondition;      // 만병 습득 조건 해제
  extern bool bYumokCondition;      // 유목기병 습득 조건 해제
  extern bool bSangbyeongCondition; // 상병 습득 조건 해제
  extern bool bFactionLordBonus;    // 세력 군주 보너스 자동 배정
  extern bool bSpeedHack;           // 2026-04-04 배속
  extern float g_speedMultiplier;   // 2026-04-04 배속 배율 (0.1x ~ 5.0x)

  extern bool bShowSelectedOfficerWin;
  extern bool bShowOfficerListWin;
  extern bool bShowSpouseListWin;
  extern bool bShowSpecialtyInfoWin;
  extern bool bShowMemoryNotepadWin;
  extern bool bOfficerCapture;
  extern uintptr_t g_capturedOfficerBase;
  extern bool bAllowGameClick;           // 게임 화면 클릭 허용 여부
  extern bool bBlockClickInOfficerList;  // 모든 장수 편집 리스트용 클릭 차단 여부
  extern bool bBlockClickInMemoryEditor; // 메모리 에디터용 클릭 차단 여부
  extern bool bIsMenuCollapsed;          // 메인 메뉴 접힘 여부
  extern bool bAutoLoadMenu;

  extern bool bShowWidgetTengi;       // 위젯: 전기발생 취소 표시 여부
  extern bool bShowWidgetHero;        // 위젯: 주인공 표시 여부
  extern bool bShowWidgetAllOfficers; // 위젯: 모든무장 표시 여부
  extern bool bShowWidgetNotif;       // 위젯: 알림확인 표시 여부

  extern bool bForceCenterOfficerDetail;   // 창 강제 중앙 배치 요청 (주인공)
  extern bool bForceCenterSelectedOfficer; // 창 강제 중앙 배치 요청 (선택무장)
  extern bool bForceCenterOfficerList;     // 창 강제 중앙 배치 요청 (리스트)

  extern bool bToggleMenuCollapseRequest; // 틸트 키로 접기/펴기 요청
  extern bool bShowPasswordPopup;         // 디버그 비밀번호 창 표시 여부
  extern bool bShowNotificationLog;       // 알림 기록 창 표시 여부

  extern uintptr_t g_savedHeroAddr;       // [신규] 데모플레이 대비 주인공 주소 백업용

  bool IsAnyUIOpen(); // 모든 UI 창 활성화 여부 확인 함수
} // namespace DX11Base
