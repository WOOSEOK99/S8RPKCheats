#include "pch.h"
#include "TraitTextNameHook.h"

#include "TraitTextEditorData.h"
#include "../../Hooking/MinHook.h"
#include "../../showlog.h"

#include <windows.h>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
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

struct NameEditTable {
  std::vector<NameEdit> edits;
};

using TraitNameGetter = const wchar_t* (__fastcall*)(void*);

enum class NameHookBackend {
  None,
  VersionDll,
  RuntimeOverlay,
};

static NameHookBackend g_activeBackend = NameHookBackend::None;

// version.dll이 있는 경우 v0.830 방식에서 사용하는 상태입니다.
static bool g_versionApplied = false;
static uintptr_t g_versionGameBase = 0;
static uintptr_t g_versionOriginalTarget = 0;
static uintptr_t g_versionCurrentCave = 0;

// CE의 globalalloc과 같은 수명 정책을 따릅니다.
// 게임 UI가 이전 반환 문자열 포인터를 보유할 수 있으므로 재적용/해제 시 코드케이브를 즉시 해제하지 않습니다.
static std::vector<uintptr_t> g_retiredVersionCaves;

// version.dll이 없는 경우 현재(어제 수정한) 런타임 이름 훅 체인 방식에서 사용하는 상태입니다.
static bool g_hookInstalled = false;
static uintptr_t g_hookTarget = 0;
static TraitNameGetter g_originalGetter = nullptr;
static std::atomic<const NameEditTable*> g_activeTable{nullptr};

// 게임 UI가 이전 반환 문자열 포인터를 잠시 보유할 수 있으므로 이미 공개된 문자열 세대는
// 프로세스 수명 동안 유지합니다. 동일 설정 재적용은 새 세대를 만들지 않습니다.
static std::vector<std::unique_ptr<NameEditTable>> g_generations;

void SetError(std::string* error, const std::string& text) {
  if (error)
    *error = text;
}

bool SafeRead(uintptr_t address, void* out, size_t size) {
  if (!address || !out || size == 0)
    return false;
  __try {
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    std::memset(out, 0, size);
    return false;
  }
}

bool MatchBytes(uintptr_t address, const uint8_t* expected, size_t size) {
  std::vector<uint8_t> current(size);
  return SafeRead(address, current.data(), size) &&
         std::memcmp(current.data(), expected, size) == 0;
}

bool ReadPointer(uintptr_t address, uintptr_t& value) {
  return SafeRead(address, &value, sizeof(value));
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

bool IsExecutableAddress(uintptr_t address) {
  if (address < 0x10000)
    return false;

  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) != sizeof(mbi))
    return false;
  if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
    return false;

  const DWORD protect = mbi.Protect & 0xFF;
  return protect == PAGE_EXECUTE ||
         protect == PAGE_EXECUTE_READ ||
         protect == PAGE_EXECUTE_READWRITE ||
         protect == PAGE_EXECUTE_WRITECOPY;
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

bool HasExternalGameVersionDll() {
  wchar_t exePath[MAX_PATH] = {};
  const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  if (len == 0 || len >= MAX_PATH)
    return false;

  const std::filesystem::path versionPath =
      std::filesystem::path(exePath).parent_path() / L"version.dll";
  std::error_code ec;
  return std::filesystem::is_regular_file(versionPath, ec) && !ec;
}

// -----------------------------------------------------------------------------
// version.dll 있음: v0.830 방식
// -----------------------------------------------------------------------------

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

bool VerifyVersionDllNameGetter(uintptr_t gameBase, uintptr_t versionBase, std::string* error) {
  if (!MatchBytes(gameBase + kNameGetterOffset, kNameGetterStub, sizeof(kNameGetterStub))) {
    SetError(error, "SAN8RPK.exe+170E2D0 이름 getter 스텁이 v0.830 예상 바이트와 다릅니다.");
    return false;
  }

  if (!MatchBytes(versionBase + kVersionNameGetterOffset,
                  kVersionGetterPrologue, sizeof(kVersionGetterPrologue))) {
    SetError(error, "version.dll+5CB0 이름 getter 원본 바이트가 v0.830 예상과 다릅니다.");
    return false;
  }

  return true;
}

