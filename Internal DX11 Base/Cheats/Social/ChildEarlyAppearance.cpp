#include "../../pch.h"
#include "ChildEarlyAppearance.h"
#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../MenuState.h"
#include "../../showlog.h"

#include <psapi.h>
#include <cstring>
#include <vector>

namespace DX11Base {
namespace {

static uintptr_t g_childHookAddr = 0;
static uintptr_t g_childCaveAddr = 0;
static uint8_t g_childOriginal[7] = {};
static bool g_childApplied = false;

// MonthCapture.cpp와 동일한 시나리오 연도 경로.
static constexpr uintptr_t kScenarioInstanceStaticOffset = 0x2E98BC8;
static constexpr uintptr_t kScenarioYearOffset = 0x72D0;

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

static void Emit8(std::vector<uint8_t>& code, uint8_t v) {
  code.push_back(v);
}

static void Emit16(std::vector<uint8_t>& code, uint16_t v) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
  code.insert(code.end(), p, p + sizeof(v));
}

static void Emit64(std::vector<uint8_t>& code, uintptr_t v) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
  code.insert(code.end(), p, p + sizeof(v));
}

static bool PatchRel8(std::vector<uint8_t>& code, size_t dispPos, size_t targetPos) {
  const ptrdiff_t rel = (ptrdiff_t)targetPos - (ptrdiff_t)(dispPos + 1);
  if (rel < -128 || rel > 127)
    return false;
  code[dispPos] = (uint8_t)(int8_t)rel;
  return true;
}

static bool InstallChildCave(uintptr_t hookAddr) {
  g_childCaveAddr = AllocNear(hookAddr, 256);
  if (!g_childCaveAddr)
    return false;

  std::vector<uint8_t> code;
  code.reserve(192);

  // Original: mov rax,[rbp+000001C0]
  code.insert(code.end(), g_childOriginal,
              g_childOriginal + sizeof(g_childOriginal));

  // 원본 mov 명령은 FLAGS를 변경하지 않으므로 사용자 로직도 FLAGS까지 보존합니다.
  Emit8(code, 0x9C); // pushfq
  Emit8(code, 0x53); // push rbx
  Emit8(code, 0x51); // push rcx
  Emit8(code, 0x52); // push rdx

  // 현재 연도는 레지스터를 추정하지 않고 MonthCapture.cpp와 동일한
  // 시나리오 데이터 인스턴스 + 0x72D0에서 직접 읽습니다.
  // mov rdx, exeBase + kScenarioInstanceStaticOffset
  Emit8(code, 0x48); Emit8(code, 0xBA);
  Emit64(code, (uintptr_t)GetModuleHandle(nullptr) + kScenarioInstanceStaticOffset);
  // mov rdx,[rdx]
  Emit8(code, 0x48); Emit8(code, 0x8B); Emit8(code, 0x12);
  // test rdx,rdx / jz end
  Emit8(code, 0x48); Emit8(code, 0x85); Emit8(code, 0xD2);
  Emit8(code, 0x74); const size_t jNoScenario = code.size(); Emit8(code, 0x00);
  // movzx ebx,word ptr [rdx+72D0]
  Emit8(code, 0x0F); Emit8(code, 0xB7); Emit8(code, 0x9A);
  code.push_back(0xD0); code.push_back(0x72); code.push_back(0x00); code.push_back(0x00);

  // cmp bx,170 / jbe end
  Emit8(code, 0x66); Emit8(code, 0x81); Emit8(code, 0xFB); Emit16(code, 170);
  Emit8(code, 0x76); const size_t jYearLow = code.size(); Emit8(code, 0x00);

  // cmp bx,270 / jae end
  Emit8(code, 0x66); Emit8(code, 0x81); Emit8(code, 0xFB); Emit16(code, 270);
  Emit8(code, 0x73); const size_t jYearHigh = code.size(); Emit8(code, 0x00);

  // mov rdx,&vChildEarlyAppearanceYears / mov edx,[rdx]
  Emit8(code, 0x48); Emit8(code, 0xBA);
  Emit64(code, (uintptr_t)&vChildEarlyAppearanceYears);
  Emit8(code, 0x8B); Emit8(code, 0x12);

  // xor rcx,rcx
  Emit8(code, 0x48); Emit8(code, 0x31); Emit8(code, 0xC9);

  // cx = appearance year; if appearance-N <= current year, already close enough => skip
  // mov cx,[rax+32]
  Emit8(code, 0x66); Emit8(code, 0x8B); Emit8(code, 0x48); Emit8(code, 0x32);
  // sub cx,dx
  Emit8(code, 0x66); Emit8(code, 0x2B); Emit8(code, 0xCA);
  // cmp cx,bx / jbe end
  Emit8(code, 0x66); Emit8(code, 0x3B); Emit8(code, 0xCB);
  Emit8(code, 0x76); const size_t jAlreadyClose = code.size(); Emit8(code, 0x00);

  // appearance year = current year + N
  Emit8(code, 0x66); Emit8(code, 0x8B); Emit8(code, 0xCB); // mov cx,bx
  Emit8(code, 0x66); Emit8(code, 0x03); Emit8(code, 0xCA); // add cx,dx
  Emit8(code, 0x66); Emit8(code, 0x89); Emit8(code, 0x48); Emit8(code, 0x32);

  // birth year = appearance year - 15
  Emit8(code, 0x66); Emit8(code, 0x83); Emit8(code, 0xE9); Emit8(code, 0x0F);
  Emit8(code, 0x66); Emit8(code, 0x89); Emit8(code, 0x48); Emit8(code, 0x34);

  // death year = min(current year + 99, 270)
  Emit8(code, 0x66); Emit8(code, 0x83); Emit8(code, 0xC3); Emit8(code, 0x63);
  Emit8(code, 0x66); Emit8(code, 0x81); Emit8(code, 0xFB); Emit16(code, 270);
  Emit8(code, 0x76); const size_t jDeathOk = code.size(); Emit8(code, 0x00);

  // mov bx,270
  Emit8(code, 0x66); Emit8(code, 0xBB); Emit16(code, 270);

  const size_t deathOkPos = code.size();
  // mov [rax+36],bx
  Emit8(code, 0x66); Emit8(code, 0x89); Emit8(code, 0x58); Emit8(code, 0x36);

  const size_t endPos = code.size();
  Emit8(code, 0x5A); // pop rdx
  Emit8(code, 0x59); // pop rcx
  Emit8(code, 0x5B); // pop rbx
  Emit8(code, 0x9D); // popfq

  if (!PatchRel8(code, jNoScenario, endPos) ||
      !PatchRel8(code, jYearLow, endPos) ||
      !PatchRel8(code, jYearHigh, endPos) ||
      !PatchRel8(code, jAlreadyClose, endPos) ||
      !PatchRel8(code, jDeathOk, deathOkPos)) {
    VirtualFree((LPVOID)g_childCaveAddr, 0, MEM_RELEASE);
    g_childCaveAddr = 0;
    return false;
  }

  // Absolute return jump: jmp qword ptr [rip+0]
  Emit8(code, 0xFF); Emit8(code, 0x25);
  Emit8(code, 0x00); Emit8(code, 0x00); Emit8(code, 0x00); Emit8(code, 0x00);
  Emit64(code, hookAddr + sizeof(g_childOriginal));

  memcpy((void*)g_childCaveAddr, code.data(), code.size());
  FlushInstructionCache(GetCurrentProcess(), (LPCVOID)g_childCaveAddr, code.size());

  if (!ApplyJmp(hookAddr, g_childCaveAddr, sizeof(g_childOriginal))) {
    VirtualFree((LPVOID)g_childCaveAddr, 0, MEM_RELEASE);
    g_childCaveAddr = 0;
    return false;
  }

  FlushInstructionCache(GetCurrentProcess(), (LPCVOID)hookAddr,
                        sizeof(g_childOriginal));
  return true;
}

} // namespace

