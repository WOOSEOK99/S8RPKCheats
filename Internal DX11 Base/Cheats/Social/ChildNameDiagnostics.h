#pragma once

#include "../../pch.h"
#include "../../Cheats.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include "ChildEarlyAppearance.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace DX11Base {
namespace ChildNameDiagnostics {

static constexpr uint16_t kTargetChildId = 4004;
static constexpr uintptr_t kPregnancyTableOffset = 0x5B40;
static constexpr uintptr_t kPregnancySlotStride = 0x28;
static constexpr uintptr_t kPregnancyChildPtrOffset = 0x10;
static constexpr size_t kOfficerRecordSize = 0x3D0;

// 이름 레코드 실측 구조
// 성 0x16 bytes + 명 0x16 bytes + 자 0x16 bytes = 0x42 bytes / officer
static constexpr size_t kNamePartSize = 0x16;
static constexpr size_t kNameRecordSize = 0x42;

// 두 번의 독립 실행에서 ID4004 성 슬롯이 임신 테이블 기준 정확히 +0x1D6CB6로 동일했다.
// 4004 = base + (4004 - 1) * 0x42 이므로 전체 이름 테이블 시작 후보는 +0x1964B0.
static constexpr uintptr_t kNameTableFromPregnancyTable = 0x1964B0;

// 테스트 입력: 성=가나 / 명=다라 / 자=마바
static const wchar_t kSurnameMarker[] = L"가나";
static const wchar_t kGivenMarker[] = L"다라";
static const wchar_t kStyleMarker[] = L"마바";

static bool SafeReadMem(uintptr_t addr, void* out, size_t size) {
  if (!out || size == 0 || addr <= 0x10000)
    return false;
  SIZE_T read = 0;
  return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, out, size, &read) != FALSE &&
         read == size;
}

static uintptr_t FindOfficerRecord(uintptr_t rosterBase, uint16_t targetId) {
  if (rosterBase <= 0x10000)
    return 0;
  const uintptr_t direct = rosterBase + (uintptr_t)(targetId - 1) * kOfficerRecordSize;
  uint16_t verify = 0;
  if (ChildManagerDetail::SafeRead16(direct + 0x08, &verify) && verify == targetId)
    return direct;
  for (int i = 0; i < 5102; ++i) {
    const uintptr_t p = rosterBase + (uintptr_t)i * kOfficerRecordSize;
    if (ChildManagerDetail::SafeRead16(p + 0x08, &verify) && verify == targetId)
      return p;
  }
  return 0;
}

static bool IsChildLinkedInPregnancySlot(uint16_t childId) {
  const uintptr_t gameBase = GetGameBase();
  if (gameBase <= 0x10000)
    return false;
  const uintptr_t tableBase = gameBase + kPregnancyTableOffset;
  for (int slot = 0; slot < 3; ++slot) {
    uintptr_t rawChildPtr = 0;
    if (!ChildManagerDetail::SafeReadPtr(tableBase + (uintptr_t)slot * kPregnancySlotStride +
                                            kPregnancyChildPtrOffset,
                                        &rawChildPtr))
      continue;
    const uintptr_t childPtr = ChildManagerDetail::NormalizeOfficerPtr(rawChildPtr);
    if (childPtr <= 0x10000)
      continue;
    uint16_t id = 0;
    if (ChildManagerDetail::SafeRead16(childPtr + 0x08, &id) && id == childId)
      return true;
  }
  return false;
}

static std::string WideFixedToUtf8(const wchar_t* src, size_t wcharCount) {
  if (!src || wcharCount == 0)
    return std::string();

  size_t len = 0;
  while (len < wcharCount && src[len] != L'\0')
    ++len;
  if (len == 0)
    return std::string();

  const int needed = WideCharToMultiByte(CP_UTF8, 0, src, (int)len,
                                          nullptr, 0, nullptr, nullptr);
  if (needed <= 0)
    return std::string();

  std::string out((size_t)needed, '\0');
  WideCharToMultiByte(CP_UTF8, 0, src, (int)len,
                      &out[0], needed, nullptr, nullptr);
  return out;
}

struct NameRecordText {
  bool readable = false;
  std::string surname;
  std::string given;
  std::string style;
};

