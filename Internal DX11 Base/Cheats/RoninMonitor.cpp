// =============================================================================
// RoninMonitor.cpp  –  재야 장수 자동 감시 모듈 (독립형)
// 2026-04-07
//
// [작동 방식]
//   1. RoninMonitor_Tick(p1)을 Menu::Loops() 백그라운드 스레드에서 매 루프 호출.
//   2. IsConfigReady() (= 3초 지연 통과)가 참이 되면 단 한 번, p1 기반으로
//      무장 배열 시작점을 자동 계산합니다.
//        arrayBase = p1 - ((heroID - 1) * 0x3D0)
//   3. 이후 2초마다 5102명 전수 조사하여 미발견→재야 전환을 감지합니다.
//   4. RoninMonitor_Draw()를 Engine.cpp 렌더링 루프에서 호출해 알림창을 그립니다.
// =============================================================================

#include "RoninMonitor.h"
#include "BattleMonitor.h" // IsInBattle()
#include "Cheats.h"        // GetGameBase(), IsValidPtr(), bMonitorRonin
#include "CityData.h"      // g_CityList, g_CityCount
#include "Config.h"        // IsConfigReady()
#include "Framework/imgui.h"
#include "MenuState.h"   // bMonitorRonin
#include "OfficerData.h" // g_officerNames
#include "OfficerRosterResolve.h"
#include "SystemMonth.h" // GetSystemMonthValue()
#include "pch.h"
#include "showlog.h" // AddLog()

#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

namespace DX11Base {

  namespace {
    struct RoninNotification {
      std::string name;
      std::string cityName;
      float timeRemaining;
    };

    static uintptr_t s_lastHeroAddr = 0;
    static uintptr_t s_arrayBase = 0;
    static uintptr_t s_cityBase = 0;
    static bool s_baseResolved = false;
    static bool s_initialized = false;
    static uint8_t s_lastSystemMonth = 0xFF;

    // unordered_map 대신 고정 배열 – O(1) 접근, 할당 오버헤드 없음
    static bool s_isRonin[5103] = {false};

    // Tick(배경)과 Draw(UI)가 공유 – 짧은 잠금으로만 보호
    static std::mutex s_notifMtx;
    static std::vector<RoninNotification> s_notifications;
  } // namespace

  // 외부(수동 조작) 동기화
  void RoninMonitor_UpdatePrevStatus(int id, uint8_t st) {
    if (id >= 1 && id <= 5102)
      s_isRonin[id] = (st == 0x58);
  }

  // ---------------------------------------------------------------------------
  static bool TryResolveBase(uintptr_t p1) {
    if (s_baseResolved)
      return true;

    if (!p1)
      return false;

    // 1) 포인터 체인을 통한 안정적인 해상도 (SelectOfficercapture 방식)
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t chainBase = 0;
    if (exeBase && TryResolveOfficerRosterArrayBase(exeBase, &chainBase) && chainBase > 0x10000) {
      s_arrayBase = chainBase;
      s_baseResolved = true;
      AddLog(u8"[RoninMonitor] 무장 배열 확보: 0x%llX", (unsigned long long)s_arrayBase);
      return true;
    }

    return false;
  }

