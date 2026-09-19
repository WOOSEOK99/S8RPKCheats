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
#include "../../debug.h"
#include "../Officer/OfficerData.h"
#include "../Officer/OfficerRosterResolve.h"
#include "../System/MonthCapture.h"
#include "CityData.h"
#include <windows.h>
#include <algorithm>
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

  // 군단 자동배치 저장 설정. 포인터 자체는 저장하지 않고 안정적인 식별값만 저장한다.
  bool g_corpsAutoDeploymentEachCouncil = false;
  int g_corpsAutoScenarioId = 0;
  int g_corpsAutoScenarioStartYear = 0;
  int g_corpsAutoScenarioStartMonth = 0;
  int g_corpsAutoForceLordId = 0;
  int g_corpsAutoCorpsNo = 0;
  int g_corpsAutoGovernorGeneralId = 0;

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

    static bool SafeReadS32(uintptr_t addr, int32_t *out) {
      if (!out)
        return false;
      __try {
        *out = *(int32_t *)addr;
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = 0;
        return false;
      }
    }

    static bool SafeGetModuleImageEnd(
        uintptr_t exeBase, uintptr_t *outImageEnd) {
      if (!exeBase || !outImageEnd)
        return false;
      __try {
        const IMAGE_DOS_HEADER *dos =
            (const IMAGE_DOS_HEADER *)exeBase;
        const IMAGE_NT_HEADERS *nt =
            (const IMAGE_NT_HEADERS *)(
                exeBase + (uintptr_t)dos->e_lfanew);
        *outImageEnd =
            exeBase + (uintptr_t)nt->OptionalHeader.SizeOfImage;
        return *outImageEnd > exeBase;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outImageEnd = 0;
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

      // 1순위: 현재 태수(City+0x98) -> Officer+0x18 세력
      uintptr_t ownerLink = 0;
      uintptr_t forcePtr = 0;
      if (SafeReadPtrAllowZero(rawCity + OFF_CITY_FORCE_LINK_RAW, &ownerLink) &&
          ownerLink > 0x10000 &&
          SafeReadPtr(ownerLink + 0x18, &forcePtr) &&
          forcePtr > 0x10000) {
        return forcePtr;
      }

      // 2순위: 태수/장수가 없는 도시도 군단 소속이 남아 있을 수 있음.
      // City+0x90 -> DivisionData+0x10 세력을 fallback으로 사용한다.
      uintptr_t corpsPtr = 0;
      forcePtr = 0;
      if (SafeReadPtrAllowZero(rawCity + OFF_CITY_CORPS_RAW, &corpsPtr) &&
          corpsPtr > 0x10000 &&
          SafeReadPtr(corpsPtr + 0x10, &forcePtr) &&
          forcePtr > 0x10000) {
        return forcePtr;
      }

      return 0;
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
    static std::vector<CityOfficerRow> s_corpsOfficerRows;
    static uintptr_t s_officerSelectedCorpsPtr = 0;
    static int s_officerCityIndex = -1;
    static int s_officerMoveTargetCity = -1;
    static int s_selectedOfficerId = -1;
    static uintptr_t s_officerPlayerForce = 0;
    static bool s_officerRosterDirty = true;
    static constexpr uintptr_t OFFICER_CORPS_FILTER_ALL = ~(uintptr_t)0;
    static uintptr_t s_officerCorpsFilter = OFFICER_CORPS_FILTER_ALL;

    struct CorpsDeploymentCityRecommendation {
      int cityIndex = -1;
      bool frontline = false;
      int currentCount = 0;
      int targetCount = 0;
      uint16_t currentGovernorId = 0;
      uint16_t recommendedGovernorId = 0;
      int governorScore = 0;
      std::vector<uint16_t> recommendedOfficerIds;
    };

    struct CorpsDeploymentOfficerRecommendation {
      uint16_t id = 0;
      uint8_t status = 0;
      uint8_t loyalty = 0;
      int currentCityIndex = -1;
      int recommendedCityIndex = -1;
      bool recommendedGovernor = false;
      std::string reason;
    };

    static std::vector<CorpsDeploymentCityRecommendation>
        s_corpsDeploymentCities;
    static std::vector<CorpsDeploymentOfficerRecommendation>
        s_corpsDeploymentOfficers;
    static uintptr_t s_corpsDeploymentCorpsPtr = 0;
    static bool s_corpsDeploymentValid = false;

    struct CorpsDeploymentTargetBaseline {
      int cityIndex = -1;
      int targetCount = 0;
    };
    static uintptr_t s_corpsDeploymentTargetCorpsPtr = 0;
    static std::vector<CorpsDeploymentTargetBaseline>
        s_corpsDeploymentTargetBaseline;
    static uint8_t s_corpsDeploymentLastRelevantGameState = 0;
    static constexpr uint8_t CORPS_DEPLOY_STATE_COUNCIL = 0x05;
    static constexpr uint8_t CORPS_DEPLOY_STATE_DOMESTIC = 0x07;

    // 설정 파일에서 자동배치 선호/대상은 복원하지만 실제 자동 실행은
    // 현재 실행 세션에서 사용자가 다시 대상을 지정한 뒤에만 허용한다.
    static bool s_corpsAutoSessionArmed = false;
    static uintptr_t s_corpsAutoArmedForcePtr = 0;
    static uintptr_t s_corpsAutoArmedCorpsPtr = 0;
    static uint16_t s_corpsAutoLastObservedYear = 0;
    static uint8_t s_corpsAutoLastObservedMonth = 0;

    struct OfficerCityCorpsInfo {
      bool readable = false;
      uintptr_t corpsPtr = 0;
      uintptr_t corpsNo = 0;
      uintptr_t forcePtr = 0;
      uintptr_t governorGeneralPtr = 0;
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
      if (info.corpsPtr > 0x10000) {
        SafeReadPtrAllowZero(info.corpsPtr + 0x18, &info.corpsNo);
        SafeReadPtr(info.corpsPtr + 0x10, &info.forcePtr);
        SafeReadPtrAllowZero(info.corpsPtr + 0x20, &info.governorGeneralPtr);
      } else {
        // 직할 도시는 DivisionData가 없으므로 도시의 소유 세력으로 표시한다.
        info.forcePtr = GetCityForcePtr(rawCity);
      }
      return info;
    }

    static bool ReadOfficerIdFromPtr(
        uintptr_t officerPtr, uint16_t *outId) {
      if (!outId || officerPtr <= 0x10000)
        return false;
      uint16_t id = 0;
      if (!SafeRead16(officerPtr + 0x08, &id) ||
          id == 0 || id > 5102)
        return false;
      *outId = id;
      return true;
    }

    static bool ReadForceLordId(
        uintptr_t forcePtr, uint16_t *outId) {
      if (!outId || forcePtr <= 0x10000)
        return false;
      uintptr_t lordPtr = 0;
      return SafeReadPtr(forcePtr + 0xC0, &lordPtr) &&
             ReadOfficerIdFromPtr(lordPtr, outId);
    }

    static bool ReadCorpsGovernorGeneralId(
        uintptr_t corpsPtr, uint16_t *outId) {
      if (!outId || corpsPtr <= 0x10000)
        return false;
      uintptr_t governorPtr = 0;
      if (!SafeReadPtrAllowZero(
              corpsPtr + 0x20, &governorPtr) ||
          governorPtr <= 0x10000)
        return false;
      return ReadOfficerIdFromPtr(governorPtr, outId);
    }

    static void DisarmCorpsAutoDeploymentSession(
        const char *reason, bool notifyUser = false) {
      if (!s_corpsAutoSessionArmed)
        return;

      s_corpsAutoSessionArmed = false;
      s_corpsAutoArmedForcePtr = 0;
      s_corpsAutoArmedCorpsPtr = 0;
      s_corpsAutoLastObservedYear = 0;
      s_corpsAutoLastObservedMonth = 0;

      AddLog(u8"[군단 자동배치] 현재 세이브 자동 실행 해제: %s",
             reason ? reason : u8"상태 변경");
      if (notifyUser)
        AddNotification(
            u8"군단 자동배치: 현재 게임/세이브 상태가 달라 자동 실행을 해제했습니다. 대상 군단을 다시 지정해주세요.");
    }

    static bool BindCorpsAutoDeploymentTarget(
        const OfficerCityCorpsInfo &info) {
      if (!info.readable || info.corpsPtr <= 0x10000 ||
          info.forcePtr <= 0x10000 || info.corpsNo == 0)
        return false;

      uint8_t scenarioId = 0;
      unsigned short startYear = 0;
      uint8_t startMonth = 0;
      uint16_t lordId = 0;
      uint16_t governorGeneralId = 0;

      if (!ReadScenarioIdentity(
              &scenarioId, &startYear, &startMonth) ||
          !ReadForceLordId(info.forcePtr, &lordId) ||
          !ReadCorpsGovernorGeneralId(
              info.corpsPtr, &governorGeneralId))
        return false;

      g_corpsAutoScenarioId = (int)scenarioId;
      g_corpsAutoScenarioStartYear = (int)startYear;
      g_corpsAutoScenarioStartMonth = (int)startMonth;
      g_corpsAutoForceLordId = (int)lordId;
      g_corpsAutoCorpsNo = (int)info.corpsNo;
      g_corpsAutoGovernorGeneralId =
          (int)governorGeneralId;

      s_corpsAutoSessionArmed = true;
      s_corpsAutoArmedForcePtr = info.forcePtr;
      s_corpsAutoArmedCorpsPtr = info.corpsPtr;
      ReadScenarioDate(
          &s_corpsAutoLastObservedYear,
          &s_corpsAutoLastObservedMonth);

      SaveConfig();
      AddLog(u8"[군단 자동배치] 자동 대상 지정: 시나리오 %d / 군주 %u / %llu군단 / 도독 %u / corps 0x%llX",
             g_corpsAutoScenarioId,
             (unsigned int)lordId,
             (unsigned long long)info.corpsNo,
             (unsigned int)governorGeneralId,
             (unsigned long long)info.corpsPtr);
      return true;
    }

    static bool MatchesStoredCorpsAutoIdentity(
        const OfficerCityCorpsInfo &info) {
      if (!info.readable || info.corpsPtr <= 0x10000 ||
          info.forcePtr <= 0x10000 || info.corpsNo == 0)
        return false;

      uint8_t scenarioId = 0;
      unsigned short startYear = 0;
      uint8_t startMonth = 0;
      uint16_t lordId = 0;
      uint16_t governorGeneralId = 0;

      if (!ReadScenarioIdentity(
              &scenarioId, &startYear, &startMonth) ||
          !ReadForceLordId(info.forcePtr, &lordId) ||
          !ReadCorpsGovernorGeneralId(
              info.corpsPtr, &governorGeneralId))
        return false;

      return (int)scenarioId == g_corpsAutoScenarioId &&
             (int)startYear == g_corpsAutoScenarioStartYear &&
             (int)startMonth == g_corpsAutoScenarioStartMonth &&
             (int)lordId == g_corpsAutoForceLordId &&
             (int)info.corpsNo == g_corpsAutoCorpsNo &&
             (int)governorGeneralId ==
                 g_corpsAutoGovernorGeneralId;
    }

    static std::string GetOfficerForceShortName(uintptr_t forcePtr) {
      std::string name = BuildForceName(forcePtr);
      const std::string suffix = u8" 세력";
      if (name.size() >= suffix.size() &&
          name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        name.erase(name.size() - suffix.size());
      }
      return name;
    }

    static std::string GetOfficerCorpsName(uintptr_t shiftedCityBase,
                                           int cityIndex) {
      const OfficerCityCorpsInfo info =
          GetOfficerCityCorpsInfo(shiftedCityBase, cityIndex);
      if (!info.readable)
        return u8"군단 ?";

      const std::string forceName =
          info.forcePtr ? GetOfficerForceShortName(info.forcePtr)
                        : std::string(u8"미확인");

      // 직할은 군단 도독이 없으므로 세력 군주 이름으로 표시.
      if (info.corpsPtr <= 0x10000)
        return forceName + u8" 직할";

      // 일반 군단은 세력 군주가 아니라 Division+0x20 도독 이름으로 구분한다.
      std::string governorName = u8"도독 ?";
      if (info.governorGeneralPtr > 0x10000) {
        uint16_t governorId = 0;
        if (SafeRead16(info.governorGeneralPtr + 0x08, &governorId) &&
            governorId >= 1 && governorId <= 5102) {
          auto it = g_officerNames.find((int)governorId);
          if (it != g_officerNames.end() && !it->second.empty())
            governorName = it->second;
          else
            governorName = u8"무장 ID " + std::to_string((int)governorId);
        }
      }

      if (info.corpsNo)
        return governorName + " " +
               std::to_string((unsigned long long)info.corpsNo) + u8"군단";
      return governorName + u8" 군단 ?";
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
      s_corpsOfficerRows.clear();
      s_officerSelectedCorpsPtr = 0;
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
      SafeReadPtrAllowZero(selectedCityRaw + OFF_CITY_CORPS_RAW,
                           &s_officerSelectedCorpsPtr);

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
        if (forcePtr != s_officerPlayerForce)
          continue;

        if (cityPtr == selectedCityRaw)
          s_cityOfficerRows.push_back(row);

        if (s_officerSelectedCorpsPtr > 0x10000 && cityPtr > 0x10000) {
          uintptr_t officerCorps = 0;
          if (SafeReadPtrAllowZero(cityPtr + OFF_CITY_CORPS_RAW,
                                   &officerCorps) &&
              officerCorps == s_officerSelectedCorpsPtr) {
            s_corpsOfficerRows.push_back(row);
          }
        }
      }

      auto sortOfficerRows = [](std::vector<CityOfficerRow> &rows) {
        std::sort(rows.begin(), rows.end(),
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
      };
      sortOfficerRows(s_cityOfficerRows);
      sortOfficerRows(s_corpsOfficerRows);

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

    // ── 태수 / 도독 교체 선택 상태 ────────────────────────────────────────
    static int s_governorOldId = -1;
    static int s_governorNewId = -1;
    static int s_governorGeneralOldId = -1;
    static int s_governorGeneralNewId = -1;

    static const CityOfficerRow *FindCityOfficerById(int officerId) {
      for (const auto &row : s_cityOfficerRows) {
        if ((int)row.id == officerId)
          return &row;
      }
      return nullptr;
    }

    static std::string BuildOfficerName(uint16_t officerId) {
      auto it = g_officerNames.find((int)officerId);
      if (it != g_officerNames.end() && !it->second.empty())
        return it->second;
      return u8"무장 ID " + std::to_string((int)officerId);
    }

    static const CityOfficerRow *FindCorpsOfficerById(int officerId) {
      for (const auto &row : s_corpsOfficerRows) {
        if ((int)row.id == officerId)
          return &row;
      }
      return nullptr;
    }

    static void NormalizeGovernorGeneralSelections() {
      const CityOfficerRow *oldGov =
          FindCorpsOfficerById(s_governorGeneralOldId);
      if (!oldGov || oldGov->status != 0xD8) {
        s_governorGeneralOldId = -1;
        for (const auto &row : s_corpsOfficerRows) {
          if (row.status == 0xD8) {
            s_governorGeneralOldId = row.id;
            break;
          }
        }
      }

      const CityOfficerRow *candidate =
          FindCorpsOfficerById(s_governorGeneralNewId);
      if (!candidate || candidate->status != 0xE8) {
        s_governorGeneralNewId = -1;
        for (const auto &row : s_corpsOfficerRows) {
          if (row.status == 0xE8) {
            s_governorGeneralNewId = row.id;
            break;
          }
        }
      }
    }

    static void NormalizeGovernorSelections() {
      const CityOfficerRow *oldGov = FindCityOfficerById(s_governorOldId);
      if (!oldGov || oldGov->status != 0xE8) {
        s_governorOldId = -1;
        for (const auto &row : s_cityOfficerRows) {
          if (row.status == 0xE8) {
            s_governorOldId = row.id;
            break;
          }
        }
      }

      const CityOfficerRow *candidate = FindCityOfficerById(s_governorNewId);
      if (!candidate || candidate->status != 0x28) {
        s_governorNewId = -1;
        for (const auto &row : s_cityOfficerRows) {
          if (row.status == 0x28) {
            s_governorNewId = row.id;
            break;
          }
        }
      }
    }


    static int GetOfficerCurrentCityIndex(
        uintptr_t shiftedCityBase, const CityOfficerRow &row) {
      uintptr_t cityPtr = 0;
      if (!SafeReadPtr(row.officerBase + 0x20, &cityPtr))
        return -1;
      for (int i = 0; i < g_CityCount; ++i) {
        if (GetRawCityBase(shiftedCityBase, i) == cityPtr)
          return i;
      }
      return -1;
    }

    static CorpsDeploymentCityRecommendation *FindDeploymentCity(int cityIndex) {
      for (auto &city : s_corpsDeploymentCities) {
        if (city.cityIndex == cityIndex)
          return &city;
      }
      return nullptr;
    }

    static const CityOfficerRow *FindCorpsOfficerByRecId(uint16_t officerId) {
      for (const auto &row : s_corpsOfficerRows) {
        if (row.id == officerId)
          return &row;
      }
      return nullptr;
    }

    static int GetFrontDeploymentScore(const CityOfficerRow &row) {
      // 전선은 통솔+무력형과 고지력형 모두 우대한다.
      const int combat = (int)row.lead + (int)row.war;
      const int intellect = (int)row.intel * 2;
      return (std::max)(combat, intellect);
    }

    static int GetRearDeploymentScore(const CityOfficerRow &row) {
      return (int)row.pol + (int)row.cha;
    }

    static int GetGovernorScore(const CityOfficerRow &row, bool frontline) {
      return frontline
                 ? ((int)row.lead + (int)row.war)
                 : ((int)row.pol + (int)row.cha);
    }

    static bool IsSafeFirstGovernorNormal(
        const CityOfficerRow &row) {
      if (row.status != 0x28 || row.loyalty != 100)
        return false;

      uint16_t pair = 0;
      return SafeRead16(row.officerBase + 0x10, &pair) &&
             pair == 0xD328u;
    }

    static int PickDeploymentCity(bool frontline) {
      CorpsDeploymentCityRecommendation *best = nullptr;
      int bestRemain = -1000000;
      int bestAssigned = 1000000;

      for (auto &city : s_corpsDeploymentCities) {
        if (city.frontline != frontline)
          continue;
        const int assigned = (int)city.recommendedOfficerIds.size();
        const int remain = city.targetCount - assigned;
        if (remain <= 0)
          continue;
        if (!best || remain > bestRemain ||
            (remain == bestRemain && assigned < bestAssigned) ||
            (remain == bestRemain && assigned == bestAssigned &&
             city.cityIndex < best->cityIndex)) {
          best = &city;
          bestRemain = remain;
          bestAssigned = assigned;
        }
      }

      return best ? best->cityIndex : -1;
    }

    static void AddOfficerDeploymentRecommendation(
        const CityOfficerRow &row, int currentCityIndex,
        int recommendedCityIndex, bool governor, const std::string &reason) {
      CorpsDeploymentOfficerRecommendation rec;
      rec.id = row.id;
      rec.status = row.status;
      rec.loyalty = row.loyalty;
      rec.currentCityIndex = currentCityIndex;
      rec.recommendedCityIndex = recommendedCityIndex;
      rec.recommendedGovernor = governor;
      rec.reason = reason;
      s_corpsDeploymentOfficers.push_back(rec);

      CorpsDeploymentCityRecommendation *city =
          FindDeploymentCity(recommendedCityIndex);
      if (city)
        city->recommendedOfficerIds.push_back(row.id);
    }

    static void ResetCorpsDeploymentTargetBaseline() {
      s_corpsDeploymentTargetCorpsPtr = 0;
      s_corpsDeploymentTargetBaseline.clear();
      s_corpsDeploymentValid = false;
      s_corpsDeploymentCities.clear();
      s_corpsDeploymentOfficers.clear();
      s_corpsDeploymentCorpsPtr = 0;
    }

    static bool HasValidCorpsDeploymentTargetBaseline(
        uintptr_t corpsPtr, const std::vector<int> &corpsCities) {
      if (corpsPtr <= 0x10000 ||
          s_corpsDeploymentTargetCorpsPtr != corpsPtr ||
          s_corpsDeploymentTargetBaseline.size() != corpsCities.size())
        return false;

      for (int cityIndex : corpsCities) {
        bool found = false;
        for (const auto &base : s_corpsDeploymentTargetBaseline) {
          if (base.cityIndex == cityIndex) {
            found = true;
            break;
          }
        }
        if (!found)
          return false;
      }
      return true;
    }

    static bool BuildCorpsDeploymentRecommendation(
        uintptr_t shiftedCityBase);
    static bool ApplyCorpsDeploymentMovements(
        uintptr_t p1, uintptr_t shiftedCityBase);
    static bool ApplyCorpsDeploymentGovernorStage(
        uintptr_t p1, uintptr_t shiftedCityBase);

    static int GetCorpsDeploymentBaselineTargetCount(int cityIndex) {
      for (const auto &base : s_corpsDeploymentTargetBaseline) {
        if (base.cityIndex == cityIndex)
          return base.targetCount;
      }
      return -1;
    }

    static void CaptureCorpsDeploymentTargetBaseline(
        uintptr_t corpsPtr,
        const std::vector<CorpsDeploymentCityRecommendation> &cities) {
      s_corpsDeploymentTargetBaseline.clear();
      for (const auto &city : cities) {
        CorpsDeploymentTargetBaseline base;
        base.cityIndex = city.cityIndex;
        base.targetCount = city.targetCount;
        s_corpsDeploymentTargetBaseline.push_back(base);
      }
      s_corpsDeploymentTargetCorpsPtr = corpsPtr;
    }

    static void RefreshCorpsDeploymentPlanCurrentState(
        uintptr_t shiftedCityBase) {
      if (!s_corpsDeploymentValid ||
          s_corpsDeploymentCorpsPtr <= 0x10000)
        return;

      for (auto &city : s_corpsDeploymentCities) {
        city.currentCount = 0;
        city.currentGovernorId = 0;

        const uintptr_t rawCity =
            GetRawCityBase(shiftedCityBase, city.cityIndex);
        uintptr_t governorPtr = 0;
        if (rawCity)
          SafeReadPtrAllowZero(rawCity + OFF_CITY_FORCE_LINK_RAW,
                               &governorPtr);

        for (const auto &row : s_corpsOfficerRows) {
          const int currentCity =
              GetOfficerCurrentCityIndex(shiftedCityBase, row);
          if (currentCity == city.cityIndex)
            ++city.currentCount;
          if (row.officerBase == governorPtr)
            city.currentGovernorId = row.id;
        }
      }

      for (auto &rec : s_corpsDeploymentOfficers) {
        const CityOfficerRow *row = FindCorpsOfficerByRecId(rec.id);
        if (!row)
          continue;
        rec.status = row->status;
        rec.loyalty = row->loyalty;
        rec.currentCityIndex =
            GetOfficerCurrentCityIndex(shiftedCityBase, *row);
      }
    }

    static uint8_t ReadCorpsDeploymentRelevantGameState() {
      const uintptr_t gameBase = GetGameBase();
      if (gameBase <= 0x10000)
        return 0;

      uint8_t state = 0;
      if (!SafeRead8(gameBase + 0xD0, &state))
        return 0;

      return (state == CORPS_DEPLOY_STATE_COUNCIL ||
              state == CORPS_DEPLOY_STATE_DOMESTIC)
                 ? state
                 : 0;
    }

    static void RunCorpsDeploymentPhaseMonitor(uintptr_t p1) {
      const uint8_t state = ReadCorpsDeploymentRelevantGameState();

      // 현재 세션에 묶인 자동설정은 세력 포인터/시나리오/날짜 역행을 계속 감시한다.
      if (s_corpsAutoSessionArmed) {
        uintptr_t playerCity = 0;
        uintptr_t currentForce = 0;
        uint16_t year = 0;
        uint8_t month = 0;
        uint8_t scenarioId = 0;
        unsigned short startYear = 0;
        uint8_t startMonth = 0;

        const bool contextOk =
            p1 > 0x10000 &&
            SafeReadPtr(p1 + 0x20, &playerCity) &&
            (currentForce = GetCityForcePtr(playerCity)) > 0x10000 &&
            currentForce == s_corpsAutoArmedForcePtr &&
            ReadScenarioIdentity(
                &scenarioId, &startYear, &startMonth) &&
            (int)scenarioId == g_corpsAutoScenarioId &&
            (int)startYear == g_corpsAutoScenarioStartYear &&
            (int)startMonth == g_corpsAutoScenarioStartMonth &&
            ReadScenarioDate(&year, &month);

        if (!contextOk) {
          DisarmCorpsAutoDeploymentSession(
              u8"세력/시나리오 식별값 변경", true);
        } else if (s_corpsAutoLastObservedYear != 0) {
          const int previous =
              (int)s_corpsAutoLastObservedYear * 12 +
              (int)s_corpsAutoLastObservedMonth;
          const int current =
              (int)year * 12 + (int)month;
          if (current < previous) {
            DisarmCorpsAutoDeploymentSession(
                u8"게임 날짜 역행(다른 세이브 로드 가능성)", true);
          }
        }

        if (s_corpsAutoSessionArmed) {
          s_corpsAutoLastObservedYear = year;
          s_corpsAutoLastObservedMonth = month;
        }
      }

      if (state == 0)
        return;

      if (s_corpsDeploymentLastRelevantGameState == 0) {
        s_corpsDeploymentLastRelevantGameState = state;
        return;
      }

      if (state == s_corpsDeploymentLastRelevantGameState)
        return;

      const uint8_t previous = s_corpsDeploymentLastRelevantGameState;
      s_corpsDeploymentLastRelevantGameState = state;

      if (previous != CORPS_DEPLOY_STATE_DOMESTIC ||
          state != CORPS_DEPLOY_STATE_COUNCIL)
        return;

      ResetCorpsDeploymentTargetBaseline();

      // 자동 사용 안 함이면 기존처럼 아무 계획도 자동 생성하지 않는다.
      // 수동 '추천 계산'은 UI에서 언제든 사용할 수 있다.
      if (!g_corpsAutoDeploymentEachCouncil) {
        AddLog(u8"[군단 자동배치] 평정 진입(07->05): 자동 실행 OFF");
        return;
      }

      if (!s_corpsAutoSessionArmed) {
        AddLog(u8"[군단 자동배치] 평정 진입(07->05): 설정은 ON이나 현재 세이브 대상 미지정 - 자동 실행 안 함");
        return;
      }

      const uintptr_t cityBase = GetCityArrBase();
      if (p1 <= 0x10000 || cityBase <= 0x10000) {
        AddLog(u8"[군단 자동배치] 평정 진입: 도시/주인공 데이터 없음 - 자동 실행 안 함");
        return;
      }

      // 먼저 현재 플레이어 세력/도시 목록을 다시 만든다.
      s_officerRosterDirty = true;
      RefreshCityOfficerRoster(p1, cityBase);
      if (s_officerPlayerForce != s_corpsAutoArmedForcePtr) {
        DisarmCorpsAutoDeploymentSession(
            u8"현재 플레이어 세력이 설정 당시와 다름", true);
        return;
      }

      int targetCityIndex = -1;
      OfficerCityCorpsInfo targetInfo;
      for (int cityIndex : s_officerPlayerCities) {
        const OfficerCityCorpsInfo info =
            GetOfficerCityCorpsInfo(cityBase, cityIndex);
        if (!MatchesStoredCorpsAutoIdentity(info))
          continue;
        targetCityIndex = cityIndex;
        targetInfo = info;
        break;
      }

      if (targetCityIndex < 0 ||
          targetInfo.corpsPtr != s_corpsAutoArmedCorpsPtr) {
        DisarmCorpsAutoDeploymentSession(
            u8"저장된 군단 식별값/포인터가 현재 세이브와 불일치", true);
        return;
      }

      const int previousCityIndex = s_officerCityIndex;
      s_officerCityIndex = targetCityIndex;
      s_officerRosterDirty = true;
      RefreshCityOfficerRoster(p1, cityBase);

      bool ok =
          s_officerSelectedCorpsPtr ==
              s_corpsAutoArmedCorpsPtr &&
          BuildCorpsDeploymentRecommendation(cityBase);

      if (ok)
        ok = ApplyCorpsDeploymentMovements(p1, cityBase);
      if (ok)
        ok = ApplyCorpsDeploymentGovernorStage(p1, cityBase);

      AddLog(u8"[군단 자동배치] 평정 자동 실행: %d군단 / 결과 %s",
             g_corpsAutoCorpsNo,
             ok ? u8"완료" : u8"실패");

      // UI에서 사용자가 보고 있던 도시는 가능한 한 복구한다.
      if (previousCityIndex >= 0 &&
          previousCityIndex < g_CityCount)
        s_officerCityIndex = previousCityIndex;
      s_officerRosterDirty = true;
      RefreshCityOfficerRoster(p1, cityBase);
    }


    static bool BuildCorpsDeploymentRecommendation(
        uintptr_t shiftedCityBase) {
      s_corpsDeploymentValid = false;
      s_corpsDeploymentCities.clear();
      s_corpsDeploymentOfficers.clear();
      s_corpsDeploymentCorpsPtr = 0;

      if (s_officerSelectedCorpsPtr <= 0x10000 ||
          s_officerPlayerForce <= 0x10000) {
        AddNotification(u8"군단 자동배치: 군단 소속 도시를 선택해주세요.");
        return false;
      }

      std::vector<int> corpsCities;
      for (int cityIndex : s_officerPlayerCities) {
        const OfficerCityCorpsInfo info =
            GetOfficerCityCorpsInfo(shiftedCityBase, cityIndex);
        if (info.readable && info.corpsPtr == s_officerSelectedCorpsPtr)
          corpsCities.push_back(cityIndex);
      }

      if (corpsCities.empty() || s_corpsOfficerRows.empty()) {
        AddNotification(u8"군단 자동배치: 추천할 도시 또는 무장이 없습니다.");
        return false;
      }

      // 도시별 현재 인원/태수와 전선 여부를 먼저 수집한다.
      for (int cityIndex : corpsCities) {
        CorpsDeploymentCityRecommendation city;
        city.cityIndex = cityIndex;
        city.frontline =
            IsCityFrontlineForForce(shiftedCityBase, cityIndex,
                                    s_officerPlayerForce);

        const uintptr_t rawCity =
            GetRawCityBase(shiftedCityBase, cityIndex);
        uintptr_t governorPtr = 0;
        SafeReadPtrAllowZero(rawCity + OFF_CITY_FORCE_LINK_RAW, &governorPtr);

        for (const auto &row : s_corpsOfficerRows) {
          const int currentCity =
              GetOfficerCurrentCityIndex(shiftedCityBase, row);
          if (currentCity == cityIndex)
            ++city.currentCount;
          if (row.officerBase == governorPtr)
            city.currentGovernorId = row.id;
        }
        city.targetCount = city.currentCount;
        s_corpsDeploymentCities.push_back(city);
      }

      const bool reuseTargetBaseline =
          HasValidCorpsDeploymentTargetBaseline(
              s_officerSelectedCorpsPtr, corpsCities);

      if (reuseTargetBaseline) {
        // 같은 군단에서는 첫 추천 때 잡은 목표 인원수를 계속 사용한다.
        // 1단계 이동 직후의 임시 인원수를 새 기준으로 잡으면 반복 배치가
        // 발생할 수 있으므로, 명시적으로 초기화하기 전까지 목표를 고정한다.
        for (auto &city : s_corpsDeploymentCities) {
          const int savedTarget =
              GetCorpsDeploymentBaselineTargetCount(city.cityIndex);
          if (savedTarget >= 0)
            city.targetCount = savedTarget;
        }
      } else {
        // 첫 추천 계산에서만 현재 인원을 기준으로 목표 인원을 만든다.
        // 가능하면 군단 도시를 비우지 않는다.
        if ((int)s_corpsOfficerRows.size() >=
            (int)s_corpsDeploymentCities.size()) {
          for (auto &city : s_corpsDeploymentCities) {
            if (city.targetCount > 0)
              continue;

            CorpsDeploymentCityRecommendation *donor = nullptr;
            for (auto &candidate : s_corpsDeploymentCities) {
              if (candidate.targetCount <= 1)
                continue;
              if (!donor || candidate.targetCount > donor->targetCount)
                donor = &candidate;
            }
            if (donor) {
              --donor->targetCount;
              city.targetCount = 1;
            }
          }
        }

        CaptureCorpsDeploymentTargetBaseline(
            s_officerSelectedCorpsPtr, s_corpsDeploymentCities);
      }

      auto isUsed = [&](uint16_t id) {
        for (const auto &rec : s_corpsDeploymentOfficers) {
          if (rec.id == id)
            return true;
        }
        return false;
      };

      // 군주/도독 및 기타 특수 신분은 현재 도시에 고정한다.
      // 군주(C8)와 도독(D8)이 있는 도시는 책임자 자리도 잠가
      // 별도의 태수를 중복 추천하지 않는다.
      for (const auto &row : s_corpsOfficerRows) {
        if (row.status == 0x18 || row.status == 0x28 ||
            row.status == 0xE8)
          continue;

        const int currentCity =
            GetOfficerCurrentCityIndex(shiftedCityBase, row);
        if (currentCity < 0)
          continue;

        const bool fixedLeader =
            row.status == 0xC8 || row.status == 0xD8;
        const char *reason =
            row.status == 0xC8
                ? u8"군주 고정"
                : (row.status == 0xD8 ? u8"도독 고정"
                                      : u8"특수 신분 유지");

        AddOfficerDeploymentRecommendation(
            row, currentCity, currentCity, fixedLeader, reason);

        if (fixedLeader) {
          CorpsDeploymentCityRecommendation *city =
              FindDeploymentCity(currentCity);
          if (city) {
            city->recommendedGovernorId = row.id;
            city->governorScore = 0;
          }
        }
      }

      // 완전히 빈 도시는 기존 태수(E8)를 끌어오지 않는다.
      // 정상 게임에서 두 번 확인한 28/D3 + 충성100 일반장수만 먼저 배정하고,
      // 그런 안전한 후보가 없으면 이번 계획에서는 도시를 비운 채 그대로 둔다.
      for (auto &city : s_corpsDeploymentCities) {
        if (city.currentCount != 0 ||
            city.currentGovernorId != 0 ||
            city.targetCount <= 0 ||
            city.recommendedGovernorId != 0)
          continue;

        const CityOfficerRow *chosen = nullptr;
        int chosenScore = -1;

        for (const auto &row : s_corpsOfficerRows) {
          if (isUsed(row.id) ||
              !IsSafeFirstGovernorNormal(row))
            continue;

          const int score =
              city.frontline
                  ? GetFrontDeploymentScore(row)
                  : GetRearDeploymentScore(row);
          if (!chosen || score > chosenScore ||
              (score == chosenScore && row.id < chosen->id)) {
            chosen = &row;
            chosenScore = score;
          }
        }

        if (!chosen) {
          city.targetCount = 0;
          AddLog(u8"[군단 자동배치] 빈 도시 %s: 충성100 28/D3 일반 장수가 없어 이번 계획에서는 비움 유지",
                 g_CityList[city.cityIndex].cityname);
          continue;
        }

        const int currentCity =
            GetOfficerCurrentCityIndex(
                shiftedCityBase, *chosen);
        city.recommendedGovernorId = chosen->id;
        city.governorScore =
            GetGovernorScore(*chosen, city.frontline);

        AddOfficerDeploymentRecommendation(
            *chosen, currentCity, city.cityIndex, true,
            city.frontline
                ? u8"빈 도시 · 충성100 28/D3 · 전선 최초 태수"
                : u8"빈 도시 · 충성100 28/D3 · 후방 최초 태수");

        AddLog(u8"[군단 자동배치] 빈 도시 %s: %s 일반장수를 최초 태수 후보로 선배치",
               g_CityList[city.cityIndex].cityname,
               BuildOfficerName(chosen->id).c_str());
      }

      // 먼저 전선/후방 배치를 끝낸 뒤, 각 도시 안에서 충성 100 태수를 고른다.
      // 태수를 먼저 뽑아 후방으로 보내면서 전투형 장수가 전선에서 밀리는 문제를 막는다.
      std::vector<const CityOfficerRow *> forcedRear;
      std::vector<const CityOfficerRow *> advisersFront;
      std::vector<const CityOfficerRow *> flexible;

      for (const auto &row : s_corpsOfficerRows) {
        if (isUsed(row.id))
          continue;
        if (row.status != 0x18 && row.status != 0x28 &&
            row.status != 0xE8)
          continue;

        if (row.loyalty < 90)
          forcedRear.push_back(&row);
        else if (row.status == 0x18)
          advisersFront.push_back(&row);
        else
          flexible.push_back(&row);
      }

      auto countRemainingSlots = [&](bool frontline) {
        int slots = 0;
        for (const auto &city : s_corpsDeploymentCities) {
          if (city.frontline != frontline)
            continue;
          slots += (std::max)(
              0, city.targetCount -
                     (int)city.recommendedOfficerIds.size());
        }
        return slots;
      };

      // 충성 90 미만은 후방 고정이 절대 규칙이다.
      // 현재 목표 인원으로 후방 자리가 부족하면 전선 목표 인원 일부를 후방으로 옮긴다.
      int rearNeed = (int)forcedRear.size();
      while (countRemainingSlots(false) < rearNeed) {
        CorpsDeploymentCityRecommendation *donor = nullptr;
        CorpsDeploymentCityRecommendation *receiver = nullptr;

        // 가능하면 전선 도시를 비우지 않는 선에서 한 자리만 넘긴다.
        for (auto &city : s_corpsDeploymentCities) {
          if (!city.frontline)
            continue;
          const int assigned =
              (int)city.recommendedOfficerIds.size();
          if (city.targetCount <= assigned ||
              city.targetCount <= 1)
            continue;
          if (!donor || city.targetCount > donor->targetCount)
            donor = &city;
        }

        // 그래도 부족하면 고정 배치 인원보다 많은 전선 자리에서 가져온다.
        if (!donor) {
          for (auto &city : s_corpsDeploymentCities) {
            if (!city.frontline)
              continue;
            const int assigned =
                (int)city.recommendedOfficerIds.size();
            if (city.targetCount <= assigned)
              continue;
            if (!donor || city.targetCount > donor->targetCount)
              donor = &city;
          }
        }

        for (auto &city : s_corpsDeploymentCities) {
          if (city.frontline)
            continue;
          if (!receiver ||
              city.targetCount < receiver->targetCount ||
              (city.targetCount == receiver->targetCount &&
               city.cityIndex < receiver->cityIndex))
            receiver = &city;
        }

        if (!donor || !receiver)
          break;

        --donor->targetCount;
        ++receiver->targetCount;
      }

      // 위에서 목표 인원을 조정했을 수 있으므로 이번 평정 기준도 최종값으로 갱신한다.
      CaptureCorpsDeploymentTargetBaseline(
          s_officerSelectedCorpsPtr, s_corpsDeploymentCities);

      std::sort(forcedRear.begin(), forcedRear.end(),
                [](const CityOfficerRow *a,
                   const CityOfficerRow *b) {
                  if (a->loyalty != b->loyalty)
                    return a->loyalty < b->loyalty;
                  const int as = GetRearDeploymentScore(*a);
                  const int bs = GetRearDeploymentScore(*b);
                  if (as != bs)
                    return as > bs;
                  return a->id < b->id;
                });

      for (const CityOfficerRow *row : forcedRear) {
        const int currentCity =
            GetOfficerCurrentCityIndex(shiftedCityBase, *row);
        int targetCity = PickDeploymentCity(false);
        if (targetCity < 0)
          targetCity = currentCity;
        AddOfficerDeploymentRecommendation(
            *row, currentCity, targetCity, false,
            u8"충성 90 미만 · 후방 보호");
      }

      // 군사는 신분을 유지하면서 지력 높은 순으로 전선 자리를 우선 사용한다.
      std::sort(advisersFront.begin(), advisersFront.end(),
                [](const CityOfficerRow *a,
                   const CityOfficerRow *b) {
                  if (a->intel != b->intel)
                    return a->intel > b->intel;
                  return a->id < b->id;
                });

      for (const CityOfficerRow *row : advisersFront) {
        const int currentCity =
            GetOfficerCurrentCityIndex(shiftedCityBase, *row);

        bool toFront = true;
        int targetCity = PickDeploymentCity(true);
        if (targetCity < 0) {
          toFront = false;
          targetCity = PickDeploymentCity(false);
        }
        if (targetCity < 0)
          targetCity = currentCity;

        AddOfficerDeploymentRecommendation(
            *row, currentCity, targetCity, false,
            toFront ? u8"군사 유지 · 지력 우선 전선"
                    : u8"군사 유지 · 전선 자리 부족");
      }

      // 남은 일반/태수 신분 장수는 먼저 전선 인원수를 확정한다.
      // 전선 자리는 전선 점수 자체가 높은 장수부터 채우므로,
      // 후방 태수 선발 때문에 하후돈 같은 전투형 장수가 밀리지 않는다.
      std::sort(flexible.begin(), flexible.end(),
                [](const CityOfficerRow *a,
                   const CityOfficerRow *b) {
                  const int af = GetFrontDeploymentScore(*a);
                  const int bf = GetFrontDeploymentScore(*b);
                  if (af != bf)
                    return af > bf;

                  const int ac =
                      (int)a->lead + (int)a->war;
                  const int bc =
                      (int)b->lead + (int)b->war;
                  if (ac != bc)
                    return ac > bc;
                  return a->id < b->id;
                });

      int frontRemaining = countRemainingSlots(true);
      const size_t frontTake =
          (std::min)((size_t)(std::max)(0, frontRemaining),
                     flexible.size());

      std::vector<const CityOfficerRow *> rearFlexible;
      rearFlexible.reserve(flexible.size() - frontTake);

      for (size_t i = 0; i < flexible.size(); ++i) {
        const CityOfficerRow *row = flexible[i];
        if (i >= frontTake) {
          rearFlexible.push_back(row);
          continue;
        }

        const int currentCity =
            GetOfficerCurrentCityIndex(shiftedCityBase, *row);
        bool toFront = true;
        int targetCity = PickDeploymentCity(true);
        if (targetCity < 0) {
          toFront = false;
          targetCity = PickDeploymentCity(false);
        }
        if (targetCity < 0)
          targetCity = currentCity;

        AddOfficerDeploymentRecommendation(
            *row, currentCity, targetCity, false,
            toFront
                ? (row->intel * 2 >=
                           (int)row->lead + (int)row->war
                       ? u8"전선 · 지력 적성"
                       : u8"전선 · 통솔/무력 적성")
                : u8"후방 · 전선 자리 부족");
      }

      // 전선에 들어가지 않은 장수는 정치+매력 순으로 후방 도시에 배치한다.
      std::sort(rearFlexible.begin(), rearFlexible.end(),
                [](const CityOfficerRow *a,
                   const CityOfficerRow *b) {
                  const int ar = GetRearDeploymentScore(*a);
                  const int br = GetRearDeploymentScore(*b);
                  if (ar != br)
                    return ar > br;
                  return a->id < b->id;
                });

      for (const CityOfficerRow *row : rearFlexible) {
        const int currentCity =
            GetOfficerCurrentCityIndex(shiftedCityBase, *row);
        bool toFront = false;
        int targetCity = PickDeploymentCity(false);
        if (targetCity < 0) {
          toFront = true;
          targetCity = PickDeploymentCity(true);
        }
        if (targetCity < 0)
          targetCity = currentCity;

        AddOfficerDeploymentRecommendation(
            *row, currentCity, targetCity, false,
            toFront
                ? u8"전선 · 후방 자리 부족"
                : u8"후방 · 정치/매력 적성");
      }

      auto findDeploymentRec =
          [&](uint16_t id)
              -> CorpsDeploymentOfficerRecommendation * {
        for (auto &rec : s_corpsDeploymentOfficers) {
          if (rec.id == id)
            return &rec;
        }
        return nullptr;
      };

      auto isGovernorEligible = [&](uint16_t id) {
        const CityOfficerRow *row =
            FindCorpsOfficerByRecId(id);
        return row &&
               (row->status == 0x28 ||
                row->status == 0xE8) &&
               row->loyalty == 100;
      };

      auto countCityGovernorEligible =
          [&](const CorpsDeploymentCityRecommendation &city) {
        int count = 0;
        for (uint16_t id : city.recommendedOfficerIds) {
          if (isGovernorEligible(id))
            ++count;
        }
        return count;
      };

      // 같은 전선/후방 안에서는 가능하면 모든 도시에 충성100 태수 후보를 한 명씩 확보한다.
      // 이미 군주/도독이 책임자인 도시는 별도 태수 후보가 필요 없다.
      for (auto &city : s_corpsDeploymentCities) {
        if (city.targetCount <= 0 ||
            city.recommendedGovernorId != 0 ||
            countCityGovernorEligible(city) > 0)
          continue;

        uint16_t swapOutId = 0;
        for (uint16_t id : city.recommendedOfficerIds) {
          const CityOfficerRow *row =
              FindCorpsOfficerByRecId(id);
          if (!row)
            continue;
          if (row->status == 0x18 ||
              row->status == 0x28 ||
              row->status == 0xE8) {
            swapOutId = id;
            break;
          }
        }
        if (!swapOutId)
          continue;

        CorpsDeploymentCityRecommendation *donorCity = nullptr;
        uint16_t donorId = 0;
        int donorScore = -1;

        for (auto &candidateCity :
             s_corpsDeploymentCities) {
          if (candidateCity.cityIndex == city.cityIndex ||
              candidateCity.frontline != city.frontline)
            continue;

          const bool donorNeedsOwnGovernor =
              candidateCity.targetCount > 0 &&
              candidateCity.recommendedGovernorId == 0;
          const int eligibleCount =
              countCityGovernorEligible(candidateCity);
          if (donorNeedsOwnGovernor &&
              eligibleCount <= 1)
            continue;

          for (uint16_t id :
               candidateCity.recommendedOfficerIds) {
            if (!isGovernorEligible(id))
              continue;

            const CityOfficerRow *row =
                FindCorpsOfficerByRecId(id);
            if (!row)
              continue;

            const int score =
                GetGovernorScore(*row, city.frontline);
            if (!donorCity || score > donorScore ||
                (score == donorScore && id < donorId)) {
              donorCity = &candidateCity;
              donorId = id;
              donorScore = score;
            }
          }
        }

        if (!donorCity || !donorId)
          continue;

        auto cityIt = std::find(
            city.recommendedOfficerIds.begin(),
            city.recommendedOfficerIds.end(),
            swapOutId);
        auto donorIt = std::find(
            donorCity->recommendedOfficerIds.begin(),
            donorCity->recommendedOfficerIds.end(),
            donorId);
        if (cityIt == city.recommendedOfficerIds.end() ||
            donorIt == donorCity->recommendedOfficerIds.end())
          continue;

        *cityIt = donorId;
        *donorIt = swapOutId;

        CorpsDeploymentOfficerRecommendation *donorRec =
            findDeploymentRec(donorId);
        CorpsDeploymentOfficerRecommendation *swapRec =
            findDeploymentRec(swapOutId);
        if (donorRec)
          donorRec->recommendedCityIndex = city.cityIndex;
        if (swapRec)
          swapRec->recommendedCityIndex =
              donorCity->cityIndex;
      }

      // 모든 배치가 끝난 뒤 각 도시 안에서 태수를 고른다.
      // 전선은 통솔+무력, 후방은 정치+매력, 충성도는 반드시 100.
      for (auto &city : s_corpsDeploymentCities) {
        if (city.targetCount <= 0 ||
            city.recommendedGovernorId != 0)
          continue;

        const CityOfficerRow *chosen = nullptr;
        int chosenScore = -1;

        for (uint16_t id : city.recommendedOfficerIds) {
          const CityOfficerRow *row =
              FindCorpsOfficerByRecId(id);
          if (!row ||
              (row->status != 0x28 &&
               row->status != 0xE8) ||
              row->loyalty != 100)
            continue;

          const int score =
              GetGovernorScore(*row, city.frontline);
          if (!chosen || score > chosenScore ||
              (score == chosenScore && row->id < chosen->id)) {
            chosen = row;
            chosenScore = score;
          }
        }

        if (!chosen)
          continue;

        city.recommendedGovernorId = chosen->id;
        city.governorScore = chosenScore;

        CorpsDeploymentOfficerRecommendation *rec =
            findDeploymentRec(chosen->id);
        if (rec) {
          rec->recommendedGovernor = true;
          rec->reason =
              city.frontline
                  ? u8"충성100 · 전선 배치 후 태수(통솔+무력)"
                  : u8"충성100 · 후방 배치 후 태수(정치+매력)";
        }
      }

      // 같은 도시 안에서는 태수 -> 군사 -> 나머지 순으로 보여준다.
      for (auto &city : s_corpsDeploymentCities) {
        std::stable_sort(
            city.recommendedOfficerIds.begin(),
            city.recommendedOfficerIds.end(),
            [&](uint16_t a, uint16_t b) {
              if (a == city.recommendedGovernorId)
                return true;
              if (b == city.recommendedGovernorId)
                return false;
              const CityOfficerRow *ar = FindCorpsOfficerByRecId(a);
              const CityOfficerRow *br = FindCorpsOfficerByRecId(b);
              const bool aa = ar && ar->status == 0x18;
              const bool ba = br && br->status == 0x18;
              if (aa != ba)
                return aa;
              return a < b;
            });
      }

      s_corpsDeploymentCorpsPtr = s_officerSelectedCorpsPtr;
      s_corpsDeploymentValid = true;
      AddNotification(u8"군단 자동배치: 추천 계산 완료. 아직 실제 배치는 변경하지 않았습니다.");
      return true;
    }

    struct CorpsDeploymentMoveWrite {
      uint16_t id = 0;
      uint8_t status = 0;
      uintptr_t officerBase = 0;
      uintptr_t oldCity = 0;
      uintptr_t newCity = 0;
      int oldCityIndex = -1;
      int newCityIndex = -1;
    };

    static bool ApplyCorpsDeploymentMovements(
        uintptr_t p1, uintptr_t shiftedCityBase) {
      if (!s_corpsDeploymentValid ||
          s_corpsDeploymentCorpsPtr <= 0x10000 ||
          s_corpsDeploymentCorpsPtr != s_officerSelectedCorpsPtr) {
        AddNotification(u8"군단 자동배치: 먼저 현재 군단의 추천을 계산해주세요.");
        return false;
      }

      std::vector<CorpsDeploymentMoveWrite> writes;
      writes.reserve(s_corpsDeploymentOfficers.size());

      // 먼저 모든 이동을 검증한다. 이번 단계에서는 일반/군사만 이동한다.
      for (const auto &rec : s_corpsDeploymentOfficers) {
        if (rec.currentCityIndex == rec.recommendedCityIndex)
          continue;
        if (rec.status != 0x28 && rec.status != 0x18)
          continue;
        if (rec.recommendedCityIndex < 0 ||
            rec.recommendedCityIndex >= g_CityCount)
          continue;

        const CityOfficerRow *row = FindCorpsOfficerByRecId(rec.id);
        if (!row || row->officerBase <= 0x10000) {
          AddNotification(u8"군단 자동배치: 추천 무장 정보를 다시 읽어야 합니다.");
          return false;
        }

        uint8_t currentStatus = 0;
        uintptr_t forcePtr = 0;
        uintptr_t currentCity = 0;
        if (!SafeRead8(row->officerBase + 0x10, &currentStatus) ||
            !SafeReadPtr(row->officerBase + 0x18, &forcePtr) ||
            !SafeReadPtr(row->officerBase + 0x20, &currentCity)) {
          AddNotification(u8"군단 자동배치: 무장 현재 상태를 읽지 못했습니다.");
          return false;
        }

        if (currentStatus != rec.status ||
            (currentStatus != 0x28 && currentStatus != 0x18)) {
          AddNotification(u8"군단 자동배치: 추천 후 무장 신분이 변경되어 적용을 중단했습니다.");
          AddLog(u8"[군단 자동배치] 신분 변경 감지: ID %u 추천=0x%02X 현재=0x%02X",
                 (unsigned int)rec.id,
                 (unsigned int)rec.status,
                 (unsigned int)currentStatus);
          return false;
        }

        const uintptr_t targetCity =
            GetRawCityBase(shiftedCityBase, rec.recommendedCityIndex);
        if (!targetCity || forcePtr != s_officerPlayerForce ||
            GetCityForcePtr(targetCity) != s_officerPlayerForce) {
          AddNotification(u8"군단 자동배치: 무장/목적 도시의 소속 세력을 다시 확인해주세요.");
          return false;
        }

        uintptr_t sourceCorps = 0;
        uintptr_t targetCorps = 0;
        if (!SafeReadPtrAllowZero(currentCity + OFF_CITY_CORPS_RAW,
                                  &sourceCorps) ||
            !SafeReadPtrAllowZero(targetCity + OFF_CITY_CORPS_RAW,
                                  &targetCorps) ||
            sourceCorps != s_corpsDeploymentCorpsPtr ||
            targetCorps != s_corpsDeploymentCorpsPtr) {
          AddNotification(u8"군단 자동배치: 군단 소속이 달라져 적용을 중단했습니다.");
          AddLog(u8"[군단 자동배치] 군단 검증 실패: ID %u 출발=0x%llX 목적=0x%llX 기준=0x%llX",
                 (unsigned int)rec.id,
                 (unsigned long long)sourceCorps,
                 (unsigned long long)targetCorps,
                 (unsigned long long)s_corpsDeploymentCorpsPtr);
          return false;
        }

        CorpsDeploymentMoveWrite write;
        write.id = rec.id;
        write.status = currentStatus;
        write.officerBase = row->officerBase;
        write.oldCity = currentCity;
        write.newCity = targetCity;
        write.oldCityIndex = rec.currentCityIndex;
        write.newCityIndex = rec.recommendedCityIndex;
        writes.push_back(write);
      }

      if (writes.empty()) {
        AddNotification(u8"군단 자동배치: 이번 단계에서 이동할 일반/군사가 없습니다.");
        return true;
      }

      size_t writtenCount = 0;
      for (size_t i = 0; i < writes.size(); ++i) {
        const auto &write = writes[i];
        if (!SafeWritePtr(write.officerBase + 0x20, write.newCity)) {
          for (size_t j = 0; j < writtenCount; ++j)
            SafeWritePtr(writes[j].officerBase + 0x20, writes[j].oldCity);

          AddNotification(u8"군단 자동배치: 이동 중 실패하여 이전 이동을 원복했습니다.");
          AddLog(u8"[군단 자동배치] 쓰기 실패/롤백: ID %u (%u/%u)",
                 (unsigned int)write.id,
                 (unsigned int)i,
                 (unsigned int)writes.size());
          return false;
        }
        ++writtenCount;
      }

      bool verifyOk = true;
      for (const auto &write : writes) {
        uintptr_t cityNow = 0;
        if (!SafeReadPtr(write.officerBase + 0x20, &cityNow) ||
            cityNow != write.newCity) {
          verifyOk = false;
          break;
        }
      }

      if (!verifyOk) {
        for (const auto &write : writes)
          SafeWritePtr(write.officerBase + 0x20, write.oldCity);
        AddNotification(u8"군단 자동배치: 이동 후 검증 실패로 전체 원복했습니다.");
        return false;
      }

      int normalCount = 0;
      int adviserCount = 0;
      for (const auto &write : writes) {
        if (write.status == 0x18)
          ++adviserCount;
        else
          ++normalCount;

        AddLog(u8"[군단 자동배치] %s: %s -> %s (%s)",
               BuildOfficerName(write.id).c_str(),
               write.oldCityIndex >= 0
                   ? g_CityList[write.oldCityIndex].cityname
                   : u8"?",
               write.newCityIndex >= 0
                   ? g_CityList[write.newCityIndex].cityname
                   : u8"?",
               write.status == 0x18 ? u8"군사 유지" : u8"일반");
      }

      char notice[256]{};
      sprintf_s(notice,
                u8"군단 자동배치 1단계 완료: 일반 %d명 / 군사 %d명 이동",
                normalCount, adviserCount);
      AddNotification(notice);

      s_selectedOfficerId = -1;
      s_officerRosterDirty = true;
      RefreshCityOfficerRoster(p1, shiftedCityBase);

      // 같은 평정에서는 최초 추천안을 유지하고 현재 위치만 갱신한다.
      RefreshCorpsDeploymentPlanCurrentState(shiftedCityBase);
      return true;
    }


    struct CorpsDeploymentStage2OfficerSnapshot {
      uintptr_t officerBase = 0;
      uint16_t statusPair = 0;
      uintptr_t cityPtr = 0;
    };

    struct CorpsDeploymentStage2CitySnapshot {
      uintptr_t rawCity = 0;
      uintptr_t governorPtr = 0;
    };

    struct CorpsDeploymentStage2Snapshot {
      std::vector<CorpsDeploymentStage2OfficerSnapshot> officers;
      std::vector<CorpsDeploymentStage2CitySnapshot> cities;
    };

    static CorpsDeploymentOfficerRecommendation *
    FindDeploymentOfficerRecommendation(uint16_t id) {
      for (auto &rec : s_corpsDeploymentOfficers) {
        if (rec.id == id)
          return &rec;
      }
      return nullptr;
    }

    static int GetOfficerCityIndexByBase(uintptr_t shiftedCityBase,
                                         uintptr_t officerBase) {
      uintptr_t cityPtr = 0;
      if (officerBase <= 0x10000 ||
          !SafeReadPtr(officerBase + 0x20, &cityPtr))
        return -1;

      for (int i = 0; i < g_CityCount; ++i) {
        if (GetRawCityBase(shiftedCityBase, i) == cityPtr)
          return i;
      }
      return -1;
    }

    static bool CaptureCorpsDeploymentStage2Snapshot(
        uintptr_t shiftedCityBase,
        CorpsDeploymentStage2Snapshot *out) {
      if (!out)
        return false;

      out->officers.clear();
      out->cities.clear();

      for (const auto &row : s_corpsOfficerRows) {
        CorpsDeploymentStage2OfficerSnapshot snap;
        snap.officerBase = row.officerBase;
        if (snap.officerBase <= 0x10000 ||
            !SafeRead16(snap.officerBase + 0x10, &snap.statusPair) ||
            !SafeReadPtr(snap.officerBase + 0x20, &snap.cityPtr))
          return false;
        out->officers.push_back(snap);
      }

      for (const auto &city : s_corpsDeploymentCities) {
        CorpsDeploymentStage2CitySnapshot snap;
        snap.rawCity = GetRawCityBase(shiftedCityBase, city.cityIndex);
        if (!snap.rawCity ||
            !SafeReadPtrAllowZero(
                snap.rawCity + OFF_CITY_FORCE_LINK_RAW,
                &snap.governorPtr))
          return false;
        out->cities.push_back(snap);
      }

      return true;
    }

    static void RestoreCorpsDeploymentStage2Snapshot(
        const CorpsDeploymentStage2Snapshot &snapshot) {
      for (const auto &snap : snapshot.officers) {
        if (snap.officerBase <= 0x10000)
          continue;
        SafeWrite16(snap.officerBase + 0x10, snap.statusPair);
        SafeWritePtr(snap.officerBase + 0x20, snap.cityPtr);
      }

      for (const auto &snap : snapshot.cities) {
        if (snap.rawCity <= 0x10000)
          continue;
        SafeWritePtr(snap.rawCity + OFF_CITY_FORCE_LINK_RAW,
                     snap.governorPtr);
      }
    }

    static bool MoveNormalOfficerWithinDeploymentCorps(
        uintptr_t shiftedCityBase, uintptr_t officerBase,
        int targetCityIndex) {
      if (officerBase <= 0x10000 ||
          targetCityIndex < 0 || targetCityIndex >= g_CityCount)
        return false;

      uint8_t status = 0;
      uintptr_t forcePtr = 0;
      uintptr_t currentCity = 0;
      if (!SafeRead8(officerBase + 0x10, &status) ||
          !SafeReadPtr(officerBase + 0x18, &forcePtr) ||
          !SafeReadPtr(officerBase + 0x20, &currentCity) ||
          status != 0x28 ||
          forcePtr != s_officerPlayerForce)
        return false;

      const uintptr_t targetCity =
          GetRawCityBase(shiftedCityBase, targetCityIndex);
      if (!targetCity)
        return false;
      if (currentCity == targetCity)
        return true;

      if (GetCityForcePtr(targetCity) != s_officerPlayerForce)
        return false;

      uintptr_t sourceCorps = 0;
      uintptr_t targetCorps = 0;
      if (!SafeReadPtrAllowZero(currentCity + OFF_CITY_CORPS_RAW,
                                &sourceCorps) ||
          !SafeReadPtrAllowZero(targetCity + OFF_CITY_CORPS_RAW,
                                &targetCorps) ||
          sourceCorps != s_corpsDeploymentCorpsPtr ||
          targetCorps != s_corpsDeploymentCorpsPtr)
        return false;

      if (!SafeWritePtr(officerBase + 0x20, targetCity))
        return false;

      uintptr_t verifyCity = 0;
      if (!SafeReadPtr(officerBase + 0x20, &verifyCity) ||
          verifyCity != targetCity) {
        SafeWritePtr(officerBase + 0x20, currentCity);
        return false;
      }
      return true;
    }

    static bool SwapDeploymentGovernorAtCity(
        uintptr_t shiftedCityBase, int cityIndex,
        uintptr_t oldGovernorBase, uintptr_t candidateBase) {
      if (cityIndex < 0 || cityIndex >= g_CityCount ||
          oldGovernorBase <= 0x10000 || candidateBase <= 0x10000)
        return false;

      const uintptr_t rawCity =
          GetRawCityBase(shiftedCityBase, cityIndex);
      if (!rawCity)
        return false;

      uintptr_t cityCorps = 0;
      if (!SafeReadPtrAllowZero(rawCity + OFF_CITY_CORPS_RAW,
                                &cityCorps) ||
          cityCorps != s_corpsDeploymentCorpsPtr)
        return false;

      uint16_t oldPair = 0;
      uint16_t newPair = 0;
      uintptr_t cityGovernor = 0;
      uintptr_t oldCity = 0;
      uintptr_t newCity = 0;
      uintptr_t oldForce = 0;
      uintptr_t newForce = 0;
      uint8_t candidateLoyalty = 0;

      if (!SafeRead16(oldGovernorBase + 0x10, &oldPair) ||
          !SafeRead16(candidateBase + 0x10, &newPair) ||
          !SafeReadPtr(rawCity + OFF_CITY_FORCE_LINK_RAW,
                       &cityGovernor) ||
          !SafeReadPtr(oldGovernorBase + 0x20, &oldCity) ||
          !SafeReadPtr(candidateBase + 0x20, &newCity) ||
          !SafeReadPtr(oldGovernorBase + 0x18, &oldForce) ||
          !SafeReadPtr(candidateBase + 0x18, &newForce) ||
          !SafeRead8(candidateBase + 0xEC, &candidateLoyalty))
        return false;

      const uint8_t oldStatus = (uint8_t)(oldPair & 0xFF);
      const uint8_t oldAux = (uint8_t)((oldPair >> 8) & 0xFF);
      const uint8_t newStatus = (uint8_t)(newPair & 0xFF);
      const uint8_t newAux = (uint8_t)((newPair >> 8) & 0xFF);

      if (oldStatus != 0xE8 || newStatus != 0x28 ||
          candidateLoyalty != 100 ||
          cityGovernor != oldGovernorBase ||
          oldCity != rawCity || newCity != rawCity ||
          oldForce != newForce ||
          oldForce != s_officerPlayerForce)
        return false;

      const uint16_t oldAfter =
          (uint16_t)(((uint16_t)newAux << 8) | 0x28u);
      const uint16_t newAfter =
          (uint16_t)(((uint16_t)oldAux << 8) | 0xE8u);

      bool oldWritten =
          SafeWrite16(oldGovernorBase + 0x10, oldAfter);
      bool newWritten = false;
      bool cityWritten = false;
      if (oldWritten)
        newWritten = SafeWrite16(candidateBase + 0x10, newAfter);
      if (oldWritten && newWritten)
        cityWritten = SafeWritePtr(
            rawCity + OFF_CITY_FORCE_LINK_RAW, candidateBase);

      if (!oldWritten || !newWritten || !cityWritten) {
        if (cityWritten)
          SafeWritePtr(rawCity + OFF_CITY_FORCE_LINK_RAW,
                       cityGovernor);
        if (newWritten)
          SafeWrite16(candidateBase + 0x10, newPair);
        if (oldWritten)
          SafeWrite16(oldGovernorBase + 0x10, oldPair);
        return false;
      }

      uint16_t verifyOld = 0;
      uint16_t verifyNew = 0;
      uintptr_t verifyGovernor = 0;
      const bool verifyOk =
          SafeRead16(oldGovernorBase + 0x10, &verifyOld) &&
          SafeRead16(candidateBase + 0x10, &verifyNew) &&
          SafeReadPtr(rawCity + OFF_CITY_FORCE_LINK_RAW,
                      &verifyGovernor) &&
          verifyOld == oldAfter &&
          verifyNew == newAfter &&
          verifyGovernor == candidateBase;

      if (!verifyOk) {
        SafeWritePtr(rawCity + OFF_CITY_FORCE_LINK_RAW,
                     cityGovernor);
        SafeWrite16(candidateBase + 0x10, newPair);
        SafeWrite16(oldGovernorBase + 0x10, oldPair);
        return false;
      }

      uint16_t oldId = 0;
      uint16_t newId = 0;
      SafeRead16(oldGovernorBase + 0x08, &oldId);
      SafeRead16(candidateBase + 0x08, &newId);
      AddLog(u8"[군단 자동배치 2단계] %s 태수: %s -> %s",
             g_CityList[cityIndex].cityname,
             BuildOfficerName(oldId).c_str(),
             BuildOfficerName(newId).c_str());
      return true;
    }

    static bool AppointFirstDeploymentGovernorAtCity(
        uintptr_t shiftedCityBase, int cityIndex,
        uintptr_t candidateBase) {
      if (cityIndex < 0 || cityIndex >= g_CityCount ||
          candidateBase <= 0x10000)
        return false;

      const uintptr_t rawCity =
          GetRawCityBase(shiftedCityBase, cityIndex);
      if (!rawCity)
        return false;

      uintptr_t cityCorps = 0;
      uintptr_t cityGovernor = 0;
      uintptr_t candidateCity = 0;
      uintptr_t candidateForce = 0;
      uint16_t candidatePair = 0;
      uint8_t candidateLoyalty = 0;

      if (!SafeReadPtrAllowZero(
              rawCity + OFF_CITY_CORPS_RAW, &cityCorps) ||
          !SafeReadPtrAllowZero(
              rawCity + OFF_CITY_FORCE_LINK_RAW, &cityGovernor) ||
          !SafeReadPtr(
              candidateBase + 0x20, &candidateCity) ||
          !SafeReadPtr(
              candidateBase + 0x18, &candidateForce) ||
          !SafeRead16(
              candidateBase + 0x10, &candidatePair) ||
          !SafeRead8(
              candidateBase + 0xEC, &candidateLoyalty))
        return false;

      // 정상 게임에서 두 번 관찰된 빈 도시 최초 임명 패턴:
      // 일반 28/D3 (0xD328) -> 태수 E8/D2 (0xD2E8),
      // City+0x98 0 -> 해당 OfficerData*.
      if (cityCorps != s_corpsDeploymentCorpsPtr ||
          cityGovernor != 0 ||
          candidateCity != rawCity ||
          candidateForce != s_officerPlayerForce ||
          candidateLoyalty != 100 ||
          candidatePair != 0xD328u ||
          GetCityForcePtr(rawCity) != s_officerPlayerForce)
        return false;

      // 군주/도독/기존 태수가 이미 거주하는 특수 상황에서는 최초 임명을 하지 않는다.
      for (const auto &row : s_corpsOfficerRows) {
        if (row.officerBase == candidateBase)
          continue;
        if (GetOfficerCurrentCityIndex(
                shiftedCityBase, row) != cityIndex)
          continue;
        if (row.status == 0xC8 ||
            row.status == 0xD8 ||
            row.status == 0xE8)
          return false;
      }

      const uint16_t candidateAfter = 0xD2E8u;
      bool pairWritten =
          SafeWrite16(candidateBase + 0x10, candidateAfter);
      bool cityWritten = false;
      if (pairWritten) {
        cityWritten = SafeWritePtr(
            rawCity + OFF_CITY_FORCE_LINK_RAW, candidateBase);
      }

      if (!pairWritten || !cityWritten) {
        if (cityWritten) {
          SafeWritePtr(
              rawCity + OFF_CITY_FORCE_LINK_RAW, 0);
        }
        if (pairWritten) {
          SafeWrite16(candidateBase + 0x10, candidatePair);
        }
        return false;
      }

      uint16_t verifyPair = 0;
      uintptr_t verifyGovernor = 0;
      uintptr_t verifyCity = 0;
      uint8_t verifyLoyalty = 0;
      const bool verifyOk =
          SafeRead16(
              candidateBase + 0x10, &verifyPair) &&
          SafeReadPtr(
              candidateBase + 0x20, &verifyCity) &&
          SafeRead8(
              candidateBase + 0xEC, &verifyLoyalty) &&
          SafeReadPtr(
              rawCity + OFF_CITY_FORCE_LINK_RAW,
              &verifyGovernor) &&
          verifyPair == candidateAfter &&
          verifyCity == rawCity &&
          verifyLoyalty == 100 &&
          verifyGovernor == candidateBase;

      if (!verifyOk) {
        SafeWritePtr(
            rawCity + OFF_CITY_FORCE_LINK_RAW, 0);
        SafeWrite16(candidateBase + 0x10, candidatePair);
        return false;
      }

      uint16_t candidateId = 0;
      SafeRead16(candidateBase + 0x08, &candidateId);
      AddLog(u8"[군단 자동배치 2단계] %s 최초 태수 임명: %s (28/D3 -> E8/D2, City+0x98 설정)",
             g_CityList[cityIndex].cityname,
             BuildOfficerName(candidateId).c_str());
      return true;
    }

    static bool IsDeploymentFixedLeader(uint16_t id) {
      const CityOfficerRow *row = FindCorpsOfficerByRecId(id);
      if (!row)
        return false;
      return row->status == 0xC8 || row->status == 0xD8;
    }

    static bool HasDeploymentCityIndex(
        const std::vector<int> &cities, int cityIndex) {
      return std::find(cities.begin(), cities.end(), cityIndex) !=
             cities.end();
    }

    static uintptr_t ReadDeploymentCityGovernor(
        uintptr_t shiftedCityBase, int cityIndex) {
      const uintptr_t rawCity =
          GetRawCityBase(shiftedCityBase, cityIndex);
      uintptr_t governor = 0;
      if (!rawCity ||
          !SafeReadPtrAllowZero(
              rawCity + OFF_CITY_FORCE_LINK_RAW, &governor))
        return 0;
      return governor;
    }

    static std::string BuildDeploymentCityResponsibilityDiagnostic(
        uintptr_t shiftedCityBase, int cityIndex) {
      if (cityIndex < 0 || cityIndex >= g_CityCount)
        return u8"도시 인덱스 오류";

      int residentCount = 0;
      std::string rulerName;
      std::string governorGeneralName;
      std::string governorName;

      for (const auto &row : s_corpsOfficerRows) {
        if (GetOfficerCurrentCityIndex(
                shiftedCityBase, row) != cityIndex)
          continue;

        ++residentCount;
        if (row.status == 0xC8 && rulerName.empty())
          rulerName = BuildOfficerName(row.id);
        else if (row.status == 0xD8 &&
                 governorGeneralName.empty())
          governorGeneralName = BuildOfficerName(row.id);
        else if (row.status == 0xE8 &&
                 governorName.empty())
          governorName = BuildOfficerName(row.id);
      }

      const uintptr_t cityGovernor =
          ReadDeploymentCityGovernor(
              shiftedCityBase, cityIndex);

      std::string ptrInfo;
      if (cityGovernor <= 0x10000) {
        ptrInfo = u8"City+0x98=0";
      } else {
        uint16_t ptrId = 0;
        uint8_t ptrStatus = 0;
        const bool idOk =
            SafeRead16(cityGovernor + 0x08, &ptrId);
        const bool statusOk =
            SafeRead8(cityGovernor + 0x10, &ptrStatus);

        ptrInfo = u8"City+0x98=";
        if (idOk)
          ptrInfo += BuildOfficerName(ptrId);
        else
          ptrInfo += u8"ID?";
        if (statusOk) {
          char statusBuf[24]{};
          sprintf_s(statusBuf, "(0x%02X)",
                    (unsigned int)ptrStatus);
          ptrInfo += statusBuf;
        }
      }

      if (!rulerName.empty())
        return u8"군주 " + rulerName + u8" 있음 | " + ptrInfo;

      if (!governorGeneralName.empty())
        return u8"도독 " + governorGeneralName + u8" 있음 | " +
               ptrInfo;

      if (!governorName.empty())
        return u8"거주 E8 태수 " + governorName +
               u8" 있음 / 포인터 상태 확인 필요 | " +
               ptrInfo + u8" | 직접 체크";

      return u8"C8/D8/E8 없음, 거주 " +
             std::to_string(residentCount) +
             u8"명 | " + ptrInfo + u8" | 직접 체크";
    }

    static void BuildDeferredDeploymentGovernorCities(
        uintptr_t shiftedCityBase, std::vector<int> *outDeferred) {
      if (!outDeferred)
        return;
      outDeferred->clear();

      auto markDeferred = [&](int cityIndex) {
        if (!HasDeploymentCityIndex(*outDeferred, cityIndex))
          outDeferred->push_back(cityIndex);
      };

      // City+0x98==0은 검증된 28/D3 -> E8/D2 최초 태수 패턴으로 처리한다.
      // 0이 아닌 포인터가 E8 태수를 가리키지 않는 특수 상태만 보류한다.
      for (const auto &city : s_corpsDeploymentCities) {
        if (!city.recommendedGovernorId ||
            IsDeploymentFixedLeader(city.recommendedGovernorId))
          continue;

        const CityOfficerRow *desired =
            FindCorpsOfficerByRecId(city.recommendedGovernorId);
        if (!desired)
          continue;

        const uintptr_t currentGovernor =
            ReadDeploymentCityGovernor(
                shiftedCityBase, city.cityIndex);
        if (currentGovernor == desired->officerBase)
          continue;

        if (currentGovernor == 0) {
          const int desiredCity =
              GetOfficerCityIndexByBase(
                  shiftedCityBase, desired->officerBase);
          if (IsSafeFirstGovernorNormal(*desired) &&
              desiredCity == city.cityIndex)
            continue;

          // 빈 도시인데 최종 후보가 기존 태수이거나 안전한 28/D3 일반이 아니면
          // 이번 2단계에서는 건드리지 않는다. 연결된 기존 태수 도시도 함께 보류된다.
          markDeferred(city.cityIndex);
          continue;
        }

        uint8_t currentStatus = 0;
        if (currentGovernor <= 0x10000 ||
            !SafeRead8(currentGovernor + 0x10, &currentStatus) ||
            currentStatus != 0xE8) {
          markDeferred(city.cityIndex);
        }
      }

      // 보류 도시와 E8 이동 관계로 연결된 도시도 함께 보류한다.
      bool changed = true;
      while (changed) {
        changed = false;
        for (const auto &blockedCity : s_corpsDeploymentCities) {
          if (!HasDeploymentCityIndex(
                  *outDeferred, blockedCity.cityIndex))
            continue;

          const uintptr_t blockedCurrent =
              ReadDeploymentCityGovernor(
                  shiftedCityBase, blockedCity.cityIndex);
          const CityOfficerRow *blockedDesired =
              blockedCity.recommendedGovernorId
                  ? FindCorpsOfficerByRecId(
                        blockedCity.recommendedGovernorId)
                  : nullptr;
          const uintptr_t blockedDesiredBase =
              blockedDesired ? blockedDesired->officerBase : 0;

          for (const auto &other : s_corpsDeploymentCities) {
            if (HasDeploymentCityIndex(
                    *outDeferred, other.cityIndex) ||
                !other.recommendedGovernorId ||
                IsDeploymentFixedLeader(
                    other.recommendedGovernorId))
              continue;

            const CityOfficerRow *otherDesired =
                FindCorpsOfficerByRecId(other.recommendedGovernorId);
            if (!otherDesired)
              continue;

            const uintptr_t otherCurrent =
                ReadDeploymentCityGovernor(
                    shiftedCityBase, other.cityIndex);

            const bool dependsOnBlockedCurrent =
                blockedCurrent > 0x10000 &&
                otherDesired->officerBase == blockedCurrent;
            const bool ownsBlockedDesired =
                blockedDesiredBase > 0x10000 &&
                otherCurrent == blockedDesiredBase;

            if (dependsOnBlockedCurrent || ownsBlockedDesired) {
              outDeferred->push_back(other.cityIndex);
              changed = true;
            }
          }
        }
      }

      std::sort(outDeferred->begin(), outDeferred->end());
    }

    static int CountPendingDeploymentGovernorChanges(
        uintptr_t shiftedCityBase, int *outDeferredCount = nullptr) {
      std::vector<int> deferred;
      BuildDeferredDeploymentGovernorCities(
          shiftedCityBase, &deferred);

      int applicable = 0;
      int deferredPending = 0;
      for (const auto &city : s_corpsDeploymentCities) {
        if (!city.recommendedGovernorId ||
            IsDeploymentFixedLeader(city.recommendedGovernorId))
          continue;

        const CityOfficerRow *desired =
            FindCorpsOfficerByRecId(city.recommendedGovernorId);
        if (!desired)
          continue;

        const uintptr_t currentGovernor =
            ReadDeploymentCityGovernor(
                shiftedCityBase, city.cityIndex);
        if (currentGovernor == desired->officerBase)
          continue;

        if (HasDeploymentCityIndex(
                deferred, city.cityIndex))
          ++deferredPending;
        else
          ++applicable;
      }

      if (outDeferredCount)
        *outDeferredCount = deferredPending;
      return applicable;
    }

    static bool ApplyCorpsDeploymentGovernorStage(
        uintptr_t p1, uintptr_t shiftedCityBase) {
      if (!s_corpsDeploymentValid ||
          s_corpsDeploymentCorpsPtr <= 0x10000 ||
          s_corpsDeploymentCorpsPtr != s_officerSelectedCorpsPtr) {
        AddNotification(u8"군단 자동배치 2단계: 현재 군단의 고정된 추천 계획이 없습니다.");
        return false;
      }

      s_officerRosterDirty = true;
      RefreshCityOfficerRoster(p1, shiftedCityBase);
      RefreshCorpsDeploymentPlanCurrentState(shiftedCityBase);

      std::vector<int> deferredCities;
      BuildDeferredDeploymentGovernorCities(
          shiftedCityBase, &deferredCities);
      for (int cityIndex : deferredCities) {
        const std::string diagnostic =
            BuildDeploymentCityResponsibilityDiagnostic(
                shiftedCityBase, cityIndex);
        AddLog(u8"[군단 자동배치 2단계] 보류: %s | %s",
               cityIndex >= 0 && cityIndex < g_CityCount
                   ? g_CityList[cityIndex].cityname
                   : u8"?",
               diagnostic.c_str());
      }

      // 1단계 이동이 남아 있으면 태수 교체를 시작하지 않는다.
      for (const auto &rec : s_corpsDeploymentOfficers) {
        if ((rec.status == 0x28 || rec.status == 0x18) &&
            rec.currentCityIndex != rec.recommendedCityIndex) {
          AddNotification(u8"군단 자동배치 2단계: 먼저 1단계 일반/군사 배치를 완료해주세요.");
          return false;
        }
      }

      // 태수가 필요한 도시에는 충성 100 추천 책임자가 반드시 있어야 한다.
      for (const auto &city : s_corpsDeploymentCities) {
        if (HasDeploymentCityIndex(
                deferredCities, city.cityIndex))
          continue;
        if (city.targetCount <= 0)
          continue;
        if (!city.recommendedGovernorId) {
          AddNotification(u8"군단 자동배치 2단계: 충성 100 태수 후보가 부족한 도시가 있어 중단했습니다.");
          return false;
        }

        const CityOfficerRow *desired =
            FindCorpsOfficerByRecId(city.recommendedGovernorId);
        if (!desired)
          return false;
        if (desired->status == 0xC8 || desired->status == 0xD8)
          continue;
        if (desired->loyalty != 100) {
          AddNotification(u8"군단 자동배치 2단계: 추천 태수의 충성도가 100이 아니어서 중단했습니다.");
          return false;
        }
      }

      CorpsDeploymentStage2Snapshot snapshot;
      if (!CaptureCorpsDeploymentStage2Snapshot(
              shiftedCityBase, &snapshot)) {
        AddNotification(u8"군단 자동배치 2단계: 롤백용 현재 상태를 저장하지 못했습니다.");
        return false;
      }

      int moveCount = 0;
      int swapCount = 0;
      int firstGovernorCount = 0;
      int cycleBreakCount = 0;
      const int deferredCount =
          (int)deferredCities.size();
      bool failed = false;
      std::string failReason;

      for (int guard = 0; guard < 512; ++guard) {
        s_officerRosterDirty = true;
        RefreshCityOfficerRoster(p1, shiftedCityBase);
        RefreshCorpsDeploymentPlanCurrentState(shiftedCityBase);

        bool progress = false;

        // 교체로 일반 신분이 된 기존 태수는 이제 안전하게 추천 도시로 이동할 수 있다.
        for (auto &rec : s_corpsDeploymentOfficers) {
          const CityOfficerRow *row =
              FindCorpsOfficerByRecId(rec.id);
          if (!row || row->status != 0x28 ||
              rec.currentCityIndex == rec.recommendedCityIndex)
            continue;
          if (HasDeploymentCityIndex(
                  deferredCities, rec.recommendedCityIndex))
            continue;
          if (rec.recommendedCityIndex < 0 ||
              rec.recommendedCityIndex >= g_CityCount)
            continue;

          const int oldCity = rec.currentCityIndex;
          if (!MoveNormalOfficerWithinDeploymentCorps(
                  shiftedCityBase, row->officerBase,
                  rec.recommendedCityIndex)) {
            failed = true;
            failReason = u8"해제된 태수의 도시 이동에 실패했습니다.";
            break;
          }

          ++moveCount;
          progress = true;
          AddLog(u8"[군단 자동배치 2단계] %s 이동: %s -> %s",
                 BuildOfficerName(rec.id).c_str(),
                 oldCity >= 0 ? g_CityList[oldCity].cityname : u8"?",
                 g_CityList[rec.recommendedCityIndex].cityname);
        }

        if (failed)
          break;
        if (progress)
          continue;

        // 추천 태수가 일반(28) 상태로 목표 도시에 도착한 곳부터 교체한다.
        for (const auto &city : s_corpsDeploymentCities) {
          if (HasDeploymentCityIndex(
                  deferredCities, city.cityIndex))
            continue;
          if (!city.recommendedGovernorId ||
              IsDeploymentFixedLeader(city.recommendedGovernorId))
            continue;

          const CityOfficerRow *desired =
              FindCorpsOfficerByRecId(city.recommendedGovernorId);
          if (!desired)
            continue;

          const uintptr_t rawCity =
              GetRawCityBase(shiftedCityBase, city.cityIndex);
          uintptr_t currentGovernor = 0;
          if (!rawCity ||
              !SafeReadPtrAllowZero(
                  rawCity + OFF_CITY_FORCE_LINK_RAW,
                  &currentGovernor)) {
            failed = true;
            failReason = u8"도시 태수 포인터를 읽지 못했습니다.";
            break;
          }

          if (currentGovernor == desired->officerBase)
            continue;

          const int desiredCity =
              GetOfficerCityIndexByBase(
                  shiftedCityBase, desired->officerBase);
          if (desired->status != 0x28 ||
              desiredCity != city.cityIndex)
            continue;

          if (currentGovernor == 0) {
            if (!IsSafeFirstGovernorNormal(*desired)) {
              failed = true;
              failReason =
                  u8"선검사를 통과한 빈 도시의 추천 태수가 안전한 28/D3 일반장수가 아닙니다.";
              break;
            }

            if (!AppointFirstDeploymentGovernorAtCity(
                    shiftedCityBase, city.cityIndex,
                    desired->officerBase)) {
              failed = true;
              failReason =
                  u8"빈 도시 최초 태수 직접 임명 쓰기/검증에 실패했습니다.";
              break;
            }

            ++firstGovernorCount;
            progress = true;
            break;
          }

          if (currentGovernor <= 0x10000) {
            failed = true;
            failReason =
                u8"도시 책임자 포인터가 0이 아닌 비정상 값입니다.";
            break;
          }

          uint8_t currentStatus = 0;
          if (!SafeRead8(currentGovernor + 0x10,
                         &currentStatus) ||
              currentStatus != 0xE8) {
            failed = true;
            failReason = u8"현재 책임자가 태수(E8)가 아니어서 교체를 중단했습니다.";
            break;
          }

          if (!SwapDeploymentGovernorAtCity(
                  shiftedCityBase, city.cityIndex,
                  currentGovernor, desired->officerBase)) {
            failed = true;
            failReason = u8"태수 교체 쓰기/검증에 실패했습니다.";
            break;
          }

          ++swapCount;
          progress = true;
          break;
        }

        if (failed)
          break;
        if (progress)
          continue;

        // 여기까지 왔는데 미완료라면 태수끼리 서로 물고 있는 순환 배치일 수 있다.
        int unresolvedCityIndex = -1;
        uintptr_t unresolvedDesiredBase = 0;
        int desiredCurrentCity = -1;

        for (const auto &city : s_corpsDeploymentCities) {
          if (HasDeploymentCityIndex(
                  deferredCities, city.cityIndex))
            continue;
          if (!city.recommendedGovernorId ||
              IsDeploymentFixedLeader(city.recommendedGovernorId))
            continue;

          const CityOfficerRow *desired =
              FindCorpsOfficerByRecId(city.recommendedGovernorId);
          if (!desired)
            continue;

          const uintptr_t rawCity =
              GetRawCityBase(shiftedCityBase, city.cityIndex);
          uintptr_t currentGovernor = 0;
          if (!rawCity ||
              !SafeReadPtrAllowZero(
                  rawCity + OFF_CITY_FORCE_LINK_RAW,
                  &currentGovernor))
            continue;
          if (currentGovernor == desired->officerBase)
            continue;

          if (desired->status == 0xE8) {
            unresolvedCityIndex = city.cityIndex;
            unresolvedDesiredBase = desired->officerBase;
            desiredCurrentCity =
                GetOfficerCityIndexByBase(
                    shiftedCityBase, desired->officerBase);
            break;
          }
        }

        if (unresolvedCityIndex < 0)
          break;

        if (desiredCurrentCity < 0 ||
            desiredCurrentCity >= g_CityCount) {
          failed = true;
          failReason = u8"순환 태수의 현재 도시를 확인하지 못했습니다.";
          break;
        }

        // 충성 100 일반 장수를 임시 태수로 써서 E8 순환 고리를 한 번 끊는다.
        const CityOfficerRow *buffer = nullptr;
        int bufferCity = -1;
        for (const auto &rec : s_corpsDeploymentOfficers) {
          if (rec.recommendedGovernor || rec.loyalty != 100)
            continue;

          const CityOfficerRow *row =
              FindCorpsOfficerByRecId(rec.id);
          if (!row || row->status != 0x28)
            continue;

          const int currentCity =
              GetOfficerCityIndexByBase(
                  shiftedCityBase, row->officerBase);
          if (currentCity < 0 ||
              currentCity != rec.recommendedCityIndex)
            continue;

          buffer = row;
          bufferCity = currentCity;
          break;
        }

        if (!buffer) {
          failed = true;
          failReason = u8"태수 순환을 풀 충성 100 일반 장수 버퍼가 없습니다.";
          break;
        }

        if (!MoveNormalOfficerWithinDeploymentCorps(
                shiftedCityBase, buffer->officerBase,
                desiredCurrentCity)) {
          failed = true;
          failReason = u8"순환 해제용 임시 장수 이동에 실패했습니다.";
          break;
        }
        if (bufferCity != desiredCurrentCity)
          ++moveCount;

        if (!SwapDeploymentGovernorAtCity(
                shiftedCityBase, desiredCurrentCity,
                unresolvedDesiredBase, buffer->officerBase)) {
          failed = true;
          failReason = u8"순환 해제용 임시 태수 교체에 실패했습니다.";
          break;
        }

        ++swapCount;
        ++cycleBreakCount;
        AddLog(u8"[군단 자동배치 2단계] 태수 순환 해제: %s를 임시 태수로 사용",
               BuildOfficerName(buffer->id).c_str());
      }

      if (!failed) {
        s_officerRosterDirty = true;
        RefreshCityOfficerRoster(p1, shiftedCityBase);
        RefreshCorpsDeploymentPlanCurrentState(shiftedCityBase);

        // 최종 태수/충성도 검증. 보류 도시는 원상 유지 대상으로 제외한다.
        for (const auto &city : s_corpsDeploymentCities) {
          if (HasDeploymentCityIndex(
                  deferredCities, city.cityIndex))
            continue;
          if (!city.recommendedGovernorId ||
              IsDeploymentFixedLeader(city.recommendedGovernorId))
            continue;

          const CityOfficerRow *desired =
              FindCorpsOfficerByRecId(city.recommendedGovernorId);
          const uintptr_t rawCity =
              GetRawCityBase(shiftedCityBase, city.cityIndex);
          uintptr_t currentGovernor = 0;
          if (!desired || !rawCity ||
              !SafeReadPtrAllowZero(
                  rawCity + OFF_CITY_FORCE_LINK_RAW,
                  &currentGovernor) ||
              currentGovernor != desired->officerBase ||
              desired->status != 0xE8 ||
              desired->loyalty != 100 ||
              GetOfficerCityIndexByBase(
                  shiftedCityBase, desired->officerBase) !=
                  city.cityIndex) {
            failed = true;
            failReason = u8"최종 태수 배치 검증에 실패했습니다.";
            break;
          }
        }
      }

      if (failed) {
        RestoreCorpsDeploymentStage2Snapshot(snapshot);
        s_officerRosterDirty = true;
        RefreshCityOfficerRoster(p1, shiftedCityBase);
        RefreshCorpsDeploymentPlanCurrentState(shiftedCityBase);

        AddNotification(
            (std::string(u8"군단 자동배치 2단계: ") +
             failReason + u8" 전체 원복했습니다.").c_str());
        AddLog(u8"[군단 자동배치 2단계] 실패/전체 롤백: %s",
               failReason.c_str());
        return false;
      }

      char notice[256]{};
      sprintf_s(notice,
                u8"군단 자동배치 2단계 완료: 태수 교체 %d회 / 최초 임명 %d회 / 이동 %d회 / 보류 %d도시",
                swapCount, firstGovernorCount,
                moveCount, deferredCount);
      AddNotification(notice);
      AddLog(u8"[군단 자동배치 2단계] 완료: 교체 %d / 최초임명 %d / 이동 %d / 순환해제 %d / 보류 %d",
             swapCount, firstGovernorCount,
             moveCount, cycleBreakCount, deferredCount);
      return true;
    }


    static void DrawCorpsDeploymentRecommendation(
        uintptr_t p1, uintptr_t shiftedCityBase, float sc) {
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.f),
                         u8"[ 군단 자동배치 추천 ]");
      ImGui::SameLine(0.f, 12.f * sc);
      ImGui::TextDisabled(u8"계획 / 단계 적용");

      if (ImGui::Checkbox(
              u8"매 평정 자동 배치 사용##CorpsAutoEachCouncil",
              &g_corpsAutoDeploymentEachCouncil)) {
        SaveConfig();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            u8"ON이면 현재 세션에서 지정한 군단만 평정 진입(07->05) 때 자동으로 추천 계산→1단계→2단계를 실행합니다.");
        ImGui::TextUnformatted(
            u8"설정 파일에는 대상 식별값을 저장하지만, 게임 재실행 후에는 안전을 위해 대상 군단을 다시 지정해야 자동 실행됩니다.");
        ImGui::EndTooltip();
      }

      ImGui::SameLine(0.f, 12.f * sc);
      const bool canBindAutoTarget =
          s_officerSelectedCorpsPtr > 0x10000;
      if (!canBindAutoTarget)
        ImGui::BeginDisabled();
      if (ImGui::Button(
              u8"현재 군단을 자동 대상으로 지정##CorpsAutoBind",
              ImVec2(210.f * sc, 0.f))) {
        const OfficerCityCorpsInfo info =
            GetOfficerCityCorpsInfo(
                shiftedCityBase, s_officerCityIndex);
        if (BindCorpsAutoDeploymentTarget(info)) {
          AddNotification(
              u8"군단 자동배치: 현재 군단을 이 세션의 자동 적용 대상으로 지정했습니다.");
        } else {
          AddNotification(
              u8"군단 자동배치: 시나리오/군주/군단/도독 식별값을 읽지 못해 자동 대상을 지정하지 못했습니다.");
        }
      }
      if (!canBindAutoTarget)
        ImGui::EndDisabled();

      ImGui::SameLine(0.f, 10.f * sc);
      if (s_corpsAutoSessionArmed) {
        ImGui::TextColored(
            ImVec4(0.45f, 0.95f, 0.55f, 1.f),
            u8"자동 대상: %d군단 / 도독 %s / 현재 세이브 활성",
            g_corpsAutoCorpsNo,
            BuildOfficerName(
                (uint16_t)g_corpsAutoGovernorGeneralId).c_str());
      } else if (g_corpsAutoDeploymentEachCouncil &&
                 g_corpsAutoCorpsNo > 0) {
        ImGui::TextColored(
            ImVec4(1.f, 0.70f, 0.25f, 1.f),
            u8"저장 대상: %d군단 / 현재 세이브 재지정 필요",
            g_corpsAutoCorpsNo);
      } else {
        ImGui::TextDisabled(u8"자동 대상 미지정");
      }

      const bool canBuild = s_officerSelectedCorpsPtr > 0x10000;
      if (!canBuild)
        ImGui::BeginDisabled();
      if (ImGui::Button(u8"추천 계산##CorpsDeploymentBuild",
                        ImVec2(120.f * sc, 0.f))) {
        ResetCorpsDeploymentTargetBaseline();
        BuildCorpsDeploymentRecommendation(shiftedCityBase);
      }
      if (!canBuild)
        ImGui::EndDisabled();

      ImGui::SameLine(0.f, 6.f * sc);
      const bool hasTargetBaseline =
          s_corpsDeploymentTargetCorpsPtr == s_officerSelectedCorpsPtr &&
          !s_corpsDeploymentTargetBaseline.empty();
      if (!hasTargetBaseline)
        ImGui::BeginDisabled();
      if (ImGui::SmallButton(u8"현재 계획 취소##CorpsDeploymentReset")) {
        ResetCorpsDeploymentTargetBaseline();
      }
      if (!hasTargetBaseline)
        ImGui::EndDisabled();

      ImGui::SameLine(0.f, 12.f * sc);
      ImGui::TextDisabled(
          u8"태수=충성100 필수 | 충성<90 후방 고정 | 군사 신분 유지/전선 우선");
      if (hasTargetBaseline) {
        ImGui::SameLine(0.f, 8.f * sc);
        ImGui::TextDisabled(u8"| 이번 평정 계획 고정");
      }

      if (!s_corpsDeploymentValid ||
          s_corpsDeploymentCorpsPtr != s_officerSelectedCorpsPtr) {
        ImGui::TextDisabled(
            u8"군단 도시를 선택한 뒤 '추천 계산'을 눌러주세요.");
        return;
      }

      int movableNormal = 0;
      int movableAdviser = 0;
      for (const auto &rec : s_corpsDeploymentOfficers) {
        if (rec.currentCityIndex == rec.recommendedCityIndex)
          continue;
        if (rec.status == 0x28)
          ++movableNormal;
        else if (rec.status == 0x18)
          ++movableAdviser;
      }

      ImGui::SameLine(0.f, 12.f * sc);
      const bool hasMovable = movableNormal > 0 || movableAdviser > 0;
      if (!hasMovable)
        ImGui::BeginDisabled();
      if (ImGui::Button(u8"1단계 배치 적용##CorpsDeploymentMoveApply",
                        ImVec2(145.f * sc, 0.f))) {
        ApplyCorpsDeploymentMovements(p1, shiftedCityBase);
      }
      if (!hasMovable)
        ImGui::EndDisabled();

      ImGui::SameLine(0.f, 8.f * sc);
      ImGui::TextDisabled(u8"일반 %d / 군사 %d 이동 예정",
                          movableNormal, movableAdviser);

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            u8"1단계는 같은 군단 안에서 일반/군사의 도시만 이동합니다.");
        ImGui::TextUnformatted(
            u8"군주·도독·태수의 신분과 태수 포인터는 아직 변경하지 않습니다.");
        ImGui::EndTooltip();
      }

      int deferredGovernorChanges = 0;
      const int pendingGovernorChanges =
          CountPendingDeploymentGovernorChanges(
              shiftedCityBase, &deferredGovernorChanges);
      ImGui::SameLine(0.f, 12.f * sc);
      const bool canApplyGovernorStage =
          !hasMovable && pendingGovernorChanges > 0;
      if (!canApplyGovernorStage)
        ImGui::BeginDisabled();
      if (ImGui::Button(u8"2단계 태수 적용##CorpsDeploymentGovernorApply",
                        ImVec2(145.f * sc, 0.f))) {
        ApplyCorpsDeploymentGovernorStage(p1, shiftedCityBase);
      }
      if (!canApplyGovernorStage)
        ImGui::EndDisabled();

      ImGui::SameLine(0.f, 8.f * sc);
      ImGui::TextDisabled(u8"태수 변경 %d / 보류 %d 도시",
                          pendingGovernorChanges,
                          deferredGovernorChanges);

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            u8"기존 태수를 현지에서 일반 신분으로 해제한 뒤 필요한 경우 이동합니다.");
        ImGui::TextUnformatted(
            u8"태수끼리 순환하는 경우 충성 100 일반 장수를 임시 태수로 사용합니다.");
        ImGui::TextUnformatted(
            u8"빈 도시는 충성100 28/D3 일반장수를 먼저 배치한 경우에만 최초 태수(E8/D2)로 임명합니다.");
        ImGui::TextUnformatted(
            u8"안전한 일반장수가 없으면 빈 도시는 이번 계획에서 그대로 두고 AI 처리를 기다립니다.");
        ImGui::TextUnformatted(
            u8"0이 아닌 City+0x98이 E8이 아닌 특수 상태만 진단 후 보류합니다.");
        ImGui::TextUnformatted(
            u8"중간 실패 시 적용 대상 도시는 2단계 시작 직전 상태로 전체 원복합니다.");
        ImGui::EndTooltip();
      }

      static ImGuiTableFlags deployFlags =
          ImGuiTableFlags_BordersInner |
          ImGuiTableFlags_RowBg |
          ImGuiTableFlags_SizingFixedFit |
          ImGuiTableFlags_NoSavedSettings;

      if (ImGui::BeginTable("##CorpsDeploymentCityTbl", 7, deployFlags,
                            ImVec2(0.f, 0.f))) {
        ImGui::TableSetupColumn(u8"구분", ImGuiTableColumnFlags_WidthFixed,
                                50.f * sc);
        ImGui::TableSetupColumn(u8"도시", ImGuiTableColumnFlags_WidthFixed,
                                80.f * sc);
        ImGui::TableSetupColumn(u8"인원", ImGuiTableColumnFlags_WidthFixed,
                                55.f * sc);
        ImGui::TableSetupColumn(u8"현재 책임자",
                                ImGuiTableColumnFlags_WidthFixed,
                                105.f * sc);
        ImGui::TableSetupColumn(u8"추천 책임자",
                                ImGuiTableColumnFlags_WidthFixed,
                                105.f * sc);
        ImGui::TableSetupColumn(u8"점수", ImGuiTableColumnFlags_WidthFixed,
                                55.f * sc);
        ImGui::TableSetupColumn(u8"추천 배치",
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto &city : s_corpsDeploymentCities) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(city.frontline ? u8"전선" : u8"후방");

          ImGui::TableSetColumnIndex(1);
          ImGui::TextUnformatted(g_CityList[city.cityIndex].cityname);

          ImGui::TableSetColumnIndex(2);
          ImGui::Text("%d→%d", city.currentCount, city.targetCount);

          ImGui::TableSetColumnIndex(3);
          if (city.currentGovernorId)
            ImGui::TextUnformatted(
                BuildOfficerName(city.currentGovernorId).c_str());
          else
            ImGui::TextDisabled(u8"없음");

          ImGui::TableSetColumnIndex(4);
          if (city.recommendedGovernorId)
            ImGui::TextUnformatted(
                BuildOfficerName(city.recommendedGovernorId).c_str());
          else
            ImGui::TextColored(ImVec4(1.f, 0.45f, 0.25f, 1.f),
                               u8"후보 부족");

          ImGui::TableSetColumnIndex(5);
          if (city.governorScore > 0)
            ImGui::Text("%d", city.governorScore);
          else
            ImGui::TextDisabled("-");

          ImGui::TableSetColumnIndex(6);
          std::string names;
          for (uint16_t id : city.recommendedOfficerIds) {
            if (!names.empty())
              names += ", ";
            names += BuildOfficerName(id);
          }
          if (names.empty())
            ImGui::TextDisabled(u8"없음");
          else
            ImGui::TextWrapped("%s", names.c_str());
        }
        ImGui::EndTable();
      }

      ImGui::Spacing();
      ImGui::TextUnformatted(u8"이동/역할 변경 예정");

      if (ImGui::BeginTable("##CorpsDeploymentOfficerTbl", 7, deployFlags,
                            ImVec2(0.f, 220.f * sc))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(u8"이름", ImGuiTableColumnFlags_WidthFixed,
                                105.f * sc);
        ImGui::TableSetupColumn(u8"현재 신분",
                                ImGuiTableColumnFlags_WidthFixed,
                                65.f * sc);
        ImGui::TableSetupColumn(u8"충성", ImGuiTableColumnFlags_WidthFixed,
                                45.f * sc);
        ImGui::TableSetupColumn(u8"현재", ImGuiTableColumnFlags_WidthFixed,
                                75.f * sc);
        ImGui::TableSetupColumn(u8"추천", ImGuiTableColumnFlags_WidthFixed,
                                75.f * sc);
        ImGui::TableSetupColumn(u8"역할", ImGuiTableColumnFlags_WidthFixed,
                                60.f * sc);
        ImGui::TableSetupColumn(u8"이유",
                                ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const auto &rec : s_corpsDeploymentOfficers) {
          if (rec.currentCityIndex == rec.recommendedCityIndex &&
              !rec.recommendedGovernor)
            continue;

          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(BuildOfficerName(rec.id).c_str());

          ImGui::TableSetColumnIndex(1);
          ImGui::TextUnformatted(GetOfficerStatusName(rec.status));

          ImGui::TableSetColumnIndex(2);
          if (rec.loyalty < 90)
            ImGui::TextColored(ImVec4(1.f, 0.4f, 0.25f, 1.f),
                               "%u", (unsigned int)rec.loyalty);
          else
            ImGui::Text("%u", (unsigned int)rec.loyalty);

          ImGui::TableSetColumnIndex(3);
          if (rec.currentCityIndex >= 0)
            ImGui::TextUnformatted(
                g_CityList[rec.currentCityIndex].cityname);
          else
            ImGui::TextDisabled("?");

          ImGui::TableSetColumnIndex(4);
          if (rec.recommendedCityIndex >= 0)
            ImGui::TextUnformatted(
                g_CityList[rec.recommendedCityIndex].cityname);
          else
            ImGui::TextDisabled("?");

          ImGui::TableSetColumnIndex(5);
          ImGui::TextUnformatted(
              rec.recommendedGovernor
                  ? (rec.status == 0xC8
                         ? u8"군주 고정"
                         : (rec.status == 0xD8 ? u8"도독 고정"
                                               : u8"태수"))
                  : (rec.status == 0x18
                         ? u8"군사 유지"
                         : (rec.status == 0xE8
                                ? u8"태수 해제→배치"
                                : u8"배치")));

          ImGui::TableSetColumnIndex(6);
          ImGui::TextWrapped("%s", rec.reason.c_str());
        }
        ImGui::EndTable();
      }
    }


    static bool ApplyGovernorGeneralSwap(uintptr_t p1,
                                           uintptr_t shiftedCityBase) {
      NormalizeGovernorGeneralSelections();

      if (s_officerSelectedCorpsPtr <= 0x10000) {
        AddNotification(u8"도독 교체: 현재 도시는 군단 소속이 아닙니다.");
        return false;
      }

      const CityOfficerRow *oldGov =
          FindCorpsOfficerById(s_governorGeneralOldId);
      const CityOfficerRow *candidate =
          FindCorpsOfficerById(s_governorGeneralNewId);
      if (!oldGov || !candidate ||
          oldGov->status != 0xD8 || candidate->status != 0xE8) {
        AddNotification(u8"도독 교체: 현재 도독과 태수 후보를 다시 선택해주세요.");
        return false;
      }

      uint8_t oldStatus = 0;
      uint8_t newStatus = 0;
      uint8_t oldAux = 0;
      uint8_t newAux = 0;
      uintptr_t divisionGovernor = 0;
      uintptr_t divisionForce = 0;
      uintptr_t oldForce = 0, newForce = 0;
      uintptr_t oldCity = 0, newCity = 0;
      uintptr_t oldCorps = 0, newCorps = 0;

      if (!SafeRead8(oldGov->officerBase + 0x10, &oldStatus) ||
          !SafeRead8(candidate->officerBase + 0x10, &newStatus) ||
          !SafeRead8(oldGov->officerBase + 0x11, &oldAux) ||
          !SafeRead8(candidate->officerBase + 0x11, &newAux) ||
          !SafeReadPtr(s_officerSelectedCorpsPtr + 0x20,
                       &divisionGovernor) ||
          !SafeReadPtr(s_officerSelectedCorpsPtr + 0x10,
                       &divisionForce) ||
          !SafeReadPtr(oldGov->officerBase + 0x18, &oldForce) ||
          !SafeReadPtr(candidate->officerBase + 0x18, &newForce) ||
          !SafeReadPtr(oldGov->officerBase + 0x20, &oldCity) ||
          !SafeReadPtr(candidate->officerBase + 0x20, &newCity) ||
          !SafeReadPtrAllowZero(oldCity + OFF_CITY_CORPS_RAW, &oldCorps) ||
          !SafeReadPtrAllowZero(newCity + OFF_CITY_CORPS_RAW, &newCorps)) {
        AddNotification(u8"도독 교체: 현재 메모리 상태를 읽지 못했습니다.");
        return false;
      }

      if (oldStatus != 0xD8 || newStatus != 0xE8) {
        AddNotification(u8"도독 교체: 신분 바이트(+0x10)가 예상과 달라 중단했습니다.");
        AddLog(u8"[도독교체] 신분 불일치: 기존=%02X(+0x11=%02X) / 후보=%02X(+0x11=%02X)",
               (unsigned int)oldStatus, (unsigned int)oldAux,
               (unsigned int)newStatus, (unsigned int)newAux);
        return false;
      }

      if (divisionGovernor != oldGov->officerBase) {
        AddNotification(u8"도독 교체: Division+0x20의 현재 도독이 선택한 도독과 다릅니다.");
        AddLog(u8"[도독교체] Division+0x20 불일치: 현재=0x%llX / 선택=0x%llX",
               (unsigned long long)divisionGovernor,
               (unsigned long long)oldGov->officerBase);
        return false;
      }

      if (!divisionForce || oldForce != divisionForce ||
          newForce != divisionForce ||
          (s_officerPlayerForce && divisionForce != s_officerPlayerForce)) {
        AddNotification(u8"도독 교체: 군단과 두 무장의 소속 세력이 일치하지 않습니다.");
        AddLog(u8"[도독교체] 세력 불일치: 군단=0x%llX / 기존=0x%llX / 후보=0x%llX / 플레이어=0x%llX",
               (unsigned long long)divisionForce,
               (unsigned long long)oldForce,
               (unsigned long long)newForce,
               (unsigned long long)s_officerPlayerForce);
        return false;
      }

      if (oldCorps != s_officerSelectedCorpsPtr ||
          newCorps != s_officerSelectedCorpsPtr) {
        AddNotification(u8"도독 교체: 두 무장이 현재 같은 군단 소속이 아닙니다.");
        AddLog(u8"[도독교체] 군단 불일치: 선택=0x%llX / 기존도시군단=0x%llX / 후보도시군단=0x%llX",
               (unsigned long long)s_officerSelectedCorpsPtr,
               (unsigned long long)oldCorps,
               (unsigned long long)newCorps);
        return false;
      }

      // 정상 게임 실측:
      //   기존 도독 +0x10 D8 -> E8
      //   후보 태수 +0x10 E8 -> D8
      //   DivisionData +0x20 기존 도독 -> 신규 도독
      // +0x11은 양쪽 모두 변하지 않았으므로 절대 수정하지 않는다.
      if (!SafeWrite8(oldGov->officerBase + 0x10, 0xE8)) {
        AddNotification(u8"도독 교체: 기존 도독 신분 쓰기에 실패했습니다.");
        return false;
      }

      if (!SafeWrite8(candidate->officerBase + 0x10, 0xD8)) {
        SafeWrite8(oldGov->officerBase + 0x10, 0xD8);
        AddNotification(u8"도독 교체: 후보 태수 신분 쓰기에 실패해 원복했습니다.");
        return false;
      }

      if (!SafeWritePtr(s_officerSelectedCorpsPtr + 0x20,
                        candidate->officerBase)) {
        SafeWrite8(candidate->officerBase + 0x10, 0xE8);
        SafeWrite8(oldGov->officerBase + 0x10, 0xD8);
        AddNotification(u8"도독 교체: 군단 도독 포인터 쓰기에 실패해 원복했습니다.");
        return false;
      }

      uint8_t oldStatusAfter = 0;
      uint8_t newStatusAfter = 0;
      uint8_t oldAuxAfter = 0;
      uint8_t newAuxAfter = 0;
      uintptr_t divisionGovernorAfter = 0;
      const bool verifyOk =
          SafeRead8(oldGov->officerBase + 0x10, &oldStatusAfter) &&
          SafeRead8(candidate->officerBase + 0x10, &newStatusAfter) &&
          SafeRead8(oldGov->officerBase + 0x11, &oldAuxAfter) &&
          SafeRead8(candidate->officerBase + 0x11, &newAuxAfter) &&
          SafeReadPtr(s_officerSelectedCorpsPtr + 0x20,
                      &divisionGovernorAfter) &&
          oldStatusAfter == 0xE8 &&
          newStatusAfter == 0xD8 &&
          oldAuxAfter == oldAux &&
          newAuxAfter == newAux &&
          divisionGovernorAfter == candidate->officerBase;

      if (!verifyOk) {
        SafeWritePtr(s_officerSelectedCorpsPtr + 0x20,
                     oldGov->officerBase);
        SafeWrite8(candidate->officerBase + 0x10, 0xE8);
        SafeWrite8(oldGov->officerBase + 0x10, 0xD8);
        AddNotification(u8"도독 교체: 적용 후 검증에 실패해 원복했습니다.");
        AddLog(u8"[도독교체] 검증 실패: 기존=%02X 후보=%02X Division+0x20=0x%llX / +0x11 기존=%02X->%02X 후보=%02X->%02X",
               (unsigned int)oldStatusAfter,
               (unsigned int)newStatusAfter,
               (unsigned long long)divisionGovernorAfter,
               (unsigned int)oldAux, (unsigned int)oldAuxAfter,
               (unsigned int)newAux, (unsigned int)newAuxAfter);
        return false;
      }

      const std::string oldName = BuildOfficerName(oldGov->id);
      const std::string newName = BuildOfficerName(candidate->id);
      uintptr_t corpsNo = 0;
      SafeReadPtrAllowZero(s_officerSelectedCorpsPtr + 0x18, &corpsNo);

      AddLog(u8"[도독교체] %llu군단: %s -> %s",
             (unsigned long long)corpsNo,
             oldName.c_str(), newName.c_str());
      AddLog(u8"[도독교체] 기존 %s +0x10 D8->E8 / 후보 %s +0x10 E8->D8 / +0x11 유지 %02X,%02X",
             oldName.c_str(), newName.c_str(),
             (unsigned int)oldAux, (unsigned int)newAux);
      AddLog(u8"[도독교체] Division+0x20: 0x%llX -> 0x%llX",
             (unsigned long long)oldGov->officerBase,
             (unsigned long long)candidate->officerBase);

      char notice[256]{};
      sprintf_s(notice, u8"%llu군단 도독: %s → %s",
                (unsigned long long)corpsNo,
                oldName.c_str(), newName.c_str());
      AddNotification(notice);
      s_selectedOfficerId = -1;
      s_officerRosterDirty = true;
      RefreshCityOfficerRoster(p1, shiftedCityBase);
      NormalizeGovernorGeneralSelections();
      return true;
    }


    static bool ApplyGovernorSwap(uintptr_t p1, uintptr_t shiftedCityBase) {
      NormalizeGovernorSelections();

      const CityOfficerRow *oldGov = FindCityOfficerById(s_governorOldId);
      const CityOfficerRow *candidate = FindCityOfficerById(s_governorNewId);
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

      const std::string oldName = BuildOfficerName(oldGov->id);
      const std::string newName = BuildOfficerName(candidate->id);
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
      s_selectedOfficerId = -1;
      s_officerRosterDirty = true;
      RefreshCityOfficerRoster(p1, shiftedCityBase);
      NormalizeGovernorSelections();
      return true;
    }

    static void DrawGovernorPanel(uintptr_t p1,
                                  uintptr_t shiftedCityBase,
                                  float sc) {
      NormalizeGovernorSelections();

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.30f, 1.f),
                         u8"[ 태수 교체 ]");

      const char *cityName =
          (s_officerCityIndex >= 0 && s_officerCityIndex < g_CityCount)
              ? g_CityList[s_officerCityIndex].cityname
              : u8"도시 없음";
      ImGui::Text(u8"도시: %s", cityName);
      ImGui::SameLine(0.f, 18.f * sc);

      const CityOfficerRow *oldGov = FindCityOfficerById(s_governorOldId);
      const std::string oldGovName =
          oldGov ? BuildOfficerName(oldGov->id) : u8"태수 없음";
      ImGui::TextUnformatted(u8"현재 태수");
      ImGui::SameLine(0.f, 6.f * sc);
      ImGui::SetNextItemWidth(125.f * sc);
      if (ImGui::BeginCombo("##GovernorOld", oldGovName.c_str())) {
        for (const auto &row : s_cityOfficerRows) {
          if (row.status != 0xE8)
            continue;
          const std::string name = BuildOfficerName(row.id);
          const bool selected = ((int)row.id == s_governorOldId);
          if (ImGui::Selectable(name.c_str(), selected))
            s_governorOldId = row.id;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::SameLine(0.f, 18.f * sc);
      const CityOfficerRow *candidate = FindCityOfficerById(s_governorNewId);
      const std::string candidateName =
          candidate ? BuildOfficerName(candidate->id) : u8"일반 없음";
      ImGui::TextUnformatted(u8"교체 후보");
      ImGui::SameLine(0.f, 6.f * sc);
      ImGui::SetNextItemWidth(125.f * sc);
      if (ImGui::BeginCombo("##GovernorNew", candidateName.c_str())) {
        for (const auto &row : s_cityOfficerRows) {
          if (row.status != 0x28)
            continue;
          const std::string name = BuildOfficerName(row.id);
          const bool selected = ((int)row.id == s_governorNewId);
          if (ImGui::Selectable(name.c_str(), selected))
            s_governorNewId = row.id;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      const bool canApply =
          oldGov != nullptr && candidate != nullptr &&
          s_officerCityIndex >= 0;

      ImGui::SameLine(0.f, 12.f * sc);
      if (!canApply)
        ImGui::BeginDisabled();
      if (ImGui::Button(u8"태수 교체##GovernorApply",
                        ImVec2(125.f * sc, 0.f)))
        ApplyGovernorSwap(p1, shiftedCityBase);
      if (!canApply)
        ImGui::EndDisabled();

      if (bShowDebug && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            u8"디버그: Officer +0x10/+0x11 상태쌍과 City +0x98 태수 포인터를 교체합니다.");
        ImGui::EndTooltip();
      }
    }


    static void DrawGovernorGeneralPanel(
        uintptr_t p1, uintptr_t shiftedCityBase, float sc) {
      NormalizeGovernorGeneralSelections();

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextColored(ImVec4(0.85f, 0.60f, 1.0f, 1.f),
                         u8"[ 도독 교체 ]");

      if (s_officerSelectedCorpsPtr <= 0x10000) {
        ImGui::TextDisabled(u8"현재 도시는 군단 소속이 아닙니다.");
        return;
      }

      const std::string corpsName =
          GetOfficerCorpsName(shiftedCityBase, s_officerCityIndex);
      ImGui::Text(u8"현재 군단: %s", corpsName.c_str());
      if (bShowDebug) {
        ImGui::SameLine(0.f, 10.f * sc);
        ImGui::TextDisabled(u8"Division 0x%llX",
                            (unsigned long long)s_officerSelectedCorpsPtr);
      }

      const CityOfficerRow *oldGov =
          FindCorpsOfficerById(s_governorGeneralOldId);
      const std::string oldName =
          oldGov ? BuildOfficerName(oldGov->id) : u8"도독 없음";

      ImGui::TextUnformatted(u8"현재 도독");
      ImGui::SameLine(0.f, 6.f * sc);
      ImGui::SetNextItemWidth(135.f * sc);
      if (ImGui::BeginCombo("##GovernorGeneralOld", oldName.c_str())) {
        for (const auto &row : s_corpsOfficerRows) {
          if (row.status != 0xD8)
            continue;
          const std::string name = BuildOfficerName(row.id);
          const bool selected =
              ((int)row.id == s_governorGeneralOldId);
          if (ImGui::Selectable(name.c_str(), selected))
            s_governorGeneralOldId = row.id;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::SameLine(0.f, 18.f * sc);
      const CityOfficerRow *candidate =
          FindCorpsOfficerById(s_governorGeneralNewId);
      const std::string candidateName =
          candidate ? BuildOfficerName(candidate->id) : u8"태수 없음";

      ImGui::TextUnformatted(u8"교체 태수");
      ImGui::SameLine(0.f, 6.f * sc);
      ImGui::SetNextItemWidth(135.f * sc);
      if (ImGui::BeginCombo("##GovernorGeneralNew",
                            candidateName.c_str())) {
        for (const auto &row : s_corpsOfficerRows) {
          if (row.status != 0xE8)
            continue;
          const std::string name = BuildOfficerName(row.id);
          const bool selected =
              ((int)row.id == s_governorGeneralNewId);
          if (ImGui::Selectable(name.c_str(), selected))
            s_governorGeneralNewId = row.id;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      const bool canApply =
          oldGov != nullptr && candidate != nullptr;

      ImGui::SameLine(0.f, 12.f * sc);
      if (!canApply)
        ImGui::BeginDisabled();
      if (ImGui::Button(u8"도독 교체##GovernorGeneralApply",
                        ImVec2(125.f * sc, 0.f)))
        ApplyGovernorGeneralSwap(p1, shiftedCityBase);
      if (!canApply)
        ImGui::EndDisabled();

      ImGui::SameLine(0.f, 12.f * sc);
      ImGui::TextDisabled(u8"같은 군단의 태수만 선택 가능");

      if (bShowDebug && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            u8"디버그: 기존 D8→E8, 후보 E8→D8, Division +0x20 도독 포인터를 교체합니다.");
        ImGui::TextUnformatted(u8"+0x11과 도시/군단 소속 포인터는 변경하지 않습니다.");
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
                         u8"[ 군단 디버그 ]");
      ImGui::SameLine(0.f, 12.f * sc);
      ImGui::TextDisabled(u8"CityData / DivisionData 포인터");

      ImGui::Text(u8"도시: %s  Raw CityData: 0x%llX",
                  g_CityList[s_officerCityIndex].cityname,
                  (unsigned long long)rawCity);

      if (!corpsRead) {
        ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f),
                           u8"City +0x90 읽기 실패");
        return;
      }

      ImGui::Text(u8"City +0x90 군단 포인터: 0x%llX",
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

        ImGui::Text(u8"군단 +0x10 세력 포인터: %s0x%llX",
                    forceOk ? "" : u8"(읽기 실패) ",
                    (unsigned long long)forcePtr);
        ImGui::Text(u8"군단 +0x18 군단 번호: %s%llu (0x%llX)",
                    noOk ? "" : u8"(읽기 실패) ",
                    (unsigned long long)corpsNoRaw,
                    (unsigned long long)corpsNoRaw);
        ImGui::Text(u8"군단 +0x20 도독 포인터: %s0x%llX",
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


    // ── 구 CT 기반 무장 관계 탐색 (읽기 전용) ─────────────────────────────
    // SAN8R v13.54 CT 기준:
    // 숙명: stride 0x20, +08/+10 Officer*, +18 relation(1 상극/2 상생), +19 발생
    // 관계: stride 0x40, +08 relation(1 의형제/2 배우자/3 원수/4 호적수),
    //       +10/+18/+20/+28/+30 Officer*, +38 flag
    static constexpr uintptr_t LEGACY_SYNERGETIC_PTR_OFFSET = 0x433210;
    static constexpr uintptr_t LEGACY_RELATION_PTR_OFFSET = 0x462BA8;
    static constexpr uintptr_t LEGACY_RELATION_PTR_DELTA =
        LEGACY_RELATION_PTR_OFFSET - LEGACY_SYNERGETIC_PTR_OFFSET;

    static bool IsRelationshipRosterOfficerPtr(
        uintptr_t ptr, uintptr_t rosterBase) {
      if (ptr < rosterBase)
        return false;
      const uintptr_t delta = ptr - rosterBase;
      if (delta >= (uintptr_t)5102 * 0x3D0)
        return false;
      return (delta % 0x3D0) == 0;
    }

    static bool ReadRelationshipOfficerId(
        uintptr_t ptr, uintptr_t rosterBase, uint16_t *outId) {
      if (!outId ||
          !IsRelationshipRosterOfficerPtr(ptr, rosterBase))
        return false;
      uint16_t id = 0;
      if (!SafeRead16(ptr + 0x08, &id) ||
          id == 0 || id > 5200)
        return false;
      *outId = id;
      return true;
    }

    static int ScoreSynergeticBase(
        uintptr_t base, uintptr_t rosterBase,
        int sampleCount = 384) {
      if (base <= 0x10000 || rosterBase <= 0x10000)
        return -1;

      int valid = 0;
      int invalid = 0;
      for (int i = 0; i < sampleCount; ++i) {
        const uintptr_t slot =
            base + (uintptr_t)i * 0x20;
        uint8_t relation = 0;
        if (!SafeRead8(slot + 0x18, &relation))
          return -1;
        if (relation == 0)
          continue;
        if (relation != 1 && relation != 2) {
          if (++invalid > 3)
            return -1;
          continue;
        }

        uintptr_t p1 = 0, p2 = 0;
        if (!SafeReadPtrAllowZero(slot + 0x08, &p1) ||
            !SafeReadPtrAllowZero(slot + 0x10, &p2) ||
            !IsRelationshipRosterOfficerPtr(p1, rosterBase) ||
            !IsRelationshipRosterOfficerPtr(p2, rosterBase)) {
          if (++invalid > 3)
            return -1;
          continue;
        }
        ++valid;
      }
      return valid;
    }

    static bool TryResolveSynergeticBaseFromLegacyCt(
        uintptr_t exeBase, uintptr_t gameBase,
        uintptr_t rosterBase, uintptr_t *outBase,
        uintptr_t *outFieldOffset, uintptr_t *outPatternAddr) {
      if (!outBase || !outFieldOffset || !outPatternAddr)
        return false;
      *outBase = 0;
      *outFieldOffset = 0;
      *outPatternAddr = 0;

      // 구 CT가 사용하던 시그니처:
      // 48 8D [modrm + disp32] 48 3B ? 44 0F ? ? 77 ? 48 8B
      uintptr_t imageEnd = 0;
      SafeGetModuleImageEnd(exeBase, &imageEnd);

      const std::string pattern =
          "48 8D ? ? ? ? ? 48 3B ? 44 0F ? ? 77 ? 48 8B";

      if (imageEnd > exeBase) {
        uintptr_t search = exeBase;
        for (int matchIndex = 0;
             matchIndex < 32 && search + 32 < imageEnd;
             ++matchIndex) {
          const uintptr_t found =
              FindPattern(search, imageEnd, pattern);
          if (!found)
            break;

          int32_t disp = 0;
          SafeReadS32(found + 3, &disp);

          if (disp > 0) {
            uintptr_t tablePtr = 0;
            if (SafeReadPtrAllowZero(
                    gameBase + (uintptr_t)(uint32_t)disp,
                    &tablePtr) &&
                tablePtr > 0x10000) {
              const uintptr_t candidate = tablePtr + 0xA0;
              const int score =
                  ScoreSynergeticBase(candidate, rosterBase);
              if (score >= 3) {
                *outBase = candidate;
                *outFieldOffset =
                    (uintptr_t)(uint32_t)disp;
                *outPatternAddr = found;
                return true;
              }
            }
          }
          search = found + 1;
        }
      }

      // 시그니처가 달라졌을 때를 위한 제한적 fallback.
      // 구 CT 오프셋 근처의 gameBase 포인터 필드만 읽어 구조 점수로 확인한다.
      const intptr_t window = 0x10000;
      for (intptr_t delta = -window;
           delta <= window; delta += 8) {
        const intptr_t signedOff =
            (intptr_t)LEGACY_SYNERGETIC_PTR_OFFSET + delta;
        if (signedOff <= 0)
          continue;

        uintptr_t tablePtr = 0;
        if (!SafeReadPtrAllowZero(
                gameBase + (uintptr_t)signedOff,
                &tablePtr) ||
            tablePtr <= 0x10000)
          continue;

        const uintptr_t candidate = tablePtr + 0xA0;
        const int score =
            ScoreSynergeticBase(candidate, rosterBase, 256);
        if (score >= 3) {
          *outBase = candidate;
          *outFieldOffset = (uintptr_t)signedOff;
          return true;
        }
      }
      return false;
    }

    static int ScoreRelationshipBase(
        uintptr_t base, uintptr_t rosterBase,
        int sampleCount = 384) {
      if (base <= 0x10000 || rosterBase <= 0x10000)
        return -1;

      int valid = 0;
      int invalid = 0;
      for (int i = 0; i < sampleCount; ++i) {
        const uintptr_t slot =
            base + (uintptr_t)i * 0x40;
        uint8_t relation = 0;
        if (!SafeRead8(slot + 0x08, &relation))
          return -1;
        if (relation == 0)
          continue;
        if (relation < 1 || relation > 4) {
          if (++invalid > 3)
            return -1;
          continue;
        }

        uintptr_t p1 = 0, p2 = 0;
        if (!SafeReadPtrAllowZero(slot + 0x10, &p1) ||
            !SafeReadPtrAllowZero(slot + 0x18, &p2) ||
            !IsRelationshipRosterOfficerPtr(p1, rosterBase) ||
            !IsRelationshipRosterOfficerPtr(p2, rosterBase)) {
          if (++invalid > 3)
            return -1;
          continue;
        }
        ++valid;
      }
      return valid;
    }

    static bool TryResolveRelationshipBaseFromLegacyCt(
        uintptr_t gameBase, uintptr_t rosterBase,
        uintptr_t synergeticFieldOffset,
        uintptr_t *outBase, uintptr_t *outFieldOffset) {
      if (!outBase || !outFieldOffset)
        return false;
      *outBase = 0;
      *outFieldOffset = 0;

      const uintptr_t center =
          synergeticFieldOffset > 0
              ? synergeticFieldOffset +
                    LEGACY_RELATION_PTR_DELTA
              : LEGACY_RELATION_PTR_OFFSET;

      // 먼저 구 CT의 상대 거리 그대로 확인.
      uintptr_t tablePtr = 0;
      if (SafeReadPtrAllowZero(
              gameBase + center, &tablePtr) &&
          tablePtr > 0x10000 &&
          tablePtr > 0x80) {
        const uintptr_t candidate = tablePtr - 0x80;
        if (ScoreRelationshipBase(
                candidate, rosterBase) >= 3) {
          *outBase = candidate;
          *outFieldOffset = center;
          return true;
        }
      }

      // PK에서 필드가 조금 이동했을 가능성만 제한적으로 탐색한다.
      const intptr_t window = 0x20000;
      for (intptr_t delta = -window;
           delta <= window; delta += 8) {
        if (delta == 0)
          continue;
        const intptr_t signedOff =
            (intptr_t)center + delta;
        if (signedOff <= 0)
          continue;

        uintptr_t ptr = 0;
        if (!SafeReadPtrAllowZero(
                gameBase + (uintptr_t)signedOff, &ptr) ||
            ptr <= 0x10080)
          continue;

        const uintptr_t candidate = ptr - 0x80;
        const int score =
            ScoreRelationshipBase(
                candidate, rosterBase, 256);
        if (score >= 3) {
          *outBase = candidate;
          *outFieldOffset = (uintptr_t)signedOff;
          return true;
        }
      }
      return false;
    }

    static const char *GetLegacyRelationshipName(
        uint8_t relation) {
      switch (relation) {
      case 1: return u8"의형제";
      case 2: return u8"배우자";
      case 3: return u8"원수";
      case 4: return u8"호적수";
      default: return u8"?";
      }
    }

    static const char *GetLegacySynergeticName(
        uint8_t relation) {
      switch (relation) {
      case 1: return u8"상극";
      case 2: return u8"상생";
      default: return u8"?";
      }
    }

    static void LogSelectedOfficerRelationshipProbe(
        const CityOfficerRow &selected) {
      const uintptr_t exe =
          (uintptr_t)GetModuleHandle(NULL);
      const uintptr_t gameBase = GetGameBase();

      uintptr_t rosterBase = 0;
      if (!exe || gameBase <= 0x10000 ||
          !TryResolveOfficerRosterArrayBase(
              exe, &rosterBase) ||
          rosterBase <= 0x10000) {
        AddLog(u8"[관계DBG] 기본 주소 확보 실패: exe=0x%llX gameBase=0x%llX roster=0x%llX",
               (unsigned long long)exe,
               (unsigned long long)gameBase,
               (unsigned long long)rosterBase);
        return;
      }

      AddLog(u8"[관계DBG] ===== %s(ID %u) 관계 탐색 시작 =====",
             BuildOfficerName(selected.id).c_str(),
             (unsigned int)selected.id);
      AddLog(u8"[관계DBG] Officer=0x%llX / Roster=0x%llX / stride=0x3D0",
             (unsigned long long)selected.officerBase,
             (unsigned long long)rosterBase);

      uintptr_t synerBase = 0;
      uintptr_t synerFieldOffset = 0;
      uintptr_t synerPattern = 0;
      if (TryResolveSynergeticBaseFromLegacyCt(
              exe, gameBase, rosterBase,
              &synerBase, &synerFieldOffset,
              &synerPattern)) {
        AddLog(u8"[관계DBG] 숙명 테이블 후보 확인: gameBase+0x%llX -> base 0x%llX / AOB 0x%llX",
               (unsigned long long)synerFieldOffset,
               (unsigned long long)synerBase,
               (unsigned long long)synerPattern);

        int foundCount = 0;
        for (int i = 0; i < 5000; ++i) {
          const uintptr_t slot =
              synerBase + (uintptr_t)i * 0x20;
          uintptr_t p1 = 0, p2 = 0;
          uint8_t relation = 0, occurred = 0;
          if (!SafeReadPtrAllowZero(slot + 0x08, &p1) ||
              !SafeReadPtrAllowZero(slot + 0x10, &p2) ||
              !SafeRead8(slot + 0x18, &relation) ||
              !SafeRead8(slot + 0x19, &occurred))
            break;
          if ((relation != 1 && relation != 2) ||
              (p1 != selected.officerBase &&
               p2 != selected.officerBase))
            continue;

          const uintptr_t other =
              p1 == selected.officerBase ? p2 : p1;
          uint16_t otherId = 0;
          if (!ReadRelationshipOfficerId(
                  other, rosterBase, &otherId))
            continue;

          AddLog(u8"[관계DBG][숙명] 슬롯 %d / %s / 상대 %s(ID %u) / 발생값 0x%02X",
                 i + 1,
                 GetLegacySynergeticName(relation),
                 BuildOfficerName(otherId).c_str(),
                 (unsigned int)otherId,
                 (unsigned int)occurred);
          ++foundCount;
        }
        AddLog(u8"[관계DBG] 숙명 일치 %d건", foundCount);
      } else {
        AddLog(u8"[관계DBG] 숙명 테이블 후보를 찾지 못했습니다. 구 CT AOB/0x433210 근처 재탐색 필요");
      }

      uintptr_t relationBase = 0;
      uintptr_t relationFieldOffset = 0;
      if (TryResolveRelationshipBaseFromLegacyCt(
              gameBase, rosterBase, synerFieldOffset,
              &relationBase, &relationFieldOffset)) {
        AddLog(u8"[관계DBG] 관계 테이블 후보 확인: gameBase+0x%llX -> base 0x%llX",
               (unsigned long long)relationFieldOffset,
               (unsigned long long)relationBase);

        int foundCount = 0;
        for (int i = 0; i < 3000; ++i) {
          const uintptr_t slot =
              relationBase + (uintptr_t)i * 0x40;
          uint8_t relation = 0;
          if (!SafeRead8(slot + 0x08, &relation))
            break;
          if (relation < 1 || relation > 4)
            continue;

          uintptr_t members[5]{};
          bool readOk = true;
          for (int j = 0; j < 5; ++j) {
            if (!SafeReadPtrAllowZero(
                    slot + 0x10 + (uintptr_t)j * 8,
                    &members[j])) {
              readOk = false;
              break;
            }
          }
          if (!readOk)
            break;

          bool containsSelected = false;
          for (uintptr_t member : members) {
            if (member == selected.officerBase) {
              containsSelected = true;
              break;
            }
          }
          if (!containsSelected)
            continue;

          uintptr_t flag = 0;
          SafeReadPtrAllowZero(slot + 0x38, &flag);
          AddLog(u8"[관계DBG][관계] 슬롯 %d / %s / flag 0x%llX",
                 i + 1,
                 GetLegacyRelationshipName(relation),
                 (unsigned long long)flag);

          uint16_t loggedIds[5]{};
          int loggedCount = 0;
          for (int j = 0; j < 5; ++j) {
            const uintptr_t member = members[j];
            if (!member ||
                member == selected.officerBase)
              continue;

            uint16_t memberId = 0;
            if (!ReadRelationshipOfficerId(
                    member, rosterBase, &memberId))
              continue;

            bool duplicate = false;
            for (int k = 0; k < loggedCount; ++k) {
              if (loggedIds[k] == memberId) {
                duplicate = true;
                break;
              }
            }
            if (duplicate)
              continue;

            loggedIds[loggedCount++] = memberId;
            AddLog(u8"[관계DBG][관계]   +%02X 슬롯%d: %s(ID %u)",
                   0x10 + j * 8, j + 1,
                   BuildOfficerName(memberId).c_str(),
                   (unsigned int)memberId);
          }
          ++foundCount;
        }
        AddLog(u8"[관계DBG] 의형제/배우자/원수/호적수 일치 %d건",
               foundCount);
      } else {
        AddLog(u8"[관계DBG] 관계 테이블 후보를 찾지 못했습니다. 구 CT 상대거리(+0x%llX) 근처 재탐색 필요",
               (unsigned long long)LEGACY_RELATION_PTR_DELTA);
      }

      AddLog(u8"[관계DBG] ===== 관계 탐색 종료 =====");
    }


    static bool MoveSelectedOfficerToCity(uintptr_t p1,
                                          uintptr_t shiftedCityBase) {
      const CityOfficerRow *selected = FindSelectedCityOfficer();
      if (!selected || s_officerMoveTargetCity < 0 ||
          s_officerMoveTargetCity >= g_CityCount)
        return false;

      if (selected->status == 0xD8 || selected->status == 0xE8) {
        AddNotification(u8"무장 이동: 도독/태수는 도시 이동할 수 없습니다. 해당 도시에서 교체 기능을 사용해주세요.");
        AddLog(u8"[도시 무장] 이동 차단: ID %u / 신분 0x%02X (도독/태수는 도시 참조 포인터 보호를 위해 이동 금지)",
               (unsigned int)selected->id,
               (unsigned int)selected->status);
        return false;
      }

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
        int cityIndex = -1;
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
          corpsOptions.push_back({info.corpsPtr, info.corpsNo, idx});
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
        for (const auto &opt : corpsOptions) {
          if (opt.corpsPtr == s_officerCorpsFilter) {
            corpsFilterPreview =
                GetOfficerCorpsName(shiftedCityBase, opt.cityIndex);
            break;
          }
        }
      }

      ImGui::TextUnformatted(u8"군단");
      ImGui::SameLine(0.f, 6.f * sc);
      ImGui::SetNextItemWidth(135.f * sc);
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
          const std::string label =
              GetOfficerCorpsName(shiftedCityBase, opt.cityIndex);

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

        if (selected->status == 0xD8 || selected->status == 0xE8) {
          ImGui::SameLine(0.f, 10.f * sc);
          ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                             u8"[이동 불가]");
        }

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

      if (bShowDebug && selected) {
        ImGui::SameLine(0.f, 12.f * sc);
        if (ImGui::SmallButton(
                u8"관계 탐색 로그##OfficerRelationshipProbe")) {
          LogSelectedOfficerRelationshipProbe(*selected);
        }
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::TextUnformatted(
              u8"구 CT의 숙명(0x20) / 의형제·배우자·원수·호적수(0x40) 구조를 기준으로 PK 주소를 읽기 전용 탐색합니다.");
          ImGui::TextUnformatted(
              u8"메모리 쓰기는 하지 않으며 결과는 로그 창에 출력됩니다.");
          ImGui::EndTooltip();
        }
      }

      ImGui::SameLine(0.f, 24.f * sc);
      ImGui::TextUnformatted(u8"이동할 도시");
      ImGui::SameLine(0.f, 8.f * sc);

      auto buildMoveTargetLabel = [&](int cityIndex) -> std::string {
        if (cityIndex < 0 || cityIndex >= g_CityCount)
          return u8"도시 없음";

        const std::string corpsName =
            GetOfficerCorpsName(shiftedCityBase, cityIndex);
        const bool frontline =
            IsCityFrontlineForForce(shiftedCityBase, cityIndex,
                                    s_officerPlayerForce);
        return "[" + corpsName + "][" +
               std::string(frontline ? u8"전선" : u8"후방") + "] " +
               g_CityList[cityIndex].cityname;
      };

      const std::string targetName =
          buildMoveTargetLabel(s_officerMoveTargetCity);
      ImGui::SetNextItemWidth(195.f * sc);
      if (ImGui::BeginCombo("##OfficerMoveTarget", targetName.c_str())) {
        for (int idx : s_officerPlayerCities) {
          if (idx == s_officerCityIndex)
            continue;
          const std::string label = buildMoveTargetLabel(idx);
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
        const bool targetFrontline =
            IsCityFrontlineForForce(shiftedCityBase,
                                    s_officerMoveTargetCity,
                                    s_officerPlayerForce);
        ImGui::SameLine(0.f, 10.f * sc);
        ImGui::TextDisabled(u8"→ %s / %s",
                            targetCorps.c_str(),
                            targetFrontline ? u8"전선" : u8"후방");
      }

      ImGui::SameLine(0.f, 12.f * sc);
      const bool isFixedOffice =
          selected != nullptr &&
          (selected->status == 0xD8 || selected->status == 0xE8);
      const bool canMove =
          selected != nullptr && s_officerMoveTargetCity >= 0 &&
          !isFixedOffice;
      if (!canMove)
        ImGui::BeginDisabled();
      if (ImGui::Button(u8"선택도시로 이동##MoveCityOfficer",
                        ImVec2(145.f * sc, 0.f)))
        MoveSelectedOfficerToCity(p1, shiftedCityBase);
      if (!canMove)
        ImGui::EndDisabled();

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(u8"같은 세력의 도시로 무장을 이동합니다.");
        ImGui::TextUnformatted(u8"도독·태수는 이동할 수 없습니다.");
        ImGui::TextUnformatted(u8"다른 군단으로의 전속은 일반 무장만 가능합니다.");
        if (bShowDebug) {
          ImGui::Separator();
          ImGui::TextDisabled(
              u8"디버그: Officer +0x20 도시 포인터만 변경하며 목적 도시의 City +0x90 군단을 따릅니다.");
        }
        ImGui::EndTooltip();
      }

      DrawGovernorPanel(p1, shiftedCityBase, sc);
      DrawGovernorGeneralPanel(p1, shiftedCityBase, sc);
      DrawCorpsDeploymentRecommendation(p1, shiftedCityBase, sc);
      if (bShowDebug)
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

  void ResetCorpsAutoDeploymentSession() {
    DisarmCorpsAutoDeploymentSession(
        u8"게임/P1 리셋 감지", false);
    s_corpsDeploymentLastRelevantGameState = 0;
    ResetCorpsDeploymentTargetBaseline();
  }

  void RunYearlyRearSupport(uintptr_t p1) {
    static ULONGLONG s_lastPollMs = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - s_lastPollMs < 500)
      return;
    s_lastPollMs = now;

    RunCorpsDeploymentPhaseMonitor(p1);

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
