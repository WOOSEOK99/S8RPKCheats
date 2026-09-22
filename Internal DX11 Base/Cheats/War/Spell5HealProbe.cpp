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

    constexpr int kMaxTrackedUnits = 60;
    constexpr uintptr_t kUnitTroopsOffset = 0x38;
    constexpr uintptr_t kUnitXOffset = 0x48;
    constexpr uintptr_t kUnitYOffset = 0x4C;
    constexpr uintptr_t kUnitMoraleOffset = 0x80;
    constexpr uintptr_t kUnitStateOffset = 0x238;

    struct UnitDiagSnapshot {
      uintptr_t unit = 0;
      uint8_t morale = 0;
      bool valid = false;
    };

    static bool g_applied = false;
    static uintptr_t g_spell4Addr = 0;
    static uintptr_t g_spell5Addr = 0;
    static SpellRecord g_spell4Original{};
    static SpellRecord g_spell5Original{};

    static UnitDiagSnapshot g_diag[kMaxTrackedUnits]{};
    static bool g_diagPrimed = false;
    static uintptr_t g_diagListBase = 0;

    static void ResetDiagnostics() {
      std::memset(g_diag, 0, sizeof(g_diag));
      g_diagPrimed = false;
      g_diagListBase = 0;
    }

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

      // Known working carrier:
      // - effect1=10 / +40 is confirmed to work as an ally morale effect.
      // - effect2=20 / 2000 is retained only as a negative control: it did not
      //   heal troops in the previous real-game test.
      clone.code1 = 5;
      clone.code2 = 5;
      clone.code3 = 5;
      clone.target = 1;
      clone.effect1 = 10;
      clone.power1 = 40;
      clone.duration1 = 0;
      clone.effect2 = 20;
      clone.power2 = 2000;
      clone.duration2 = 0;
      clone.range = 5;

      if (!WriteRecord(spell5, clone)) {
        AddLog(u8"[책략5DBG] 실제 5번 레코드에 실험 데이터 쓰기 실패.");
        return false;
      }

      if (!WriteRecord(spell4, clone)) {
        WriteRecord(spell5, g_spell5Original);
        AddLog(u8"[책략5DBG] 4번 선택 슬롯 치환 실패 - 5번 레코드는 원복했습니다.");
        return false;
      }

      g_spell4Addr = spell4;
      g_spell5Addr = spell5;
      g_applied = true;
      ResetDiagnostics();

      AddLog(u8"[책략5DBG] 적용: 아군 / 효과1 사기+40 / 효과2=20(비치료 확인용) / 범위5");
      AddLog(u8"[책략5DBG] table=%p selector4=%p canonical5=%p source2=%p",
             reinterpret_cast<void *>(table), reinterpret_cast<void *>(spell4),
             reinterpret_cast<void *>(spell5), reinterpret_cast<void *>(spell2));
      AddLog(u8"[책략5DBG] 대상 진단도 활성화됨: 전의 +40(또는 100 상한) 변화 부대를 읽기 전용으로 기록합니다.");
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
    ResetDiagnostics();
    return true;
  }

  void UpdateSpell5TargetDiagnostics(int unitCount, uintptr_t unitListBase) {
    if (!g_applied || unitListBase <= 0x10000 || unitCount <= 0)
      return;

    if (unitCount > kMaxTrackedUnits)
      unitCount = kMaxTrackedUnits;

    if (g_diagListBase != unitListBase) {
      ResetDiagnostics();
      g_diagListBase = unitListBase;
    }

    int validCount = 0;

    for (int i = 0; i < unitCount; ++i) {
      const uintptr_t ptrAddr = unitListBase + 0x08 + (uintptr_t)i * 0x10;
      if (!IsValidPtr(ptrAddr, sizeof(uintptr_t))) {
        g_diag[i] = {};
        continue;
      }

      const uintptr_t unit = *reinterpret_cast<const uintptr_t *>(ptrAddr);
      if (!unit || !IsValidPtr(unit, kUnitStateOffset + sizeof(uint32_t))) {
        g_diag[i] = {};
        continue;
      }

      ++validCount;

      const uint8_t morale = *reinterpret_cast<const uint8_t *>(unit + kUnitMoraleOffset);

      if (!g_diagPrimed || !g_diag[i].valid || g_diag[i].unit != unit) {
        g_diag[i].unit = unit;
        g_diag[i].morale = morale;
        g_diag[i].valid = true;
        continue;
      }

      const uint8_t before = g_diag[i].morale;
      if (morale > before) {
        const int delta = (int)morale - (int)before;
        const bool exactPlus40 = (delta == 40);
        const bool cappedPlus40 =
            (morale == 100 && before < 100 && ((int)before + 40) >= 100);

        if (exactPlus40 || cappedPlus40) {
          const uint16_t troops =
              *reinterpret_cast<const uint16_t *>(unit + kUnitTroopsOffset);
          const uint8_t x = *reinterpret_cast<const uint8_t *>(unit + kUnitXOffset);
          const uint8_t y = *reinterpret_cast<const uint8_t *>(unit + kUnitYOffset);
          const uint32_t state =
              *reinterpret_cast<const uint32_t *>(unit + kUnitStateOffset);

          AddLog(u8"[책략5대상DBG] 후보[%d] unit=%p 전의=%u->%u(+%d) 병력=%u 좌표=(%u,%u) 상태=%u",
                 i, reinterpret_cast<void *>(unit),
                 (unsigned)before, (unsigned)morale, delta,
                 (unsigned)troops, (unsigned)x, (unsigned)y, (unsigned)state);
        }
      }

      g_diag[i].unit = unit;
      g_diag[i].morale = morale;
      g_diag[i].valid = true;
    }

    if (!g_diagPrimed && validCount > 0) {
      g_diagPrimed = true;
      AddLog(u8"[책략5대상DBG] 전투부대 %d개 기준값 저장 완료. 이제 5번 책략을 사용하세요.", validCount);
    }

    for (int i = unitCount; i < kMaxTrackedUnits; ++i)
      g_diag[i] = {};
  }
} // namespace DX11Base
