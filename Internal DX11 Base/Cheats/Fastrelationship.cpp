
#include "pch.h"

#include "Fastrelationship.h"
#include "Cheats.h"
#include "Infinitetalk.h"
#include "InstantLoveCave.h"
#include "Loyaltycave.h"
#include "Resonancecave.h"
#include "showlog.h"

#include <psapi.h>
#include <string>
#include <vector>

namespace DX11Base {

// ───────────────────────────────────────────────
  //  빠른 관계 증가 패치 (cave 방식)
  //  원본: add eax, r8d / cmp eax, r14d  (6바이트)
  //  패치: r8d를 32로 강제 설정 후 원본 실행
  //  효과: 경애 장수와 담화 시 관계치 급상승
  // ───────────────────────────────────────────────

  static uintptr_t g_relHookAddr = 0;
  static uint8_t g_relOriginal[6] = {};
  static uintptr_t g_relCaveAddr = 0;
  static bool g_relApplied = false;

  void SetFastRelationship(bool enable) {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return;

    if (!g_relHookAddr) {
      uintptr_t found = FindPattern(exeBase, exeBase + 0x3000000, "41 03 C0 41 3B C6 41");
      if (found) g_relHookAddr = found;
    }
    if (!g_relHookAddr)
      return;

    if (!g_relApplied && enable) {
      // ... (existing code omitted for brevity in this mock-up, replace_file_content uses exact matches)
    } else if (g_relApplied && !enable) {
      RestoreBytes(g_relHookAddr, g_relOriginal, 6);
      VirtualFree((LPVOID)g_relCaveAddr, 0, MEM_RELEASE);
      g_relCaveAddr = 0;
      g_relApplied = false;
    }
  }

}
