#include "TraitViewerWidths.h"
#include "../../showlog.h"
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace DX11Base {
namespace {
constexpr uintptr_t kUnitColumnIdOffset   = 0x2C72948; // qword == 0x99
constexpr uintptr_t kUnitWidthOffset      = 0x2C72958; // dword BC -> 84 -> A0
constexpr uintptr_t kStatusColumnIdOffset = 0x2C6F668; // qword == 0x05
constexpr uintptr_t kStatusWidthOffset    = 0x2C6F678; // dword 68 -> 4C

constexpr uint64_t kUnitColumnId   = 0x99;
constexpr uint64_t kStatusColumnId = 0x05;
constexpr uint32_t kUnitOriginal   = 0xBC;
constexpr uint32_t kUnitStage1     = 0x84;
constexpr uint32_t kUnitEnabled    = 0xA0;
constexpr uint32_t kStatusOriginal = 0x68;
constexpr uint32_t kStatusEnabled  = 0x4C;

bool g_applied = false;

bool SafeRead(const void* src, void* dst, size_t size) {
  __try {
    std::memcpy(dst, src, size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    std::memset(dst, 0, size);
    return false;
  }
}

bool WriteProtected(void* dst, const void* src, size_t size) {
  DWORD oldProtect = 0;
  if (!VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &oldProtect))
    return false;

  bool ok = false;
  __try {
    std::memcpy(dst, src, size);
    ok = true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    ok = false;
  }

  DWORD ignored = 0;
  VirtualProtect(dst, size, oldProtect, &ignored);
  if (ok)
    FlushInstructionCache(GetCurrentProcess(), dst, size);
  return ok;
}

bool ReadU32(uintptr_t addr, uint32_t& out) {
  return SafeRead(reinterpret_cast<const void*>(addr), &out, sizeof(out));
}

bool ReadU64(uintptr_t addr, uint64_t& out) {
  return SafeRead(reinterpret_cast<const void*>(addr), &out, sizeof(out));
}

bool WriteU32(uintptr_t addr, uint32_t value) {
  return WriteProtected(reinterpret_cast<void*>(addr), &value, sizeof(value));
}
} // namespace

bool IsTraitViewerWidthsApplied() {
  return g_applied;
}

bool SetTraitViewerWidths(bool enable) {
  const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!base) {
    AddLog(u8"[기재표시/Step1] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
    return false;
  }

  const uintptr_t unitIdAddr   = base + kUnitColumnIdOffset;
  const uintptr_t unitAddr     = base + kUnitWidthOffset;
  const uintptr_t statusIdAddr = base + kStatusColumnIdOffset;
  const uintptr_t statusAddr   = base + kStatusWidthOffset;

  uint64_t unitId = 0, statusId = 0;
  uint32_t unitBefore = 0, statusBefore = 0;
  if (!ReadU64(unitIdAddr, unitId) || !ReadU64(statusIdAddr, statusId) ||
      !ReadU32(unitAddr, unitBefore) || !ReadU32(statusAddr, statusBefore)) {
    AddLog(u8"[기재표시/Step1] 열 너비 테이블 읽기 실패");
    return false;
  }

  if (unitId != kUnitColumnId || statusId != kStatusColumnId) {
    AddLog(u8"[기재표시/Step1] 패치 거부: 열 ID 불일치 (unit=%llX, status=%llX)",
           static_cast<unsigned long long>(unitId),
           static_cast<unsigned long long>(statusId));
    return false;
  }

  const bool validUnit = unitBefore == kUnitOriginal || unitBefore == kUnitStage1 || unitBefore == kUnitEnabled;
  const bool validStatus = statusBefore == kStatusOriginal || statusBefore == kStatusEnabled;
  if (!validUnit || !validStatus) {
    AddLog(u8"[기재표시/Step1] 패치 거부: 예상 너비와 다름 (unit=%X, status=%X)",
           unitBefore, statusBefore);
    return false;
  }

  if (enable) {
    // CT 91200 -> 91210 순서를 그대로 보존합니다.
    if (unitBefore == kUnitOriginal && !WriteU32(unitAddr, kUnitStage1)) {
      AddLog(u8"[기재표시/Step1] 91200 적용 실패");
      return false;
    }

    uint32_t currentUnit = 0;
    if (!ReadU32(unitAddr, currentUnit) || currentUnit != kUnitStage1) {
      if (unitBefore == kUnitOriginal)
        WriteU32(unitAddr, unitBefore);
      AddLog(u8"[기재표시/Step1] 91210 사전 상태 검증 실패");
      return false;
    }

    if (statusBefore != kStatusOriginal || !WriteU32(unitAddr, kUnitEnabled) ||
        !WriteU32(statusAddr, kStatusEnabled)) {
      // 호출 전 상태로 롤백.
      WriteU32(unitAddr, unitBefore);
      WriteU32(statusAddr, statusBefore);
      AddLog(u8"[기재표시/Step1] 91210 적용 실패 (롤백)");
      return false;
    }

    g_applied = true;
    AddLog(u8"[기재표시/Step1] 열 너비 보정 ON: UNIT 188→132→160, STATUS 104→76");
    return true;
  }

  // CT DISABLE 역순: 91210 -> 91200.
  if (unitBefore == kUnitEnabled || statusBefore == kStatusEnabled) {
    if (unitBefore != kUnitEnabled || statusBefore != kStatusEnabled ||
        !WriteU32(unitAddr, kUnitStage1) || !WriteU32(statusAddr, kStatusOriginal)) {
      WriteU32(unitAddr, unitBefore);
      WriteU32(statusAddr, statusBefore);
      AddLog(u8"[기재표시/Step1] 91210 해제 실패 (롤백)");
      return false;
    }
  }

  uint32_t currentUnit = 0;
  if (!ReadU32(unitAddr, currentUnit) || currentUnit != kUnitStage1) {
    if (unitBefore == kUnitEnabled || statusBefore == kStatusEnabled) {
      WriteU32(unitAddr, unitBefore);
      WriteU32(statusAddr, statusBefore);
    }
    AddLog(u8"[기재표시/Step1] 91200 해제 전 상태 검증 실패");
    return false;
  }

  if (!WriteU32(unitAddr, kUnitOriginal)) {
    WriteU32(unitAddr, unitBefore);
    WriteU32(statusAddr, statusBefore);
    AddLog(u8"[기재표시/Step1] 91200 해제 실패 (롤백)");
    return false;
  }

  g_applied = false;
  AddLog(u8"[기재표시/Step1] 열 너비 보정 OFF: UNIT 188, STATUS 104 복구");
  return true;
}

} // namespace DX11Base
