// =============================================================================
// RoninMonitor.cpp  –  재야 장수 / 장수 변동 자동 감시 모듈
//
// [작동 방식]
//   1. 내정(0x07) -> 평정(0x05): 기존 재야 장수 등장 알림.
//   2. 평정 시작 시 전체 장수의 상태/세력/도시를 장수 변동 기준선으로 저장.
//   3. 평정(0x05) -> 내정(0x07): 기준선과 비교해 새 사망 및 세력 변경 장수를 알림.
//   4. 모든 팝업은 기존 RoninMonitor ImGui 창 하나를 공용으로 사용.
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
    constexpr uint8_t kStatusDead = 0x88;

    struct RoninNotification {
      std::string name;
      std::string cityName;
      float timeRemaining;
    };

    struct OfficerChangeSnapshot {
      bool valid = false;
      uint8_t status = 0;
      uintptr_t forcePtr = 0;
      uintptr_t cityPtr = 0;
    };

    struct SharedPopup {
      std::string title;
      std::vector<std::string> lines;
      float timeRemaining = 12.f;
    };

    static uintptr_t s_lastHeroAddr = 0;
    static uintptr_t s_arrayBase = 0;
    static uintptr_t s_cityBase = 0;
    static bool s_baseResolved = false;
    static bool s_initialized = false;
    static bool s_changeBaselineInitialized = false;
    static bool s_wasEnabled = false;
    static uint8_t s_lastRelevantGameState = 0;

    // ID 1~5102의 직전 평정 기준 재야 여부
    static bool s_isRonin[5103] = {false};

    // 평정 시작 시점 기준 상태/세력/도시
    static OfficerChangeSnapshot s_changeBaseline[5103]{};

    static std::mutex s_notifMtx;
    static std::vector<RoninNotification> s_notifications;
    static std::vector<SharedPopup> s_sharedPopupQueue;
  } // namespace

  void RoninMonitor_UpdatePrevStatus(int id, uint8_t st) {
    if (id >= 1 && id <= 5102)
      s_isRonin[id] = (st == 0x58);
  }

  static void ResetRoninMonitorState(bool clearNotifications) {
    s_initialized = false;
    s_changeBaselineInitialized = false;
    s_baseResolved = false;
    s_arrayBase = 0;
    s_cityBase = 0;
    s_lastRelevantGameState = 0;
    std::memset(s_isRonin, 0, sizeof(s_isRonin));
    std::memset(s_changeBaseline, 0, sizeof(s_changeBaseline));

    if (clearNotifications) {
      std::lock_guard<std::mutex> lk(s_notifMtx);
      s_notifications.clear();
      s_sharedPopupQueue.clear();
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

  // SEH는 C++ 소멸자가 있는 함수 안에서 사용할 수 없으므로 원시 읽기만 별도 헬퍼로 분리합니다.
  static bool SafeReadOfficerScanFields(
      uintptr_t addr,
      uint16_t* outId,
      uint8_t* outStatus,
      uintptr_t* outForcePtr,
      uintptr_t* outCityPtr) {
    if (!outId || !outStatus || !outForcePtr || !outCityPtr)
      return false;

    __try {
      *outId = *(uint16_t *)(addr + 0x08);
      *outStatus = *(uint8_t *)(addr + 0x10);
      *outForcePtr = *(uintptr_t *)(addr + 0x18);
      *outCityPtr = *(uintptr_t *)(addr + 0x20);
      return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      *outId = 0;
      *outStatus = 0;
      *outForcePtr = 0;
      *outCityPtr = 0;
      return false;
    }
  }

  static bool SafeReadForceLordId(uintptr_t forcePtr, uint16_t* outLordId) {
    if (!outLordId || forcePtr <= 0x10000)
      return false;

    __try {
      uintptr_t lordPtr = *(uintptr_t *)(forcePtr + 0xC0);
      if (lordPtr <= 0x10000)
        return false;
      uint16_t id = *(uint16_t *)(lordPtr + 0x08);
      if (id < 1 || id > 5102)
        return false;
      *outLordId = id;
      return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
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

  static std::string OfficerName(uint16_t id) {
    auto it = g_officerNames.find((int)id);
    if (it != g_officerNames.end() && !it->second.empty())
      return it->second;
    return u8"미등록 무장(ID:" + std::to_string((int)id) + u8")";
  }

  static std::string CityName(uintptr_t cityPtr) {
    uintptr_t cityBase = ResolveCityBaseOnce();
    if (cityBase > 0x10000 && cityPtr >= cityBase) {
      uintptr_t diff = cityPtr - cityBase;
      if ((diff % 0x2A0) == 0) {
        int idx = (int)(diff / 0x2A0);
        if (idx >= 0 && idx < g_CityCount)
          return g_CityList[idx].cityname;
      }
    }
    return u8"도시 미확인";
  }

  static std::string ForceName(uintptr_t forcePtr) {
    if (forcePtr <= 0x10000)
      return u8"무소속";

    uint16_t lordId = 0;
    if (SafeReadForceLordId(forcePtr, &lordId))
      return OfficerName(lordId) + u8" 세력";

    return u8"세력 미확인";
  }

  static void ScanRonins(bool notifyNew) {
    if (s_arrayBase <= 0x10000)
      return;

    std::vector<RoninNotification> found;
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
      uintptr_t forcePtr = 0;
      uintptr_t cityPtr = 0;
      if (!SafeReadOfficerScanFields(addr, &realID, &status, &forcePtr, &cityPtr))
        continue;

      if (realID == 0 || realID > 5102 || seenThisTick[realID])
        continue;
      seenThisTick[realID] = true;

      const bool isRonin = (status == 0x58);
      currentRonin[realID] = isRonin;

      if (notifyNew && isRonin && !s_isRonin[realID]) {
        found.push_back({OfficerName(realID), CityName(cityPtr), 12.f});
      }
    }

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

  static void CaptureOfficerChangeBaseline() {
    if (s_arrayBase <= 0x10000)
      return;

    OfficerChangeSnapshot next[5103]{};
    bool seenThisTick[5103] = {false};
    uintptr_t lastPage = 0;
    bool pageOk = false;

    for (int i = 1; i <= 5102; ++i) {
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

      uint16_t id = 0;
      uint8_t status = 0;
      uintptr_t forcePtr = 0;
      uintptr_t cityPtr = 0;
      if (!SafeReadOfficerScanFields(addr, &id, &status, &forcePtr, &cityPtr))
        continue;
      if (id < 1 || id > 5102 || seenThisTick[id])
        continue;

      seenThisTick[id] = true;
      next[id].valid = true;
      next[id].status = status;
      next[id].forcePtr = forcePtr;
      next[id].cityPtr = cityPtr;
    }

    std::memcpy(s_changeBaseline, next, sizeof(s_changeBaseline));
    s_changeBaselineInitialized = true;
    AddLog(u8"[장수변동] 평정 시작 기준선 저장 완료");
  }

  static void ScanOfficerChangesAndNotify() {
    if (s_arrayBase <= 0x10000 || !s_changeBaselineInitialized)
      return;

    LoadOfficerNames();

    std::vector<std::string> deadLines;
    std::vector<std::string> recruitLines;
    OfficerChangeSnapshot next[5103]{};
    bool seenThisTick[5103] = {false};
    uintptr_t lastPage = 0;
    bool pageOk = false;

    for (int i = 1; i <= 5102; ++i) {
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

      uint16_t id = 0;
      uint8_t status = 0;
      uintptr_t forcePtr = 0;
      uintptr_t cityPtr = 0;
      if (!SafeReadOfficerScanFields(addr, &id, &status, &forcePtr, &cityPtr))
        continue;
      if (id < 1 || id > 5102 || seenThisTick[id])
        continue;

      seenThisTick[id] = true;
      next[id].valid = true;
      next[id].status = status;
      next[id].forcePtr = forcePtr;
      next[id].cityPtr = cityPtr;

      const OfficerChangeSnapshot &before = s_changeBaseline[id];
      if (!before.valid)
        continue;

      if (before.status != kStatusDead && status == kStatusDead) {
        std::string line =
            u8"사망  " + OfficerName(id) +
            u8"  [" + ForceName(before.forcePtr) +
            u8" / " + CityName(before.cityPtr) + u8"]";
        deadLines.push_back(line);
        AddLog(u8"[장수변동] 사망: %s / 직전 %s / %s",
               OfficerName(id).c_str(),
               ForceName(before.forcePtr).c_str(),
               CityName(before.cityPtr).c_str());
        continue;
      }

      // 사망자는 세력 포인터가 0으로 바뀌는 경우가 있으므로 위에서 제외합니다.
      if (status != kStatusDead &&
          before.forcePtr != forcePtr &&
          forcePtr > 0x10000) {
        std::string line =
            u8"등용  " + OfficerName(id) +
            u8"  " + ForceName(before.forcePtr) +
            u8" → " + ForceName(forcePtr) +
            u8" / " + CityName(cityPtr);
        recruitLines.push_back(line);
        AddLog(u8"[장수변동] 등용/세력변경: %s / %s -> %s / %s",
               OfficerName(id).c_str(),
               ForceName(before.forcePtr).c_str(),
               ForceName(forcePtr).c_str(),
               CityName(cityPtr).c_str());
      }
    }

    std::memcpy(s_changeBaseline, next, sizeof(s_changeBaseline));
    s_changeBaselineInitialized = true;

    if (deadLines.empty() && recruitLines.empty()) {
      AddLog(u8"[장수변동] 평정 종료 확인: 사망/등용 변동 없음");
      return;
    }

    std::vector<std::string> lines;
    lines.reserve(deadLines.size() + recruitLines.size());
    lines.insert(lines.end(), recruitLines.begin(), recruitLines.end());
    lines.insert(lines.end(), deadLines.begin(), deadLines.end());

    RoninMonitor_QueueSharedNotice(u8" [ 사망장수 및 등용장수 ]", lines);
    AddLog(u8"[장수변동] 평정 종료 알림 큐 등록: 등용 %zu명 / 사망 %zu명",
           recruitLines.size(), deadLines.size());
  }

  void RoninMonitor_QueueSharedNotice(
      const std::string& title,
      const std::vector<std::string>& lines) {
    if (lines.empty())
      return;

    std::lock_guard<std::mutex> lk(s_notifMtx);

    SharedPopup popup;
    popup.title = title;
    const size_t limit = (std::min<size_t>)(lines.size(), 24);
    popup.lines.assign(lines.begin(), lines.begin() + limit);
    popup.timeRemaining = 12.f;
    s_sharedPopupQueue.push_back(std::move(popup));

    if (s_sharedPopupQueue.size() > 4)
      s_sharedPopupQueue.erase(s_sharedPopupQueue.begin());
  }

  void RoninMonitor_QueueSpecialAbilityNotice(const std::vector<std::string>& lines) {
    RoninMonitor_QueueSharedNotice(u8" [ 특수 능력 부여!! ]", lines);
  }

  // ---------------------------------------------------------------------------
  // Tick – 백그라운드 스레드
  // ---------------------------------------------------------------------------
  void RoninMonitor_Tick(uintptr_t p1) {
    const bool anyMonitorEnabled = bMonitorRonin || bOfficerChangeNotify;
    if (!anyMonitorEnabled) {
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

    // 개별 체크박스를 끈 동안에는 해당 기준선을 무효화합니다.
    // 같은 세션에서 다시 켰을 때 과거 상태를 소급 비교하지 않고 현재 상태부터 새로 시작합니다.
    if (!bMonitorRonin)
      s_initialized = false;
    if (!bOfficerChangeNotify)
      s_changeBaselineInitialized = false;

    // 기능을 켠 직후에는 현재 상태를 기준선으로만 저장합니다.
    if (s_lastRelevantGameState == 0) {
      s_lastRelevantGameState = gameState;
      if (bMonitorRonin && !s_initialized)
        ScanRonins(false);
      if (bOfficerChangeNotify && !s_changeBaselineInitialized)
        CaptureOfficerChangeBaseline();
      return;
    }

    // 다른 모니터가 이미 동작 중인 상태에서 체크박스를 새로 켠 경우도
    // 현재 상태를 즉시 기준선으로 잡아 다음 전환부터 정상 비교합니다.
    if (bMonitorRonin && !s_initialized)
      ScanRonins(false);
    if (bOfficerChangeNotify && !s_changeBaselineInitialized)
      CaptureOfficerChangeBaseline();

    if (gameState == s_lastRelevantGameState)
      return;

    const uint8_t prevState = s_lastRelevantGameState;
    s_lastRelevantGameState = gameState;

    if (prevState == kStateDomestic && gameState == kStateCouncil) {
      // 기존 재야 장수 알림.
      if (bMonitorRonin)
        ScanRonins(s_initialized);

      // 이번 평정 동안 발생할 사망/등용 비교용 기준선.
      if (bOfficerChangeNotify)
        CaptureOfficerChangeBaseline();
      else
        s_changeBaselineInitialized = false;

      return;
    }

    if (prevState == kStateCouncil && gameState == kStateDomestic) {
      if (bOfficerChangeNotify)
        ScanOfficerChangesAndNotify();
      else
        s_changeBaselineInitialized = false;
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
    std::vector<std::string> sharedLines;
    std::string sharedTitle;
    bool showRonin = false;
    bool showShared = false;

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

      // 공용 알림은 재야 팝업이 떠 있는 동안 타이머를 줄이지 않고 뒤에서 대기합니다.
      if (!showRonin && !s_sharedPopupQueue.empty()) {
        SharedPopup &popup = s_sharedPopupQueue.front();
        popup.timeRemaining -= dt;
        if (popup.timeRemaining <= 0.f) {
          s_sharedPopupQueue.erase(s_sharedPopupQueue.begin());
        } else {
          sharedTitle = popup.title;
          sharedLines = popup.lines;
          showShared = true;
        }
      }
    }

    if (!showRonin && !showShared)
      return;

    float sc = ImGui::GetIO().FontGlobalScale;
    ImVec2 disp = ImGui::GetIO().DisplaySize;

    float sharedColumnWidth = 360.f * sc;
    if (showShared) {
      float maxTextWidth = ImGui::CalcTextSize(sharedTitle.c_str()).x * 1.8f;
      for (const std::string &line : sharedLines) {
        const float w = ImGui::CalcTextSize(line.c_str()).x * 1.8f;
        if (w > maxTextWidth)
          maxTextWidth = w;
      }

      const float maxAllowed = (std::max)(360.f * sc, disp.x - 100.f * sc);
      sharedColumnWidth =
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
              : sharedTitle.c_str());
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
                "##SharedNotificationTable",
                1,
                ImGuiTableFlags_SizingFixedFit)) {
          ImGui::TableSetupColumn(
              u8"내역",
              ImGuiTableColumnFlags_WidthFixed,
              sharedColumnWidth);

          for (const std::string &line : sharedLines) {
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
