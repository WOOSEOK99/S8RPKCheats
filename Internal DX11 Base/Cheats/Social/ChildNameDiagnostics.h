#pragma once

#include "../../pch.h"
#include "../../Cheats.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include "../Officer/OfficerData.h"
#include "ChildEarlyAppearance.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace DX11Base {
namespace ChildNameDiagnostics {

static constexpr uintptr_t kPregnancyTableOffset = 0x5B40;

// 이전 실측에서 확인한 런타임 이름 테이블.
// officer 1개당 성 0x16 + 명 0x16 + 자 0x16 = 0x42 bytes.
// 이름 테이블 시작 = pregnancyTable + 0x1964B0.
static constexpr uintptr_t kNameTableFromPregnancyTable = 0x1964B0;
static constexpr size_t kNamePartSize = 0x16;
static constexpr size_t kNameRecordSize = 0x42;
static constexpr uint16_t kGeneratedFirstId = 4001;
static constexpr uint16_t kGeneratedLastId = 4020;

static bool SafeReadMem(uintptr_t addr, void* out, size_t size) {
  if (!out || size == 0 || addr <= 0x10000)
    return false;
  SIZE_T read = 0;
  return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, out, size, &read) != FALSE &&
         read == size;
}

static std::string WideFixedToUtf8(const wchar_t* src, size_t wcharCount) {
  if (!src || wcharCount == 0)
    return std::string();

  size_t len = 0;
  while (len < wcharCount && src[len] != L'\0') {
    const wchar_t ch = src[len];
    if (ch == 0xFFFF || ch < 0x20)
      return std::string();
    ++len;
  }
  if (len == 0)
    return std::string();

  const int needed = WideCharToMultiByte(
      CP_UTF8, 0, src, (int)len, nullptr, 0, nullptr, nullptr);
  if (needed <= 0)
    return std::string();

  std::string out((size_t)needed, '\0');
  WideCharToMultiByte(CP_UTF8, 0, src, (int)len,
                      out.data(), needed, nullptr, nullptr);
  return out;
}

struct RuntimeNameRecord {
  bool readable = false;
  std::string surname;
  std::string given;
  std::string style;

  std::string FullName() const {
    if (surname.empty() && given.empty())
      return std::string();
    return surname + given;
  }
};

static uintptr_t GetNameTableBase() {
  const uintptr_t gameBase = GetGameBase();
  if (gameBase <= 0x10000)
    return 0;
  return gameBase + kPregnancyTableOffset + kNameTableFromPregnancyTable;
}

static uintptr_t GetNameRecordAddress(uint16_t officerId) {
  const uintptr_t base = GetNameTableBase();
  if (base <= 0x10000 || officerId == 0)
    return 0;
  return base + (uintptr_t)(officerId - 1) * kNameRecordSize;
}

static RuntimeNameRecord ReadRuntimeName(uint16_t officerId) {
  RuntimeNameRecord result{};
  const uintptr_t addr = GetNameRecordAddress(officerId);
  if (addr <= 0x10000)
    return result;

  std::array<uint8_t, kNameRecordSize> raw{};
  if (!SafeReadMem(addr, raw.data(), raw.size()))
    return result;

  wchar_t surname[12]{};
  wchar_t given[12]{};
  wchar_t style[12]{};
  memcpy(surname, raw.data(), kNamePartSize);
  memcpy(given, raw.data() + kNamePartSize, kNamePartSize);
  memcpy(style, raw.data() + kNamePartSize * 2, kNamePartSize);

  result.readable = true;
  result.surname = WideFixedToUtf8(surname, 11);
  result.given = WideFixedToUtf8(given, 11);
  result.style = WideFixedToUtf8(style, 11);
  return result;
}

static void ApplyGeneratedRuntimeNames() {
  LoadOfficerNames();

  const uintptr_t nameBase = GetNameTableBase();
  if (nameBase <= 0x10000) {
    AddLog(u8"[자녀이름DBG] 런타임 이름 테이블 주소 확보 실패");
    return;
  }

  AddLog(u8"[자녀이름DBG] ===== 생성자녀 런타임 이름 -> 치트 이름맵 적용 시작 =====");
  AddLog(u8"[자녀이름DBG] nameBase=%p / stride=0x%zX",
         (void*)nameBase, kNameRecordSize);

  int applied = 0;
  for (uint16_t id = kGeneratedFirstId; id <= kGeneratedLastId; ++id) {
    const RuntimeNameRecord rec = ReadRuntimeName(id);
    if (!rec.readable)
      continue;

    const std::string full = rec.FullName();
    if (full.empty())
      continue;

    const std::string before =
        g_officerNames.count((int)id) ? g_officerNames[(int)id] : std::string();
    g_officerNames[(int)id] = full;
    ++applied;

    AddLog(u8"[자녀이름DBG] RUNTIME ID=%u addr=%p 성='%s' 명='%s' 자='%s' 합='%s' / 기존='%s'",
           (unsigned)id,
           (void*)GetNameRecordAddress(id),
           rec.surname.c_str(), rec.given.c_str(), rec.style.c_str(),
           full.c_str(), before.c_str());
  }

  AddLog(u8"[자녀이름DBG] 생성자녀 런타임 이름 적용=%d명", applied);
  AddLog(u8"[자녀이름DBG] 이제 '모든 장수' 목록에서 4001~ 생성자녀 이름을 확인하세요.");
  AddLog(u8"[자녀이름DBG] ===== 생성자녀 런타임 이름 적용 종료 =====");
}

struct MonitorState {
  bool completed = false;
  ULONGLONG armedMs = 0;
};

static MonitorState g_state{};

static void Tick() {
  if (g_state.completed)
    return;

  // 자녀 관리 창을 한 번 열면 현재 세이브의 생성자녀 이름을 즉시 검증/적용한다.
  // 새 출산은 필요 없다. 기존 4001~4004도 그대로 읽는다.
  if (!bShowChildManagerWin) {
    g_state.armedMs = 0;
    return;
  }

  const ULONGLONG now = GetTickCount64();
  if (g_state.armedMs == 0) {
    g_state.armedMs = now;
    return;
  }

  if (now - g_state.armedMs < 300)
    return;

  ApplyGeneratedRuntimeNames();
  g_state.completed = true;
}

} // namespace ChildNameDiagnostics
} // namespace DX11Base
