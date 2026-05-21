// =============================================================================
// CityInfoWindow.cpp  –  도시 정보 & 자동 환전 설정 창
// =============================================================================
#include "CityInfoWindow.h"
#include "../../Cheats.h"
#include "../../Framework/imgui.h"
#include "../../MenuState.h"
#include "../../pch.h"
#include "../../showlog.h"
#include "../../Config.h"
#include "CityData.h"
#include <windows.h>

namespace DX11Base {

  // ── 자동 환전 설정 변수 (Config.cpp에서 저장/로드) ────────────────────────
  int g_cityMaxGrainLimit = 200000; // 최대 군량 기준치 (MAX_GRAIN_LIMIT)
  int g_cityKeepGrain = 100000;     // 초과 시 남겨둘 군량 수치 (KEEP_GRAIN)
  int g_cityExchangeRate = 10;      // 1회 교환 비율 (EXCHANGE_RATE)
  bool g_cityAutoExchangeEnabled = false;

  // ── 내부 상태 ──────────────────────────────────────────────────────────────
  namespace {

    static uintptr_t s_cityArrBase = 0;
    static ULONGLONG s_lastResolveMs = 0;

    static bool SafeReadPtr(uintptr_t addr, uintptr_t *out) {
      __try {
        *out = *(uintptr_t *)addr;
        return (*out > 0x10000);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }
    static bool SafeRead16(uintptr_t addr, uint16_t *out) {
      __try {
        *out = *(uint16_t *)addr;
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }
    static bool SafeRead32(uintptr_t addr, uint32_t *out) {
      __try {
        *out = *(uint32_t *)addr;
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }
    static bool SafeWrite16(uintptr_t addr, uint16_t val) {
      DWORD old = 0, dummy = 0;
      if (!VirtualProtect((LPVOID)addr, 2, PAGE_READWRITE, &old))
        return false;
      __try {
        *(uint16_t *)addr = val;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        VirtualProtect((LPVOID)addr, 2, old, &dummy);
        return false;
      }
      VirtualProtect((LPVOID)addr, 2, old, &dummy);
      return true;
    }
    static bool SafeWrite32(uintptr_t addr, uint32_t val) {
      DWORD old = 0, dummy = 0;
      if (!VirtualProtect((LPVOID)addr, 4, PAGE_READWRITE, &old))
        return false;
      __try {
        *(uint32_t *)addr = val;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        VirtualProtect((LPVOID)addr, 4, old, &dummy);
        return false;
      }
      VirtualProtect((LPVOID)addr, 4, old, &dummy);
      return true;
    }

    // 도시 배열 베이스: exeBase+0x34C8630 → [+0] → [+0] → [+0]
    static uintptr_t GetCityArrBase() {
      ULONGLONG now = GetTickCount64();
      if (s_cityArrBase > 0x10000 && now - s_lastResolveMs < 3000)
        return s_cityArrBase;
      s_lastResolveMs = now;
      s_cityArrBase = 0;
      uintptr_t exe = (uintptr_t)GetModuleHandle(NULL);
      if (!exe)
        return 0;
      uintptr_t p1 = 0, p2 = 0, arr = 0;
      if (!SafeReadPtr(exe + 0x34C8630, &p1))
        return 0;
      if (!SafeReadPtr(p1, &p2))
        return 0;
      if (!SafeReadPtr(p2, &arr))
        return 0;
      arr += 0x28; // 디버그 모드의 "거주도시" 포인터와 동일한 Base를 사용하기 위해 +0x28 추가
      s_cityArrBase = arr;
      return arr;
    }

    // ── 선택된 도시 인덱스 & 편집 상태 ──────────────────────────────────────
    static int s_selIdx = 0;

    // 도시 오프셋 상수 (거주도시 포인터 기준, 즉 arr + 0x28 기준)
    static constexpr uintptr_t OFF_GOLD = 0xBC;     // 금 uint32
    static constexpr uintptr_t OFF_GRAIN = 0xC0;    // 군량 uint32
    static constexpr uintptr_t OFF_DEV_MAX = 0xA8;  // 개발한도 uint16 (이전 0xD0 - 0x28)
    static constexpr uintptr_t OFF_COM_MAX = 0xAC;  // 상업한도 uint16 (이전 0xD4 - 0x28)
    static constexpr uintptr_t OFF_DEF_MAX = 0xB0;  // 방어한도 uint16 (이전 0xD8 - 0x28)
    static constexpr uintptr_t OFF_TEC_MAX = 0xB4;  // 기술한도 uint16 (이전 0xDC - 0x28)
    static constexpr uintptr_t OFF_SOL_MAX = 0x100; // 병사한도 uint32 (ca - 0x28 + 0x100)

    // ── 상단: 자동 환전 UI ───────────────────────────────────────────────────
    static void DrawAutoExchangePanel(uintptr_t p1, float sc) {
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.11f, 0.17f, 1.f));
      ImGui::BeginChild("##CityTop", ImVec2(0.f, 125.f * sc), true);

      ImGui::TextColored(ImVec4(1.f, 0.82f, 0.28f, 1.f), u8"[ 자동 환전 설정 ]");
      // ImGui::SameLine(0.f, 20.f * sc);
      // ImGui::TextDisabled(u8"(내정 중 양식→금 자동 교환 설정)");
      ImGui::Separator();

      float fw = 95.f * sc;

      ImGui::TextUnformatted(u8"군량 한도");
      ImGui::SameLine(); // 다음 아이템을 같은 줄에 배치
      ImGui::SetNextItemWidth(fw);
      if (ImGui::InputInt(u8"##gl", &g_cityMaxGrainLimit, 0, 0)) {
        if (g_cityMaxGrainLimit < 0) g_cityMaxGrainLimit = 0;
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"군량이 이 값을 초과하면 자동으로 금으로 환전합니다.");
        ImGui::EndTooltip();
      }
      ImGui::SameLine();

      ImGui::TextUnformatted(u8"남길 군량");
      ImGui::SameLine(); // 다음 아이템을 같은 줄에 배치
      ImGui::SetNextItemWidth(fw);
      if (ImGui::InputInt(u8"##kg", &g_cityKeepGrain, 0, 0)) {
        if (g_cityKeepGrain < 0) g_cityKeepGrain = 0;
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"환전 시 이 수치만큼은 남기고 초과분만 환전합니다.");
        ImGui::EndTooltip();
      }
      ImGui::SameLine();

