#include "pch.h"
#include "TraitTextNameHook.h"

#include "TraitTextEditorData.h"
#include "../../showlog.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace DX11Base {
namespace {

constexpr uintptr_t kNameGetterOffset = 0x170E2D0;
constexpr uintptr_t kNameGetterPointerOffset = 0x170E2D6;
constexpr uintptr_t kVersionNameGetterOffset = 0x5CB0;
constexpr size_t kCaveSize = 0x40000;

constexpr uint8_t kNameGetterStub[] = {
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00
};

constexpr uint8_t kVersionGetterPrologue[] = {
    0x48, 0x89, 0x4C, 0x24, 0x08,
    0x48, 0x83, 0xEC, 0x28,
    0x0F, 0xB7, 0x41, 0x08
};

struct NameEdit {
  std::wstring oldName;
  std::wstring newName;
};

static bool g_applied = false;
static uintptr_t g_gameBase = 0;
static uintptr_t g_originalTarget = 0;
static uintptr_t g_currentCave = 0;

// CE의 globalalloc과 같은 수명 정책을 따릅니다.
// 게임 UI가 이전 반환 문자열 포인터를 보유할 수 있으므로 재적용/해제 시 코드케이브를 즉시 해제하지 않습니다.
static std::vector<uintptr_t> g_retiredCaves;

void SetError(std::string* error, const std::string& text) {
  if (error)
    *error = text;
}

bool SafeRead(const void* src, void* dst, size_t size) {
  __try {
    std::memcpy(dst, src, size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    std::memset(dst, 0, size);
    return false;
  }
}

bool MatchBytes(uintptr_t address, const uint8_t* expected, size_t size) {
  std::vector<uint8_t> current(size);
  return SafeRead(reinterpret_cast<const void*>(address), current.data(), size) &&
         std::memcmp(current.data(), expected, size) == 0;
}

bool ReadPointer(uintptr_t address, uintptr_t& value) {
  return SafeRead(reinterpret_cast<const void*>(address), &value, sizeof(value));
}

bool WritePointer(uintptr_t address, uintptr_t value) {
  DWORD oldProtect = 0;
  void* dst = reinterpret_cast<void*>(address);
  if (!VirtualProtect(dst, sizeof(value), PAGE_EXECUTE_READWRITE, &oldProtect))
    return false;

  bool ok = false;
  __try {
    std::memcpy(dst, &value, sizeof(value));
    ok = true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    ok = false;
  }

  DWORD ignored = 0;
  VirtualProtect(dst, sizeof(value), oldProtect, &ignored);
  if (ok)
    FlushInstructionCache(GetCurrentProcess(), dst, sizeof(value));
  return ok;
}

bool Utf8ToWide(const std::string& text, std::wstring& out) {
  if (text.find('\0') != std::string::npos)
    return false;
  if (text.empty()) {
    out.clear();
    return true;
  }

  const int count = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0)
    return false;

  out.resize(static_cast<size_t>(count));
  return MultiByteToWideChar(
             CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
             out.data(), count) == count;
}

bool CollectNameEdits(std::vector<NameEdit>& edits, std::string* error) {
  edits.clear();
  auto& rows = GetTraitTextEditRows();

  for (size_t i = 0; i < rows.size(); ++i) {
    const auto& row = rows[i];
    if (row.newName.empty() || row.newName == row.oldName)
      continue;

    std::wstring oldWide;
    std::wstring newWide;
    if (!Utf8ToWide(row.oldName, oldWide) || !Utf8ToWide(row.newName, newWide)) {
      SetError(error, "기재 이름 UTF-8 변환 실패");
      return false;
    }
    if (newWide.size() > 5) {
      SetError(error, "새 기재명은 UTF-16 기준 5글자 이하만 허용됩니다.");
      return false;
    }

    edits.push_back({std::move(oldWide), std::move(newWide)});
  }
  return true;
}

struct CodeBuilder {
  std::vector<uint8_t> bytes;

  size_t Pos() const { return bytes.size(); }

  void U8(uint8_t value) { bytes.push_back(value); }

  void U16(uint16_t value) {
    U8(static_cast<uint8_t>(value & 0xFF));
    U8(static_cast<uint8_t>((value >> 8) & 0xFF));
  }

