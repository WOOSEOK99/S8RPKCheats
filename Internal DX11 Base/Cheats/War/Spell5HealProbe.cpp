#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "Spell5HealProbe.h"

#include <cstdint>
#include <cstring>

namespace DX11Base {
  namespace {
#pragma pack(push, 1)
    struct SpellRecord {
      int16_t code1;       // +00
      int16_t code2;       // +02
      int16_t code3;       // +04
      int16_t target;      // +06
      int16_t effect1;     // +08
      int16_t power1;      // +0A
      int16_t duration1;   // +0C
      int16_t effect2;     // +0E
      int16_t power2;      // +10
      int16_t duration2;   // +12
      int16_t range;       // +14
      int16_t unknown16;   // +16
      uint64_t unknownPtr; // +18 - semantics unknown; preserved/copied only
    };
#pragma pack(pop)

    static_assert(sizeof(SpellRecord) == 0x20, "SpellRecord layout mismatch");

    constexpr uintptr_t kSpellRootOffset = 0x034C8630;
    constexpr uintptr_t kSpellTableOffset = 0x0D38;
    constexpr uintptr_t kSpellStride = 0x20;

    static bool g_applied = false;
    static uintptr_t g_spell4Addr = 0;
    static uintptr_t g_spell5Addr = 0;
    static SpellRecord g_spell4Original{};
    static SpellRecord g_spell5Original{};

    static bool Deref(uintptr_t base, uintptr_t offset, uintptr_t &out) {
      const uintptr_t addr = base + offset;
      if (!IsValidPtr(addr, sizeof(uintptr_t)))
        return false;

      out = *reinterpret_cast<uintptr_t *>(addr);
      return IsValidPtr(out, sizeof(uintptr_t));
    }

    static const SpellRecord *Entry(uintptr_t table, int index) {
      return reinterpret_cast<const SpellRecord *>(table + (uintptr_t)index * kSpellStride);
    }

    static bool ValidateSpellTable(uintptr_t table) {
      if (!IsValidPtr(table, kSpellStride * 5))
        return false;

      for (int i = 0; i < 5; ++i) {
        const SpellRecord *s = Entry(table, i);
        const int16_t expected = (int16_t)(i + 1);
        if (s->code1 != expected || s->code2 != expected || s->code3 != expected)
          return false;
      }

      const SpellRecord *s1 = Entry(table, 0);
      const SpellRecord *s2 = Entry(table, 1);
      const SpellRecord *s3 = Entry(table, 2);
      const SpellRecord *s4 = Entry(table, 3);

      return s1->target == 2 && s1->effect1 == 10 && s1->power1 == -40 && s1->range == 8 &&
             s2->target == 2 && s2->effect1 == 11 && s2->power1 == 200 &&
             s2->effect2 == 12 && s2->power2 == 100 && s2->range == 5 &&
             s3->target == 2 && s3->effect1 == 3 && s3->power1 == 3 &&
             s3->duration1 == 1 && s3->range == 5 &&
             s4->target == 2 && s4->effect1 == 3 && s4->power1 == 2 &&
             s4->duration1 == 3 && s4->range == 6;
    }

    static uintptr_t ResolveSpellTable() {
      const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase)
        return 0;

      uintptr_t p = 0;
      if (!Deref(exeBase, kSpellRootOffset, p))
        return 0;
      if (!Deref(p, 0xBB8, p))
        return 0;
      if (!Deref(p, 0x000, p))
        return 0;
      if (!Deref(p, 0x170, p))
        return 0;
      if (!Deref(p, 0x010, p))
        return 0;

      const uintptr_t table = p + kSpellTableOffset;
      if (!ValidateSpellTable(table))
        return 0;

      return table;
    }

