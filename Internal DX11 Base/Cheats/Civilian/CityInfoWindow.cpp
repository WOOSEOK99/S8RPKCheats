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
#include "../Officer/OfficerData.h"
#include "CityData.h"
#include <windows.h>
#include <string>
#include <vector>

namespace DX11Base {

  // ── 자동 환전 설정 변수 (Config.cpp에서 저장/로드) ────────────────────────
  int g_cityMaxGrainLimit = 200000; // 최대 군량 기준치 (MAX_GRAIN_LIMIT)
  int g_cityKeepGrain = 100000;     // 초과 시 남겨둘 군량 수치 (KEEP_GRAIN)
  int g_cityExchangeRate = 10;      // 1회 교환 비율 (EXCHANGE_RATE)
  bool g_cityAutoExchangeEnabled = false;
  bool g_cityRevoltAlwaysZero = false; // 모든 도시 반란 카운트(+0x105)를 항상 0으로 유지

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
    static bool SafeRead8(uintptr_t addr, uint8_t *out) {
      __try {
        *out = *(uint8_t *)addr;
        return true;
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
    static bool SafeWrite8(uintptr_t addr, uint8_t val) {
      DWORD old = 0, dummy = 0;
      if (!VirtualProtect((LPVOID)addr, 1, PAGE_READWRITE, &old))
        return false;
      __try {
        *(uint8_t *)addr = val;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        VirtualProtect((LPVOID)addr, 1, old, &dummy);
        return false;
      }
      VirtualProtect((LPVOID)addr, 1, old, &dummy);
      return true;
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
    // CT 기준 CityData + 0x105. 현재 ca는 원본 CityData보다 +0x28 이동된 기준이므로
    // 실제 접근 주소는 ca - 0x28 + OFF_REVOLT_RAW.
    static constexpr uintptr_t OFF_REVOLT_RAW = 0x105; // 반란 카운트 uint8

    // ── 상단: 자동 환전 UI ───────────────────────────────────────────────────
    static void DrawAutoExchangePanel(uintptr_t p1, float sc) {
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.11f, 0.17f, 1.f));
      ImGui::BeginChild("##CityTop", ImVec2(0.f, 158.f * sc), true);

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

      ImGui::Spacing();
      if (ImGui::Checkbox(u8"반란카운트 항상 0##revoltzero", &g_cityRevoltAlwaysZero)) {
        if (g_cityRevoltAlwaysZero)
          ResetAllCityRevoltCounters();
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"모든 도시의 CityData + 0x105 반란 카운트를 계속 0으로 유지합니다.");
        ImGui::EndTooltip();
      }

      ImGui::SameLine(0.f, 18.f * sc);
      if (ImGui::Button(u8"즉시 반란카운터 0으로 설정##revoltreset",
                        ImVec2(210.f * sc, 0.f))) {
        ResetAllCityRevoltCounters();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"현재 51개 도시의 반란 카운트를 즉시 0으로 설정합니다.");
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
      int revolt = 0;
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
        uint8_t rv = 0;
        SafeRead16(ca + OFF_DEV_MAX, &dv);
        SafeRead16(ca + OFF_COM_MAX, &cm);
        SafeRead16(ca + OFF_DEF_MAX, &df);
        SafeRead16(ca + OFF_TEC_MAX, &tc);
        SafeRead32(ca - 0x28 + OFF_GOLD, &gd);
        SafeRead32(ca - 0x28 + OFF_GRAIN, &gr);
        SafeRead32(ca - 0x28 + OFF_SOL_MAX, &sl);
        SafeRead8(ca - 0x28 + OFF_REVOLT_RAW, &rv);
        s_snap[i] = {(int)gd, (int)gr, (int)sl, dv, cm, df, tc, (int)rv, true};
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

      if (!ImGui::BeginTable("##CityTbl", 10, tf))
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
      ImGui::TableSetupColumn(u8"반란카운트", ImGuiTableColumnFlags_WidthFixed, 80.f * sc);
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

        // 반란 카운트 (CT: CityData + 0x105)
        ImGui::TableSetColumnIndex(9);
        ImGui::Text("%d", snap.revolt);

        // 주소
        // ImGui::TableSetColumnIndex(10);
        // ImGui::TextDisabled("0x%llX", (unsigned long long)(ca - 0x28));
      }

