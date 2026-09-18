#include "../../pch.h"
#include "Fastrelationship.h"
#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"

#include <psapi.h>
#include <cstring>

namespace DX11Base {
namespace {

// CT ID 300: Fast relationship, intimacy in Adoration status
// Original: add eax,r8d / cmp eax,r14d
// Patch: force r8d = 0x32 (50 decimal), then execute the original instructions.
static uintptr_t g_relHookAddr = 0;
static uint8_t g_relOriginal[6] = {};
static uintptr_t g_relCaveAddr = 0;
static bool g_relApplied = false;

// CT ID 321: Fast relationship into intimacy, excluding the gift path.
// Original bytes: 0F B6 CB 3A DA 0F 4F CA
// Patched bytes : 0F B6 CA 88 D3 90 90 90
// This makes the current relationship-update path write dl (0x64 / 100)
// for the officer being processed; it is not the menu-open/list-wide patch (CT ID 343).
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

static bool InstallFastRelationshipCave(uintptr_t hookAddr) {
  g_relCaveAddr = AllocNear(hookAddr, 128);
  if (!g_relCaveAddr)
    return false;

  uint8_t* cave = (uint8_t*)g_relCaveAddr;
  size_t cur = 0;

  // mov r8d, 0x32
  cave[cur++] = 0x41;
  cave[cur++] = 0xB8;
  *(uint32_t*)&cave[cur] = 0x32;
  cur += 4;

  // Original: add eax,r8d
  cave[cur++] = 0x41;
  cave[cur++] = 0x03;
  cave[cur++] = 0xC0;

  // Original: cmp eax,r14d
  cave[cur++] = 0x41;
  cave[cur++] = 0x3B;
  cave[cur++] = 0xC6;

  // Absolute return jump: jmp qword ptr [rip+0]
  cave[cur++] = 0xFF;
  cave[cur++] = 0x25;
  *(uint32_t*)&cave[cur] = 0;
  cur += 4;
  *(uintptr_t*)&cave[cur] = hookAddr + 6;
  cur += sizeof(uintptr_t);

  if (!ApplyJmp(hookAddr, g_relCaveAddr, 6)) {
    VirtualFree((LPVOID)g_relCaveAddr, 0, MEM_RELEASE);
    g_relCaveAddr = 0;
    return false;
  }

  FlushInstructionCache(GetCurrentProcess(), (LPCVOID)hookAddr, 6);
  return true;
}

} // namespace

void SetFastRelationship(bool enable) {
  if (enable) {
    if (g_relApplied)
      return;

    uintptr_t begin = 0, end = 0;
    if (!GetModuleRange(begin, end)) {
      AddLog(u8"[Relationship] 모듈 범위 확인 실패");
      return;
    }

    if (!g_relHookAddr)
      g_relHookAddr = FindPattern(begin, end, "41 03 C0 41 3B C6 41");

    if (!g_relHookAddr) {
      AddLog(u8"[Relationship] 빠른 친밀도 패턴을 찾지 못했습니다.");
      return;
    }

    memcpy(g_relOriginal, (const void*)g_relHookAddr, sizeof(g_relOriginal));
    if (!InstallFastRelationshipCave(g_relHookAddr)) {
      AddLog(u8"[Relationship] 빠른 친밀도 Cave 설치 실패");
      return;
    }

    g_relApplied = true;
    AddLog(u8"[Relationship] 경애 상태 친밀도 증가량 가속 적용 (CT ID 300)");
    return;
  }

  if (!g_relApplied)
    return;

  RestoreBytes(g_relHookAddr, g_relOriginal, sizeof(g_relOriginal));
  FlushInstructionCache(GetCurrentProcess(), (LPCVOID)g_relHookAddr, sizeof(g_relOriginal));
  if (g_relCaveAddr)
    VirtualFree((LPVOID)g_relCaveAddr, 0, MEM_RELEASE);

  g_relCaveAddr = 0;
  g_relApplied = false;
  AddLog(u8"[Relationship] 경애 상태 친밀도 증가량 가속 해제");
}

void SetFastIntimacy(bool enable) {
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
      AddLog(u8"[Relationship] 즉시 친밀 관계 패턴을 찾지 못했습니다.");
      return;
    }

    memcpy(g_fastIntimacyOriginal, (const void*)g_fastIntimacyAddr, sizeof(g_fastIntimacyOriginal));

    static const uint8_t kPatch[8] = {
        0x0F, 0xB6, 0xCA,
        0x88, 0xD3,
        0x90, 0x90, 0x90
    };

    if (!PatchBytes(g_fastIntimacyAddr, kPatch, sizeof(kPatch))) {
      AddLog(u8"[Relationship] 즉시 친밀 관계 패치 쓰기 실패");
      return;
    }

    g_fastIntimacyApplied = true;
    AddLog(u8"[Relationship] 관계 갱신 대상 친밀도 100 적용 (CT ID 321, 선물 경로 제외)");
    return;
  }

  if (!g_fastIntimacyApplied)
    return;

  RestoreBytes(g_fastIntimacyAddr, g_fastIntimacyOriginal, sizeof(g_fastIntimacyOriginal));
  FlushInstructionCache(GetCurrentProcess(), (LPCVOID)g_fastIntimacyAddr, sizeof(g_fastIntimacyOriginal));
  g_fastIntimacyApplied = false;
  AddLog(u8"[Relationship] 관계 갱신 대상 친밀도 100 해제");
}

} // namespace DX11Base
