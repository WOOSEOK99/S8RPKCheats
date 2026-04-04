#include "pch.h"
#pragma comment(lib, "Shcore.lib")
#include "BattleMonitor.h"
#include "Cheats.h"
#include "Cheats/Battleunitcapture.h"
#include "Cheats/Defbuildingboost.h"
#include "Cheats/OfficerDetail.h"
#include "Cheats/SelectOfficercapture.h"
#include "Cheats/RoninMonitor.h"
#include "Cheats/SpeedHack.h"
#include "Cheats/MonthCapture.h"
#include "Cheats/Techpointcave.h"
#include "Config.h"
#include "Engine.h"
#include "Menu.h"
#include "MenuSections.h"
#include "MenuState.h"
#include "debug.h"
#include "showcal.h"
#include "showlog.h"

namespace DX11Base {
  // --- Menu 클래스 구현 ---

  void Menu::Render() {
    if (g_Engine->bShowMenu)
      DrawMenu();
  }

  void Menu::Loops() {
    // 무한 행동력
    if (bInfiniteAP) {
      uintptr_t gameBase = GetGameBase();
      if (gameBase) {
        uintptr_t p1 = *(uintptr_t *)(gameBase + 0xE0);
        if (p1)
          *(unsigned char *)(p1 + 0xEE) = 200;
      }
    }

    // 보주 무한
    if (bFastJewel) {
      uintptr_t gameBase = GetGameBase();
      if (gameBase)
        *(unsigned char *)(gameBase + 0x5C49) = 0;
    }

    if (bSelectOfficerFirstInit) {
      DX11Base::SetOfficerCapture(true); // 프로그램 실행 시 딱 한 번 Hook 설치
      bSelectOfficerFirstInit = false;
    }

    // 전쟁 자동화 (전쟁 관련 변수 중 하나라도 켜져 있으면 캡처 활성화)
    static bool s_autoCaptureStarted = false;
    bool isAnyWarModActive = bSelfHeal || bDongto || bTerrainIgnore || bDefAtk || bCatapult || bCelestial;

    if (isAnyWarModActive && !s_autoCaptureStarted) {
      if (!bBattleUnit) {
        bBattleUnit = true;
        DX11Base::SetBattleUnitCapture(true);
        AddLog(u8"[자동화] 전쟁 모드 감지 -> 유닛 캡처 자동 활성화");
      }
      s_autoCaptureStarted = true;
    } else if (!isAnyWarModActive && s_autoCaptureStarted) {
      s_autoCaptureStarted = false;
    }

    MonitorBattleStatus();
    MonitorTechStatus();

    uintptr_t gameBase = GetGameBase();
    uintptr_t p1 = (gameBase) ? *(uintptr_t *)(gameBase + 0xE0) : 0;
    ApplyStoredConfigs(p1, gameBase);

    // 2026-04-04 재야장수 모니터링: RoninMonitor 모듈에 p1 전달 (3초 대기 + 자동 주소 계산 포함)
    RoninMonitor_Tick(p1);

    // 2026-04-04 배속 상태 동기화
    SpeedHack_Update();

    // 2026-04-04 월 값 감지 로그 (500ms 주기로 완화하여 안정성 확보)
    if (bMonthCapture) {
      static uint64_t s_lastMonthCheck = 0;
      if (GetTickCount64() - s_lastMonthCheck >= 500) {
        s_lastMonthCheck = GetTickCount64();

        static uint8_t s_lastSysMonth = 0xFF;
        static uint8_t s_lastRealMonth = 0xFF;

        uint8_t sysMonth = GetSystemMonthValue();
        uint8_t realMonth = GetCurrentMonth();

        if (sysMonth != s_lastSysMonth || realMonth != s_lastRealMonth) {
          // 리얼 월드가 아직 캡처되지 않았을 때(0)는 비교 로그를 찍지 않음
          if (realMonth > 0) {
            AddLog(u8"[Debug] 월 비교 - 시스템: %d월, 리얼: %d월", sysMonth, realMonth);
          }
          s_lastSysMonth = sysMonth;
          s_lastRealMonth = realMonth;
        }
      }
    }
  }

