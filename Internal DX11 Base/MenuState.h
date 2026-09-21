#pragma once
#define SAM8_CHEAT_VERSION "V0.810"
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
  extern bool bAutoFillSpecialties;    // 평정 종료 시 명품 자동 배분
  extern bool bInfiniteTavernRequests; // 청부 무한 유지
  extern bool bBigCity;
  extern bool bBangmokCity;
  extern bool bNonggyeongCity;
  extern bool bSangeopCity;
  extern bool bAttitudeHack;
  extern bool bInfiniteBanquet; // 연회 사용 플래그 bit1 지속 해제
  extern bool bZeroInfamy;
  extern bool bSelectOfficerFirstInit;
  extern bool bDomestics;        // 내정 배율
  extern float fDomesticsPlayer; // 플레이어 배율
  extern float fDomesticsForce;  // 세력 배율

  // 2. 인연/관계
  extern bool bLoveCave;
  extern bool bHateCave;
  extern bool bLoyalty;
  extern bool bResonance;
  extern bool bInfiniteGift;
  extern bool bInfiniteTalk;
  extern bool bInfiniteDuel;
  extern bool bInfiniteDebate;
  extern bool bInfiniteMediation; // 중개 사용 완료 +0x71E5 bit6 지속 해제
  extern bool bFastRelationship;
  extern bool bShowChildManagerWin;

  // 3. 전쟁/전투
  extern bool bSelfHeal;
  extern bool bDongto;
  extern bool bTerrainIgnore;
  extern bool bDefBuilding;
  extern bool bCatapult;
  extern bool bCelestial;
  extern bool bSiegeWarfare;
  extern bool bAllAggressive;
  extern bool bBattleUnit;
  extern bool bBattleMapShuffle;
  extern bool bShortBattleCooldownEnabled;
  extern int iShortBattleCooldownDays;
  extern bool bTroopCountCombatScaling;
  extern bool bGovernorPrisonerDisposal;
  extern bool bGovernorPrisonerConsumePrivilege;
  extern bool bReinforcementArrivalAction;
  extern bool bReinforcementDefenderPlacement;

  // 전투 환경 / 조건 설정
  extern bool bWeatherSkillSimple;
  extern bool bWeatherSkillComplex;
  extern bool bDateAlways15;
  extern bool bDateDynamic;
  extern bool bSkipDaysEnabled;
  extern int nSkipDays;
  extern bool bTerrainAbilityAtkDef;
  extern bool bTerrainAbilityAll;
  extern bool bSiegeWarfare2;
  extern int iSiegeHealRate;
  extern bool bShowBattleEnvWin;

  // 특수 능력 (SpecialAbility)
  extern bool bGunakdae;        // 군악대
  extern bool bMusangBomyeong;  // 무쌍 보명
  extern bool bFlameKnight;     // 불꽃기병
  extern bool bRangedArcher;    // 원격 궁병
  extern bool bRattanArmor;     // 등갑군
  extern bool bGeneralissimo;   // 총사령관
  extern bool bAmbushUnit;      // 기습부대
  extern bool bGrandStrategist; // 대군사

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
  extern bool bUndiscoveredToRonin;
  extern bool bAutoStatUp99;        // 능력치 99 -> 100 자동 보정
  extern bool bInfTengi;            // 2026-04-05 무한 전기
  extern bool bTotalWarCycleShortening; // 결전 재발생 대기 주기 단축 (기본 ON)
  extern bool bCancelCastleEvent;   // 2026-04-05 중지 성성 취소
  extern bool bSkillCondition;      // 만병 습득 조건 해제
  extern bool bYumokCondition;      // 유목기병 습득 조건 해제
  extern bool bSangbyeongCondition; // 상병 습득 조건 해제
  extern bool bFactionLordBonus;    // 세력 군주 보너스 자동 배정

  extern bool bShowCityInfoWin; // 도시 정보 창
  extern bool bShowSelectedOfficerWin;
  extern bool bShowOfficerListWin;
  extern bool bShowSpouseListWin;
  extern bool bShowSpecialtyInfoWin;
  extern bool bTraitViewer; // 기재 화면 보이기 (기본 ON)
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
  extern bool bShowTacticsEditWin;        // 전법 수정 창 표시 여부
  extern bool bShowBatchOfficerEditWin;   // 모든 무장 일괄 편집 창 표시 여부
  extern bool bShowFactionTechEditor;     // 세력별 기술력 편집 창 표시 여부

  struct BatchUnitSetting {
    bool enabled = false;
    int level = 3;
  };
  extern BatchUnitSetting g_batchTraits[24];
  extern BatchUnitSetting g_batchTactics[35];

  // 전법 수정 세부 설정 (치료)
  extern bool bHealLv1_Self;
  extern int v_HealLv1_Amount;
  extern bool bHealLv2_Self;
  extern int v_HealLv2_Range;
  extern int v_HealLv2_Amount;
  extern bool bHealLv3_Self;
  extern int v_HealLv3_Range;
  extern int v_HealLv3_Amount;

  // 전법 수정 세부 설정 (동토)
  extern int v_DongtoLv1_Prob;
  extern int v_DongtoLv1_StateProb;
  extern int v_DongtoLv2_Prob;
  extern int v_DongtoLv2_StateProb;
  extern int v_DongtoLv3_Prob;
  extern int v_DongtoLv3_StateProb;

  // 전법 수정 세부 설정 (천계)
  extern int v_CelestiaLv1_Prob;
  extern int v_CelestiaLv1_Amount;
  extern int v_CelestiaLv1_Range;
  extern int v_CelestiaLv2_Prob;
  extern int v_CelestiaLv2_Amount;
  extern int v_CelestiaLv2_Range;
  extern int v_CelestiaLv3_Prob;
  extern int v_CelestiaLv3_Amount;
  extern int v_CelestiaLv3_Range;

  // 전법 수정 세부 설정 (투석기)
  extern int v_Catapult_MinRange;
  extern int v_Catapult_MaxRange;
  extern int v_Catapult_Amount;
  extern int v_Catapult_Range;

  // 전법 수정 세부 설정 (격류/낙석)
  extern int v_WaterLv1_Amount, v_WaterLv1_Range;
  extern int v_WaterLv2_Amount, v_WaterLv2_Range;
  extern int v_WaterLv3_Amount, v_WaterLv3_Range;
  extern int v_StoneLv1_Amount, v_StoneLv1_Range;
  extern int v_StoneLv2_Amount, v_StoneLv2_Range;
  extern int v_StoneLv3_Amount, v_StoneLv3_Range;

  // 방어 건물강화 세부 설정
  extern int v_City_Dur, v_City_Range, v_City_Atk, v_City_Sight;
  extern int v_Gate_Dur, v_Gate_Range, v_Gate_Atk, v_Gate_Sight;
  extern int v_Tower_Dur, v_Tower_Range, v_Tower_Atk, v_Tower_Sight;
  extern int v_WallCatapult_Dur, v_WallCatapult_Range, v_WallCatapult_Atk, v_WallCatapult_Sight;
  extern int v_Signal_Dur, v_Signal_Spirit, v_Signal_Sight;

  extern uintptr_t g_savedHeroAddr; // [신규] 데모플레이 대비 주인공 주소 백업용
  extern bool bFileLog;             // 파일 로그 출력 변수

  extern short g_TerrainBonusTable[13][18]; // 지형 보너스 테이블 [병종1~12][지형1~17]
  extern bool bShowTerrainBonusWin;         // 지형 보너스 설정 창 표시 여부

  extern void *g_RangeTextures[12]; // ID3D11ShaderResourceView* 배열 (1~11번)

  bool IsAnyUIOpen(); // 모든 UI 창 활성화 여부 확인 함수
} // namespace DX11Base
