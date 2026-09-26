#pragma once

#include "../../pch.h"
#include "../../MenuState.h"
#include "../../showlog.h"

#include <Windows.h>
#include <Psapi.h>
#include <array>
#include <cstdint>
#include <cstring>

namespace DX11Base {
namespace ChildNameDiagnostics {

// 실측된 이름 입력 UI 버퍼의 GameBase 기준 오프셋.
// 영구 이름 저장소로 사용하지 않고, SAN8RPK.exe 네이티브 코드가 이 버퍼를
// 어떻게 참조하는지 찾기 위한 정적 코드 스캔 키로만 사용한다.
static constexpr uint32_t kSurnameOffsetFromGameBase = 0x1DC7F6;
static constexpr uint32_t kGivenOffsetFromGameBase   = 0x1DC80C;
static constexpr uint32_t kStyleOffsetFromGameBase   = 0x1DC822;

static void DumpCodeAround(uintptr_t exeBase, uintptr_t hit, const char* label) {
  const uintptr_t start = hit >= exeBase + 0x20 ? hit - 0x20 : exeBase;
  std::array<uint8_t, 0x60> bytes{};
  SIZE_T read = 0;
  if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)start,
                         bytes.data(), bytes.size(), &read) || read == 0) {
    return;
  }

  AddLog(u8"[자녀이름DBG] %s 후보: RVA=+0x%llX hit=%p",
         label,
         (unsigned long long)(hit - exeBase),
         (void*)hit);

  for (size_t off = 0; off < read; off += 0x10) {
    char line[3 * 16 + 1]{};
    char* p = line;
    const size_t n = (read - off) < 0x10 ? (read - off) : 0x10;
    for (size_t i = 0; i < n; ++i) {
      sprintf_s(p, (size_t)(line + sizeof(line) - p), "%02X ", bytes[off + i]);
      p += 3;
    }
    AddLog(u8"[자녀이름DBG]   %+03llX : %s",
           (long long)((start + off) - hit), line);
  }
}

static int ScanImmediate32(uintptr_t exeBase,
                           size_t exeSize,
                           uint32_t value,
                           const char* label) {
  if (!exeBase || exeSize < 4)
    return 0;

  const uint8_t* begin = reinterpret_cast<const uint8_t*>(exeBase);
  int count = 0;

  __try {
    for (size_t i = 0; i + sizeof(uint32_t) <= exeSize; ++i) {
      uint32_t v = 0;
      memcpy(&v, begin + i, sizeof(v));
      if (v != value)
        continue;

      ++count;
      if (count <= 16)
        DumpCodeAround(exeBase, exeBase + i, label);
    }
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    AddLog(u8"[자녀이름DBG] %s 스캔 중 예외 발생", label);
  }

  AddLog(u8"[자녀이름DBG] %s 32bit 상수 후보 수=%d", label, count);
  return count;
}

static void ScanNativeExeReferences() {
  const uintptr_t exeBase = (uintptr_t)GetModuleHandleW(nullptr);
  if (!exeBase) {
    AddLog(u8"[자녀이름DBG] SAN8RPK.exe 베이스 확보 실패");
    return;
  }

  MODULEINFO mi{};
  if (!GetModuleInformation(GetCurrentProcess(), (HMODULE)exeBase,
                            &mi, sizeof(mi))) {
    AddLog(u8"[자녀이름DBG] SAN8RPK.exe 이미지 크기 확보 실패");
    return;
  }

  AddLog(u8"[자녀이름DBG] ===== SAN8RPK.exe 이름 입력버퍼 참조 상수 스캔 시작 =====");
  AddLog(u8"[자녀이름DBG] exe=%p size=0x%X / offsets=%08X,%08X,%08X",
         (void*)exeBase,
         (unsigned)mi.SizeOfImage,
         kSurnameOffsetFromGameBase,
         kGivenOffsetFromGameBase,
         kStyleOffsetFromGameBase);

  ScanImmediate32(exeBase, mi.SizeOfImage,
                  kSurnameOffsetFromGameBase, "SURNAME");
  ScanImmediate32(exeBase, mi.SizeOfImage,
                  kGivenOffsetFromGameBase, "GIVEN");
  ScanImmediate32(exeBase, mi.SizeOfImage,
                  kStyleOffsetFromGameBase, "STYLE");

  AddLog(u8"[자녀이름DBG] ===== SAN8RPK.exe 이름 입력버퍼 참조 상수 스캔 종료 =====");
}

struct State {
  bool done = false;
  ULONGLONG openedAt = 0;
};

static State g_state{};

static void Tick() {
  if (g_state.done)
    return;

  if (!bShowChildManagerWin) {
    g_state.openedAt = 0;
    return;
  }

  const ULONGLONG now = GetTickCount64();
  if (g_state.openedAt == 0) {
    g_state.openedAt = now;
    return;
  }

  if (now - g_state.openedAt < 300)
    return;

  ScanNativeExeReferences();
  g_state.done = true;
}

} // namespace ChildNameDiagnostics
} // namespace DX11Base
