#include "pch.h"
#include "TraitTextSpecialDescHook.h"

#include "TraitTextEditorData.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace DX11Base {
namespace {

constexpr uintptr_t kSpecialHookOffset = 0x146E5;
constexpr uintptr_t kSpecialReturnOffset = 0x146EA; // CT처럼 뒤의 0F 84 ... 분기는 그대로 둠
constexpr size_t kCaveSize = 0x8000;

constexpr uint8_t kSpecialAssertBytes[] = {
    0x66, 0x45, 0x39, 0x1C, 0x24,
    0x0F, 0x84, 0xB1, 0x04, 0x00, 0x00
};
constexpr uint8_t kSpecialOriginal5[] = {
    0x66, 0x45, 0x39, 0x1C, 0x24
};

struct SpecialEdit {
  std::wstring oldDesc;
  std::wstring newDesc;
};

static bool g_applied = false;
static uintptr_t g_gameBase = 0;
static uintptr_t g_currentCave = 0;
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

bool WriteProtected(uintptr_t address, const void* data, size_t size) {
  DWORD oldProtect = 0;
  void* dst = reinterpret_cast<void*>(address);
  if (!VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &oldProtect))
    return false;

  bool ok = false;
  __try {
    std::memcpy(dst, data, size);
    ok = true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    ok = false;
  }

