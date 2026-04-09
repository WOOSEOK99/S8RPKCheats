#include "RoadBlock.h"
#include "Cheats.h"
#include "pch.h"
#include "showlog.h"
#include <psapi.h>

namespace DX11Base {

  void AddLog(const char *fmt, ...);
  bool IsValidPtr(uintptr_t addr, SIZE_T size);

  // ───────────────────────────────────────────────
  //  도로 차단 - 건녕 ↔ 교지
  //  root + 0x7E30 = 건녕→교지
  //  root + 0x8368 = 교지→건녕
  //
  //  도로 차단 - 교지 ↔ 회계 (신규)
  //  root + 0x8370 = 교지→회계
  //  root + 0x7648 = 회계→교지
  //
  //  100ms마다 0 유지
  // ───────────────────────────────────────────────

  bool g_roadBlockRunning = false;

  static bool g_road1Enabled = false;
  static bool g_road2Enabled = false;
  static uint64_t g_road1OrigVal1 = 0;
  static uint64_t g_road1OrigVal2 = 0;
  static uint64_t g_road2OrigVal1 = 0;
  static uint64_t g_road2OrigVal2 = 0;
  static HANDLE g_roadThread = nullptr;

  static uintptr_t ResolveRoadRoot() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return 0;

    uintptr_t p = *(uintptr_t *)(exeBase + 0x034C8630);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x0);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x8);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x10);
    if (!IsValidPtr(p, 8))
      return 0;
    p = *(uintptr_t *)(p + 0x0);
    if (!IsValidPtr(p, 8))
      return 0;

    return p;
  }

  static void WriteZeroToAddr(uintptr_t addr) {
    if (!IsValidPtr(addr, 8))
      return;
    DWORD old, tmp;
    VirtualProtect((LPVOID)addr, 8, PAGE_READWRITE, &old);
    *(uint64_t *)addr = 0;
    VirtualProtect((LPVOID)addr, 8, old, &tmp);
  }

  static void RestoreAddr(uintptr_t addr, uint64_t origVal) {
    if (!IsValidPtr(addr, 8))
      return;
    DWORD old, tmp;
    VirtualProtect((LPVOID)addr, 8, PAGE_READWRITE, &old);
    *(uint64_t *)addr = origVal;
    VirtualProtect((LPVOID)addr, 8, old, &tmp);
  }

  static DWORD WINAPI RoadBlockThread(LPVOID) {
    while (g_road1Enabled || g_road2Enabled) {
      Sleep(100);
      if (!g_road1Enabled && !g_road2Enabled)
        break;

      uintptr_t root = ResolveRoadRoot();
      if (!root)
        continue;

      if (g_road1Enabled) {
        WriteZeroToAddr(root + 0x7E30);
        WriteZeroToAddr(root + 0x8368);
      }
      if (g_road2Enabled) {
        WriteZeroToAddr(root + 0x8370);
        WriteZeroToAddr(root + 0x7648);
      }
    }

    g_roadBlockRunning = false;
    return 0;
  }

  static void StartRoadThread() {
    if (!g_roadThread) {
      g_roadBlockRunning = true;
      g_roadThread = CreateThread(nullptr, 0, RoadBlockThread, nullptr, 0, nullptr);
    }
  }

  static void StopRoadThread() {
    if (g_roadThread) {
      if (!g_road1Enabled && !g_road2Enabled) {
        WaitForSingleObject(g_roadThread, 500);
        CloseHandle(g_roadThread);
        g_roadThread = nullptr;
      }
    }
  }

  void SetRoadBlock(bool enable) {
    if (enable) {
      if (g_road1Enabled)
        return;

      uintptr_t root = ResolveRoadRoot();
      if (!root) {
        AddLog(u8"[도로차단] 포인터 해석 실패 (건녕↔교지)");
        return;
      }

      g_road1OrigVal1 = *(uint64_t *)(root + 0x7E30);
      g_road1OrigVal2 = *(uint64_t *)(root + 0x8368);

      g_road1Enabled = true;
      StartRoadThread();
      AddLog(u8"[도로차단] 건녕↔교지 차단 활성화");
    } else {
      if (!g_road1Enabled)
        return;
      g_road1Enabled = false;
      
      uintptr_t root = ResolveRoadRoot();
      if (root) {
        RestoreAddr(root + 0x7E30, g_road1OrigVal1);
        RestoreAddr(root + 0x8368, g_road1OrigVal2);
      }
      StopRoadThread();
      AddLog(u8"[도로차단] 건녕↔교지 차단 해제");
    }
  }

  void SetRoadBlock2(bool enable) {
    if (enable) {
      if (g_road2Enabled)
        return;

      uintptr_t root = ResolveRoadRoot();
      if (!root) {
        AddLog(u8"[도로차단] 포인터 해석 실패 (교지↔회계)");
        return;
      }

      g_road2OrigVal1 = *(uint64_t *)(root + 0x8370);
      g_road2OrigVal2 = *(uint64_t *)(root + 0x7648);

      g_road2Enabled = true;
      StartRoadThread();
      AddLog(u8"[도로차단] 교지↔회계 차단 활성화");
    } else {
      if (!g_road2Enabled)
        return;
      g_road2Enabled = false;

      uintptr_t root = ResolveRoadRoot();
      if (root) {
        RestoreAddr(root + 0x8370, g_road2OrigVal1);
        RestoreAddr(root + 0x7648, g_road2OrigVal2);
      }
      StopRoadThread();
      AddLog(u8"[도로차단] 교지↔회계 차단 해제");
    }
  }
} // namespace DX11Base