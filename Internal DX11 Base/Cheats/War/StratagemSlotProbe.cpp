#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "StratagemSlotProbe.h"

#include <cstdint>
#include <cstring>

namespace DX11Base {
  namespace {
    static bool IsWritableProtect(DWORD protect) {
      const DWORD base = protect & 0xFF;
      return base == PAGE_READWRITE ||
             base == PAGE_WRITECOPY ||
             base == PAGE_EXECUTE_READWRITE ||
             base == PAGE_EXECUTE_WRITECOPY;
    }

    static void LogWindow(uintptr_t addr, size_t bytes) {
      if (!IsValidPtr(addr, bytes))
        return;

      char line[512] = {};
      int pos = 0;
      for (size_t i = 0; i < bytes && pos < (int)sizeof(line) - 4; ++i) {
        pos += sprintf_s(line + pos, sizeof(line) - pos, "%02X ", *(uint8_t *)(addr + i));
      }
      AddLog(u8"[책략5슬롯DBG] bytes @ %p : %s", (void *)addr, line);
    }

    static bool HasNearbyCount4(uintptr_t addr, uintptr_t regionStart, uintptr_t regionEnd) {
      const uintptr_t from = (addr > regionStart + 0x20) ? addr - 0x20 : regionStart;
      const uintptr_t to = (addr + 0x40 < regionEnd) ? addr + 0x40 : regionEnd;

      for (uintptr_t p = from; p + 4 <= to; ++p) {
        if (*(uint8_t *)p == 4)
          return true;
        if ((p & 1) == 0 && p + 2 <= to && *(uint16_t *)p == 4)
          return true;
        if ((p & 3) == 0 && p + 4 <= to && *(uint32_t *)p == 4)
          return true;
      }
      return false;
    }
  }

  void ScanStratagemFiveSlotCandidates() {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    uintptr_t p = (uintptr_t)si.lpMinimumApplicationAddress;
    const uintptr_t maxAddr = (uintptr_t)si.lpMaximumApplicationAddress;

    int byteCandidates = 0;
    int shortCandidates = 0;
    int logged = 0;
    constexpr int kMaxLogs = 80;

    AddLog(u8"[책략5슬롯DBG] 스캔 시작: 전투 준비에서 기본 책략 1,2,3,4를 모두 선택한 상태를 기준으로 찾습니다.");

    while (p < maxAddr) {
      MEMORY_BASIC_INFORMATION mbi{};
      if (VirtualQuery((LPCVOID)p, &mbi, sizeof(mbi)) != sizeof(mbi))
        break;

      const uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
      const uintptr_t regionEnd = regionStart + mbi.RegionSize;

      const bool scan =
          mbi.State == MEM_COMMIT &&
          !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
          IsWritableProtect(mbi.Protect) &&
          mbi.RegionSize >= 0x20 &&
          mbi.RegionSize <= (64ull * 1024ull * 1024ull);

      if (scan) {
        __try {
          const uint8_t *b = (const uint8_t *)regionStart;
          const size_t n = mbi.RegionSize;

          // byte array candidate: 01 02 03 04 [00/FF/...]
          for (size_t i = 0; i + 8 <= n; ++i) {
            if (b[i + 0] == 1 && b[i + 1] == 2 &&
                b[i + 2] == 3 && b[i + 3] == 4) {
              const uintptr_t a = regionStart + i;
              ++byteCandidates;
              if (logged < kMaxLogs && HasNearbyCount4(a, regionStart, regionEnd)) {
                const uint8_t fifth = b[i + 4];
                AddLog(u8"[책략5슬롯DBG] BYTE 후보 #%d addr=%p fifth=%u region=%p size=0x%llX",
                       byteCandidates, (void *)a, (unsigned)fifth,
                       (void *)regionStart, (unsigned long long)mbi.RegionSize);
                const uintptr_t w = (a >= regionStart + 0x10) ? a - 0x10 : regionStart;
                const size_t remain = (size_t)(regionEnd - w);
                LogWindow(w, remain >= 0x30 ? 0x30 : remain);
                ++logged;
              }
            }
          }

          // uint16 array candidate: 1,2,3,4
          for (size_t i = 0; i + 16 <= n; i += 2) {
            const uint16_t *s = (const uint16_t *)(b + i);
            if (s[0] == 1 && s[1] == 2 && s[2] == 3 && s[3] == 4) {
              const uintptr_t a = regionStart + i;
              ++shortCandidates;
              if (logged < kMaxLogs && HasNearbyCount4(a, regionStart, regionEnd)) {
                const uint16_t fifth = s[4];
                AddLog(u8"[책략5슬롯DBG] WORD 후보 #%d addr=%p fifth=%u region=%p size=0x%llX",
                       shortCandidates, (void *)a, (unsigned)fifth,
                       (void *)regionStart, (unsigned long long)mbi.RegionSize);
                const uintptr_t w = (a >= regionStart + 0x10) ? a - 0x10 : regionStart;
                const size_t remain = (size_t)(regionEnd - w);
                LogWindow(w, remain >= 0x30 ? 0x30 : remain);
                ++logged;
              }
            }
          }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
          // Skip regions that change while scanning.
        }
      }

      if (regionEnd <= p)
        break;
      p = regionEnd;
    }

    AddLog(u8"[책략5슬롯DBG] 스캔 완료: BYTE=%d WORD=%d / 로그=%d. 후보가 많으면 선택 순서를 바꿔 재스캔합니다.",
           byteCandidates, shortCandidates, logged);
  }
} // namespace DX11Base
