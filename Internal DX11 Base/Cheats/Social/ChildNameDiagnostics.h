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

// 수동 이름 입력 테스트: 성=가나 / 명=다라 / 자=마바
static const wchar_t kSurnameMarker[] = L"가나";
static const wchar_t kGivenMarker[] = L"다라";
static const wchar_t kStyleMarker[] = L"마바";

// 세 번의 실행에서 현재 이름 입력 버퍼는 임신 테이블 기준 같은 상대 위치에서 관찰됨.
// 이것을 영구 장수 이름 테이블로 취급하지 않고, 현재 이름짓기 UI의 임시 버퍼로만 추적한다.
static constexpr uintptr_t kObservedInputBufferDelta = 0x1D6CB6;
static constexpr size_t kNamePartStride = 0x16;
static constexpr uintptr_t kSearchRadius = 0x400000; // 임신 테이블 기준 +/- 4MB
static constexpr size_t kChunkSize = 0x10000;
static constexpr int kMaxRefs = 32;

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
  const DWORD p = protect & 0xFF;
  return p == PAGE_READONLY || p == PAGE_READWRITE ||
         p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READ ||
         p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

static uintptr_t FindOfficerRecord(uintptr_t rosterBase, uint16_t targetId) {
  if (rosterBase <= 0x10000)
    return 0;

  const uintptr_t direct =
      rosterBase + (uintptr_t)(targetId - 1) * kOfficerRecordSize;
  uint16_t verify = 0;
  if (ChildManagerDetail::SafeRead16(direct + 0x08, &verify) &&
      verify == targetId) {
    return direct;
  }

  for (int i = 0; i < 5102; ++i) {
    const uintptr_t p = rosterBase + (uintptr_t)i * kOfficerRecordSize;
    if (ChildManagerDetail::SafeRead16(p + 0x08, &verify) &&
        verify == targetId) {
      return p;
    }
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
    if (!ChildManagerDetail::SafeReadPtr(
            tableBase + (uintptr_t)slot * kPregnancySlotStride +
                kPregnancyChildPtrOffset,
            &rawChildPtr)) {
      continue;
    }

    const uintptr_t childPtr =
        ChildManagerDetail::NormalizeOfficerPtr(rawChildPtr);
    if (childPtr <= 0x10000)
      continue;

    uint16_t id = 0;
    if (ChildManagerDetail::SafeRead16(childPtr + 0x08, &id) &&
        id == childId) {
      return true;
    }
  }
  return false;
}

static bool MarkerMatches(uintptr_t surnameAddr) {
  wchar_t surname[3]{};
  wchar_t given[3]{};
  wchar_t style[3]{};

  if (!SafeReadMem(surnameAddr, surname, 2 * sizeof(wchar_t)) ||
      !SafeReadMem(surnameAddr + kNamePartStride,
                   given, 2 * sizeof(wchar_t)) ||
      !SafeReadMem(surnameAddr + kNamePartStride * 2,
                   style, 2 * sizeof(wchar_t))) {
    return false;
  }

  return wmemcmp(surname, kSurnameMarker, 2) == 0 &&
         wmemcmp(given, kGivenMarker, 2) == 0 &&
         wmemcmp(style, kStyleMarker, 2) == 0;
}

static uintptr_t FindInputBuffer() {
  const uintptr_t tableBase = GetGameBase() + kPregnancyTableOffset;
  if (tableBase <= 0x10000)
    return 0;

  // 먼저 이전 실행에서 반복 확인된 상대 위치를 즉시 검증한다.
  const uintptr_t observed = tableBase + kObservedInputBufferDelta;
  if (MarkerMatches(observed)) {
    AddLog(u8"[자녀이름DBG] 이름 입력 버퍼 고정상대 위치 재확인: %p / tableDelta=0x%llX",
           (void*)observed,
           (unsigned long long)(observed - tableBase));
    return observed;
  }

  // 상대 위치가 달라졌을 때만 좁은 범위에서 성/명/자 세 표식을 동시에 찾는다.
  const uintptr_t start =
      tableBase > kSearchRadius ? tableBase - kSearchRadius : 0x10000;
  const uintptr_t end = tableBase + kSearchRadius;
  std::array<uint8_t, kChunkSize> buffer{};

  AddLog(u8"[자녀이름DBG] 관찰 위치 불일치. 제한 검색으로 입력 버퍼 재탐색");

  uintptr_t addr = start;
  while (addr < end) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi))
      break;

    const uintptr_t regionBase = (uintptr_t)mbi.BaseAddress;
    const uintptr_t regionEnd = regionBase + mbi.RegionSize;
    const uintptr_t scanStart = regionBase < start ? start : regionBase;
    const uintptr_t scanEnd = regionEnd > end ? end : regionEnd;

    if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
        IsReadableProtect(mbi.Protect) && scanStart < scanEnd) {
      uintptr_t cur = scanStart;
      while (cur < scanEnd) {
        const size_t remain = (size_t)(scanEnd - cur);
        const size_t want = remain < kChunkSize ? remain : kChunkSize;
        SIZE_T got = 0;
        if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)cur,
                              buffer.data(), want, &got) != FALSE &&
            got >= 0x30) {
          for (size_t i = 0; i + 0x30 <= got; i += 2) {
            const uintptr_t candidate = cur + i;
            if (!MarkerMatches(candidate))
              continue;
            AddLog(u8"[자녀이름DBG] 이름 입력 버퍼 재탐색 성공: %p / tableDelta=0x%llX",
                   (void*)candidate,
                   (unsigned long long)(candidate - tableBase));
            return candidate;
          }
        }
        cur += want;
      }
    }

    if (regionEnd <= addr)
      break;
    addr = regionEnd;
  }

  AddLog(u8"[자녀이름DBG] 이름 입력 버퍼를 찾지 못함");
  return 0;
}

