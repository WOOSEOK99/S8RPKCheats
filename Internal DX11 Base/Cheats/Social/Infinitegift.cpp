#include "pch.h"

#include "Cheats.h"
#include "InstantLoveCave.h"
#include "Loyaltycave.h"
#include "Resonancecave.h"
#include "showlog.h"

#include <psapi.h>
#include <string>
#include <vector>

namespace DX11Base {
  // ───────────────────────────────────────────────
  //  무한 기증 패치
  //  원본: or [rsi+0x320], 0x400   (10바이트) - 기증 완료 플래그 세팅
  //  패치: and [rsi+0x320], 0xFFFFFBFF        - 플래그 클리어 (항상 기증 가능)
  // ───────────────────────────────────────────────

  static uintptr_t g_giftHookAddr = 0;
  static uint8_t g_giftOriginal[10] = {};
  static bool g_giftApplied = false;

  // ───────────────────────────────────────────────
  // 교류 상태 객체 캡처 (디버그 전용)
  // 기증 완료 플래그를 갱신하는 명령의 RSI를 저장하고,
  // 현재 설치되어 있던 원본/패치 10바이트는 그대로 실행한다.
  // ───────────────────────────────────────────────
  static uintptr_t g_giftCaptureCaveAddr = 0;
  static uintptr_t g_giftCapturedInteractionBase = 0;
  static uint8_t g_giftCaptureOriginal[10] = {};
  static bool g_giftCaptureApplied = false;

  bool StartGiftInteractionCapture() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return false;

    g_giftCapturedInteractionBase = 0;

    if (g_giftCaptureApplied) {
      AddLog(u8"[교류캡처DBG] 기증 캡처값 초기화 완료. 이제 기증을 1회 실행하세요.");
      return true;
    }

    if (!g_giftHookAddr) {
      // 원본 상태 우선, 이미 기증 무제한이 적용된 경우 패치 바이트도 허용.
      g_giftHookAddr = FindPattern(exeBase, exeBase + 0x3000000,
                                   "81 8E 20 03 00 00 00 04 00 00");
      if (!g_giftHookAddr) {
        g_giftHookAddr = FindPattern(exeBase, exeBase + 0x3000000,
                                     "81 A6 20 03 00 00 FF FB FF FF");
      }
    }

    if (!g_giftHookAddr) {
      AddLog(u8"[교류캡처DBG] 기증 상태 플래그 코드를 찾지 못했습니다.");
      return false;
    }

    memcpy(g_giftCaptureOriginal, (const void*)g_giftHookAddr, 10);

    g_giftCaptureCaveAddr = AllocNear(g_giftHookAddr, 128);
    if (!g_giftCaptureCaveAddr) {
      AddLog(u8"[교류캡처DBG] 기증 캡처 cave 할당 실패.");
      return false;
    }

    uint8_t* cave = (uint8_t*)g_giftCaptureCaveAddr;
    int p = 0;

    // push rax
    cave[p++] = 0x50;

    // mov rax, &g_giftCapturedInteractionBase
    cave[p++] = 0x48;
    cave[p++] = 0xB8;
    *(uintptr_t*)&cave[p] = (uintptr_t)&g_giftCapturedInteractionBase;
    p += 8;

    // mov [rax], rsi
    cave[p++] = 0x48;
    cave[p++] = 0x89;
    cave[p++] = 0x30;

    // pop rax
    cave[p++] = 0x58;

    // 캡처 설치 직전의 10바이트를 그대로 실행
    memcpy(&cave[p], g_giftCaptureOriginal, 10);
    p += 10;

    // 절대 복귀 점프
    const uintptr_t retAddr = g_giftHookAddr + 10;
    cave[p++] = 0xFF;
    cave[p++] = 0x25;
    cave[p++] = 0x00;
    cave[p++] = 0x00;
    cave[p++] = 0x00;
    cave[p++] = 0x00;
    *(uintptr_t*)&cave[p] = retAddr;
    p += 8;

