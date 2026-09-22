#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "Spell5HealProbe.h"

#include <cstdint>
#include <cstring>

namespace DX11Base {
  namespace {
#pragma pack(push, 1)
    struct SpellPrefix {
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
    };
#pragma pack(pop)

    static_assert(sizeof(SpellPrefix) == 0x16, "SpellPrefix layout mismatch");

    constexpr uintptr_t kSpellRootOffset = 0x034C8630;
    constexpr uintptr_t kSpellTableOffset = 0x0D38;
    constexpr uintptr_t kSpellStride = 0x20;

    static bool g_applied = false;
    static uintptr_t g_spell4Addr = 0;
    static uintptr_t g_spell5Addr = 0;
    static SpellPrefix g_spell4Original{};
    static SpellPrefix g_spell5Original{};

    static bool Deref(uintptr_t base, uintptr_t offset, uintptr_t &out) {
      const uintptr_t addr = base + offset;
      if (!IsValidPtr(addr, sizeof(uintptr_t)))
        return false;

      out = *reinterpret_cast<uintptr_t *>(addr);
      return IsValidPtr(out, sizeof(uintptr_t));
    }

    static const SpellPrefix *Entry(uintptr_t table, int index) {
      return reinterpret_cast<const SpellPrefix *>(table + (uintptr_t)index * kSpellStride);
    }

    static bool ValidateSpellTable(uintptr_t table) {
      if (!IsValidPtr(table, kSpellStride * 5))
        return false;

      for (int i = 0; i < 5; ++i) {
        const SpellPrefix *s = Entry(table, i);
        const int16_t expected = (int16_t)(i + 1);
        if (s->code1 != expected || s->code2 != expected || s->code3 != expected)
          return false;
      }

      const SpellPrefix *s1 = Entry(table, 0);
      const SpellPrefix *s2 = Entry(table, 1);
      const SpellPrefix *s3 = Entry(table, 2);
      const SpellPrefix *s4 = Entry(table, 3);

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

    static bool WritePrefix(uintptr_t addr, const SpellPrefix &value) {
      if (!IsValidPtr(addr, sizeof(SpellPrefix)))
        return false;

      DWORD oldProtect = 0;
      DWORD tmpProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(addr), sizeof(SpellPrefix), PAGE_READWRITE, &oldProtect))
        return false;

      std::memcpy(reinterpret_cast<void *>(addr), &value, sizeof(value));
      VirtualProtect(reinterpret_cast<LPVOID>(addr), sizeof(SpellPrefix), oldProtect, &tmpProtect);
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

      // Step 2:
      // The first probe only rewrote entry #4 to code 5. That was enough for the
      // battle-preparation picker to show/select code 5, but the in-battle list
      // dropped it. The likely lookup path is: selected ID 5 -> canonical entry #5.
      // Therefore keep the real #5 record populated as well, while using #4 as the
      // temporary selector surrogate. Only the confirmed 22-byte prefix is touched.
      const uintptr_t spell4 = table + 3 * kSpellStride;
      const uintptr_t spell5 = table + 4 * kSpellStride;
      if (!IsValidPtr(spell4, sizeof(SpellPrefix)) || !IsValidPtr(spell5, sizeof(SpellPrefix))) {
        AddLog(u8"[책략5DBG] 4/5번 책략 레코드 주소가 유효하지 않습니다.");
        return false;
      }

      std::memcpy(&g_spell4Original, reinterpret_cast<const void *>(spell4), sizeof(g_spell4Original));
      std::memcpy(&g_spell5Original, reinterpret_cast<const void *>(spell5), sizeof(g_spell5Original));

      // Probe profile:
      // - code 5 / ally target
      // - effect1=10 / power1=+40: known-good strategy effect, cast anchor
      // - effect2=20 / power2=2000: healing candidate (confirmed only in tactics so far)
      // - range=5
      const SpellPrefix probe{
          5, 5, 5,
          1,
          10, 40, 0,
          20, 2000, 0,
          5,
      };

      // Populate the canonical #5 definition first.
      if (!WritePrefix(spell5, probe)) {
        AddLog(u8"[책략5DBG] 실제 5번 책략 레코드 쓰기 실패.");
        return false;
      }

      // Then expose code 5 through the existing 4th selectable slot.
      if (!WritePrefix(spell4, probe)) {
        WritePrefix(spell5, g_spell5Original);
        AddLog(u8"[책략5DBG] 4번 선택 슬롯 치환 실패 - 5번 레코드는 원복했습니다.");
        return false;
      }

      g_spell4Addr = spell4;
      g_spell5Addr = spell5;
      g_applied = true;

      AddLog(u8"[책략5DBG] Step2 적용: table=%p, selector4=%p, canonical5=%p",
             reinterpret_cast<void *>(table), reinterpret_cast<void *>(spell4),
             reinterpret_cast<void *>(spell5));
      AddLog(u8"[책략5DBG] 4번 선택 슬롯과 실제 5번 정의를 동일하게 설정: 아군 / 사기+40 / 효과2=20 / 2000 / 범위5");
      AddLog(u8"[책략5DBG] 전투 준비에서 5번 선택 후, 실제 전투 책략 목록에 유지되는지 먼저 확인하세요.");
      AddLog(u8"[책략5DBG] 목록에 남으면 그 다음으로 병력 +2000 여부를 확인하세요.");
      return true;
    }

    if (!g_applied)
      return true;

    bool restored4 = false;
    bool restored5 = false;

    if (g_spell4Addr && IsValidPtr(g_spell4Addr, sizeof(SpellPrefix)))
      restored4 = WritePrefix(g_spell4Addr, g_spell4Original);
    if (g_spell5Addr && IsValidPtr(g_spell5Addr, sizeof(SpellPrefix)))
      restored5 = WritePrefix(g_spell5Addr, g_spell5Original);

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
