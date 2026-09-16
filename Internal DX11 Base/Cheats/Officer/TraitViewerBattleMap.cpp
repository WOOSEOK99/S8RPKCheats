#include "pch.h"
#include "TraitViewerBattleMap.h"

#include "../../MemoryUtils.h"
#include "../../showlog.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace DX11Base {
namespace {

struct PatchBytes {
  uintptr_t offset;
  const uint8_t *expected;
  const uint8_t *patched;
  size_t size;
};

static bool g_applied = false;
static uintptr_t g_base = 0;
static uintptr_t g_cave = 0;

bool MatchRaw(uintptr_t address, const uint8_t *expected, size_t size) {
  return std::memcmp(reinterpret_cast<const void *>(address), expected, size) == 0;
}

bool WriteMemory(uintptr_t address, const void *data, size_t size) {
  DWORD oldProtect = 0;
  if (!VirtualProtect(reinterpret_cast<void *>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect))
    return false;
  std::memcpy(reinterpret_cast<void *>(address), data, size);
  FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(address), size);
  DWORD ignored = 0;
  VirtualProtect(reinterpret_cast<void *>(address), size, oldProtect, &ignored);
  return true;
}

bool PatchRel32(std::vector<uint8_t> &code, size_t dispOffset, uintptr_t codeAddress, uintptr_t target) {
  const uintptr_t next = codeAddress + dispOffset + 4;
  const int64_t delta = static_cast<int64_t>(target) - static_cast<int64_t>(next);
  if (delta < std::numeric_limits<int32_t>::min() || delta > std::numeric_limits<int32_t>::max())
    return false;
  const int32_t rel = static_cast<int32_t>(delta);
  std::memcpy(code.data() + dispOffset, &rel, sizeof(rel));
  return true;
}

static constexpr uint8_t kObj208[] = {0xBA,0x08,0x02,0x00,0x00};
static constexpr uint8_t kObj220[] = {0xBA,0x20,0x02,0x00,0x00};
static constexpr uint8_t kCmp6_4[] = {0x48,0x83,0xFD,0x06};
static constexpr uint8_t kCmp9_4[] = {0x48,0x83,0xFD,0x09};
static constexpr uint8_t kCmp6_3[] = {0x83,0xFD,0x06};
static constexpr uint8_t kCmp9_3[] = {0x83,0xFD,0x09};
static constexpr uint8_t kMov6[] = {0xBF,0x06,0x00,0x00,0x00};
static constexpr uint8_t kMov9[] = {0xBF,0x09,0x00,0x00,0x00};
static constexpr uint8_t kLea1E8A[] = {0x48,0x8D,0x9E,0xE8,0x01,0x00,0x00};
static constexpr uint8_t kLea200A[] = {0x48,0x8D,0x9E,0x00,0x02,0x00,0x00};
static constexpr uint8_t kLea1E8B[] = {0x4D,0x8D,0xB7,0xE8,0x01,0x00,0x00};
static constexpr uint8_t kLea200B[] = {0x4D,0x8D,0xB7,0x00,0x02,0x00,0x00};
static constexpr uint8_t kCmpSi6[] = {0x83,0xFE,0x06};
static constexpr uint8_t kCmpSi9[] = {0x83,0xFE,0x09};
static constexpr uint8_t kLea1E8C[] = {0x49,0x8D,0xBF,0xE8,0x01,0x00,0x00};
static constexpr uint8_t kLea200C[] = {0x49,0x8D,0xBF,0x00,0x02,0x00,0x00};
static constexpr uint8_t kLeaDx6[] = {0x8D,0x56,0x06};
static constexpr uint8_t kLeaDx9[] = {0x8D,0x56,0x09};

static const PatchBytes kStaticPatches[] = {
    {0x1DB76B6, kObj208, kObj220, sizeof(kObj208)},
    {0x1DD2405, kObj208, kObj220, sizeof(kObj208)},
    {0x751F7A,  kObj208, kObj220, sizeof(kObj208)},
    {0x1DD5936, kCmp6_4, kCmp9_4, sizeof(kCmp6_4)},
    {0x1DD5B97, kCmp6_3, kCmp9_3, sizeof(kCmp6_3)},
    {0x1DD5A39, kMov6, kMov9, sizeof(kMov6)},
    {0x1DD5A5D, kLea1E8A, kLea200A, sizeof(kLea1E8A)},
    {0x1DD5BC6, kLea1E8B, kLea200B, sizeof(kLea1E8B)},
    {0x1DD6A39, kCmpSi6, kCmpSi9, sizeof(kCmpSi6)},
    {0x1DD6A45, kLea1E8C, kLea200C, sizeof(kLea1E8C)},
    {0x1DD6B19, kLeaDx6, kLeaDx9, sizeof(kLeaDx6)},
};

static constexpr uint8_t kCallOriginal[] = {0xE8,0x9A,0xAF,0x10,0x00};
static constexpr uint8_t kInitOriginal[] = {0x48,0x8B,0xC3,0x48,0x83,0xC4,0x20};

