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

    static bool s_isTechBoostApplied = false;

    // 방어 건물 강화가 켜져 있을 때만 작동
    if (bDefBuilding) {
        // 1. 캡처 모드가 꺼져 있다면 활성화 (이미 켜져 있으면 무시됨)
        SetTechPCapture(true);

        // 2. 캡처된 주소가 있는지 확인
        uintptr_t techBase = g_capturedTechPAddr;
        if (techBase != 0) {
            // 주소가 유효하다면 자동 리프레시
            if (IsValidPtr(techBase, 0x24B)) {
                AddLog(u8"[자동화] 기술 포인트 기반 포착(%p) -> 방어 건물 강화 리프래시", (void*)techBase);
                
                // 껐다 켜서 확실하게 적용 (Refresh)
                SetDefBuildingBoost(false);
                SetDefBuildingBoost(true);
                
                s_isTechBoostApplied = true;
            }

            // [핵심] 하트비트: 다시 포착할 수 있도록 전역 주소 초기화
            g_capturedTechPAddr = 0;
        }
    } else {
        if (s_isTechBoostApplied) {
            SetTechPCapture(false);
            s_isTechBoostApplied = false;
        }
    }
}

} // namespace DX11Base
