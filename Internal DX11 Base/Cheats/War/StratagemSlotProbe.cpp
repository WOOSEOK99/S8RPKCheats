#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "StratagemSlotProbe.h"

#include <cstdint>
#include <cstring>

namespace DX11Base {
  namespace {
    static volatile LONG g_scanRunning = 0;

    static bool IsWritableProtect(DWORD protect) {
      const DWORD base = protect & 0xFF;
      return base == PAGE_READWRITE ||
             base == PAGE_WRITECOPY ||
             base == PAGE_EXECUTE_READWRITE ||
             base == PAGE_EXECUTE_WRITECOPY;
    }

    static bool HasNearbyCount4(uintptr_t addr, uintptr_t regionStart, uintptr_t regionEnd) {
      const uintptr_t from = (addr > regionStart + 0x20) ? addr - 0x20 : regionStart;
      const uintptr_t to = (addr + 0x40 < regionEnd) ? addr + 0x40 : regionEnd;

      for (uintptr_t p = from; p + 4 <= to; ++p) {
        if (*(const uint8_t *)p == 4)
          return true;
        if ((p & 1) == 0 && p + 2 <= to && *(const uint16_t *)p == 4)
          return true;
        if ((p & 3) == 0 && p + 4 <= to && *(const uint32_t *)p == 4)
          return true;
      }
      return false;
    }

    static void LogWindow(uintptr_t addr, uintptr_t regionStart, uintptr_t regionEnd) {
      const uintptr_t from = (addr > regionStart + 0x10) ? addr - 0x10 : regionStart;
      const size_t remain = (size_t)(regionEnd - from);
      const size_t bytes = remain >= 0x30 ? 0x30 : remain;
      if (!IsValidPtr(from, bytes))
        return;

      char line[512] = {};
      int pos = 0;
      for (size_t i = 0; i < bytes && pos < (int)sizeof(line) - 4; ++i)
        pos += sprintf_s(line + pos, sizeof(line) - pos, "%02X ", *(const uint8_t *)(from + i));

      AddLog(u8"[책략5슬롯DBG] bytes @ %p : %s", (void *)from, line);
    }

    static bool IsPlausibleEmpty8(uint8_t v) {
      return v == 0 || v == 0xFF;
    }

    static bool IsPlausibleEmpty16(uint16_t v) {
      return v == 0 || v == 0xFFFF;
    }

    static bool IsPlausibleEmpty32(uint32_t v) {
      return v == 0 || v == 0xFFFFFFFFu;
    }

    static DWORD WINAPI ScanThreadProc(LPVOID) {
      SYSTEM_INFO si{};
      GetSystemInfo(&si);

      uintptr_t p = (uintptr_t)si.lpMinimumApplicationAddress;
      const uintptr_t maxAddr = (uintptr_t)si.lpMaximumApplicationAddress;

      int byteCandidates = 0;
      int wordCandidates = 0;
      int dwordCandidates = 0;
      int logged = 0;
      constexpr int kMaxLogs = 40;

      AddLog(u8"[책략5슬롯DBG] 경량 스캔 시작: 기존 1,2,3,4 뒤에 빈 5번째 칸(0/FFFF)만 찾습니다.");
      AddLog(u8"[책략5슬롯DBG] 이전 로그의 1..17 연속 배열은 일반 ID 테이블로 판정하여 제외합니다.");

      while (p < maxAddr) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery((LPCVOID)p, &mbi, sizeof(mbi)) != sizeof(mbi))
          break;

        const uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
        const uintptr_t regionEnd = regionStart + mbi.RegionSize;

        const bool scan =
            mbi.State == MEM_COMMIT &&
            mbi.Type == MEM_PRIVATE &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            IsWritableProtect(mbi.Protect) &&
            mbi.RegionSize >= 0x20 &&
            mbi.RegionSize <= (128ull * 1024ull * 1024ull);

