#include "pch.h"
#include "TraitEditorDiagnostics.h"

#include "OfficerRosterResolve.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <string>

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
  __try {
    out = *reinterpret_cast<uint16_t *>(address);
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    out = 0;
    return false;
  }
}

bool ReadPtr(uintptr_t address, uintptr_t &out) {
  __try {
    out = *reinterpret_cast<uintptr_t *>(address);
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    out = 0;
    return false;
  }
}

bool CanReadRoster(uintptr_t roster) {
  if (roster < 0x10000)
    return false;

  uint16_t firstId = 0;
  uint16_t lastId = 0;
  const uintptr_t lastOfficer =
      roster + static_cast<uintptr_t>(kOfficerCount - 1) * kOfficerStride;

  return ReadU16(roster + 0x08, firstId) &&
         ReadU16(lastOfficer + 0x08, lastId);
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

  if (!CanReadRoster(roster))
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

bool DumpCodeRange(uintptr_t exeBase, uintptr_t offset, size_t size, const char *fileName) {
  if (!exeBase || !fileName || size == 0)
    return false;

  std::vector<uint8_t> bytes(size);
  __try {
    std::memcpy(bytes.data(), reinterpret_cast<const void *>(exeBase + offset), size);
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }

  char cwd[MAX_PATH] = {};
  if (!GetCurrentDirectoryA(MAX_PATH, cwd))
    return false;

  std::string path = std::string(cwd) + "\\" + fileName;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out)
    return false;

  out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  out.close();

  AddLog(u8"[기재3 진단] 코드 덤프 저장: %s (SAN8RPK.exe+0x%llX, 0x%zX bytes)",
         path.c_str(),
         static_cast<unsigned long long>(offset),
         size);
  return true;
}

void DumpRelevantEditorCode() {
  const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"SAN8RPK.exe"));
  if (!exeBase)
    return;

  // 선택창의 기존 슬롯 읽기/선택 쓰기/남은 슬롯 삭제가 모두 포함된 범위.
  DumpCodeRange(exeBase, 0x12E2600, 0x600, "trait3_diag_12E2600.bin");

  // 진행 중 편집기 저장/초기화 훅 주변. 취소 복원 경로가 caller 쪽에 있는지도 함께 확인합니다.
  DumpCodeRange(exeBase, 0x12B6500, 0x800, "trait3_diag_12B6500.bin");
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
    DumpRelevantEditorCode();
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

  if (!CanReadRoster(g_rosterBase)) {
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