  void U32(uint32_t value) {
    for (int i = 0; i < 4; ++i)
      U8(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
  }

  void U64(uint64_t value) {
    for (int i = 0; i < 8; ++i)
      U8(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
  }

  void PatchI32(size_t offset, int32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  }

  void Align(size_t alignment) {
    while (bytes.size() % alignment)
      U8(0x90);
  }
};

bool PatchRel32(CodeBuilder& code, size_t dispOffset, size_t nextInstruction, size_t targetOffset) {
  const int64_t delta = static_cast<int64_t>(targetOffset) -
                        static_cast<int64_t>(nextInstruction);
  if (delta < INT32_MIN || delta > INT32_MAX)
    return false;
  code.PatchI32(dispOffset, static_cast<int32_t>(delta));
  return true;
}

bool BuildNameCave(const std::vector<NameEdit>& edits,
                   uintptr_t originalTarget,
                   uintptr_t& caveOut,
                   std::string* error) {
  CodeBuilder code;
  std::vector<std::vector<size_t>> mismatchPatches;
  std::vector<size_t> stringLeaPatches;
  std::vector<size_t> stringLeaNext;

  // sub rsp,28
  code.U8(0x48); code.U8(0x83); code.U8(0xEC); code.U8(0x28);

  // call qword ptr [rip+disp32] -> original target slot
  code.U8(0xFF); code.U8(0x15);
  const size_t callDisp = code.Pos();
  code.U32(0);
  const size_t callNext = code.Pos();

  // add rsp,28
  code.U8(0x48); code.U8(0x83); code.U8(0xC4); code.U8(0x28);

  // test rax,rax
  code.U8(0x48); code.U8(0x85); code.U8(0xC0);

  // je finalRet
  code.U8(0x0F); code.U8(0x84);
  const size_t nullJumpDisp = code.Pos();
  code.U32(0);
  const size_t nullJumpNext = code.Pos();

  mismatchPatches.resize(edits.size());
  stringLeaPatches.resize(edits.size());
  stringLeaNext.resize(edits.size());

  std::vector<size_t> nameBlockOffsets(edits.size());
  for (size_t i = 0; i < edits.size(); ++i) {
    nameBlockOffsets[i] = code.Pos();

    for (size_t j = 0; j < edits[i].oldName.size(); ++j) {
      // cmp word ptr [rax+disp32], imm16
      code.U8(0x66); code.U8(0x81); code.U8(0xB8);
      code.U32(static_cast<uint32_t>(j * 2));
      code.U16(static_cast<uint16_t>(edits[i].oldName[j]));

      // jne next name/final
      code.U8(0x0F); code.U8(0x85);
      const size_t disp = code.Pos();
      code.U32(0);
      mismatchPatches[i].push_back(disp);
    }

    // lea rax,[rip+disp32] -> replacement string
    code.U8(0x48); code.U8(0x8D); code.U8(0x05);
    stringLeaPatches[i] = code.Pos();
    code.U32(0);
    stringLeaNext[i] = code.Pos();

    code.U8(0xC3); // ret
  }

  const size_t finalRet = code.Pos();
  code.U8(0xC3);

  code.Align(8);
  const size_t originalTargetSlot = code.Pos();
  code.U64(static_cast<uint64_t>(originalTarget));

  std::vector<size_t> stringOffsets(edits.size());
  for (size_t i = 0; i < edits.size(); ++i) {
    code.Align(2);
    stringOffsets[i] = code.Pos();
    for (wchar_t ch : edits[i].newName)
      code.U16(static_cast<uint16_t>(ch));
    code.U16(0);
  }

  if (code.bytes.size() > kCaveSize) {
    SetError(error, "기재 이름 코드케이브 크기를 초과했습니다.");
    return false;
  }

  if (!PatchRel32(code, callDisp, callNext, originalTargetSlot) ||
      !PatchRel32(code, nullJumpDisp, nullJumpNext, finalRet)) {
    SetError(error, "기재 이름 코드케이브 rel32 계산 실패");
    return false;
  }

  for (size_t i = 0; i < edits.size(); ++i) {
    const size_t mismatchTarget = (i + 1 < edits.size()) ? nameBlockOffsets[i + 1] : finalRet;

    for (size_t disp : mismatchPatches[i]) {
      const size_t next = disp + 4;
      if (!PatchRel32(code, disp, next, mismatchTarget)) {
        SetError(error, "기재 이름 비교 분기 rel32 계산 실패");
        return false;
      }
    }

    if (!PatchRel32(code, stringLeaPatches[i], stringLeaNext[i], stringOffsets[i])) {
      SetError(error, "기재 이름 문자열 rel32 계산 실패");
      return false;
    }
  }

  void* cave = VirtualAlloc(nullptr, kCaveSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!cave) {
    SetError(error, "기재 이름 코드케이브 할당 실패");
    return false;
  }

  std::memcpy(cave, code.bytes.data(), code.bytes.size());
  DWORD oldProtect = 0;
  if (!VirtualProtect(cave, kCaveSize, PAGE_EXECUTE_READ, &oldProtect)) {
    VirtualFree(cave, 0, MEM_RELEASE);
    SetError(error, "기재 이름 코드케이브 실행 권한 설정 실패");
    return false;
  }

  FlushInstructionCache(GetCurrentProcess(), cave, code.bytes.size());
  caveOut = reinterpret_cast<uintptr_t>(cave);
  return true;
}

bool VerifyVersion(uintptr_t gameBase, uintptr_t versionBase, std::string* error) {
  if (!MatchBytes(gameBase + kNameGetterOffset, kNameGetterStub, sizeof(kNameGetterStub))) {
    SetError(error, "SAN8RPK.exe+170E2D0 이름 getter 스텁이 CT 예상 바이트와 다릅니다.");
    return false;
  }

  if (!MatchBytes(versionBase + kVersionNameGetterOffset,
                  kVersionGetterPrologue, sizeof(kVersionGetterPrologue))) {
    SetError(error, "version.dll+5CB0 이름 getter 원본 바이트가 CT 예상과 다릅니다.");
    return false;
  }

  return true;
}

} // namespace

bool IsTraitTextNameHookApplied() {
  if (!g_applied || !g_gameBase || !g_currentCave)
    return false;

  uintptr_t current = 0;
  return ReadPointer(g_gameBase + kNameGetterPointerOffset, current) &&
         current == g_currentCave;
}

bool RemoveTraitTextNameHook(std::string* error) {
  if (!g_applied)
    return true;

  if (!g_gameBase || !g_originalTarget || !g_currentCave) {
    SetError(error, "기재 이름 후크 내부 상태가 유효하지 않습니다.");
    return false;
  }

  uintptr_t current = 0;
  if (!ReadPointer(g_gameBase + kNameGetterPointerOffset, current)) {
    SetError(error, "기재 이름 getter 포인터 읽기 실패");
    return false;
  }

  if (current != g_currentCave) {
    if (current == g_originalTarget) {
      g_applied = false;
      return true;
    }
    SetError(error, "기재 이름 getter가 다른 패치에 의해 변경되어 있어 복구를 중단했습니다.");
    return false;
  }

  if (!WritePointer(g_gameBase + kNameGetterPointerOffset, g_originalTarget)) {
    SetError(error, "기재 이름 getter 원본 포인터 복구 실패");
    return false;
  }

  g_retiredCaves.push_back(g_currentCave);
  g_currentCave = 0;
  g_applied = false;
  AddLog(u8"[기재 편집/Step2] 이름 치환 후크 해제");
  return true;
}

bool ApplyTraitTextNameHook(std::string* error) {
  std::vector<NameEdit> edits;
  if (!CollectNameEdits(edits, error))
    return false;

  // CT S.apply()와 동일하게 재적용 전 기존 후크부터 복구합니다.
  if (g_applied && !RemoveTraitTextNameHook(error))
    return false;

  if (edits.empty()) {
    AddLog(u8"[기재 편집/Step2] 변경된 기재명이 없어 이름 후크를 적용하지 않습니다.");
    return true;
  }

  const uintptr_t gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  const uintptr_t versionBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"version.dll"));
  if (!gameBase || !versionBase) {
    SetError(error, "SAN8RPK.exe 또는 version.dll 모듈을 찾지 못했습니다.");
    return false;
  }

