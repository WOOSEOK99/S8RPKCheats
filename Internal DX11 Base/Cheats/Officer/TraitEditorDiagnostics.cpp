#include "pch.h"
#include "TraitEditorDiagnostics.h"

#include "OfficerRosterResolve.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"

#include <array>
#include <cstdint>

namespace DX11Base {
namespace {

constexpr int kOfficerCount = 5102;
constexpr int kTraitSlotCount = 3;
constexpr uintptr_t kOfficerStride = 0x3D0;
constexpr uintptr_t kTraitSlotBase = 0x88;
constexpr ULONGLONG kPollIntervalMs = 250;
constexpr int kMaxLogsPerPoll = 64;

struct TraitSlotState {
  uintptr_t ptr = 0;
  uint16_t traitId = 0;
};

static bool g_enabled = false;
static bool g_baselineReady = false;
static uintptr_t g_rosterBase = 0;
static ULONGLONG g_lastPollMs = 0;
static std::array<TraitSlotState, kOfficerCount * kTraitSlotCount> g_snapshot{};

bool ReadU16(uintptr_t address, uint16_t &out) {
  if (!IsValidPtr(address, sizeof(uint16_t))) {
    out = 0;
    return false;
  }
  out = *reinterpret_cast<uint16_t *>(address);
  return true;
}

bool ReadPtr(uintptr_t address, uintptr_t &out) {
  if (!IsValidPtr(address, sizeof(uintptr_t))) {
    out = 0;
    return false;
  }
  out = *reinterpret_cast<uintptr_t *>(address);
  return true;
}

uint16_t ReadTraitId(uintptr_t traitPtr) {
  if (traitPtr < 0x10000)
    return 0;

  uint16_t id = 0;
  if (!ReadU16(traitPtr + 0x08, id))
    return 0;
  return id;
}

bool ResolveRosterBase() {
  const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"SAN8RPK.exe"));
  if (!exeBase)
    return false;

  uintptr_t roster = 0;
  if (!TryResolveOfficerRosterArrayBase(exeBase, &roster) || roster < 0x10000)
    return false;

  if (!IsValidPtr(roster, kOfficerStride))
    return false;

  g_rosterBase = roster;
  return true;
}

TraitSlotState ReadSlot(uintptr_t officerBase, int slot) {
  TraitSlotState s{};
  ReadPtr(officerBase + kTraitSlotBase + static_cast<uintptr_t>(slot) * sizeof(uintptr_t), s.ptr);
  s.traitId = ReadTraitId(s.ptr);
  return s;
}

void CaptureBaseline() {
  if (!ResolveRosterBase())
    return;

  for (int i = 0; i < kOfficerCount; ++i) {
    const uintptr_t officerBase = g_rosterBase + static_cast<uintptr_t>(i) * kOfficerStride;
    for (int slot = 0; slot < kTraitSlotCount; ++slot) {
      g_snapshot[i * kTraitSlotCount + slot] = ReadSlot(officerBase, slot);
    }
  }

  g_baselineReady = true;
  AddLog(u8"[기재3 진단] 기준 스냅샷 완료. 이제 진행 중 원본 편집기에서 기재1→취소, 기재2→취소, 기재3→취소 순서로 테스트하세요.");
}

} // namespace

void SetInProgressTraitDiagnostics(bool enable) {
  if (g_enabled == enable)
    return;

  g_enabled = enable;
  g_baselineReady = false;
  g_rosterBase = 0;
  g_lastPollMs = 0;

  if (enable) {
    AddLog(u8"[기재3 진단] 시작. 실제 무장 5102명의 기재 슬롯(+88/+90/+98) 변화를 읽기 전용으로 감시합니다.");
    AddLog(u8"[기재3 진단] 중요: '변경' 로그가 확인 버튼을 누르기 전에 나오면 실제 무장 데이터가 즉시 바뀐 것입니다.");
  } else {
    AddLog(u8"[기재3 진단] 종료.");
  }
}

bool IsInProgressTraitDiagnosticsEnabled() {
  return g_enabled;
}

void TickInProgressTraitDiagnostics() {
  if (!g_enabled)
    return;

  const ULONGLONG now = GetTickCount64();
  if (g_lastPollMs != 0 && now - g_lastPollMs < kPollIntervalMs)
    return;
  g_lastPollMs = now;

  if (!g_baselineReady) {
    CaptureBaseline();
    return;
  }

  if (g_rosterBase < 0x10000 || !IsValidPtr(g_rosterBase, kOfficerStride)) {
    g_baselineReady = false;
    g_rosterBase = 0;
    AddLog(u8"[기재3 진단] 무장 배열 주소가 바뀌어 기준 스냅샷을 다시 잡습니다.");
    return;
  }

  int logCount = 0;
  int suppressed = 0;

  for (int i = 0; i < kOfficerCount; ++i) {
    const uintptr_t officerBase = g_rosterBase + static_cast<uintptr_t>(i) * kOfficerStride;

    uint16_t officerId = 0;
    ReadU16(officerBase + 0x08, officerId);

    for (int slot = 0; slot < kTraitSlotCount; ++slot) {
      const int index = i * kTraitSlotCount + slot;
      const TraitSlotState current = ReadSlot(officerBase, slot);
      const TraitSlotState previous = g_snapshot[index];

      if (current.ptr == previous.ptr && current.traitId == previous.traitId)
        continue;

      if (logCount < kMaxLogsPerPoll) {
        AddLog(
          u8"[기재3 진단] 실제무장 변경 ID:%u 슬롯%d(+0x%02X) 기재:%u→%u 포인터:%p→%p",
          static_cast<unsigned>(officerId),
          slot + 1,
          static_cast<unsigned>(kTraitSlotBase + slot * sizeof(uintptr_t)),
          static_cast<unsigned>(previous.traitId),
          static_cast<unsigned>(current.traitId),
          reinterpret_cast<void *>(previous.ptr),
          reinterpret_cast<void *>(current.ptr));
        ++logCount;
      } else {
        ++suppressed;
      }

      g_snapshot[index] = current;
    }
  }

  if (suppressed > 0) {
    AddLog(u8"[기재3 진단] 같은 주기에 변경 로그 %d건을 추가로 생략했습니다.", suppressed);
  }
}

} // namespace DX11Base
