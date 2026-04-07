#include "pch.h"
#pragma comment(lib, "Shcore.lib")
#include "BattleMonitor.h"
#include "Cheats.h"
#include "Cheats/Battleunitcapture.h"
#include "Cheats/Defbuildingboost.h"
#include "Cheats/MonthCapture.h"
#include "Cheats/OfficerDetail.h"
#include "Cheats/RoninMonitor.h"
#include "Cheats/SelectOfficercapture.h"
#include "Cheats/SpeedHack.h"
#include "Cheats/SystemMonth.h"
#include "Cheats/Techpointcave.h"
#include "Cheats/TengiCave.h"
#include "Config.h"
#include "Engine.h"
#include "Menu.h"
#include "MenuSections.h"
#include "MenuState.h"
#include "debug.h"
#include "showcal.h"
#include "showlog.h"
#include <functional>

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

    // 중지 성성 취소 무조건 취소 모니터링 루프
    if (bCancelCastleEvent) {
      uintptr_t captAddr = DX11Base::GetCapturedTengiAddr();
      if (captAddr != 0) {
        uintptr_t addr80 = captAddr - 0x10;
        uintptr_t addr90 = captAddr;
        uintptr_t addrA0 = captAddr + 0x10;

        // 포인터 유효성 검사
        if (DX11Base::IsValidPtr(addr80, 2) && DX11Base::IsValidPtr(addr90, 1) && DX11Base::IsValidPtr(addrA0, 2)) {
          if (*(uint8_t *)(addr80) == 0x90 && *(uint8_t *)(addr80 + 1) == 0xE0 && *(uint8_t *)(addr90) == 0xE0 &&
              *(uint8_t *)(addrA0) == 0x28 && *(uint8_t *)(addrA0 + 1) == 0xCB) {

            // 조건 일치시 전기 취소와 동일하게 완전히 초기화 (0x18, 0x08, 0x10 초기화)
            DX11Base::CancelTengi();
          }
        }
      }
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

        uint8_t sysMonth = GetSystemMonthValue(); // 신규 AOB 방식
        uint8_t realMonth = GetCurrentMonth();    // 기존 RealMonth 방식

        if (sysMonth != s_lastSysMonth || realMonth != s_lastRealMonth) {
          if (sysMonth > 0 || realMonth > 0) {
            AddLog(u8"[Debug] 월 비교 - 시스템(AOB): %d, 리얼(Capture): %d", sysMonth, realMonth);
          }
          s_lastSysMonth = sysMonth;
          s_lastRealMonth = realMonth;
        }
      }
    }
  }

  void Menu::DrawMenu() {
    ImGuiIO &io = ImGui::GetIO();
    // [수정] FontGlobalScale은 폰트 렌더링 크기만 변경하고 ImGui 히트박스는 바꾸지 않습니다.
    // 버튼 크기 계산에는 InitImGui와 동일한 DPI 기반 scale을 사용해야 히트박스가 시각과 일치합니다.
    UINT dpi = GetDpiForWindow(g_Engine->pGameWindow);
    float scale = (dpi == 0) ? 1.0f : (float)dpi / 96.0f;

    static const char *s_windowTitleStr = u8"삼국지 8 리메이크 치트 (V0.60)###SAM8_CHEAT";
    ImGuiWindow *pMainWin = ImGui::FindWindowByName(s_windowTitleStr);
    bool bMenuCollapsedLastFrame = pMainWin ? pMainWin->Collapsed : false;

    // 상태에 따른 표시용 문자열과 ImGui 고유 ID 문자열 결정
    const char *visibleTitle = bMenuCollapsedLastFrame ? u8"치트" : u8"삼국지 8 리메이크 치트 (V0.60)";
    s_windowTitleStr =
        bMenuCollapsedLastFrame ? u8"치트###SAM8_CHEAT" : u8"삼국지 8 리메이크 치트 (V0.60)###SAM8_CHEAT";

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
      ImGui::SetNextWindowSize(ImVec2(650 * scale, 0), ImGuiCond_Always);

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
      // 접힌 상태: 배경 투명화 + 둥근 황금색 테두리
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * scale);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
      pushStyleCount = 2;

      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.88f, 0.0f, 0.8f));          // 황금색 테두리
      ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.0f, 0.0f, 0.0f, 0.4f)); // 투명 배경 (살짝 어둡게)
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.88f, 0.0f, 1.0f));            // 황금색 텍스트
      pushColorCount = 3;
    } else {
      // 펼쳐진 상태: 기존처럼 테두리 제거
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      pushStyleCount = 1;
    }

    // 펼쳐졌을 때만 bKeepOpen 포인터를 넘겨서 X 버튼을 표시함
    bool bKeepOpen = true;
    bool bMenuExpanded = ImGui::Begin(s_windowTitleStr, bMenuCollapsedLastFrame ? NULL : &bKeepOpen, Flags);

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
        float gap = 5.0f * scale; // 왼쪽과 오른쪽 사이의 확실한 간격
        float leftColWidth = 330.0f * scale;

        // 1. 테이블 시작 (2열, 가로 꽉 채우기 플래그)
        if (ImGui::BeginTable("MainLayoutTable", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
          // 2. 컬럼 설정: 왼쪽은 고정, 오른쪽은 남은 공간 전부(Stretch)
          ImGui::TableSetupColumn("Left", ImGuiTableColumnFlags_WidthFixed, leftColWidth);
          ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthStretch);

          ImGui::TableNextRow();

          // --- [ 왼쪽 컬럼 ] ---
          ImGui::TableSetColumnIndex(0);
          // 왼쪽 내용 그리기
          MenuSections::DrawCivilianSection(p1, gameBase, scale);

          // --- [ 오른쪽 컬럼 ] ---
          ImGui::TableSetColumnIndex(1);

          // 간격을 주기 위해 오른쪽 컬럼 시작점에서 살짝 띄웁니다.
          ImGui::Indent(gap);

          MenuSections::DrawSocialSection(p1, gameBase, scale);
          MenuSections::DrawWarSection(p1, gameBase, scale);
          MenuSections::DrawOfficerDetailSection(p1, ImGui::GetWindowPos(), ImGui::GetWindowSize(), scale);

          ImGui::Unindent(gap); // 들여쓰기 해제

          ImGui::EndTable();
        }
      }

      // 하단: 공용 설정 (배속 및 UI 배율 한 줄 통합)
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
      if (ImGui::Checkbox(u8"배속", &bSpeedHack)) {
        SpeedHack_Update();
        SaveConfig();
      }
      ImGui::PopStyleColor();

      ImGui::SameLine();
      // [-] 버튼 - 높이를 0으로 두어 슬라이더와 동일한 자동 높이 사용 (히트박스 일치)
      if (ImGui::Button("-##SpeedMinus", ImVec2(25 * scale, 0))) {
        g_speedMultiplier -= 0.1f;
        if (g_speedMultiplier < 0.1f)
          g_speedMultiplier = 0.1f;
        SpeedHack_Update();
        SaveConfig();
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(100.0f * scale);
      if (ImGui::SliderFloat(u8"##SpeedMul", &g_speedMultiplier, 0.1f, 5.0f, u8"%.1fx")) {
        SpeedHack_Update();
        SaveConfig();
      }
      ImGui::SameLine();
      // [+] 버튼 - 높이를 0으로 두어 슬라이더와 동일한 자동 높이 사용 (히트박스 일치)
      if (ImGui::Button("+##SpeedPlus", ImVec2(25 * scale, 0))) {
        g_speedMultiplier += 0.1f;
        if (g_speedMultiplier > 5.0f)
          g_speedMultiplier = 5.0f;
        SpeedHack_Update();
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"0.1 = 슬로우, 1.0 = 정상, 2.0 = 2배속, 최대 5배속");
        ImGui::EndTooltip();
      }

      ImGui::SameLine(0, 20.0f * scale);
      ImGui::Text(u8"UI 배율");
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

      ImGui::SameLine(0, 15.0f * scale);
      if (ImGui::Checkbox(u8"자동로드", &DX11Base::bAutoLoadMenu)) {
        DX11Base::SaveConfig();
      }
    }

    if (p1 == 0) {
      ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), u8"주인공 정보가 아직 로드되지 않았습니다.");
    }

    // 디버깅 섹션 (맨 아래로 이동)
    DX11Base::debuging(gameBase, p1);

    // 메인 창의 현재 좌표와 크기를 기록 (창이 접히더라도 GetWindowPos 등은 동작함)
    ImVec2 mPos = ImGui::GetWindowPos();
    ImGui::End();
    ImVec2 mSize = ImGui::GetWindowSize();

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

        ImGuiWindowFlags badgeFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;

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

      // 사용자 요청 순서: 전기취소, 주인공, 모든무장 (오른쪽에서 왼쪽 방향으로 배치되므로 역순으로 체크)
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
      if (DX11Base::bShowWidgetTengi && p1 != 0) {
        DrawBadge(u8"전기취소", u8"전기취소###WIDGET_TENGI", [&]() {
          if (DX11Base::GetCapturedTengiAddr() != 0) {
            DX11Base::CancelTengi();
            DX11Base::AddLog(u8"[위젯] 전기 취소 (플래그 적용)");
          }
        });
      }

      ImGui::PopStyleColor(3);
      ImGui::PopStyleVar(2);
    }

    // 부속 창들 렌더링 (메인 메뉴의 접힘/펼침 상태와 독립적으로 항상 그려지도록 분리)
    DrawOfficerDetailWindow(p1, mPos, mSize, scale);
    DrawSelectedOfficerWindow(mPos, mSize, scale);
    DrawOfficerListWindow(p1, scale);

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