  DWORD ignored = 0;
  VirtualProtect(dst, size, oldProtect, &ignored);
  if (ok)
    FlushInstructionCache(GetCurrentProcess(), dst, size);
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

bool CollectSpecialEdits(std::vector<SpecialEdit>& edits, std::string* error) {
  edits.clear();
  auto& rows = GetTraitTextEditRows();

  for (size_t i = 0; i < rows.size(); ++i) {
    const auto& row = rows[i];
    if (row.oldName != u8"괴물" && row.oldName != u8"잔병첩보")
      continue;
    if (row.newDesc.empty() || row.newDesc == row.oldDesc)
      continue;

    if (!ValidateTraitTextRow(i, error))
      return false;

    std::wstring oldWide;
    std::wstring newWide;
    if (!Utf8ToWide(row.oldDesc, oldWide) || !Utf8ToWide(row.newDesc, newWide)) {
      SetError(error, "특수 기재 설명 UTF-8 변환 실패");
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
  void PatchI32(size_t offset, int32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
  }
  void Align(size_t alignment) {
    while (bytes.size() % alignment)
      U8(0x90);
  }
};

bool PatchInternalRel32(CodeBuilder& code, size_t dispOffset, size_t nextOffset, size_t targetOffset) {
  const int64_t delta = static_cast<int64_t>(targetOffset) - static_cast<int64_t>(nextOffset);
  if (delta < INT32_MIN || delta > INT32_MAX)
    return false;
  code.PatchI32(dispOffset, static_cast<int32_t>(delta));
  return true;
}

bool PatchExternalRel32(CodeBuilder& code,
                        uintptr_t cave,
                        size_t dispOffset,
                        size_t nextOffset,
                        uintptr_t target) {
  const int64_t delta = static_cast<int64_t>(target) -
                        static_cast<int64_t>(cave + nextOffset);
  if (delta < INT32_MIN || delta > INT32_MAX)
    return false;
  code.PatchI32(dispOffset, static_cast<int32_t>(delta));
  return true;
}

bool BuildSpecialCave(const std::vector<SpecialEdit>& edits,
                      uintptr_t gameBase,
                      uintptr_t& caveOut,
                      std::string* error) {
  CodeBuilder code;

  std::vector<std::vector<std::pair<size_t, size_t>>> mismatchPatches(edits.size());
  std::vector<size_t> blockOffsets(edits.size());
  std::vector<size_t> leaDisp(edits.size());
  std::vector<size_t> leaNext(edits.size());
  std::vector<size_t> newStringOffsets(edits.size());

  for (size_t i = 0; i < edits.size(); ++i) {
    blockOffsets[i] = code.Pos();

    for (size_t j = 0; j <= edits[i].oldDesc.size(); ++j) {
      const uint16_t ch = (j < edits[i].oldDesc.size())
                              ? static_cast<uint16_t>(edits[i].oldDesc[j])
                              : 0;

      // cmp word ptr [r12+disp32], imm16
      code.U8(0x66); code.U8(0x41); code.U8(0x81); code.U8(0xBC); code.U8(0x24);
      code.U32(static_cast<uint32_t>(j * 2));
      code.U16(ch);

      // jne nextBlock/original
      code.U8(0x0F); code.U8(0x85);
      const size_t disp = code.Pos();
      code.U32(0);
      const size_t next = code.Pos();
      mismatchPatches[i].push_back({disp, next});
    }

    // lea r12,[rip+newString]
    code.U8(0x4C); code.U8(0x8D); code.U8(0x25);
    leaDisp[i] = code.Pos();
    code.U32(0);
    leaNext[i] = code.Pos();

    // CT special block semantics:
    // mov rsi,r12
    code.U8(0x4C); code.U8(0x89); code.U8(0xE6);
    // mov qword ptr [rsp+D8],r12
    code.U8(0x4C); code.U8(0x89); code.U8(0xA4); code.U8(0x24);
    code.U32(0x000000D8);

    // jmp originalBlock
    code.U8(0xE9);
    const size_t disp = code.Pos();
    code.U32(0);
    const size_t next = code.Pos();
    mismatchPatches[i].push_back({disp, next});
  }

  const size_t originalBlock = code.Pos();

  // 원래 덮어쓴 5바이트를 그대로 재현: cmp word ptr [r12],r11w
  code.U8(0x66); code.U8(0x45); code.U8(0x39); code.U8(0x1C); code.U8(0x24);

  // 뒤의 기존 0F 84 B1 04 00 00 으로 복귀
  code.U8(0xE9);
  const size_t returnDisp = code.Pos();
  code.U32(0);
  const size_t returnNext = code.Pos();

  code.Align(8);
  for (size_t i = 0; i < edits.size(); ++i) {
    newStringOffsets[i] = code.Pos();
    for (wchar_t ch : edits[i].newDesc)
      code.U16(static_cast<uint16_t>(ch));
    code.U16(0);
    code.Align(2);
  }

  for (size_t i = 0; i < edits.size(); ++i) {
    const size_t mismatchTarget = (i + 1 < edits.size()) ? blockOffsets[i + 1] : originalBlock;
    for (size_t k = 0; k + 1 < mismatchPatches[i].size(); ++k) {
      if (!PatchInternalRel32(code,
                              mismatchPatches[i][k].first,
                              mismatchPatches[i][k].second,
                              mismatchTarget)) {
        SetError(error, "특수 설명 비교 분기 생성 실패");
        return false;
      }
    }

    const auto& toOriginal = mismatchPatches[i].back();
    if (!PatchInternalRel32(code, toOriginal.first, toOriginal.second, originalBlock) ||
        !PatchInternalRel32(code, leaDisp[i], leaNext[i], newStringOffsets[i])) {
      SetError(error, "특수 설명 코드케이브 내부 재배치 실패");
      return false;
    }
  }

  const uintptr_t hookAddress = gameBase + kSpecialHookOffset;
  const uintptr_t cave = AllocNear(hookAddress, kCaveSize);
  if (!cave) {
    SetError(error, "특수 설명 코드케이브 확보 실패");
    return false;
  }

  if (!PatchExternalRel32(code, cave, returnDisp, returnNext, gameBase + kSpecialReturnOffset)) {
    VirtualFree(reinterpret_cast<void*>(cave), 0, MEM_RELEASE);
    SetError(error, "특수 설명 원본 복귀 분기 재배치 실패");
    return false;
  }

  if (code.bytes.size() > kCaveSize ||
      !WriteProtected(cave, code.bytes.data(), code.bytes.size())) {
    VirtualFree(reinterpret_cast<void*>(cave), 0, MEM_RELEASE);
    SetError(error, "특수 설명 코드케이브 기록 실패");
    return false;
  }

  caveOut = cave;
  return true;
}

bool InstallHook(uintptr_t gameBase, uintptr_t cave, std::string* error) {
  const uintptr_t hookAddress = gameBase + kSpecialHookOffset;
  const int64_t delta = static_cast<int64_t>(cave) - static_cast<int64_t>(hookAddress + 5);
  if (delta < INT32_MIN || delta > INT32_MAX) {
    SetError(error, "특수 설명 후크가 rel32 범위를 벗어났습니다.");
    return false;
  }

  uint8_t patch[5] = {0xE9, 0, 0, 0, 0};
  const int32_t rel = static_cast<int32_t>(delta);
  std::memcpy(patch + 1, &rel, sizeof(rel));

  if (!WriteProtected(hookAddress, patch, sizeof(patch))) {
    SetError(error, "특수 설명 후크 설치 실패");
    return false;
  }
  return true;
}

} // namespace

bool ApplyTraitTextSpecialDescHook(std::string* error) {
  std::vector<SpecialEdit> edits;
  if (!CollectSpecialEdits(edits, error))
    return false;

  if (g_applied && !RemoveTraitTextSpecialDescHook(error))
    return false;

  if (edits.empty()) {
    SetError(error, "");
    return true;
  }

  const uintptr_t gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase) {
    SetError(error, "SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
    return false;
  }

  const uintptr_t hookAddress = gameBase + kSpecialHookOffset;
  if (!MatchBytes(hookAddress, kSpecialAssertBytes, sizeof(kSpecialAssertBytes))) {
    SetError(error, "SAN8RPK.exe+146E5 원본 바이트가 다릅니다. 기존 특수 설명 패치를 먼저 끄세요.");
    return false;
  }

  uintptr_t cave = 0;
  if (!BuildSpecialCave(edits, gameBase, cave, error))
    return false;

  if (!InstallHook(gameBase, cave, error)) {
    VirtualFree(reinterpret_cast<void*>(cave), 0, MEM_RELEASE);
    return false;
  }

  g_applied = true;
  g_gameBase = gameBase;
  g_currentCave = cave;
  AddLog(u8"[기재 문구/Step4] 특수 설명 치환 후크 적용: %zu개", edits.size());
  return true;
}

bool RemoveTraitTextSpecialDescHook(std::string* error) {
  if (!g_applied)
    return true;

  const uintptr_t currentBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!currentBase || currentBase != g_gameBase) {
    SetError(error, "적용 당시와 게임 베이스 주소가 달라 특수 설명 후크를 해제할 수 없습니다.");
    return false;
  }

  const uintptr_t hookAddress = g_gameBase + kSpecialHookOffset;
  if (!WriteProtected(hookAddress, kSpecialOriginal5, sizeof(kSpecialOriginal5))) {
    SetError(error, "특수 설명 원본 바이트 복구 실패");
    return false;
  }

  if (g_currentCave)
    g_retiredCaves.push_back(g_currentCave);

  g_applied = false;
  g_gameBase = 0;
  g_currentCave = 0;
  AddLog(u8"[기재 문구/Step4] 특수 설명 치환 후크 해제");
  return true;
}

bool IsTraitTextSpecialDescHookApplied() {
  return g_applied;
}

} // namespace DX11Base