bool IsVersionDllNameHookApplied() {
  if (!g_versionApplied || !g_versionGameBase || !g_versionCurrentCave)
    return false;

  uintptr_t current = 0;
  return ReadPointer(g_versionGameBase + kNameGetterPointerOffset, current) &&
         current == g_versionCurrentCave;
}

bool RemoveVersionDllNameHook(std::string* error) {
  if (!g_versionApplied) {
    if (g_activeBackend == NameHookBackend::VersionDll)
      g_activeBackend = NameHookBackend::None;
    return true;
  }

  if (!g_versionGameBase || !g_versionOriginalTarget || !g_versionCurrentCave) {
    SetError(error, "기재 이름 version.dll 후크 내부 상태가 유효하지 않습니다.");
    return false;
  }

  uintptr_t current = 0;
  if (!ReadPointer(g_versionGameBase + kNameGetterPointerOffset, current)) {
    SetError(error, "기재 이름 getter 포인터 읽기 실패");
    return false;
  }

  if (current != g_versionCurrentCave) {
    if (current == g_versionOriginalTarget) {
      g_versionApplied = false;
      g_versionCurrentCave = 0;
      if (g_activeBackend == NameHookBackend::VersionDll)
        g_activeBackend = NameHookBackend::None;
      return true;
    }
    SetError(error, "기재 이름 getter가 다른 패치에 의해 변경되어 있어 복구를 중단했습니다.");
    return false;
  }

  if (!WritePointer(g_versionGameBase + kNameGetterPointerOffset, g_versionOriginalTarget)) {
    SetError(error, "기재 이름 getter 원본 포인터 복구 실패");
    return false;
  }

  g_retiredVersionCaves.push_back(g_versionCurrentCave);
  g_versionCurrentCave = 0;
  g_versionApplied = false;
  if (g_activeBackend == NameHookBackend::VersionDll)
    g_activeBackend = NameHookBackend::None;
  AddLog(u8"[기재 편집/이름] version.dll(v0.830) 이름 치환 후크 해제");
  return true;
}

bool ApplyVersionDllNameHook(const std::vector<NameEdit>& edits,
                             uintptr_t versionBase,
                             std::string* error) {
  // v0.830의 S.apply()와 동일하게 재적용 전 기존 후크부터 복구합니다.
  if (g_versionApplied && !RemoveVersionDllNameHook(error))
    return false;

  if (edits.empty()) {
    g_activeBackend = NameHookBackend::None;
    AddLog(u8"[기재 편집/이름] version.dll(v0.830): 변경된 기재명이 없어 이름 후크를 적용하지 않습니다.");
    return true;
  }

  const uintptr_t gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase || !versionBase) {
    SetError(error, "SAN8RPK.exe 또는 version.dll 모듈을 찾지 못했습니다.");
    return false;
  }

  if (!VerifyVersionDllNameGetter(gameBase, versionBase, error))
    return false;

  const uintptr_t expectedOriginal = versionBase + kVersionNameGetterOffset;
  uintptr_t currentTarget = 0;
  if (!ReadPointer(gameBase + kNameGetterPointerOffset, currentTarget)) {
    SetError(error, "SAN8RPK.exe+170E2D6 이름 getter 포인터 읽기 실패");
    return false;
  }

  // v0.830의 assert(readQword(...+170E2D6)==version.dll+5CB0)와 동일한 충돌 방지.
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

  g_versionGameBase = gameBase;
  g_versionOriginalTarget = expectedOriginal;
  g_versionCurrentCave = cave;
  g_versionApplied = true;
  g_activeBackend = NameHookBackend::VersionDll;

  AddLog(u8"[기재 편집/이름] version.dll 감지 -> v0.830 이름 치환 후크 ON (%zu개)", edits.size());
  return true;
}

// -----------------------------------------------------------------------------
// version.dll 없음: 현재 런타임 이름 훅 체인 방식
// -----------------------------------------------------------------------------

bool SameEdits(const NameEditTable* table, const std::vector<NameEdit>& edits) {
  if (!table || table->edits.size() != edits.size())
    return false;
  for (size_t i = 0; i < edits.size(); ++i) {
    if (table->edits[i].oldName != edits[i].oldName ||
        table->edits[i].newName != edits[i].newName)
      return false;
  }
  return true;
}