void SetChildEarlyAppearance(bool enable) {
  if (vChildEarlyAppearanceYears < 1)
    vChildEarlyAppearanceYears = 1;
  if (vChildEarlyAppearanceYears > 10)
    vChildEarlyAppearanceYears = 10;

  if (enable) {
    if (g_childApplied)
      return;

    uintptr_t begin = 0, end = 0;
    if (!GetModuleRange(begin, end)) {
      AddLog(u8"[Children] 모듈 범위 확인 실패");
      return;
    }

    if (!g_childHookAddr) {
      g_childHookAddr = FindPattern(
          begin, end,
          "48 8B 85 C0 01 00 00 0F B7 58 34 E8 1D");
    }

    if (!g_childHookAddr) {
      AddLog(u8"[Children] 자녀 조기 등장 패턴을 찾지 못했습니다.");
      return;
    }

    memcpy(g_childOriginal, (const void*)g_childHookAddr,
           sizeof(g_childOriginal));

    if (!InstallChildCave(g_childHookAddr)) {
      AddLog(u8"[Children] 자녀 조기 등장 Cave 설치 실패");
      return;
    }

    g_childApplied = true;
    AddLog(u8"[Children] 자녀 조기 등장 적용: %d년 후",
           vChildEarlyAppearanceYears);
    return;
  }

  if (!g_childApplied)
    return;

  RestoreBytes(g_childHookAddr, g_childOriginal, sizeof(g_childOriginal));
  FlushInstructionCache(GetCurrentProcess(), (LPCVOID)g_childHookAddr,
                        sizeof(g_childOriginal));

  if (g_childCaveAddr)
    VirtualFree((LPVOID)g_childCaveAddr, 0, MEM_RELEASE);

  g_childCaveAddr = 0;
  g_childApplied = false;
  AddLog(u8"[Children] 자녀 조기 등장 해제");
}

} // namespace DX11Base
