// =============================================================================
// RoninMonitor.cpp  –  재야 장수 자동 감시 모듈
//
// [작동 방식]
//   1. 기능 활성화 후 현재 재야 상태를 기준선으로 1회 저장합니다. (알림 없음)
//   2. 이후 월 변경이 아니라 내정(0x07) -> 평정(0x05) 전환 시점에만 5102명을 다시 검사합니다.
//   3. 직전 기준선에서는 재야가 아니었지만 현재 0x58(재야)이 된 장수만 이름/도시 리스트로 표시합니다.
//   4. 검사 결과를 다음 평정 비교용 기준선으로 저장합니다.
// =============================================================================

#include "RoninMonitor.h"
#include "../../BattleMonitor.h"
#include "../../Cheats.h"
#include "../Civilian/CityData.h"
#include "../../Config.h"
#include "../../Framework/imgui.h"
#include "../../MenuState.h"
#include "OfficerData.h"
#include "OfficerRosterResolve.h"
#include "../../pch.h"
#include "../../showlog.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

namespace DX11Base {

  namespace {
    constexpr uint8_t kStateCouncil = 0x05;
    constexpr uint8_t kStateDomestic = 0x07;

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
    static bool s_wasEnabled = false;
    static uint8_t s_lastRelevantGameState = 0;

    // ID 1~5102의 직전 평정 기준 재야 여부
    static bool s_isRonin[5103] = {false};

    static std::mutex s_notifMtx;
    static std::vector<RoninNotification> s_notifications;

    struct SpecialAbilityPopup {
      std::vector<std::string> lines;
      float timeRemaining = 12.f;
    };

    static std::vector<SpecialAbilityPopup> s_specialAbilityQueue;
  } // namespace

  void RoninMonitor_UpdatePrevStatus(int id, uint8_t st) {
    if (id >= 1 && id <= 5102)
      s_isRonin[id] = (st == 0x58);
  }

  static void ResetRoninMonitorState(bool clearNotifications) {
    s_initialized = false;
    s_baseResolved = false;
    s_arrayBase = 0;
    s_cityBase = 0;
    s_lastRelevantGameState = 0;
    std::memset(s_isRonin, 0, sizeof(s_isRonin));

    if (clearNotifications) {
      std::lock_guard<std::mutex> lk(s_notifMtx);
      s_notifications.clear();
    }
  }

