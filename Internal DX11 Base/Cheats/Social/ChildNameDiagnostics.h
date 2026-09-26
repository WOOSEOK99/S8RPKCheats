#pragma once

#include "../../MenuState.h"
#include "../../showlog.h"
#include "ChildEarlyAppearance.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace DX11Base {
namespace ChildNameDiagnostics {

static constexpr uint16_t kNamedChildId = 4004;
static constexpr uint16_t kBlankChildId = 4005;

// "유청" UTF-8 / UTF-16LE 바이트.
static constexpr uint8_t kYuCheongUtf8[] = {
    0xEC, 0x9C, 0xA0, 0xEC, 0xB2, 0xAD};
static constexpr uint8_t kYuCheongUtf16[] = {
    0x20, 0xC7, 0xAD, 0xCC};

static bool SafeReadMem(uintptr_t addr, void* out, size_t size) {
  if (!out || size == 0 || addr <= 0x10000)
    return false;
  SIZE_T read = 0;
  return ReadProcessMemory(
             GetCurrentProcess(),
             (LPCVOID)addr,
             out,
             size,
             &read) != FALSE &&
         read == size;
}

static bool IsReadablePointer(uintptr_t addr) {
  if (addr <= 0x10000)
    return false;
  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi))
    return false;
  if (mbi.State != MEM_COMMIT ||
      (mbi.Protect & PAGE_GUARD) ||
      (mbi.Protect & PAGE_NOACCESS)) {
    return false;
  }
  return true;
}

static bool ContainsBytes(const uint8_t* data, size_t size,
                          const uint8_t* needle, size_t needleSize) {
  if (!data || !needle || needleSize == 0 || size < needleSize)
    return false;
  for (size_t i = 0; i + needleSize <= size; ++i) {
    if (memcmp(data + i, needle, needleSize) == 0)
      return true;
  }
  return false;
}

static uintptr_t FindOfficerRecord(uintptr_t rosterBase, uint16_t targetId) {
  if (rosterBase <= 0x10000)
    return 0;

  const uintptr_t direct =
      rosterBase + (uintptr_t)(targetId - 1) * 0x3D0;
  uint16_t verify = 0;
  if (ChildManagerDetail::SafeRead16(direct + 0x08, &verify) &&
      verify == targetId) {
    return direct;
  }

  for (int i = 0; i < 5102; ++i) {
    const uintptr_t p = rosterBase + (uintptr_t)i * 0x3D0;
    if (ChildManagerDetail::SafeRead16(p + 0x08, &verify) &&
        verify == targetId) {
      return p;
    }
  }
  return 0;
}

static void LogPointerCandidates(uintptr_t namedRecord,
                                 uintptr_t blankRecord) {
  int logged = 0;
  for (size_t off = 0; off + sizeof(uintptr_t) <= 0x3D0; off += 8) {
    uintptr_t namedValue = 0;
    uintptr_t blankValue = 0;
    if (!SafeReadMem(namedRecord + off, &namedValue, sizeof(namedValue)) ||
        !SafeReadMem(blankRecord + off, &blankValue, sizeof(blankValue))) {
      continue;
    }

    const uintptr_t namedPtr =
        namedValue & 0x0000FFFFFFFFFFFFULL;
    const uintptr_t blankPtr =
        blankValue & 0x0000FFFFFFFFFFFFULL;

    if (namedPtr == blankPtr || !IsReadablePointer(namedPtr))
      continue;

    uint8_t preview[128]{};
    SIZE_T read = 0;
    const bool previewOk =
        ReadProcessMemory(
            GetCurrentProcess(),
            (LPCVOID)namedPtr,
            preview,
            sizeof(preview),
            &read) != FALSE &&
        read >= 4;

    const bool hasUtf8 =
        previewOk && ContainsBytes(
                         preview, (size_t)read,
                         kYuCheongUtf8, sizeof(kYuCheongUtf8));
    const bool hasUtf16 =
        previewOk && ContainsBytes(
                         preview, (size_t)read,
                         kYuCheongUtf16, sizeof(kYuCheongUtf16));

    AddLog(
        u8"[자녀이름DBG] ptr 후보 +0x%zX : 4004=%p / 4005=%p / 유청UTF8=%u UTF16=%u",
        off,
        (void*)namedPtr,
        (void*)blankPtr,
        (unsigned)(hasUtf8 ? 1 : 0),
        (unsigned)(hasUtf16 ? 1 : 0));

    if (hasUtf8 || hasUtf16) {
      AddLog(
          u8"[자녀이름DBG] >>> 이름 직접 연결 후보 발견: record+0x%zX -> %p",
          off, (void*)namedPtr);
    }

    if (++logged >= 48) {
      AddLog(u8"[자녀이름DBG] 포인터 후보가 많아 48개까지만 표시");
      break;
    }
  }

  if (logged == 0) {
    AddLog(
        u8"[자녀이름DBG] 4004/4005 레코드 차이 중 읽을 수 있는 포인터 후보 없음");
  }
}