  // ---------------------------------------------------------------------------
  // Tick – 백그라운드 스레드
  // ---------------------------------------------------------------------------
  void RoninMonitor_Tick(uintptr_t p1) {
    if (!bMonitorRonin) {
      s_initialized = s_baseResolved = false;
      s_arrayBase = 0;
      memset(s_isRonin, 0, sizeof(s_isRonin));
      s_lastSystemMonth = 0xFF;
      {
        std::lock_guard<std::mutex> lk(s_notifMtx);
        s_notifications.clear();
      }
      return;
    }

    // --- [세션 관리] 주인공 주소가 바뀌면 세이브 로드로 간주하여 초기화 ---
    if (p1 != 0 && p1 != s_lastHeroAddr) {
      if (s_lastHeroAddr != 0) {
        AddLog(u8"[RoninMonitor] 주인공 주소 변경 감지 (0x%llX -> 0x%llX). 세션 초기화 및 재스캔 예약.", 
               (unsigned long long)s_lastHeroAddr, (unsigned long long)p1);
      }
      s_lastHeroAddr = p1;
      s_initialized = false;
      s_baseResolved = false;
      s_lastSystemMonth = 0xFF; // 다음 월 체크 때 즉시 트리거되도록
    }

    if (IsInBattle())
      return;
    if (!IsConfigReady()) {
      static bool s_warnedConfig = false;
      if (!s_warnedConfig) {
        s_warnedConfig = true;
        AddLog(u8"[RoninMonitor] IsConfigReady=false, 대기 중...");
      }
      return;
    }

    // 매달 스캔 직전에 주소 재확인 (세션 동기화)
    if (!TryResolveBase(p1)) {
      return; // 베이스 확보될 때까지 대기
    }

    // 월이 바뀔 때만 전수 조사 수행 (GetSystemMonthValue 기준)
    uint8_t currentMonth = GetSystemMonthValue();
    if (currentMonth == 0 || currentMonth > 12)
      return;
    if (s_lastSystemMonth == currentMonth)
      return;
    s_lastSystemMonth = currentMonth;

    // 전수 조사 – 4KB 페이지 단위 유효성 체크 (~50회 vs 5102회)
    std::vector<RoninNotification> found;
    std::vector<std::string> initialRonins;

    // ⑤ 도시 배열 주소 획득 (가급적 한 번만 수행)
    if (s_cityBase <= 0x10000) {
      uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
      if (exeBase) {
        uintptr_t pp1 = *(uintptr_t *)(exeBase + 0x34C8630);
        if (pp1 > 0x10000 && IsValidPtr(pp1, 8)) {
          uintptr_t pp2 = *(uintptr_t *)(pp1);
          if (pp2 > 0x10000 && IsValidPtr(pp2, 8)) {
            s_cityBase = *(uintptr_t *)(pp2);
          }
        }
      }
    }
    uintptr_t cityBase = s_cityBase;

    uintptr_t lastPage = 0;
    bool pageOk = false;

    // 가비지 데이터(예: 배열 뒤편의 쓰레기값)가 동일한 realID를 가질 경우 반복 갱신되는 버그 방어
    bool seenThisTick[5103] = {false};

    for (int i = 1; i <= 5102; i++) {
      uintptr_t addr = s_arrayBase + (uintptr_t)(i - 1) * 0x3D0;
      uintptr_t page = addr & ~0xFFFull;
      if (page != lastPage) {
        lastPage = page;
        pageOk = IsValidPtr(page, 0x1000);
      }
      if (!pageOk)
        continue;

      // 경계 교차 시 다음 페이지만 한 번 더 검사 (O(1) 최적화)
      if ((addr + 0x60) > (page + 0xFFF)) {
        if (!IsValidPtr(page + 0x1000, 0x1000))
          continue;
      }

      uint16_t realID = *(uint16_t *)(addr + 0x08);
      if (realID == 0 || realID > 5102)
        continue;

      // 이미 이번 틱에서 읽은 ID라면 중복 가비지로 간주하고 스킵
      if (seenThisTick[realID])
        continue;
      seenThisTick[realID] = true;

      uint8_t cur = *(uint8_t *)(addr + 0x10);
      
      if (!s_initialized) {
        // 첫 스캔: 재야(0x58) 무장만 추출해서 저장, 알림 없음
        bool isRonin = (cur == 0x58);
        s_isRonin[realID] = isRonin;
        if (isRonin) {
          std::string name = g_officerNames.count(realID) ? g_officerNames[realID] : (u8"미등록 무장(ID:" + std::to_string(realID) + u8")");
          initialRonins.push_back(name);
        }
      } else {
        // 이후 스캔: 재야 무장에 추가되었는지 확인
        if (cur == 0x58) {
          if (!s_isRonin[realID]) {
            // 새로 추가된 경우 알림
            std::string name = g_officerNames.count(realID) ? g_officerNames[realID] : (u8"미등록 무장(ID:" + std::to_string(realID) + u8")");
            std::string city = u8"알 수 없는 장소";
            if (cityBase > 0x10000) {
              uintptr_t cityPtr = *(uintptr_t *)(addr + 0x20);
              if (cityPtr >= cityBase) {
                int idx = (int)((cityPtr - cityBase) / 0x2A0);
                if (idx >= 0 && idx < g_CityCount) {
                  city = g_CityList[idx].cityname;
                }
              }
            }
            found.push_back({name, city, 12.f});
            s_isRonin[realID] = true;
          }
        } else {
          // 재야가 아닌 경우
          s_isRonin[realID] = false;
        }
      }
    }
    
    // 2026-04-14 유저 요청: 초기 재야장수 목록 너무 길어서 출력 삭제
    if (!s_initialized && !initialRonins.empty()) {
        // 내부 데이터는 위에서 이미 s_isRonin 배열에 정상 등록됨. 출력만 제거.
    }
    s_initialized = true;

    // 알림 목록에 추가 + 로그 – 잠금 해제 후 AddLog (데드락 방지)
    if (!found.empty()) {
      {
        std::lock_guard<std::mutex> lk(s_notifMtx);
        for (auto &f : found)
          if ((int)s_notifications.size() < 10)
            s_notifications.push_back(f);
      }
      for (auto &f : found)
        AddLog(u8"[재야 감지] %s → %s", f.name.c_str(), f.cityName.c_str());
    }
  }

