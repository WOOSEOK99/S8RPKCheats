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

  namespace {
    static const uint8_t kGiftOriginalBytes[10] = {
        0x81, 0x8E, 0x20, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00};
    static const uint8_t kGiftPatchedBytes[10] = {
        0x81, 0xA6, 0x20, 0x03, 0x00, 0x00, 0xFF, 0xFB, 0xFF, 0xFF};

    static uintptr_t ResolveGiftFlagCode(uintptr_t exeBase) {
      uintptr_t found = FindPattern(
          exeBase, exeBase + 0x3000000,
          "81 8E 20 03 00 00 00 04 00 00");
      if (!found) {
        found = FindPattern(
            exeBase, exeBase + 0x3000000,
            "81 A6 20 03 00 00 FF FB FF FF");
      }
      return found;
    }

    static bool WriteGiftCode(const uint8_t* bytes) {
      if (!g_giftHookAddr || !bytes)
        return false;

      DWORD old = 0, tmp = 0;
      if (!VirtualProtect((LPVOID)g_giftHookAddr, 10, PAGE_EXECUTE_READWRITE, &old))
        return false;

      memcpy((void*)g_giftHookAddr, bytes, 10);
      FlushInstructionCache(GetCurrentProcess(), (LPCVOID)g_giftHookAddr, 10);
      VirtualProtect((LPVOID)g_giftHookAddr, 10, old, &tmp);
      return true;
    }
  }

  static uintptr_t g_interactionCaptureCave = 0;
  static uintptr_t g_interactionCapturedBase = 0;
  static uint8_t g_interactionCaptureOriginal[10] = {};
  static bool g_interactionCaptureApplied = false;

  static void RestoreInteractionCaptureHook(bool clearCapturedBase) {
    if (g_interactionCaptureApplied && g_giftHookAddr) {
      RestoreBytes(g_giftHookAddr, g_interactionCaptureOriginal, 10);
      FlushInstructionCache(GetCurrentProcess(), (LPCVOID)g_giftHookAddr, 10);
    }

    if (g_interactionCaptureCave) {
      VirtualFree((LPVOID)g_interactionCaptureCave, 0, MEM_RELEASE);
      g_interactionCaptureCave = 0;
    }

    g_interactionCaptureApplied = false;
    if (clearCapturedBase)
      g_interactionCapturedBase = 0;
  }

  bool StartInteractionStateCaptureFromGift() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return false;

    // 기존 캡처가 남아 있으면 먼저 정상 원본으로 복구.
    RestoreInteractionCaptureHook(true);

    if (!g_giftHookAddr)
      g_giftHookAddr = ResolveGiftFlagCode(exeBase);

    if (!g_giftHookAddr) {
      AddLog(u8"[교류상태DBG] 기증 +0x320 코드를 찾지 못했습니다.");
      return false;
    }

    // 캡처는 반드시 정상 원본 OR 상태에서만 설치한다.
    // 기증 무제한 패치가 켜진 상태면 백업/복구 충돌을 만들지 않고 거부.
    if (memcmp((const void*)g_giftHookAddr, kGiftOriginalBytes, 10) != 0) {
      AddLog(u8"[교류상태DBG] 선물 기증 무제한을 OFF한 뒤 다시 눌러주세요.");
      return false;
    }

    memcpy(g_interactionCaptureOriginal, kGiftOriginalBytes, 10);

    g_interactionCaptureCave = AllocNear(g_giftHookAddr, 128);
    if (!g_interactionCaptureCave) {
      AddLog(u8"[교류상태DBG] 캡처 cave 할당 실패.");
      return false;
    }

    uint8_t* cave = (uint8_t*)g_interactionCaptureCave;
    int p = 0;

    // push rax
    cave[p++] = 0x50;

    // mov rax, &g_interactionCapturedBase
    cave[p++] = 0x48;
    cave[p++] = 0xB8;
    *(uintptr_t*)&cave[p] = (uintptr_t)&g_interactionCapturedBase;
    p += 8;

    // mov [rax], rsi
    cave[p++] = 0x48;
    cave[p++] = 0x89;
    cave[p++] = 0x30;

    // pop rax
    cave[p++] = 0x58;

    // 정상 원본: or dword ptr [rsi+0x320], 0x400
    memcpy(&cave[p], kGiftOriginalBytes, 10);
    p += 10;

    const uintptr_t retAddr = g_giftHookAddr + 10;
    cave[p++] = 0xFF;
    cave[p++] = 0x25;
    cave[p++] = 0x00;
    cave[p++] = 0x00;
    cave[p++] = 0x00;
    cave[p++] = 0x00;
    *(uintptr_t*)&cave[p] = retAddr;
    p += 8;

    if (!ApplyJmp(g_giftHookAddr, g_interactionCaptureCave, 10)) {
      VirtualFree((LPVOID)g_interactionCaptureCave, 0, MEM_RELEASE);
      g_interactionCaptureCave = 0;
      AddLog(u8"[교류상태DBG] 캡처 훅 설치 실패.");
      return false;
    }

    g_interactionCaptureApplied = true;
    AddLog(u8"[교류상태DBG] 캡처 시작. 기증을 1회 실행한 뒤 '현재 플래그 확인'을 누르세요.");
    return true;
  }

  void LogCapturedInteractionState() {
    if (!g_interactionCapturedBase) {
      AddLog(u8"[교류상태DBG] 아직 상태 객체가 캡처되지 않았습니다.");
      return;
    }

    // 첫 결과 확인 시 실행 훅은 즉시 제거하지만 캡처 주소는 유지한다.
    if (g_interactionCaptureApplied) {
      RestoreInteractionCaptureHook(false);
      AddLog(u8"[교류상태DBG] 기증 캡처 훅 원복 완료. 이후에는 저장된 상태 객체만 읽습니다.");
    }

    const uintptr_t flagAddr = g_interactionCapturedBase + 0x320;
    if (!IsValidPtr(flagAddr, sizeof(uint32_t))) {
      AddLog(u8"[교류상태DBG] 저장된 base가 더 이상 유효하지 않습니다: %p",
             (void*)g_interactionCapturedBase);
      return;
    }

    const uint32_t raw = *(const uint32_t*)flagAddr;
    AddLog(u8"[교류상태DBG] base=%p +0x320 raw=0x%08X / 담화(bit2)=%u 대련(bit8)=%u 토론(bit9)=%u 기증(bit10)=%u 방문(bit11)=%u",
           (void*)g_interactionCapturedBase,
           raw,
           (raw & 0x00000004u) ? 1u : 0u,
           (raw & 0x00000100u) ? 1u : 0u,
           (raw & 0x00000200u) ? 1u : 0u,
           (raw & 0x00000400u) ? 1u : 0u,
           (raw & 0x00000800u) ? 1u : 0u);
  }

  void CancelInteractionStateCapture() {
    RestoreInteractionCaptureHook(true);
    AddLog(u8"[교류상태DBG] 캡처 취소/초기화 완료.");
  }

  bool ClearCapturedDuelUsedBitForTest() {
    if (!g_interactionCapturedBase) {
      AddLog(u8"[교류TEST] 먼저 교류 상태 객체를 캡처해주세요.");
      return false;
    }

    const uintptr_t addr = g_interactionCapturedBase + 0x320;
    if (!IsValidPtr(addr, sizeof(uint32_t))) {
      AddLog(u8"[교류TEST] 저장된 +0x320 주소가 유효하지 않습니다.");
      return false;
    }

    const uint32_t before = *(const uint32_t*)addr;
    const uint32_t after = before & ~0x00000100u;
    *(uint32_t*)addr = after;
    const uint32_t readback = *(const uint32_t*)addr;

    AddLog(u8"[교류TEST] 대련 bit8 해제: 0x%08X -> 0x%08X / readback=0x%08X",
           before, after, readback);
    return readback == after;
  }

  bool ClearCapturedDebateUsedBitForTest() {
    if (!g_interactionCapturedBase) {
      AddLog(u8"[교류TEST] 먼저 교류 상태 객체를 캡처해주세요.");
      return false;
    }

    const uintptr_t addr = g_interactionCapturedBase + 0x320;
    if (!IsValidPtr(addr, sizeof(uint32_t))) {
      AddLog(u8"[교류TEST] 저장된 +0x320 주소가 유효하지 않습니다.");
      return false;
    }

    const uint32_t before = *(const uint32_t*)addr;
    const uint32_t after = before & ~0x00000200u;
    *(uint32_t*)addr = after;
    const uint32_t readback = *(const uint32_t*)addr;

    AddLog(u8"[교류TEST] 토론 bit9 해제: 0x%08X -> 0x%08X / readback=0x%08X",
           before, after, readback);
    return readback == after;
  }

  void SetInfiniteGift(bool enable) {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return;

    if (!g_giftHookAddr)
      g_giftHookAddr = ResolveGiftFlagCode(exeBase);

    if (!g_giftHookAddr)
      return;

    const bool isOriginal =
        memcmp((const void*)g_giftHookAddr, kGiftOriginalBytes, 10) == 0;
    const bool isPatched =
        memcmp((const void*)g_giftHookAddr, kGiftPatchedBytes, 10) == 0;

    if (enable) {
      if (isPatched) {
        g_giftApplied = true;
        return;
      }
      if (!isOriginal) {
        AddLog(u8"[기증] 예상하지 못한 코드 상태라 무제한 패치를 적용하지 않았습니다.");
        return;
      }

      memcpy(g_giftOriginal, kGiftOriginalBytes, 10);
      if (WriteGiftCode(kGiftPatchedBytes))
        g_giftApplied = true;

    } else {
      // 디버그 훅/상태 플래그와 무관하게 실제 코드가 AND 패치로 남아 있으면
      // 항상 원본 OR 명령으로 복구한다.
      if (isPatched) {
        if (WriteGiftCode(kGiftOriginalBytes)) {
          AddLog(u8"[기증] 무제한 코드 원본 복구 완료.");
        }
      }
      g_giftApplied = false;
    }
  }} // namespace DX11Base