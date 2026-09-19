#include "../../pch.h"
#include "ChildEarlyAppearance.h"
#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../MenuState.h"
#include "../../Cheats/System/MonthCapture.h"
#include "../../Cheats/Officer/OfficerData.h"
#include "../../showlog.h"

#include <psapi.h>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace DX11Base {
namespace {

static uintptr_t g_childHookAddr = 0;
static uintptr_t g_childCaveAddr = 0;
static uint8_t g_childOriginal[7] = {};
static bool g_childCaptureApplied = false;

static constexpr int kChildRingSize = 64;
static uintptr_t g_childAddrRing[kChildRingSize] = {};
static volatile LONG g_childWriteIndex = 0;
static int g_childReadIndex = 0;

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

static bool GetModuleRange(uintptr_t& begin, uintptr_t& end) {
  begin = (uintptr_t)GetModuleHandle(nullptr);
  if (!begin)
    return false;
  MODULEINFO mi{};
  if (!GetModuleInformation(GetCurrentProcess(), (HMODULE)begin, &mi, sizeof(mi)))
    return false;
  end = begin + mi.SizeOfImage;
  return end > begin;
}

static void Emit8(std::vector<uint8_t>& code, uint8_t v) {
  code.push_back(v);
}

static void Emit64(std::vector<uint8_t>& code, uintptr_t v) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
  code.insert(code.end(), p, p + sizeof(v));
}

static bool InstallCaptureCave(uintptr_t hookAddr) {
  g_childCaveAddr = AllocNear(hookAddr, 160);
  if (!g_childCaveAddr)
    return false;

  std::vector<uint8_t> code;
  code.reserve(128);

  // 원본: mov rax,[rbp+000001C0]
  code.insert(code.end(), g_childOriginal, g_childOriginal + sizeof(g_childOriginal));

  // 원본 mov은 FLAGS를 변경하지 않으므로 캡처 코드도 상태를 모두 보존.
  Emit8(code, 0x9C); // pushfq
  Emit8(code, 0x51); // push rcx
  Emit8(code, 0x52); // push rdx

  // next = (writeIndex + 1) & 63
  Emit8(code, 0x48); Emit8(code, 0xBA);
  Emit64(code, (uintptr_t)&g_childWriteIndex);
  Emit8(code, 0x8B); Emit8(code, 0x0A);             // mov ecx,[rdx]
  Emit8(code, 0xFF); Emit8(code, 0xC1);             // inc ecx
  Emit8(code, 0x83); Emit8(code, 0xE1); Emit8(code, 0x3F); // and ecx,63

  Emit8(code, 0x48); Emit8(code, 0xBA);
  Emit64(code, (uintptr_t)&g_childAddrRing[0]);
  Emit8(code, 0x48); Emit8(code, 0x89); Emit8(code, 0x04); Emit8(code, 0xCA);
  // mov [rdx+rcx*8],rax

  Emit8(code, 0x48); Emit8(code, 0xBA);
  Emit64(code, (uintptr_t)&g_childWriteIndex);
  Emit8(code, 0x89); Emit8(code, 0x0A);             // mov [rdx],ecx

  Emit8(code, 0x5A); // pop rdx
  Emit8(code, 0x59); // pop rcx
  Emit8(code, 0x9D); // popfq

  // absolute return jump
  Emit8(code, 0xFF); Emit8(code, 0x25);
  Emit8(code, 0x00); Emit8(code, 0x00); Emit8(code, 0x00); Emit8(code, 0x00);
  Emit64(code, hookAddr + sizeof(g_childOriginal));

  memcpy((void*)g_childCaveAddr, code.data(), code.size());
  FlushInstructionCache(GetCurrentProcess(), (LPCVOID)g_childCaveAddr, code.size());

  if (!ApplyJmp(hookAddr, g_childCaveAddr, sizeof(g_childOriginal))) {
    VirtualFree((LPVOID)g_childCaveAddr, 0, MEM_RELEASE);
    g_childCaveAddr = 0;
    return false;
  }
  return true;
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

} // namespace

