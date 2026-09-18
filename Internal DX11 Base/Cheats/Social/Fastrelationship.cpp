#include "../../pch.h"
#include "Fastrelationship.h"
#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"

#include <psapi.h>
#include <cstring>

namespace DX11Base {
namespace {

// CT ID 321: Fast relationship into intimacy, excluding the gift path.
// Original bytes: 0F B6 CB 3A DA 0F 4F CA
// Patched bytes : 0F B6 CA 88 D3 90 90 90
//
// This is intentionally NOT CT ID 343. ID 343 modifies intimacy while
// opening/browsing the relationship menu. This patch only affects the
// relationship-update path for the officer currently being processed.
static uintptr_t g_fastIntimacyAddr = 0;
static uint8_t g_fastIntimacyOriginal[8] = {};
static bool g_fastIntimacyApplied = false;

static bool GetModuleRange(uintptr_t& begin, uintptr_t& end) {
  begin = (uintptr_t)GetModuleHandle(nullptr);
  if (!begin)
    return false;

  MODULEINFO mi{};
  if (!GetModuleInformation(GetCurrentProcess(), (HMODULE)begin, &mi, sizeof(mi)))
    return false;

  end = begin + mi.SizeOfImage;
  return end > begin;
}

static bool PatchBytes(uintptr_t address, const uint8_t* bytes, size_t size) {
  if (!address || !bytes || size == 0)
    return false;

  DWORD oldProtect = 0;
  if (!VirtualProtect((LPVOID)address, size, PAGE_EXECUTE_READWRITE, &oldProtect))
    return false;

  memcpy((void*)address, bytes, size);
  FlushInstructionCache(GetCurrentProcess(), (LPCVOID)address, size);

  DWORD dummy = 0;
  VirtualProtect((LPVOID)address, size, oldProtect, &dummy);
  return true;
}

} // namespace

void SetFastRelationship(bool enable) {
  if (enable) {
    if (g_fastIntimacyApplied)
      return;

    uintptr_t begin = 0, end = 0;
    if (!GetModuleRange(begin, end)) {
      AddLog(u8"[Relationship] 모듈 범위 확인 실패");
      return;
    }

    if (!g_fastIntimacyAddr)
      g_fastIntimacyAddr = FindPattern(begin, end, "0F B6 CB 3A DA 0F 4F CA");

    if (!g_fastIntimacyAddr) {
      AddLog(u8"[Relationship] 즉시 경애 패턴을 찾지 못했습니다.");
      return;
    }

    memcpy(g_fastIntimacyOriginal, (const void*)g_fastIntimacyAddr,
           sizeof(g_fastIntimacyOriginal));

    static const uint8_t kPatch[8] = {
        0x0F, 0xB6, 0xCA,
        0x88, 0xD3,
        0x90, 0x90, 0x90
    };

    if (!PatchBytes(g_fastIntimacyAddr, kPatch, sizeof(kPatch))) {
      AddLog(u8"[Relationship] 즉시 경애 패치 쓰기 실패");
      return;
    }

    g_fastIntimacyApplied = true;
    AddLog(u8"[Relationship] 즉시 경애 맺기 적용 (CT ID 321)");
    return;
  }

  if (!g_fastIntimacyApplied)
    return;

  RestoreBytes(g_fastIntimacyAddr, g_fastIntimacyOriginal,
               sizeof(g_fastIntimacyOriginal));
  FlushInstructionCache(GetCurrentProcess(), (LPCVOID)g_fastIntimacyAddr,
                        sizeof(g_fastIntimacyOriginal));
  g_fastIntimacyApplied = false;
  AddLog(u8"[Relationship] 즉시 경애 맺기 해제");
}

} // namespace DX11Base
