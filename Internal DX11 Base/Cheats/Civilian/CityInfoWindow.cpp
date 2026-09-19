// =============================================================================
// CityInfoWindow.cpp  –  도시 정보 & 자동 환전 설정 창
// =============================================================================
#include "CityInfoWindow.h"
#include "../../Cheats.h"
#include "../../Framework/imgui.h"
#include "../../MenuState.h"
#include "../../NotificationManager.h"
#include "../../pch.h"
#include "../../showlog.h"
#include "../../Config.h"
#include "../Officer/OfficerData.h"
#include "../Officer/OfficerRosterResolve.h"
#include "../System/MonthCapture.h"
#include "CityData.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <fstream>
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

    // 0도 정상값으로 허용하는 포인터/QWORD 읽기.
    // __try는 C++ 소멸자가 있는 함수 안에서 사용할 수 없으므로 별도 helper로 분리한다.
    static bool SafeReadPtrAllowZero(uintptr_t addr, uintptr_t *out) {
      if (!out)
        return false;
      __try {
        *out = *(uintptr_t *)addr;
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = 0;
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

    // 태수 교체 디버그용 읽기 전용 블록 복사.
    // 호출부에서 std::array 같은 C++ 객체를 사용해도 __try가 이 helper 내부에만 남도록 분리한다.
    static bool SafeReadBlock(uintptr_t addr, uint8_t *out, size_t size) {
      if (addr <= 0x10000 || !out || size == 0)
        return false;
      __try {
        for (size_t i = 0; i < size; ++i)
          out[i] = *(uint8_t *)(addr + i);
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

    static bool SafeWritePtr(uintptr_t addr, uintptr_t val) {
      DWORD old = 0, dummy = 0;
      if (!VirtualProtect((LPVOID)addr, sizeof(uintptr_t), PAGE_READWRITE, &old))
        return false;
      __try {
        *(uintptr_t *)addr = val;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        VirtualProtect((LPVOID)addr, sizeof(uintptr_t), old, &dummy);
        return false;
      }
      VirtualProtect((LPVOID)addr, sizeof(uintptr_t), old, &dummy);
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
      ImGui::BeginChild("##CityTop", ImVec2(0.f, 168.f * sc), true);

      const float fw = 95.f * sc;

      // 1) 자동 환전: 모든 도시의 군량 초과분을 금으로 환전
      ImGui::TextColored(ImVec4(1.f, 0.82f, 0.28f, 1.f),
                         u8"[ 자동 환전 - 모든 도시 군량 → 금 ]");
      ImGui::Separator();

      ImGui::TextUnformatted(u8"군량 한도");
      ImGui::SameLine();
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

      ImGui::SameLine(0.f, 18.f * sc);
      ImGui::TextUnformatted(u8"남길 군량");
      ImGui::SameLine();
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

      ImGui::SameLine(0.f, 18.f * sc);
      ImGui::TextUnformatted(u8"환전 비율");
      ImGui::SameLine();
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

      ImGui::SameLine(0.f, 20.f * sc);
      if (ImGui::Checkbox(u8"자동 환전##autoex", &g_cityAutoExchangeEnabled)) {
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"매 턴 내정이 끝나고 평정이 시작될 때 위 조건에 따라 자동 환전합니다.");
        ImGui::EndTooltip();
      }

      ImGui::Spacing();

      // 2) 반란 카운트
      ImGui::TextColored(ImVec4(0.75f, 0.86f, 1.f, 1.f), u8"[ 반란 카운트 ]");
      ImGui::SameLine(0.f, 18.f * sc);
      if (ImGui::Checkbox(u8"항상 0 유지##revoltzero", &g_cityRevoltAlwaysZero)) {
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
      if (ImGui::Button(u8"즉시 모든 도시 0으로 설정##revoltreset",
                        ImVec2(205.f * sc, 0.f))) {
        ResetAllCityRevoltCounters();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"현재 51개 도시의 반란 카운트를 즉시 0으로 설정합니다.");
        ImGui::EndTooltip();
      }

      ImGui::Spacing();
      ImGui::Separator();

      // 3) 모든 도시 일괄 최대화
      ImGui::TextColored(ImVec4(1.f, 0.58f, 0.58f, 1.f), u8"[ 모든 도시 일괄 최대화 ]");

      const float gap = 10.f * sc;
      const float avail = ImGui::GetContentRegionAvail().x;
      const float btnW = (avail - gap * 2.f) / 3.f;

      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.3f, 0.3f, 1.f));

      if (ImGui::Button(u8"군량 / 금 최대화##maxall", ImVec2(btnW, 0.f))) {
        MaximizeAllCityResources();
      }
      ImGui::SameLine(0.f, gap);
      if (ImGui::Button(u8"병사 최대화##maxsol", ImVec2(btnW, 0.f))) {
        MaximizeAllCitySoldierMax();
      }
      ImGui::SameLine(0.f, gap);
      if (ImGui::Button(u8"개발 최대화##maxdev", ImVec2(btnW, 0.f))) {
        MaximizeAllCityDevMax();
      }

      if (ImGui::Button(u8"상업 최대화##maxcom", ImVec2(btnW, 0.f))) {
        MaximizeAllCityComMax();
      }
      ImGui::SameLine(0.f, gap);
      if (ImGui::Button(u8"방어 최대화##maxdef", ImVec2(btnW, 0.f))) {
        MaximizeAllCityDefMax();
      }
      ImGui::SameLine(0.f, gap);
      if (ImGui::Button(u8"기술 최대화##maxtec", ImVec2(btnW, 0.f))) {
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
    static constexpr uintptr_t OFF_CITY_CORPS_RAW = 0x90;      // 군단 포인터 후보 (구버전 구조 -0x08 패턴 검증용)
    static constexpr uintptr_t OFF_CITY_FORCE_LINK_RAW = 0x98; // 현재 확인상 태수 OfficerData*
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
      uint32_t troopMax = 0;
    };

    static std::vector<FrontierFaction> s_frontierFactions;
    static std::vector<FrontierCityRow> s_frontierRows;
    static uintptr_t s_frontierSelectedForce = 0;
    static uintptr_t s_frontierPlayerForce = 0;
    static bool s_frontierDirty = true;
    static int s_frontierFilter = 0; // 0=전체, 1=전선, 2=후방

    // 수동 즉시 지원: 선택 세력 내 도시끼리 자유 이동(전선->전선 포함)
    static int s_supportSourceCity = -1;
    static int s_supportTargetCity = -1;
    static int s_supportMode = 0; // 0=정량, 1=비율
    static int s_supportFixedGold = 0;
    static int s_supportFixedGrain = 0;
    static int s_supportFixedTroops = 0;
    static int s_supportPercentGold = 0;
    static int s_supportPercentGrain = 0;
    static int s_supportPercentTroops = 0;

    struct AutoSupportRoute {
      bool enabled = true;
      int sourceCity = -1; // 후방
      int targetCity = -1; // 전선
      int mode = 0;        // 0=정량, 1=비율
      int gold = 0;
      int grain = 0;
      int troops = 0;
    };
    static std::vector<AutoSupportRoute> s_autoSupportRoutes;
    static bool s_autoSupportRoutesLoaded = false;
    static bool s_autoSupportYearlyEnabled = false;
    static uint8_t s_autoSupportLastObservedMonth = 0;
    static uint16_t s_autoSupportLastObservedYear = 0;

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

    static void NormalizeSupportSelections();

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
            if (!SafeReadPtrAllowZero(slotAddr, &connectedPtr))
              connectedPtr = 0;

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
          SafeRead32(rawCity + OFF_SOL_MAX, &row.troopMax);
          s_frontierRows.push_back(std::move(row));
        }
      }

      s_frontierDirty = false;
      NormalizeSupportSelections();
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

    static const FrontierCityRow *FindFrontierRow(int cityIndex) {
      for (const auto &row : s_frontierRows) {
        if (row.cityIndex == cityIndex)
          return &row;
      }
      return nullptr;
    }

    static void NormalizeSupportSelections() {
      const FrontierCityRow *src = FindFrontierRow(s_supportSourceCity);
      if (!src) {
        s_supportSourceCity = s_frontierRows.empty() ? -1 : s_frontierRows.front().cityIndex;
      }

      const FrontierCityRow *dst = FindFrontierRow(s_supportTargetCity);
      if (!dst || s_supportTargetCity == s_supportSourceCity) {
        s_supportTargetCity = -1;
        for (const auto &row : s_frontierRows) {
          if (row.cityIndex != s_supportSourceCity) {
            s_supportTargetCity = row.cityIndex;
            break;
          }
        }
      }
    }

    static uint32_t CalcSupportAmount(uint32_t sourceValue, int fixedValue, int percentValue) {
      uint64_t amount = 0;
      if (s_supportMode == 0) {
        amount = fixedValue > 0 ? (uint64_t)fixedValue : 0;
      } else {
        int pct = percentValue;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        amount = ((uint64_t)sourceValue * (uint64_t)pct) / 100ull;
      }

      if (amount > sourceValue)
        amount = sourceValue;
      return (uint32_t)amount;
    }

    static void ClampSupportInput(int &v, int maxValue) {
      if (v < 0) v = 0;
      if (v > maxValue) v = maxValue;
    }

    static bool ExecuteRearSupport(uintptr_t p1, uintptr_t shiftedCityBase) {
      // 실행 직전에 소유 세력/전선 판정을 다시 읽어, stale UI 값으로 잘못 보내지 않도록 한다.
      RefreshFrontierAnalysis(p1, shiftedCityBase);
      NormalizeSupportSelections();

      const FrontierCityRow *srcRow = FindFrontierRow(s_supportSourceCity);
      const FrontierCityRow *dstRow = FindFrontierRow(s_supportTargetCity);
      if (!srcRow || !dstRow || s_supportSourceCity == s_supportTargetCity) {
        AddLog(u8"[도시지원] 실행 실패: 서로 다른 같은 세력 도시를 선택해야 합니다.");
        return false;
      }

      const uintptr_t srcRaw = GetRawCityBase(shiftedCityBase, s_supportSourceCity);
      const uintptr_t dstRaw = GetRawCityBase(shiftedCityBase, s_supportTargetCity);
      if (!srcRaw || !dstRaw) {
        AddLog(u8"[도시지원] 실행 실패: 도시 주소를 확인할 수 없습니다.");
        return false;
      }

      const uintptr_t srcForce = GetCityForcePtr(srcRaw);
      const uintptr_t dstForce = GetCityForcePtr(dstRaw);
      if (!s_frontierSelectedForce || srcForce != s_frontierSelectedForce ||
          dstForce != s_frontierSelectedForce) {
        AddLog(u8"[도시지원] 실행 실패: 두 도시가 현재 선택 세력 소유가 아닙니다.");
        return false;
      }

      uint32_t srcGold = 0, srcGrain = 0, srcTroops = 0;
      uint32_t dstGold = 0, dstGrain = 0, dstTroops = 0, dstTroopMax = 0;
      if (!SafeRead32(srcRaw + OFF_GOLD, &srcGold) ||
          !SafeRead32(srcRaw + OFF_GRAIN, &srcGrain) ||
          !SafeRead32(srcRaw + OFF_CITY_TROOPS_RAW, &srcTroops) ||
          !SafeRead32(dstRaw + OFF_GOLD, &dstGold) ||
          !SafeRead32(dstRaw + OFF_GRAIN, &dstGrain) ||
          !SafeRead32(dstRaw + OFF_CITY_TROOPS_RAW, &dstTroops) ||
          !SafeRead32(dstRaw + OFF_SOL_MAX, &dstTroopMax)) {
        AddLog(u8"[도시지원] 실행 실패: 도시 자원 값을 읽지 못했습니다.");
        return false;
      }

      uint32_t moveGold = CalcSupportAmount(srcGold, s_supportFixedGold, s_supportPercentGold);
      uint32_t moveGrain = CalcSupportAmount(srcGrain, s_supportFixedGrain, s_supportPercentGrain);
      uint32_t moveTroops = CalcSupportAmount(srcTroops, s_supportFixedTroops, s_supportPercentTroops);
      const uint32_t requestedTroops = moveTroops;

      // 목적지 uint32 오버플로 방지
      const uint64_t goldRoom = 0xFFFFFFFFull - (uint64_t)dstGold;
      const uint64_t grainRoom = 0xFFFFFFFFull - (uint64_t)dstGrain;
      if ((uint64_t)moveGold > goldRoom) moveGold = (uint32_t)goldRoom;
      if ((uint64_t)moveGrain > grainRoom) moveGrain = (uint32_t)grainRoom;

      // 병력은 목적 도시 병사한도를 넘지 않도록 제한
      uint32_t troopRoom = 0;
      if (dstTroopMax > dstTroops)
        troopRoom = dstTroopMax - dstTroops;

      const bool troopCapLimited = requestedTroops > troopRoom;
      if (moveTroops > troopRoom)
        moveTroops = troopRoom;

      if (troopCapLimited) {
        char notice[256]{};
        if (troopRoom == 0) {
          sprintf_s(notice, u8"도시지원: %s 병사 한도(%u)에 도달하여 병력은 이동하지 못했습니다.",
                    g_CityList[s_supportTargetCity].cityname, dstTroopMax);
        } else {
          sprintf_s(notice, u8"도시지원: %s 병사 한도로 요청 %u명 중 %u명만 이동합니다.",
                    g_CityList[s_supportTargetCity].cityname, requestedTroops, moveTroops);
        }
        AddNotification(notice);
        AddLog(u8"[도시지원] 병력 한도 제한: 요청=%u, 실제=%u, 도착=%u/%u",
               requestedTroops, moveTroops, dstTroops, dstTroopMax);
      }

      if (moveGold == 0 && moveGrain == 0 && moveTroops == 0) {
        AddLog(u8"[도시지원] 이동 가능한 자원이 없습니다. 출발 병력=%u, 도착 병력=%u/%u",
               srcTroops, dstTroops, dstTroopMax);
        return false;
      }

      struct WriteOp {
        uintptr_t addr;
        uint32_t before;
        uint32_t after;
      };
      WriteOp ops[6]{};
      int opCount = 0;

      if (moveGold > 0) {
        ops[opCount++] = {srcRaw + OFF_GOLD, srcGold, srcGold - moveGold};
        ops[opCount++] = {dstRaw + OFF_GOLD, dstGold, dstGold + moveGold};
      }
      if (moveGrain > 0) {
        ops[opCount++] = {srcRaw + OFF_GRAIN, srcGrain, srcGrain - moveGrain};
        ops[opCount++] = {dstRaw + OFF_GRAIN, dstGrain, dstGrain + moveGrain};
      }
      if (moveTroops > 0) {
        ops[opCount++] = {srcRaw + OFF_CITY_TROOPS_RAW, srcTroops, srcTroops - moveTroops};
        ops[opCount++] = {dstRaw + OFF_CITY_TROOPS_RAW, dstTroops, dstTroops + moveTroops};
      }

      int written = 0;
      for (; written < opCount; ++written) {
        if (!SafeWrite32(ops[written].addr, ops[written].after))
          break;
      }

      if (written != opCount) {
        for (int i = written - 1; i >= 0; --i)
          SafeWrite32(ops[i].addr, ops[i].before);
        AddLog(u8"[도시지원] 쓰기 실패: 적용된 변경을 원상 복구했습니다.");
        return false;
      }

      AddLog(u8"[도시지원] %s -> %s | 금 %u / 군량 %u / 병력 %u",
             g_CityList[s_supportSourceCity].cityname,
             g_CityList[s_supportTargetCity].cityname,
             moveGold, moveGrain, moveTroops);

      s_snapDirty = true;
      RefreshFrontierAnalysis(p1, shiftedCityBase);
      NormalizeSupportSelections();
      return true;
    }

    static void DrawRearSupportPanel(uintptr_t p1, uintptr_t shiftedCityBase, float sc) {
      NormalizeSupportSelections();

      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.12f, 0.16f, 0.82f));
      ImGui::BeginChild("##ManualSupportPanel", ImVec2(0.f, 116.f * sc), true);

      ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.f), u8"[ 수동 즉시 지원 ]");
      ImGui::SameLine(0.f, 16.f * sc);
      ImGui::TextDisabled(u8"같은 세력 도시 간 즉시 이동 · 전선 → 전선 가능");
      ImGui::Separator();

      const char *srcName =
          (s_supportSourceCity >= 0 && s_supportSourceCity < g_CityCount)
              ? g_CityList[s_supportSourceCity].cityname
              : u8"도시 없음";
      const char *dstName =
          (s_supportTargetCity >= 0 && s_supportTargetCity < g_CityCount)
              ? g_CityList[s_supportTargetCity].cityname
              : u8"도시 없음";

      // 1행: 출발 / 도착 / 방식
      ImGui::TextUnformatted(u8"보내는 도시");
      ImGui::SameLine(0.f, 8.f * sc);
      ImGui::SetNextItemWidth(125.f * sc);
      if (ImGui::BeginCombo("##SupportSourceCity", srcName)) {
        for (const auto &row : s_frontierRows) {
          const bool selected = (row.cityIndex == s_supportSourceCity);
          if (ImGui::Selectable(g_CityList[row.cityIndex].cityname, selected))
            s_supportSourceCity = row.cityIndex;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::SameLine(0.f, 18.f * sc);
      ImGui::TextUnformatted(u8"→");
      ImGui::SameLine(0.f, 18.f * sc);
      ImGui::TextUnformatted(u8"받는 도시");
      ImGui::SameLine(0.f, 8.f * sc);
      ImGui::SetNextItemWidth(125.f * sc);
      if (ImGui::BeginCombo("##SupportTargetCity", dstName)) {
        for (const auto &row : s_frontierRows) {
          if (row.cityIndex == s_supportSourceCity)
            continue;
          const bool selected = (row.cityIndex == s_supportTargetCity);
          if (ImGui::Selectable(g_CityList[row.cityIndex].cityname, selected))
            s_supportTargetCity = row.cityIndex;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::SameLine(0.f, 24.f * sc);
      ImGui::TextUnformatted(u8"방식");
      ImGui::SameLine(0.f, 8.f * sc);
      if (ImGui::RadioButton(u8"정량##SupportFixed", s_supportMode == 0))
        s_supportMode = 0;
      ImGui::SameLine();
      if (ImGui::RadioButton(u8"비율##SupportPercent", s_supportMode == 1))
        s_supportMode = 1;

      int *goldInput = (s_supportMode == 0) ? &s_supportFixedGold : &s_supportPercentGold;
      int *grainInput = (s_supportMode == 0) ? &s_supportFixedGrain : &s_supportPercentGrain;
      int *troopInput = (s_supportMode == 0) ? &s_supportFixedTroops : &s_supportPercentTroops;
      const int inputMax = (s_supportMode == 0) ? 2000000000 : 100;
      const char *unitText = (s_supportMode == 0) ? u8"" : u8"%";

      // 2행: 자원량 + 실행 버튼
      ImGui::TextUnformatted(u8"금");
      ImGui::SameLine(0.f, 8.f * sc);
      ImGui::SetNextItemWidth(100.f * sc);
      if (ImGui::InputInt("##SupportGold", goldInput, 0, 0))
        ClampSupportInput(*goldInput, inputMax);
      if (s_supportMode == 1) {
        ImGui::SameLine(0.f, 3.f * sc);
        ImGui::TextUnformatted(unitText);
      }

      ImGui::SameLine(0.f, 18.f * sc);
      ImGui::TextUnformatted(u8"군량");
      ImGui::SameLine(0.f, 8.f * sc);
      ImGui::SetNextItemWidth(100.f * sc);
      if (ImGui::InputInt("##SupportGrain", grainInput, 0, 0))
        ClampSupportInput(*grainInput, inputMax);
      if (s_supportMode == 1) {
        ImGui::SameLine(0.f, 3.f * sc);
        ImGui::TextUnformatted(unitText);
      }

      ImGui::SameLine(0.f, 18.f * sc);
      ImGui::TextUnformatted(u8"병력");
      ImGui::SameLine(0.f, 8.f * sc);
      ImGui::SetNextItemWidth(100.f * sc);
      if (ImGui::InputInt("##SupportTroops", troopInput, 0, 0))
        ClampSupportInput(*troopInput, inputMax);
      if (s_supportMode == 1) {
        ImGui::SameLine(0.f, 3.f * sc);
        ImGui::TextUnformatted(unitText);
      }

      const bool canExecute = (s_supportSourceCity >= 0 && s_supportTargetCity >= 0);
      ImGui::SameLine(0.f, 20.f * sc);
      if (!canExecute)
        ImGui::BeginDisabled();
      if (ImGui::Button(u8"지금 지원##RearSupport", ImVec2(110.f * sc, 0.f)))
        ExecuteRearSupport(p1, shiftedCityBase);
      if (!canExecute)
        ImGui::EndDisabled();

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"선택 세력의 어느 도시끼리든 즉시 자원을 이동합니다.");
        ImGui::TextUnformatted(u8"전선->전선 이동도 가능하며, 실행 직전에 같은 세력인지 다시 확인합니다.");
        ImGui::TextUnformatted(u8"병력은 도착 도시의 병사한도를 넘지 않습니다.");
        ImGui::EndTooltip();
      }

      // 3행: 예상 결과 / 병사 한도 안내
      uint32_t previewGold = 0, previewGrain = 0, previewTroops = 0;
      uint32_t previewRequestedTroops = 0;
      uint32_t previewTroopRoom = 0;
      const FrontierCityRow *srcRow = FindFrontierRow(s_supportSourceCity);
      const FrontierCityRow *dstRow = FindFrontierRow(s_supportTargetCity);
      if (srcRow && dstRow) {
        previewGold = CalcSupportAmount(srcRow->gold, s_supportFixedGold, s_supportPercentGold);
        previewGrain = CalcSupportAmount(srcRow->grain, s_supportFixedGrain, s_supportPercentGrain);
        previewTroops = CalcSupportAmount(srcRow->troops, s_supportFixedTroops, s_supportPercentTroops);
        previewRequestedTroops = previewTroops;

        const uint64_t goldRoom = 0xFFFFFFFFull - (uint64_t)dstRow->gold;
        const uint64_t grainRoom = 0xFFFFFFFFull - (uint64_t)dstRow->grain;
        if ((uint64_t)previewGold > goldRoom) previewGold = (uint32_t)goldRoom;
        if ((uint64_t)previewGrain > grainRoom) previewGrain = (uint32_t)grainRoom;

        previewTroopRoom =
            (dstRow->troopMax > dstRow->troops) ? (dstRow->troopMax - dstRow->troops) : 0;
        if (previewTroops > previewTroopRoom)
          previewTroops = previewTroopRoom;
      }

      ImGui::TextDisabled(u8"예상 지원  금 %u / 군량 %u / 병력 %u",
                          previewGold, previewGrain, previewTroops);
      if (srcRow && dstRow) {
        ImGui::SameLine(0.f, 18.f * sc);
        ImGui::TextDisabled(u8"병력: 출발 %u · 도착 %u/%u · 여유 %u",
                            srcRow->troops, dstRow->troops, dstRow->troopMax, previewTroopRoom);
      }

      if (srcRow && dstRow && previewRequestedTroops > previewTroopRoom) {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.2f, 1.f),
                           u8"※ 병사 한도 제한: 요청 %u명 중 %u명만 이동 가능",
                           previewRequestedTroops, previewTroops);
        if (previewTroopRoom == 0) {
          ImGui::SameLine(0.f, 8.f * sc);
          ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.25f, 1.f),
                             u8"(도착 도시 병사 한도 도달)");
        }
      }

      ImGui::EndChild();
      ImGui::PopStyleColor();
      ImGui::Separator();
    }

    static std::string GetAutoSupportRoutePath() {
      std::filesystem::path p(GetConfigPath());
      return p.parent_path().append("S8RPK_rear_support_routes.json").string();
    }

    static void SaveAutoSupportRoutes() {
      std::ofstream out(GetAutoSupportRoutePath(), std::ios::binary | std::ios::trunc);
      if (!out.is_open())
        return;

      out << "{\n";
      out << "  \"yearly_auto\": " << (s_autoSupportYearlyEnabled ? 1 : 0) << ",\n";
      out << "  \"routes\": [\n";
      for (size_t i = 0; i < s_autoSupportRoutes.size(); ++i) {
        const auto &r = s_autoSupportRoutes[i];
        out << "    {\"enabled\": " << (r.enabled ? 1 : 0)
            << ", \"source\": " << r.sourceCity
            << ", \"target\": " << r.targetCity
            << ", \"mode\": " << r.mode
            << ", \"gold\": " << r.gold
            << ", \"grain\": " << r.grain
            << ", \"troops\": " << r.troops << "}";
        if (i + 1 < s_autoSupportRoutes.size())
          out << ",";
        out << "\n";
      }
      out << "  ]\n}\n";
    }

    static void LoadAutoSupportRoutes() {
      if (s_autoSupportRoutesLoaded)
        return;
      s_autoSupportRoutesLoaded = true;
      s_autoSupportRoutes.clear();

      std::ifstream in(GetAutoSupportRoutePath(), std::ios::binary);
      if (!in.is_open())
        return;

      std::string line;
      while (std::getline(in, line)) {
        if (line.find("\"yearly_auto\"") != std::string::npos) {
          int yearly = 0;
          if (sscanf_s(line.c_str(), "  \"yearly_auto\": %d", &yearly) == 1)
            s_autoSupportYearlyEnabled = (yearly != 0);
          continue;
        }

        int enabled = 0, source = -1, target = -1, mode = 0;
        int gold = 0, grain = 0, troops = 0;
        const int n = sscanf_s(
            line.c_str(),
            "    {\"enabled\": %d, \"source\": %d, \"target\": %d, \"mode\": %d, \"gold\": %d, \"grain\": %d, \"troops\": %d}",
            &enabled, &source, &target, &mode, &gold, &grain, &troops);
        if (n != 7)
          continue;
        if (source < 0 || source >= g_CityCount || target < 0 || target >= g_CityCount || source == target)
          continue;

        AutoSupportRoute r;
        r.enabled = enabled != 0;
        r.sourceCity = source;
        r.targetCity = target;
        r.mode = (mode == 1) ? 1 : 0;
        r.gold = gold < 0 ? 0 : gold;
        r.grain = grain < 0 ? 0 : grain;
        r.troops = troops < 0 ? 0 : troops;
        if (r.mode == 1) {
          if (r.gold > 100) r.gold = 100;
          if (r.grain > 100) r.grain = 100;
          if (r.troops > 100) r.troops = 100;
        }
        s_autoSupportRoutes.push_back(r);
      }
    }

    static bool ExecuteAutoSupportRoute(size_t routeIndex, uintptr_t p1, uintptr_t shiftedCityBase) {
      if (routeIndex >= s_autoSupportRoutes.size())
        return false;

      RefreshFrontierAnalysis(p1, shiftedCityBase);
      const AutoSupportRoute &r = s_autoSupportRoutes[routeIndex];
      const FrontierCityRow *src = FindFrontierRow(r.sourceCity);
      const FrontierCityRow *dst = FindFrontierRow(r.targetCity);

      if (!src || !dst) {
        AddNotification(u8"자동 후방지원: 도시 소유 세력이 바뀌어 해당 노선을 건너뜁니다.");
        return false;
      }
      if (src->frontline || !dst->frontline) {
        char msg[256]{};
        sprintf_s(msg, u8"자동 후방지원: %s -> %s 노선은 현재 후방->전선 조건이 아니어서 건너뜁니다.",
                  g_CityList[r.sourceCity].cityname, g_CityList[r.targetCity].cityname);
        AddNotification(msg);
        return false;
      }

      const int oldSource = s_supportSourceCity;
      const int oldTarget = s_supportTargetCity;
      const int oldMode = s_supportMode;
      const int oldFG = s_supportFixedGold, oldFGr = s_supportFixedGrain, oldFT = s_supportFixedTroops;
      const int oldPG = s_supportPercentGold, oldPGr = s_supportPercentGrain, oldPT = s_supportPercentTroops;

      s_supportSourceCity = r.sourceCity;
      s_supportTargetCity = r.targetCity;
      s_supportMode = r.mode;
      if (r.mode == 0) {
        s_supportFixedGold = r.gold;
        s_supportFixedGrain = r.grain;
        s_supportFixedTroops = r.troops;
      } else {
        s_supportPercentGold = r.gold;
        s_supportPercentGrain = r.grain;
        s_supportPercentTroops = r.troops;
      }

      const bool ok = ExecuteRearSupport(p1, shiftedCityBase);

      s_supportSourceCity = oldSource;
      s_supportTargetCity = oldTarget;
      s_supportMode = oldMode;
      s_supportFixedGold = oldFG; s_supportFixedGrain = oldFGr; s_supportFixedTroops = oldFT;
      s_supportPercentGold = oldPG; s_supportPercentGrain = oldPGr; s_supportPercentTroops = oldPT;
      NormalizeSupportSelections();
      return ok;
    }

    static void DrawAutoSupportRoutes(uintptr_t p1, uintptr_t shiftedCityBase, float sc) {
      LoadAutoSupportRoutes();

      ImGui::Spacing();

      // 노선 수에 따라 패널이 자연스럽게 커지되, 너무 많아지면 내부 스크롤로 전환.
      const int routeCount = (int)s_autoSupportRoutes.size();
      const int visibleRouteCount = (routeCount < 4) ? routeCount : 4;
      const float routeCardH = 68.f * sc;
      const float listH = (routeCount == 0) ? 34.f * sc
                                             : (routeCardH * visibleRouteCount + 4.f * sc);
      const float panelH = 58.f * sc + listH;

      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.09f, 0.15f, 0.82f));
      ImGui::BeginChild("##AutoSupportRoutesPanel", ImVec2(0.f, panelH), true);

      // 헤더
      ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.f), u8"[ 자동 후방지원 노선 ]");
      ImGui::SameLine(0.f, 16.f * sc);

      if (ImGui::Checkbox(u8"12월→1월 자동 실행##YearlyRearSupport", &s_autoSupportYearlyEnabled))
        SaveAutoSupportRoutes();
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"게임에서 12월이 끝나고 1월로 넘어가는 순간 활성 노선을 위에서부터 1회 실행합니다.");
        ImGui::TextUnformatted(u8"프로그램을 1월에 새로 켠 경우에는 소급 실행하지 않고 다음 12월→1월부터 실행합니다.");
        ImGui::EndTooltip();
      }

      ImGui::SameLine(0.f, 14.f * sc);
      if (ImGui::SmallButton(u8"+ 노선 추가")) {
        int rear = -1, front = -1;
        for (const auto &row : s_frontierRows) {
          if (!row.frontline && rear < 0) rear = row.cityIndex;
          if (row.frontline && front < 0) front = row.cityIndex;
        }
        if (rear >= 0 && front >= 0) {
          AutoSupportRoute r;
          r.sourceCity = rear;
          r.targetCity = front;
          s_autoSupportRoutes.push_back(r);
          SaveAutoSupportRoutes();
        } else {
          AddNotification(u8"자동 후방지원: 후방 도시와 전선 도시가 각각 1개 이상 필요합니다.");
        }
      }

      ImGui::SameLine(0.f, 8.f * sc);
      if (ImGui::SmallButton(u8"활성 노선 전체 지금 실행")) {
        for (size_t i = 0; i < s_autoSupportRoutes.size(); ++i) {
          if (s_autoSupportRoutes[i].enabled)
            ExecuteAutoSupportRoute(i, p1, shiftedCityBase);
        }
      }

      ImGui::SameLine(0.f, 12.f * sc);
      ImGui::TextDisabled(u8"후방 → 전선 전용");
      ImGui::Separator();

      bool saveNeeded = false;
      int deleteIndex = -1;

      const ImGuiWindowFlags listFlags =
          (routeCount > 4) ? ImGuiWindowFlags_AlwaysVerticalScrollbar : 0;
      ImGui::BeginChild("##AutoSupportRouteList", ImVec2(0.f, listH), false, listFlags);

      if (s_autoSupportRoutes.empty()) {
        ImGui::TextDisabled(u8"등록된 노선이 없습니다.  '+ 노선 추가'로 후방지원 노선을 등록하세요.");
      }

      for (size_t i = 0; i < s_autoSupportRoutes.size(); ++i) {
        AutoSupportRoute &r = s_autoSupportRoutes[i];
        ImGui::PushID((int)i + 10000);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.12f, 0.17f, 0.95f));
        ImGui::BeginChild("##AutoRouteCard", ImVec2(0.f, 62.f * sc), true);

        // 1행: 노선 / 도시 / 방식 / 실행
        ImGui::TextColored(ImVec4(0.72f, 0.78f, 1.0f, 1.f), u8"노선 %d", (int)i + 1);
        ImGui::SameLine(0.f, 10.f * sc);
        if (ImGui::Checkbox(u8"사용##AutoEnabled", &r.enabled))
          saveNeeded = true;

        ImGui::SameLine(0.f, 14.f * sc);
        const char *srcName = (r.sourceCity >= 0 && r.sourceCity < g_CityCount)
                                  ? g_CityList[r.sourceCity].cityname : u8"후방 도시";
        ImGui::SetNextItemWidth(105.f * sc);
        if (ImGui::BeginCombo("##AutoRouteSource", srcName)) {
          for (const auto &row : s_frontierRows) {
            if (row.frontline) continue;
            const bool selected = row.cityIndex == r.sourceCity;
            if (ImGui::Selectable(g_CityList[row.cityIndex].cityname, selected)) {
              r.sourceCity = row.cityIndex;
              saveNeeded = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }

        ImGui::SameLine(0.f, 6.f * sc);
        ImGui::TextUnformatted(u8"→");
        ImGui::SameLine(0.f, 6.f * sc);

        const char *dstName = (r.targetCity >= 0 && r.targetCity < g_CityCount)
                                  ? g_CityList[r.targetCity].cityname : u8"전선 도시";
        ImGui::SetNextItemWidth(105.f * sc);
        if (ImGui::BeginCombo("##AutoRouteTarget", dstName)) {
          for (const auto &row : s_frontierRows) {
            if (!row.frontline) continue;
            const bool selected = row.cityIndex == r.targetCity;
            if (ImGui::Selectable(g_CityList[row.cityIndex].cityname, selected)) {
              r.targetCity = row.cityIndex;
              saveNeeded = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }

        ImGui::SameLine(0.f, 14.f * sc);
        if (ImGui::RadioButton(u8"정량##AutoFixed", r.mode == 0)) {
          r.mode = 0;
          saveNeeded = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(u8"비율##AutoPercent", r.mode == 1)) {
          r.mode = 1;
          saveNeeded = true;
        }

        ImGui::SameLine(0.f, 14.f * sc);
        if (ImGui::SmallButton(u8"지금"))
          ExecuteAutoSupportRoute(i, p1, shiftedCityBase);
        ImGui::SameLine(0.f, 5.f * sc);
        if (ImGui::SmallButton(u8"삭제"))
          deleteIndex = (int)i;

        // 2행: 노선별 자원량
        const int maxValue = (r.mode == 0) ? 2000000000 : 100;

        ImGui::TextUnformatted(u8"금");
        ImGui::SameLine(0.f, 6.f * sc);
        ImGui::SetNextItemWidth(90.f * sc);
        if (ImGui::InputInt("##AutoGold", &r.gold, 0, 0)) {
          ClampSupportInput(r.gold, maxValue);
          saveNeeded = true;
        }
        if (r.mode == 1) {
          ImGui::SameLine(0.f, 2.f * sc);
          ImGui::TextDisabled("%%");
        }

        ImGui::SameLine(0.f, 18.f * sc);
        ImGui::TextUnformatted(u8"군량");
        ImGui::SameLine(0.f, 6.f * sc);
        ImGui::SetNextItemWidth(100.f * sc);
        if (ImGui::InputInt("##AutoGrain", &r.grain, 0, 0)) {
          ClampSupportInput(r.grain, maxValue);
          saveNeeded = true;
        }
        if (r.mode == 1) {
          ImGui::SameLine(0.f, 2.f * sc);
          ImGui::TextDisabled("%%");
        }

        ImGui::SameLine(0.f, 18.f * sc);
        ImGui::TextUnformatted(u8"병력");
        ImGui::SameLine(0.f, 6.f * sc);
        ImGui::SetNextItemWidth(100.f * sc);
        if (ImGui::InputInt("##AutoTroops", &r.troops, 0, 0)) {
          ClampSupportInput(r.troops, maxValue);
          saveNeeded = true;
        }
        if (r.mode == 1) {
          ImGui::SameLine(0.f, 2.f * sc);
          ImGui::TextDisabled("%%");
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
      }

      ImGui::EndChild();

      if (deleteIndex >= 0 && deleteIndex < (int)s_autoSupportRoutes.size()) {
        s_autoSupportRoutes.erase(s_autoSupportRoutes.begin() + deleteIndex);
        saveNeeded = true;
      }
      if (saveNeeded)
        SaveAutoSupportRoutes();

      ImGui::EndChild();
      ImGui::PopStyleColor();
      ImGui::Separator();
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

      DrawRearSupportPanel(p1, shiftedCityBase, sc);
      DrawAutoSupportRoutes(p1, shiftedCityBase, sc);

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
      ImGui::TableSetupColumn(u8"병사/한도", ImGuiTableColumnFlags_WidthFixed, 115.f * sc);
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
        ImGui::Text("%u / %u", row.troops, row.troopMax);
      }

      ImGui::EndTable();
    }

    // ── 도시별 무장 배치 ───────────────────────────────────────────────────
    struct CityOfficerRow {
      uintptr_t officerBase = 0;
      uint16_t id = 0;
      uint8_t status = 0;
      uint8_t loyalty = 0;
      uint8_t lead = 0;
      uint8_t war = 0;
      uint8_t intel = 0;
      uint8_t pol = 0;
      uint8_t cha = 0;
    };

    static std::vector<int> s_officerPlayerCities;
    static std::vector<CityOfficerRow> s_cityOfficerRows;
    static int s_officerCityIndex = -1;
    static int s_officerMoveTargetCity = -1;
    static int s_selectedOfficerId = -1;
    static uintptr_t s_officerPlayerForce = 0;
    static bool s_officerRosterDirty = true;
    static constexpr uintptr_t OFFICER_CORPS_FILTER_ALL = ~(uintptr_t)0;
    static uintptr_t s_officerCorpsFilter = OFFICER_CORPS_FILTER_ALL;

    struct OfficerCityCorpsInfo {
      bool readable = false;
      uintptr_t corpsPtr = 0;
      uintptr_t corpsNo = 0;
    };

    static OfficerCityCorpsInfo GetOfficerCityCorpsInfo(
        uintptr_t shiftedCityBase, int cityIndex) {
      OfficerCityCorpsInfo info;
      const uintptr_t rawCity = GetRawCityBase(shiftedCityBase, cityIndex);
      if (!rawCity)
        return info;

      if (!SafeReadPtrAllowZero(rawCity + OFF_CITY_CORPS_RAW, &info.corpsPtr))
        return info;

      info.readable = true;
      if (info.corpsPtr > 0x10000)
        SafeReadPtrAllowZero(info.corpsPtr + 0x18, &info.corpsNo);
      return info;
    }

    static std::string GetOfficerCorpsName(uintptr_t shiftedCityBase,
                                           int cityIndex) {
      const OfficerCityCorpsInfo info =
          GetOfficerCityCorpsInfo(shiftedCityBase, cityIndex);
      if (!info.readable)
        return u8"군단 ?";
      if (info.corpsPtr <= 0x10000)
        return u8"직할";
      if (info.corpsNo)
        return std::to_string((unsigned long long)info.corpsNo) + u8"군단";
      return u8"군단 ?";
    }

    static std::string BuildOfficerCityCorpsLabel(uintptr_t shiftedCityBase,
                                                   int cityIndex) {
      if (cityIndex < 0 || cityIndex >= g_CityCount)
        return u8"도시 없음";
      return "[" + GetOfficerCorpsName(shiftedCityBase, cityIndex) + "] " +
             g_CityList[cityIndex].cityname;
    }

    static bool OfficerCityMatchesCorpsFilter(uintptr_t shiftedCityBase,
                                              int cityIndex) {
      if (s_officerCorpsFilter == OFFICER_CORPS_FILTER_ALL)
        return true;
      const OfficerCityCorpsInfo info =
          GetOfficerCityCorpsInfo(shiftedCityBase, cityIndex);
      return info.readable && info.corpsPtr == s_officerCorpsFilter;
    }

    static bool SafeReadOfficerRow(uintptr_t base, CityOfficerRow *out,
                                   uintptr_t *outForce, uintptr_t *outCity) {
      if (!out || !outForce || !outCity)
        return false;
      __try {
        out->officerBase = base;
        out->id = *(uint16_t *)(base + 0x08);
        out->status = *(uint8_t *)(base + 0x10);
        *outForce = *(uintptr_t *)(base + 0x18);
        *outCity = *(uintptr_t *)(base + 0x20);
        out->lead = *(uint8_t *)(base + 0xAA);
        out->war = *(uint8_t *)(base + 0xAB);
        out->intel = *(uint8_t *)(base + 0xAC);
        out->pol = *(uint8_t *)(base + 0xAD);
        out->cha = *(uint8_t *)(base + 0xAE);
        out->loyalty = *(uint8_t *)(base + 0xEC);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outForce = 0;
        *outCity = 0;
        return false;
      }
    }

    static bool IsEmployedOfficerStatus(uint8_t status) {
      return status == 0x18 || status == 0x28 || status == 0x38 ||
             status == 0x48 || status == 0xC8 || status == 0xD8 ||
             status == 0xE8;
    }

    static const char *GetOfficerStatusName(uint8_t status) {
      switch (status) {
      case 0x18: return u8"군사";
      case 0x28: return u8"일반";
      case 0x38: return u8"두령";
      case 0x48: return u8"동지";
      case 0xC8: return u8"군주";
      case 0xD8: return u8"도독";
      case 0xE8: return u8"태수";
      default: return u8"기타";
      }
    }

    static int GetOfficerStatusSortRank(uint8_t status) {
      switch (status) {
      case 0xC8: return 0; // 군주
      case 0xD8: return 1; // 도독
      case 0xE8: return 2; // 태수
      case 0x18: return 3; // 군사
      case 0x38: return 4; // 두령
      case 0x48: return 5; // 동지
      case 0x28: return 6; // 일반
      default: return 99;
      }
    }

    static bool IsCityFrontlineForForce(uintptr_t shiftedCityBase, int cityIndex,
                                        uintptr_t forcePtr) {
      if (shiftedCityBase <= 0x10000 || cityIndex < 0 ||
          cityIndex >= g_CityCount || !forcePtr)
        return false;

      const uintptr_t rawCityArrayBase = shiftedCityBase - 0x28;
      const uintptr_t rawCity = GetRawCityBase(shiftedCityBase, cityIndex);
      for (int slot = 0; slot < CITY_CONNECTION_SLOTS; ++slot) {
        uintptr_t connectedPtr = 0;
        if (!SafeReadPtrAllowZero(
                rawCity + OFF_CITY_CONNECTION_RAW +
                    (uintptr_t)slot * sizeof(uintptr_t),
                &connectedPtr) ||
            !connectedPtr)
          continue;

        const int connectedIdx =
            ConnectionPtrToCityIndex(rawCityArrayBase, connectedPtr);
        if (connectedIdx < 0)
          continue;

        const uintptr_t connectedRaw =
            GetRawCityBase(shiftedCityBase, connectedIdx);
        if (GetCityForcePtr(connectedRaw) != forcePtr)
          return true;
      }
      return false;
    }

    static void NormalizeOfficerCitySelections() {
      auto containsCity = [](const std::vector<int> &cities, int idx) {
        for (int c : cities) {
          if (c == idx)
            return true;
        }
        return false;
      };

      if (!containsCity(s_officerPlayerCities, s_officerCityIndex))
        s_officerCityIndex =
            s_officerPlayerCities.empty() ? -1 : s_officerPlayerCities.front();

      if (!containsCity(s_officerPlayerCities, s_officerMoveTargetCity) ||
          s_officerMoveTargetCity == s_officerCityIndex) {
        s_officerMoveTargetCity = -1;
        for (int idx : s_officerPlayerCities) {
          if (idx != s_officerCityIndex) {
            s_officerMoveTargetCity = idx;
            break;
          }
        }
      }
    }

    static void RefreshCityOfficerRoster(uintptr_t p1,
                                         uintptr_t shiftedCityBase) {
      s_officerPlayerCities.clear();
      s_cityOfficerRows.clear();
      s_officerPlayerForce = 0;

      if (p1 <= 0x10000 || shiftedCityBase <= 0x10000) {
        s_officerRosterDirty = false;
        return;
      }

      uintptr_t playerCity = 0;
      if (!SafeReadPtr(p1 + 0x20, &playerCity)) {
        s_officerRosterDirty = false;
        return;
      }
      s_officerPlayerForce = GetCityForcePtr(playerCity);
      if (!s_officerPlayerForce) {
        s_officerRosterDirty = false;
        return;
      }

      for (int i = 0; i < g_CityCount; ++i) {
        const uintptr_t rawCity = GetRawCityBase(shiftedCityBase, i);
        if (GetCityForcePtr(rawCity) == s_officerPlayerForce)
          s_officerPlayerCities.push_back(i);
      }

      NormalizeOfficerCitySelections();
      if (s_officerCityIndex < 0) {
        s_officerRosterDirty = false;
        return;
      }

      uintptr_t rosterBase = 0;
      const uintptr_t exe = (uintptr_t)GetModuleHandle(NULL);
      if (!exe ||
          !TryResolveOfficerRosterArrayBase(exe, &rosterBase) ||
          rosterBase <= 0x10000) {
        s_officerRosterDirty = false;
        return;
      }

      LoadOfficerNames();
      const uintptr_t selectedCityRaw =
          GetRawCityBase(shiftedCityBase, s_officerCityIndex);

      for (int i = 0; i < 5102; ++i) {
        const uintptr_t officerBase =
            rosterBase + (uintptr_t)i * 0x3D0;
        CityOfficerRow row;
        uintptr_t forcePtr = 0, cityPtr = 0;
        if (!SafeReadOfficerRow(officerBase, &row, &forcePtr, &cityPtr))
          continue;
        if (row.id == 0 || row.id > 5102)
          continue;
        if (!IsEmployedOfficerStatus(row.status))
          continue;
        if (forcePtr != s_officerPlayerForce || cityPtr != selectedCityRaw)
          continue;

        s_cityOfficerRows.push_back(row);
      }

      std::sort(s_cityOfficerRows.begin(), s_cityOfficerRows.end(),
                [](const CityOfficerRow &a, const CityOfficerRow &b) {
                  const int ar = GetOfficerStatusSortRank(a.status);
                  const int br = GetOfficerStatusSortRank(b.status);
                  if (ar != br)
                    return ar < br;
                  if (a.lead != b.lead)
                    return a.lead > b.lead;
                  if (a.war != b.war)
                    return a.war > b.war;
                  return a.id < b.id;
                });

      bool selectedStillExists = false;
      for (const auto &row : s_cityOfficerRows) {
        if ((int)row.id == s_selectedOfficerId) {
          selectedStillExists = true;
          break;
        }
      }
      if (!selectedStillExists)
        s_selectedOfficerId = -1;

      s_officerRosterDirty = false;
    }

    static const CityOfficerRow *FindSelectedCityOfficer() {
      for (const auto &row : s_cityOfficerRows) {
        if ((int)row.id == s_selectedOfficerId)
          return &row;
      }
      return nullptr;
    }

    // ── 태수 교체 메모리 스냅샷 / diff (읽기 전용) ───────────────────────
    struct GovernorDebugSnapshot {
      bool valid = false;
      int cityIndex = -1;
      uint16_t oldGovernorId = 0;
      uint16_t newGovernorId = 0;
      uintptr_t oldGovernorBase = 0;
      uintptr_t newGovernorBase = 0;
      uintptr_t cityBase = 0;
      std::array<uint8_t, 0x3D0> oldGovernor{};
      std::array<uint8_t, 0x3D0> newGovernor{};
      std::array<uint8_t, 0x2A0> city{};
    };

    static GovernorDebugSnapshot s_governorDbgSnapshot;
    static int s_governorDbgOldId = -1;
    static int s_governorDbgNewId = -1;

    static const CityOfficerRow *FindCityOfficerById(int officerId) {
      for (const auto &row : s_cityOfficerRows) {
        if ((int)row.id == officerId)
          return &row;
      }
      return nullptr;
    }

    static std::string BuildOfficerDebugName(uint16_t officerId) {
      auto it = g_officerNames.find((int)officerId);
      if (it != g_officerNames.end() && !it->second.empty())
        return it->second;
      return u8"무장 ID " + std::to_string((int)officerId);
    }

    static void NormalizeGovernorDebugSelections() {
      const CityOfficerRow *oldGov = FindCityOfficerById(s_governorDbgOldId);
      if (!oldGov || oldGov->status != 0xE8) {
        s_governorDbgOldId = -1;
        for (const auto &row : s_cityOfficerRows) {
          if (row.status == 0xE8) {
            s_governorDbgOldId = row.id;
            break;
          }
        }
      }

      const CityOfficerRow *candidate = FindCityOfficerById(s_governorDbgNewId);
      if (!candidate || candidate->status != 0x28) {
        s_governorDbgNewId = -1;
        for (const auto &row : s_cityOfficerRows) {
          if (row.status == 0x28) {
            s_governorDbgNewId = row.id;
            break;
          }
        }
      }
    }

    static uint64_t ReadLe64(const uint8_t *p) {
      uint64_t value = 0;
      for (int i = 0; i < 8; ++i)
        value |= ((uint64_t)p[i]) << (i * 8);
      return value;
    }

    static bool LooksLikeUserPointer(uint64_t value) {
      return value > 0x10000ull && value < 0x0000800000000000ull;
    }

    static int LogGovernorDebugDiff(const char *label,
                                    const uint8_t *before,
                                    const uint8_t *after,
                                    size_t size) {
      int byteChanges = 0;
      int pointerCandidates = 0;

      AddLog(u8"[태수DBG] %s", label);
      for (size_t i = 0; i < size; ++i) {
        if (before[i] == after[i])
          continue;
        ++byteChanges;
        AddLog(u8"  +0x%03X : %02X -> %02X",
               (unsigned int)i,
               (unsigned int)before[i],
               (unsigned int)after[i]);
      }

      for (size_t off = 0; off + 8 <= size; off += 8) {
        const uint64_t beforeQ = ReadLe64(before + off);
        const uint64_t afterQ = ReadLe64(after + off);
        if (beforeQ == afterQ)
          continue;
        if (!LooksLikeUserPointer(beforeQ) && !LooksLikeUserPointer(afterQ))
          continue;

        ++pointerCandidates;
        AddLog(u8"  [PTR? +0x%03X] 0x%016llX -> 0x%016llX",
               (unsigned int)off,
               (unsigned long long)beforeQ,
               (unsigned long long)afterQ);
      }

      AddLog(u8"[태수DBG] %s 요약: 변경 바이트 %d개 / 8바이트 포인터 후보 %d개",
             label, byteChanges, pointerCandidates);
      return byteChanges;
    }

    static bool CaptureGovernorDebugBaseline(uintptr_t shiftedCityBase) {
      NormalizeGovernorDebugSelections();

      const CityOfficerRow *oldGov = FindCityOfficerById(s_governorDbgOldId);
      const CityOfficerRow *candidate = FindCityOfficerById(s_governorDbgNewId);
      if (!oldGov || oldGov->status != 0xE8 ||
          !candidate || candidate->status != 0x28 ||
          s_officerCityIndex < 0 || s_officerCityIndex >= g_CityCount) {
        AddNotification(u8"태수DBG: 현재 도시의 태수 A와 일반 후보 B를 선택해주세요.");
        return false;
      }

      const uintptr_t rawCity =
          GetRawCityBase(shiftedCityBase, s_officerCityIndex);
      if (!rawCity) {
        AddNotification(u8"태수DBG: 도시 메모리 주소를 확인하지 못했습니다.");
        return false;
      }

      GovernorDebugSnapshot next;
      next.cityIndex = s_officerCityIndex;
      next.oldGovernorId = oldGov->id;
      next.newGovernorId = candidate->id;
      next.oldGovernorBase = oldGov->officerBase;
      next.newGovernorBase = candidate->officerBase;
      next.cityBase = rawCity;

      if (!SafeReadBlock(next.oldGovernorBase, next.oldGovernor.data(),
                         next.oldGovernor.size()) ||
          !SafeReadBlock(next.newGovernorBase, next.newGovernor.data(),
                         next.newGovernor.size()) ||
          !SafeReadBlock(next.cityBase, next.city.data(), next.city.size())) {
        AddNotification(u8"태수DBG: 기준 메모리 읽기에 실패했습니다.");
        return false;
      }

      next.valid = true;
      s_governorDbgSnapshot = next;

      const std::string oldName = BuildOfficerDebugName(next.oldGovernorId);
      const std::string newName = BuildOfficerDebugName(next.newGovernorId);
      AddLog(u8"[태수DBG] 기준 저장 완료");
      AddLog(u8"[태수DBG] 기존 태수 A: %s (ID %u) base=0x%llX size=0x3D0",
             oldName.c_str(), (unsigned int)next.oldGovernorId,
             (unsigned long long)next.oldGovernorBase);
      AddLog(u8"[태수DBG] 후보 일반 B: %s (ID %u) base=0x%llX size=0x3D0",
             newName.c_str(), (unsigned int)next.newGovernorId,
             (unsigned long long)next.newGovernorBase);
      AddLog(u8"[태수DBG] 도시 C: %s base=0x%llX size=0x2A0",
             g_CityList[next.cityIndex].cityname,
             (unsigned long long)next.cityBase);
      AddLog(u8"[태수DBG] 이제 게임의 정상 평정 메뉴로 A -> 일반 / B -> 태수 교체 후 '변경값 비교'를 누르세요.");
      AddNotification(u8"태수DBG: 기준 저장 완료. 정상 게임 기능으로 태수를 교체하세요.");
      return true;
    }

    static bool CompareGovernorDebugBaseline() {
      if (!s_governorDbgSnapshot.valid) {
        AddNotification(u8"태수DBG: 먼저 '태수교체 기준 저장'을 눌러주세요.");
        return false;
      }

      std::array<uint8_t, 0x3D0> oldGovernorNow{};
      std::array<uint8_t, 0x3D0> newGovernorNow{};
      std::array<uint8_t, 0x2A0> cityNow{};

      if (!SafeReadBlock(s_governorDbgSnapshot.oldGovernorBase,
                         oldGovernorNow.data(), oldGovernorNow.size()) ||
          !SafeReadBlock(s_governorDbgSnapshot.newGovernorBase,
                         newGovernorNow.data(), newGovernorNow.size()) ||
          !SafeReadBlock(s_governorDbgSnapshot.cityBase,
                         cityNow.data(), cityNow.size())) {
        AddNotification(u8"태수DBG: 현재 메모리 읽기에 실패했습니다.");
        return false;
      }

      const std::string oldName =
          BuildOfficerDebugName(s_governorDbgSnapshot.oldGovernorId);
      const std::string newName =
          BuildOfficerDebugName(s_governorDbgSnapshot.newGovernorId);

      char oldLabel[160]{};
      char newLabel[160]{};
      char cityLabel[160]{};
      sprintf_s(oldLabel, u8"기존 태수 A %s (ID %u)",
                oldName.c_str(),
                (unsigned int)s_governorDbgSnapshot.oldGovernorId);
      sprintf_s(newLabel, u8"신규 태수 후보 B %s (ID %u)",
                newName.c_str(),
                (unsigned int)s_governorDbgSnapshot.newGovernorId);
      sprintf_s(cityLabel, u8"도시 C %s",
                g_CityList[s_governorDbgSnapshot.cityIndex].cityname);

      AddLog(u8"[태수DBG] ================= 변경값 비교 시작 =================");
      const int aChanges =
          LogGovernorDebugDiff(oldLabel,
                               s_governorDbgSnapshot.oldGovernor.data(),
                               oldGovernorNow.data(),
                               oldGovernorNow.size());
      const int bChanges =
          LogGovernorDebugDiff(newLabel,
                               s_governorDbgSnapshot.newGovernor.data(),
                               newGovernorNow.data(),
                               newGovernorNow.size());
      const int cityChanges =
          LogGovernorDebugDiff(cityLabel,
                               s_governorDbgSnapshot.city.data(),
                               cityNow.data(),
                               cityNow.size());
      AddLog(u8"[태수DBG] 전체 요약: A %d바이트 / B %d바이트 / 도시 %d바이트 변경",
             aChanges, bChanges, cityChanges);
      AddLog(u8"[태수DBG] =====================================================");
      AddNotification(u8"태수DBG: 변경값 비교 완료. 로그 창을 확인하세요.");
      return true;
    }


    static bool ApplyGovernorSwap(uintptr_t p1, uintptr_t shiftedCityBase) {
      NormalizeGovernorDebugSelections();

      const CityOfficerRow *oldGov = FindCityOfficerById(s_governorDbgOldId);
      const CityOfficerRow *candidate = FindCityOfficerById(s_governorDbgNewId);
      if (!oldGov || !candidate ||
          oldGov->status != 0xE8 || candidate->status != 0x28 ||
          s_officerCityIndex < 0 || s_officerCityIndex >= g_CityCount) {
        AddNotification(u8"태수 교체: 현재 태수와 일반 후보를 다시 선택해주세요.");
        return false;
      }

      const uintptr_t rawCity =
          GetRawCityBase(shiftedCityBase, s_officerCityIndex);
      if (!rawCity) {
        AddNotification(u8"태수 교체: 도시 주소를 확인하지 못했습니다.");
        return false;
      }

      uint16_t oldStatusPair = 0;
      uint16_t newStatusPair = 0;
      uintptr_t cityGovernor = 0;
      uintptr_t oldCity = 0, newCity = 0;
      uintptr_t oldForce = 0, newForce = 0;

      if (!SafeRead16(oldGov->officerBase + 0x10, &oldStatusPair) ||
          !SafeRead16(candidate->officerBase + 0x10, &newStatusPair) ||
          !SafeReadPtr(rawCity + OFF_CITY_FORCE_LINK_RAW, &cityGovernor) ||
          !SafeReadPtr(oldGov->officerBase + 0x20, &oldCity) ||
          !SafeReadPtr(candidate->officerBase + 0x20, &newCity) ||
          !SafeReadPtr(oldGov->officerBase + 0x18, &oldForce) ||
          !SafeReadPtr(candidate->officerBase + 0x18, &newForce)) {
        AddNotification(u8"태수 교체: 현재 메모리 상태를 읽지 못했습니다.");
        return false;
      }

      // +0x10은 신분 바이트로 확정(E8=태수, 28=일반).
      // +0x11은 정상 게임 태수 교체에서 두 무장 사이 값이 서로 바뀌는 것이 관측됐으므로
      // 특정 C2/C3 값으로 고정하지 않고 현재 두 값을 서로 교환한다.
      const uint8_t oldStatus = (uint8_t)(oldStatusPair & 0xFF);
      const uint8_t oldAux = (uint8_t)((oldStatusPair >> 8) & 0xFF);
      const uint8_t newStatus = (uint8_t)(newStatusPair & 0xFF);
      const uint8_t newAux = (uint8_t)((newStatusPair >> 8) & 0xFF);

      if (oldStatus != 0xE8 || newStatus != 0x28) {
        AddNotification(u8"태수 교체: 신분 바이트(+0x10)가 예상과 달라 중단했습니다.");
        AddLog(u8"[태수교체] 신분 불일치: 기존 +0x10=%02X(+0x11=%02X) / 후보 +0x10=%02X(+0x11=%02X)",
               (unsigned int)oldStatus, (unsigned int)oldAux,
               (unsigned int)newStatus, (unsigned int)newAux);
        return false;
      }

      const uint16_t oldPairAfter =
          (uint16_t)(((uint16_t)newAux << 8) | 0x28u);
      const uint16_t newPairAfter =
          (uint16_t)(((uint16_t)oldAux << 8) | 0xE8u);

      AddLog(u8"[태수교체] 적용 전 상태쌍: 기존=0x%04X / 후보=0x%04X -> 적용값 기존=0x%04X / 후보=0x%04X",
             (unsigned int)oldStatusPair, (unsigned int)newStatusPair,
             (unsigned int)oldPairAfter, (unsigned int)newPairAfter);

      if (cityGovernor != oldGov->officerBase) {
        AddNotification(u8"태수 교체: 도시의 현재 태수 포인터가 선택한 태수와 다릅니다.");
        AddLog(u8"[태수교체] City+0x98 불일치: 현재 0x%llX / 선택 태수 0x%llX",
               (unsigned long long)cityGovernor,
               (unsigned long long)oldGov->officerBase);
        return false;
      }

      if (oldCity != rawCity || newCity != rawCity) {
        AddNotification(u8"태수 교체: 두 무장이 현재 같은 도시에 있지 않습니다.");
        AddLog(u8"[태수교체] 도시 불일치: city=0x%llX / 기존=0x%llX / 후보=0x%llX",
               (unsigned long long)rawCity,
               (unsigned long long)oldCity,
               (unsigned long long)newCity);
        return false;
      }

      if (!oldForce || oldForce != newForce ||
          (s_officerPlayerForce && oldForce != s_officerPlayerForce)) {
        AddNotification(u8"태수 교체: 두 무장의 소속 세력이 일치하지 않습니다.");
        AddLog(u8"[태수교체] 세력 불일치: 기존=0x%llX / 후보=0x%llX / 플레이어=0x%llX",
               (unsigned long long)oldForce,
               (unsigned long long)newForce,
               (unsigned long long)s_officerPlayerForce);
        return false;
      }

      const uint16_t oldPairBefore = oldStatusPair;
      const uint16_t newPairBefore = newStatusPair;
      const uintptr_t cityGovernorBefore = cityGovernor;

      bool oldWritten = false;
      bool newWritten = false;
      bool cityWritten = false;

      oldWritten = SafeWrite16(oldGov->officerBase + 0x10, oldPairAfter);
      if (oldWritten)
        newWritten = SafeWrite16(candidate->officerBase + 0x10, newPairAfter);
      if (oldWritten && newWritten)
        cityWritten = SafeWritePtr(rawCity + OFF_CITY_FORCE_LINK_RAW,
                                   candidate->officerBase);

      if (!oldWritten || !newWritten || !cityWritten) {
        if (cityWritten)
          SafeWritePtr(rawCity + OFF_CITY_FORCE_LINK_RAW, cityGovernorBefore);
        if (newWritten)
          SafeWrite16(candidate->officerBase + 0x10, newPairBefore);
        if (oldWritten)
          SafeWrite16(oldGov->officerBase + 0x10, oldPairBefore);

        AddNotification(u8"태수 교체: 쓰기 중 실패하여 원래 값으로 복구했습니다.");
        AddLog(u8"[태수교체] 쓰기 실패/롤백: old=%d new=%d city=%d",
               oldWritten ? 1 : 0, newWritten ? 1 : 0, cityWritten ? 1 : 0);
        return false;
      }

      uint16_t verifyOld = 0, verifyNew = 0;
      uintptr_t verifyCityGovernor = 0;
      const bool verifyOk =
          SafeRead16(oldGov->officerBase + 0x10, &verifyOld) &&
          SafeRead16(candidate->officerBase + 0x10, &verifyNew) &&
          SafeReadPtr(rawCity + OFF_CITY_FORCE_LINK_RAW, &verifyCityGovernor) &&
          verifyOld == oldPairAfter &&
          verifyNew == newPairAfter &&
          verifyCityGovernor == candidate->officerBase;

      if (!verifyOk) {
        SafeWritePtr(rawCity + OFF_CITY_FORCE_LINK_RAW, cityGovernorBefore);
        SafeWrite16(candidate->officerBase + 0x10, newPairBefore);
        SafeWrite16(oldGov->officerBase + 0x10, oldPairBefore);

        AddNotification(u8"태수 교체: 적용 후 검증 실패로 원래 값으로 복구했습니다.");
        AddLog(u8"[태수교체] 검증 실패/롤백: 기존=0x%04X 후보=0x%04X City+0x98=0x%llX",
               (unsigned int)verifyOld,
               (unsigned int)verifyNew,
               (unsigned long long)verifyCityGovernor);
        return false;
      }

      const std::string oldName = BuildOfficerDebugName(oldGov->id);
      const std::string newName = BuildOfficerDebugName(candidate->id);
      AddLog(u8"[태수교체] %s -> 일반 (0x%04X -> 0x%04X)",
             oldName.c_str(), (unsigned int)oldPairBefore,
             (unsigned int)oldPairAfter);
      AddLog(u8"[태수교체] %s -> 태수 (0x%04X -> 0x%04X)",
             newName.c_str(), (unsigned int)newPairBefore,
             (unsigned int)newPairAfter);
      AddLog(u8"[태수교체] %s City+0x98: 0x%llX -> 0x%llX",
             g_CityList[s_officerCityIndex].cityname,
             (unsigned long long)cityGovernorBefore,
             (unsigned long long)candidate->officerBase);

      char notice[256]{};
      sprintf_s(notice, u8"%s 태수: %s → %s",
                g_CityList[s_officerCityIndex].cityname,
                oldName.c_str(), newName.c_str());
      AddNotification(notice);

      s_governorDbgSnapshot.valid = false;
      s_selectedOfficerId = -1;
      s_officerRosterDirty = true;
      RefreshCityOfficerRoster(p1, shiftedCityBase);
      NormalizeGovernorDebugSelections();
      return true;
    }

    static void DrawGovernorDebugPanel(uintptr_t p1, uintptr_t shiftedCityBase, float sc) {
      NormalizeGovernorDebugSelections();

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.30f, 1.f),
                         u8"[ 태수 교체 ]");
      ImGui::SameLine(0.f, 12.f * sc);
      ImGui::TextDisabled(u8"실제 교체 + 읽기 전용 diff");

      const char *cityName =
          (s_officerCityIndex >= 0 && s_officerCityIndex < g_CityCount)
              ? g_CityList[s_officerCityIndex].cityname
              : u8"도시 없음";
      ImGui::Text(u8"도시 C: %s", cityName);
      ImGui::SameLine(0.f, 18.f * sc);

      const CityOfficerRow *oldGov = FindCityOfficerById(s_governorDbgOldId);
      std::string oldGovName =
          oldGov ? BuildOfficerDebugName(oldGov->id) : u8"태수 없음";
      ImGui::TextUnformatted(u8"기존 태수 A");
      ImGui::SameLine(0.f, 6.f * sc);
      ImGui::SetNextItemWidth(125.f * sc);
      if (ImGui::BeginCombo("##GovernorDbgOld", oldGovName.c_str())) {
        for (const auto &row : s_cityOfficerRows) {
          if (row.status != 0xE8)
            continue;
          const std::string name = BuildOfficerDebugName(row.id);
          const bool selected = ((int)row.id == s_governorDbgOldId);
          if (ImGui::Selectable(name.c_str(), selected))
            s_governorDbgOldId = row.id;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::SameLine(0.f, 18.f * sc);
      const CityOfficerRow *candidate =
          FindCityOfficerById(s_governorDbgNewId);
      std::string candidateName =
          candidate ? BuildOfficerDebugName(candidate->id) : u8"일반 없음";
      ImGui::TextUnformatted(u8"후보 일반 B");
      ImGui::SameLine(0.f, 6.f * sc);
      ImGui::SetNextItemWidth(125.f * sc);
      if (ImGui::BeginCombo("##GovernorDbgNew", candidateName.c_str())) {
        for (const auto &row : s_cityOfficerRows) {
          if (row.status != 0x28)
            continue;
          const std::string name = BuildOfficerDebugName(row.id);
          const bool selected = ((int)row.id == s_governorDbgNewId);
          if (ImGui::Selectable(name.c_str(), selected))
            s_governorDbgNewId = row.id;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      const bool canCapture =
          oldGov != nullptr && candidate != nullptr &&
          s_officerCityIndex >= 0;

      if (!canCapture)
        ImGui::BeginDisabled();
      if (ImGui::Button(u8"태수 교체 적용##GovernorApply",
                        ImVec2(145.f * sc, 0.f)))
        ApplyGovernorSwap(p1, shiftedCityBase);
      if (!canCapture)
        ImGui::EndDisabled();

      ImGui::SameLine(0.f, 10.f * sc);
      if (!canCapture)
        ImGui::BeginDisabled();
      if (ImGui::Button(u8"태수교체 기준 저장##GovernorDbgCapture",
                        ImVec2(165.f * sc, 0.f)))
        CaptureGovernorDebugBaseline(shiftedCityBase);
      if (!canCapture)
        ImGui::EndDisabled();

      ImGui::SameLine(0.f, 10.f * sc);
      if (!s_governorDbgSnapshot.valid)
        ImGui::BeginDisabled();
      if (ImGui::Button(u8"변경값 비교##GovernorDbgCompare",
                        ImVec2(130.f * sc, 0.f)))
        CompareGovernorDebugBaseline();
      if (!s_governorDbgSnapshot.valid)
        ImGui::EndDisabled();

      ImGui::SameLine(0.f, 14.f * sc);
      ImGui::TextDisabled(
          u8"저장: A/B 각 0x3D0 + 도시 0x2A0 | 비교: 1바이트 diff + 8바이트 정렬 PTR 후보");

      if (s_governorDbgSnapshot.valid) {
        const std::string savedA =
            BuildOfficerDebugName(s_governorDbgSnapshot.oldGovernorId);
        const std::string savedB =
            BuildOfficerDebugName(s_governorDbgSnapshot.newGovernorId);
        ImGui::TextDisabled(u8"저장된 기준: %s(A) -> %s(B), 도시 %s",
                            savedA.c_str(), savedB.c_str(),
                            g_CityList[s_governorDbgSnapshot.cityIndex].cityname);
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            u8"태수 교체 적용은 확인된 +0x10/+0x11 상태쌍과 City+0x98 태수 포인터만 변경합니다.");
        ImGui::TextUnformatted(
            u8"기준 저장/변경값 비교 기능은 계속 읽기 전용입니다.");
        ImGui::EndTooltip();
      }
    }


    // ── 도시 군단 포인터 확인 (읽기 전용) ────────────────────────────────
    static void DrawCityCorpsPointerDebug(uintptr_t shiftedCityBase, float sc) {
      if (s_officerCityIndex < 0 || s_officerCityIndex >= g_CityCount)
        return;

      const uintptr_t rawCity =
          GetRawCityBase(shiftedCityBase, s_officerCityIndex);
      if (!rawCity)
        return;

      uintptr_t corpsPtr = 0;
      const bool corpsRead =
          SafeReadPtr(rawCity + OFF_CITY_CORPS_RAW, &corpsPtr);

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.55f, 0.90f, 1.0f, 1.f),
                         u8"[ 군단 포인터 확인 - 읽기 전용 ]");
      ImGui::SameLine(0.f, 12.f * sc);
      ImGui::TextDisabled(u8"CityData +0x90 후보 검증");

      ImGui::Text(u8"도시: %s  Raw CityData: 0x%llX",
                  g_CityList[s_officerCityIndex].cityname,
                  (unsigned long long)rawCity);

      if (!corpsRead) {
        ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f),
                           u8"City +0x90 읽기 실패");
        return;
      }

      ImGui::Text(u8"City +0x90 군단 후보: 0x%llX",
                  (unsigned long long)corpsPtr);

      if (corpsPtr <= 0x10000) {
        ImGui::TextDisabled(
            u8"군단 포인터가 비어 있습니다. 직할/군단 미지정 상태일 가능성을 확인하세요.");
      } else {
        uintptr_t forcePtr = 0;
        uintptr_t corpsNoRaw = 0;
        uintptr_t governorGeneralPtr = 0;
        const bool forceOk = SafeReadPtr(corpsPtr + 0x10, &forcePtr);
        const bool noOk = SafeReadPtrAllowZero(corpsPtr + 0x18, &corpsNoRaw);
        const bool govOk = SafeReadPtr(corpsPtr + 0x20, &governorGeneralPtr);

        ImGui::Text(u8"군단 +0x10 세력 후보: %s0x%llX",
                    forceOk ? "" : u8"(읽기 실패) ",
                    (unsigned long long)forcePtr);
        ImGui::Text(u8"군단 +0x18 군단 번호: %s%llu (0x%llX)",
                    noOk ? "" : u8"(읽기 실패) ",
                    (unsigned long long)corpsNoRaw,
                    (unsigned long long)corpsNoRaw);
        ImGui::Text(u8"군단 +0x20 도독 후보: %s0x%llX",
                    govOk ? "" : u8"(읽기 실패) ",
                    (unsigned long long)governorGeneralPtr);
      }

      if (ImGui::Button(u8"군단 포인터 로그##CityCorpsPtrLog",
                        ImVec2(145.f * sc, 0.f))) {
        uintptr_t forcePtr = 0;
        uintptr_t corpsNoRaw = 0;
        uintptr_t governorGeneralPtr = 0;
        const bool forceOk =
            corpsPtr > 0x10000 && SafeReadPtr(corpsPtr + 0x10, &forcePtr);
        const bool noOk =
            corpsPtr > 0x10000 && SafeReadPtrAllowZero(corpsPtr + 0x18, &corpsNoRaw);
        const bool govOk =
            corpsPtr > 0x10000 && SafeReadPtr(corpsPtr + 0x20, &governorGeneralPtr);

        AddLog(u8"[군단DBG] 도시 %s Raw=0x%llX",
               g_CityList[s_officerCityIndex].cityname,
               (unsigned long long)rawCity);
        AddLog(u8"[군단DBG] City+0x90 = 0x%llX",
               (unsigned long long)corpsPtr);
        AddLog(u8"[군단DBG] Corps+0x10 세력 후보 = %s0x%llX",
               forceOk ? "" : u8"(읽기 실패) ",
               (unsigned long long)forcePtr);
        AddLog(u8"[군단DBG] Corps+0x18 군단 번호 = %s%llu (0x%llX)",
               noOk ? "" : u8"(읽기 실패) ",
               (unsigned long long)corpsNoRaw,
               (unsigned long long)corpsNoRaw);
        AddLog(u8"[군단DBG] Corps+0x20 도독 후보 = %s0x%llX",
               govOk ? "" : u8"(읽기 실패) ",
               (unsigned long long)governorGeneralPtr);
      }
    }

    static bool MoveSelectedOfficerToCity(uintptr_t p1,
                                          uintptr_t shiftedCityBase) {
      const CityOfficerRow *selected = FindSelectedCityOfficer();
      if (!selected || s_officerMoveTargetCity < 0 ||
          s_officerMoveTargetCity >= g_CityCount)
        return false;

      const uint16_t officerId = selected->id;
      const uintptr_t targetRaw =
          GetRawCityBase(shiftedCityBase, s_officerMoveTargetCity);
      if (!targetRaw ||
          GetCityForcePtr(targetRaw) != s_officerPlayerForce) {
        AddNotification(u8"무장 이동: 목적 도시가 현재 주인공 세력 소유가 아닙니다.");
        return false;
      }

      uintptr_t rosterBase = 0;
      const uintptr_t exe = (uintptr_t)GetModuleHandle(NULL);
      if (!exe ||
          !TryResolveOfficerRosterArrayBase(exe, &rosterBase) ||
          rosterBase <= 0x10000)
        return false;

      uintptr_t targetOfficerBase = 0;
      for (int i = 0; i < 5102; ++i) {
        const uintptr_t officerBase =
            rosterBase + (uintptr_t)i * 0x3D0;
        uint16_t id = 0;
        if (!SafeRead16(officerBase + 0x08, &id))
          continue;
        if (id == officerId) {
          targetOfficerBase = officerBase;
          break;
        }
      }

      if (!targetOfficerBase)
        return false;

      uintptr_t forcePtr = 0, currentCity = 0;
      if (!SafeReadPtr(targetOfficerBase + 0x18, &forcePtr) ||
          !SafeReadPtr(targetOfficerBase + 0x20, &currentCity) ||
          forcePtr != s_officerPlayerForce) {
        AddNotification(u8"무장 이동: 현재 무장 소속 세력을 다시 확인해주세요.");
        return false;
      }

      uintptr_t sourceCorps = 0;
      uintptr_t targetCorps = 0;
      const bool sourceCorpsOk =
          SafeReadPtrAllowZero(currentCity + OFF_CITY_CORPS_RAW, &sourceCorps);
      const bool targetCorpsOk =
          SafeReadPtrAllowZero(targetRaw + OFF_CITY_CORPS_RAW, &targetCorps);
      if (!sourceCorpsOk || !targetCorpsOk) {
        AddNotification(u8"무장 이동: 출발/목적 도시의 군단 정보를 읽지 못했습니다.");
        return false;
      }

      const bool crossCorps = (sourceCorps != targetCorps);
      if (crossCorps && selected->status != 0x28) {
        AddNotification(u8"무장 이동: 다른 군단으로의 이동은 현재 일반 신분 무장만 허용합니다.");
        AddLog(u8"[도시 무장] 군단 간 이동 차단: ID %u / 신분 0x%02X / 출발군단 0x%llX / 목적군단 0x%llX",
               (unsigned int)officerId,
               (unsigned int)selected->status,
               (unsigned long long)sourceCorps,
               (unsigned long long)targetCorps);
        return false;
      }

      if (!SafeWritePtr(targetOfficerBase + 0x20, targetRaw)) {
        AddNotification(u8"무장 이동: 도시 포인터 쓰기에 실패했습니다.");
        return false;
      }

      const std::string name =
          g_officerNames.count(officerId)
              ? g_officerNames[officerId]
              : (u8"무장 ID " + std::to_string((int)officerId));

      char notice[256]{};
      sprintf_s(notice, u8"%s → %s 이동 완료",
                name.c_str(),
                g_CityList[s_officerMoveTargetCity].cityname);
      AddNotification(notice);
      AddLog(u8"[도시 무장] %s -> %s 이동 (+0x20 도시 포인터 변경)%s",
             name.c_str(),
             g_CityList[s_officerMoveTargetCity].cityname,
             crossCorps ? u8" [군단 간 전속]" : "");
      AddLog(u8"[도시 무장] 군단: 0x%llX -> 0x%llX (군단 포인터 직접 쓰기 없음)",
             (unsigned long long)sourceCorps,
             (unsigned long long)targetCorps);

      s_selectedOfficerId = -1;
      s_officerRosterDirty = true;
      RefreshCityOfficerRoster(p1, shiftedCityBase);
      return true;
    }

    static void DrawCityOfficerRoster(uintptr_t p1,
                                      uintptr_t shiftedCityBase,
                                      float sc) {
      if (s_officerRosterDirty)
        RefreshCityOfficerRoster(p1, shiftedCityBase);

      ImGui::Spacing();
      ImGui::TextColored(ImVec4(0.55f, 0.90f, 1.0f, 1.f),
                         u8"[ 내 도시 무장 배치 ]");
      ImGui::SameLine(0.f, 18.f * sc);

      struct CorpsFilterOption {
        uintptr_t corpsPtr = 0;
        uintptr_t corpsNo = 0;
      };
      std::vector<CorpsFilterOption> corpsOptions;
      for (int idx : s_officerPlayerCities) {
        const OfficerCityCorpsInfo info =
            GetOfficerCityCorpsInfo(shiftedCityBase, idx);
        if (!info.readable)
          continue;
        bool exists = false;
        for (const auto &opt : corpsOptions) {
          if (opt.corpsPtr == info.corpsPtr) {
            exists = true;
            break;
          }
        }
        if (!exists)
          corpsOptions.push_back({info.corpsPtr, info.corpsNo});
      }
      std::sort(corpsOptions.begin(), corpsOptions.end(),
                [](const CorpsFilterOption &a, const CorpsFilterOption &b) {
                  if ((a.corpsPtr <= 0x10000) != (b.corpsPtr <= 0x10000))
                    return a.corpsPtr <= 0x10000;
                  if (a.corpsNo != b.corpsNo)
                    return a.corpsNo < b.corpsNo;
                  return a.corpsPtr < b.corpsPtr;
                });

      bool corpsFilterValid =
          (s_officerCorpsFilter == OFFICER_CORPS_FILTER_ALL);
      for (const auto &opt : corpsOptions) {
        if (opt.corpsPtr == s_officerCorpsFilter) {
          corpsFilterValid = true;
          break;
        }
      }
      if (!corpsFilterValid)
        s_officerCorpsFilter = OFFICER_CORPS_FILTER_ALL;

      std::string corpsFilterPreview = u8"전체";
      if (s_officerCorpsFilter != OFFICER_CORPS_FILTER_ALL) {
        if (s_officerCorpsFilter <= 0x10000) {
          corpsFilterPreview = u8"직할";
        } else {
          for (const auto &opt : corpsOptions) {
            if (opt.corpsPtr == s_officerCorpsFilter) {
              corpsFilterPreview =
                  opt.corpsNo
                      ? (std::to_string((unsigned long long)opt.corpsNo) + u8"군단")
                      : std::string(u8"군단 ?");
              break;
            }
          }
        }
      }

      ImGui::TextUnformatted(u8"군단");
      ImGui::SameLine(0.f, 6.f * sc);
      ImGui::SetNextItemWidth(95.f * sc);
      if (ImGui::BeginCombo("##OfficerCorpsFilter",
                            corpsFilterPreview.c_str())) {
        const bool allSelected =
            (s_officerCorpsFilter == OFFICER_CORPS_FILTER_ALL);
        if (ImGui::Selectable(u8"전체", allSelected)) {
          s_officerCorpsFilter = OFFICER_CORPS_FILTER_ALL;
        }
        if (allSelected)
          ImGui::SetItemDefaultFocus();

        for (const auto &opt : corpsOptions) {
          std::string label;
          if (opt.corpsPtr <= 0x10000)
            label = u8"직할";
          else if (opt.corpsNo)
            label = std::to_string((unsigned long long)opt.corpsNo) + u8"군단";
          else
            label = u8"군단 ?";

          const bool selected =
              (s_officerCorpsFilter == opt.corpsPtr);
          if (ImGui::Selectable(label.c_str(), selected)) {
            s_officerCorpsFilter = opt.corpsPtr;

            if (!OfficerCityMatchesCorpsFilter(shiftedCityBase,
                                               s_officerCityIndex)) {
              s_officerCityIndex = -1;
              for (int idx : s_officerPlayerCities) {
                if (OfficerCityMatchesCorpsFilter(shiftedCityBase, idx)) {
                  s_officerCityIndex = idx;
                  break;
                }
              }
              s_selectedOfficerId = -1;
              s_officerRosterDirty = true;
              RefreshCityOfficerRoster(p1, shiftedCityBase);
            }
          }
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::SameLine(0.f, 12.f * sc);
      ImGui::TextUnformatted(u8"도시");
      ImGui::SameLine(0.f, 6.f * sc);

      const std::string cityName =
          BuildOfficerCityCorpsLabel(shiftedCityBase, s_officerCityIndex);

      ImGui::SetNextItemWidth(165.f * sc);
      if (ImGui::BeginCombo("##OfficerCity", cityName.c_str())) {
        for (int idx : s_officerPlayerCities) {
          if (!OfficerCityMatchesCorpsFilter(shiftedCityBase, idx))
            continue;

          const std::string label =
              BuildOfficerCityCorpsLabel(shiftedCityBase, idx);
          const bool selected = (idx == s_officerCityIndex);
          if (ImGui::Selectable(label.c_str(), selected)) {
            s_officerCityIndex = idx;
            s_selectedOfficerId = -1;
            NormalizeOfficerCitySelections();
            s_officerRosterDirty = true;
            RefreshCityOfficerRoster(p1, shiftedCityBase);
          }
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::SameLine(0.f, 10.f * sc);
      if (ImGui::SmallButton(u8"새로고침##OfficerRoster")) {
        s_officerRosterDirty = true;
        RefreshCityOfficerRoster(p1, shiftedCityBase);
      }

      if (s_officerCityIndex >= 0) {
        const bool frontline =
            IsCityFrontlineForForce(shiftedCityBase,
                                    s_officerCityIndex,
                                    s_officerPlayerForce);
        ImGui::SameLine(0.f, 16.f * sc);
        if (frontline)
          ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.2f, 1.f),
                             u8"전선");
        else
          ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.45f, 1.f),
                             u8"후방");

        ImGui::SameLine(0.f, 12.f * sc);
        ImGui::TextDisabled(u8"무장 %d명",
                            (int)s_cityOfficerRows.size());
      }

      ImGui::Separator();

      static ImGuiTableFlags officerFlags =
          ImGuiTableFlags_BordersInner |
          ImGuiTableFlags_RowBg |
          ImGuiTableFlags_ScrollY |
          ImGuiTableFlags_SizingFixedFit |
          ImGuiTableFlags_NoSavedSettings;

      const float tableH = 360.f * sc;
      if (ImGui::BeginTable("##CityOfficerTbl", 8, officerFlags,
                            ImVec2(0.f, tableH))) {
        ImGui::TableSetupScrollFreeze(2, 1);
        ImGui::TableSetupColumn(u8"신분",
                                ImGuiTableColumnFlags_WidthFixed,
                                62.f * sc);
        ImGui::TableSetupColumn(u8"이름",
                                ImGuiTableColumnFlags_WidthStretch,
                                1.5f);
        ImGui::TableSetupColumn(u8"충성",
                                ImGuiTableColumnFlags_WidthFixed,
                                55.f * sc);
        ImGui::TableSetupColumn(u8"통솔",
                                ImGuiTableColumnFlags_WidthFixed,
                                55.f * sc);
        ImGui::TableSetupColumn(u8"무력",
                                ImGuiTableColumnFlags_WidthFixed,
                                55.f * sc);
        ImGui::TableSetupColumn(u8"지력",
                                ImGuiTableColumnFlags_WidthFixed,
                                55.f * sc);
        ImGui::TableSetupColumn(u8"정치",
                                ImGuiTableColumnFlags_WidthFixed,
                                55.f * sc);
        ImGui::TableSetupColumn(u8"매력",
                                ImGuiTableColumnFlags_WidthFixed,
                                55.f * sc);
        ImGui::TableHeadersRow();

        for (const auto &row : s_cityOfficerRows) {
          ImGui::TableNextRow();

          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(GetOfficerStatusName(row.status));

          ImGui::TableSetColumnIndex(1);
          const std::string name =
              g_officerNames.count(row.id)
                  ? g_officerNames[row.id]
                  : (u8"무장 ID " + std::to_string((int)row.id));
          const bool selected =
              ((int)row.id == s_selectedOfficerId);
          if (ImGui::Selectable(name.c_str(), selected,
                                ImGuiSelectableFlags_SpanAllColumns))
            s_selectedOfficerId = row.id;

          ImGui::TableSetColumnIndex(2);
          ImGui::Text("%u", (unsigned int)row.loyalty);
          ImGui::TableSetColumnIndex(3);
          ImGui::Text("%u", (unsigned int)row.lead);
          ImGui::TableSetColumnIndex(4);
          ImGui::Text("%u", (unsigned int)row.war);
          ImGui::TableSetColumnIndex(5);
          ImGui::Text("%u", (unsigned int)row.intel);
          ImGui::TableSetColumnIndex(6);
          ImGui::Text("%u", (unsigned int)row.pol);
          ImGui::TableSetColumnIndex(7);
          ImGui::Text("%u", (unsigned int)row.cha);
        }

        ImGui::EndTable();
      }

      const CityOfficerRow *selected = FindSelectedCityOfficer();
      ImGui::Spacing();
      if (selected) {
        const std::string name =
            g_officerNames.count(selected->id)
                ? g_officerNames[selected->id]
                : (u8"무장 ID " +
                   std::to_string((int)selected->id));
        ImGui::Text(u8"선택: %s (%s)",
                    name.c_str(),
                    GetOfficerStatusName(selected->status));

        if (s_officerCityIndex >= 0) {
          const std::string currentCorps =
              GetOfficerCorpsName(shiftedCityBase, s_officerCityIndex);
          ImGui::SameLine(0.f, 18.f * sc);
          ImGui::TextDisabled(u8"현재 소속: %s / %s",
                              currentCorps.c_str(),
                              g_CityList[s_officerCityIndex].cityname);
        }
      } else {
        ImGui::TextDisabled(u8"이동할 무장을 목록에서 선택하세요.");
      }

      ImGui::SameLine(0.f, 24.f * sc);
      ImGui::TextUnformatted(u8"이동할 도시");
      ImGui::SameLine(0.f, 8.f * sc);

      const std::string targetName =
          BuildOfficerCityCorpsLabel(shiftedCityBase,
                                     s_officerMoveTargetCity);
      ImGui::SetNextItemWidth(165.f * sc);
      if (ImGui::BeginCombo("##OfficerMoveTarget", targetName.c_str())) {
        for (int idx : s_officerPlayerCities) {
          if (idx == s_officerCityIndex)
            continue;
          const std::string label =
              BuildOfficerCityCorpsLabel(shiftedCityBase, idx);
          const bool isSelected =
              (idx == s_officerMoveTargetCity);
          if (ImGui::Selectable(label.c_str(), isSelected))
            s_officerMoveTargetCity = idx;
          if (isSelected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      if (s_officerMoveTargetCity >= 0 &&
          s_officerMoveTargetCity < g_CityCount) {
        const std::string targetCorps =
            GetOfficerCorpsName(shiftedCityBase, s_officerMoveTargetCity);
        ImGui::SameLine(0.f, 10.f * sc);
        ImGui::TextDisabled(u8"→ %s", targetCorps.c_str());
      }

      ImGui::SameLine(0.f, 12.f * sc);
      const bool canMove =
          selected != nullptr && s_officerMoveTargetCity >= 0;
      if (!canMove)
        ImGui::BeginDisabled();
      if (ImGui::Button(u8"선택도시로 이동##MoveCityOfficer",
                        ImVec2(145.f * sc, 0.f)))
        MoveSelectedOfficerToCity(p1, shiftedCityBase);
      if (!canMove)
        ImGui::EndDisabled();

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            u8"무장의 신분/충성/능력치는 건드리지 않고 +0x20 도시 포인터만 변경합니다.");
        ImGui::TextUnformatted(
            u8"현재 주인공 세력이 소유한 도시끼리 이동하며, 목적 도시의 +0x90 군단 소속을 자동으로 따릅니다.");
        ImGui::TextUnformatted(
            u8"다른 군단으로의 전속은 현재 일반 신분(0x28) 무장만 허용합니다.");
        ImGui::EndTooltip();
      }

      DrawGovernorDebugPanel(p1, shiftedCityBase, sc);
      DrawCityCorpsPointerDebug(shiftedCityBase, sc);
    }

  } // anonymous namespace

  // ═══════════════════════════════════════════════════════════════════════════
  //  공개 API
  // ═══════════════════════════════════════════════════════════════════════════
  void DrawCityInfoWindow(uintptr_t p1, float scale) {
    if (!bShowCityInfoWin) {
      s_frontierDirty = true;
      s_officerRosterDirty = true;
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

      if (ImGui::BeginTabItem(u8"수송")) {
        DrawFrontierAnalysis(p1, cityBase, scale);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem(u8"무장 배치")) {
        DrawCityOfficerRoster(p1, cityBase, scale);
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }

    ImGui::End();
  }

  void RunYearlyRearSupport(uintptr_t p1) {
    static ULONGLONG s_lastPollMs = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - s_lastPollMs < 500)
      return;
    s_lastPollMs = now;

    LoadAutoSupportRoutes();

    uint16_t year = 0;
    uint8_t month = 0;
    if (!ReadScenarioDate(&year, &month) || month < 1 || month > 12)
      return;

    // 처음 관측한 시점은 기준값만 잡고 소급 실행하지 않는다.
    if (s_autoSupportLastObservedMonth == 0) {
      s_autoSupportLastObservedMonth = month;
      s_autoSupportLastObservedYear = year;
      return;
    }

    const bool crossedNewYear =
        (s_autoSupportLastObservedMonth == 12 && month == 1);

    s_autoSupportLastObservedMonth = month;
    s_autoSupportLastObservedYear = year;

    if (!crossedNewYear || !s_autoSupportYearlyEnabled)
      return;

    if (p1 <= 0x10000 || s_autoSupportRoutes.empty())
      return;

    const uintptr_t cityBase = GetCityArrBase();
    if (cityBase <= 0x10000) {
      AddNotification(u8"자동 후방지원: 도시 데이터를 읽지 못해 이번 연도 지원을 실행하지 못했습니다.");
      return;
    }

    // 연간 자동지원은 UI에서 보고 있던 세력과 무관하게 주인공 세력 기준으로 실행한다.
    const uintptr_t previousSelectedForce = s_frontierSelectedForce;
    RefreshFrontierAnalysis(p1, cityBase);
    if (!s_frontierPlayerForce) {
      AddNotification(u8"자동 후방지원: 주인공 세력을 확인하지 못해 이번 연도 지원을 건너뜁니다.");
      return;
    }

    s_frontierSelectedForce = s_frontierPlayerForce;
    RefreshFrontierAnalysis(p1, cityBase);

    int enabledCount = 0;
    int successCount = 0;
    for (size_t i = 0; i < s_autoSupportRoutes.size(); ++i) {
      if (!s_autoSupportRoutes[i].enabled)
        continue;
      enabledCount++;
      if (ExecuteAutoSupportRoute(i, p1, cityBase))
        successCount++;
    }

    char notice[256]{};
    sprintf_s(notice, u8"%u년 자동 후방지원 완료: 활성 %d개 / 실행 %d개",
              (unsigned int)year, enabledCount, successCount);
    AddNotification(notice);
    AddLog(u8"[자동 후방지원] %u년 1월 자동 실행 완료 (활성 %d / 실행 %d)",
           (unsigned int)year, enabledCount, successCount);

    // 사용자가 전선 분석에서 보고 있던 세력 선택은 가능한 한 복구한다.
    s_frontierSelectedForce = previousSelectedForce;
    RefreshFrontierAnalysis(p1, cityBase);
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