  if (!VerifyVersion(gameBase, versionBase, error))
    return false;

  const uintptr_t expectedOriginal = versionBase + kVersionNameGetterOffset;
  uintptr_t currentTarget = 0;
  if (!ReadPointer(gameBase + kNameGetterPointerOffset, currentTarget)) {
    SetError(error, "SAN8RPK.exe+170E2D6 이름 getter 포인터 읽기 실패");
    return false;
  }

  // CT의 assert(readQword(...+170E2D6)==version.dll+5CB0)와 동일한 충돌 방지.
  if (currentTarget != expectedOriginal) {
    SetError(error, "기존 이름 패치가 활성화되어 있습니다. 먼저 해제하세요.");
    return false;
  }

  uintptr_t cave = 0;
  if (!BuildNameCave(edits, expectedOriginal, cave, error))
    return false;

  if (!WritePointer(gameBase + kNameGetterPointerOffset, cave)) {
    // 아직 게임에 노출되지 않은 새 cave이므로 이 경우만 즉시 해제해도 안전합니다.
    VirtualFree(reinterpret_cast<void*>(cave), 0, MEM_RELEASE);
    SetError(error, "SAN8RPK.exe+170E2D6 이름 getter 포인터 교체 실패");
    return false;
  }

  g_gameBase = gameBase;
  g_originalTarget = expectedOriginal;
  g_currentCave = cave;
  g_applied = true;

  AddLog(u8"[기재 편집/Step2] 이름 치환 후크 ON (%zu개)", edits.size());
  return true;
}

} // namespace DX11Base