// MinHook가 SAN8RPK.exe+170E2D0에 설치한 E9 relay를 따라가 현재 기재JSON 이름
// detour의 실제 함수 주소를 얻습니다. 이 경로는 version.dll이 없을 때만 사용합니다.
bool ResolveRuntimeNameDetour(uintptr_t gameBase, uintptr_t& detourOut, std::string* error) {
  detourOut = 0;
  const uintptr_t target = gameBase + kNameGetterOffset;

  uint8_t first[16] = {};
  if (!SafeRead(target, first, sizeof(first))) {
    SetError(error, "SAN8RPK.exe+170E2D0 읽기 실패");
    return false;
  }

  if (first[0] != 0xE9) {
    SetError(error, "기재JSON 이름 훅이 아직 준비되지 않았습니다. 잠시 후 다시 적용하세요.");
    return false;
  }

  int32_t rel = 0;
  std::memcpy(&rel, first + 1, sizeof(rel));
  const uintptr_t relay = target + 5 + static_cast<int64_t>(rel);
  if (!IsExecutableAddress(relay)) {
    SetError(error, "기재JSON 이름 훅 relay 주소가 유효하지 않습니다.");
    return false;
  }

  uint8_t relayBytes[16] = {};
  if (!SafeRead(relay, relayBytes, sizeof(relayBytes))) {
    SetError(error, "기재JSON 이름 훅 relay 읽기 실패");
    return false;
  }

  uintptr_t detour = 0;
  if (relayBytes[0] == 0xFF && relayBytes[1] == 0x25 &&
      relayBytes[2] == 0x00 && relayBytes[3] == 0x00 &&
      relayBytes[4] == 0x00 && relayBytes[5] == 0x00) {
    if (!SafeRead(relay + 6, &detour, sizeof(detour))) {
      SetError(error, "기재JSON 이름 detour 포인터 읽기 실패");
      return false;
    }
  } else {
    // 일부 MinHook 빌드는 relay 자체가 직접 실행 가능한 detour 진입점일 수 있습니다.
    detour = relay;
  }

  if (!IsExecutableAddress(detour)) {
    SetError(error, "기재JSON 이름 detour 주소가 실행 가능 영역이 아닙니다.");
    return false;
  }

  detourOut = detour;
  AddLog(u8"[기재 편집/이름] 기존 기재JSON 이름 훅 연결 확인: game=%p relay=%p detour=%p",
         reinterpret_cast<void*>(target), reinterpret_cast<void*>(relay),
         reinterpret_cast<void*>(detour));
  return true;
}

const wchar_t* __fastcall TraitTextNameEditorDetour(void* traitObject) {
  const wchar_t* original = g_originalGetter ? g_originalGetter(traitObject) : nullptr;
  if (!original || original[0] == L'\0')
    return original;

  const NameEditTable* table = g_activeTable.load(std::memory_order_acquire);
  if (!table)
    return original;

  for (const auto& edit : table->edits) {
    if (std::wcscmp(original, edit.oldName.c_str()) == 0)
      return edit.newName.c_str();
  }
  return original;
}

bool EnsureEditorHookInstalled(std::string* error) {
  if (g_hookInstalled)
    return true;

  const uintptr_t gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase) {
    SetError(error, "SAN8RPK.exe 모듈을 찾지 못했습니다.");
    return false;
  }

  uintptr_t detour = 0;
  if (!ResolveRuntimeNameDetour(gameBase, detour, error))
    return false;

  LPVOID original = nullptr;
  const MH_STATUS createStatus = MH_CreateHook(
      reinterpret_cast<LPVOID>(detour),
      reinterpret_cast<LPVOID>(&TraitTextNameEditorDetour),
      &original);
  if (createStatus != MH_OK) {
    SetError(error, std::string("기재 이름 편집 연결 훅 생성 실패: ") + MH_StatusToString(createStatus));
    return false;
  }

  const MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(detour));
  if (enableStatus != MH_OK) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(detour));
    SetError(error, std::string("기재 이름 편집 연결 훅 활성화 실패: ") + MH_StatusToString(enableStatus));
    return false;
  }

  g_hookTarget = detour;
  g_originalGetter = reinterpret_cast<TraitNameGetter>(original);
  g_hookInstalled = true;
  AddLog(u8"[기재 편집/이름] version.dll 없음 -> 런타임 연결 훅 설치 완료: target=%p trampoline=%p",
         reinterpret_cast<void*>(g_hookTarget), reinterpret_cast<void*>(g_originalGetter));
  return true;
}

