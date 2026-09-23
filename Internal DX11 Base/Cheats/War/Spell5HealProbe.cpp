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
    };
#pragma pack(pop)

    // PDB 확인: 실제 TrickData 객체는 0x20 stride이며 +0x00에 8-byte vptr,
    // payload는 +0x08부터 +0x1F까지 0x18 bytes입니다.
    // 예전 +0x18 qword는 현재 레코드 tail이 아니라 다음 TrickData의 vptr이었습니다.
    static_assert(sizeof(SpellRecord) == 0x18, "SpellRecord payload layout mismatch");

    constexpr uintptr_t kSpellRootOffset = 0x034C8630;
    constexpr uintptr_t kSpellTableOffset = 0x0D38;
    constexpr uintptr_t kSpellStride = 0x20;

    constexpr int kMaxTrackedUnits = 60;
    constexpr uintptr_t kUnitTroopsOffset = 0x38;
    constexpr uintptr_t kUnitXOffset = 0x48;
    constexpr uintptr_t kUnitYOffset = 0x4C;
    constexpr uintptr_t kUnitMoraleOffset = 0x80;
    constexpr uintptr_t kUnitStateOffset = 0x238;
    static Spell5CustomSettings g_customSettings{};

    static int ClampInt(int value, int lo, int hi) {
      return value < lo ? lo : (value > hi ? hi : value);
    }

    static int NormalizeEffect(int effect) {
      switch (effect) {
      case 0:
      case 3:
      case 10:
      case 11:
      case 12:
        return effect;
      default:
        return 0;
      }
    }

    static Spell5CustomSettings NormalizeSettings(Spell5CustomSettings v) {
      v.target = ClampInt(v.target, 1, 3);
      v.effect1 = NormalizeEffect(v.effect1);
      v.effect2 = NormalizeEffect(v.effect2);
      v.power1 = ClampInt(v.power1, -32768, 32767);
      v.power2 = ClampInt(v.power2, -32768, 32767);
      v.duration1 = ClampInt(v.duration1, 0, 30);
      v.duration2 = ClampInt(v.duration2, 0, 30);
      v.range = ClampInt(v.range, 1, 100);
      v.healAmount = ClampInt(v.healAmount, 0, 65535);
      return v;
    }

    static bool RecordMatchesSettings(const SpellRecord &cur) {
      const Spell5CustomSettings s = g_customSettings;
      return cur.code1 == 5 && cur.code2 == 5 && cur.code3 == 5 &&
             cur.target == s.target &&
             cur.effect1 == s.effect1 && cur.power1 == s.power1 &&
             cur.duration1 == s.duration1 &&
             cur.effect2 == s.effect2 && cur.power2 == s.power2 &&
             cur.duration2 == s.duration2 &&
             cur.range == s.range;
    }

    static void ApplySettingsToRecord(SpellRecord &clone) {
      const Spell5CustomSettings s = g_customSettings;
      clone.code1 = 5;
      clone.code2 = 5;
      clone.code3 = 5;
      clone.target = static_cast<int16_t>(s.target);
      clone.effect1 = static_cast<int16_t>(s.effect1);
      clone.power1 = static_cast<int16_t>(s.power1);
      clone.duration1 = static_cast<int16_t>(s.duration1);
      clone.effect2 = static_cast<int16_t>(s.effect2);
      clone.power2 = static_cast<int16_t>(s.power2);
      clone.duration2 = static_cast<int16_t>(s.duration2);
      clone.range = static_cast<int16_t>(s.range);
    }

    struct UnitDiagSnapshot {
      uintptr_t unit = 0;
      uint8_t morale = 0;
      bool valid = false;
    };

    static bool g_applied = false;
    static bool g_requested = false;
    static uintptr_t g_spell5Addr = 0;
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

  static bool ApplySpell5ToResolvedTable(uintptr_t table) {
    if (!ValidateSpellTable(table))
      return false;

    const uintptr_t spell2 = table + 1 * kSpellStride;
    const uintptr_t spell5 = table + 4 * kSpellStride;
    if (!IsValidPtr(spell2, sizeof(SpellRecord)) ||
        !IsValidPtr(spell5, sizeof(SpellRecord)))
      return false;

    if (g_applied && g_spell5Addr == spell5 &&
        IsValidPtr(spell5, sizeof(SpellRecord))) {
      const SpellRecord *cur =
          reinterpret_cast<const SpellRecord *>(spell5);
      if (RecordMatchesSettings(*cur))
        return true;
    }

    if (g_applied && g_spell5Addr != spell5) {
      g_applied = false;
      g_spell5Addr = 0;
      g_spell5Original = {};
      ResetDiagnostics();
    }

    if (!g_applied)
      std::memcpy(&g_spell5Original,
                  reinterpret_cast<const void *>(spell5),
                  sizeof(g_spell5Original));

    SpellRecord clone{};
    std::memcpy(&clone,
                reinterpret_cast<const void *>(spell2),
                sizeof(clone));
    ApplySettingsToRecord(clone);

    if (!WriteRecord(spell5, clone))
      return false;

    g_spell5Addr = spell5;
    g_applied = true;
    ResetDiagnostics();
    return true;
  }

  Spell5CustomSettings GetSpell5CustomSettings() {
    return g_customSettings;
  }

  void SetSpell5CustomSettings(const Spell5CustomSettings &settings) {
    const Spell5CustomSettings normalized = NormalizeSettings(settings);
    g_customSettings = normalized;

    if (g_applied && g_spell5Addr &&
        IsValidPtr(g_spell5Addr, sizeof(SpellRecord))) {
      SpellRecord updated =
          *reinterpret_cast<const SpellRecord *>(g_spell5Addr);
      ApplySettingsToRecord(updated);
      if (WriteRecord(g_spell5Addr, updated)) {
        ResetDiagnostics();
        AddLog(u8"[책략5설정] 즉시 반영: 대상=%d 효과1=%d/%d/%d 효과2=%d/%d/%d 범위=%d 추가병력=%d",
               normalized.target,
               normalized.effect1, normalized.power1, normalized.duration1,
               normalized.effect2, normalized.power2, normalized.duration2,
               normalized.range, normalized.healAmount);
      } else {
        AddLog(u8"[책략5설정] 현재 ID5 레코드 즉시 반영 실패. 다음 재적용 때 반영됩니다.");
      }
    }
  }

  bool SetSpell5HealProbeFromMetadataTable(uintptr_t metadataTable) {
    g_requested = true;
    if (!metadataTable || !IsValidPtr(metadataTable, 5 * kSpellStride))
      return false;

    const uintptr_t payloadTable = metadataTable + 0x08;
    if (!ApplySpell5ToResolvedTable(payloadTable))
      return false;

    AddLog(u8"[책략5STATE] live TrickData table에서 ID5 데이터 즉시 준비: table=%p row5=%p",
           reinterpret_cast<void *>(metadataTable),
           reinterpret_cast<void *>(metadataTable + 4 * kSpellStride));
    return true;
  }

  bool SetSpell5HealProbe(bool enable) {
    if (enable) {
      g_requested = true;

      if (g_applied) {
        const uintptr_t tableNow = ResolveSpellTable();
        const uintptr_t expectedAddr =
            tableNow ? tableNow + 4 * kSpellStride : 0;
        if (expectedAddr &&
            g_spell5Addr == expectedAddr &&
            IsValidPtr(expectedAddr, sizeof(SpellRecord))) {
          const SpellRecord *cur =
              reinterpret_cast<const SpellRecord *>(expectedAddr);
          if (RecordMatchesSettings(*cur)) {
            return true;
          }
        }

        AddLog(u8"[책략5수명] 현재 TrickData 세대가 기존 적용과 달라 재적용합니다.");
        g_applied = false;
        g_spell5Addr = 0;
        g_spell5Original = {};
        ResetDiagnostics();
      }

      const uintptr_t table = ResolveSpellTable();
      if (!table) {
        AddLog(u8"[책략5수명] 5번 책략 데이터 ON 요청 유지. TrickData 테이블 미준비 -> 자동 재시도 대기.");
        return true;
      }

      const uintptr_t spell2 = table + 1 * kSpellStride;
      const uintptr_t spell5 = table + 4 * kSpellStride;
      if (!IsValidPtr(spell2, sizeof(SpellRecord)) ||
          !IsValidPtr(spell5, sizeof(SpellRecord))) {
        AddLog(u8"[책략5수명] 5번 책략 데이터 ON 요청 유지. 2/5번 레코드 미준비 -> 자동 재시도 대기.");
        return true;
      }

      std::memcpy(&g_spell5Original, reinterpret_cast<const void *>(spell5), sizeof(g_spell5Original));

      SpellRecord clone{};
      std::memcpy(&clone, reinterpret_cast<const void *>(spell2), sizeof(clone));

      // Known working carrier:
      // - effect1=10 / +40 is confirmed to work as an ally morale effect.
      // - effect2=20 was confirmed NOT to heal troops in real-game testing,
      //   so keep secondary effect disabled. Healing is injected separately
      //   after the affected unit is identified from the +40 morale change.
      ApplySettingsToRecord(clone);

      if (!WriteRecord(spell5, clone)) {
        AddLog(u8"[책략5수명] 5번 책략 데이터 ON 요청 유지. 현재 세대 쓰기 실패 -> 자동 재시도 대기.");
        return true;
      }

      // 이번 단계에서는 4번 사모위계를 절대 치환하지 않습니다.
      // 실제 5번 횟수 슬롯(+14C)을 켰을 때 5번째 책략이 독립적으로
      // 노출되는지 확인하기 위한 테스트입니다.
      g_spell5Addr = spell5;
      g_applied = true;
      ResetDiagnostics();

      const Spell5CustomSettings applied = g_customSettings;
      AddLog(u8"[책략5DBG] 적용: 대상=%d / 효과1=%d 수치1=%d 기간1=%d / 효과2=%d 수치2=%d 기간2=%d / 범위=%d / 추가병력=%d",
             applied.target, applied.effect1, applied.power1, applied.duration1,
             applied.effect2, applied.power2, applied.duration2,
             applied.range, applied.healAmount);
      AddLog(u8"[책략5DBG] table=%p canonical5=%p source2=%p / 4번 사모위계 유지",
             reinterpret_cast<void *>(table),
             reinterpret_cast<void *>(spell5), reinterpret_cast<void *>(spell2));
      if (applied.healAmount > 0 &&
          applied.effect1 == 10 && applied.power1 > 0) {
        AddLog(u8"[책략5DBG] 추가 병력회복 활성화: 사기 +%d 변화 감지 시 병력 +%d(최대병력 상한).",
               applied.power1, applied.healAmount);
      } else if (applied.healAmount > 0) {
        AddLog(u8"[책략5DBG] 추가 병력회복은 효과1=사기 증가일 때만 동작합니다.");
      }
      return true;
    }

    g_requested = false;

    if (!g_applied)
      return true;

    bool restored5 = false;

    if (g_spell5Addr && IsValidPtr(g_spell5Addr, sizeof(SpellRecord)))
      restored5 = WriteRecord(g_spell5Addr, g_spell5Original);

    if (restored5)
      AddLog(u8"[책략5DBG] 원복 완료: 실제 5번 더미 레코드 복구. 4번은 처음부터 건드리지 않았습니다.");
    else
      AddLog(u8"[책략5DBG] 5번 원복 주소가 더 이상 유효하지 않습니다. 전투/화면 전환으로 테이블이 재생성되었을 수 있습니다.");

    g_applied = false;
    g_spell5Addr = 0;
    g_spell5Original = {};
    ResetDiagnostics();
    return true;
  }

  bool IsSpell5HealProbeReady() {
    if (!g_requested || !g_applied || !g_spell5Addr ||
        !IsValidPtr(g_spell5Addr, sizeof(SpellRecord)))
      return false;

    const SpellRecord *cur =
        reinterpret_cast<const SpellRecord *>(g_spell5Addr);
    return RecordMatchesSettings(*cur);
  }

  void ResetSpell5HealProbeBattleRuntime() {
    // Treat ID5 data as battle-scoped too. Restore the row while the address is
    // still valid, but keep the user's persistent ON request so the next battle
    // can rebuild from its own live TrickData table.
    bool restored = false;
    if (g_applied && g_spell5Addr &&
        IsValidPtr(g_spell5Addr, sizeof(SpellRecord)))
      restored = WriteRecord(g_spell5Addr, g_spell5Original);

    if (g_applied) {
      AddLog(restored
                 ? u8"[책략5수명] 전투 종료: ID5 TrickData 원복 완료. ON 요청은 유지."
                 : u8"[책략5수명] 전투 종료: ID5 TrickData 주소가 유효하지 않아 쓰지 않고 폐기. ON 요청은 유지.");
    }

    g_applied = false;
    g_spell5Addr = 0;
    g_spell5Original = {};
    ResetDiagnostics();
  }

  void ResetSpell5HealProbeSession(uintptr_t oldP1, uintptr_t newP1) {
    if (!g_requested)
      return;

    AddLog(u8"[책략5수명] TrickData 적용 세대 초기화: p1 %p -> %p",
           reinterpret_cast<void *>(oldP1),
           reinterpret_cast<void *>(newP1));

    // Do not restore through an address from the old save generation.
    g_applied = false;
    g_spell5Addr = 0;
    g_spell5Original = {};
    ResetDiagnostics();
  }

  void RefreshSpell5HealProbe() {
    if (!g_requested)
      return;

    if (SetSpell5HealProbe(true))
      return;
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
      const Spell5CustomSettings settings = g_customSettings;
      if (settings.healAmount > 0 &&
          settings.effect1 == 10 &&
          settings.power1 > 0 &&
          morale > before) {
        const int delta = (int)morale - (int)before;
        const bool exactIncrease = (delta == settings.power1);
        const bool cappedIncrease =
            (morale == 100 && before < 100 &&
             ((int)before + settings.power1) >= 100);

        if (exactIncrease || cappedIncrease) {
          const uintptr_t troopsAddr = unit + kUnitTroopsOffset;
          uint16_t troopsBefore = 0;
          uint16_t troopsAfter = 0;
          uint16_t maxTroops = 0;
          bool healed = false;

          if (IsValidPtr(troopsAddr, sizeof(uint16_t)) &&
              IsValidPtr(unit + 0x18, sizeof(uintptr_t))) {
            const uintptr_t maxTroopsAddr =
                *reinterpret_cast<const uintptr_t *>(unit + 0x18);

            if (maxTroopsAddr &&
                IsValidPtr(maxTroopsAddr, sizeof(uint16_t))) {
              troopsBefore =
                  *reinterpret_cast<const uint16_t *>(troopsAddr);
              maxTroops =
                  *reinterpret_cast<const uint16_t *>(maxTroopsAddr);

              uint32_t next =
                  (uint32_t)troopsBefore +
                  (uint32_t)settings.healAmount;
              if (next > maxTroops)
                next = maxTroops;

              troopsAfter = (uint16_t)next;
              if (troopsAfter != troopsBefore) {
                *reinterpret_cast<uint16_t *>(troopsAddr) = troopsAfter;
                healed =
                    (*reinterpret_cast<const uint16_t *>(troopsAddr) ==
                     troopsAfter);
              }
            }
          }

          const uint8_t x = *reinterpret_cast<const uint8_t *>(unit + kUnitXOffset);
          const uint8_t y = *reinterpret_cast<const uint8_t *>(unit + kUnitYOffset);
          const uint32_t state =
              *reinterpret_cast<const uint32_t *>(unit + kUnitStateOffset);

          AddLog(u8"[책략5힐DBG] 후보[%d] unit=%p 전의=%u->%u(+%d) 병력=%u->%u / 최대=%u / 회복=%s 좌표=(%u,%u) 상태=%u",
                 i, reinterpret_cast<void *>(unit),
                 (unsigned)before, (unsigned)morale, delta,
                 (unsigned)troopsBefore, (unsigned)troopsAfter,
                 (unsigned)maxTroops, healed ? "YES" : "NO",
                 (unsigned)x, (unsigned)y, (unsigned)state);
        }
      }

      g_diag[i].unit = unit;
      g_diag[i].morale = morale;
      g_diag[i].valid = true;
    }

    if (!g_diagPrimed && validCount > 0) {
      g_diagPrimed = true;
      AddLog(u8"[책략5힐DBG] 전투부대 %d개 기준값 저장 완료. 현재 5번 책략 설정을 기준으로 추가 병력회복을 감시합니다.", validCount);
    }

    for (int i = unitCount; i < kMaxTrackedUnits; ++i)
      g_diag[i] = {};
  }
} // namespace DX11Base
