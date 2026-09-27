#include "pch.h"
#include "TraitTextNameHook.h"

#include "TraitTextEditorData.h"
#include "../../Hooking/MinHook.h"
#include "../../showlog.h"

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace DX11Base {
namespace {

constexpr uintptr_t kNameGetterOffset = 0x170E2D0;

struct NameEdit {
  std::wstring oldName;
  std::wstring newName;
};

struct NameEditTable {
  std::vector<NameEdit> edits;
};

using TraitNameGetter = const wchar_t* (__fastcall*)(void*);

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
// detour의 실제 함수 주소를 얻습니다. version.dll은 전혀 사용하지 않습니다.
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
  AddLog(u8"[기재 편집/이름] version.dll 독립 연결 훅 설치 완료: target=%p trampoline=%p",
         reinterpret_cast<void*>(g_hookTarget), reinterpret_cast<void*>(g_originalGetter));
  return true;
}

} // namespace

bool IsTraitTextNameHookApplied() {
  return g_activeTable.load(std::memory_order_acquire) != nullptr;
}

bool RemoveTraitTextNameHook(std::string* error) {
  (void)error;
  if (!IsTraitTextNameHookApplied())
    return true;

  // 연결 훅 자체는 유지하고 오버레이만 비활성화합니다. 재적용 시 MinHook/code cave를
  // 다시 만들지 않으므로 T06의 반복 적용 누적도 방지합니다.
  g_activeTable.store(nullptr, std::memory_order_release);
  AddLog(u8"[기재 편집/이름] 이름 오버레이 해제 (연결 훅 유지)");
  return true;
}

bool ApplyTraitTextNameHook(std::string* error) {
  std::vector<NameEdit> edits;
  if (!CollectNameEdits(edits, error))
    return false;

  if (edits.empty()) {
    g_activeTable.store(nullptr, std::memory_order_release);
    AddLog(u8"[기재 편집/이름] 변경된 기재명이 없어 이름 오버레이를 적용하지 않습니다.");
    return true;
  }

  const NameEditTable* current = g_activeTable.load(std::memory_order_acquire);
  if (SameEdits(current, edits)) {
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

  AddLog(u8"[기재 편집/이름] 이름 오버레이 적용 완료 (%zu개)", published->edits.size());
  return true;
}

} // namespace DX11Base
