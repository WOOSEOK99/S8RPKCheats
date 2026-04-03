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
  //  100ms마다 0 유지
  // ───────────────────────────────────────────────

  bool g_roadBlockRunning = false;

  static bool g_roadBlockEnabled = false;
  static uint64_t g_roadOrigVal1 = 0;
  static uint64_t g_roadOrigVal2 = 0;
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

  // 기존 코드 교체
  static DWORD WINAPI RoadBlockThread(LPVOID) {
    while (g_roadBlockEnabled) {
      Sleep(100);
      if (!g_roadBlockEnabled)
        break;

      // 매번 포인터 재해석
      uintptr_t root = ResolveRoadRoot();
      if (!root)
        continue;

      uintptr_t addr1 = root + 0x7E30;
      uintptr_t addr2 = root + 0x8368;

      if (!IsValidPtr(addr1, 8) || !IsValidPtr(addr2, 8))
        continue;

      DWORD old, tmp;
      VirtualProtect((LPVOID)addr1, 8, PAGE_READWRITE, &old);
      *(uint64_t *)addr1 = 0;
      VirtualProtect((LPVOID)addr1, 8, old, &tmp);

      VirtualProtect((LPVOID)addr2, 8, PAGE_READWRITE, &old);
      *(uint64_t *)addr2 = 0;
      VirtualProtect((LPVOID)addr2, 8, old, &tmp);
    }

    g_roadBlockRunning = false;
    return 0;
  }

  void SetRoadBlock(bool enable) {
    if (enable) {
      if (g_roadBlockEnabled)
        return;

      uintptr_t root = ResolveRoadRoot();
      if (!root) {
        AddLog(u8"[도로차단] 포인터 해석 실패");
        return;
      }

      // g_roadAddr1/2 대신 로컬 변수 사용
      uintptr_t addr1 = root + 0x7E30;
      uintptr_t addr2 = root + 0x8368;

      if (!IsValidPtr(addr1, 8) || !IsValidPtr(addr2, 8)) {
        AddLog(u8"[도로차단] 주소 유효하지 않음");
        return;
      }

      // 원래값 저장
      g_roadOrigVal1 = *(uint64_t *)addr1;
      g_roadOrigVal2 = *(uint64_t *)addr2;

      // 최초 0으로 설정
      DWORD old, tmp;
      VirtualProtect((LPVOID)addr1, 8, PAGE_READWRITE, &old);
      *(uint64_t *)addr1 = 0;
      VirtualProtect((LPVOID)addr1, 8, old, &tmp);

      VirtualProtect((LPVOID)addr2, 8, PAGE_READWRITE, &old);
      *(uint64_t *)addr2 = 0;
      VirtualProtect((LPVOID)addr2, 8, old, &tmp);

      g_roadBlockEnabled = true;
      g_roadBlockRunning = true;

      g_roadThread = CreateThread(nullptr, 0, RoadBlockThread, nullptr, 0, nullptr);
      if (!g_roadThread) {
        g_roadBlockEnabled = false;
        g_roadBlockRunning = false;
        AddLog(u8"[도로차단] 스레드 생성 실패");
        return;
      }

      AddLog(u8"[도로차단] 건녕↔교지 차단 활성화");
    } else {
      g_roadBlockEnabled = false;

      if (g_roadThread) {
        WaitForSingleObject(g_roadThread, 500);
        CloseHandle(g_roadThread);
        g_roadThread = nullptr;
      }

      // 비활성화 시점에 포인터 새로 해석해서 복구
      uintptr_t root = ResolveRoadRoot();
      if (root) {
        uintptr_t addr1 = root + 0x7E30;
        uintptr_t addr2 = root + 0x8368;

        if (IsValidPtr(addr1, 8)) {
          DWORD old, tmp;
          VirtualProtect((LPVOID)addr1, 8, PAGE_READWRITE, &old);
          *(uint64_t *)addr1 = g_roadOrigVal1;
          VirtualProtect((LPVOID)addr1, 8, old, &tmp);
        }
        if (IsValidPtr(addr2, 8)) {
          DWORD old, tmp;
          VirtualProtect((LPVOID)addr2, 8, PAGE_READWRITE, &old);
          *(uint64_t *)addr2 = g_roadOrigVal2;
          VirtualProtect((LPVOID)addr2, 8, old, &tmp);
        }
      }

      AddLog(u8"[도로차단] 건녕↔교지 차단 해제");
    }
  }
} // namespace DX11Base