    if (!ApplyJmp(g_giftHookAddr, g_giftCaptureCaveAddr, 10)) {
      VirtualFree((LPVOID)g_giftCaptureCaveAddr, 0, MEM_RELEASE);
      g_giftCaptureCaveAddr = 0;
      AddLog(u8"[교류캡처DBG] 기증 캡처 훅 설치 실패.");
      return false;
    }

    g_giftCaptureApplied = true;
    AddLog(u8"[교류캡처DBG] 기증 상태 객체 캡처 시작. 기증을 1회 실행한 뒤 결과 확인을 누르세요.");
    return true;
  }

  void LogGiftInteractionCaptureResult() {
    if (!g_giftCaptureApplied) {
      AddLog(u8"[교류캡처DBG] 먼저 기증 상태 객체 캡처를 시작해주세요.");
      return;
    }

    if (!g_giftCapturedInteractionBase) {
      AddLog(u8"[교류캡처DBG] 아직 기증 코드가 호출되지 않았습니다.");
      return;
    }

    const uintptr_t flagAddr = g_giftCapturedInteractionBase + 0x320;
    if (!IsValidPtr(flagAddr, sizeof(uint32_t))) {
      AddLog(u8"[교류캡처DBG] 캡처 base=%p / +0x320 주소가 유효하지 않습니다.",
             (void*)g_giftCapturedInteractionBase);
      return;
    }

    const uint32_t raw = *(const uint32_t*)flagAddr;
    AddLog(u8"[교류캡처DBG] base=%p +0x320 raw=0x%08X / 담화(bit2)=%u 대련(bit8)=%u 토론(bit9)=%u 기증(bit10)=%u",
           (void*)g_giftCapturedInteractionBase,
           raw,
           (raw & 0x00000004u) ? 1u : 0u,
           (raw & 0x00000100u) ? 1u : 0u,
           (raw & 0x00000200u) ? 1u : 0u,
           (raw & 0x00000400u) ? 1u : 0u);
  }

  void StopGiftInteractionCapture() {
    if (!g_giftCaptureApplied)
      return;

    RestoreBytes(g_giftHookAddr, g_giftCaptureOriginal, 10);
    VirtualFree((LPVOID)g_giftCaptureCaveAddr, 0, MEM_RELEASE);
    g_giftCaptureCaveAddr = 0;
    g_giftCapturedInteractionBase = 0;
    g_giftCaptureApplied = false;

    AddLog(u8"[교류캡처DBG] 기증 상태 객체 캡처 훅 해제 완료.");
  }

  void SetInfiniteGift(bool enable) {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return;

    if (!g_giftHookAddr) {
      uintptr_t found = FindPattern(exeBase, exeBase + 0x3000000, "81 8E 20 03 00 00 00 04 00 00");
      if (found) g_giftHookAddr = found;
    }

    AddLog("[DEBUG] giftHookAddr: %p", (void *)g_giftHookAddr);

    if (!g_giftHookAddr)
      return;

    if (!g_giftApplied && enable) {
      memcpy(g_giftOriginal, (void *)g_giftHookAddr, 10);

      DWORD old, tmp;
      VirtualProtect((LPVOID)g_giftHookAddr, 10, PAGE_EXECUTE_READWRITE, &old);

      // and [rsi+0x320], 0xFFFFFBFF
      // 81 A6 20 03 00 00 FF FB FF FF
      uint8_t patch[] = {0x81, 0xA6, 0x20, 0x03, 0x00, 0x00, 0xFF, 0xFB, 0xFF, 0xFF};
      memcpy((void *)g_giftHookAddr, patch, 10);

      VirtualProtect((LPVOID)g_giftHookAddr, 10, old, &tmp);
      g_giftApplied = true;

    } else if (g_giftApplied && !enable) {
      DWORD old, tmp;
      VirtualProtect((LPVOID)g_giftHookAddr, 10, PAGE_EXECUTE_READWRITE, &old);
      memcpy((void *)g_giftHookAddr, g_giftOriginal, 10);
      VirtualProtect((LPVOID)g_giftHookAddr, 10, old, &tmp);
      g_giftApplied = false;
    }
  }
} // namespace DX11Base