static void LogWordDifferences(uintptr_t namedRecord,
                               uintptr_t blankRecord) {
  // 이름이 포인터가 아니라 동적 이름 테이블의 번호/인덱스로 저장되는 경우를 보기 위해
  // 16-bit 단위 차이도 제한적으로 출력합니다.
  int logged = 0;
  for (size_t off = 0; off + 2 <= 0x3D0; off += 2) {
    uint16_t a = 0;
    uint16_t b = 0;
    if (!ChildManagerDetail::SafeRead16(namedRecord + off, &a) ||
        !ChildManagerDetail::SafeRead16(blankRecord + off, &b)) {
      continue;
    }
    if (a == b)
      continue;

    // 이미 구조가 확정된 필드는 이름 후보 분석에서 제외.
    if (off == 0x08 ||
        off == 0x32 || off == 0x34 || off == 0x36 ||
        (off >= 0x48 && off < 0x58)) {
      continue;
    }

    AddLog(
        u8"[자녀이름DBG] word 차이 +0x%zX : 4004=0x%04X(%u) / 4005=0x%04X(%u)",
        off,
        (unsigned)a, (unsigned)a,
        (unsigned)b, (unsigned)b);

    if (++logged >= 80) {
      AddLog(u8"[자녀이름DBG] word 차이가 많아 80개까지만 표시");
      break;
    }
  }
}

static void RunOnce() {
  uintptr_t rosterBase = 0;
  uintptr_t heroMaster = 0;
  uint16_t heroId = 0;
  if (!ChildManagerDetail::ResolveHeroAndRoster(
          rosterBase, heroMaster, heroId)) {
    AddLog(u8"[자녀이름DBG] 주인공/무장 배열 확인 실패");
    return;
  }

  const uintptr_t namedRecord =
      FindOfficerRecord(rosterBase, kNamedChildId);
  const uintptr_t blankRecord =
      FindOfficerRecord(rosterBase, kBlankChildId);

  if (!namedRecord || !blankRecord) {
    AddLog(
        u8"[자녀이름DBG] 레코드 확인 실패: 4004=%p / 4005=%p",
        (void*)namedRecord,
        (void*)blankRecord);
    return;
  }

  uint16_t namedBirth = 0;
  uint16_t blankBirth = 0;
  ChildManagerDetail::SafeRead16(namedRecord + 0x34, &namedBirth);
  ChildManagerDetail::SafeRead16(blankRecord + 0x34, &blankBirth);

  AddLog(
      u8"[자녀이름DBG] ===== 이름 진단 시작: ID4004='유청' / 4004=%p birth=%u / 4005=%p birth=%u =====",
      (void*)namedRecord, (unsigned)namedBirth,
      (void*)blankRecord, (unsigned)blankBirth);

  if (namedBirth == 0) {
    AddLog(u8"[자녀이름DBG] ID4004가 아직 생성된 자녀 상태가 아님");
    return;
  }
  if (blankBirth != 0) {
    AddLog(
        u8"[자녀이름DBG] ID4005도 이미 사용 중이라 blank 비교 기준으로 부적합");
  }

  LogPointerCandidates(namedRecord, blankRecord);
  LogWordDifferences(namedRecord, blankRecord);

  AddLog(u8"[자녀이름DBG] ===== 이름 진단 종료 =====");
}

static void Tick() {
  static bool wasOpen = false;
  if (!bShowChildManagerWin) {
    wasOpen = false;
    return;
  }

  if (!wasOpen) {
    wasOpen = true;
    RunOnce();
  }
}

} // namespace ChildNameDiagnostics
} // namespace DX11Base
