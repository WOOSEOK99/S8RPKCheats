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
    constexpr uint16_t kSpell5HealAmount = 2000;

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
          if (cur->code1 == 5 && cur->code2 == 5 && cur->code3 == 5 &&
              cur->target == 1 &&
              cur->effect1 == 10 && cur->power1 == 40 &&
              cur->effect2 == 0 && cur->power2 == 0 &&
              cur->range == 5) {
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
        AddLog(u8"[책략5DBG] 책략 테이블 해석/검증 실패. 현재 데이터가 원본 상태인지 확인하세요.");
        return false;
      }

      const uintptr_t spell2 = table + 1 * kSpellStride;
      const uintptr_t spell5 = table + 4 * kSpellStride;
      if (!IsValidPtr(spell2, sizeof(SpellRecord)) ||
          !IsValidPtr(spell5, sizeof(SpellRecord))) {
        AddLog(u8"[책략5DBG] 2/5번 책략 레코드 주소가 유효하지 않습니다.");
        return false;
      }

      std::memcpy(&g_spell5Original, reinterpret_cast<const void *>(spell5), sizeof(g_spell5Original));

      SpellRecord clone{};
      std::memcpy(&clone, reinterpret_cast<const void *>(spell2), sizeof(clone));

      // Known working carrier:
      // - effect1=10 / +40 is confirmed to work as an ally morale effect.
      // - effect2=20 was confirmed NOT to heal troops in real-game testing,
      //   so keep secondary effect disabled. Healing is injected separately
      //   after the affected unit is identified from the +40 morale change.
      clone.code1 = 5;
      clone.code2 = 5;
      clone.code3 = 5;
      clone.target = 1;
      clone.effect1 = 10;
      clone.power1 = 40;
      clone.duration1 = 0;
      clone.effect2 = 0;
      clone.power2 = 0;
      clone.duration2 = 0;
      clone.range = 5;

      if (!WriteRecord(spell5, clone)) {
        AddLog(u8"[책략5DBG] 실제 5번 레코드에 실험 데이터 쓰기 실패.");
        return false;
      }

      // 이번 단계에서는 4번 사모위계를 절대 치환하지 않습니다.
      // 실제 5번 횟수 슬롯(+14C)을 켰을 때 5번째 책략이 독립적으로
      // 노출되는지 확인하기 위한 테스트입니다.
      g_spell5Addr = spell5;
      g_applied = true;
      ResetDiagnostics();

      AddLog(u8"[책략5DBG] 적용: 아군 / 효과1 사기+40 / 효과2 없음 / 범위5");
      AddLog(u8"[책략5DBG] table=%p canonical5=%p source2=%p / 4번 사모위계 유지",
             reinterpret_cast<void *>(table),
             reinterpret_cast<void *>(spell5), reinterpret_cast<void *>(spell2));
      AddLog(u8"[책략5DBG] 광역힐 2차 테스트 활성화: 전의 +40(또는 100 상한) 변화 부대에 병력 +2000(최대병력 상한) 적용.");
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

  void ResetSpell5HealProbeBattleRuntime() {
    // The TrickData table belongs to the current game/save generation and may
    // remain valid across battles. Only the unit-list/morale snapshots are
    // battle-local and must not leak into the next battle.
    ResetDiagnostics();
    AddLog(u8"[책략5수명] 전투 종료 확정: 5번 책략 대상/힐 진단 상태 초기화.");
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
      if (morale > before) {
        const int delta = (int)morale - (int)before;
        const bool exactPlus40 = (delta == 40);
        const bool cappedPlus40 =
            (morale == 100 && before < 100 && ((int)before + 40) >= 100);

        if (exactPlus40 || cappedPlus40) {
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

              uint32_t next = (uint32_t)troopsBefore + kSpell5HealAmount;
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
      AddLog(u8"[책략5힐DBG] 전투부대 %d개 기준값 저장 완료. 사기 60 이하의 부대를 범위에 두고 5번 책략을 사용하세요.", validCount);
    }

    for (int i = unitCount; i < kMaxTrackedUnits; ++i)
      g_diag[i] = {};
  }
} // namespace DX11Base
