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

namespace DX11Base {
namespace ChildNameDiagnostics {

static constexpr uint16_t kTargetChildId = 4004;
static constexpr uintptr_t kPregnancyTableOffset = 0x5B40;
static constexpr uintptr_t kPregnancySlotStride = 0x28;
static constexpr uintptr_t kPregnancyChildPtrOffset = 0x10;
static constexpr size_t kOfficerRecordSize = 0x3D0;

// 다음 테스트에서 사용자가 직접 입력할 고유 표식.
// 성=가나 / 명=다라 / 자=마바
static const wchar_t kSurnameMarker[] = L"가나";
static const wchar_t kGivenMarker[] = L"다라";
static const wchar_t kStyleMarker[] = L"마바";
static const wchar_t kCombinedMarker[] = L"가나다라마바";

static bool SafeReadMem(uintptr_t addr, void* out, size_t size) {
  if (!out || size == 0 || addr <= 0x10000)
    return false;
  SIZE_T read = 0;
  return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, out, size, &read) != FALSE &&
         read == size;
}

static bool IsReadableProtect(DWORD protect) {
  if ((protect & PAGE_GUARD) || (protect & PAGE_NOACCESS))
    return false;
  const DWORD base = protect & 0xFF;
  return base == PAGE_READONLY || base == PAGE_READWRITE ||
         base == PAGE_WRITECOPY || base == PAGE_EXECUTE_READ ||
         base == PAGE_EXECUTE_READWRITE || base == PAGE_EXECUTE_WRITECOPY;
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

static void LogAroundHit(const char* label, uintptr_t hit) {
  if (!label || hit <= 0x10000)
    return;

  uintptr_t start = hit >= 0x30 ? hit - 0x30 : hit;
  std::array<uint8_t, 0x80> raw{};
  if (!SafeReadMem(start, raw.data(), raw.size()))
    return;

  AddLog(u8"[자녀이름DBG] HIT %s addr=%p 주변 0x80", label, (void*)hit);
  for (size_t off = 0; off < raw.size(); off += 0x10) {
    uint16_t w[8]{};
    memcpy(w, raw.data() + off, sizeof(w));
    AddLog(u8"[자녀이름DBG] HITDUMP %s %p +0x%02zX: %04X %04X %04X %04X %04X %04X %04X %04X",
           label, (void*)start, off,
           (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[3],
           (unsigned)w[4], (unsigned)w[5], (unsigned)w[6], (unsigned)w[7]);
  }
}

struct MarkerSpec {
  const char* label;
  const wchar_t* text;
  size_t wcharCount;
  int hits;
};

static void ScanNameMarkers(const char* stage) {
  MarkerSpec markers[] = {
      {"SURNAME_가나", kSurnameMarker, 2, 0},
      {"GIVEN_다라", kGivenMarker, 2, 0},
      {"STYLE_마바", kStyleMarker, 2, 0},
      {"FULL_가나다라마바", kCombinedMarker, 6, 0},
  };

  AddLog(u8"[자녀이름DBG] ===== 이름 문자열 메모리 스캔 %s 시작 =====",
         stage ? stage : "");

  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;
  const uintptr_t maxAddr = (uintptr_t)si.lpMaximumApplicationAddress;
  constexpr size_t kChunk = 0x10000;
  constexpr size_t kOverlap = 0x20;
  std::array<uint8_t, kChunk> buffer{};

  while (addr < maxAddr) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi))
      break;

    const uintptr_t regionBase = (uintptr_t)mbi.BaseAddress;
    const uintptr_t regionEnd = regionBase + mbi.RegionSize;

    // 사용자 입력 문자열은 런타임 힙에 있을 가능성이 가장 높으므로
    // 우선 MEM_PRIVATE + 읽기 가능한 영역만 검색한다.
    if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
        IsReadableProtect(mbi.Protect)) {
      uintptr_t cur = regionBase;
      while (cur < regionEnd) {
        const size_t remain = (size_t)(regionEnd - cur);
        const size_t want = remain < kChunk ? remain : kChunk;
        SIZE_T got = 0;
        if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)cur,
                              buffer.data(), want, &got) != FALSE && got > 0) {
          for (auto& marker : markers) {
            if (marker.hits >= 12)
              continue;
            const size_t patternBytes = marker.wcharCount * sizeof(wchar_t);
            if (got < patternBytes)
              continue;
            for (size_t i = 0; i + patternBytes <= got; ++i) {
              if (memcmp(buffer.data() + i, marker.text, patternBytes) != 0)
                continue;
              const uintptr_t hit = cur + i;
              AddLog(u8"[자녀이름DBG] %s %s UTF16 hit=%p region=%p size=0x%zX protect=0x%X",
                     stage ? stage : "SCAN", marker.label,
                     (void*)hit, mbi.BaseAddress, (size_t)mbi.RegionSize,
                     (unsigned)mbi.Protect);
              LogAroundHit(marker.label, hit);
              ++marker.hits;
              if (marker.hits >= 12)
                break;
            }
          }
        }

        if (want <= kOverlap)
          break;
        const size_t step = want - kOverlap;
        cur += step;
      }
    }

    if (regionEnd <= addr)
      break;
    addr = regionEnd;
  }

  for (const auto& marker : markers) {
    AddLog(u8"[자녀이름DBG] %s %s hitCount=%d",
           stage ? stage : "SCAN", marker.label, marker.hits);
  }
  AddLog(u8"[자녀이름DBG] ===== 이름 문자열 메모리 스캔 %s 종료 =====",
         stage ? stage : "");
}

struct MonitorState {
  bool armed = false;
  bool completed = false;
  bool firstScanDone = false;
  bool secondScanDone = false;
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
  g_state.firstScanDone = false;
  g_state.secondScanDone = false;
  g_state.record = record;
  g_state.lastPollMs = 0;
  g_state.linkedMs = 0;

  AddLog(u8"[자녀이름DBG] ===== ID4004 이름 문자열 진단 대기: record=%p birth=%u =====",
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
    AddLog(u8"[자녀이름DBG] childPtr=4004 연결 감지. 이름 확정 후 문자열 스캔을 시작합니다.");
  }

  if (g_state.linkedMs == 0)
    return;

  if (!g_state.firstScanDone && now - g_state.linkedMs >= 300) {
    ScanNameMarkers("T+0.3s");
    g_state.firstScanDone = true;
  }

  if (!g_state.secondScanDone && now - g_state.linkedMs >= 1800) {
    ScanNameMarkers("T+1.8s");
    g_state.secondScanDone = true;
  }

  if (g_state.secondScanDone && now - g_state.linkedMs >= 2200) {
    AddLog(u8"[자녀이름DBG] ===== ID4004 이름 문자열 진단 종료 =====");
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