static void DumpRefContext(uintptr_t refAddr,
                           uintptr_t officerRecord,
                           int refIndex) {
  const uintptr_t start = refAddr >= 0x40 ? refAddr - 0x40 : refAddr;
  std::array<uint8_t, 0x80> raw{};
  if (!SafeReadMem(start, raw.data(), raw.size()))
    return;

  int id16Off = -1;
  int id32Off = -1;
  int officerPtrOff = -1;

  for (size_t off = 0; off + 2 <= raw.size(); off += 2) {
    uint16_t v = 0;
    memcpy(&v, raw.data() + off, sizeof(v));
    if (v == kTargetChildId) {
      id16Off = (int)off;
      break;
    }
  }

  for (size_t off = 0; off + 4 <= raw.size(); off += 4) {
    uint32_t v = 0;
    memcpy(&v, raw.data() + off, sizeof(v));
    if (v == (uint32_t)kTargetChildId) {
      id32Off = (int)off;
      break;
    }
  }

  for (size_t off = 0; off + sizeof(uintptr_t) <= raw.size(); off += 8) {
    uintptr_t v = 0;
    memcpy(&v, raw.data() + off, sizeof(v));
    v &= 0x0000FFFFFFFFFFFFULL;
    if (v == officerRecord) {
      officerPtrOff = (int)off;
      break;
    }
  }

  AddLog(u8"[자녀이름DBG] REFCTX #%d start=%p id16Off=%d id32Off=%d officerPtrOff=%d",
         refIndex, (void*)start, id16Off, id32Off, officerPtrOff);

  // 4004와 직접 연결되는 단서가 있는 참조만 주변 qword를 자세히 출력한다.
  if (id16Off < 0 && id32Off < 0 && officerPtrOff < 0)
    return;

  for (size_t off = 0; off < raw.size(); off += 0x20) {
    uintptr_t q[4]{};
    memcpy(q, raw.data() + off, sizeof(q));
    AddLog(u8"[자녀이름DBG] REFD64 #%d +0x%02zX: %p %p %p %p",
           refIndex, off,
           (void*)q[0], (void*)q[1], (void*)q[2], (void*)q[3]);
  }
}

