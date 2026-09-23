#include "../../pch.h"

#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"
#include "StratagemSlotProbe.h"

#include <psapi.h>
#include <cstdint>
#include <cstring>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kSideOffset = 0x18;
    constexpr uintptr_t kGaugeOffset = 0x154;
    constexpr uintptr_t kFirstStratagemCountOffset = 0x10C;
    constexpr uintptr_t kStratagemCountStride = 0x10;
    constexpr int kStratagemCountSlots = 10;

    static uintptr_t g_hookAddr = 0;
    static uintptr_t g_caveAddr = 0;
    static uint8_t g_original[5] = {};
    static bool g_hookApplied = false;

    static volatile uintptr_t g_attackInfo = 0;
    static volatile uintptr_t g_defenseInfo = 0;

    static bool BuildCaptureCave(uintptr_t hookAddr) {
      g_caveAddr = AllocNear(hookAddr, 128);
      if (!g_caveAddr)
        return false;

      uint8_t *cave = reinterpret_cast<uint8_t *>(g_caveAddr);
      int idx = 0;

      auto emit8 = [&](uint8_t v) { cave[idx++] = v; };
      auto emit64 = [&](uintptr_t v) {
        *reinterpret_cast<uintptr_t *>(&cave[idx]) = v;
        idx += 8;
      };

      emit8(0x52); // push rdx

      // cmp byte ptr [rax+18],0
      emit8(0x80); emit8(0x78); emit8(0x18); emit8(0x00);
      emit8(0x75); const int jneDefense = idx++;

      // attack -> g_attackInfo
      emit8(0x48); emit8(0xBA); emit64(reinterpret_cast<uintptr_t>(&g_attackInfo));
      emit8(0x48); emit8(0x89); emit8(0x02);
      emit8(0xEB); const int jmpDoneAttack = idx++;

      const int checkDefense = idx;

      // cmp byte ptr [rax+18],1
      emit8(0x80); emit8(0x78); emit8(0x18); emit8(0x01);
      emit8(0x75); const int jneDone = idx++;

      // defense -> g_defenseInfo
      emit8(0x48); emit8(0xBA); emit64(reinterpret_cast<uintptr_t>(&g_defenseInfo));
      emit8(0x48); emit8(0x89); emit8(0x02);

      const int done = idx;

      emit8(0x5A); // pop rdx
      emit8(0x0F); emit8(0xB6); emit8(0x40); emit8(0x18); // original movzx eax,[rax+18]
      emit8(0xC3); // original ret

      auto patchRel8 = [&](int dispIndex, int target) -> bool {
        const int rel = target - (dispIndex + 1);
        if (rel < -128 || rel > 127)
          return false;
        cave[dispIndex] = static_cast<uint8_t>(static_cast<int8_t>(rel));
        return true;
      };

      if (!patchRel8(jneDefense, checkDefense) ||
          !patchRel8(jmpDoneAttack, done) ||
          !patchRel8(jneDone, done)) {
        VirtualFree(reinterpret_cast<LPVOID>(g_caveAddr), 0, MEM_RELEASE);
        g_caveAddr = 0;
        return false;
      }

      FlushInstructionCache(GetCurrentProcess(), cave, idx);
      return ApplyJmp(hookAddr, g_caveAddr, 5);
    }

    static bool EnsureCaptureHook() {
      if (g_hookApplied)
        return true;

      const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
      if (!exeBase)
        return false;

      MODULEINFO mi{};
      if (!GetModuleInformation(GetCurrentProcess(),
                                reinterpret_cast<HMODULE>(exeBase),
                                &mi, sizeof(mi)))
        return false;

      const uintptr_t imageEnd = exeBase + static_cast<uintptr_t>(mi.SizeOfImage);

      // CT battle-info getter:
      // 48 8B 41 08
      // 48 8B 48 30
      // 48 8B 01
      // 0F B6 40 18
      // C3
      const char *pattern =
          "48 8B 41 08 48 8B 48 30 48 8B 01 0F B6 40 18 C3";
      const uintptr_t found = FindPattern(exeBase, imageEnd, pattern);
      if (!found) {
        AddLog(u8"[책략5슬롯DBG] 전장 정보 getter 패턴을 찾지 못했습니다.");
        return false;
      }

      g_hookAddr = found + 0x0B;
      static const uint8_t expected[5] = {
          0x0F, 0xB6, 0x40, 0x18, 0xC3
      };

      if (!IsValidPtr(g_hookAddr, sizeof(expected)) ||
          std::memcmp(reinterpret_cast<const void *>(g_hookAddr),
                      expected, sizeof(expected)) != 0) {
        AddLog(u8"[책략5슬롯DBG] getter 검증 실패: %p",
               reinterpret_cast<void *>(g_hookAddr));
        g_hookAddr = 0;
        return false;
      }

      std::memcpy(g_original,
                  reinterpret_cast<const void *>(g_hookAddr),
                  sizeof(g_original));

      if (!BuildCaptureCave(g_hookAddr)) {
        AddLog(u8"[책략5슬롯DBG] 전장 정보 캡처 훅 설치 실패.");
        g_hookAddr = 0;
        g_caveAddr = 0;
        return false;
      }

      g_hookApplied = true;
      g_attackInfo = 0;
      g_defenseInfo = 0;

      AddLog(u8"[책략5슬롯DBG] 전장 정보 캡처 훅 설치 완료.");
      AddLog(u8"[책략5슬롯DBG] 책략 선택 화면을 한 번 열거나 수량을 변경한 뒤 이 버튼을 다시 누르세요.");
      return true;
    }

    static bool ValidateInfo(uintptr_t ptr, uint8_t expectedSide) {
      if (!ptr)
        return false;

      const uintptr_t lastCount =
          ptr + kFirstStratagemCountOffset +
          (kStratagemCountSlots - 1) * kStratagemCountStride;

      if (!IsValidPtr(ptr + kSideOffset, 1) ||
          !IsValidPtr(ptr + kGaugeOffset, sizeof(uint16_t)) ||
          !IsValidPtr(lastCount, 1))
        return false;

      return *reinterpret_cast<const uint8_t *>(ptr + kSideOffset) ==
             expectedSide;
    }

    static void DumpSide(const char *name, uintptr_t ptr, uint8_t side) {
      if (!ValidateInfo(ptr, side))
        return;

      uint8_t counts[kStratagemCountSlots] = {};
      for (int i = 0; i < kStratagemCountSlots; ++i) {
        counts[i] = *reinterpret_cast<const uint8_t *>(
            ptr + kFirstStratagemCountOffset +
            (uintptr_t)i * kStratagemCountStride);
      }

      const uint16_t gauge =
          *reinterpret_cast<const uint16_t *>(ptr + kGaugeOffset);

      AddLog(
          u8"[책략5슬롯DBG] %s ptr=%p side=%u gauge=%u / "
          "책략수량 ID1~10 = %u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
          name, reinterpret_cast<void *>(ptr), (unsigned)side,
          (unsigned)gauge,
          (unsigned)counts[0], (unsigned)counts[1],
          (unsigned)counts[2], (unsigned)counts[3],
          (unsigned)counts[4], (unsigned)counts[5],
          (unsigned)counts[6], (unsigned)counts[7],
          (unsigned)counts[8], (unsigned)counts[9]);

      AddLog(
          u8"[책략5슬롯DBG] offsets: "
          "ID1=+10C ID2=+11C ID3=+12C ID4=+13C "
          "ID5후보=+14C ID6=+15C ... ID10=+19C");
    }
  }

  void ScanStratagemFiveSlotCandidates() {
    if (!EnsureCaptureHook())
      return;

    bool dumped = false;

    const uintptr_t attack = g_attackInfo;
    if (ValidateInfo(attack, 0)) {
      DumpSide(u8"공격측", attack, 0);
      dumped = true;
    }

    const uintptr_t defense = g_defenseInfo;
    if (ValidateInfo(defense, 1)) {
      DumpSide(u8"수비측", defense, 1);
      dumped = true;
    }

    if (!dumped) {
      AddLog(u8"[책략5슬롯DBG] 아직 전장 정보 포인터가 잡히지 않았습니다.");
      AddLog(u8"[책략5슬롯DBG] 책략 선택 화면을 열고 수량을 한 번 변경한 뒤 다시 누르세요.");
    }
  }
} // namespace DX11Base