  // ---------------------------------------------------------------------------
  // Draw – UI 스레드
  // ---------------------------------------------------------------------------
  void RoninMonitor_Draw() {
    if (!bMonitorRonin)
      return;

    float dt = ImGui::GetIO().DeltaTime;
    if (dt > 0.1f)
      dt = 0.1f; // 프리징 후 dt 스파이크로 알림 즉시 만료 방지
    std::vector<RoninNotification> snap;
    {
      std::lock_guard<std::mutex> lk(s_notifMtx);
      for (auto &n : s_notifications)
        n.timeRemaining -= dt;
      s_notifications.erase(std::remove_if(s_notifications.begin(), s_notifications.end(),
                                           [](const RoninNotification &n) { return n.timeRemaining <= 0.f; }),
                            s_notifications.end());
      snap = s_notifications;
    }

    if (snap.empty())
      return;

    float sc = ImGui::GetIO().FontGlobalScale;
    ImVec2 disp = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(disp.x - 20.f, disp.y * 0.12f), ImGuiCond_Always, ImVec2(1.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.f, 0.84f, 0.f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30.f, 20.f));

    constexpr ImGuiWindowFlags kF = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##RoninMonitorNotif", nullptr, kF)) {
      ImGui::SetWindowFontScale(1.8f);

      // 헤더 1회 출력
      ImGui::Separator();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 0.6f, 1.f));
      ImGui::Text(u8" [ 재야 장수 발견!! ]");
      ImGui::PopStyleColor();
      ImGui::Separator();

      // 테이블 형태로 무장 목록 출력
      if (ImGui::BeginTable("##RoninTable", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn(u8"이름", ImGuiTableColumnFlags_WidthFixed, 130.f * sc);
        ImGui::TableSetupColumn(u8"도시", ImGuiTableColumnFlags_WidthFixed, 90.f * sc);

        // 테이블 헤더 (이름        도시)
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.f), u8"이름");
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.f), u8"도시");

        // 데이터 행
        for (auto &n : snap) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextColored(ImVec4(0.3f, 1.f, 1.f, 1.f), u8"%s", n.name.c_str());
          ImGui::TableSetColumnIndex(1);
          ImGui::TextColored(ImVec4(1.f, 1.f, 0.6f, 1.f), u8"%s", n.cityName.c_str());
        }
        ImGui::EndTable();
      }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
  }

} // namespace DX11Base
