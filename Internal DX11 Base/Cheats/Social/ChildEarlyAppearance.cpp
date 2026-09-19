#include "../../pch.h"
#include "../../Cheats.h"
#include "ChildEarlyAppearance.h"
#include "../../MenuState.h"
#include "../../Cheats/System/MonthCapture.h"
#include "../../Cheats/Officer/OfficerData.h"
#include "../../Cheats/Officer/OfficerRosterResolve.h"
#include "../../showlog.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace DX11Base {
namespace {

struct ChildEntry {
  uint16_t id = 0;
  uintptr_t addr = 0;
  bool selected = false;
  int yearsLater = 3;
  uint16_t appearanceYear = 0;
  uint16_t birthYear = 0;
  uint16_t deathYear = 0;
  uint16_t appliedTargetYear = 0;
};

static std::unordered_map<uint16_t, ChildEntry> g_children;
static ULONGLONG g_lastChildScanMs = 0;
static uint16_t g_lastHeroId = 0;

static uintptr_t NormalizeOfficerPtr(uintptr_t p) {
  return p & 0x0000FFFFFFFFFFFFULL;
}

static bool SafeRead16(uintptr_t addr, uint16_t* out) {
  __try {
    *out = *(uint16_t*)addr;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool SafeReadPtr(uintptr_t addr, uintptr_t* out) {
  __try {
    *out = *(uintptr_t*)addr;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool RefreshChild(ChildEntry& e) {
  if (!e.addr || !IsValidPtr(e.addr, 0x38))
    return false;

  __try {
    const uint16_t id = *(uint16_t*)(e.addr + 0x08);
    if (id != e.id)
      return false;

    e.appearanceYear = *(uint16_t*)(e.addr + 0x32);
    e.birthYear = *(uint16_t*)(e.addr + 0x34);
    e.deathYear = *(uint16_t*)(e.addr + 0x36);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool ApplyChildSchedule(ChildEntry& e) {
  if (!e.addr || !IsValidPtr(e.addr, 0x38))
    return false;

  unsigned short currentYear = 0;
  if (!ReadScenarioYear(&currentYear) || currentYear < 171 || currentYear >= 270)
    return false;

  e.yearsLater = (std::max)(1, (std::min)(10, e.yearsLater));
  const uint16_t targetAppearance = (uint16_t)(currentYear + e.yearsLater);
  if (targetAppearance >= 270)
    return false;

  // 임관 시 15세가 되도록 출생년도도 함께 조정.
  const uint16_t targetBirth = (uint16_t)(targetAppearance - 15);

  __try {
    if (*(uint16_t*)(e.addr + 0x08) != e.id)
      return false;

    DWORD oldProt = 0, tmp = 0;
    if (!VirtualProtect((LPVOID)(e.addr + 0x32), 4, PAGE_READWRITE, &oldProt))
      return false;

    *(uint16_t*)(e.addr + 0x32) = targetAppearance;
    *(uint16_t*)(e.addr + 0x34) = targetBirth;

    VirtualProtect((LPVOID)(e.addr + 0x32), 4, oldProt, &tmp);

    e.appearanceYear = targetAppearance;
    e.birthYear = targetBirth;
    e.appliedTargetYear = targetAppearance;

    AddLog(u8"[ChildManager] ID %u 임관 예약: %u년 (출생 %u년, %d년 후)",
           e.id, targetAppearance, targetBirth, e.yearsLater);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool ResolveHeroAndRoster(uintptr_t& rosterBase, uintptr_t& heroMaster, uint16_t& heroId) {
  rosterBase = 0;
  heroMaster = 0;
  heroId = 0;

  uintptr_t gameBase = GetGameBase();
  if (!gameBase)
    return false;

  uintptr_t heroLive = 0;
  if (!SafeReadPtr(gameBase + 0xE0, &heroLive) || heroLive <= 0x10000)
    return false;

  if (!SafeRead16(heroLive + 0x08, &heroId) || heroId < 1 || heroId > 5102)
    return false;

  const uintptr_t exeBase = (uintptr_t)GetModuleHandle(nullptr);
  if (!exeBase || !TryResolveOfficerRosterArrayBase(exeBase, &rosterBase) || rosterBase <= 0x10000)
    return false;

  heroMaster = rosterBase + (uintptr_t)(heroId - 1) * 0x3D0;
  uint16_t verify = 0;
  if (!SafeRead16(heroMaster + 0x08, &verify) || verify != heroId)
    return false;

  return true;
}

static void ScanCurrentHeroChildren(bool forceLog) {
  uintptr_t rosterBase = 0;
  uintptr_t heroMaster = 0;
  uint16_t heroId = 0;
  if (!ResolveHeroAndRoster(rosterBase, heroMaster, heroId))
    return;

  // 주인공이 교체되면 이전 주인공 기준 목록은 즉시 폐기.
  if (g_lastHeroId != 0 && g_lastHeroId != heroId) {
    g_children.clear();
    AddLog(u8"[ChildManager] 주인공 변경 감지: %u -> %u, 자녀 목록 초기화",
           g_lastHeroId, heroId);
  }
  g_lastHeroId = heroId;

  std::unordered_map<uint16_t, ChildEntry> found;
  found.reserve(8);

  const uintptr_t heroNorm = NormalizeOfficerPtr(heroMaster);

  for (int i = 0; i < 5102; ++i) {
    const uintptr_t officerBase = rosterBase + (uintptr_t)i * 0x3D0;

    uint16_t id = 0;
    if (!SafeRead16(officerBase + 0x08, &id) || id < 1 || id > 5102)
      continue;

    uintptr_t dadPtr = 0;
    uintptr_t momPtr = 0;
    SafeReadPtr(officerBase + 0x48, &dadPtr);
    SafeReadPtr(officerBase + 0x50, &momPtr);

    const bool hasParentLink =
        (NormalizeOfficerPtr(dadPtr) == heroNorm) ||
        (NormalizeOfficerPtr(momPtr) == heroNorm);

    if (!hasParentLink)
      continue;

    // 일부 미사용/더미 무장 슬롯에도 혈연 포인터 값이 남아 있을 수 있습니다.
    // 실제 자녀 후보는 정상적인 생년/등장년/몰년 데이터를 가진 레코드만 허용합니다.
    uint16_t appearance = 0;
    uint16_t birth = 0;
    uint16_t death = 0;
    if (!SafeRead16(officerBase + 0x32, &appearance) ||
        !SafeRead16(officerBase + 0x34, &birth) ||
        !SafeRead16(officerBase + 0x36, &death))
      continue;

    const bool hasValidLifeYears =
        birth != 0 &&
        appearance != 0 &&
        death != 0 &&
        appearance >= birth &&
        death >= birth;

    if (!hasValidLifeYears)
      continue;

    ChildEntry e;
    auto oldIt = g_children.find(id);
    if (oldIt != g_children.end()) {
      e = oldIt->second; // 체크/연수/예약 상태 유지
    }

    e.id = id;
    e.addr = officerBase;
    RefreshChild(e);
    found[id] = e;
  }

  if (forceLog || found.size() != g_children.size()) {
    AddLog(u8"[ChildManager] 혈연 데이터 기준 자녀 검색 완료: 주인공 ID %u / %zu명",
           heroId, found.size());
    for (const auto& kv : found) {
      const ChildEntry& e = kv.second;
      AddLog(u8"[ChildManager] 자녀 ID %u | 출생 %u 등장 %u 사망 %u",
             e.id, e.birthYear, e.appearanceYear, e.deathYear);
    }
  }

  g_children.swap(found);
}

} // namespace

void EnsureChildManagerCapture() {
  // 더 이상 정보 > 자녀 화면 훅을 사용하지 않습니다.
  // 무장 마스터 배열의 부친(+0x48)/모친(+0x50) 혈연 포인터를 직접 사용합니다.
}

void RunChildManagerUpdate() {
  const ULONGLONG now = GetTickCount64();

  // 주인공이 바뀌었는지 빠르게 확인하기 위해 1초 간격으로 재검색.
  if (g_lastChildScanMs != 0 && (now - g_lastChildScanMs) < 1000)
    return;

  g_lastChildScanMs = now;
  ScanCurrentHeroChildren(false);

  for (auto& kv : g_children)
    RefreshChild(kv.second);
}

void DrawChildManagerWindow(float scale) {
  if (!bShowChildManagerWin)
    return;

  RunChildManagerUpdate();

  ImGui::SetNextWindowSize(ImVec2(650.0f * scale, 350.0f * scale), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(u8"자녀 관리###ChildManager", &bShowChildManagerWin)) {
    ImGui::End();
    return;
  }

  ImGui::TextColored(ImVec4(1, 1, 0, 1),
                     u8"무장 혈연 데이터의 부친/모친 포인터를 기준으로 현재 주인공의 자녀를 직접 표시합니다.");
  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1),
                     u8"체크하거나 연수를 변경하면 그 시점 기준으로 한 번만 임관년도를 적용합니다.");

  if (ImGui::Button(u8"목록 새로고침", ImVec2(110.0f * scale, 0))) {
    ScanCurrentHeroChildren(true);
  }

  ImGui::Separator();

  if (g_children.empty()) {
    ImGui::TextUnformatted(u8"현재 주인공의 자녀가 없습니다.");
  } else if (ImGui::BeginTable("ChildManagerTable", 7,
                                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn(u8"적용", ImGuiTableColumnFlags_WidthFixed, 45.0f * scale);
    ImGui::TableSetupColumn(u8"자녀", ImGuiTableColumnFlags_WidthFixed, 120.0f * scale);
    ImGui::TableSetupColumn(u8"출생", ImGuiTableColumnFlags_WidthFixed, 55.0f * scale);
    ImGui::TableSetupColumn(u8"등장", ImGuiTableColumnFlags_WidthFixed, 55.0f * scale);
    ImGui::TableSetupColumn(u8"사망", ImGuiTableColumnFlags_WidthFixed, 55.0f * scale);
    ImGui::TableSetupColumn(u8"몇 년 후", ImGuiTableColumnFlags_WidthFixed, 85.0f * scale);
    ImGui::TableSetupColumn(u8"예약", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    std::vector<uint16_t> ids;
    ids.reserve(g_children.size());
    for (const auto& kv : g_children)
      ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());

    for (uint16_t id : ids) {
      ChildEntry& e = g_children[id];
      ImGui::PushID((int)id);
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      bool selected = e.selected;
      if (ImGui::Checkbox("##select", &selected)) {
        e.selected = selected;
        if (e.selected)
          ApplyChildSchedule(e);
      }

      ImGui::TableNextColumn();
      auto nameIt = g_officerNames.find(id);
      if (nameIt != g_officerNames.end() && !nameIt->second.empty())
        ImGui::Text("%s (%u)", nameIt->second.c_str(), id);
      else
        ImGui::Text(u8"자녀 ID %u", id);

      ImGui::TableNextColumn();
      ImGui::Text("%u", e.birthYear);

      ImGui::TableNextColumn();
      ImGui::Text("%u", e.appearanceYear);

      ImGui::TableNextColumn();
      ImGui::Text("%u", e.deathYear);

      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(55.0f * scale);
      int years = e.yearsLater;
      if (ImGui::InputInt("##years", &years, 0, 0)) {
        e.yearsLater = (std::max)(1, (std::min)(10, years));
        if (e.selected)
          ApplyChildSchedule(e);
      }

      ImGui::TableNextColumn();
      if (e.appliedTargetYear)
        ImGui::Text(u8"%u년", e.appliedTargetYear);
      else
        ImGui::TextUnformatted(u8"-");

      ImGui::PopID();
    }

    ImGui::EndTable();
  }

  ImGui::Spacing();
  ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f),
                     u8"※ 선택된 자녀만 등장년도와 출생년도를 함께 조정하며 사망년도는 변경하지 않습니다.");
  ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f),
                     u8"※ 자녀 출생/임관/주인공 변경은 혈연 데이터를 다시 읽어 목록에 자동 반영합니다.");

  ImGui::End();
}

} // namespace DX11Base