static void ScanInputBufferReferences(uintptr_t surnameAddr,
                                      uintptr_t officerRecord) {
  const uintptr_t tableBase = GetGameBase() + kPregnancyTableOffset;
  if (surnameAddr <= 0x10000 || tableBase <= 0x10000)
    return;

  const uintptr_t targetMin = surnameAddr >= 0x40 ? surnameAddr - 0x40 : surnameAddr;
  const uintptr_t targetMax = surnameAddr + 0x80;
  const uintptr_t start =
      tableBase > kSearchRadius ? tableBase - kSearchRadius : 0x10000;
  const uintptr_t end = tableBase + kSearchRadius;

  AddLog(u8"[자녀이름DBG] ===== 이름 입력 버퍼 참조 검색 시작 =====");
  AddLog(u8"[자녀이름DBG] buffer=%p given=%p style=%p / officer4004=%p",
         (void*)surnameAddr,
         (void*)(surnameAddr + kNamePartStride),
         (void*)(surnameAddr + kNamePartStride * 2),
         (void*)officerRecord);

  std::array<uint8_t, kChunkSize> buffer{};
  int refs = 0;
  uintptr_t addr = start;

  while (addr < end && refs < kMaxRefs) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi))
      break;

    const uintptr_t regionBase = (uintptr_t)mbi.BaseAddress;
    const uintptr_t regionEnd = regionBase + mbi.RegionSize;
    const uintptr_t scanStart = regionBase < start ? start : regionBase;
    const uintptr_t scanEnd = regionEnd > end ? end : regionEnd;

    if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
        IsReadableProtect(mbi.Protect) && scanStart < scanEnd) {
      uintptr_t cur = scanStart;
      while (cur < scanEnd && refs < kMaxRefs) {
        const size_t remain = (size_t)(scanEnd - cur);
        const size_t want = remain < kChunkSize ? remain : kChunkSize;
        SIZE_T got = 0;

        if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)cur,
                              buffer.data(), want, &got) != FALSE &&
            got >= sizeof(uintptr_t)) {
          for (size_t i = 0;
               i + sizeof(uintptr_t) <= got && refs < kMaxRefs;
               i += 8) {
            uintptr_t value = 0;
            memcpy(&value, buffer.data() + i, sizeof(value));
            value &= 0x0000FFFFFFFFFFFFULL;

            // 성/명/자 버퍼 자체뿐 아니라 그 주변 작은 객체를 가리키는 포인터도 포함한다.
            if (value < targetMin || value >= targetMax)
              continue;

            const uintptr_t refAddr = cur + i;
            if (refAddr >= surnameAddr - 0x100 &&
                refAddr < surnameAddr + 0x100) {
              continue;
            }

            ++refs;
            AddLog(u8"[자녀이름DBG] REF #%d at=%p -> %p / targetDelta=%lld / tableDelta=0x%llX",
                   refs,
                   (void*)refAddr,
                   (void*)value,
                   (long long)(value - surnameAddr),
                   (unsigned long long)(refAddr - tableBase));
            DumpRefContext(refAddr, officerRecord, refs);
          }
        }
        cur += want;
      }
    }

    if (regionEnd <= addr)
      break;
    addr = regionEnd;
  }

  AddLog(u8"[자녀이름DBG] ===== 이름 입력 버퍼 참조 검색 종료 / refs=%d =====",
         refs);
}

struct MonitorState {
  bool armed = false;
  bool completed = false;
  bool scanned = false;
  uintptr_t officerRecord = 0;
  ULONGLONG lastPollMs = 0;
  ULONGLONG linkedMs = 0;
};

static MonitorState g_state{};

static bool ArmBeforeBirth() {
  uintptr_t rosterBase = 0;
  uintptr_t heroMaster = 0;
  uint16_t heroId = 0;
  if (!ChildManagerDetail::ResolveHeroAndRoster(
          rosterBase, heroMaster, heroId)) {
    return false;
  }

  const uintptr_t record = FindOfficerRecord(rosterBase, kTargetChildId);
  if (!record)
    return false;

  uint16_t birth = 0;
  if (!ChildManagerDetail::SafeRead16(record + 0x34, &birth))
    return false;

  g_state.armed = true;
  g_state.completed = false;
  g_state.scanned = false;
  g_state.officerRecord = record;
  g_state.lastPollMs = 0;
  g_state.linkedMs = 0;

  AddLog(u8"[자녀이름DBG] ===== ID4004 이름 입력버퍼 참조 진단 대기: record=%p birth=%u =====",
         (void*)record, (unsigned)birth);
  AddLog(u8"[자녀이름DBG] 테스트 입력값: 성=가나 / 명=다라 / 자=마바");
  return true;
}

static void Poll() {
  const ULONGLONG now = GetTickCount64();
  if (g_state.lastPollMs != 0 && now - g_state.lastPollMs < 100)
    return;
  g_state.lastPollMs = now;

  if (g_state.linkedMs == 0 &&
      IsChildLinkedInPregnancySlot(kTargetChildId)) {
    g_state.linkedMs = now;
    AddLog(u8"[자녀이름DBG] childPtr=4004 연결 감지. 이름 입력 버퍼의 참조를 제한 검색합니다.");
  }

  if (g_state.linkedMs == 0)
    return;

  if (!g_state.scanned && now - g_state.linkedMs >= 250) {
    const uintptr_t inputBuffer = FindInputBuffer();
    if (inputBuffer != 0)
      ScanInputBufferReferences(inputBuffer, g_state.officerRecord);
    g_state.scanned = true;
  }

  if (g_state.scanned && now - g_state.linkedMs >= 800) {
    AddLog(u8"[자녀이름DBG] ===== ID4004 이름 입력버퍼 참조 진단 종료 =====");
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
