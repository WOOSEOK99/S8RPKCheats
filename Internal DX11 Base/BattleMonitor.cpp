#include "pch.h"
#include "BattleMonitor.h"
#include "MenuState.h"
#include "Cheats.h"
#include "showlog.h"
#include "Cheats/Battleunitcapture.h"
#include "Cheats/Celestia.h"
#include "Cheats/Catapult.h"
#include "Cheats/Defatkboost.h"
#include "Cheats/Defbuildingboost.h"
#include "Cheats/Selfheal.h"
#include "Cheats/Terrainignore.h"
#include "Cheats/Techpointcave.h"
#include "Cheats/Dongto.h"
#include "Cheats/Roadblock.h"
#include "Cheats/MonthCapture.h"
#include "Cheats/SystemMonth.h"
#include "Cheats/BattleMapShuffle.h"
#include "Cheats/StatMonitor.h"

namespace DX11Base {

void MonitorBattleStatus() {
    static bool s_isWarModsApplied = false;
    static float s_lastSeenTime = 0.0f;
    float currentTime = (float)GetTickCount64() / 1000.0f;

    // 0. 기반 주소 체크 (게임 로딩/메뉴 시 자동 초기화)
    uintptr_t gameBase = DX11Base::GetGameBase();
    if (gameBase == 0) {
      if (s_isWarModsApplied) {
        s_isWarModsApplied = false;
        s_lastSeenTime = 0;
        DX11Base::g_battleUnitAddr1 = 0;
        DX11Base::g_battleUnitAddr2 = 0;
      }
      return;
    }

    // 1. 현재 캡처된 주소 확인
    uintptr_t addr1 = DX11Base::g_battleUnitAddr1;
    uintptr_t addr2 = DX11Base::g_battleUnitAddr2;

    if (addr1 != 0 || addr2 != 0) {
      // [전투 중] 주소가 포착됨
      s_lastSeenTime = currentTime;


      // 아직 리프레시를 안 했다면 실행
      if (!s_isWarModsApplied) {
        AddLog(u8"[자동화] 전투 감지(%llX) -> 모든 전쟁 모드 리프레시", addr1);

        // 켜져 있는 기능들에 대해 원본 복구 후 다시 적용 (Refresh)
        if (bSelfHeal)       { DX11Base::SetSelfHeal(false); DX11Base::SetSelfHeal(true); }
        if (bDongto)         { DX11Base::SetDongto(false); DX11Base::SetDongto(true); } // 2026-03-30 추가
        if (bTerrainIgnore)  { DX11Base::SetTerrainIgnore(false); DX11Base::SetTerrainIgnore(true); }
        if (bDefAtk)         { DX11Base::SetDefAtkBoost(false); DX11Base::SetDefAtkBoost(true); }
        if (bCatapult)       { DX11Base::SetCatapultCheat(false); DX11Base::SetCatapultCheat(true); }
        if (bCelestial)      { DX11Base::SetCelestialMod(false); DX11Base::SetCelestialMod(true); }

        s_isWarModsApplied = true;
      }

      // [핵심: 하트비트] 읽은 주소를 즉시 비웁니다.
      DX11Base::g_battleUnitAddr1 = 0;
      DX11Base::g_battleUnitAddr2 = 0;

    } else {
      // [비전투 중] 주소가 0임
      if (s_isWarModsApplied && (currentTime - s_lastSeenTime > 3.0f)) {
        AddLog(u8"[자동화] 상태 초기화 (다음 전투 대기)");
        s_isWarModsApplied = false;
        s_lastSeenTime = 0;
      }
    }
}

void MonitorTechStatus() {
    static uint8_t s_lastAppliedMonth = 0xFF; // 초기값 0xFF (0월 감지 가능하도록)

    // [2026-04-04] 월 비교를 통한 평정(Council) 자동 감지
    uint8_t sm = GetSystemMonthValue();
    uint8_t rm = GetCurrentMonth();

    // 평정 조건: 시스템월(sm)과 실제월(rm)의 차이 기반 감지
    // sm=0(1월), 3(4월), 6(7월), 9(10월) 일 때 rm이 1, 4, 7, 10 이면 평정
    bool isCouncil = (sm % 3 == 0) && (rm == (sm % 12) + 1);

    if (isCouncil) {
        if (s_lastAppliedMonth != sm) {
            AddLog(u8"[자동화] 평정(Council) 감지됨 (Sys:%d, Real:%d)", sm, rm);

            if (bDefBuilding) {
                SetDefBuildingBoost(false);
                SetDefBuildingBoost(true);
            }

            // 능력치 99 -> 100 자동 보정
            UpdateOfficerStats99To100();

            s_lastAppliedMonth = sm; 
        }
    } else {
        if (s_lastAppliedMonth != 0xFF) {
            s_lastAppliedMonth = 0xFF; 
        }
    }

    UpdateBattleMapAuto(isCouncil);
}

// 전투 상태 반환 함수 추가 (외부 모듈에서 현재 전투중인지 판별할 때 사용)
bool IsInBattle() {
    float currentTime = (float)GetTickCount64() / 1000.0f;
    static float s_lastKnownSeenTime = 0.0f;
    
    // Check global addr directly to update heartbeat if needed without MonitorBattleStatus side effects
    if (DX11Base::g_battleUnitAddr1 != 0 || DX11Base::g_battleUnitAddr2 != 0) {
        s_lastKnownSeenTime = currentTime;
        return true;
    }
    
    // Heartbeat timeout is 1.25s
    if ((currentTime - s_lastKnownSeenTime) < 1.25f) {
        return true;
    }
    return false;
}

} // namespace DX11Base
