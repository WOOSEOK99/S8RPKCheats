#include "pch.h"
#pragma comment(lib, "Shcore.lib")
#include "BattleMonitor.h"
#include "Cheats.h"
#include "Cheats/Civilian/CityInfoWindow.h"
#include "Cheats/Civilian/JewelSettings.h"
#include "Cheats/Civilian/Techpointcave.h"
#include "Cheats/Officer/OfficerDetail.h"
#include "Cheats/Officer/AIOfficerGrowth.h"
#include "Cheats/Officer/SpecialAbilityAutoAssign.h"
#include "Cheats/Officer/OfficerData.h"
#include "Cheats/Officer/RoninMonitor.h"
#include "Cheats/Officer/SelectOfficercapture.h"
#include "Cheats/Officer/StatMonitor.h"
#include "Cheats/Officer/TraitConfigRuntime.h"
#include "Cheats/Social/ChildEarlyAppearance.h"
#include "Cheats/System/MonthCapture.h"
#include "Cheats/System/SpeedHack.h"
#include "Cheats/System/SystemMonth.h"
#include "Cheats/System/TengiCave.h"
#include "Cheats/Civilian/TavernMonitor.h"
#include "Cheats/War/Battleunitcapture.h"
#include "Cheats/War/Defbuildingboost.h"
#include "Cheats/War/SiegeWarfare.h"
#include "Cheats/War/Spell5HealProbe.h"
#include "Cheats/War/StratagemSlotProbe.h"
#include "Config.h"
#include "Engine.h"
#include "Menu.h"
#include "MenuSections.h"
#include "MainColumns.h"
#include "MenuState.h"
#include "NotificationManager.h"
#include "TacticsEditWindow.h"
#include "debug.h"
#include "showcal.h"
#include "showlog.h"
#include <functional>

namespace DX11Base {

  static void DrawBattleEnvWindow(float scale) {
    if (!bShowBattleEnvWin)
      return;

    ImGui::SetNextWindowSize(ImVec2(670 * scale, 150 * scale), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(u8"전투 환경 및 조건 설정###BattleEnvWin", &bShowBattleEnvWin)) {
      if (ImGui::BeginTable("BattleEnvTable", 4, ImGuiTableFlags_None)) {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(u8"[택일] 날씨 전법 변경");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(u8"[택일] 전투일자 변경");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(u8"[택일] 지형 능력 변경");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(u8"[택일] 성벽 여울 변경");

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Checkbox(u8"간단 변경", &bWeatherSkillSimple)) {
          if (bWeatherSkillSimple)
            bWeatherSkillComplex = false;
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
              u8"악천후시 열화, 화시, 화전을 사용할 수 없습니다. 우천시 격류를 지형 제약 없이 사용할 수 있습니다.");
          ImGui::EndTooltip();
        }

        ImGui::TableNextColumn();
        if (ImGui::Checkbox(u8"상시 15일", &bDateAlways15)) {
          if (bDateAlways15)
            bDateDynamic = false;
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"15일차에 전투가 종료되고 수비측이 승리합니다.");
          ImGui::EndTooltip();
        }