static NameRecordText ReadNameRecord(uintptr_t recordAddr) {
  NameRecordText result{};
  std::array<uint8_t, kNameRecordSize> raw{};
  if (!SafeReadMem(recordAddr, raw.data(), raw.size()))
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

static uintptr_t GetNameTableBase() {
  const uintptr_t gameBase = GetGameBase();
  if (gameBase <= 0x10000)
    return 0;
  const uintptr_t pregnancyTable = gameBase + kPregnancyTableOffset;
  return pregnancyTable + kNameTableFromPregnancyTable;
}

static uintptr_t GetNameRecordAddress(uint16_t officerId) {
  const uintptr_t base = GetNameTableBase();
  if (base <= 0x10000 || officerId == 0)
    return 0;
  return base + (uintptr_t)(officerId - 1) * kNameRecordSize;
}

static bool MarkerMatches4004(uintptr_t recordAddr) {
  wchar_t surname[3]{};
  wchar_t given[3]{};
  wchar_t style[3]{};
  if (!SafeReadMem(recordAddr, surname, 2 * sizeof(wchar_t)) ||
      !SafeReadMem(recordAddr + kNamePartSize, given, 2 * sizeof(wchar_t)) ||
      !SafeReadMem(recordAddr + kNamePartSize * 2, style, 2 * sizeof(wchar_t))) {
    return false;
  }

  return wmemcmp(surname, kSurnameMarker, 2) == 0 &&
         wmemcmp(given, kGivenMarker, 2) == 0 &&
         wmemcmp(style, kStyleMarker, 2) == 0;
}

static void LogNameRecord(uint16_t officerId) {
  const uintptr_t addr = GetNameRecordAddress(officerId);
  const NameRecordText rec = ReadNameRecord(addr);
  if (!rec.readable) {
    AddLog(u8"[자녀이름DBG] NAMETABLE ID=%u addr=%p READ_FAIL",
           (unsigned)officerId, (void*)addr);
    return;
  }

  AddLog(u8"[자녀이름DBG] NAMETABLE ID=%u addr=%p 성='%s' 명='%s' 자='%s' 합='%s%s'",
         (unsigned)officerId, (void*)addr,
         rec.surname.c_str(), rec.given.c_str(), rec.style.c_str(),
         rec.surname.c_str(), rec.given.c_str());
}

static void ValidateNameTable() {
  const uintptr_t pregnancyTable = GetGameBase() + kPregnancyTableOffset;
  const uintptr_t nameBase = GetNameTableBase();
  const uintptr_t id4004Addr = GetNameRecordAddress(kTargetChildId);

  AddLog(u8"[자녀이름DBG] ===== 이름 테이블 공식 검증 =====");
  AddLog(u8"[자녀이름DBG] pregnancyTable=%p / nameBase=%p / baseDelta=0x%llX",
         (void*)pregnancyTable, (void*)nameBase,
         (unsigned long long)(nameBase - pregnancyTable));
  AddLog(u8"[자녀이름DBG] ID4004 계산주소=%p / deltaFromTable=0x%llX / marker=%s",
         (void*)id4004Addr,
         (unsigned long long)(id4004Addr - pregnancyTable),
         MarkerMatches4004(id4004Addr) ? "MATCH" : "MISMATCH");

  // 현재 세이브에서 4001~4004는 생성 완료, 4005는 아직 비어 있으므로
  // 연속 0x42 레코드 가설을 검증하기 좋은 구간이다.
  for (uint16_t id = 4001; id <= 4005; ++id)
    LogNameRecord(id);

  // 역사/현재 등장 장수 쪽에도 같은 테이블이 적용되는지 샘플 확인.
  const uint16_t samples[] = {1, 163, 565, 792, 952};
  for (uint16_t id : samples)
    LogNameRecord(id);

  AddLog(u8"[자녀이름DBG] ===== 이름 테이블 공식 검증 종료 =====");
}

struct MonitorState {
  bool armed = false;
  bool completed = false;
  bool validated = false;
  uintptr_t record = 0;
  ULONGLONG lastPollMs = 0;
  ULONGLONG linkedMs = 0;
};

static MonitorState g_state{};

static bool ArmBeforeBirth() {
  uintptr_t rosterBase = 0, heroMaster = 0;
  uint16_t heroId = 0;
  if (!ChildManagerDetail::ResolveHeroAndRoster(rosterBase, heroMaster, heroId))
    return false;

  const uintptr_t record = FindOfficerRecord(rosterBase, kTargetChildId);
  if (!record)
    return false;

  uint16_t birth = 0;
  if (!ChildManagerDetail::SafeRead16(record + 0x34, &birth))
    return false;

  g_state.armed = true;
  g_state.completed = false;
  g_state.validated = false;
  g_state.record = record;
  g_state.lastPollMs = 0;
  g_state.linkedMs = 0;

  AddLog(u8"[자녀이름DBG] ===== ID4004 이름 테이블 검증 대기: record=%p birth=%u =====",
         (void*)record, (unsigned)birth);
  AddLog(u8"[자녀이름DBG] 테스트 입력값: 성=가나 / 명=다라 / 자=마바");
  return true;
}

static void Poll() {
  const ULONGLONG now = GetTickCount64();
  if (g_state.lastPollMs != 0 && now - g_state.lastPollMs < 100)
    return;
  g_state.lastPollMs = now;

  const bool linked = IsChildLinkedInPregnancySlot(kTargetChildId);
  if (linked && g_state.linkedMs == 0) {
    g_state.linkedMs = now;
    AddLog(u8"[자녀이름DBG] childPtr=4004 연결 감지. 스캔 없이 이름 테이블 공식을 검증합니다.");
  }

  if (g_state.linkedMs == 0)
    return;

  if (!g_state.validated && now - g_state.linkedMs >= 250) {
    ValidateNameTable();
    g_state.validated = true;
  }

  if (g_state.validated && now - g_state.linkedMs >= 700) {
    AddLog(u8"[자녀이름DBG] ===== ID4004 이름 테이블 진단 종료 =====");
    g_state.armed = false;
    g_state.completed = true;
  }
}

static void Tick() {
  if (g_state.completed)
    return;
  if (!g_state.armed) {
    if (!bShowChildManagerWin)
      return;
    ArmBeforeBirth();
    return;
  }
  Poll();
}

} // namespace ChildNameDiagnostics
} // namespace DX11Base