      ImGui::TextUnformatted(u8"환전 비율");
      ImGui::SameLine(); // 다음 아이템을 같은 줄에 배치
      ImGui::SetNextItemWidth(fw);
      if (ImGui::InputInt(u8"##er", &g_cityExchangeRate, 0, 0)) {
        if (g_cityExchangeRate < 1) g_cityExchangeRate = 1;
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"군량 N개당 1금으로 환전합니다. (예: 10 입력 시 10군량 -> 1금)");
        ImGui::EndTooltip();
      }
      ImGui::SameLine(0.f, 18.f * sc);

      if (ImGui::Checkbox(u8"자동 환전##autoex", &g_cityAutoExchangeEnabled)) {
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"매 턴 내정이 끝나고 평정이 시작될 때 위 조건에 따라 자동 환전합니다.");
        ImGui::EndTooltip();
      }

      ImGui::Separator();

      // 일괄 제어 버튼
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.3f, 0.3f, 1.f));
      if (ImGui::Button(u8"모든 도시 군량/금 최대화##maxall", ImVec2(200.f * sc, 0.f))) {
        MaximizeAllCityResources();
      }
      ImGui::SameLine(0.f, 10.f * sc);
      if (ImGui::Button(u8"모든 도시 병사 최대화##maxsol", ImVec2(180.f * sc, 0.f))) {
        MaximizeAllCitySoldierMax();
      }
      
      ImGui::Spacing();
      
      float limitBtnW = 135.f * sc;
      if (ImGui::Button(u8"모든 개발 최대화##maxdev", ImVec2(limitBtnW, 0.f))) {
        MaximizeAllCityDevMax();
      }
      ImGui::SameLine(0.f, 10.f * sc);
      if (ImGui::Button(u8"모든 상업 최대화##maxcom", ImVec2(limitBtnW, 0.f))) {
        MaximizeAllCityComMax();
      }
      ImGui::SameLine(0.f, 10.f * sc);
      if (ImGui::Button(u8"모든 방어 최대화##maxdef", ImVec2(limitBtnW, 0.f))) {
        MaximizeAllCityDefMax();
      }
      ImGui::SameLine(0.f, 10.f * sc);
      if (ImGui::Button(u8"모든 기술 최대화##maxtec", ImVec2(limitBtnW, 0.f))) {
        MaximizeAllCityTecMax();
      }
      ImGui::PopStyleColor(2);

      ImGui::EndChild();
      ImGui::PopStyleColor();
    }

    // ── 도시 스냅샷 캐시 (창 열 때 / 새로고침 버튼 클릭 시에만 읽음) ─────────
    struct CitySnap {
      int gold = 0, grain = 0, soldier = 0;
      int devMax = 0, comMax = 0, defMax = 0, tecMax = 0;
      bool valid = false;
    };
    static std::vector<CitySnap> s_snap;
    static bool s_snapDirty = true; // 창 첫 열림 시 읽기 트리거

    static void RefreshSnap(uintptr_t cityBase) {
      s_snap.assign(g_CityCount, CitySnap{});
      for (int i = 0; i < g_CityCount; i++) {
        uintptr_t ca = cityBase + (uintptr_t)i * 0x2A0;
        uint16_t dv = 0, cm = 0, df = 0, tc = 0;
        uint32_t gd = 0, gr = 0, sl = 0;
        SafeRead16(ca + OFF_DEV_MAX, &dv);
        SafeRead16(ca + OFF_COM_MAX, &cm);
        SafeRead16(ca + OFF_DEF_MAX, &df);
        SafeRead16(ca + OFF_TEC_MAX, &tc);
        SafeRead32(ca - 0x28 + OFF_GOLD, &gd);
        SafeRead32(ca - 0x28 + OFF_GRAIN, &gr);
        SafeRead32(ca - 0x28 + OFF_SOL_MAX, &sl);
        s_snap[i] = {(int)gd, (int)gr, (int)sl, dv, cm, df, tc, true};
      }
      s_snapDirty = false;
    }

    // ── 하단: 도시 리스트 테이블 ─────────────────────────────────────────────
    static void DrawCityTable(uintptr_t cityBase, float sc) {
      // 스냅샷이 필요하면 읽기
      if (s_snapDirty || (int)s_snap.size() != g_CityCount)
        RefreshSnap(cityBase);

      ImGui::Spacing();
      ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.85f, 1.f), u8"[ 도시 리스트 ]");
      // ImGui::SameLine();
      // ImGui::TextDisabled(u8"– Base: 0x%llX  Stride: 0x2A0", (unsigned long long)cityBase);
      ImGui::SameLine(0.f, 20.f * sc);
      if (ImGui::SmallButton(u8"새로고침")) {
        s_snapDirty = true;
        RefreshSnap(cityBase);
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"게임 메모리에서 최신 값을 다시 읽어옵니다.");
        ImGui::EndTooltip();
      }
      ImGui::SameLine(0.f, 20.f * sc);
      ImGui::TextDisabled(u8"입력 시 즉시 적용 | 새로고침 버튼으로 현재 값 재읽기");
      ImGui::Separator();

      static ImGuiTableFlags tf = ImGuiTableFlags_BordersInner | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;

      if (!ImGui::BeginTable("##CityTbl", 9, tf))
        return;

      ImGui::TableSetupScrollFreeze(2, 1);
      ImGui::TableSetupColumn(u8"#", ImGuiTableColumnFlags_WidthFixed, 28.f * sc);
      ImGui::TableSetupColumn(u8"도시명", ImGuiTableColumnFlags_WidthFixed, 72.f * sc);
      ImGui::TableSetupColumn(u8"금", ImGuiTableColumnFlags_WidthFixed, 85.f * sc);
      ImGui::TableSetupColumn(u8"군량", ImGuiTableColumnFlags_WidthFixed, 85.f * sc);
      ImGui::TableSetupColumn(u8"개발한도", ImGuiTableColumnFlags_WidthFixed, 75.f * sc);
      ImGui::TableSetupColumn(u8"상업한도", ImGuiTableColumnFlags_WidthFixed, 75.f * sc);
      ImGui::TableSetupColumn(u8"방어한도", ImGuiTableColumnFlags_WidthFixed, 75.f * sc);
      ImGui::TableSetupColumn(u8"기술한도", ImGuiTableColumnFlags_WidthFixed, 75.f * sc);
      ImGui::TableSetupColumn(u8"병사한도", ImGuiTableColumnFlags_WidthFixed, 85.f * sc);
      // ImGui::TableSetupColumn(u8"주소", ImGuiTableColumnFlags_WidthFixed, 100.f * sc);
      ImGui::TableHeadersRow();

      for (int i = 0; i < g_CityCount; i++) {
        if (i >= (int)s_snap.size() || !s_snap[i].valid)
          continue;
        uintptr_t ca = cityBase + (uintptr_t)i * 0x2A0;
        CitySnap &snap = s_snap[i];

        bool selected = (s_selIdx == i);
        ImGui::TableNextRow();
        if (selected)
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(30, 120, 100, 80));

        // # 컬럼 (클릭 시 해당 행 선택)
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%d", i);
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
          s_selIdx = i;

        // 도시명
        ImGui::TableSetColumnIndex(1);
        if (selected)
          ImGui::TextColored(ImVec4(0.3f, 1.f, 0.8f, 1.f), u8"%s", g_CityList[i].cityname);
        else
          ImGui::TextUnformatted(g_CityList[i].cityname);
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
          s_selIdx = i;

        // 금
        ImGui::TableSetColumnIndex(2);
        ImGui::PushID(i * 7 + 0);
        ImGui::SetNextItemWidth(80.f * sc);
        if (ImGui::InputInt("##gd", &snap.gold, 0, 0)) {
          snap.gold = snap.gold < 0 ? 0 : snap.gold;
          if (SafeWrite32(ca - 0x28 + OFF_GOLD, (uint32_t)snap.gold))
            AddLog(u8"[도시] %s 금 → %d", g_CityList[i].cityname, snap.gold);
        }
        // if (ImGui::IsItemHovered()) {
        //   ImGui::BeginTooltip();
        //   ImGui::TextDisabled(u8"주소: 0x%llX", (unsigned long long)(ca - 0x28 + OFF_GOLD));
        //   ImGui::EndTooltip();
        // }
        ImGui::PopID();

        // 군량
        ImGui::TableSetColumnIndex(3);
        ImGui::PushID(i * 7 + 1);
        ImGui::SetNextItemWidth(80.f * sc);
        if (ImGui::InputInt("##gr", &snap.grain, 0, 0)) {
          snap.grain = snap.grain < 0 ? 0 : snap.grain;
          if (SafeWrite32(ca - 0x28 + OFF_GRAIN, (uint32_t)snap.grain))
            AddLog(u8"[도시] %s 군량 → %d", g_CityList[i].cityname, snap.grain);
        }
        // if (ImGui::IsItemHovered()) {
        //   ImGui::BeginTooltip();
        //   ImGui::TextDisabled(u8"주소: 0x%llX", (unsigned long long)(ca - 0x28 + OFF_GRAIN));
        //   ImGui::EndTooltip();
        // }
        ImGui::PopID();

        // 개발한도
        ImGui::TableSetColumnIndex(4);
        ImGui::PushID(i * 7 + 2);
        ImGui::SetNextItemWidth(70.f * sc);
        if (ImGui::InputInt("##dv", &snap.devMax, 0, 0)) {
          snap.devMax = snap.devMax < 0 ? 0 : (snap.devMax > 30000 ? 30000 : snap.devMax);
          if (SafeWrite16(ca + OFF_DEV_MAX, (uint16_t)snap.devMax))
            AddLog(u8"[도시] %s 개발한도 → %d", g_CityList[i].cityname, snap.devMax);
        }
        ImGui::PopID();

        // 상업한도
        ImGui::TableSetColumnIndex(5);
        ImGui::PushID(i * 7 + 3);
        ImGui::SetNextItemWidth(70.f * sc);
        if (ImGui::InputInt("##cm", &snap.comMax, 0, 0)) {
          snap.comMax = snap.comMax < 0 ? 0 : (snap.comMax > 30000 ? 30000 : snap.comMax);
          if (SafeWrite16(ca + OFF_COM_MAX, (uint16_t)snap.comMax))
            AddLog(u8"[도시] %s 상업한도 → %d", g_CityList[i].cityname, snap.comMax);
        }
        ImGui::PopID();

        // 방어한도
        ImGui::TableSetColumnIndex(6);
        ImGui::PushID(i * 7 + 4);
        ImGui::SetNextItemWidth(70.f * sc);
        if (ImGui::InputInt("##df", &snap.defMax, 0, 0)) {
          snap.defMax = snap.defMax < 0 ? 0 : (snap.defMax > 30000 ? 30000 : snap.defMax);
          if (SafeWrite16(ca + OFF_DEF_MAX, (uint16_t)snap.defMax))
            AddLog(u8"[도시] %s 방어한도 → %d", g_CityList[i].cityname, snap.defMax);
        }
        ImGui::PopID();

        // 기술한도
        ImGui::TableSetColumnIndex(7);
        ImGui::PushID(i * 7 + 5);
        ImGui::SetNextItemWidth(70.f * sc);
        if (ImGui::InputInt("##tc", &snap.tecMax, 0, 0)) {
          snap.tecMax = snap.tecMax < 0 ? 0 : (snap.tecMax > 30000 ? 30000 : snap.tecMax);
          if (SafeWrite16(ca + OFF_TEC_MAX, (uint16_t)snap.tecMax))
            AddLog(u8"[도시] %s 기술한도 → %d", g_CityList[i].cityname, snap.tecMax);
        }
        ImGui::PopID();

        // 병사한도
        ImGui::TableSetColumnIndex(8);
        ImGui::PushID(i * 7 + 6);
        ImGui::SetNextItemWidth(80.f * sc);
        if (ImGui::InputInt("##sl", &snap.soldier, 0, 0)) {
          snap.soldier = snap.soldier < 0 ? 0 : snap.soldier;
          if (SafeWrite32(ca - 0x28 + OFF_SOL_MAX, (uint32_t)snap.soldier))
            AddLog(u8"[도시] %s 병사한도 → %d", g_CityList[i].cityname, snap.soldier);
        }
        // if (ImGui::IsItemHovered()) {
        //   ImGui::BeginTooltip();
        //   ImGui::TextDisabled(u8"주소: 0x%llX", (unsigned long long)(ca - 0x28 + OFF_SOL_MAX));
        //   ImGui::EndTooltip();
        // }
        ImGui::PopID();

        // 주소
        // ImGui::TableSetColumnIndex(9);
        // ImGui::TextDisabled("0x%llX", (unsigned long long)(ca - 0x28));
      }

      ImGui::EndTable();
    }

  } // anonymous namespace

  // ═══════════════════════════════════════════════════════════════════════════
  //  공개 API
  // ═══════════════════════════════════════════════════════════════════════════
  void DrawCityInfoWindow(uintptr_t p1, float scale) {
    if (!bShowCityInfoWin)
      return;

    ImGui::SetNextWindowSize(ImVec2(750.f * scale, 760.f * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(500.f * scale, 300.f * scale), ImVec2(1400.f * scale, 900.f * scale));

    if (!ImGui::Begin(u8"도시 정보###CityInfoWin", &bShowCityInfoWin, ImGuiWindowFlags_NoSavedSettings)) {
      ImGui::End();
      return;
    }

    // 상단: 자동 환전 패널
    DrawAutoExchangePanel(p1, scale);

    // 하단: 도시 리스트
    uintptr_t cityBase = GetCityArrBase();
    if (cityBase <= 0x10000) {
      ImGui::Spacing();
      ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f),
                         u8"도시 배열을 읽을 수 없습니다. 게임 플레이 화면에서 열어주세요.");
    } else {
      DrawCityTable(cityBase, scale);
    }

    ImGui::End();
  }

  void RunAutoCityExchange() {
    if (!g_cityAutoExchangeEnabled)
      return;

    uintptr_t cityBase = GetCityArrBase();
    if (cityBase <= 0x10000)
      return;

    int exchangeCount = 0;
    for (int i = 0; i < g_CityCount; i++) {
      uintptr_t ca = cityBase + (uintptr_t)i * 0x2A0;
      uint32_t currentGrain = 0;
      if (!SafeRead32(ca - 0x28 + OFF_GRAIN, &currentGrain))
        continue;

      if ((int)currentGrain > g_cityMaxGrainLimit) {
        int excessGrain = (int)currentGrain - g_cityKeepGrain;
        if (excessGrain > 0) {
          int gainGold = excessGrain / g_cityExchangeRate;

          uint32_t currentGold = 0;
          SafeRead32(ca - 0x28 + OFF_GOLD, &currentGold);

          // Update memory
          SafeWrite32(ca - 0x28 + OFF_GRAIN, (uint32_t)g_cityKeepGrain);
          SafeWrite32(ca - 0x28 + OFF_GOLD, currentGold + (uint32_t)gainGold);
          exchangeCount++;
        }
      }
    }

    if (exchangeCount > 0) {
      AddLog(u8"[자동화] %d개 도시에서 군량 초과분을 금으로 환전했습니다.", exchangeCount);
    }
  }

  void MaximizeAllCityResources() {
    uintptr_t cityBase = GetCityArrBase();
    if (cityBase <= 0x10000)
      return;

    const uint32_t MAX_VAL = 9999999;
    for (int i = 0; i < g_CityCount; i++) {
      uintptr_t ca = cityBase + (uintptr_t)i * 0x2A0;
      SafeWrite32(ca - 0x28 + OFF_GOLD, MAX_VAL);
      SafeWrite32(ca - 0x28 + OFF_GRAIN, MAX_VAL);
    }

    // Refresh snapshot so UI updates immediately
    s_snapDirty = true;
    AddLog(u8"[도시정보] 모든 도시의 금과 군량을 최대치로 설정했습니다.");
  }

  void MaximizeAllCityDevMax() {
    uintptr_t cityBase = GetCityArrBase();
    if (cityBase <= 0x10000) return;
    const uint16_t MAX_LIMIT = 30000;
    for (int i = 0; i < g_CityCount; i++) {
      uintptr_t ca = cityBase + (uintptr_t)i * 0x2A0;
      SafeWrite16(ca + OFF_DEV_MAX, MAX_LIMIT);
    }
    s_snapDirty = true;
    AddLog(u8"[도시정보] 모든 도시의 개발한도를 최대치로 설정했습니다.");
  }

  void MaximizeAllCityComMax() {
    uintptr_t cityBase = GetCityArrBase();
    if (cityBase <= 0x10000) return;
    const uint16_t MAX_LIMIT = 30000;
    for (int i = 0; i < g_CityCount; i++) {
      uintptr_t ca = cityBase + (uintptr_t)i * 0x2A0;
      SafeWrite16(ca + OFF_COM_MAX, MAX_LIMIT);
    }
    s_snapDirty = true;
    AddLog(u8"[도시정보] 모든 도시의 상업한도를 최대치로 설정했습니다.");
  }

  void MaximizeAllCityDefMax() {
    uintptr_t cityBase = GetCityArrBase();
    if (cityBase <= 0x10000) return;
    const uint16_t MAX_LIMIT = 30000;
    for (int i = 0; i < g_CityCount; i++) {
      uintptr_t ca = cityBase + (uintptr_t)i * 0x2A0;
      SafeWrite16(ca + OFF_DEF_MAX, MAX_LIMIT);
    }
    s_snapDirty = true;
    AddLog(u8"[도시정보] 모든 도시의 방어한도를 최대치로 설정했습니다.");
  }

  void MaximizeAllCityTecMax() {
    uintptr_t cityBase = GetCityArrBase();
    if (cityBase <= 0x10000) return;
    const uint16_t MAX_LIMIT = 30000;
    for (int i = 0; i < g_CityCount; i++) {
      uintptr_t ca = cityBase + (uintptr_t)i * 0x2A0;
      SafeWrite16(ca + OFF_TEC_MAX, MAX_LIMIT);
    }
    s_snapDirty = true;
    AddLog(u8"[도시정보] 모든 도시의 기술한도를 최대치로 설정했습니다.");
  }

  void MaximizeAllCitySoldierMax() {
    uintptr_t cityBase = GetCityArrBase();
    if (cityBase <= 0x10000) return;
    const uint32_t MAX_SOLDIER = 9999999;
    for (int i = 0; i < g_CityCount; i++) {
      uintptr_t ca = cityBase + (uintptr_t)i * 0x2A0;
      SafeWrite32(ca - 0x28 + OFF_SOL_MAX, MAX_SOLDIER);
    }
    s_snapDirty = true;
    AddLog(u8"[도시정보] 모든 도시의 병사한도를 최대치로 설정했습니다.");
  }

} // namespace DX11Base
