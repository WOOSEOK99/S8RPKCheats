#pragma once

#include <cstdint>
#include <cstring>

// PDB: war::Trick[5] at Tricker+10, m_trickNum at +60.
// UI/model indices are independent of the canonical TrickData IDs.
namespace DX11Base::StratagemFiveModel {
struct Entry {
  uintptr_t data;
  uint32_t index;
  uint32_t available;
};
static_assert(sizeof(Entry) == 0x10);

struct Plan {
  uint32_t originalCount = 0;
  uint32_t campSlot = 0;
  Entry before{};
  Entry added{};
  bool alreadyPresent = false;
};

inline bool RowId(uintptr_t data, uintptr_t table, uint32_t &id) {
  if (!table || data < table || data - table >= 5 * 0x20 || (data - table) % 0x20)
    return false;
  id = static_cast<uint32_t>((data - table) / 0x20 + 1);
  return true;
}

inline bool Prepare(const Entry (&entries)[5], uint32_t count,
                    const uintptr_t (&campRows)[5], uintptr_t table, Plan &out) {
  out = {};
  if (count < 1 || count > 5)
    return false;
  uint32_t seen = 0;
  int fifthEntry = -1, fifthCamp = -1, emptyCamp = -1;
  for (uint32_t i = 0; i < 5; ++i) {
    if (!campRows[i]) {
      if (emptyCamp < 0) emptyCamp = static_cast<int>(i);
      continue;
    }
    uint32_t id = 0;
    if (!RowId(campRows[i], table, id)) return false;
    if (id == 5) {
      if (fifthCamp >= 0) return false;
      fifthCamp = static_cast<int>(i);
    }
  }
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t id = 0;
    if (!RowId(entries[i].data, table, id) || entries[i].index != i ||
        (seen & (1u << id)))
      return false;
    seen |= 1u << id;
    bool inCamp = false;
    for (uintptr_t row : campRows) inCamp |= row == entries[i].data;
    if (!inCamp) return false;
    if (id == 5) fifthEntry = static_cast<int>(i);
  }
  // Never add a second ID5 or rewrite a native/pre-existing ID5's uses.
  if (fifthEntry >= 0) {
    if (count < 2 || fifthEntry != static_cast<int>(count - 1)) return false;
    out.originalCount = count - 1;
    out.campSlot = static_cast<uint32_t>(fifthCamp);
    out.alreadyPresent = true;
    return true;
  }
  if (count > 4 || (fifthCamp < 0 && emptyCamp < 0)) return false;
  // Only an unused physical entry may be synthesized. Do not overwrite an
  // unexposed record, even when its memory is readable.
  if (entries[count].data || entries[count].index || entries[count].available > 1)
    return false;
  out.originalCount = count;
  out.campSlot = static_cast<uint32_t>(fifthCamp >= 0 ? fifthCamp : emptyCamp);
  out.before = entries[count];
  out.added = {table + 4 * 0x20, count, 1};
  return true;
}

inline bool SameIdentity(const Entry &a, const Entry &b) {
  return a.data == b.data && a.index == b.index;
}

inline bool CanRestore(const Entry (&current)[5], uint32_t count,
                       const Entry (&original)[5], const Plan &plan,
                       uintptr_t campRow) {
  if (plan.alreadyPresent || plan.originalCount < 1 || plan.originalCount > 4 ||
      count != plan.originalCount + 1 || campRow != plan.added.data ||
      !SameIdentity(current[plan.originalCount], plan.added))
    return false;
  for (uint32_t i = 0; i < plan.originalCount; ++i)
    if (!SameIdentity(current[i], original[i])) return false;
  return true; // available may legitimately decrease during battle.
}

inline int Position(int center, int nativeStep, uint32_t total, uint32_t index) {
  const int step = total == 5 ? nativeStep * 7 / 8 : nativeStep;
  return center - step * static_cast<int>(total - 1) / 2 + step * static_cast<int>(index);
}
} // namespace DX11Base::StratagemFiveModel