        ImGui::TableNextColumn();
        if (ImGui::Checkbox(u8"공방만", &bTerrainAbilityAtkDef)) {
          if (bTerrainAbilityAtkDef)
            bTerrainAbilityAll = false;
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"-- 보병 : 삼림, 여울, 황무지, 암석에서 +10%.");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"-- 만병은 삼림 +20%로 증가, 습지 +10% 추가 적용");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"-- 기병 : 가도, 다리, 초원에서 +10%. 삼림, 암석에서 -10%. 습지, 산에서 -20%.");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"-- 유목기병은 평지 10% 추가 적용");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"-- 궁병 : 산지, 암석에서 +10%. 삼림, 습지에서 -10%.");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"-- 연노병은 산지 +20%로 증가. 삼림 페널티 제거.");

          ImGui::EndTooltip();
        }

        ImGui::TableNextColumn();
        if (ImGui::Checkbox(u8"성 주변 1칸 여울", &bSiegeWarfare)) {
          if (bSiegeWarfare) {
            bSiegeWarfare2 = false;
            DX11Base::SetSiegeWarfare2(false); // 2칸 모드 끄기
            DX11Base::SetSiegeWarfare(true);
          } else {
            DX11Base::SetSiegeWarfare(false);
          }
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"지형 보정치 적용할 경우 여울에서 보병 10%, 궁병 20% 보정 적용");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"여울 소모 이동력은 60 (이동력 풀로 써야만 진입 가능)");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"여울에 위치한 수비 부대는 매턴 총병력의 %d%%가 회복됨. (단, 타세력 동맹군은 제외)",
                             iSiegeHealRate);
          ImGui::EndTooltip();
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (ImGui::Checkbox(u8"대폭 변경", &bWeatherSkillComplex)) {
          if (bWeatherSkillComplex)
            bWeatherSkillSimple = false;
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"간단 변경 모두 적용");
          ImGui::Separator();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"강풍시 낙석을 지형 제약 없이 사용할 수 있습니다.");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"호우, 눈때 요격을 지형 제약 없이 사용할 수 있습니다.");
          ImGui::EndTooltip();
        }

        ImGui::TableNextColumn();
        if (ImGui::Checkbox(u8"유동 조절", &bDateDynamic)) {
          if (bDateDynamic)
            bDateAlways15 = false;
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"총참전 부대수에 따라 전투기한이 달라집니다.");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"20부대 미만: 15일 / 20~29부대: 20일 / 30부대 이상: 25일");
          ImGui::EndTooltip();
        }
        ImGui::AlignTextToFramePadding();
        if (ImGui::Checkbox(u8"스킵 활성화", &bSkipDaysEnabled)) {
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"전투 진입 시 시작 경과일을 설정하여 초반 턴을 스킵합니다.");
          ImGui::EndTooltip();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(50.0f * scale);
        if (ImGui::InputInt("##SkipDays", &nSkipDays, 0, 0)) {
          if (nSkipDays < 0) nSkipDays = 0;
          SaveConfig();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(u8"일");

        ImGui::TableNextColumn();
        if (ImGui::Checkbox(u8"공방/무력/지력", &bTerrainAbilityAll)) {
          if (bTerrainAbilityAll)
            bTerrainAbilityAtkDef = false;
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), u8"[공방만] 의 모든 효과 적용 + 무력/지력 추가");
          ImGui::Separator();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"지형에 따른 보정이 부대 전투력(공방)뿐 아니라");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"장수의 무력(데미지)과 지력(책략 성공률)에도 함께 적용됩니다.");
          ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), u8"예: 황무지의 보병 +10% -> 전투력/무력/지력 모두 110%");
          ImGui::EndTooltip();
        }

        // 지형 보너스 설정 버튼 추가
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f * scale);
        if (ImGui::Button(u8"지형 보너스 설정")) {
          bShowTerrainBonusWin = true;
        }

        ImGui::TableNextColumn();
        if (ImGui::Checkbox(u8"성 주변 2칸 여울", &bSiegeWarfare2)) {
          if (bSiegeWarfare2) {
            bSiegeWarfare = false;
            DX11Base::SetSiegeWarfare(false); // 1칸 모드 끄기
            DX11Base::SetSiegeWarfare2(true);
          } else {
            DX11Base::SetSiegeWarfare2(false);
          }
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"지형 보정치 적용할 경우 여울에서 보병 10%, 궁병 20% 보정 적용");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), u8"여울 소모 이동력은 60 (이동력 풀로 써야만 진입 가능)");
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                             u8"여울에 위치한 수비 부대는 매턴 총병력의 %d%%가 회복됨. (단, 타세력 동맹군은 제외)",
                             iSiegeHealRate);
          ImGui::EndTooltip();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(u8"부대회복 비율 :");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f * scale);
        if (ImGui::InputInt("##SiegeHealRate", &iSiegeHealRate, 1, 5)) {
          if (iSiegeHealRate < 0)
            iSiegeHealRate = 0;
          if (iSiegeHealRate > 100)
            iSiegeHealRate = 100;
          SaveConfig();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(u8"%");

        ImGui::EndTable();
      }
    }
    ImGui::End();
  }

  static void DrawTerrainBonusSettingsWin(float scale) {
    if (!bShowTerrainBonusWin)
      return;

    ImGui::SetNextWindowSize(ImVec2(980 * scale, 520 * scale), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(u8"지형 보너스 설정 (-30 ~ +30)###TerrainBonusWin", &bShowTerrainBonusWin)) {
      ImGui::Text(u8"각 칸은 -30 ~ +30 범위. 지형 순서별 개별 입력. 저장 후 지형 보정 코드는 껐다가 다시 켜야 반영됨.");

      static const char *troopNames[] = {u8"경보병",   u8"중보병",   u8"정예보병", u8"만병", u8"경기병",   u8"중기병",
                                         u8"정예기병", u8"유목기병", u8"궁병",     u8"노병", u8"정예궁병", u8"연노병"};

      static const char *terrainNames[] = {u8"가도", u8"평지", u8"황무지", u8"초원", u8"삼림", u8"습지",
                                           u8"산지", u8"여울", u8"다리",   u8"도하", u8"암석"};

      // 스크린샷과 동일한 지형 인덱스 매핑
      static const int terrainIndices[] = {1, 2, 3, 4, 5, 6, 7, 10, 15, 16, 17};

      if (ImGui::BeginTable("TerrainBonusTable", 12, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        // 헤더
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); // 첫 칸은 비움
        for (int j = 0; j < 11; j++) {
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(terrainNames[j]);
        }

        // 행 데이터
        for (int i = 0; i < 12; i++) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(troopNames[i]);

          int troopIdx = i + 1; // 병종 1~12
          for (int j = 0; j < 11; j++) {
            ImGui::TableNextColumn();
            int terrainIdx = terrainIndices[j];

            char id[32];
            sprintf_s(id, "##v_%d_%d", troopIdx, terrainIdx);

            int val = (int)g_TerrainBonusTable[troopIdx][terrainIdx];
            ImGui::SetNextItemWidth(60.0f * scale);
            if (ImGui::InputInt(id, &val, 0, 0)) {
              if (val > 30)
                val = 30;
              if (val < -30)
                val = -30;
              g_TerrainBonusTable[troopIdx][terrainIdx] = (short)val;
            }
          }
        }
        ImGui::EndTable();
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      if (ImGui::Button(u8"기본값 복원", ImVec2(120 * scale, 35 * scale))) {
        // 기본값 재설정 (Lua 스크립트 기반 초기값)
        short defaultTable[13][18] = {{0},
                                      {0, 0, 0, 10, 0, 10, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 10},
                                      {0, 0, 0, 10, 0, 10, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 10},
                                      {0, 0, 0, 10, 0, 10, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 10},
                                      {0, 0, 0, 10, 0, 20, 10, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 10},
                                      {0, 10, 0, 0, 10, -10, -20, -20, 0, 0, 0, 0, 0, 0, 0, 10, 0, -10},
                                      {0, 10, 0, 0, 10, -10, -20, -20, 0, 0, 0, 0, 0, 0, 0, 10, 0, -10},
                                      {0, 10, 0, 0, 10, -10, -20, -20, 0, 0, 0, 0, 0, 0, 0, 10, 0, -10},
                                      {0, 10, 10, 0, 10, -10, -20, -20, 0, 0, 0, 0, 0, 0, 0, 10, 0, -10},
                                      {0, 0, 0, 0, 0, -10, -10, 10, 0, 0, 20, 0, 0, 0, 0, 0, 0, 10},
                                      {0, 0, 0, 0, 0, -10, -10, 10, 0, 0, 20, 0, 0, 0, 0, 0, 0, 10},
                                      {0, 0, 0, 0, 0, -10, -10, 10, 0, 0, 20, 0, 0, 0, 0, 0, 0, 10},
                                      {0, 0, 0, 0, 0, 0, -10, 20, 0, 0, 20, 0, 0, 0, 0, 0, 0, 10}};
        memcpy(g_TerrainBonusTable, defaultTable, sizeof(g_TerrainBonusTable));
      }

      ImGui::SameLine(ImGui::GetWindowWidth() - 260.0f * scale);
      if (ImGui::Button(u8"저장 후 닫기", ImVec2(120 * scale, 35 * scale))) {
        SaveConfig();
        bShowTerrainBonusWin = false;
      }
      ImGui::SameLine();
      if (ImGui::Button(u8"닫기", ImVec2(80 * scale, 35 * scale))) {
        bShowTerrainBonusWin = false;
      }
    }
    ImGui::End();
  }

  static void DrawMemoryNotepadWindow(float scale) {
    if (!bShowMemoryNotepadWin)
      return;

    static char s_noteBuf[32768] = {};
    ImGui::SetNextWindowSize(ImVec2(720 * scale, 500 * scale), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(u8"메모장###MemoryNotepadWin", &bShowMemoryNotepadWin)) {
      ImGui::TextDisabled(u8"Ctrl+C/Ctrl+V 가능 · 버튼으로 전체 복사/지우기");
      if (ImGui::Button(u8"전체 복사", ImVec2(100 * scale, 0))) {
        ImGui::SetClipboardText(s_noteBuf);
      }
      ImGui::SameLine();
      if (ImGui::Button(u8"지우기", ImVec2(80 * scale, 0))) {
        s_noteBuf[0] = '\0';
      }
      ImGui::Separator();
      ImGui::InputTextMultiline("##note", s_noteBuf, sizeof(s_noteBuf), ImVec2(-1, -1));
    }
    ImGui::End();
  }

  // --- Menu 클래스 구현 ---

  void Menu::Render() {
    UINT dpi = GetDpiForWindow(g_Engine->pGameWindow);
    float scale = (dpi == 0) ? 1.0f : (float)dpi / 96.0f;

    if (g_Engine->bShowMenu)
      DrawMenu();

    DrawBattleEnvWindow(scale);
    DrawTerrainBonusSettingsWin(scale);
    DrawMemoryNotepadWindow(scale);

    // 상단 마퀴 알림 및 설정 저장 실패 팝업은 치트메뉴와 독립적으로 항상 실행
    DrawMarqueeNotifications(scale);
    DrawConfigSaveErrorPopup(scale);
  }

  void Menu::Loops() {
    PerfRefreshRawHookGate();
    PerfScope perfScope(PerfMetric::MenuLoops);

    uintptr_t gameBase = GetGameBase();
    uintptr_t p1 = (gameBase) ? *(uintptr_t *)(gameBase + 0xE0) : 0;

    // IsValidPtr → VirtualQuery 비용 큼: 주소/시간 기준으로만 재검증
    static uintptr_t s_loopProbeGameBase = 0;
    static uintptr_t s_loopProbeP1 = 0;
    static uint64_t s_loopProbeTick = 0;
    static bool s_loopP1Ready = false;
    static bool s_loopGameBaseReady = false;

    uint64_t nowTick = GetTickCount64();
    const bool needProbe =
        (gameBase != s_loopProbeGameBase) || (p1 != s_loopProbeP1) || (nowTick - s_loopProbeTick >= 200ull);
    if (needProbe) {
      s_loopProbeTick = nowTick;
      s_loopProbeGameBase = gameBase;
      s_loopProbeP1 = p1;
      s_loopP1Ready = (p1 > 0x10000) && IsValidPtr(p1, 0x200);
      s_loopGameBaseReady = (gameBase > 0x10000) && IsValidPtr(gameBase, 0x6000);
    }

    const bool p1Ready = s_loopP1Ready;
    const bool gameBaseReady = s_loopGameBaseReady;

    // version.dll 없이도 san8r_traits_config.json의 254개 기재 정의를 런타임 테이블에 적용합니다.
    // 내부에서 500ms 주기로 제한되며 테이블 재생성/효과 초기화 때만 실제 쓰기를 수행합니다.
    DX11Base::TickTraitConfigRuntime();

    // 저장게임/새 게임 로드 시 p1은 새 세대 주소로 교체되거나 잠시 0이 됩니다.
    // 전투/평정 모니터가 이전 전투 UI 포인터를 만지기 전에 5번 책략의
    // object-bound 상태를 먼저 폐기합니다. 체크 의도는 유지되므로 다음 전투에서 재적용됩니다.
    static uintptr_t s_stratagem5SessionP1 = 0;
    if (s_stratagem5SessionP1 != 0 && p1 != s_stratagem5SessionP1) {
      // Important: consume the transition immediately, including p1->0.
      // Otherwise the 30ms loop repeats the same reset/log while a save or
      // battle transition is in progress and can contend with the UI thread.
      const uintptr_t oldSessionP1 = s_stratagem5SessionP1;
      DX11Base::ResetStratagemFiveSessionRuntime(oldSessionP1, p1);
      DX11Base::ResetSpell5HealProbeSession(oldSessionP1, p1);
      s_stratagem5SessionP1 = p1Ready ? p1 : 0;
    } else if (s_stratagem5SessionP1 == 0 && p1Ready) {
      // First stable p1 after startup/load. The old generation was already
      // abandoned when it went to zero, so only attach the new generation.
      s_stratagem5SessionP1 = p1;
      AddLog(u8"[책략5수명] 새 p1 세대 연결: %p", reinterpret_cast<void *>(p1));
    }

    // 초기 설정 지연 로드 (안정성을 위해 GameBase가 최초로 사용 가능해진 시점에 로드)
    static bool s_configLoaded = false;
    if (gameBaseReady && !s_configLoaded) {
      DX11Base::LoadConfig();
      if (DX11Base::bAutoLoadMenu) {
        g_Engine->bShowMenu = true;
      }
      s_configLoaded = true;
      DX11Base::AddLog(u8"[System] GameBase 설정 준비 완료. 사용자 설정 불러오기 시작.");
    }

    // 무한 행동력 (매 프레임 VirtualQuery 낭비 방지: p1Ready가 0x200까지 보장)
    if (bInfiniteAP) {
      if (p1Ready)
        *(unsigned char *)(p1 + 0xEE) = 200;
    }

    // 연회 무제한: 확인된 시나리오 데이터 +0x71E4의 bit1만 지속 해제.
    // 매 프레임 호출되더라도 bit1이 이미 0이면 쓰기를 하지 않습니다.
    if (bInfiniteBanquet) {
      DX11Base::TickInfiniteBanquet();
    }

    // 중개 무제한: 확인된 시나리오 데이터 +0x71E5 bit6만 지속 해제.
    // 이미 0이면 쓰지 않으므로 다른 상태 비트에는 영향이 없습니다.
    if (bInfiniteMediation) {
      DX11Base::TickInfiniteMediation();
    }

    // 저장게임 로드 시 보주 개방 비트맵이 세이브 데이터로 덮이는 경우 자동 재적용.
    DX11Base::TickJewelSettings();

    // 보주 무한 (매 프레임 VirtualQuery 낭비 방지: gameBaseReady가 0x6000까지 보장)
    if (bFastJewel) {
      if (gameBaseReady)
        *(unsigned char *)(gameBase + 0x5C49) = 0;
    }

    // if (bSelectOfficerFirstInit) {
    //   DX11Base::SetOfficerCapture(true); // 프로그램 실행 시 딱 한 번 Hook 설치
    //   bSelectOfficerFirstInit = false;
    // }

    if (bInfTengi) {
      uint8_t sm = GetSystemMonthValue();
      uint8_t rm = GetCurrentMonth();
      static uint8_t s_lastTengiMonth = 0;

      // 무한 전기 발생 (1, 4, 7, 10월)
      if (sm == 1 || sm == 4 || sm == 7 || sm == 10) {
        // 치트 엔진처럼 지속적으로 값을 고정시킵니다.
        // SetTengi 에서는 포인터가 생성된 시점에 플래그를 1로 설정하여 크래시를 방지합니다.
        if (DX11Base::GetCapturedTengiAddr() != 0) {
          DX11Base::SetTengi(100);
        }
      }
    }

    // 전쟁 자동화 (전쟁 관련 변수 중 하나라도 켜져 있으면 캡처 활성화)
    static bool s_autoCaptureStarted = false;
    bool isAnyWarModActive =
        bSelfHeal || bDongto || bTerrainIgnore || bDefBuilding || bCatapult || bCelestial || bSiegeWarfare;

    if (isAnyWarModActive && !s_autoCaptureStarted) {
      if (!bBattleUnit) {
        bBattleUnit = true;
        DX11Base::SetBattleUnitCapture(true);
      }
      s_autoCaptureStarted = true;
    } else if (!isAnyWarModActive && s_autoCaptureStarted) {
      s_autoCaptureStarted = false;
    }

    static uint64_t s_lastBattleMonitorTick = 0;
    static uint64_t s_lastTechMonitorTick = 0;

    if (nowTick - s_lastBattleMonitorTick >= 100) {
      s_lastBattleMonitorTick = nowTick;
      MonitorBattleStatus();
    }
    if (nowTick - s_lastTechMonitorTick >= 500) {
      s_lastTechMonitorTick = nowTick;

      // 1월 평정에서는 AI 성장을 먼저 확정한 뒤
      // MonitorTechStatus()의 특수능력 자동판정이 성장 후 데이터를 읽도록 합니다.
      DX11Base::TickAIOfficerAutoGrowth();
      MonitorTechStatus();
      MonitorAllAggressive();
    }

    // 주의: 아래 로직도 동일 프레임의 gameBase/p1을 재사용

    // --- [주인공 주소 감지 및 로그] ---
    if (p1 > 0x10000 && p1 != g_savedHeroAddr) {
      // 주인공 주소가 처음 잡히거나, 다른 주소로 변경된 경우에만 백업 갱신 및 로그 출력
      if (IsValidPtr(p1, 0x100)) {
        g_savedHeroAddr = p1;
        AddLog(u8"[System] 주인공 주소 감지: %p", (void *)g_savedHeroAddr);
        s_loopProbeP1 = p1;
        s_loopP1Ready = (p1 > 0x10000) && IsValidPtr(p1, 0x200);
      }
    }

    ApplyStoredConfigs(p1, gameBase);
    RunYearlyRearSupport(p1);
    TickAutoAffinityGrowth(p1);
    DX11Base::TickSpecialAbilityAutoAssign();

    // 2026-04-04 재야장수 모니터링: RoninMonitor 모듈에 p1 전달 (3초 대기 + 자동 주소 계산 포함)
    RoninMonitor_Tick(p1);

    // 2026-04-04 배속 상태 동기화
    SpeedHack_Update(p1);

    // [추가] 전기 주소 캡처 감시 (알림 발생용)
    DX11Base::TengiCave_Tick();

    // 2026-04-04 월 값 감지 로그 (500ms 주기로 완화하여 안정성 확보)
    if (bMonthCapture) {
      static uint64_t s_lastMonthCheck = 0;
      if (GetTickCount64() - s_lastMonthCheck >= 500) {
        s_lastMonthCheck = GetTickCount64();

        static uint8_t s_lastSysMonth = 0xFF;
        static uint8_t s_lastRealMonth = 0xFF;

        uint8_t sysMonth = GetSystemMonthValue(); // 신규 AOB 방식
        uint8_t realMonth = GetCurrentMonth();    // 기존 RealMonth 방식

        if (sysMonth != s_lastSysMonth || realMonth != s_lastRealMonth) {
          s_lastSysMonth = sysMonth;
          s_lastRealMonth = realMonth;
        }
      }
    }

    // 주점 청부 무한 모니터링 (500ms 간격으로 스로틀링하여 렉 방지)
    static uint64_t s_lastTavernMonitor = 0;
    if (GetTickCount64() - s_lastTavernMonitor >= 500) {
        s_lastTavernMonitor = GetTickCount64();
        DX11Base::UpdateTavernRequests();
    }
  }

  void Menu::DrawMenu() {
    ImGuiIO &io = ImGui::GetIO();
    // [수정] FontGlobalScale은 폰트 렌더링 크기만 변경하고 ImGui 히트박스는 바꾸지 않습니다.
    // 버튼 크기 계산에는 InitImGui와 동일한 DPI 기반 scale을 사용해야 히트박스가 시각과 일치합니다.
    UINT dpi = GetDpiForWindow(g_Engine->pGameWindow);
    float scale = (dpi == 0) ? 1.0f : (float)dpi / 96.0f;

    static const char *s_windowTitleStr = u8"삼국지 8 리메이크 PK 치트 (" SAM8_CHEAT_VERSION ")###SAM8_CHEAT";
    ImGuiWindow *pMainWin = ImGui::FindWindowByName(s_windowTitleStr);
    bool bMenuCollapsedLastFrame = pMainWin ? pMainWin->Collapsed : false;

    // 상태에 따른 표시용 문자열과 ImGui 고유 ID 문자열 결정
    const char *visibleTitle = bMenuCollapsedLastFrame ? u8"치트" : u8"삼국지 8 리메이크 PK 치트 (" SAM8_CHEAT_VERSION ")";
    const char *finalTitle = bMenuCollapsedLastFrame ? u8"치트###SAM8_CHEAT"
                                                     : u8"삼국지 8 리메이크 PK 치트 (" SAM8_CHEAT_VERSION ")###SAM8_CHEAT";

    // AlwaysAutoResize: 레이아웃이 복잡할 때 좌표 계산 오차가 발생할 수 있음
    // 펼쳐진 상태에서는 스크롤바는 끄되, 가로/세로 자동 조절은 켜둠 (NoScrollbar만으로 오프셋 해결 시도)
    ImGuiWindowFlags Flags =
        bMenuCollapsedLastFrame
            ? (ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)
            : (ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (bMenuCollapsedLastFrame) {
      // 접혔을 때: 창 너비를 제목 글씨 크기에 딱 맞게 축소 (### 부분 제외하고 계산)
      // 'X' 버튼이 없으므로 보정값을 58.0f -> 35.0f로 줄임
      ImVec2 titleSize = ImGui::CalcTextSize(visibleTitle);
      ImGui::SetNextWindowSize(ImVec2(titleSize.x + 35.0f * scale, 0), ImGuiCond_Always);
    } else {
      ImGui::SetNextWindowSize(ImVec2(1050 * scale, 0), ImGuiCond_Always);

      // ImGui::SetNextWindowSizeConstraints(ImVec2(650 * scale, -1), ImVec2(650 * scale, -1));
    }

    // 상태 전환 감지 및 자동 위치 이동
    // 상태 전환 감지 및 자동 위치 이동
    static bool s_prevCollapsedState = bMenuCollapsedLastFrame;
    static bool s_pendingCenterNextFrame = false; // 중앙으로 보낼 예약 플래그

    if (bMenuCollapsedLastFrame) {
      // [접힌 상태] 매 프레임 우측 상단 끝으로 위치를 강제 고정 (더블 클릭 즉시 반응 및 밀착)
      ImVec2 titleSize = ImGui::CalcTextSize(visibleTitle);
      float cWidth = titleSize.x + 35.0f * scale;
      // 5.0f * scale 만큼의 오른쪽 여백을 추가로 뺍니다.
      float margin = 10.0f * scale; // 원하는 여백만큼 조절하세요 (예: 10픽셀)

      ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - cWidth - margin, 5.0f), ImGuiCond_Always);

      if (!s_prevCollapsedState) {
        // 이제 막 접힌 경우 (혹시 모를 초기화)
        s_pendingCenterNextFrame = false;
      }
    } else {
      // [펼쳐진 상태] 방금 펼쳐진 것이 감지되면 중앙으로 보냄
      if (s_prevCollapsedState || s_pendingCenterNextFrame) {
        ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        s_pendingCenterNextFrame = false;
      }
    }
    s_prevCollapsedState = bMenuCollapsedLastFrame;

    uintptr_t gameBase = GetGameBase();
    uintptr_t p1 = (gameBase) ? *(uintptr_t *)(gameBase + 0xE0) : 0;

    // 악명 0 고정 로직 (메뉴 표시 여부와 상관없이 매 프레임 동작)
    if (bZeroInfamy && p1 != 0) {
      unsigned short *pInfamy = (unsigned short *)(p1 + 0x108);
      if (*pInfamy > 0) {
        *pInfamy = 0;
      }
    }

    // ApplyStoredConfigs(p1, gameBase); // 가로챘으므로 여기서 중복 호출하지 않음 (Loops에서 수행)

    static bool bFirstFrame = true;
    if (bFirstFrame) {
      bFirstFrame = false;
      if (bAutoLoadMenu) {
        ImGui::SetNextWindowCollapsed(true, ImGuiCond_Appearing);
        ImVec2 titleSize = ImGui::CalcTextSize(u8"치트");
        float cWidth = titleSize.x + 58.0f * scale;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - cWidth - 10.0f, 5.0f), ImGuiCond_Appearing);
      }
    }

    // 틸트 키로 인한 접기/펴기 요청 처리
    if (DX11Base::bToggleMenuCollapseRequest) {
      DX11Base::bToggleMenuCollapseRequest = false;
      bool nextCollapsed = !bMenuCollapsedLastFrame;
      ImGui::SetNextWindowCollapsed(nextCollapsed, ImGuiCond_Always);
      if (!nextCollapsed)
        s_pendingCenterNextFrame = true; // 펼쳐질 예정이면 예약
    }

    // [수정] 접혀있을 때 UI 디자인: 둥근 테두리 및 투명 배경
    int pushStyleCount = 0;
    int pushColorCount = 0;

    if (bMenuCollapsedLastFrame) {
      // 접힌 상태: 배경 투명화 + 둥근 황금색 테두리 + 제목 좌측 정렬(간격 축소)
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * scale);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, ImVec2(0.0f, 0.5f)); // ▶ 버튼과 글자 사이 간격 좁히기
      ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0.0f, 0.0f)); // 추가로 아이템 간 간격도 0으로
      pushStyleCount = 4;

      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.88f, 0.0f, 0.8f));          // 황금색 테두리
      ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.0f, 0.0f, 0.0f, 0.4f)); // 투명 배경 (살짝 어둡게)
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.88f, 0.0f, 1.0f));            // 황금색 텍스트
      pushColorCount = 3;
    } else {
      // 펼쳐진 상태: 기존처럼 테두리 제거 (글자는 중앙 정렬 유지)
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      pushStyleCount = 1;
    }

    // 펼쳐졌을 때만 bKeepOpen 포인터를 넘겨서 X 버튼을 표시함
    bool bKeepOpen = true;
    bool bMenuExpanded = ImGui::Begin(finalTitle, bMenuCollapsedLastFrame ? NULL : &bKeepOpen, Flags);

    if (pushColorCount > 0)
      ImGui::PopStyleColor(pushColorCount);
    if (pushStyleCount > 0)
      ImGui::PopStyleVar(pushStyleCount);

    if (!bKeepOpen) {
      // 펼쳐진 상태에서 'X' 버튼 클릭 시: 창을 닫는 대신 접힘 상태로 전환
      ImGui::SetWindowCollapsed(true);
    }

    // 현재 메뉴 접힘 상태를 전역 변수에 저장 (FindWindowByName으로 알아낸 값이 가장 정확함)
    bIsMenuCollapsed = pMainWin ? pMainWin->Collapsed : false;

    // ─── [ 추가 ] 버전 클릭 시 디버그 모드 토글 (10초 내 10번 클릭) ───
    if (!bIsMenuCollapsed && bMenuExpanded) {
      static int s_versionClickCount = 0;
      static double s_lastVersionClickTime = 0.0;

      if (ImGui::IsMouseClicked(0)) {
        ImVec2 mousePos = ImGui::GetMousePos();
        ImVec2 winPos = ImGui::GetWindowPos();
        float titleBarHeight = ImGui::GetFrameHeight();

        // 타이틀바 영역 내 클릭인지 확인 (X 버튼 영역 제외한 제목 부분 위주)
        if (mousePos.x >= winPos.x && mousePos.x <= winPos.x + ImGui::GetWindowWidth() - 40.0f * scale &&
            mousePos.y >= winPos.y && mousePos.y <= winPos.y + titleBarHeight) {

          double currentTime = ImGui::GetTime();
          if (currentTime - s_lastVersionClickTime > 10.0) {
            s_versionClickCount = 0; // 10초 지나면 초기화
          }

          s_versionClickCount++;
          s_lastVersionClickTime = currentTime;

          if (s_versionClickCount >= 10) {
            bShowPasswordPopup = true; // 비밀번호 창 띄우기
            s_versionClickCount = 0;
            // 시각적 피드백 (로그)
            AddLog(u8"[시스템] 2차 인증이 필요합니다. 비밀번호를 입력해 주세요.");
          }
        }
      }
    }

    if (bMenuExpanded) {

      if (gameBase) {
        if (ImGui::BeginTable(
                "MainLayoutTable",
                5,
                ImGuiTableFlags_SizingStretchSame |
                    ImGuiTableFlags_NoSavedSettings)) {
          ImGui::TableSetupColumn("Column1", ImGuiTableColumnFlags_WidthStretch, 1.0f);
          ImGui::TableSetupColumn("Gap1", ImGuiTableColumnFlags_WidthFixed, 10.0f * scale);
          ImGui::TableSetupColumn("Column2", ImGuiTableColumnFlags_WidthStretch, 1.0f);
          ImGui::TableSetupColumn("Gap2", ImGuiTableColumnFlags_WidthFixed, 10.0f * scale);
          ImGui::TableSetupColumn("Column3", ImGuiTableColumnFlags_WidthStretch, 1.0f);

          ImGui::TableNextRow();

          ImGui::TableSetColumnIndex(0);
          MainColumns::DrawColumn1(p1, gameBase, scale);

          ImGui::TableSetColumnIndex(2);
          MainColumns::DrawColumn2(p1, gameBase, scale);

          ImGui::TableSetColumnIndex(4);
          MainColumns::DrawColumn3(p1, gameBase, scale);

          ImGui::EndTable();
        }
      }

      // 하단 고정 1행: 위젯 + 알림 설정
      ImGui::Separator();
      ImGui::Spacing();

      if (ImGui::BeginTable(
              "FooterPrimaryLayout",
              2,
              ImGuiTableFlags_SizingStretchSame |
                  ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("FooterWidgets", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("FooterNotifications", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), u8"[ 위젯 ]");
        ImGui::SameLine(0.0f, 10.0f * scale);

        if (ImGui::Checkbox(u8"전기취소##FOOTER_WIDGET", &DX11Base::bShowWidgetTengi)) {
          NotifyFeatureToggle(u8"위젯: 전기취소", DX11Base::bShowWidgetTengi);
          SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(u8"주인공##FOOTER_WIDGET", &DX11Base::bShowWidgetHero)) {
          NotifyFeatureToggle(u8"위젯: 주인공", DX11Base::bShowWidgetHero);
          SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(u8"모든무장##FOOTER_WIDGET", &DX11Base::bShowWidgetAllOfficers)) {
          NotifyFeatureToggle(u8"위젯: 모든 무장", DX11Base::bShowWidgetAllOfficers);
          SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox(u8"알림확인##FOOTER_WIDGET", &DX11Base::bShowWidgetNotif)) {
          NotifyFeatureToggle(u8"위젯: 알림확인", DX11Base::bShowWidgetNotif);
          SaveConfig();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), u8"[ 알림 ]");
        ImGui::SameLine(0.0f, 10.0f * scale);
        ImGui::TextDisabled(u8"속도");
        ImGui::SameLine(0.0f, 6.0f * scale);
        ImGui::SetNextItemWidth(150.0f * scale);
        if (ImGui::SliderFloat(u8"##FooterNotificationSpeed", &DX11Base::g_notificationSpeed,
                               20.0f, 500.0f, "%.0f px/s")) {
          SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button(u8"알림 비우기##Footer", ImVec2(100.0f * scale, 0))) {
          g_notifications.clear();
          AddLog(u8"[알림] 모든 내역을 초기화했습니다.");
        }

        ImGui::EndTable();
      }

      // 하단 고정 2행: 로그 + 게임 속도 + UI + 시작 설정
      ImGui::Spacing();

      if (ImGui::BeginTable(
              "FooterSecondaryLayout",
              4,
              ImGuiTableFlags_SizingStretchProp |
                  ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("FooterLog", ImGuiTableColumnFlags_WidthStretch, 1.8f);
        ImGui::TableSetupColumn("FooterSpeed", ImGuiTableColumnFlags_WidthStretch, 3.2f);
        ImGui::TableSetupColumn("FooterUI", ImGuiTableColumnFlags_WidthStretch, 2.8f);
        ImGui::TableSetupColumn("FooterStart", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.75f, 1.0f), u8"[ 로그 ]");
        ImGui::SameLine(0.0f, 8.0f * scale);
        if (ImGui::Checkbox(u8"파일 로그 출력", &bFileLog)) {
          SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::Text(u8"에포크 초기화 오류 파악용 로그(S8RPK_cheat.log)를 자동 저장합니다.");
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.0f, 1.0f), u8"[ 게임 속도 ]");
        ImGui::SameLine(0.0f, 8.0f * scale);

        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
        if (ImGui::Checkbox(u8"배속", &bSpeedHack)) {
          NotifyFeatureToggle(u8"배속", bSpeedHack);
          SpeedHack_Update(p1);
          SaveConfig();
        }
        ImGui::PopStyleColor();

        // 실제 적용 배율과 선택 중인 배율을 분리합니다.
        // +/-는 선택값만 바꾸고 [적용]을 눌렀을 때 한 번만 실제 속도를 변경합니다.
        static bool s_speedPendingInitialized = false;
        static float s_speedPending = 1.0f;
        static float s_speedAppliedSnapshot = 1.0f;

        const float normalizedApplied =
            SpeedHack_NormalizeMultiplier(g_speedMultiplier);
        if (!s_speedPendingInitialized ||
            normalizedApplied != s_speedAppliedSnapshot) {
          s_speedPending = normalizedApplied;
          s_speedAppliedSnapshot = normalizedApplied;
          s_speedPendingInitialized = true;
        }

        ImGui::SameLine();

        if (s_speedPending <= 1.0f)
          ImGui::BeginDisabled();
        if (ImGui::Button("-##SpeedMinus", ImVec2(25 * scale, 0)))
          s_speedPending =
              SpeedHack_NormalizeMultiplier(s_speedPending - 0.5f);
        if (s_speedPending <= 1.0f)
          ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::Text(u8"%.1fx", s_speedPending);
        ImGui::SameLine();

        if (s_speedPending >= 5.0f)
          ImGui::BeginDisabled();
        if (ImGui::Button("+##SpeedPlus", ImVec2(25 * scale, 0)))
          s_speedPending =
              SpeedHack_NormalizeMultiplier(s_speedPending + 0.5f);
        if (s_speedPending >= 5.0f)
          ImGui::EndDisabled();

        ImGui::SameLine();
        const bool hasPendingChange =
            s_speedPending != normalizedApplied;
        if (!hasPendingChange)
          ImGui::BeginDisabled();
        if (ImGui::Button(u8"적용##SpeedApply", ImVec2(55 * scale, 0))) {
          g_speedMultiplier =
              SpeedHack_NormalizeMultiplier(s_speedPending);
          s_speedAppliedSnapshot = g_speedMultiplier;

          if (bSpeedHack)
            SpeedHack_Update(p1);

          AddLog(u8"[SpeedHack] 사용자 배율 적용: %.1fx",
                 g_speedMultiplier);
          SaveConfig();
        }
        if (!hasPendingChange)
          ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextColored(
              ImVec4(1, 1, 0, 1),
              u8"1.0x가 기본 속도이며 0.5x 단위로 최대 5.0x까지 설정합니다.");
          ImGui::TextUnformatted(
              u8"+/-로 배율을 선택한 뒤 [적용]을 눌렀을 때만 실제 속도가 변경됩니다.");
          ImGui::TextDisabled(
              u8"※ 배속 체크를 끄면 게임은 1.0x로 동작하며, 선택한 배율 값은 다음 사용을 위해 저장됩니다.");
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), u8"[ UI ]");
        ImGui::SameLine(0.0f, 8.0f * scale);
        ImGui::Text(u8"배율");
        ImGui::SameLine();

        float scaleBtnSize = 25.0f * scale;
        if (ImGui::Button("-##ScaleDown", ImVec2(scaleBtnSize, 0))) {
          io.FontGlobalScale = (std::max)(0.5f, io.FontGlobalScale - 0.1f);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f * scale);
        if (ImGui::SliderFloat(u8"##UIScale", &io.FontGlobalScale, 0.5f, 3.0f, "%.1f")) {
          SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button("+##ScaleUp", ImVec2(scaleBtnSize, 0))) {
          io.FontGlobalScale = (std::min)(3.0f, io.FontGlobalScale + 0.1f);
        }

        ImGui::TableSetColumnIndex(3);
        ImGui::TextColored(ImVec4(0.65f, 1.0f, 0.65f, 1.0f), u8"[ 시작 ]");
        ImGui::SameLine(0.0f, 8.0f * scale);
        if (ImGui::Checkbox(u8"시작 시 치트창 표시", &DX11Base::bAutoLoadMenu)) {
          NotifyFeatureToggle(u8"시작 시 치트창 표시", DX11Base::bAutoLoadMenu);
          DX11Base::SaveConfig();
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextUnformatted(
              u8"체크하면 게임 시작 시 치트창을 자동으로 표시합니다.");
          ImGui::TextDisabled(
              u8"※ 저장된 치트 설정 자체는 이 옵션과 관계없이 항상 불러옵니다.");
          ImGui::EndTooltip();
        }

        ImGui::EndTable();
      }
    }

    if (p1 == 0) {
      ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), u8"주인공 정보가 아직 로드되지 않았습니다.");
      // (복원 버튼은 시나리오 섹션으로 이동됨)
    }

    // 디버깅 섹션 (맨 아래로 이동)
    DX11Base::debuging(gameBase, p1);

    // 메인 창의 현재 좌표와 크기를 기록 (창이 접히더라도 pMainWin 자료구조에서 직접 가져옴)
    ImVec2 mPos = pMainWin ? pMainWin->Pos : ImGui::GetWindowPos();
    ImVec2 mSize = pMainWin ? pMainWin->Size : ImGui::GetWindowSize();
    ImGui::End();

    // [수정] 접힘 상태일 때 추가 위젯(뱃지) 렌더링
    if (bIsMenuCollapsed) {
      float currentX = mPos.x; // 메인 배지의 현재 X 좌표
      float widgetMargin = 5.0f * scale;

      // 스타일 설정 (메인 배지와 동일하게 유지)
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * scale);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.88f, 0.0f, 0.8f));
      ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.0f, 0.0f, 0.0f, 0.4f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.88f, 0.0f, 1.0f));

      ImGuiWindowFlags badgeFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;

      auto DrawBadge = [&](const char *label, const char *id, std::function<void()> onClick) {
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        float paddingX = 18.0f * scale; // 여백 약간 확대
        float paddingY = 6.0f * scale;
        float wWidth = labelSize.x + paddingX;
        float wHeight = labelSize.y + paddingY;
        currentX -= (wWidth + widgetMargin);

        // 윈도우 크기를 실제 그릴 영역보다 약간 여유있게 설정 (클리핑 방지)
        ImGui::SetNextWindowPos(ImVec2(currentX - 1.0f, mPos.y - 1.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(wWidth + 2.0f, wHeight + 2.0f), ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin(id, nullptr, badgeFlags)) {
          ImDrawList *drawList = ImGui::GetWindowDrawList();
          ImVec2 pMin = ImVec2(ImGui::GetWindowPos().x + 1.0f, ImGui::GetWindowPos().y + 1.0f);
          ImVec2 pMax = ImVec2(pMin.x + wWidth, pMin.y + wHeight);
          float rounding = 12.0f * scale;

          // 투명 버튼으로 상호작용 영역 확보
          ImGui::SetCursorPos(ImVec2(1.0f, 1.0f));
          ImGui::InvisibleButton("##btn", ImVec2(wWidth, wHeight));

          bool isHovered = ImGui::IsItemHovered();
          bool isActive = ImGui::IsItemActive();

          if (ImGui::IsItemDeactivatedAfterEdit() || (isHovered && ImGui::IsMouseReleased(0))) {
            // 클릭 판정 (IsItemDeactivatedAfterEdit는 보통 Input에 쓰이지만, 버튼 클릭 후 떼졌을 때 체크용)
            // 여기서는 단순히 InvisibleButton의 클릭 리턴을 써도 되지만, 더 확실하게 하기 위해:
          }

          // InvisibleButton이 true를 리턴하는 시점에 onClick 호출 (버튼 클릭 동작)
          if (ImGui::IsItemClicked(0)) {
            // 클릭 시작
          }
          if (ImGui::IsItemDeactivated() && isHovered) {
            onClick();
          }

          // 1. 마우스 상태에 따른 배경색 결정
          ImU32 bgColor = IM_COL32(20, 20, 20, 160); // 기본
          if (isActive)
            bgColor = IM_COL32(80, 80, 80, 220); // 클릭 시
          else if (isHovered)
            bgColor = IM_COL32(50, 50, 50, 190); // 호버 시

          // 2. 배경 및 테두리
          drawList->AddRectFilled(pMin, pMax, bgColor, rounding);
          ImU32 borderColor = isHovered ? IM_COL32(255, 255, 100, 255) : IM_COL32(255, 224, 0, 200);
          drawList->AddRect(pMin, pMax, borderColor, rounding, 0, 1.5f);

          // 3. 텍스트 렌더링
          ImVec2 textPos = ImVec2(pMin.x + paddingX * 0.5f, pMin.y + paddingY * 0.5f);
          drawList->AddText(textPos, IM_COL32(255, 224, 0, 255), label);
        }
        ImGui::End();
        ImGui::PopStyleVar();
      };

      // 사용자 요청 순서: 알림확인, 전기취소, 주인공, 모든무장 (오른쪽에서 왼쪽 방향으로 배치되므로 역순으로 체크)
      if (DX11Base::bShowWidgetAllOfficers && p1 != 0) {
        DrawBadge(u8"모든무장", u8"모든무장###WIDGET_ALL", [&]() {
          DX11Base::bShowOfficerListWin = !DX11Base::bShowOfficerListWin;
          if (DX11Base::bShowOfficerListWin)
            DX11Base::bForceCenterOfficerList = true;
        });
      }
      if (DX11Base::bShowWidgetHero && p1 != 0) {
        DrawBadge(u8"주인공", u8"주인공###WIDGET_HERO", [&]() {
          DX11Base::bShowOfficerDetail = !DX11Base::bShowOfficerDetail;
          if (DX11Base::bShowOfficerDetail)
            DX11Base::bForceCenterOfficerDetail = true;
        });
      }
      if (DX11Base::bShowWidgetTengi && p1 != 0 && DX11Base::GetCapturedTengiAddr() != 0) {
        DrawBadge(u8"전기취소", u8"전기취소###WIDGET_TENGI", [&]() {
          if (DX11Base::GetCapturedTengiAddr() != 0) {
            DX11Base::CancelTengi();
            DX11Base::AddLog(u8"[위젯] 전기 취소 (플래그 적용)");
          }
        });
      }

      // [신규] 알림확인 배지
      if (DX11Base::bShowWidgetNotif) {
        DrawBadge(u8"알림확인", u8"알림확인###WIDGET_NOTIF_HIST",
                  [&]() { DX11Base::bShowNotificationLog = !DX11Base::bShowNotificationLog; });
      }

      ImGui::PopStyleColor(3);
      ImGui::PopStyleVar(2);
    }

    // 자녀 목록 감시/갱신: 관리 창을 닫아도 새 자녀를 계속 감지
    EnsureChildManagerCapture();
    RunChildManagerUpdate();

    // 도시 반란 카운트 자동 유지 (도시 정보 창을 닫아도 동작)
    RunCityRevoltAlwaysZero();

    // 부속 창들 렌더링 (메인 메뉴의 접힘/펼침 상태와 독립적으로 항상 그려지도록 분리)
    DrawOfficerDetailWindow(p1, mPos, mSize, scale);
    DrawSelectedOfficerWindow(mPos, mSize, scale);
    DrawOfficerListWindow(p1, scale);
    DrawSpouseListWindow(scale);
    DrawSpecialtyInfoWindow(scale);
    DrawCityInfoWindow(p1, scale);
    DrawChildManagerWindow(scale);
    DrawMemoryNotepadWindow(scale);
    DrawNotificationHistoryWindow(scale);
    MenuSections::DrawTacticsEditWindow(scale);

    // [전역] 숫자 입력기 관리 (어떤 창에서 요청했든 상관없이 렌더링되게 함)
    if (pSelectedVar != nullptr) {
      ImGui::OpenPopup(currentLabel.c_str());
      ShowCalcPopup(currentLabel.c_str(), pSelectedVar);
    }
    // [추가] 디버그 비밀번호 인증 팝업
    if (bShowPasswordPopup) {
      ImGui::OpenPopup(u8"디버그 비밀번호 인증");
    }

    if (ImGui::BeginPopupModal(u8"디버그 비밀번호 인증", &bShowPasswordPopup, ImGuiWindowFlags_AlwaysAutoResize)) {
      static char passBuf[64] = "";
      ImGui::Text(u8"개발자 도구 접근을 위해 비밀번호를 입력하세요:");
      ImGui::Spacing();

      bool enterPressed = ImGui::InputText("##DebugPassword", passBuf, sizeof(passBuf),
                                           ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      if (ImGui::Button(u8"확인", ImVec2(120 * scale, 0)) || enterPressed) {
        if (strcmp(passBuf, "jws1234") == 0) {
          bShowDebug = !bShowDebug;
          bShowPasswordPopup = false;
          memset(passBuf, 0, sizeof(passBuf));
          AddLog(u8"[시스템] 인증 성공: 디버그 모드가 %s되었습니다.", bShowDebug ? u8"활성화" : u8"비활성화");
        } else {
          AddLog(u8"[시스템] 인증 실패: 비밀번호가 올바르지 않습니다.");
        }
      }
      ImGui::SameLine();
      if (ImGui::Button(u8"취소", ImVec2(120 * scale, 0))) {
        bShowPasswordPopup = false;
        memset(passBuf, 0, sizeof(passBuf));
      }

      ImGui::EndPopup();
    }
  }
} // namespace DX11Base