void EnsureChildManagerCapture() {
  if (g_childCaptureApplied)
    return;

  uintptr_t begin = 0, end = 0;
  if (!GetModuleRange(begin, end))
    return;

  if (!g_childHookAddr) {
    g_childHookAddr = FindPattern(
        begin, end,
        "48 8B 85 C0 01 00 00 0F B7 58 34 E8 1D");
  }

  if (!g_childHookAddr) {
    AddLog(u8"[ChildManager] 자녀 처리 패턴을 찾지 못했습니다.");
    return;
  }

  memcpy(g_childOriginal, (const void*)g_childHookAddr, sizeof(g_childOriginal));
  if (!InstallCaptureCave(g_childHookAddr)) {
    AddLog(u8"[ChildManager] 자녀 목록 캡처 훅 설치 실패");
    return;
  }

  g_childReadIndex = (int)g_childWriteIndex;
  g_childCaptureApplied = true;
  AddLog(u8"[ChildManager] 자녀 목록 감시 시작");
}

void RunChildManagerUpdate() {
  if (!g_childCaptureApplied)
    return;

  const int writeIndex = (int)g_childWriteIndex;
  int guard = 0;
  while (g_childReadIndex != writeIndex && guard++ < kChildRingSize) {
    g_childReadIndex = (g_childReadIndex + 1) & (kChildRingSize - 1);
    const uintptr_t addr = g_childAddrRing[g_childReadIndex];

    if (!addr || !IsValidPtr(addr, 0x38))
      continue;

    uint16_t id = 0;
    uint16_t appearance = 0;
    uint16_t birth = 0;
    uint16_t death = 0;
    __try {
      id = *(uint16_t*)(addr + 0x08);
      appearance = *(uint16_t*)(addr + 0x32);
      birth = *(uint16_t*)(addr + 0x34);
      death = *(uint16_t*)(addr + 0x36);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      continue;
    }

    if (id < 1 || id > 5102)
      continue;

    auto it = g_children.find(id);
    if (it == g_children.end()) {
      ChildEntry e;
      e.id = id;
      e.addr = addr;
      e.appearanceYear = appearance;
      e.birthYear = birth;
      e.deathYear = death;
      g_children.emplace(id, e);
      AddLog(u8"[ChildManager] 자녀 감지: ID %u (등장 %u / 출생 %u / 사망 %u)",
             id, appearance, birth, death);
    } else {
      it->second.addr = addr;
      RefreshChild(it->second);
    }
  }

  for (auto& kv : g_children)
    RefreshChild(kv.second);
}

void DrawChildManagerWindow(float scale) {
  if (!bShowChildManagerWin)
    return;

  EnsureChildManagerCapture();
  RunChildManagerUpdate();

  ImGui::SetNextWindowSize(ImVec2(620.0f * scale, 330.0f * scale), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(u8"자녀 관리###ChildManager", &bShowChildManagerWin)) {
    ImGui::End();
    return;
  }

  ImGui::TextColored(ImVec4(1, 1, 0, 1),
                     u8"자녀 처리 루틴에서 감지된 자녀를 ID별로 표시합니다.");
  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1),
                     u8"체크하거나 연수를 변경하면 그 시점 기준으로 한 번만 임관년도를 적용합니다.");
  ImGui::Separator();

  if (g_children.empty()) {
    ImGui::TextUnformatted(u8"아직 감지된 자녀가 없습니다.");
    ImGui::TextWrapped(u8"평정 진입/종료 또는 자녀 관련 처리가 발생하면 목록에 자동 추가됩니다.");
  } else if (ImGui::BeginTable("ChildManagerTable", 7,
                                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn(u8"적용", ImGuiTableColumnFlags_WidthFixed, 45.0f * scale);
    ImGui::TableSetupColumn(u8"자녀", ImGuiTableColumnFlags_WidthFixed, 105.0f * scale);
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
                     u8"※ 새로 태어난 자녀는 자동 감지되지만 기본값은 미선택입니다.");

  ImGui::End();
}

} // namespace DX11Base
