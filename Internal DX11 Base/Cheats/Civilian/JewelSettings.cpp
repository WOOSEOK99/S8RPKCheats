#include "../../pch.h"
#include "JewelSettings.h"

#include "../../Cheats.h"
#include "../../Config.h"
#include "../../Framework/imgui.h"
#include "../../Hooking/MinHook.h"
#include "../../showlog.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace DX11Base {
namespace {

// version.dll의 정의 테이블(185개)을 그대로 비트 마스크로 변환한 값.
// 개방 비트는 raw jewel ID 자체를 bit index로 사용합니다.
constexpr std::array<uint8_t, 34> kDefinedJewelOpenMask = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFC, 0xFF, 0xFF, 0xBF, 0xFF, 0xFF, 0xF7, 0xFF,
    0xFF, 0xFE, 0xFF, 0xDF, 0xFF, 0xFF, 0xFB, 0xFF,
    0x7F, 0xFF, 0xFF, 0xEF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x03, 0x00,
};

// "강제" 판정 함수의 ID는 raw jewel ID - 0x40이며, 내부 비트는 (ID-1)을 사용합니다.
// 아래 마스크 역시 같은 185개 정의만 포함합니다.
constexpr std::array<uint8_t, 64> kDefinedSecondaryJewelMask = {
    0xFE, 0xFF, 0xFF, 0xDF, 0xFF, 0xFF, 0xFB, 0xFF,
    0x7F, 0xFF, 0xFF, 0xEF, 0xFF, 0xFF, 0xFD, 0xFF,
    0xBF, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xFF, 0xFF,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr uintptr_t kJewelOpenBitmapOffset = 0x71E0;
constexpr uintptr_t kSecondaryJewelJudgeOffset = 0x17ABBF0;
constexpr uintptr_t kSecondaryJewelObjectTableIndexBase = 0xB11DE;
constexpr uint16_t kRawJewelIdBias = 0x40;
constexpr uint16_t kMaxSecondaryJewelId = 500;
constexpr size_t kSecondaryForceWordCount = 8;

#include "JewelSettingsDefinitions.inc"

constexpr std::array<JewelCategory, 11> kJewelCategoryOrder = {
    JewelCategory::Common,
    JewelCategory::Ronin,
    JewelCategory::Leader,
    JewelCategory::Normal,
    JewelCategory::Strategist,
    JewelCategory::Governor,
    JewelCategory::Viceroy,
    JewelCategory::Ruler,
    JewelCategory::Evil,
    JewelCategory::Warrior,
    JewelCategory::Scholar,
};

using SecondaryJewelJudgeFn = bool(__fastcall *)(uint16_t jewelId);

std::atomic_bool g_allJewelsOpenPreferred{false};
std::array<std::atomic<uint64_t>, kSecondaryForceWordCount> g_secondaryForceMask{};
SecondaryJewelJudgeFn g_originalSecondaryJewelJudge = nullptr;
bool g_secondaryJewelHookInstalled = false;

// 0 = 미확인, 1 = 런타임 데이터 없음, 2 = 런타임 데이터 존재.
// 원 version.dll도 ID별 캐시를 사용해 hot path에서 반복 포인터 검증을 피합니다.
std::array<uint8_t, kMaxSecondaryJewelId + 1> g_secondaryRuntimeCache{};
uintptr_t g_secondaryRuntimeCacheBase = 0;

bool g_showJewelSettingsWindow = false;
std::array<uint8_t, 34> g_jewelOpenSnapshot{};
bool g_jewelOpenSnapshotValid = false;
ULONGLONG g_jewelOpenSnapshotMs = 0;
ImGuiTextFilter g_jewelNameFilter;

const char *GetJewelCategoryName(JewelCategory category) {
  switch (category) {
  case JewelCategory::Common: return u8"공통";
  case JewelCategory::Ronin: return u8"재야";
  case JewelCategory::Leader: return u8"두령";
  case JewelCategory::Normal: return u8"일반";
  case JewelCategory::Strategist: return u8"군사";
  case JewelCategory::Governor: return u8"태수";
  case JewelCategory::Viceroy: return u8"도독";
  case JewelCategory::Ruler: return u8"군주";
  case JewelCategory::Evil: return u8"악한";
  case JewelCategory::Warrior: return u8"무인";
  case JewelCategory::Scholar: return u8"문인";
  }
  return u8"기타";
}

bool AreAllJewelsOpenAtBase(uintptr_t gameBase) {
  if (!gameBase)
    return false;

  const uintptr_t bitmapAddress = gameBase + kJewelOpenBitmapOffset;
  if (!IsValidPtr(bitmapAddress, kDefinedJewelOpenMask.size()))
    return false;

  const auto *bitmap = reinterpret_cast<const uint8_t *>(bitmapAddress);
  for (size_t i = 0; i < kDefinedJewelOpenMask.size(); ++i) {
    const uint8_t mask = kDefinedJewelOpenMask[i];
    if (mask != 0 && (bitmap[i] & mask) != mask)
      return false;
  }
  return true;
}

bool ApplyAllJewelsOpenAtBase(uintptr_t gameBase, bool enable) {
  if (!gameBase)
    return false;

  const uintptr_t bitmapAddress = gameBase + kJewelOpenBitmapOffset;
  if (!IsValidPtr(bitmapAddress, kDefinedJewelOpenMask.size()))
    return false;

  auto *bitmap = reinterpret_cast<uint8_t *>(bitmapAddress);
  __try {
    for (size_t i = 0; i < kDefinedJewelOpenMask.size(); ++i) {
      if (enable)
        bitmap[i] = static_cast<uint8_t>(bitmap[i] | kDefinedJewelOpenMask[i]);
      else
        bitmap[i] = static_cast<uint8_t>(bitmap[i] & ~kDefinedJewelOpenMask[i]);
    }
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

void ResetSecondaryJewelRuntimeCache() {
  g_secondaryRuntimeCache.fill(0);
  g_secondaryRuntimeCacheBase = 0;
}

bool IsDefinedSecondaryJewelId(uint16_t jewelId) {
  if (jewelId < 1 || jewelId > kMaxSecondaryJewelId)
    return false;

  const uint32_t bitIndex = static_cast<uint32_t>(jewelId - 1);
  const uint32_t byteIndex = bitIndex >> 3;
  const uint8_t bitMask = static_cast<uint8_t>(1u << (bitIndex & 7));
  return (kDefinedSecondaryJewelMask[byteIndex] & bitMask) != 0;
}

bool IsSecondaryJewelForceEnabledFast(uint16_t jewelId) {
  if (jewelId > kMaxSecondaryJewelId)
    return false;

  const size_t wordIndex = static_cast<size_t>(jewelId) >> 6;
  const uint64_t bitMask = 1ull << (jewelId & 63);
  return (g_secondaryForceMask[wordIndex].load(std::memory_order_relaxed) & bitMask) != 0;
}

void StoreSecondaryJewelForceBit(uint16_t jewelId, bool enable) {
  const size_t wordIndex = static_cast<size_t>(jewelId) >> 6;
  const uint64_t bitMask = 1ull << (jewelId & 63);
  if (enable)
    g_secondaryForceMask[wordIndex].fetch_or(bitMask, std::memory_order_relaxed);
  else
    g_secondaryForceMask[wordIndex].fetch_and(~bitMask, std::memory_order_relaxed);
}

bool IsJewelOpenFast(uintptr_t gameBase, uint16_t rawJewelId) {
  const uint32_t byteIndex = static_cast<uint32_t>(rawJewelId) >> 3;
  if (byteIndex >= kDefinedJewelOpenMask.size())
    return false;

  const uintptr_t address = gameBase + kJewelOpenBitmapOffset + byteIndex;
  const uint8_t bitMask = static_cast<uint8_t>(1u << (rawJewelId & 7));

  __try {
    return ((*reinterpret_cast<const uint8_t *>(address)) & bitMask) != 0;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool ProbeSecondaryJewelRuntimeData(uintptr_t gameBase, uint16_t jewelId) {
  const uintptr_t slot =
      gameBase + sizeof(uintptr_t) *
                     (static_cast<uintptr_t>(jewelId) + kSecondaryJewelObjectTableIndexBase);

  __try {
    const uintptr_t first = *reinterpret_cast<const uintptr_t *>(slot);
    if (!first)
      return false;

    const uintptr_t second = *reinterpret_cast<const uintptr_t *>(first);
    return second != 0;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool HasSecondaryJewelRuntimeDataCached(uintptr_t gameBase, uint16_t jewelId) {
  if (jewelId > kMaxSecondaryJewelId)
    return false;

  // 세이브/시나리오 전환 등으로 gameBase가 바뀌면 캐시를 다시 계산합니다.
  if (g_secondaryRuntimeCacheBase != gameBase) {
    g_secondaryRuntimeCache.fill(0);
    g_secondaryRuntimeCacheBase = gameBase;
  }

  uint8_t &cached = g_secondaryRuntimeCache[jewelId];
  if (cached == 0)
    cached = ProbeSecondaryJewelRuntimeData(gameBase, jewelId) ? 2 : 1;

  return cached == 2;
}

bool __fastcall HookSecondaryJewelJudge(uint16_t jewelId) {
  // 선택된 보주만 강제 경로로 검사합니다. 개방/런타임 존재 조건은 기존 version.dll 재현 로직을 유지합니다.
  if (IsSecondaryJewelForceEnabledFast(jewelId) &&
      IsDefinedSecondaryJewelId(jewelId)) {
    const uintptr_t gameBase = GetGameBaseFast();
    if (gameBase) {
      const uint16_t rawJewelId =
          static_cast<uint16_t>(jewelId + kRawJewelIdBias);

      if (IsJewelOpenFast(gameBase, rawJewelId) &&
          HasSecondaryJewelRuntimeDataCached(gameBase, jewelId)) {
        return true;
      }
    }
  }

  return g_originalSecondaryJewelJudge
             ? g_originalSecondaryJewelJudge(jewelId)
             : false;
}

bool EnsureSecondaryJewelHook() {
  if (g_secondaryJewelHookInstalled)
    return true;

  HMODULE exe = GetModuleHandleW(L"SAN8RPK.exe");
  if (!exe)
    exe = GetModuleHandleW(nullptr);
  if (!exe) {
    AddLog(u8"[보주] SAN8RPK.exe 모듈을 찾지 못했습니다.");
    return false;
  }

  const uintptr_t target =
      reinterpret_cast<uintptr_t>(exe) + kSecondaryJewelJudgeOffset;
  constexpr uint8_t kExpectedEntry[5] = {0x48, 0x89, 0x5C, 0x24, 0x08};

  if (!IsValidPtr(target, sizeof(kExpectedEntry)) ||
      std::memcmp(reinterpret_cast<const void *>(target), kExpectedEntry,
                  sizeof(kExpectedEntry)) != 0) {
    AddLog(u8"[보주] 보조 보주 판정 함수 바이트가 예상과 다릅니다: SAN8RPK.exe+0x17ABBF0");
    return false;
  }

  const MH_STATUS createStatus =
      MH_CreateHook(reinterpret_cast<LPVOID>(target),
                    reinterpret_cast<LPVOID>(&HookSecondaryJewelJudge),
                    reinterpret_cast<LPVOID *>(&g_originalSecondaryJewelJudge));
  if (createStatus != MH_OK) {
    AddLog(u8"[보주] 보조 보주 훅 생성 실패: MH_STATUS=%d",
           static_cast<int>(createStatus));
    return false;
  }

  const MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(target));
  if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    g_originalSecondaryJewelJudge = nullptr;
    AddLog(u8"[보주] 보조 보주 훅 활성화 실패: MH_STATUS=%d",
           static_cast<int>(enableStatus));
    return false;
  }

  g_secondaryJewelHookInstalled = true;
  AddLog(u8"[보주] 보조 보주 판정 훅 적용: SAN8RPK.exe+0x17ABBF0");
  return true;
}

bool SetSecondaryJewelForceEnabled(uint16_t jewelId, bool enable) {
  if (!IsDefinedSecondaryJewelId(jewelId))
    return false;

  if (enable && !EnsureSecondaryJewelHook())
    return false;

  StoreSecondaryJewelForceBit(jewelId, enable);
  if (enable)
    ResetSecondaryJewelRuntimeCache();
  return true;
}

bool SetSecondaryJewelCategoryEnabled(JewelCategory category, bool enable) {
  if (enable && !EnsureSecondaryJewelHook())
    return false;

  bool changed = false;
  for (const auto &definition : kJewelDefinitions) {
    if (definition.category != category)
      continue;
    StoreSecondaryJewelForceBit(definition.secondaryId, enable);
    changed = true;
  }
  if (enable && changed)
    ResetSecondaryJewelRuntimeCache();
  return changed;
}

size_t GetSecondaryJewelForceEnabledCount() {
  size_t count = 0;
  for (const auto &definition : kJewelDefinitions) {
    if (IsSecondaryJewelForceEnabledFast(definition.secondaryId))
      ++count;
  }
  return count;
}

size_t GetSecondaryJewelCategoryCount(JewelCategory category, bool selectedOnly) {
  size_t count = 0;
  for (const auto &definition : kJewelDefinitions) {
    if (definition.category != category)
      continue;
    if (!selectedOnly || IsSecondaryJewelForceEnabledFast(definition.secondaryId))
      ++count;
  }
  return count;
}

void RefreshJewelOpenSnapshot() {
  const ULONGLONG now = GetTickCount64();
  if (now - g_jewelOpenSnapshotMs < 250ull)
    return;
  g_jewelOpenSnapshotMs = now;
  g_jewelOpenSnapshotValid = false;

  const uintptr_t gameBase = GetGameBaseFast();
  if (!gameBase)
    return;

  const uintptr_t bitmapAddress = gameBase + kJewelOpenBitmapOffset;
  if (!IsValidPtr(bitmapAddress, g_jewelOpenSnapshot.size()))
    return;

  __try {
    std::memcpy(g_jewelOpenSnapshot.data(),
                reinterpret_cast<const void *>(bitmapAddress),
                g_jewelOpenSnapshot.size());
    g_jewelOpenSnapshotValid = true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    g_jewelOpenSnapshotValid = false;
  }
}

int GetJewelOpenStateFromSnapshot(uint16_t rawJewelId) {
  if (!g_jewelOpenSnapshotValid)
    return -1;
  const size_t byteIndex = static_cast<size_t>(rawJewelId) >> 3;
  if (byteIndex >= g_jewelOpenSnapshot.size())
    return -1;
  const uint8_t bitMask = static_cast<uint8_t>(1u << (rawJewelId & 7));
  return (g_jewelOpenSnapshot[byteIndex] & bitMask) != 0 ? 1 : 0;
}

int HexNibble(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

} // namespace

bool SetAllJewelsOpen(bool enable) {
  // 실제 비트맵과 별개로 사용자가 원하는 ON/OFF 상태를 보존합니다.
  // ON 상태는 저장게임 로드 후 비트맵이 다시 덮여도 TickJewelSettings()가 재적용합니다.
  g_allJewelsOpenPreferred.store(enable, std::memory_order_relaxed);

  const uintptr_t gameBase = GetGameBase();
  if (!gameBase) {
    AddLog(u8"[보주] 게임 기준 주소를 찾지 못해 전체 개방을 즉시 변경하지 못했습니다.");
    return false;
  }

  if (!ApplyAllJewelsOpenAtBase(gameBase, enable)) {
    AddLog(u8"[보주] 개방 비트맵 주소가 유효하지 않습니다: %p",
           reinterpret_cast<void *>(gameBase + kJewelOpenBitmapOffset));
    return false;
  }

  if (enable)
    ResetSecondaryJewelRuntimeCache();

  AddLog(enable ? u8"[보주] 정의된 보주 185개 전체 개방 ON"
                : u8"[보주] 정의된 보주 185개 전체 개방 OFF");
  return true;
}

bool AreAllJewelsOpen() {
  return AreAllJewelsOpenAtBase(GetGameBase());
}

bool IsAllJewelsOpenPreferred() {
  return g_allJewelsOpenPreferred.load(std::memory_order_relaxed);
}

void SetAllJewelsOpenPreference(bool enable) {
  g_allJewelsOpenPreferred.store(enable, std::memory_order_relaxed);

  // 설정 로드시 OFF는 세이브에 원래 존재하는 개방 상태를 지우면 안 됩니다.
  // ON만 즉시 시도하고, 실패해도 TickJewelSettings()가 이후 재시도합니다.
  if (!enable)
    return;

  const uintptr_t gameBase = GetGameBase();
  if (gameBase && ApplyAllJewelsOpenAtBase(gameBase, true))
    ResetSecondaryJewelRuntimeCache();
}

void TickJewelSettings() {
  if (!g_allJewelsOpenPreferred.load(std::memory_order_relaxed))
    return;

  static ULONGLONG s_lastCheckMs = 0;
  const ULONGLONG now = GetTickCount64();
  if (now - s_lastCheckMs < 250ull)
    return;
  s_lastCheckMs = now;

  const uintptr_t gameBase = GetGameBaseFast();
  if (!gameBase || AreAllJewelsOpenAtBase(gameBase))
    return;

  // 저장게임/시나리오 로드가 +0x71E0 비트맵을 세이브 값으로 덮어쓴 경우 자동 복구.
  if (ApplyAllJewelsOpenAtBase(gameBase, true)) {
    // 같은 gameBase 안에서 세이브만 교체될 수도 있으므로 보조 보주 런타임 캐시도 무효화합니다.
    ResetSecondaryJewelRuntimeCache();
    AddLog(u8"[보주] 저장게임 로드 후 보주 전체 개방 자동 재적용");
  }
}

bool SetAllSecondaryJewelsEnabled(bool enable) {
  if (enable && !EnsureSecondaryJewelHook())
    return false;

  for (auto &word : g_secondaryForceMask)
    word.store(0, std::memory_order_relaxed);

  if (enable) {
    for (const auto &definition : kJewelDefinitions)
      StoreSecondaryJewelForceBit(definition.secondaryId, true);
    ResetSecondaryJewelRuntimeCache();
  }

  AddLog(enable ? u8"[보주] 보조 보주 전체 사용 ON"
                : u8"[보주] 보조 보주 전체 사용 OFF");
  return true;
}

bool IsAllSecondaryJewelsEnabled() {
  for (const auto &definition : kJewelDefinitions) {
    if (!IsSecondaryJewelForceEnabledFast(definition.secondaryId))
      return false;
  }
  return true;
}

std::string GetSecondaryJewelForceMaskHex() {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string result;
  result.reserve(kSecondaryForceWordCount * 16);
  for (const auto &word : g_secondaryForceMask) {
    const uint64_t value = word.load(std::memory_order_relaxed);
    for (int shift = 60; shift >= 0; shift -= 4)
      result.push_back(kHex[(value >> shift) & 0x0F]);
  }
  return result;
}

bool LoadSecondaryJewelForceMaskHex(const std::string &maskHex) {
  if (maskHex.size() != kSecondaryForceWordCount * 16)
    return false;

  std::array<uint64_t, kSecondaryForceWordCount> parsed{};
  for (size_t wordIndex = 0; wordIndex < kSecondaryForceWordCount; ++wordIndex) {
    uint64_t value = 0;
    for (size_t i = 0; i < 16; ++i) {
      const int nibble = HexNibble(maskHex[wordIndex * 16 + i]);
      if (nibble < 0)
        return false;
      value = (value << 4) | static_cast<uint64_t>(nibble);
    }
    parsed[wordIndex] = value;
  }

  std::array<uint64_t, kSecondaryForceWordCount> sanitized{};
  bool anyEnabled = false;
  for (const auto &definition : kJewelDefinitions) {
    const size_t wordIndex = static_cast<size_t>(definition.secondaryId) >> 6;
    const uint64_t bitMask = 1ull << (definition.secondaryId & 63);
    if ((parsed[wordIndex] & bitMask) != 0) {
      sanitized[wordIndex] |= bitMask;
      anyEnabled = true;
    }
  }

  if (anyEnabled && !EnsureSecondaryJewelHook())
    return false;

  for (size_t i = 0; i < kSecondaryForceWordCount; ++i)
    g_secondaryForceMask[i].store(sanitized[i], std::memory_order_relaxed);

  if (anyEnabled)
    ResetSecondaryJewelRuntimeCache();
  return true;
}

void OpenJewelSettingsWindow() {
  g_showJewelSettingsWindow = true;
}

void DrawJewelSettingsWindow(float scale) {
  if (!g_showJewelSettingsWindow)
    return;

  RefreshJewelOpenSnapshot();

  ImGui::SetNextWindowSize(ImVec2(720.0f * scale, 680.0f * scale),
                           ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(u8"보조 보주 강제 사용 설정###SecondaryJewelForceSettings",
                    &g_showJewelSettingsWindow)) {
    ImGui::End();
    return;
  }

  const size_t selectedCount = GetSecondaryJewelForceEnabledCount();
  ImGui::Text(u8"선택: %zu / %zu", selectedCount, kJewelDefinitions.size());
  ImGui::SameLine();
  if (ImGui::Button(u8"전체 선택##SecondaryJewelAllOn")) {
    if (SetAllSecondaryJewelsEnabled(true))
      SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button(u8"전체 해제##SecondaryJewelAllOff")) {
    if (SetAllSecondaryJewelsEnabled(false))
      SaveConfig();
  }

  g_jewelNameFilter.Draw(u8"이름 검색", 260.0f * scale);
  ImGui::TextDisabled(u8"강제 대상으로 미리 선택해도 잠긴 보주는 실제 개방된 뒤부터 함께 적용됩니다.");
  ImGui::Separator();

  for (size_t categoryIndex = 0; categoryIndex < kJewelCategoryOrder.size(); ++categoryIndex) {
    const JewelCategory category = kJewelCategoryOrder[categoryIndex];
    const size_t categoryTotal = GetSecondaryJewelCategoryCount(category, false);
    const size_t categorySelected = GetSecondaryJewelCategoryCount(category, true);

    char header[128]{};
    std::snprintf(header, sizeof(header), "%s  %zu / %zu###JewelCategory%zu",
                  GetJewelCategoryName(category), categorySelected, categoryTotal,
                  categoryIndex);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
    if (category == JewelCategory::Common)
      flags |= ImGuiTreeNodeFlags_DefaultOpen;
    if (!ImGui::CollapsingHeader(header, flags))
      continue;

    ImGui::PushID(static_cast<int>(categoryIndex));
    if (ImGui::SmallButton(u8"계통 전체 선택")) {
      if (SetSecondaryJewelCategoryEnabled(category, true))
        SaveConfig();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(u8"계통 전체 해제")) {
      if (SetSecondaryJewelCategoryEnabled(category, false))
        SaveConfig();
    }

    if (ImGui::BeginTable("SecondaryJewelTable", 3,
                          ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn(u8"개방 상태", ImGuiTableColumnFlags_WidthFixed,
                              90.0f * scale);
      ImGui::TableSetupColumn(u8"이름");
      ImGui::TableSetupColumn(u8"강제 사용", ImGuiTableColumnFlags_WidthFixed,
                              90.0f * scale);
      ImGui::TableHeadersRow();

      for (const auto &definition : kJewelDefinitions) {
        if (definition.category != category)
          continue;
        if (!g_jewelNameFilter.PassFilter(definition.name))
          continue;

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const int openState = GetJewelOpenStateFromSnapshot(definition.rawJewelId);
        if (openState > 0)
          ImGui::TextColored(ImVec4(0.25f, 0.9f, 0.35f, 1.0f), u8"개방됨");
        else if (openState == 0)
          ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.35f, 1.0f), u8"안됨");
        else
          ImGui::TextDisabled(u8"확인 불가");

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(definition.name);

        ImGui::TableNextColumn();
        bool enabled = IsSecondaryJewelForceEnabledFast(definition.secondaryId);
        ImGui::PushID(static_cast<int>(definition.secondaryId));
        if (ImGui::Checkbox("##force", &enabled)) {
          if (SetSecondaryJewelForceEnabled(definition.secondaryId, enabled))
            SaveConfig();
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    ImGui::PopID();
    ImGui::Spacing();
  }

  ImGui::End();
}

} // namespace DX11Base