  void Menu::DrawMenu() {
    ImGuiIO &io = ImGui::GetIO();
    float scale = io.FontGlobalScale;

    static const char *s_windowTitleStr = u8"삼국지 8 리메이크 치트 (V0.4)###SAM8_CHEAT";
    ImGuiWindow *pMainWin = ImGui::FindWindowByName(s_windowTitleStr);
    bool bMenuCollapsedLastFrame = pMainWin ? pMainWin->Collapsed : false;

    // 상태에 따른 표시용 문자열과 ImGui 고유 ID 문자열 결정
    const char *visibleTitle = bMenuCollapsedLastFrame ? u8"치트" : u8"삼국지 8 리메이크 치트 (V0.4)";
    s_windowTitleStr = bMenuCollapsedLastFrame ? u8"치트###SAM8_CHEAT" : u8"삼국지 8 리메이크 치트 (V0.4)###SAM8_CHEAT";

    // AlwaysAutoResize를 접혔을 때만 제거 (이 플래그가 있으면 ImGui가 접기를 무시함)
    ImGuiWindowFlags Flags = bMenuCollapsedLastFrame ? (ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)
                                                     : ImGuiWindowFlags_AlwaysAutoResize;

    if (bMenuCollapsedLastFrame) {
      // 접혔을 때: 창 너비를 제목 글씨 크기에 딱 맞게 축소 (### 부분 제외하고 계산)
      // 'X' 버튼이 없으므로 보정값을 58.0f -> 35.0f로 줄임
      ImVec2 titleSize = ImGui::CalcTextSize(visibleTitle);
      ImGui::SetNextWindowSize(ImVec2(titleSize.x + 35.0f * scale, 0), ImGuiCond_Always);
    } else {
      ImGui::SetNextWindowSizeConstraints(ImVec2(780 * scale, -1), ImVec2(1000 * scale, -1));
    }

    // 상태 전환 감지 및 자동 위치 이동
    // 상태 전환 감지 및 자동 위치 이동
    static bool s_prevCollapsedState = bMenuCollapsedLastFrame;
    static bool s_pendingCenterNextFrame = false; // 중앙으로 보낼 예약 플래그

    if (bMenuCollapsedLastFrame) {
      // [접힌 상태] 매 프레임 우측 상단 끝으로 위치를 강제 고정 (더블 클릭 즉시 반응 및 밀착)
      ImVec2 titleSize = ImGui::CalcTextSize(visibleTitle);
      float cWidth = titleSize.x + 35.0f * scale;
      ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - cWidth, 5.0f), ImGuiCond_Always);

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

