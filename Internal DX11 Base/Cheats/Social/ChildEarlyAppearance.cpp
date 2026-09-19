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
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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

  // 관계 테이블 진단 결과
  bool relationFound = false;
  uintptr_t relationAddr = 0;
  uint8_t relationFlag = 0;
  uint32_t relationSlot = 0;
};

static std::unordered_map<uint16_t, ChildEntry> g_children;

static std::atomic<bool> g_childRelationScanning{false};
static std::atomic<float> g_childRelationScanProgress{0.0f};
static std::mutex g_childRelationResultMutex;

struct ChildRelationResult {
  uint16_t childId = 0;
  uintptr_t relationAddr = 0;
  uintptr_t targetAddr = 0;
  uint8_t flag = 0;
  uint32_t slot = 0;
};

static std::vector<ChildRelationResult> g_childRelationResults;

static bool SafeRead8(uintptr_t addr, uint8_t* out) {
  __try {
    *out = *(uint8_t*)addr;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool SafeRead16(uintptr_t addr, uint16_t* out) {
  __try {
    *out = *(uint16_t*)addr;
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool SafeRead32(uintptr_t addr, uint32_t* out) {
  __try {
    *out = *(uint32_t*)addr;
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

static bool SafeReadMem(uintptr_t addr, void* out, size_t size) {
  __try {
    memcpy(out, (const void*)addr, size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

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

static void StartChildRelationScannerAsync() {
  if (g_childRelationScanning.load())
    return;

  if (g_savedHeroAddr <= 0x10000) {
    AddLog(u8"[ChildRelation] 주인공 주소가 유효하지 않습니다.");
    return;
  }

  std::unordered_set<uint16_t> childIds;
  for (const auto& kv : g_children)
    childIds.insert(kv.first);

  if (childIds.empty()) {
    AddLog(u8"[ChildRelation] 현재 감지된 자녀 ID가 없습니다. 정보 > 자녀를 한 번 연 뒤 다시 검색해 주세요.");
    return;
  }

  const uintptr_t heroAddr = g_savedHeroAddr;
  g_childRelationScanning = true;
  g_childRelationScanProgress = 0.0f;

  {
    std::lock_guard<std::mutex> lock(g_childRelationResultMutex);
    g_childRelationResults.clear();
  }

  std::thread([heroAddr, childIds = std::move(childIds)]() {
    MEMORY_BASIC_INFORMATION mbi{};
    std::vector<MEMORY_BASIC_INFORMATION> regions;
    uintptr_t addr = 0;
    unsigned long long totalSize = 0;

    while (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) {
      if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) &&
          (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
        regions.push_back(mbi);
        totalSize += mbi.RegionSize;
      }

      const uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
      if (next <= addr)
        break;
      addr = next;
    }

    const size_t bufferSize = 4096 * 16;
    std::vector<unsigned char> buffer(bufferSize + 8);
    std::unordered_set<uintptr_t> seenRelations;
    unsigned long long processedSize = 0;

    for (const auto& region : regions) {
      const uintptr_t start = (uintptr_t)region.BaseAddress;
      const uintptr_t end = start + region.RegionSize;
      uintptr_t curr = start;

      while (curr < end) {
        const size_t remaining = (size_t)(end - curr);
        const size_t toRead = (std::min)(remaining, bufferSize);
        if (toRead < sizeof(uintptr_t))
          break;

        if (SafeReadMem(curr, buffer.data(), toRead)) {
          for (size_t i = 0; i <= toRead - sizeof(uintptr_t); ++i) {
            uintptr_t candidateHero = 0;
            memcpy(&candidateHero, buffer.data() + i, sizeof(candidateHero));
            if (candidateHero != heroAddr)
              continue;

            const uintptr_t relationAddr = curr + i;
            if (relationAddr < 8 || seenRelations.count(relationAddr))
              continue;

            uintptr_t targetAddr = 0;
            uint16_t targetId = 0;
            if (!SafeReadPtr(relationAddr + 0x08, &targetAddr) ||
                targetAddr <= 0x10000 ||
                !SafeRead16(targetAddr + 0x08, &targetId) ||
                childIds.count(targetId) == 0) {
              continue;
            }

            uint8_t flag = 0;
            uint32_t slot = 0;
            if (!SafeRead8(relationAddr - 0x08, &flag))
              continue;
            SafeRead32(relationAddr + 0x28, &slot);

            seenRelations.insert(relationAddr);

            ChildRelationResult result;
            result.childId = targetId;
            result.relationAddr = relationAddr;
            result.targetAddr = targetAddr;
            result.flag = flag;
            result.slot = slot;

            {
              std::lock_guard<std::mutex> lock(g_childRelationResultMutex);
              g_childRelationResults.push_back(result);
            }

            AddLog(u8"[ChildRelation] ID %u | Relation=%p Target=%p Flag=%u(0x%02X) Slot=%u",
                   targetId, (void*)relationAddr, (void*)targetAddr,
                   (unsigned)flag, (unsigned)flag, slot);
          }
        }

        processedSize += toRead;
        curr += toRead;
        if (totalSize > 0)
          g_childRelationScanProgress = (float)processedSize / (float)totalSize;
      }
    }

    g_childRelationScanProgress = 1.0f;
    g_childRelationScanning = false;
    AddLog(u8"[ChildRelation] 자녀 관계 검색 완료.");
  }).detach();
}

static void ApplyChildRelationResults() {
  std::vector<ChildRelationResult> results;
  {
    std::lock_guard<std::mutex> lock(g_childRelationResultMutex);
    results = g_childRelationResults;
  }

  for (auto& kv : g_children) {
    kv.second.relationFound = false;
    kv.second.relationAddr = 0;
    kv.second.relationFlag = 0;
    kv.second.relationSlot = 0;
  }

  for (const auto& r : results) {
    auto it = g_children.find(r.childId);
    if (it == g_children.end())
      continue;

    ChildEntry& e = it->second;
    if (!e.relationFound) {
      e.relationFound = true;
      e.relationAddr = r.relationAddr;
      e.relationFlag = r.flag;
      e.relationSlot = r.slot;
    }
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

  ApplyChildRelationResults();
}

void DrawChildManagerWindow(float scale) {
  if (!bShowChildManagerWin)
    return;

  EnsureChildManagerCapture();
  RunChildManagerUpdate();

  ImGui::SetNextWindowSize(ImVec2(760.0f * scale, 390.0f * scale), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(u8"자녀 관리###ChildManager", &bShowChildManagerWin)) {
    ImGui::End();
    return;
  }

  ImGui::TextColored(ImVec4(1, 1, 0, 1),
                     u8"자녀 처리 루틴에서 감지된 자녀를 ID별로 표시합니다.");
  ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1),
                     u8"체크하거나 연수를 변경하면 그 시점 기준으로 한 번만 임관년도를 적용합니다.");
  ImGui::Separator();

  if (g_childRelationScanning.load()) {
    ImGui::TextColored(ImVec4(1, 1, 0, 1), u8"자녀 관계를 메모리에서 검색 중입니다...");
    ImGui::ProgressBar(g_childRelationScanProgress.load(), ImVec2(260.0f * scale, 0));
  } else {
    if (ImGui::Button(u8"자녀 관계 검색", ImVec2(130.0f * scale, 0))) {
      StartChildRelationScannerAsync();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(u8"현재 감지된 자녀 ID와 주인공의 관계 레코드를 검색합니다.");
      ImGui::TextUnformatted(u8"자녀 관계 플래그를 확인하기 위한 진단 기능입니다.");
      ImGui::EndTooltip();
    }
  }

  ImGui::Separator();

  if (g_children.empty()) {
    ImGui::TextUnformatted(u8"아직 감지된 자녀가 없습니다.");
    ImGui::TextWrapped(u8"평정 진입/종료 또는 자녀 관련 처리가 발생하면 목록에 자동 추가됩니다.");
  } else if (ImGui::BeginTable("ChildManagerTable", 9,
                                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn(u8"적용", ImGuiTableColumnFlags_WidthFixed, 45.0f * scale);
    ImGui::TableSetupColumn(u8"자녀", ImGuiTableColumnFlags_WidthFixed, 105.0f * scale);
    ImGui::TableSetupColumn(u8"출생", ImGuiTableColumnFlags_WidthFixed, 55.0f * scale);
    ImGui::TableSetupColumn(u8"등장", ImGuiTableColumnFlags_WidthFixed, 55.0f * scale);
    ImGui::TableSetupColumn(u8"사망", ImGuiTableColumnFlags_WidthFixed, 55.0f * scale);
    ImGui::TableSetupColumn(u8"관계", ImGuiTableColumnFlags_WidthFixed, 55.0f * scale);
    ImGui::TableSetupColumn(u8"슬롯", ImGuiTableColumnFlags_WidthFixed, 55.0f * scale);
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
      if (e.relationFound)
        ImGui::Text("%u", (unsigned)e.relationFlag);
      else
        ImGui::TextUnformatted("-");

      ImGui::TableNextColumn();
      if (e.relationFound)
        ImGui::Text("%u", e.relationSlot);
      else
        ImGui::TextUnformatted("-");

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
