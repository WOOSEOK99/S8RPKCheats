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
    static uintptr_t g_patchedAddr = 0;
    static SpellPrefix g_original{};

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

      // Keep the active 4th record's +16..+1F tail untouched.
      // Only replace the confirmed 22-byte prefix so the existing live metadata remains intact.
      const uintptr_t spell4 = table + 3 * kSpellStride;
      if (!IsValidPtr(spell4, sizeof(SpellPrefix))) {
        AddLog(u8"[책략5DBG] 4번 책략 레코드 주소가 유효하지 않습니다.");
        return false;
      }

      std::memcpy(&g_original, reinterpret_cast<const void *>(spell4), sizeof(g_original));

      // Probe profile:
      // - code 4 -> code 5: asks the game to expose the unused #5 resource through the 4th slot.
      // - effect1=10 / power1=+40: known-good ally morale effect, used as a safe cast anchor.
      // - effect2=20 / power2=2000: effect 20 is confirmed as healing in tactics, but NOT yet in strategy.
      // - range=5: conservative first test.
      const SpellPrefix probe{
          5, 5, 5,
          1,
          10, 40, 0,
          20, 2000, 0,
          5,
      };

      if (!WritePrefix(spell4, probe)) {
        AddLog(u8"[책략5DBG] 5번 힐 실험 데이터 쓰기 실패.");
        return false;
      }

      g_patchedAddr = spell4;
      g_applied = true;

      AddLog(u8"[책략5DBG] 적용: table=%p, slot4=%p", reinterpret_cast<void *>(table),
             reinterpret_cast<void *>(spell4));
      AddLog(u8"[책략5DBG] 4번 슬롯 -> 코드5 / 아군 / 사기+40 / 효과2=20(치료 후보) / 2000 / 범위5");
      AddLog(u8"[책략5DBG] 주의: 책략 엔진에서 효과20은 아직 미확인입니다. 전투에서 실제 병력 회복 여부를 확인하세요.");
      return true;
    }

    if (!g_applied)
      return true;

    bool restored = false;
    if (g_patchedAddr && IsValidPtr(g_patchedAddr, sizeof(SpellPrefix))) {
      restored = WritePrefix(g_patchedAddr, g_original);
    }

    if (restored)
      AddLog(u8"[책략5DBG] 원복 완료: 기존 사모위계 22바이트 복구.");
    else
      AddLog(u8"[책략5DBG] 원복 주소가 더 이상 유효하지 않습니다. 전투/화면 전환으로 테이블이 재생성되었을 수 있습니다.");

    g_applied = false;
    g_patchedAddr = 0;
    g_original = {};
    return true;
  }
} // namespace DX11Base