    // [수정] 펼쳐졌을 때는 X를 보여주고, 접혔을 때는 배경 투명화 및 황금색 텍스트 적용
    bool bKeepOpen = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (bMenuCollapsedLastFrame) {
      // 접힌 상태: 배경 투명화 및 화살표/글자를 황금색으로 강조
      ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.88f, 0.0f, 1.0f)); // 골드 색상
    }

    // 펼쳐졌을 때만 bKeepOpen 포인터를 넘겨서 X 버튼을 표시함
    bool bMenuExpanded = ImGui::Begin(s_windowTitleStr, bMenuCollapsedLastFrame ? NULL : &bKeepOpen, Flags);

    if (bMenuCollapsedLastFrame) {
      ImGui::PopStyleColor(4);
    }
    ImGui::PopStyleVar();

    if (!bKeepOpen) {
      // 펼쳐진 상태에서 'X' 버튼 클릭 시: 창을 닫는 대신 접힘 상태로 전환
      ImGui::SetWindowCollapsed(true);
    }

    // 현재 메뉴 접힘 상태를 전역 변수에 저장 (FindWindowByName으로 알아낸 값이 가장 정확함)
    bIsMenuCollapsed = pMainWin ? pMainWin->Collapsed : false;

    if (bMenuExpanded) {
      if (gameBase) {
        // 2열 레이아웃: 경계선 제거(false) 및 컬럼 너비 최적화
        ImGui::Columns(2, "MainLayout", false);
        static bool s_rescaledMain = false;    // 배율 변경 시 재조정 등을 위해 static 사용
        ImGui::SetColumnWidth(0, 460 * scale); // 왼쪽 컬럼에 필요한 최소 공간 부여

        // 왼쪽 컬럼: 내정 및 특수 기능
        MenuSections::DrawCivilianSection(p1, gameBase, scale);

        ImGui::NextColumn();

        // 오른쪽 컬럼: 관계 및 전쟁
        MenuSections::DrawSocialSection(p1, gameBase, scale);
        MenuSections::DrawWarSection(p1, gameBase, scale);
        MenuSections::DrawOfficerDetailSection(p1, ImGui::GetWindowPos(), ImGui::GetWindowSize(), scale);

        // 팝업 처리
        if (pSelectedVar != nullptr) {
          ImGui::OpenPopup(currentLabel.c_str());
          ShowCalcPopup(currentLabel.c_str(), pSelectedVar);
        }

        ImGui::Columns(1);
      }

      // 하단: 공용 설정
      ImGui::Spacing();
      ImGui::Separator(); // 가독성을 위한 구분선 추가
      ImGui::Spacing();

      ImGui::Text(u8"UI 배율 설정");
      float scaleBtnSize = 25.0f * scale;
      if (ImGui::Button("-##ScaleDown", ImVec2(scaleBtnSize, scaleBtnSize))) {
        io.FontGlobalScale = (std::max)(0.5f, io.FontGlobalScale - 0.1f);
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(120.0f * scale);
      ImGui::SliderFloat(u8"##UIScale", &io.FontGlobalScale, 0.5f, 3.0f, "Scale: %.1f");
      ImGui::SameLine();
      if (ImGui::Button("+##ScaleUp", ImVec2(scaleBtnSize, scaleBtnSize))) {
        io.FontGlobalScale = (std::min)(3.0f, io.FontGlobalScale + 0.1f);
      }

      ImGui::SameLine(0, 10.0f * scale);
      if (ImGui::Checkbox(u8"시작시 자동로드", &DX11Base::bAutoLoadMenu)) {
        DX11Base::SaveConfig();
      }

      if (p1 != 0) {
        float spacing = 10.0f * scale; // 버튼 사이의 간격

        // 1. 실제 버튼 크기 미리 계산
        ImVec2 size0 = ImGui::CalcTextSize(u8"모든 무장 정보");
        size0.x += 20 * scale;
        size0.y = 25.0f * scale;

        ImVec2 size1 = ImGui::CalcTextSize(u8"주인공 정보");
        size1.x += 20 * scale;
        size1.y = 25.0f * scale;

        ImVec2 size2 = ImGui::CalcTextSize(u8"선택 무장 정보");
        size2.x += 20 * scale;
        size2.y = 25.0f * scale;

        float totalWidth = size0.x + size1.x + size2.x + spacing * 2;

        // 2. 우측 정렬을 위한 시작 위치 계산 (콘텐츠 가용 영역 기준)
        float startX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - totalWidth;
        if (startX < ImGui::GetCursorPosX())
          startX = ImGui::GetCursorPosX(); // 최소 왼쪽 정렬 유지

        ImGui::SameLine(startX);

        // [버튼 0: 모든 장수 목록]
        if (ImGui::Button(u8"모든 무장 정보", size0)) {
          DX11Base::bShowOfficerListWin = true;
        }
        ImGui::SameLine(0, spacing);

        // [버튼 1: 기존 무장 상세]
        if (ImGui::Button(u8"주인공 정보", size1)) {
          bShowOfficerDetail = !bShowOfficerDetail;
        }
      }

      if (p1 == 0) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), u8"주인공 정보가 아직 로드되지 않았습니다.");
      }

      // 항상 표시되는 버튼 (선택 무장 정보)
      {
        float spacing = 10.0f * scale;
        ImVec2 sizeS = ImGui::CalcTextSize(u8"선택 무장 정보");
        sizeS.x += 20 * scale;
        sizeS.y = 25.0f * scale;

        float startX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - sizeS.x;
        if (startX < ImGui::GetCursorPosX())
          startX = ImGui::GetCursorPosX();

        ImGui::SameLine(startX);
        if (ImGui::Button(u8"선택 무장 정보", sizeS)) {
          bShowSelectedOfficerWin = !bShowSelectedOfficerWin;
        }
      }

      // 디버깅 섹션 (맨 아래로 이동)
      DX11Base::debuging(gameBase, p1);
    }

    // 메인 창의 현재 좌표와 크기를 기록 (창이 접히더라도 GetWindowPos 등은 동작함)
    ImVec2 mPos = ImGui::GetWindowPos();
    ImVec2 mSize = ImGui::GetWindowSize();

    ImGui::End();

    // 부속 창들 렌더링 (메인 메뉴의 접힘/펼침 상태와 독립적으로 항상 그려지도록 분리)
    DrawOfficerDetailWindow(p1, mPos, mSize, scale);
    DrawSelectedOfficerWindow(mPos, mSize, scale);
    DrawOfficerListWindow(p1, scale);
  }
} // namespace DX11Base