      ImGui::EndTable();
    }

    // ── 전선 분석: CityData 소유 세력 + 도시 연결망 기반 읽기 전용 판정 ─────
    // CT 기준:
    //   CityData + 0x98 -> [ptr + 0x18] = 소유 ForceData
    // 게시글 기준:
    //   CityData + 0x20부터 8바이트 x 6 = 인접 도시 CityData 포인터
    static constexpr uintptr_t OFF_CITY_FORCE_LINK_RAW = 0x98;
    static constexpr uintptr_t OFF_CITY_CONNECTION_RAW = 0x20;
    static constexpr uintptr_t OFF_CITY_TROOPS_RAW = 0xC4;
    static constexpr int CITY_CONNECTION_SLOTS = 6;
    static constexpr uintptr_t CITY_STRIDE = 0x2A0;

    struct FrontierFaction {
      uintptr_t forcePtr = 0;
      std::string name;
    };

    struct FrontierCityRow {
      int cityIndex = -1;
      bool frontline = false;
      std::vector<int> neighbors;
      std::vector<int> foreignNeighbors;
      int unknownConnections = 0;
      uint32_t gold = 0;
      uint32_t grain = 0;
      uint32_t troops = 0;
    };

    static std::vector<FrontierFaction> s_frontierFactions;
    static std::vector<FrontierCityRow> s_frontierRows;
    static uintptr_t s_frontierSelectedForce = 0;
    static uintptr_t s_frontierPlayerForce = 0;
    static bool s_frontierDirty = true;
    static int s_frontierFilter = 0; // 0=전체, 1=전선, 2=후방

    static uintptr_t GetRawCityBase(uintptr_t shiftedCityBase, int cityIndex) {
      if (shiftedCityBase <= 0x28 || cityIndex < 0 || cityIndex >= g_CityCount)
        return 0;
      return (shiftedCityBase - 0x28) + (uintptr_t)cityIndex * CITY_STRIDE;
    }

    static uintptr_t GetCityForcePtr(uintptr_t rawCity) {
      if (rawCity <= 0x10000)
        return 0;

      uintptr_t ownerLink = 0;
      if (!SafeReadPtr(rawCity + OFF_CITY_FORCE_LINK_RAW, &ownerLink))
        return 0;

      uintptr_t forcePtr = 0;
      if (!SafeReadPtr(ownerLink + 0x18, &forcePtr))
        return 0;

      return forcePtr;
    }

    static int ConnectionPtrToCityIndex(uintptr_t rawCityArrayBase, uintptr_t cityPtr) {
      if (rawCityArrayBase <= 0x10000 || cityPtr < rawCityArrayBase)
        return -1;

      const uintptr_t delta = cityPtr - rawCityArrayBase;
      if ((delta % CITY_STRIDE) != 0)
        return -1;

      const int idx = (int)(delta / CITY_STRIDE);
      if (idx < 0 || idx >= g_CityCount)
        return -1;
      return idx;
    }

    static std::string BuildForceName(uintptr_t forcePtr) {
      if (!forcePtr)
        return u8"공백지";

      uintptr_t lordPtr = 0;
      if (SafeReadPtr(forcePtr + 0xC0, &lordPtr)) {
        uint16_t lordId = 0;
        if (SafeRead16(lordPtr + 0x08, &lordId)) {
          auto it = g_officerNames.find((int)lordId);
          if (it != g_officerNames.end() && !it->second.empty())
            return it->second + u8" 세력";

          return u8"무장 ID " + std::to_string((int)lordId) + u8" 세력";
        }
      }

      char buf[64];
      sprintf_s(buf, u8"세력 0x%llX", (unsigned long long)forcePtr);
      return buf;
    }

    static const char *FindForceName(uintptr_t forcePtr) {
      if (!forcePtr)
        return u8"공백지";

      for (const auto &f : s_frontierFactions) {
        if (f.forcePtr == forcePtr)
          return f.name.c_str();
      }
      return u8"미확인 세력";
    }

    static void RefreshFrontierAnalysis(uintptr_t p1, uintptr_t shiftedCityBase) {
      s_frontierFactions.clear();
      s_frontierRows.clear();
      s_frontierPlayerForce = 0;

      if (shiftedCityBase <= 0x10000) {
        s_frontierDirty = false;
        return;
      }

      LoadOfficerNames();

      const uintptr_t rawCityArrayBase = shiftedCityBase - 0x28;
      std::vector<uintptr_t> cityForces(g_CityCount, 0);

      // 1) 51개 도시의 소유 세력 수집
      for (int i = 0; i < g_CityCount; ++i) {
        const uintptr_t rawCity = GetRawCityBase(shiftedCityBase, i);
        cityForces[i] = GetCityForcePtr(rawCity);

        const uintptr_t forcePtr = cityForces[i];
        if (!forcePtr)
          continue;

        bool exists = false;
        for (const auto &f : s_frontierFactions) {
          if (f.forcePtr == forcePtr) {
            exists = true;
            break;
          }
        }
        if (!exists) {
          FrontierFaction f;
          f.forcePtr = forcePtr;
          f.name = BuildForceName(forcePtr);
          s_frontierFactions.push_back(std::move(f));
        }
      }

      // 2) 주인공 현재 도시를 통해 주인공 소속 세력을 얻음
      if (p1 > 0x10000) {
        uintptr_t playerCity = 0;
        if (SafeReadPtr(p1 + 0x20, &playerCity))
          s_frontierPlayerForce = GetCityForcePtr(playerCity);
      }

      // 최초에는 주인공 세력을 자동 선택. 선택 세력이 사라졌으면 다시 선택.
      bool selectedStillExists = false;
      for (const auto &f : s_frontierFactions) {
        if (f.forcePtr == s_frontierSelectedForce) {
          selectedStillExists = true;
          break;
        }
      }
      if (!selectedStillExists) {
        s_frontierSelectedForce = 0;
        for (const auto &f : s_frontierFactions) {
          if (f.forcePtr == s_frontierPlayerForce) {
            s_frontierSelectedForce = s_frontierPlayerForce;
            break;
          }
        }
        if (!s_frontierSelectedForce && !s_frontierFactions.empty())
          s_frontierSelectedForce = s_frontierFactions.front().forcePtr;
      }

      // 3) 선택 세력 도시만 대상으로 인접 도시의 소유 세력을 비교해 전선/후방 판정
      if (s_frontierSelectedForce) {
        for (int i = 0; i < g_CityCount; ++i) {
          if (cityForces[i] != s_frontierSelectedForce)
            continue;

          const uintptr_t rawCity = GetRawCityBase(shiftedCityBase, i);
          FrontierCityRow row;
          row.cityIndex = i;

          for (int slot = 0; slot < CITY_CONNECTION_SLOTS; ++slot) {
            uintptr_t connectedPtr = 0;
            const uintptr_t slotAddr =
                rawCity + OFF_CITY_CONNECTION_RAW + (uintptr_t)slot * sizeof(uintptr_t);

            // 빈 슬롯(0)은 정상적인 미사용 슬롯.
            __try {
              connectedPtr = *(uintptr_t *)slotAddr;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
              connectedPtr = 0;
            }

            if (!connectedPtr)
              continue;

            const int connectedIdx = ConnectionPtrToCityIndex(rawCityArrayBase, connectedPtr);
            if (connectedIdx < 0) {
              row.unknownConnections++;
              continue;
            }

            row.neighbors.push_back(connectedIdx);
            if (cityForces[connectedIdx] != s_frontierSelectedForce) {
              row.frontline = true;
              row.foreignNeighbors.push_back(connectedIdx);
            }
          }

          SafeRead32(rawCity + OFF_GOLD, &row.gold);
          SafeRead32(rawCity + OFF_GRAIN, &row.grain);
          SafeRead32(rawCity + OFF_CITY_TROOPS_RAW, &row.troops);
          s_frontierRows.push_back(std::move(row));
        }
      }

      s_frontierDirty = false;
    }

    static std::string BuildCityNameList(const std::vector<int> &indices) {
      std::string out;
      for (int idx : indices) {
        if (idx < 0 || idx >= g_CityCount)
          continue;
        if (!out.empty())
          out += ", ";
        out += g_CityList[idx].cityname;
      }
      return out.empty() ? "-" : out;
    }

    static std::string BuildForeignCityList(uintptr_t shiftedCityBase,
                                            const std::vector<int> &indices) {
      std::string out;
      for (int idx : indices) {
        if (idx < 0 || idx >= g_CityCount)
          continue;

        const uintptr_t rawCity = GetRawCityBase(shiftedCityBase, idx);
        const uintptr_t forcePtr = GetCityForcePtr(rawCity);

        if (!out.empty())
          out += ", ";
        out += g_CityList[idx].cityname;
        out += "(";
        out += FindForceName(forcePtr);
        out += ")";
      }
      return out.empty() ? "-" : out;
    }

    static void DrawFrontierAnalysis(uintptr_t p1, uintptr_t shiftedCityBase, float sc) {
      if (s_frontierDirty)
        RefreshFrontierAnalysis(p1, shiftedCityBase);

      ImGui::Spacing();
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.f), u8"[ 전선 분석 ]");
      ImGui::SameLine(0.f, 18.f * sc);

      const char *selectedName = u8"세력 없음";
      for (const auto &f : s_frontierFactions) {
        if (f.forcePtr == s_frontierSelectedForce) {
          selectedName = f.name.c_str();
          break;
        }
      }

      ImGui::SetNextItemWidth(180.f * sc);
      if (ImGui::BeginCombo("##FrontierFaction", selectedName)) {
        for (const auto &f : s_frontierFactions) {
          const bool selected = (f.forcePtr == s_frontierSelectedForce);
          if (ImGui::Selectable(f.name.c_str(), selected)) {
            s_frontierSelectedForce = f.forcePtr;
            RefreshFrontierAnalysis(p1, shiftedCityBase);
          }
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      if (s_frontierPlayerForce) {
        ImGui::SameLine(0.f, 8.f * sc);
        if (ImGui::SmallButton(u8"주인공 세력")) {
          s_frontierSelectedForce = s_frontierPlayerForce;
          RefreshFrontierAnalysis(p1, shiftedCityBase);
        }
      }

      ImGui::SameLine(0.f, 8.f * sc);
      if (ImGui::SmallButton(u8"새로고침##frontier")) {
        RefreshFrontierAnalysis(p1, shiftedCityBase);
      }

      int total = 0, front = 0, rear = 0;
      for (const auto &row : s_frontierRows) {
        total++;
        if (row.frontline)
          front++;
        else
          rear++;
      }

      ImGui::SameLine(0.f, 18.f * sc);
      ImGui::TextDisabled(u8"전체 %d / 전선 %d / 후방 %d", total, front, rear);

      if (ImGui::RadioButton(u8"전체##FrontierAll", s_frontierFilter == 0))
        s_frontierFilter = 0;
      ImGui::SameLine();
      if (ImGui::RadioButton(u8"전선##FrontierFront", s_frontierFilter == 1))
        s_frontierFilter = 1;
      ImGui::SameLine();
      if (ImGui::RadioButton(u8"후방##FrontierRear", s_frontierFilter == 2))
        s_frontierFilter = 2;

      ImGui::SameLine(0.f, 20.f * sc);
      ImGui::TextDisabled(u8"연결 도시 중 하나라도 다른 세력/공백지이면 전선");

      ImGui::Separator();

      static ImGuiTableFlags frontierFlags =
          ImGuiTableFlags_BordersInner | ImGuiTableFlags_RowBg |
          ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit |
          ImGuiTableFlags_NoSavedSettings;

      if (!ImGui::BeginTable("##FrontierCityTbl", 7, frontierFlags))
        return;

      ImGui::TableSetupScrollFreeze(2, 1);
      ImGui::TableSetupColumn(u8"도시", ImGuiTableColumnFlags_WidthFixed, 72.f * sc);
      ImGui::TableSetupColumn(u8"구분", ImGuiTableColumnFlags_WidthFixed, 55.f * sc);
      ImGui::TableSetupColumn(u8"접경 도시", ImGuiTableColumnFlags_WidthStretch, 1.1f);
      ImGui::TableSetupColumn(u8"타세력/공백지 접경", ImGuiTableColumnFlags_WidthStretch, 1.4f);
      ImGui::TableSetupColumn(u8"금", ImGuiTableColumnFlags_WidthFixed, 82.f * sc);
      ImGui::TableSetupColumn(u8"군량", ImGuiTableColumnFlags_WidthFixed, 92.f * sc);
      ImGui::TableSetupColumn(u8"병사", ImGuiTableColumnFlags_WidthFixed, 82.f * sc);
      ImGui::TableHeadersRow();

      for (const auto &row : s_frontierRows) {
        if (s_frontierFilter == 1 && !row.frontline)
          continue;
        if (s_frontierFilter == 2 && row.frontline)
          continue;

        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(g_CityList[row.cityIndex].cityname);

        ImGui::TableSetColumnIndex(1);
        if (row.frontline)
          ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.2f, 1.f), u8"전선");
        else
          ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.f), u8"후방");

        ImGui::TableSetColumnIndex(2);
        const std::string neighbors = BuildCityNameList(row.neighbors);
        ImGui::TextUnformatted(neighbors.c_str());
        if (row.unknownConnections > 0 && ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::Text(u8"도시 포인터로 해석되지 않은 연결 슬롯: %d개", row.unknownConnections);
          ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(3);
        const std::string foreign = BuildForeignCityList(shiftedCityBase, row.foreignNeighbors);
        ImGui::TextUnformatted(foreign.c_str());

        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%u", row.gold);

        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%u", row.grain);

        ImGui::TableSetColumnIndex(6);
        ImGui::Text("%u", row.troops);
      }

      ImGui::EndTable();
    }

  } // anonymous namespace

  // ═══════════════════════════════════════════════════════════════════════════
  //  공개 API
  // ═══════════════════════════════════════════════════════════════════════════
  void DrawCityInfoWindow(uintptr_t p1, float scale) {
    if (!bShowCityInfoWin) {
      s_frontierDirty = true;
      return;
    }

    ImGui::SetNextWindowSize(ImVec2(850.f * scale, 790.f * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(650.f * scale, 360.f * scale), ImVec2(1500.f * scale, 950.f * scale));

    if (!ImGui::Begin(u8"도시 정보###CityInfoWin", &bShowCityInfoWin, ImGuiWindowFlags_NoSavedSettings)) {
      ImGui::End();
      return;
    }

    // 상단: 자동 환전 패널
    DrawAutoExchangePanel(p1, scale);

    // 하단: 기존 도시 리스트 / 읽기 전용 전선 분석
    uintptr_t cityBase = GetCityArrBase();
    if (cityBase <= 0x10000) {
      ImGui::Spacing();
      ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f),
                         u8"도시 배열을 읽을 수 없습니다. 게임 플레이 화면에서 열어주세요.");
    } else if (ImGui::BeginTabBar("##CityInfoTabs")) {
      if (ImGui::BeginTabItem(u8"도시 리스트")) {
        DrawCityTable(cityBase, scale);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem(u8"전선 분석")) {
        DrawFrontierAnalysis(p1, cityBase, scale);
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }

    ImGui::End();
  }

  void ResetAllCityRevoltCounters() {
    uintptr_t cityBase = GetCityArrBase();
    if (cityBase <= 0x10000)
      return;

    int changed = 0;
    for (int i = 0; i < g_CityCount; i++) {
      uintptr_t ca = cityBase + (uintptr_t)i * 0x2A0;
      uint8_t current = 0;
      const uintptr_t revoltAddr = ca - 0x28 + OFF_REVOLT_RAW;
      if (!SafeRead8(revoltAddr, &current))
        continue;
      if (current != 0 && SafeWrite8(revoltAddr, 0))
        changed++;
    }

    s_snapDirty = true;
    AddLog(u8"[도시] 모든 도시 반란 카운트 0 설정 완료 (변경 %d개)", changed);
  }

  void RunCityRevoltAlwaysZero() {
    if (!g_cityRevoltAlwaysZero)
      return;

    uintptr_t cityBase = GetCityArrBase();
    if (cityBase <= 0x10000)
      return;

    bool changed = false;
    for (int i = 0; i < g_CityCount; i++) {
      uintptr_t ca = cityBase + (uintptr_t)i * 0x2A0;
      uint8_t current = 0;
      const uintptr_t revoltAddr = ca - 0x28 + OFF_REVOLT_RAW;
      if (!SafeRead8(revoltAddr, &current))
        continue;
      if (current != 0 && SafeWrite8(revoltAddr, 0))
        changed = true;
    }

    if (changed)
      s_snapDirty = true;
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
