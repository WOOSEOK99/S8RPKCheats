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
    static uint8_t s_lastAppliedMonth = 0;

    // 방어 건물 강화가 켜져 있을 때만 작동
    if (bDefBuilding) {
        // [2026-04-04] 월 비교를 통한 평정(Council) 자동 감지
        uint8_t sm = GetSystemMonthValue();
        uint8_t rm = GetCurrentMonth();

        // 평정 조건: 시스템월(sm)이 3,6,9,12 이고 실제월(rm)이 4,7,10,1 인 경우
        // 즉, rm이 sm보다 한 달 빠른 시점이 게임 내 '평정' 상태임
        bool isCouncil = (sm > 0 && sm % 3 == 0) && (rm == (sm % 12) + 1);

        if (isCouncil && s_lastAppliedMonth != sm) {
            AddLog(u8"[자동화] 평정(Council) 감지 (Sys:%d, Real:%d) -> 방어 건물 자동 리프레시", sm, rm);
            
            // 껐다 켜서 확실하게 적용 (Refresh)
            SetDefBuildingBoost(false);
            SetDefBuildingBoost(true);
            
            s_lastAppliedMonth = sm; // 처리 완료 기록
        }

        // [2026-04-05] 전투맵 셔플 자동 제어 (평정 시에만 활성화)
        UpdateBattleMapAuto(isCouncil);

    } else {
        if (s_lastAppliedMonth != 0) {
            s_lastAppliedMonth = 0;
        }
    }
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