  static bool TryResolveBase(uintptr_t p1) {
    if (s_baseResolved)
      return true;
    if (!p1)
      return false;

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

  // SEH는 C++ 소멸자가 있는 함수(예: std::vector를 가진 ScanRonins) 안에서 사용할 수 없으므로
  // 원시 메모리 읽기만 별도 헬퍼로 분리합니다.
  static bool SafeReadOfficerScanFields(uintptr_t addr, uint16_t* outId, uint8_t* outStatus, uintptr_t* outCityPtr) {
    if (!outId || !outStatus || !outCityPtr)
      return false;

    __try {
      *outId = *(uint16_t *)(addr + 0x08);
      *outStatus = *(uint8_t *)(addr + 0x10);
      *outCityPtr = *(uintptr_t *)(addr + 0x20);
      return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      *outId = 0;
      *outStatus = 0;
      *outCityPtr = 0;
      return false;
    }
  }

  static uint8_t ReadRelevantGameState() {
    uintptr_t gameBase = GetGameBase();
    if (gameBase <= 0x10000 || !IsValidPtr(gameBase + 0xD0, 1))
      return 0;

    uint8_t state = 0;
    __try {
      state = *(uint8_t *)(gameBase + 0xD0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return 0;
    }

    return (state == kStateCouncil || state == kStateDomestic) ? state : 0;
  }

  static uintptr_t ResolveCityBaseOnce() {
    if (s_cityBase > 0x10000)
      return s_cityBase;

    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return 0;

    __try {
      uintptr_t pp1 = *(uintptr_t *)(exeBase + 0x34C8630);
      if (pp1 <= 0x10000 || !IsValidPtr(pp1, 8))
        return 0;

      uintptr_t pp2 = *(uintptr_t *)(pp1);
      if (pp2 <= 0x10000 || !IsValidPtr(pp2, 8))
        return 0;

      uintptr_t cityBase = *(uintptr_t *)(pp2);
      if (cityBase <= 0x10000)
        return 0;

      s_cityBase = cityBase;
      return s_cityBase;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return 0;
    }
  }

  static void ScanRonins(bool notifyNew) {
    if (s_arrayBase <= 0x10000)
      return;

    std::vector<RoninNotification> found;
    uintptr_t cityBase = ResolveCityBaseOnce();

    uintptr_t lastPage = 0;
    bool pageOk = false;
    bool seenThisTick[5103] = {false};
    bool currentRonin[5103] = {false};

    for (int i = 1; i <= 5102; i++) {
      uintptr_t addr = s_arrayBase + (uintptr_t)(i - 1) * 0x3D0;
      uintptr_t page = addr & ~0xFFFull;

      if (page != lastPage) {
        lastPage = page;
        pageOk = IsValidPtr(page, 0x1000);
      }
      if (!pageOk)
        continue;

      if ((addr + 0x60) > (page + 0xFFF)) {
        if (!IsValidPtr(page + 0x1000, 0x1000))
          continue;
      }

      uint16_t realID = 0;
      uint8_t status = 0;
      uintptr_t cityPtr = 0;
      if (!SafeReadOfficerScanFields(addr, &realID, &status, &cityPtr))
        continue;

      if (realID == 0 || realID > 5102 || seenThisTick[realID])
        continue;
      seenThisTick[realID] = true;

      const bool isRonin = (status == 0x58);
      currentRonin[realID] = isRonin;

      if (notifyNew && isRonin && !s_isRonin[realID]) {
        std::string name = g_officerNames.count(realID)
                               ? g_officerNames[realID]
                               : (u8"미등록 무장(ID:" + std::to_string(realID) + u8")");
        std::string city = u8"알 수 없는 장소";

        if (cityBase > 0x10000 && cityPtr >= cityBase) {
          uintptr_t diff = cityPtr - cityBase;
          if ((diff % 0x2A0) == 0) {
            int idx = (int)(diff / 0x2A0);
            if (idx >= 0 && idx < g_CityCount)
              city = g_CityList[idx].cityname;
          }
        }

        found.push_back({name, city, 12.f});
      }
    }

    // 이번 검사 결과가 다음 평정 비교 기준선이 됩니다.
    std::memcpy(s_isRonin, currentRonin, sizeof(s_isRonin));
    s_initialized = true;

    if (!found.empty()) {
      {
        std::lock_guard<std::mutex> lk(s_notifMtx);
        for (auto &f : found) {
          if ((int)s_notifications.size() >= 10)
            break;
          s_notifications.push_back(f);
        }
      }

      for (auto &f : found)
        AddLog(u8"[재야 감지] %s → %s", f.name.c_str(), f.cityName.c_str());
    }
  }

  void RoninMonitor_QueueSpecialAbilityNotice(const std::vector<std::string>& lines) {
    if (lines.empty())
      return;

    std::lock_guard<std::mutex> lk(s_notifMtx);

    // 한 번의 연말 판정 결과를 한 팝업 묶음으로 보관합니다.
    // 너무 많은 행으로 화면이 커지는 것을 막기 위해 최대 24행까지만 표시하고,
    // 전체 내역은 기존 알림 기록에 그대로 남습니다.
    SpecialAbilityPopup popup;
    const size_t limit = (std::min<size_t>)(lines.size(), 24);
    popup.lines.assign(lines.begin(), lines.begin() + limit);
    popup.timeRemaining = 12.f;
    s_specialAbilityQueue.push_back(std::move(popup));

    if (s_specialAbilityQueue.size() > 4)
      s_specialAbilityQueue.erase(s_specialAbilityQueue.begin());
  }

  // ---------------------------------------------------------------------------
  // Tick – 백그라운드 스레드
  // ---------------------------------------------------------------------------
  void RoninMonitor_Tick(uintptr_t p1) {
    if (!bMonitorRonin) {
      if (s_wasEnabled) {
        ResetRoninMonitorState(true);
        s_wasEnabled = false;
      }
      return;
    }

    if (!s_wasEnabled) {
      ResetRoninMonitorState(true);
      s_wasEnabled = true;
    }

    // 주인공 주소 변경 = 세이브 로드/세션 변경으로 간주
    if (p1 != 0 && p1 != s_lastHeroAddr) {
      if (s_lastHeroAddr != 0) {
        AddLog(u8"[RoninMonitor] 주인공 주소 변경 감지 (0x%llX -> 0x%llX). 기준선 재구축.",
               (unsigned long long)s_lastHeroAddr, (unsigned long long)p1);
        ResetRoninMonitorState(true);
      }
      s_lastHeroAddr = p1;
    }

    if (IsInBattle())
      return;
    if (!IsConfigReady())
      return;
    if (!TryResolveBase(p1))
      return;

    uint8_t gameState = ReadRelevantGameState();
    if (gameState == 0)
      return;

    // 기능을 켠 직후에는 현재 상태를 기준선으로만 저장합니다.
    // 평정에서 켰더라도 신규 재야 알림을 소급해서 띄우지 않습니다.
    if (s_lastRelevantGameState == 0) {
      s_lastRelevantGameState = gameState;
      if (!s_initialized)
        ScanRonins(false);
      return;
    }

    if (gameState == s_lastRelevantGameState)
      return;

    const uint8_t prevState = s_lastRelevantGameState;
    s_lastRelevantGameState = gameState;

    // 실제 목적: 내정 -> 평정 전환 시에만 전체 무장을 비교하고 리스트를 표시합니다.
    if (prevState == kStateDomestic && gameState == kStateCouncil) {
      ScanRonins(s_initialized);
    }
  }

  // ---------------------------------------------------------------------------
  // Draw – UI 스레드
  // ---------------------------------------------------------------------------
  void RoninMonitor_Draw() {
    float dt = ImGui::GetIO().DeltaTime;
    if (dt > 0.1f)
      dt = 0.1f;

    std::vector<RoninNotification> roninSnap;
    std::vector<std::string> specialLines;
    bool showRonin = false;
    bool showSpecial = false;

    {
      std::lock_guard<std::mutex> lk(s_notifMtx);

      // 재야 알림은 체크 ON일 때만 타이머를 진행/표시합니다.
      if (bMonitorRonin) {
        for (auto &n : s_notifications)
          n.timeRemaining -= dt;
        s_notifications.erase(
            std::remove_if(
                s_notifications.begin(),
                s_notifications.end(),
                [](const RoninNotification &n) {
                  return n.timeRemaining <= 0.f;
                }),
            s_notifications.end());
        roninSnap = s_notifications;
      }

      showRonin = bMonitorRonin && !roninSnap.empty();

      // 특수능력 알림은 재야 체크박스와 무관합니다.
      // 실제 재야 팝업이 떠 있는 동안에는 타이머를 줄이지 않고 그대로 대기합니다.
      if (!showRonin && !s_specialAbilityQueue.empty()) {
        SpecialAbilityPopup &popup = s_specialAbilityQueue.front();
        popup.timeRemaining -= dt;
        if (popup.timeRemaining <= 0.f) {
          s_specialAbilityQueue.erase(s_specialAbilityQueue.begin());
        } else {
          specialLines = popup.lines;
          showSpecial = true;
        }
      }
    }

    if (!showRonin && !showSpecial)
      return;

    float sc = ImGui::GetIO().FontGlobalScale;
    ImVec2 disp = ImGui::GetIO().DisplaySize;

    // 특수능력 알림은 줄바꿈하지 않고 가장 긴 문자열에 맞춰 창 폭을 자동 확장합니다.
    float specialColumnWidth = 360.f * sc;
    if (showSpecial) {
      float maxTextWidth =
          ImGui::CalcTextSize(u8" [ 특수 능력 부여!! ]").x * 1.8f;
      for (const std::string &line : specialLines) {
        const float w = ImGui::CalcTextSize(line.c_str()).x * 1.8f;
        if (w > maxTextWidth)
          maxTextWidth = w;
      }

      // 좌우 패딩/테이블 여유분을 더하되 화면 밖으로 나가지는 않게 제한합니다.
      const float maxAllowed = (std::max)(360.f * sc, disp.x - 100.f * sc);
      specialColumnWidth =
          (std::min)(maxTextWidth + 30.f * sc, maxAllowed);
    }

    ImGui::SetNextWindowPos(
        ImVec2(disp.x - 20.f, disp.y * 0.12f),
        ImGuiCond_Always,
        ImVec2(1.f, 0.f));
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(0.f, 0.f, 0.f, 0.98f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(1.f, 0.84f, 0.f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 3.f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(30.f, 20.f));

    constexpr ImGuiWindowFlags kF =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##RoninMonitorNotif", nullptr, kF)) {
      ImGui::SetWindowFontScale(1.8f);

      ImGui::Separator();
      ImGui::PushStyleColor(
          ImGuiCol_Text,
          ImVec4(1.f, 1.f, 0.6f, 1.f));
      ImGui::TextUnformatted(
          showRonin
              ? u8" [ 재야 장수 발견!! ]"
              : u8" [ 특수 능력 부여!! ]");
      ImGui::PopStyleColor();
      ImGui::Separator();

      if (showRonin) {
        if (ImGui::BeginTable(
                "##RoninTable",
                2,
                ImGuiTableFlags_SizingFixedFit)) {
          ImGui::TableSetupColumn(
              u8"이름",
              ImGuiTableColumnFlags_WidthFixed,
              130.f * sc);
          ImGui::TableSetupColumn(
              u8"도시",
              ImGuiTableColumnFlags_WidthFixed,
              90.f * sc);

          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextColored(
              ImVec4(0.8f, 0.8f, 0.8f, 1.f),
              u8"이름");
          ImGui::TableSetColumnIndex(1);
          ImGui::TextColored(
              ImVec4(0.8f, 0.8f, 0.8f, 1.f),
              u8"도시");

          for (auto &n : roninSnap) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(
                ImVec4(0.3f, 1.f, 1.f, 1.f),
                u8"%s",
                n.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(
                ImVec4(1.f, 1.f, 0.6f, 1.f),
                u8"%s",
                n.cityName.c_str());
          }
          ImGui::EndTable();
        }
      } else {
        if (ImGui::BeginTable(
                "##SpecialAbilityTable",
                1,
                ImGuiTableFlags_SizingFixedFit)) {
          ImGui::TableSetupColumn(
              u8"부여 내역",
              ImGuiTableColumnFlags_WidthFixed,
              specialColumnWidth);

          for (const std::string &line : specialLines) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(
                ImVec4(0.3f, 1.f, 1.f, 1.f),
                u8"%s",
                line.c_str());
          }
          ImGui::EndTable();
        }
      }
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
  }

} // namespace DX11Base
