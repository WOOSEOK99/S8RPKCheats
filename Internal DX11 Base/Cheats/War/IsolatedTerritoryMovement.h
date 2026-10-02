#pragma once

#include "../../MemoryUtils.h"
#include "../../showlog.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace DX11Base {
namespace IsolatedTerritoryMovementDetail {

constexpr uintptr_t kHook1 = 0x1961B3C;
constexpr uintptr_t kHook2 = 0x196280C;
constexpr uintptr_t kHook3 = 0x1960F30;
constexpr uintptr_t kHook4Call = 0x144948B;
constexpr uintptr_t kHook5Call = 0x144ABE6;
constexpr uintptr_t kAiMovementCall1 = 0x144A321;
constexpr uintptr_t kAiMovementCall2 = 0x144A8F5;
constexpr uintptr_t kAiMovementCall3 = 0x145096D;
constexpr uintptr_t kHook7Call = 0x1901F77;
constexpr uintptr_t kHook8Call = 0x1963D3F;
constexpr uintptr_t kHook9Call = 0x1961BD5;
constexpr uintptr_t kHook10Call = 0x19628D5;

constexpr uintptr_t kNativeListCall = 0x193DB10;
constexpr uintptr_t kNativeMovementCheck = 0x1902120;
constexpr uintptr_t kNativeListErase = 0x1C5A0;
constexpr uintptr_t kCityContextGlobal = 0x2C643A0;

constexpr uint8_t kHook1Original[] = {
    0x41,0x83,0x7F,0x60,0x00,0x75,0x15,0x49,0x8B,0x47,0x38,
    0x48,0x85,0xC0,0x0F,0x84,0xCA,0x00,0x00,0x00};
constexpr uint8_t kHook2Original[] = {
    0x4C,0x8B,0x05,0x8D,0x1B,0x30,0x01,0x41,0x80,0x78,0x0C,
    0x01,0x0F,0x85,0x65,0x01,0x00,0x00};
constexpr uint8_t kHook3Original[] = {
    0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
    0xB8,0xD0,0x20,0x00,0x00};
constexpr uint8_t kHook4Guard[] = {
    0x45,0x33,0xC9,0x4C,0x8B,0x40,0x20,0x48,0x8D,0x54,0x24,0x48,
    0x49,0x8B,0x8C,0x24,0xC0,0x00,0x00,0x00,0xE8,0x80,0x46,0x4F,0x00};
constexpr uint8_t kHook5Guard[] = {
    0x45,0x33,0xC9,0x4C,0x8B,0x41,0x20,0x48,0x8D,0x54,0x24,0x38,
    0x49,0x8B,0x8C,0x24,0xC0,0x00,0x00,0x00,0xE8,0x25,0x2F,0x4F,0x00};
constexpr uint8_t kAiMovementCall1Original[] = {0xE8,0x7A,0x68,0x36,0x00};
constexpr uint8_t kAiMovementCall2Original[] = {0xE8,0xA6,0x62,0x36,0x00};
constexpr uint8_t kAiMovementCall3Original[] = {0xE8,0x2E,0x02,0x36,0x00};
constexpr uint8_t kHook7Original[] = {0xE8,0xA4,0x01,0x00,0x00};
constexpr uint8_t kHook8Original[] = {0xE8,0xDC,0xE3,0xF9,0xFF};
constexpr uint8_t kHook9Original[] = {0xE8,0x46,0x05,0xFA,0xFF};
constexpr uint8_t kHook10Original[] = {0xE8,0x46,0xF8,0xF9,0xFF};

inline uintptr_t gBase = 0;
inline bool gApplied = false;

struct PatchRecord {
  uintptr_t address = 0;
  std::vector<uint8_t> before;
};
inline std::vector<PatchRecord> gPatches;

inline uintptr_t gHook1Stub = 0;
inline uintptr_t gHook2Stub = 0;
inline uintptr_t gHook3Stub = 0;
inline uintptr_t gHook45Stub = 0;
inline uintptr_t gHook6Stub = 0;
inline uintptr_t gHook78Thunk = 0;
inline uintptr_t gHook910Thunk = 0;

inline bool ReadBytes(uintptr_t address, void* out, size_t size) {
  if (!address || !out || !size)
    return false;
  __try {
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

template <typename T>
inline bool ReadValue(uintptr_t address, T& value) {
  return ReadBytes(address, &value, sizeof(T));
}

inline bool ReadEq(uintptr_t address, const void* expected, size_t size) {
  std::array<uint8_t, 32> current{};
  if (size > current.size() || !ReadBytes(address, current.data(), size))
    return false;
  return std::memcmp(current.data(), expected, size) == 0;
}

inline bool WriteBytes(uintptr_t address, const void* data, size_t size) {
  if (!address || !data || !size)
    return false;

  DWORD oldProtect = 0;
  if (!VirtualProtect(reinterpret_cast<void*>(address), size,
                      PAGE_EXECUTE_READWRITE, &oldProtect))
    return false;

  bool ok = false;
  __try {
    std::memcpy(reinterpret_cast<void*>(address), data, size);
    ok = true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    ok = false;
  }

  DWORD ignored = 0;
  VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, &ignored);
  if (ok)
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);
  return ok;
}

inline bool IsGameObjectValid(uintptr_t object) {
  if (!object)
    return false;

  uintptr_t vtable = 0;
  uintptr_t validate = 0;
  if (!ReadValue(object, vtable) || !vtable ||
      !ReadValue(vtable + 0x48, validate) || !validate)
    return false;

  using ValidateFn = uint8_t(__fastcall*)(uintptr_t);
  __try {
    return reinterpret_cast<ValidateFn>(validate)(object) != 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

inline uintptr_t GetOwnerRoot(uintptr_t object) {
  uintptr_t holder = 0;
  if (!object || !ReadValue(object + 0x90, holder) ||
      !IsGameObjectValid(holder))
    return 0;

  uintptr_t root = 0;
  if (!ReadValue(holder + 0x10, root) || !IsGameObjectValid(root))
    return 0;
  return root;
}

inline bool AreConnected(uintptr_t source, uintptr_t destination) {
  if (!IsGameObjectValid(source) || !IsGameObjectValid(destination))
    return false;

  const uintptr_t owner = GetOwnerRoot(source);
  if (!owner || GetOwnerRoot(destination) != owner)
    return false;

  std::array<uintptr_t, 256> queue{};
  size_t count = 1;
  size_t cursor = 0;
  queue[0] = source;

  while (cursor < count) {
    const uintptr_t current = queue[cursor++];
    if (current == destination)
      return true;

    for (size_t i = 0; i < 6; ++i) {
      uintptr_t neighbor = 0;
      if (!ReadValue(current + 0x20 + i * sizeof(uintptr_t), neighbor) ||
          !IsGameObjectValid(neighbor) || GetOwnerRoot(neighbor) != owner)
        continue;

      bool duplicate = false;
      for (size_t j = 0; j < count; ++j) {
        if (queue[j] == neighbor) {
          duplicate = true;
          break;
        }
      }
      if (duplicate)
        continue;

      if (count >= queue.size())
        return false;
      queue[count++] = neighbor;
    }
  }
  return false;
}

inline uintptr_t ResolveListNode(uintptr_t listRef) {
  if (!listRef || !gBase)
    return 0;

  uintptr_t presence = 0;
  if (!ReadValue(gBase + 0x34C8628, presence) || !presence)
    return 0;

  uintptr_t indexPtr = 0;
  uintptr_t table = 0;
  uint32_t count = 0;
  if (!ReadValue(listRef + 0x08, indexPtr) || !indexPtr ||
      !ReadValue(gBase + 0x34C8630, table) || !table ||
      !ReadValue(gBase + 0x34C8660, count))
    return 0;

  uint32_t index = 0;
  if (!ReadValue(indexPtr, index) || index >= count)
    return 0;

  uintptr_t node = 0;
  if (!ReadValue(table + static_cast<uintptr_t>(index) * sizeof(uintptr_t), node))
    return 0;
  return node;
}

inline bool EraseListObject(uintptr_t listRef, uintptr_t object) {
  if (!gBase)
    return false;
  using EraseFn = void(__fastcall*)(uintptr_t, uintptr_t*);
  uintptr_t local = object;
  __try {
    reinterpret_cast<EraseFn>(gBase + kNativeListErase)(listRef, &local);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

inline bool FilterConnectedList(uintptr_t listRef, uintptr_t target) {
  uintptr_t node = ResolveListNode(listRef);
  uintptr_t previous = 0;
  uint32_t guard = 0x20000;

  while (node) {
    if (guard-- == 0)
      return false;

    uintptr_t object = 0;
    if (!ReadValue(node, object))
      return false;

    bool keep = IsGameObjectValid(object);
    if (keep) {
      uintptr_t city = 0;
      keep = ReadValue(object + 0x20, city) && AreConnected(city, target);
    }

    if (!keep) {
      if (!EraseListObject(listRef, object))
        return false;

      if (previous) {
        if (!ReadValue(previous + 0x08, node))
          return false;
      } else {
        node = ResolveListNode(listRef);
      }
      continue;
    }

    previous = node;
    if (!ReadValue(node + 0x08, node))
      return false;
  }

  return ResolveListNode(listRef) != 0;
}

inline bool MovementContextAllowed(uintptr_t context,
                                   uintptr_t destination,
                                   uintptr_t owner) {
  if (!IsGameObjectValid(context) || !IsGameObjectValid(destination) || !owner)
    return true;

  uintptr_t contextOwner = 0;
  if (!ReadValue(context + 0x18, contextOwner) || contextOwner != owner)
    return true;

  uintptr_t source = 0;
  if (!ReadValue(context + 0x20, source) || !IsGameObjectValid(source))
    return true;

  const uintptr_t sourceOwner = GetOwnerRoot(source);
  if (sourceOwner != owner || GetOwnerRoot(destination) != sourceOwner)
    return true;

  return AreConnected(source, destination);
}

using MovementCheckFn = uint8_t(__fastcall*)(uintptr_t, uintptr_t,
                                               uintptr_t, uintptr_t,
                                               uint32_t);

inline uint8_t __fastcall MovementCheck78(uintptr_t arg1,
                                          uintptr_t destination,
                                          uintptr_t arg3,
                                          uintptr_t arg4,
                                          uint32_t arg5) {
  const auto native = reinterpret_cast<MovementCheckFn>(gBase + kNativeMovementCheck);
  const uint8_t result = native(arg1, destination, arg3, arg4, arg5);
  if (!result || arg5 != 0 || !destination)
    return result;

  uintptr_t source = 0;
  if (!ReadValue(arg1 + 0x20, source))
    return result;
  return AreConnected(source, destination) ? 1 : 0;
}

inline uint8_t __fastcall MovementCheck910(uintptr_t arg1,
                                           uintptr_t destination,
                                           uintptr_t arg3,
                                           uintptr_t arg4,
                                           uint32_t arg5) {
  const auto native = reinterpret_cast<MovementCheckFn>(gBase + kNativeMovementCheck);
  const uint8_t result = native(arg1, destination, arg3, arg4, arg5);
  if (!result)
    return 0;
  if (!destination)
    return 0;

  for (size_t i = 0; i < 6; ++i) {
    uintptr_t neighbor = 0;
    if (!ReadValue(destination + 0x20 + i * sizeof(uintptr_t), neighbor))
      continue;
    if (neighbor && neighbor != destination && AreConnected(destination, neighbor))
      return result;
  }
  return 0;
}

struct CodeBuilder {
  struct Fixup { size_t displacement = 0; int label = 0; };
  std::vector<uint8_t> bytes;
  std::array<size_t, 24> labels{};
  std::vector<Fixup> fixups;

  CodeBuilder() { labels.fill(std::numeric_limits<size_t>::max()); }

  void U8(uint8_t v) { bytes.push_back(v); }
  void Data(const uint8_t* p, size_t n) { bytes.insert(bytes.end(), p, p + n); }
  void U32(uint32_t v) {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    Data(p, sizeof(v));
  }
  void U64(uintptr_t v) {
    const uint64_t value = static_cast<uint64_t>(v);
    const auto* p = reinterpret_cast<const uint8_t*>(&value);
    Data(p, sizeof(value));
  }
  void Bind(int label) { labels[static_cast<size_t>(label)] = bytes.size(); }
  void Jcc(uint8_t condition, int label) {
    U8(0x0F); U8(condition);
    const size_t pos = bytes.size();
    U32(0);
    fixups.push_back({pos, label});
  }
  void Jmp(int label) {
    U8(0xE9);
    const size_t pos = bytes.size();
    U32(0);
    fixups.push_back({pos, label});
  }
  void AbsJmp(uintptr_t destination) {
    const uint8_t op[] = {0xFF,0x25,0,0,0,0};
    Data(op, sizeof(op)); U64(destination);
  }
  void MovRax(uintptr_t value) { U8(0x48); U8(0xB8); U64(value); }
  void MovR8(uintptr_t value) { U8(0x49); U8(0xB8); U64(value); }
  void MovR10(uintptr_t value) { U8(0x49); U8(0xBA); U64(value); }
  void CallRax() { U8(0xFF); U8(0xD0); }

  bool Finish() {
    for (const Fixup& f : fixups) {
      if (f.label < 0 || static_cast<size_t>(f.label) >= labels.size())
        return false;
      const size_t target = labels[static_cast<size_t>(f.label)];
      if (target == std::numeric_limits<size_t>::max())
        return false;
      const int64_t rel = static_cast<int64_t>(target) -
                          static_cast<int64_t>(f.displacement + 4);
      if (rel < INT32_MIN || rel > INT32_MAX)
        return false;
      const int32_t rel32 = static_cast<int32_t>(rel);
      std::memcpy(bytes.data() + f.displacement, &rel32, sizeof(rel32));
    }
    return true;
  }
};

inline void EmitSaveVolatile(CodeBuilder& c, uint32_t stackSize) {
  const uint8_t pushes[] = {0x9C,0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53};
  c.Data(pushes, sizeof(pushes));
  c.Data(reinterpret_cast<const uint8_t*>("\x48\x81\xEC"), 3); c.U32(stackSize);
  const uint8_t xmmSave[] = {
      0xF3,0x0F,0x7F,0x44,0x24,0x20,
      0xF3,0x0F,0x7F,0x4C,0x24,0x30,
      0xF3,0x0F,0x7F,0x54,0x24,0x40,
      0xF3,0x0F,0x7F,0x5C,0x24,0x50,
      0xF3,0x0F,0x7F,0x64,0x24,0x60,
      0xF3,0x0F,0x7F,0x6C,0x24,0x70};
  c.Data(xmmSave, sizeof(xmmSave));
}

inline void EmitRestoreVolatile(CodeBuilder& c, uint32_t stackSize) {
  const uint8_t xmmRestore[] = {
      0xF3,0x0F,0x6F,0x44,0x24,0x20,
      0xF3,0x0F,0x6F,0x4C,0x24,0x30,
      0xF3,0x0F,0x6F,0x54,0x24,0x40,
      0xF3,0x0F,0x6F,0x5C,0x24,0x50,
      0xF3,0x0F,0x6F,0x64,0x24,0x60,
      0xF3,0x0F,0x6F,0x6C,0x24,0x70};
  c.Data(xmmRestore, sizeof(xmmRestore));
  c.Data(reinterpret_cast<const uint8_t*>("\x48\x81\xC4"), 3); c.U32(stackSize);
  const uint8_t pops[] = {0x41,0x5B,0x41,0x5A,0x41,0x59,0x41,0x58,0x5A,0x59,0x58,0x9D};
  c.Data(pops, sizeof(pops));
}

inline uintptr_t CommitCode(CodeBuilder& c, uintptr_t nearAddress = 0) {
  if (!c.Finish())
    return 0;

  uintptr_t memory = 0;
  if (nearAddress)
    memory = AllocNear(nearAddress, c.bytes.size() + 32);
  else
    memory = reinterpret_cast<uintptr_t>(VirtualAlloc(
        nullptr, c.bytes.size() + 32, MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
  if (!memory)
    return 0;

  std::memcpy(reinterpret_cast<void*>(memory), c.bytes.data(), c.bytes.size());
  FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(memory), c.bytes.size());
  return memory;
}

inline uintptr_t BuildHook1Stub() {
  CodeBuilder c;
  enum { LRole = 1, LNull, LDisconnected };
  const uint8_t a[] = {0x41,0x83,0x7F,0x60,0x00}; c.Data(a,sizeof(a));
  c.Jcc(0x85, LRole);
  const uint8_t b[] = {0x49,0x8B,0x47,0x38,0x48,0x85,0xC0}; c.Data(b,sizeof(b));
  c.Jcc(0x84, LNull);
  EmitSaveVolatile(c, 0x80);
  const uint8_t args[] = {0x48,0x89,0xC1,0x48,0x89,0xFA}; c.Data(args,sizeof(args));
  c.MovRax(reinterpret_cast<uintptr_t>(&AreConnected)); c.CallRax();
  const uint8_t test[] = {0x85,0xC0}; c.Data(test,sizeof(test));
  c.Jcc(0x84, LDisconnected);
  EmitRestoreVolatile(c, 0x80);
  const uint8_t cmp[] = {0x48,0x39,0xC7}; c.Data(cmp,sizeof(cmp));
  c.AbsJmp(gBase + 0x1961C18);
  c.Bind(LDisconnected);
  EmitRestoreVolatile(c, 0x80);
  c.AbsJmp(gBase + 0x1961C6F);
  c.Bind(LRole); c.AbsJmp(gBase + 0x1961B58);
  c.Bind(LNull); c.AbsJmp(gBase + 0x1961C1A);
  return CommitCode(c);
}

inline uintptr_t BuildHook2Stub() {
  CodeBuilder c;
  enum { LDisconnected = 1, LFail };
  EmitSaveVolatile(c, 0x80);
  const uint8_t args[] = {0x49,0x8B,0x4E,0x38,0x48,0x89,0xF2}; c.Data(args,sizeof(args));
  c.MovRax(reinterpret_cast<uintptr_t>(&AreConnected)); c.CallRax();
  const uint8_t test[] = {0x85,0xC0}; c.Data(test,sizeof(test));
  c.Jcc(0x84, LDisconnected);
  EmitRestoreVolatile(c, 0x80);
  c.MovR8(gBase + kCityContextGlobal);
  const uint8_t connected[] = {0x4D,0x8B,0x00,0x41,0x80,0x78,0x0C,0x01}; c.Data(connected,sizeof(connected));
  c.Jcc(0x85, LFail);
  c.AbsJmp(gBase + 0x196281E);

  c.Bind(LDisconnected);
  EmitRestoreVolatile(c, 0x80);
  c.MovR8(gBase + kCityContextGlobal);
  const uint8_t dis1[] = {
      0x4D,0x8B,0x00,0x4D,0x85,0xC0}; c.Data(dis1,sizeof(dis1));
  c.Jcc(0x84, LFail);
  const uint8_t dis2[] = {0x41,0x80,0x78,0x0C,0x01}; c.Data(dis2,sizeof(dis2));
  c.Jcc(0x85, LFail);
  const uint8_t dis3[] = {0x41,0x80,0x78,0x14,0x00}; c.Data(dis3,sizeof(dis3));
  c.Jcc(0x85, LFail);
  const uint8_t calc[] = {
      0x41,0x8B,0x50,0x10,
      0x41,0x0F,0xB7,0x40,0x0A,
      0x48,0x69,0xC8,0x9D,0x0B,0x00,0x00,
      0x4C,0x01,0xC1,
      0x8B,0x0C,0x11,
      0x4C,0x01,0xC1,
      0x48,0x01,0xD1};
  c.Data(calc,sizeof(calc));
  c.AbsJmp(gBase + 0x196298A);
  c.Bind(LFail); c.AbsJmp(gBase + 0x1962983);
  return CommitCode(c);
}

inline uintptr_t BuildHook3Stub() {
  CodeBuilder c;
  enum { LOriginal = 1, LFail, LReturnZero };
  EmitSaveVolatile(c, 0x88);
  const uint8_t retLoad[] = {0x48,0x8B,0x84,0x24,0xC8,0x00,0x00,0x00}; c.Data(retLoad,sizeof(retLoad));
  c.MovR10(gBase + 0x1962B1C);
  const uint8_t cmp[] = {0x4C,0x39,0xD0}; c.Data(cmp,sizeof(cmp));
  c.Jcc(0x85, LOriginal);
  c.MovRax(reinterpret_cast<uintptr_t>(&AreConnected)); c.CallRax();
  const uint8_t test[] = {0x85,0xC0}; c.Data(test,sizeof(test));
  c.Jcc(0x84, LFail);
  c.Bind(LOriginal);
  EmitRestoreVolatile(c, 0x88);
  c.Data(kHook3Original, sizeof(kHook3Original));
  c.AbsJmp(gBase + 0x1960F41);

  c.Bind(LFail);
  EmitRestoreVolatile(c, 0x88);
  const uint8_t tail[] = {0x48,0x8B,0x44,0x24,0x28,0x48,0x85,0xC0}; c.Data(tail,sizeof(tail));
  c.Jcc(0x84, LReturnZero);
  const uint8_t clear[] = {0xC7,0x00,0x00,0x00,0x00,0x00}; c.Data(clear,sizeof(clear));
  c.Bind(LReturnZero);
  const uint8_t retZero[] = {0x31,0xC0,0xC3}; c.Data(retZero,sizeof(retZero));
  return CommitCode(c);
}

inline uintptr_t BuildHook45Stub() {
  CodeBuilder c;
  enum { LSkip = 1 };
  EmitSaveVolatile(c, 0x88);
  const uint8_t args[] = {0x48,0x89,0xD1,0x4C,0x89,0xC2}; c.Data(args,sizeof(args));
  c.MovRax(reinterpret_cast<uintptr_t>(&FilterConnectedList)); c.CallRax();
  const uint8_t test[] = {0x85,0xC0}; c.Data(test,sizeof(test));
  c.Jcc(0x84, LSkip);
  EmitRestoreVolatile(c, 0x88);
  c.AbsJmp(gBase + kNativeListCall);
  c.Bind(LSkip);
  EmitRestoreVolatile(c, 0x88);
  c.U8(0xC3);
  return CommitCode(c, gBase + kHook4Call);
}

inline uintptr_t BuildHook6Stub() {
  CodeBuilder c;
  enum { LCheck = 1, LOriginal, LBlocked, LNull };
  EmitSaveVolatile(c, 0x88);
  const uint8_t retLoad[] = {0x48,0x8B,0x84,0x24,0xC8,0x00,0x00,0x00}; c.Data(retLoad,sizeof(retLoad));
  c.MovR10(gBase + 0x144A326);
  const uint8_t cmp[] = {0x4C,0x39,0xD0}; c.Data(cmp,sizeof(cmp)); c.Jcc(0x84,LCheck);
  c.MovR10(gBase + 0x144A8FA); c.Data(cmp,sizeof(cmp)); c.Jcc(0x84,LCheck);
  c.MovR10(gBase + 0x1450972); c.Data(cmp,sizeof(cmp)); c.Jcc(0x85,LOriginal);
  c.Bind(LCheck);
  c.MovRax(reinterpret_cast<uintptr_t>(&MovementContextAllowed)); c.CallRax();
  const uint8_t test[] = {0x85,0xC0}; c.Data(test,sizeof(test)); c.Jcc(0x84,LBlocked);
  c.Bind(LOriginal);
  EmitRestoreVolatile(c, 0x88);
  const uint8_t original1[] = {0x48,0x85,0xC9}; c.Data(original1,sizeof(original1)); c.Jcc(0x84,LNull);
  const uint8_t original2[] = {0x44,0x88,0x4C,0x24,0x20}; c.Data(original2,sizeof(original2));
  c.AbsJmp(gBase + 0x17B0BAE);
  c.Bind(LNull); c.AbsJmp(gBase + 0x17B1E93);
  c.Bind(LBlocked);
  EmitRestoreVolatile(c, 0x88);
  c.U8(0xC3);
  return CommitCode(c);
}

inline uintptr_t BuildNearJumpThunk(uintptr_t site, uintptr_t destination) {
  CodeBuilder c;
  c.MovRax(destination);
  const uint8_t jmp[] = {0xFF,0xE0}; c.Data(jmp,sizeof(jmp));
  return CommitCode(c, site);
}

inline bool ValidateOriginalState() {
  return ReadEq(gBase + kHook1, kHook1Original, sizeof(kHook1Original)) &&
         ReadEq(gBase + kHook2, kHook2Original, sizeof(kHook2Original)) &&
         ReadEq(gBase + kHook3, kHook3Original, sizeof(kHook3Original)) &&
         ReadEq(gBase + 0x1449477, kHook4Guard, sizeof(kHook4Guard)) &&
         ReadEq(gBase + 0x144ABD2, kHook5Guard, sizeof(kHook5Guard)) &&
         ReadEq(gBase + kAiMovementCall1, kAiMovementCall1Original,
                sizeof(kAiMovementCall1Original)) &&
         ReadEq(gBase + kAiMovementCall2, kAiMovementCall2Original,
                sizeof(kAiMovementCall2Original)) &&
         ReadEq(gBase + kAiMovementCall3, kAiMovementCall3Original,
                sizeof(kAiMovementCall3Original)) &&
         ReadEq(gBase + kHook7Call, kHook7Original, sizeof(kHook7Original)) &&
         ReadEq(gBase + kHook8Call, kHook8Original, sizeof(kHook8Original)) &&
         ReadEq(gBase + kHook9Call, kHook9Original, sizeof(kHook9Original)) &&
         ReadEq(gBase + kHook10Call, kHook10Original, sizeof(kHook10Original));
}

inline bool RecordAndWrite(uintptr_t address, const uint8_t* after, size_t size) {
  PatchRecord record;
  record.address = address;
  record.before.resize(size);
  if (!ReadBytes(address, record.before.data(), size))
    return false;
  if (!WriteBytes(address, after, size))
    return false;
  gPatches.push_back(std::move(record));
  return true;
}

inline bool PatchAbsoluteJump(uintptr_t address, uintptr_t destination, size_t size) {
  if (size < 14)
    return false;
  std::vector<uint8_t> patch(size, 0x90);
  patch[0]=0xFF; patch[1]=0x25;
  std::memset(patch.data()+2, 0, 4);
  const uint64_t dst = static_cast<uint64_t>(destination);
  std::memcpy(patch.data()+6, &dst, sizeof(dst));
  return RecordAndWrite(address, patch.data(), patch.size());
}

inline bool PatchCall(uintptr_t address, uintptr_t destination) {
  const int64_t rel = static_cast<int64_t>(destination) -
                      static_cast<int64_t>(address + 5);
  if (rel < INT32_MIN || rel > INT32_MAX)
    return false;
  uint8_t patch[5] = {0xE8,0,0,0,0};
  const int32_t rel32 = static_cast<int32_t>(rel);
  std::memcpy(patch + 1, &rel32, sizeof(rel32));
  return RecordAndWrite(address, patch, sizeof(patch));
}

inline bool RestoreAllPatches() {
  bool ok = true;
  for (auto it = gPatches.rbegin(); it != gPatches.rend(); ++it) {
    if (!WriteBytes(it->address, it->before.data(), it->before.size()))
      ok = false;
  }
  if (ok)
    gPatches.clear();
  return ok;
}

inline bool PrepareStubs() {
  if (!gHook1Stub) gHook1Stub = BuildHook1Stub();
  if (!gHook2Stub) gHook2Stub = BuildHook2Stub();
  if (!gHook3Stub) gHook3Stub = BuildHook3Stub();
  if (!gHook45Stub) gHook45Stub = BuildHook45Stub();
  if (!gHook6Stub) gHook6Stub = BuildHook6Stub();
  if (!gHook78Thunk) gHook78Thunk = BuildNearJumpThunk(gBase + kHook7Call,
      reinterpret_cast<uintptr_t>(&MovementCheck78));
  if (!gHook910Thunk) gHook910Thunk = BuildNearJumpThunk(gBase + kHook9Call,
      reinterpret_cast<uintptr_t>(&MovementCheck910));

  return gHook1Stub && gHook2Stub && gHook3Stub && gHook45Stub &&
         gHook6Stub && gHook78Thunk && gHook910Thunk;
}

} // namespace IsolatedTerritoryMovementDetail

inline bool IsIsolatedTerritoryMovementApplied() {
  return IsolatedTerritoryMovementDetail::gApplied;
}

inline bool SetIsolatedTerritoryMovement(bool enable) {
  using namespace IsolatedTerritoryMovementDetail;

  gBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gBase)
    return false;

  if (!enable) {
    if (!gApplied)
      return true;
    if (!RestoreAllPatches()) {
      AddLog(u8"[영토단절] 해제 실패: 일부 원본 코드 복구 실패");
      return false;
    }
    gApplied = false;
    AddLog(u8"[영토단절] 비활성화 - 원본 이동 판정 복구");
    return true;
  }

  if (gApplied)
    return true;

  if (!ValidateOriginalState()) {
    AddLog(u8"[영토단절] 적용 보류: CT 92011 원본 코드 상태 불일치");
    return false;
  }
  if (!PrepareStubs()) {
    AddLog(u8"[영토단절] 적용 실패: 트램펄린 준비 실패");
    return false;
  }

  gPatches.clear();
  bool ok = true;
  ok = ok && PatchAbsoluteJump(gBase + kHook1, gHook1Stub, sizeof(kHook1Original));
  ok = ok && PatchAbsoluteJump(gBase + kHook2, gHook2Stub, sizeof(kHook2Original));
  ok = ok && PatchAbsoluteJump(gBase + kHook3, gHook3Stub, sizeof(kHook3Original));
  ok = ok && PatchCall(gBase + kHook4Call, gHook45Stub);
  ok = ok && PatchCall(gBase + kHook5Call, gHook45Stub);
  ok = ok && PatchCall(gBase + kHook7Call, gHook78Thunk);
  ok = ok && PatchCall(gBase + kHook8Call, gHook78Thunk);
  ok = ok && PatchCall(gBase + kHook9Call, gHook910Thunk);
  ok = ok && PatchCall(gBase + kHook10Call, gHook910Thunk);

  if (!ok) {
    RestoreAllPatches();
    AddLog(u8"[영토단절] 적용 실패: 부분 변경 복구 완료");
    return false;
  }

  gApplied = true;
  AddLog(u8"[영토단절] 활성화 - 단절된 동일 세력 영토 간 무장 이동 제한 적용");
  return true;
}

} // namespace DX11Base