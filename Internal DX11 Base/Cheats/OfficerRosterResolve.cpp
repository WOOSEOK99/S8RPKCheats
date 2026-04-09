#include "pch.h"
#include "Cheats.h"
#include "OfficerRosterResolve.h"
#include "OfficerData.h"

namespace DX11Base {

namespace {

constexpr std::uintptr_t kExeStaticPtrRva = 0x034C8630;
// 각 단계: 이전 주소에서 *(base + offset) 로 다음 포인터를 읽음
constexpr std::uintptr_t kChainAddends[] = {0x48, 0x8, 0x10, 0x0, 0x8};

inline bool ReadPointer(std::uintptr_t addr, std::uintptr_t *out) {
  if (!out || !addr)
    return false;
  if (!IsValidPtr(addr, sizeof(std::uintptr_t)))
    return false;
  std::uintptr_t v = *reinterpret_cast<std::uintptr_t *>(addr);
  if (v < 0x10000)
    return false;
  *out = v;
  return true;
}

} // namespace

bool TryResolveOfficerRosterArrayBase(std::uintptr_t exeBase, std::uintptr_t *outRosterBase) {
  if (!exeBase || !outRosterBase)
    return false;
  *outRosterBase = 0;

  std::uintptr_t p = 0;
  if (!ReadPointer(exeBase + kExeStaticPtrRva, &p))
    return false;

  for (std::uintptr_t add : kChainAddends) {
    if (!ReadPointer(p + add, &p))
      return false;
  }

  if (!IsValidPtr(p, 0x3D0))
    return false;

  RosterStats s = SafeReadRosterStats(p);
  if (!s.valid)
    return false;

  *outRosterBase = p;
  return true;
}

} // namespace DX11Base