        if (scan) {
          __try {
            const uint8_t *b = (const uint8_t *)regionStart;
            const size_t n = mbi.RegionSize;

            // BYTE layouts: 1,2,3,4,empty OR zero-based 0,1,2,3,empty.
            for (size_t i = 0; i + 5 <= n; ++i) {
              const bool oneBased =
                  b[i] == 1 && b[i + 1] == 2 && b[i + 2] == 3 && b[i + 3] == 4 &&
                  IsPlausibleEmpty8(b[i + 4]);
              const bool zeroBased =
                  b[i] == 0 && b[i + 1] == 1 && b[i + 2] == 2 && b[i + 3] == 3 &&
                  IsPlausibleEmpty8(b[i + 4]);

              if ((oneBased || zeroBased) &&
                  HasNearbyCount4(regionStart + i, regionStart, regionEnd)) {
                ++byteCandidates;
                if (logged < kMaxLogs) {
                  AddLog(u8"[책략5슬롯DBG] BYTE 후보 #%d addr=%p mode=%s fifth=%u",
                         byteCandidates, (void *)(regionStart + i),
                         oneBased ? "1-based" : "0-based",
                         (unsigned)b[i + 4]);
                  LogWindow(regionStart + i, regionStart, regionEnd);
                  ++logged;
                }
              }
            }

            // WORD layout.
            for (size_t i = 0; i + 10 <= n; i += 2) {
              const uint16_t *s = (const uint16_t *)(b + i);
              const bool oneBased =
                  s[0] == 1 && s[1] == 2 && s[2] == 3 && s[3] == 4 &&
                  IsPlausibleEmpty16(s[4]);
              const bool zeroBased =
                  s[0] == 0 && s[1] == 1 && s[2] == 2 && s[3] == 3 &&
                  IsPlausibleEmpty16(s[4]);

              if ((oneBased || zeroBased) &&
                  HasNearbyCount4(regionStart + i, regionStart, regionEnd)) {
                ++wordCandidates;
                if (logged < kMaxLogs) {
                  AddLog(u8"[책략5슬롯DBG] WORD 후보 #%d addr=%p mode=%s fifth=%u",
                         wordCandidates, (void *)(regionStart + i),
                         oneBased ? "1-based" : "0-based",
                         (unsigned)s[4]);
                  LogWindow(regionStart + i, regionStart, regionEnd);
                  ++logged;
                }
              }
            }

            // DWORD layout.
            for (size_t i = 0; i + 20 <= n; i += 4) {
              const uint32_t *d = (const uint32_t *)(b + i);
              const bool oneBased =
                  d[0] == 1 && d[1] == 2 && d[2] == 3 && d[3] == 4 &&
                  IsPlausibleEmpty32(d[4]);
              const bool zeroBased =
                  d[0] == 0 && d[1] == 1 && d[2] == 2 && d[3] == 3 &&
                  IsPlausibleEmpty32(d[4]);

              if ((oneBased || zeroBased) &&
                  HasNearbyCount4(regionStart + i, regionStart, regionEnd)) {
                ++dwordCandidates;
                if (logged < kMaxLogs) {
                  AddLog(u8"[책략5슬롯DBG] DWORD 후보 #%d addr=%p mode=%s fifth=%u",
                         dwordCandidates, (void *)(regionStart + i),
                         oneBased ? "1-based" : "0-based",
                         (unsigned)d[4]);
                  LogWindow(regionStart + i, regionStart, regionEnd);
                  ++logged;
                }
              }
            }
          } __except (EXCEPTION_EXECUTE_HANDLER) {
          }

          // Yield between heap regions so the game/UI remains responsive.
          Sleep(0);
        }

        if (regionEnd <= p)
          break;
        p = regionEnd;
      }

      AddLog(u8"[책략5슬롯DBG] 경량 스캔 완료: BYTE=%d WORD=%d DWORD=%d / 로그=%d",
             byteCandidates, wordCandidates, dwordCandidates, logged);
      if (byteCandidates == 0 && wordCandidates == 0 && dwordCandidates == 0) {
        AddLog(u8"[책략5슬롯DBG] 연속 선택배열 후보 없음 -> 다음은 '선택 배열'이 아니라 UI/루프의 4개 제한을 찾는 방향으로 전환합니다.");
      }

      InterlockedExchange(&g_scanRunning, 0);
      return 0;
    }
  }

  void ScanStratagemFiveSlotCandidates() {
    if (InterlockedCompareExchange(&g_scanRunning, 1, 0) != 0) {
      AddLog(u8"[책략5슬롯DBG] 이미 스캔 중입니다.");
      return;
    }

    HANDLE h = CreateThread(nullptr, 0, ScanThreadProc, nullptr, 0, nullptr);
    if (!h) {
      InterlockedExchange(&g_scanRunning, 0);
      AddLog(u8"[책략5슬롯DBG] 스캔 스레드 생성 실패.");
      return;
    }

    CloseHandle(h);
    AddLog(u8"[책략5슬롯DBG] 백그라운드 스캔 시작. 게임 화면은 계속 조작할 수 있습니다.");
  }
} // namespace DX11Base