bool VerifyVersion(uintptr_t base) {
  for (const auto &p : kStaticPatches) {
    if (!MatchRaw(base + p.offset, p.expected, p.size))
      return false;
  }
  return MatchRaw(base + 0x1DD68F1, kCallOriginal, sizeof(kCallOriginal)) &&
         MatchRaw(base + 0x1DD6E75, kInitOriginal, sizeof(kInitOriginal));
}

bool BuildCave(uintptr_t base) {
  g_cave = AllocNear(base + 0x1DD68F1, 0x10000);
  if (!g_cave)
    return false;

  // CE layout:
  // +0000 active counter
  // +0100 first qword copied from game, +0108 vtable destructor, +0110 0x560-byte tail
  // +1000 group-copy shim, +2000 init shim, +2100 destructor shim
  const uint64_t zero = 0;
  if (!WriteMemory(g_cave, &zero, sizeof(zero)))
    return false;

  if (!WriteMemory(g_cave + 0x100, reinterpret_cast<const void *>(base + 0x2714688), 8))
    return false;
  const uint64_t destroyPtr = g_cave + 0x2100;
  if (!WriteMemory(g_cave + 0x108, &destroyPtr, sizeof(destroyPtr)))
    return false;
  if (!WriteMemory(g_cave + 0x110, reinterpret_cast<const void *>(base + 0x2714698), 0x560))
    return false;

  // qtGroup. This is the CE code byte-for-byte, except the external CALL rel32 is rebuilt.
  static constexpr uint8_t kGroupTemplate[] = {
    0x53,0x56,0x57,0x48,0x81,0xEC,0x50,0x05,0x00,0x00,0x48,0x89,0xCB,
    0x48,0x89,0x94,0x24,0x20,0x05,0x00,0x00,0x4C,0x89,0x84,0x24,0x28,0x05,0x00,0x00,
    0x4C,0x89,0x8C,0x24,0x30,0x05,0x00,0x00,0x4C,0x8B,0x9C,0x24,0xA0,0x05,0x00,0x00,
    0x4C,0x89,0xDE,0x48,0x8D,0x7C,0x24,0x40,0xB9,0x48,0x00,0x00,0x00,0xFC,0xF3,0x48,0xA5,
    0x49,0x8D,0xB3,0x20,0x01,0x00,0x00,0xB9,0x24,0x00,0x00,0x00,0xF3,0x48,0xA5,
    0x49,0x8D,0xB3,0x40,0x02,0x00,0x00,0xB9,0x30,0x00,0x00,0x00,0xF3,0x48,0xA5,
    0x8B,0x84,0x24,0x90,0x02,0x00,0x00,0x29,0x84,0x24,0x88,0x02,0x00,0x00,
    0x8B,0x84,0x24,0xF0,0x02,0x00,0x00,0x29,0x84,0x24,0xE8,0x02,0x00,0x00,
    0x8B,0x84,0x24,0x50,0x03,0x00,0x00,0x29,0x84,0x24,0x48,0x03,0x00,0x00,
    0x48,0x8B,0x84,0x24,0x90,0x05,0x00,0x00,0x48,0x89,0x44,0x24,0x20,
    0x48,0x8B,0x84,0x24,0x98,0x05,0x00,0x00,0x48,0x89,0x44,0x24,0x28,
    0x48,0x8D,0x44,0x24,0x40,0x48,0x89,0x44,0x24,0x30,0x48,0xC7,0x44,0x24,0x38,0x0D,0x00,0x00,0x00,
    0x48,0x89,0xD9,0x48,0x8B,0x94,0x24,0x20,0x05,0x00,0x00,0x4C,0x8B,0x84,0x24,0x28,0x05,0x00,0x00,
    0x4C,0x8B,0x8C,0x24,0x30,0x05,0x00,0x00,0xE8,0x00,0x00,0x00,0x00,
    0x48,0x81,0xC4,0x50,0x05,0x00,0x00,0x5F,0x5E,0x5B,0xC3
  };
  std::vector<uint8_t> group(std::begin(kGroupTemplate), std::end(kGroupTemplate));
  if (!PatchRel32(group, 0xCF, g_cave + 0x1000, base + 0x1EE1890))
    return false;
  if (!WriteMemory(g_cave + 0x1000, group.data(), group.size()))
    return false;

  // qtInit with dynamic RIP-relative references and external continuation jump.
  static constexpr uint8_t kInitTemplate[] = {
    0x48,0xC7,0x83,0x08,0x02,0x00,0x00,0x00,0x00,0x00,0x00,
    0x48,0xC7,0x83,0x10,0x02,0x00,0x00,0x00,0x00,0x00,0x00,
    0x48,0xC7,0x83,0x18,0x02,0x00,0x00,0x00,0x00,0x00,0x00,
    0x48,0x8D,0x05,0x00,0x00,0x00,0x00,
    0x48,0x89,0x03,
    0xF0,0x48,0xFF,0x05,0x00,0x00,0x00,0x00,
    0x48,0x89,0xD8,0x48,0x83,0xC4,0x20,
    0xE9,0x00,0x00,0x00,0x00
  };
  std::vector<uint8_t> init(std::begin(kInitTemplate), std::end(kInitTemplate));
  if (!PatchRel32(init, 0x23, g_cave + 0x2000, g_cave + 0x108) ||
      !PatchRel32(init, 0x2E, g_cave + 0x2000, g_cave + 0x000) ||
      !PatchRel32(init, 0x3A, g_cave + 0x2000, base + 0x1DD6E7C))
    return false;
  if (!WriteMemory(g_cave + 0x2000, init.data(), init.size()))
    return false;

  static constexpr uint8_t kDestroyTemplate[] = {
    0xF0,0x48,0xFF,0x0D,0x00,0x00,0x00,0x00,
    0xE9,0x00,0x00,0x00,0x00
  };
  std::vector<uint8_t> destroy(std::begin(kDestroyTemplate), std::end(kDestroyTemplate));
  if (!PatchRel32(destroy, 0x04, g_cave + 0x2100, g_cave + 0x000) ||
      !PatchRel32(destroy, 0x09, g_cave + 0x2100, base + 0x751F20))
    return false;
  return WriteMemory(g_cave + 0x2100, destroy.data(), destroy.size());
}