    static bool WriteRecord(uintptr_t addr, const SpellRecord &value) {
      if (!IsValidPtr(addr, sizeof(SpellRecord)))
        return false;

      DWORD oldProtect = 0;
      DWORD tmpProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(addr), sizeof(SpellRecord), PAGE_READWRITE, &oldProtect))
        return false;

      std::memcpy(reinterpret_cast<void *>(addr), &value, sizeof(value));
      VirtualProtect(reinterpret_cast<LPVOID>(addr), sizeof(SpellRecord), oldProtect, &tmpProtect);
      return true;
    }
  }

  bool SetSpell5HealProbe(bool enable) {
    if (enable) {
      if (g_applied)
        return true;

      const uintptr_t table = ResolveSpellTable();
      if (!table) {
        AddLog(u8"[책략5DBG] 책략 테이블 해석/검증 실패. 현재 데이터가 원본 상태인지 확인하세요.");
        return false;
      }

      // Step 4b:
      // Keep the entire known-good 신산화계 (#2) 0x20-byte tail (+16/+18), but
      // do NOT put the unverified effect 20 in effect1. In the previous Step 4
      // that combination froze the strategy-selection UI when opening the list.
      // Use a known-good strategy-native primary effect (morale +40) and place
      // effect 20 only in effect2 as the healing candidate.
      const uintptr_t spell2 = table + 1 * kSpellStride;
      const uintptr_t spell4 = table + 3 * kSpellStride;
      const uintptr_t spell5 = table + 4 * kSpellStride;
      if (!IsValidPtr(spell2, sizeof(SpellRecord)) ||
          !IsValidPtr(spell4, sizeof(SpellRecord)) ||
          !IsValidPtr(spell5, sizeof(SpellRecord))) {
        AddLog(u8"[책략5DBG] 2/4/5번 책략 레코드 주소가 유효하지 않습니다.");
        return false;
      }

      std::memcpy(&g_spell4Original, reinterpret_cast<const void *>(spell4), sizeof(g_spell4Original));
      std::memcpy(&g_spell5Original, reinterpret_cast<const void *>(spell5), sizeof(g_spell5Original));

      SpellRecord clone{};
      std::memcpy(&clone, reinterpret_cast<const void *>(spell2), sizeof(clone));

      // Keep the working #2 tail (+16/+18), but use only a known-safe strategy
      // effect in slot 1. The healing candidate is isolated in slot 2.
      clone.code1 = 5;
      clone.code2 = 5;
      clone.code3 = 5;
      clone.target = 1;      // ally
      clone.effect1 = 10;    // known strategy-native morale change
      clone.power1 = 40;     // +40 morale anchor
      clone.duration1 = 0;
      clone.effect2 = 20;    // healing candidate; confirmed only in tactics so far
      clone.power2 = 2000;
      clone.duration2 = 0;
      clone.range = 5;

      // Populate canonical #5 first while preserving 신산화계's +16/+18 tail.
      if (!WriteRecord(spell5, clone)) {
        AddLog(u8"[책략5DBG] 실제 5번 레코드에 치료 실험 데이터 쓰기 실패.");
        return false;
      }

      // Keep using the existing 4th selectable slot only as the route to pick ID 5.
      // It receives the exact same full record for this controlled test.
      if (!WriteRecord(spell4, clone)) {
        WriteRecord(spell5, g_spell5Original);
        AddLog(u8"[책략5DBG] 4번 선택 슬롯 치환 실패 - 5번 레코드는 원복했습니다.");
        return false;
      }

      g_spell4Addr = spell4;
      g_spell5Addr = spell5;
      g_applied = true;

      AddLog(u8"[책략5DBG] Step4b 적용: 신산화계 tail 유지 + 효과1 사기+40 + 효과2 치료후보20");
      AddLog(u8"[책략5DBG] table=%p selector4=%p canonical5=%p source2=%p",
             reinterpret_cast<void *>(table), reinterpret_cast<void *>(spell4),
             reinterpret_cast<void *>(spell5), reinterpret_cast<void *>(spell2));
      AddLog(u8"[책략5DBG] clone: target=%d effect1=%d power1=%d effect2=%d power2=%d range=%d +16=%d +18=%p",
             (int)clone.target, (int)clone.effect1, (int)clone.power1,
             (int)clone.effect2, (int)clone.power2, (int)clone.range,
             (int)clone.unknown16, reinterpret_cast<void *>((uintptr_t)clone.unknownPtr));
      AddLog(u8"[책략5DBG] 기대값: 아군 / 사기+40 + 효과2=20(회복후보2000) / 범위5. 먼저 메뉴 프리징 여부부터 확인하세요.");
      return true;
    }

    if (!g_applied)
      return true;

    bool restored4 = false;
    bool restored5 = false;

    if (g_spell4Addr && IsValidPtr(g_spell4Addr, sizeof(SpellRecord)))
      restored4 = WriteRecord(g_spell4Addr, g_spell4Original);
    if (g_spell5Addr && IsValidPtr(g_spell5Addr, sizeof(SpellRecord)))
      restored5 = WriteRecord(g_spell5Addr, g_spell5Original);

    if (restored4 && restored5)
      AddLog(u8"[책략5DBG] 원복 완료: 4번 사모위계 + 실제 5번 더미 레코드 복구.");
    else
      AddLog(u8"[책략5DBG] 일부 원복 주소가 더 이상 유효하지 않습니다. 전투/화면 전환으로 테이블이 재생성되었을 수 있습니다.");

    g_applied = false;
    g_spell4Addr = 0;
    g_spell5Addr = 0;
    g_spell4Original = {};
    g_spell5Original = {};
    return true;
  }
} // namespace DX11Base
