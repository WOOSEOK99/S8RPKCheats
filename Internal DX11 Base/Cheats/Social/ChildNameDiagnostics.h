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

// 테스트 입력: 성=가나 / 명=다라 / 자=마바
static const wchar_t kSurnameMarker[] = L"가나";
static const wchar_t kGivenMarker[] = L"다라";
static const wchar_t kStyleMarker[] = L"마바";

// 이번 실측에서 성/명/자가 각각 0x16 간격으로 한 블록에 저장됐다.
static constexpr size_t kNamePartStride = 0x16;
static constexpr uintptr_t kSearchRadius = 0x400000; // 임신 테이블 기준 ±4MB만 탐색
static constexpr size_t kChunkSize = 0x10000;
static constexpr size_t kChunkOverlap = 0x40;

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

static void DumpNameBlock(uintptr_t surnameAddr) {
  const uintptr_t start = surnameAddr >= 0x20 ? surnameAddr - 0x20 : surnameAddr;
  std::array<uint8_t, 0x80> raw{};
  if (!SafeReadMem(start, raw.data(), raw.size()))
    return;

  AddLog(u8"[자녀이름DBG] -- 이름 블록 주변 덤프 start=%p surname=%p --",
         (void*)start, (void*)surnameAddr);
  for (size_t off = 0; off < raw.size(); off += 0x10) {
    uint16_t w[8]{};
    memcpy(w, raw.data() + off, sizeof(w));
    AddLog(u8"[자녀이름DBG] NAMEBLK +0x%02zX: %04X %04X %04X %04X %04X %04X %04X %04X",
           off,
           (unsigned)w[0], (unsigned)w[1], (unsigned)w[2], (unsigned)w[3],
           (unsigned)w[4], (unsigned)w[5], (unsigned)w[6], (unsigned)w[7]);
  }
}

static bool MatchesNameBlock(const uint8_t* p, size_t available) {
  const size_t markerBytes = 2 * sizeof(wchar_t);
  const size_t needed = kNamePartStride * 2 + markerBytes;
  if (!p || available < needed)
    return false;

  return memcmp(p, kSurnameMarker, markerBytes) == 0 &&
         memcmp(p + kNamePartStride, kGivenMarker, markerBytes) == 0 &&
         memcmp(p + kNamePartStride * 2, kStyleMarker, markerBytes) == 0;
}

static uintptr_t FindNameBlockNearPregnancyTable() {
  const uintptr_t gameBase = GetGameBase();
  if (gameBase <= 0x10000)
    return 0;

  const uintptr_t tableBase = gameBase + kPregnancyTableOffset;
  const uintptr_t start = tableBase > kSearchRadius ? tableBase - kSearchRadius : 0x10000;
  const uintptr_t end = tableBase + kSearchRadius;

  AddLog(u8"[자녀이름DBG] 제한 검색 시작: table=%p range=%p~%p (8MB)",
         (void*)tableBase, (void*)start, (void*)end);

  std::array<uint8_t, kChunkSize> buffer{};
  uintptr_t cur = start;
  while (cur < end) {
    const size_t remain = (size_t)(end - cur);
    const size_t want = remain < kChunkSize ? remain : kChunkSize;
    SIZE_T got = 0;

    if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)cur,
                          buffer.data(), want, &got) != FALSE && got >= 0x30) {
      // 정상 UTF-16 정렬만 검사. 같은 블록의 성/명/자 3개가 모두 맞아야 히트로 인정.
      for (size_t i = 0; i + 0x30 <= got; i += 2) {
        if (!MatchesNameBlock(buffer.data() + i, got - i))
          continue;

        const uintptr_t hit = cur + i;
        AddLog(u8"[자녀이름DBG] >>> 이름 블록 발견: surname=%p given=%p style=%p tableDelta=%lld",
               (void*)hit,
               (void*)(hit + kNamePartStride),
               (void*)(hit + kNamePartStride * 2),
               (long long)(hit - tableBase));
        DumpNameBlock(hit);
        return hit;
      }
    }

    if (want <= kChunkOverlap)
      break;
    cur += want - kChunkOverlap;
  }

  AddLog(u8"[자녀이름DBG] 제한 검색 범위에서 이름 블록을 찾지 못함");
  return 0;
}

struct MonitorState {
  bool armed = false;
  bool completed = false;
  bool searched = false;
  uintptr_t record = 0;
  uintptr_t nameBlock = 0;
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
  g_state.searched = false;
  g_state.record = record;
  g_state.nameBlock = 0;
  g_state.lastPollMs = 0;
  g_state.linkedMs = 0;

  AddLog(u8"[자녀이름DBG] ===== ID4004 이름 블록 진단 대기: record=%p birth=%u =====",
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
    AddLog(u8"[자녀이름DBG] childPtr=4004 연결 감지. 제한된 이름 블록 검색을 예약합니다.");
  }

  if (g_state.linkedMs == 0)
    return;

  if (!g_state.searched && now - g_state.linkedMs >= 250) {
    g_state.nameBlock = FindNameBlockNearPregnancyTable();
    g_state.searched = true;
  }

  // 블록을 찾았다면 1.5초 뒤 같은 주소에 이름이 그대로 남는지도 확인한다.
  if (g_state.searched && now - g_state.linkedMs >= 1750) {
    if (g_state.nameBlock != 0) {
      wchar_t surname[3]{};
      wchar_t given[3]{};
      wchar_t style[3]{};
      const bool ok =
          SafeReadMem(g_state.nameBlock, surname, 2 * sizeof(wchar_t)) &&
          SafeReadMem(g_state.nameBlock + kNamePartStride, given, 2 * sizeof(wchar_t)) &&
          SafeReadMem(g_state.nameBlock + kNamePartStride * 2, style, 2 * sizeof(wchar_t));
      AddLog(u8"[자녀이름DBG] 이름 블록 지속성 확인: addr=%p result=%s",
             (void*)g_state.nameBlock,
             (ok && wmemcmp(surname, kSurnameMarker, 2) == 0 &&
                    wmemcmp(given, kGivenMarker, 2) == 0 &&
                    wmemcmp(style, kStyleMarker, 2) == 0)
                 ? "STILL_VALID"
                 : "CHANGED_OR_GONE");
    }

    AddLog(u8"[자녀이름DBG] ===== ID4004 이름 블록 진단 종료 =====");
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
