#pragma once

#include "../../showlog.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace DX11Base {
namespace IsolatedTerritoryMovementDiagnosticDetail {

struct HookCheck {
  uintptr_t rva;
  const uint8_t* expected;
  size_t size;
};

inline bool ReadMemory(uintptr_t address, void* out, size_t size) {
  if (!address || !out || !size)
    return false;

  MEMORY_BASIC_INFORMATION mbi{};
  if (!VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)))
    return false;
  if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
    return false;

  __try {
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

inline void LogBytes(uintptr_t address, size_t size, char* out, size_t outSize) {
  if (!out || outSize == 0)
    return;
  out[0] = '\0';

  std::array<uint8_t, 32> bytes{};
  if (size > bytes.size() || !ReadMemory(address, bytes.data(), size)) {
    strcpy_s(out, outSize, "READ_FAIL");
    return;
  }

  size_t pos = 0;
  for (size_t i = 0; i < size && pos + 4 < outSize; ++i) {
    const int written = sprintf_s(out + pos, outSize - pos,
                                  i ? " %02X" : "%02X", bytes[i]);
    if (written <= 0)
      break;
    pos += static_cast<size_t>(written);
  }
}

inline bool CheckBytes(uintptr_t address, const uint8_t* expected, size_t size) {
  std::array<uint8_t, 32> bytes{};
  if (size > bytes.size() || !ReadMemory(address, bytes.data(), size))
    return false;
  return std::memcmp(bytes.data(), expected, size) == 0;
}

} // namespace IsolatedTerritoryMovementDiagnosticDetail

inline void RunIsolatedTerritoryMovementDiagnostic() {
  using namespace IsolatedTerritoryMovementDiagnosticDetail;

  static bool s_ran = false;
  if (s_ran)
    return;
  s_ran = true;

  const uintptr_t base =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!base) {
    AddLog(u8"[영토단절DBG] SAN8RPK.exe 베이스 확인 실패");
    return;
  }

  // C++ 구현이 실제로 소유하는 12개 patch site 원본 바이트. 읽기만 합니다.
  static constexpr uint8_t h1[] = {
      0x41,0x83,0x7F,0x60,0x00,0x75,0x15,0x49,0x8B,0x47,0x38,
      0x48,0x85,0xC0,0x0F,0x84,0xCA,0x00,0x00,0x00};
  static constexpr uint8_t h2[] = {
      0x4C,0x8B,0x05,0x8D,0x1B,0x30,0x01,0x41,0x80,0x78,0x0C,
      0x01,0x0F,0x85,0x65,0x01,0x00,0x00};
  static constexpr uint8_t h3[] = {
      0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
      0xB8,0xD0,0x20,0x00,0x00};
  static constexpr uint8_t h4[] = {0xE8,0x80,0x46,0x4F,0x00};
  static constexpr uint8_t h5[] = {0xE8,0x25,0x2F,0x4F,0x00};
  static constexpr uint8_t ai1[] = {0xE8,0x7A,0x68,0x36,0x00};
  static constexpr uint8_t ai2[] = {0xE8,0xA6,0x62,0x36,0x00};
  static constexpr uint8_t ai3[] = {0xE8,0x2E,0x02,0x36,0x00};
  static constexpr uint8_t h7[] = {0xE8,0xA4,0x01,0x00,0x00};
  static constexpr uint8_t h8[] = {0xE8,0xDC,0xE3,0xF9,0xFF};
  static constexpr uint8_t h9[] = {0xE8,0x46,0x05,0xFA,0xFF};
  static constexpr uint8_t h10[] = {0xE8,0x46,0xF8,0xF9,0xFF};

  static constexpr HookCheck checks[] = {
      {0x1961B3C, h1, sizeof(h1)},
      {0x196280C, h2, sizeof(h2)},
      {0x1960F30, h3, sizeof(h3)},
      {0x144948B, h4, sizeof(h4)},
      {0x144ABE6, h5, sizeof(h5)},
      {0x144A321, ai1, sizeof(ai1)},
      {0x144A8F5, ai2, sizeof(ai2)},
      {0x145096D, ai3, sizeof(ai3)},
      {0x1901F77, h7, sizeof(h7)},
      {0x1963D3F, h8, sizeof(h8)},
      {0x1961BD5, h9, sizeof(h9)},
      {0x19628D5, h10, sizeof(h10)},
  };

  AddLog(u8"[영토단절DBG] ===== C++ v5 읽기 전용 진단 시작 =====");

  int passCount = 0;
  for (size_t i = 0; i < std::size(checks); ++i) {
    const HookCheck& check = checks[i];
    const uintptr_t address = base + check.rva;
    const bool pass = CheckBytes(address, check.expected, check.size);
    if (pass)
      ++passCount;

    char current[128]{};
    LogBytes(address, check.size, current, sizeof(current));
    AddLog(u8"[영토단절DBG] patch%u +%llX : %s | %s",
           static_cast<unsigned>(i + 1),
           static_cast<unsigned long long>(check.rva),
           pass ? "PASS" : "FAIL",
           current);
  }

  char nativeMovementEntry[128]{};
  LogBytes(base + 0x17B0BA0, 14, nativeMovementEntry,
           sizeof(nativeMovementEntry));
  AddLog(u8"[영토단절DBG] 참고 +17B0BA0 : %s | PASS/FAIL 판정 제외",
         nativeMovementEntry);

  uintptr_t cityContext = 0;
  ReadMemory(base + 0x2C643A0, &cityContext, sizeof(cityContext));
  if (cityContext) {
    uint16_t field0A = 0;
    uint8_t field0C = 0;
    uint32_t field10 = 0;
    uint8_t field14 = 0;
    const bool readable =
        ReadMemory(cityContext + 0x0A, &field0A, sizeof(field0A)) &&
        ReadMemory(cityContext + 0x0C, &field0C, sizeof(field0C)) &&
        ReadMemory(cityContext + 0x10, &field10, sizeof(field10)) &&
        ReadMemory(cityContext + 0x14, &field14, sizeof(field14));

    if (readable) {
      AddLog(u8"[영토단절DBG] +2C643A0=%p | +0A=%u +0C=%u +10=%u +14=%u",
             reinterpret_cast<void*>(cityContext),
             static_cast<unsigned>(field0A),
             static_cast<unsigned>(field0C),
             static_cast<unsigned>(field10),
             static_cast<unsigned>(field14));
    } else {
      AddLog(u8"[영토단절DBG] +2C643A0=%p | 내부 필드 읽기 실패",
             reinterpret_cast<void*>(cityContext));
    }
  } else {
    AddLog(u8"[영토단절DBG] +2C643A0 포인터 없음");
  }

  AddLog(u8"[영토단절DBG] 결과: %d/12 patch site 원본 일치 / 메모리 쓰기 없음",
         passCount);
  AddLog(u8"[영토단절DBG] ===== 진단 종료 =====");
}

} // namespace DX11Base
