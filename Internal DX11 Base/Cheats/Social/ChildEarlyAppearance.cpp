#include "../../pch.h"
#include "../../Cheats.h"
#include "ChildEarlyAppearance.h"
#include "PregnancyManager.h"
#include "../../MenuState.h"
#include "../../Cheats/System/MonthCapture.h"
#include "../../Cheats/Officer/OfficerData.h"
#include "../../Cheats/Officer/OfficerRosterResolve.h"
#include "../../showlog.h"
#include "../../debug.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace DX11Base {
namespace ChildManagerDetail {

uintptr_t NormalizeOfficerPtr(uintptr_t p) {
  return p & 0x0000FFFFFFFFFFFFULL;
}

bool SafeRead16(uintptr_t addr, uint16_t* out) {
  __try {
    *out = *(uint16_t*)addr;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool SafeReadPtr(uintptr_t addr, uintptr_t* out) {
  __try {
    *out = *(uintptr_t*)addr;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
bool ResolveHeroAndRoster(uintptr_t& rosterBase, uintptr_t& heroMaster, uint16_t& heroId) {
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

} // namespace ChildManagerDetail

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

  // 임관 전에 취소할 경우에만 원래 일정으로 되돌리기 위한 백업
  bool hasOriginalSchedule = false;
  uint16_t originalAppearanceYear = 0;
  uint16_t originalBirthYear = 0;
};

static std::unordered_map<uint16_t, ChildEntry> g_children;
static ULONGLONG g_lastChildScanMs = 0;
static uint16_t g_lastHeroId = 0;

using ChildManagerDetail::NormalizeOfficerPtr;
using ChildManagerDetail::ResolveHeroAndRoster;
using ChildManagerDetail::SafeRead16;
using ChildManagerDetail::SafeReadPtr;

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

static void FinalizeCompletedChildSchedule(ChildEntry& e, uint16_t currentYear) {
  if (!e.selected || !e.hasOriginalSchedule || e.appliedTargetYear == 0)
    return;
  if (currentYear < e.appliedTargetYear)
    return;

  const uint16_t completedYear = e.appliedTargetYear;

  // 조기 임관이 완료된 뒤에는 변경된 등장/출생년도가 곧 실제 이력이다.
  // 원본 일정 백업만 폐기하고 메모리 값은 절대 되돌리지 않는다.
  e.selected = false;
  e.appliedTargetYear = 0;
  e.hasOriginalSchedule = false;
  e.originalAppearanceYear = 0;
  e.originalBirthYear = 0;

  AddLog(u8"[ChildManager] ID %u 조기 임관 완료: %u년 / 조정된 출생·등장년도 유지 / 체크 자동 해제",
         e.id, completedYear);
}

static bool RestoreChildSchedule(ChildEntry& e) {
  if (!e.hasOriginalSchedule || !e.addr || !IsValidPtr(e.addr, 0x38))
    return false;

  unsigned short currentYear = 0;
  if (!ReadScenarioYear(&currentYear) || currentYear < 171 || currentYear >= 270)
    return false;

  // 이미 조기 임관 예정년도에 도달했다면 '예약 취소'가 아니라 '임관 완료'다.
  // 이 상태에서 생년/등장년을 원복하면 임관된 자녀의 나이만 어려지는 모순이 생긴다.
  if (e.appliedTargetYear != 0 && currentYear >= e.appliedTargetYear) {
    FinalizeCompletedChildSchedule(e, currentYear);
    return true;
  }

  __try {
    if (*(uint16_t*)(e.addr + 0x08) != e.id)
      return false;

    DWORD oldProt = 0, tmp = 0;
    if (!VirtualProtect((LPVOID)(e.addr + 0x32), 4, PAGE_READWRITE, &oldProt))
      return false;

    *(uint16_t*)(e.addr + 0x32) = e.originalAppearanceYear;
    *(uint16_t*)(e.addr + 0x34) = e.originalBirthYear;
    VirtualProtect((LPVOID)(e.addr + 0x32), 4, oldProt, &tmp);

    e.appearanceYear = e.originalAppearanceYear;
    e.birthYear = e.originalBirthYear;
    e.appliedTargetYear = 0;
    e.hasOriginalSchedule = false;
    e.originalAppearanceYear = 0;
    e.originalBirthYear = 0;

    AddLog(u8"[ChildManager] ID %u 임관 예약 취소: 등장 %u년 / 출생 %u년 복원",
           e.id, e.appearanceYear, e.birthYear);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool IsChildCommissioned(const ChildEntry& e, uint16_t currentYear) {
  return e.appearanceYear != 0 && currentYear >= e.appearanceYear;
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

  unsigned short currentYear = 0;
  const bool hasCurrentYear =
      ReadScenarioYear(&currentYear) && currentYear >= 171 && currentYear < 270;

  for (auto& kv : g_children) {
    ChildEntry& e = kv.second;
    RefreshChild(e);

    // 예정년도에 도달하면 조기 임관 작업은 끝난 것이므로 체크를 자동 해제한다.
    // 이때 조정된 출생/등장년도는 그대로 유지한다.
    if (hasCurrentYear)
      FinalizeCompletedChildSchedule(e, currentYear);
  }

  RunPregnancyManagerUpdate();
}

void DrawChildManagerWindow(float scale) {
  static bool s_childManagerWasOpen = false;

  if (!bShowChildManagerWin) {
    s_childManagerWasOpen = false;
    return;
  }

  // 창을 여는 순간에만 배우자 목록과 임신 3슬롯을 즉시 갱신합니다.
  // 검증된 직접 경로를 우선 사용하고, 실패할 때만 gameBase 주변을 경량 검색합니다.
  if (!s_childManagerWasOpen) {
    s_childManagerWasOpen = true;
    RefreshPregnancyManagerOnWindowOpen();
  }

  RunChildManagerUpdate();

  ImGui::SetNextWindowSize(ImVec2(650.0f * scale, 350.0f * scale), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(u8"자녀 관리###ChildManager", &bShowChildManagerWin)) {
    ImGui::End();
    return;
  }

  ImGui::TextColored(ImVec4(1, 1, 0, 1),
                     u8"현재 주인공의 자녀를 확인하고 자녀별 임관 시점을 설정합니다.");
  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1),
                     u8"자녀를 체크한 뒤 '몇 년 후'를 변경하면 해당 시점으로 임관 연도가 적용됩니다.");

  if (ImGui::Button(u8"목록 새로고침", ImVec2(110.0f * scale, 0))) {
    ScanCurrentHeroChildren(true);
  }

  ImGui::Separator();

  unsigned short currentYear = 0;
  const bool hasCurrentYear =
      ReadScenarioYear(&currentYear) && currentYear >= 171 && currentYear < 270;

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
    ImGui::TableSetupColumn(u8"임관예정일", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    std::vector<uint16_t> ids;
    ids.reserve(g_children.size());
    for (const auto& kv : g_children)
      ids.push_back(kv.first);

    // 이미 등장(임관)한 자녀를 먼저 보여주고, 같은 그룹 안에서는 ID 순으로 정렬한다.
    std::sort(ids.begin(), ids.end(),
              [&](uint16_t lhsId, uint16_t rhsId) {
                const ChildEntry& lhs = g_children.at(lhsId);
                const ChildEntry& rhs = g_children.at(rhsId);
                const bool lhsCommissioned =
                    hasCurrentYear && IsChildCommissioned(lhs, currentYear);
                const bool rhsCommissioned =
                    hasCurrentYear && IsChildCommissioned(rhs, currentYear);

                if (lhsCommissioned != rhsCommissioned)
                  return lhsCommissioned && !rhsCommissioned;
                return lhsId < rhsId;
              });

    for (uint16_t id : ids) {
      ChildEntry& e = g_children[id];
      const bool commissioned =
          hasCurrentYear && IsChildCommissioned(e, currentYear);

      ImGui::PushID((int)id);
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      bool selected = e.selected;
      if (commissioned)
        ImGui::BeginDisabled();

      if (ImGui::Checkbox("##select", &selected)) {
        if (selected) {
          if (!e.hasOriginalSchedule) {
            e.originalAppearanceYear = e.appearanceYear;
            e.originalBirthYear = e.birthYear;
            e.hasOriginalSchedule = true;
          }

          if (ApplyChildSchedule(e)) {
            e.selected = true;
          } else {
            e.selected = false;
            e.hasOriginalSchedule = false;
            e.originalAppearanceYear = 0;
            e.originalBirthYear = 0;
            AddLog(u8"[ChildManager] ID %u 임관 예약 적용 실패", e.id);
          }
        } else {
          if (RestoreChildSchedule(e)) {
            e.selected = false;
          } else {
            // 복원에 실패했다면 실제 메모리 예약은 남아 있을 수 있으므로 체크 상태를 유지합니다.
            e.selected = true;
            AddLog(u8"[ChildManager] ID %u 임관 예약 취소 실패", e.id);
          }
        }
      }

      if (commissioned)
        ImGui::EndDisabled();

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
      if (commissioned)
        ImGui::BeginDisabled();

      ImGui::SetNextItemWidth(55.0f * scale);
      int years = e.yearsLater;
      if (ImGui::InputInt("##years", &years, 0, 0)) {
        e.yearsLater = (std::max)(1, (std::min)(10, years));
        if (e.selected)
          ApplyChildSchedule(e);
      }

      if (commissioned)
        ImGui::EndDisabled();

      ImGui::TableNextColumn();
      if (commissioned) {
        ImGui::TextColored(
            ImVec4(0.45f, 1.0f, 0.55f, 1.0f),
            u8"임관 완료");
      } else if (e.appliedTargetYear) {
        ImGui::Text(u8"%u년", e.appliedTargetYear);
      } else {
        ImGui::TextUnformatted(u8"-");
      }

      ImGui::PopID();
    }

    ImGui::EndTable();
  }

  ImGui::Spacing();
  ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f),
                     u8"※ 이미 등장한 자녀는 임관 완료로 표시되며 조기 임관 설정을 다시 적용할 수 없습니다.");
  ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f),
                     u8"※ 임관 전 체크를 해제하면 원래 일정으로 복원되며, 임관 완료 후에는 조정된 나이가 유지됩니다.");
  ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f),
                     u8"※ 임관 예정년도에 도달하면 적용 체크는 자동으로 해제됩니다.");
  ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f),
                     u8"※ 자녀 출생/임관/주인공 변경은 혈연 데이터를 다시 읽어 목록에 자동 반영합니다.");

  ImGui::Separator();
  DrawPregnancyManagerSection(scale);

  ImGui::End();
}

} // namespace DX11Base