bool ApplyPatch(uintptr_t base) {
  if (!VerifyVersion(base)) {
    AddLog(u8"[기재 화면/전투맵] 91507 원본 바이트 검증 실패. 적용하지 않습니다.");
    return false;
  }
  if (!BuildCave(base)) {
    AddLog(u8"[기재 화면/전투맵] 91507 코드 영역 생성 실패.");
    return false;
  }

  size_t written = 0;
  for (; written < std::size(kStaticPatches); ++written) {
    const auto &p = kStaticPatches[written];
    if (!WriteMemory(base + p.offset, p.patched, p.size))
      break;
  }
  if (written != std::size(kStaticPatches)) {
    while (written > 0) {
      --written;
      const auto &p = kStaticPatches[written];
      WriteMemory(base + p.offset, p.expected, p.size);
    }
    return false;
  }

  std::array<uint8_t,5> callPatch = {0xE8,0,0,0,0};
  {
    const int64_t delta = static_cast<int64_t>(g_cave + 0x1000) - static_cast<int64_t>(base + 0x1DD68F1 + 5);
    if (delta < INT32_MIN || delta > INT32_MAX)
      return false;
    const int32_t rel = static_cast<int32_t>(delta);
    std::memcpy(callPatch.data() + 1, &rel, 4);
  }
  if (!WriteMemory(base + 0x1DD68F1, callPatch.data(), callPatch.size()))
    return false;

  std::array<uint8_t,7> initPatch = {0xE9,0,0,0,0,0x90,0x90};
  {
    const int64_t delta = static_cast<int64_t>(g_cave + 0x2000) - static_cast<int64_t>(base + 0x1DD6E75 + 5);
    if (delta < INT32_MIN || delta > INT32_MAX)
      return false;
    const int32_t rel = static_cast<int32_t>(delta);
    std::memcpy(initPatch.data() + 1, &rel, 4);
  }
  if (!WriteMemory(base + 0x1DD6E75, initPatch.data(), initPatch.size()))
    return false;

  return true;
}

void RestorePatch() {
  if (!g_base)
    return;

  // Same guard as the CT: do not tear down the custom vtable while battle panels still own it.
  if (g_cave && *reinterpret_cast<volatile uint64_t *>(g_cave) != 0)
    return;

  WriteMemory(g_base + 0x1DD6E75, kInitOriginal, sizeof(kInitOriginal));
  WriteMemory(g_base + 0x1DD68F1, kCallOriginal, sizeof(kCallOriginal));
  for (auto it = std::rbegin(kStaticPatches); it != std::rend(kStaticPatches); ++it)
    WriteMemory(g_base + it->offset, it->expected, it->size);

  if (g_cave) {
    VirtualFree(reinterpret_cast<void *>(g_cave), 0, MEM_RELEASE);
    g_cave = 0;
  }
}

} // namespace

bool SetTraitViewerBattleMap(bool enable) {
  if (enable == g_applied)
    return true;

  if (!enable) {
    if (g_cave && *reinterpret_cast<volatile uint64_t *>(g_cave) != 0) {
      AddLog(u8"[기재 화면/전투맵] 전투 패널이 남아 있어 해제할 수 없습니다. 타이틀 화면에서 다시 시도하세요.");
      return false;
    }
    RestorePatch();
    g_applied = false;
    AddLog(u8"[기재 화면/전투맵] 좌우 기재 최대 9개 패치 해제");
    return true;
  }

  auto module = GetModuleHandleW(L"SAN8RPK.exe");
  if (!module)
    return false;
  g_base = reinterpret_cast<uintptr_t>(module);

  if (!ApplyPatch(g_base)) {
    // ApplyPatch may have allocated a cave before failing. Restore only when safe.
    if (g_cave && *reinterpret_cast<volatile uint64_t *>(g_cave) == 0)
      RestorePatch();
    g_base = 0;
    return false;
  }

  g_applied = true;
  AddLog(u8"[기재 화면/전투맵] 좌우 기재 최대 9개 패치 적용");
  return true;
}

bool IsTraitViewerBattleMapApplied() {
  return g_applied;
}

} // namespace DX11Base