bool ApplyRuntimeNameOverlay(std::vector<NameEdit> edits, std::string* error) {
  if (edits.empty()) {
    g_activeTable.store(nullptr, std::memory_order_release);
    if (g_activeBackend == NameHookBackend::RuntimeOverlay)
      g_activeBackend = NameHookBackend::None;
    AddLog(u8"[기재 편집/이름] 변경된 기재명이 없어 이름 오버레이를 적용하지 않습니다.");
    return true;
  }

  const NameEditTable* current = g_activeTable.load(std::memory_order_acquire);
  if (SameEdits(current, edits)) {
    g_activeBackend = NameHookBackend::RuntimeOverlay;
    AddLog(u8"[기재 편집/이름] 동일 이름 설정 재적용 생략");
    return true;
  }

  if (!EnsureEditorHookInstalled(error))
    return false;

  auto generation = std::make_unique<NameEditTable>();
  generation->edits = std::move(edits);
  const NameEditTable* published = generation.get();
  g_generations.push_back(std::move(generation));
  g_activeTable.store(published, std::memory_order_release);
  g_activeBackend = NameHookBackend::RuntimeOverlay;

  AddLog(u8"[기재 편집/이름] version.dll 없음 -> 이름 오버레이 적용 완료 (%zu개)", published->edits.size());
  return true;
}

} // namespace

bool IsTraitTextNameHookApplied() {
  switch (g_activeBackend) {
  case NameHookBackend::VersionDll:
    return IsVersionDllNameHookApplied();
  case NameHookBackend::RuntimeOverlay:
    return g_activeTable.load(std::memory_order_acquire) != nullptr;
  default:
    return false;
  }
}

bool RemoveTraitTextNameHook(std::string* error) {
  if (g_activeBackend == NameHookBackend::VersionDll || g_versionApplied)
    return RemoveVersionDllNameHook(error);

  if (g_activeBackend == NameHookBackend::RuntimeOverlay ||
      g_activeTable.load(std::memory_order_acquire) != nullptr) {
    // 연결 훅 자체는 유지하고 오버레이만 비활성화합니다. 재적용 시 MinHook/code cave를
    // 다시 만들지 않으므로 T06의 반복 적용 누적도 방지합니다.
    g_activeTable.store(nullptr, std::memory_order_release);
    g_activeBackend = NameHookBackend::None;
    AddLog(u8"[기재 편집/이름] 이름 오버레이 해제 (연결 훅 유지)");
  }
  return true;
}

bool ApplyTraitTextNameHook(std::string* error) {
  std::vector<NameEdit> edits;
  if (!CollectNameEdits(edits, error))
    return false;

  if (HasExternalGameVersionDll()) {
    // 게임 폴더에 version.dll이 있으면 내장 기재 런타임도 비활성화되므로,
    // 이름 편집 역시 v0.830의 version.dll+5CB0 경로를 그대로 사용합니다.
    if (g_activeBackend == NameHookBackend::RuntimeOverlay) {
      g_activeTable.store(nullptr, std::memory_order_release);
      g_activeBackend = NameHookBackend::None;
    }

    const uintptr_t versionBase =
        reinterpret_cast<uintptr_t>(GetModuleHandleW(L"version.dll"));
    if (!versionBase) {
      SetError(error, "게임 폴더에 version.dll이 있지만 모듈이 로드되지 않았습니다.");
      return false;
    }

    return ApplyVersionDllNameHook(edits, versionBase, error);
  }

  // version.dll이 없으면 어제 수정한 런타임 이름 훅 체인 방식을 유지합니다.
  if (g_activeBackend == NameHookBackend::VersionDll &&
      !RemoveVersionDllNameHook(error)) {
    return false;
  }

  return ApplyRuntimeNameOverlay(std::move(edits), error);
}

} // namespace DX11Base
