#include "../../pch.h"

#include "../../Cheats.h"
#include "../../Hooking/MinHook.h"
#include "../../showlog.h"
#include "../../EmbeddedJsonResources.h"
#include "TraitConfigRuntime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <intrin.h>
#include <iterator>
#include <string>
#include <sstream>
#include <utility>

namespace DX11Base {

extern HMODULE g_hModule;

namespace {

constexpr int kTraitCount = 254;
constexpr std::size_t kTraitStride = 0x40;
constexpr std::size_t kTraitIdOffset = 0x08;
constexpr std::size_t kEffectOffset = 0x0A;
constexpr std::size_t kEffectCount = 6;
constexpr std::size_t kEffectSize = 0x06;
constexpr std::size_t kMetadataOffset = 0x2E;
constexpr std::size_t kMetadataSize = 0x12;
constexpr uint32_t kTraitsPointerOffsetFallback = 0x57A4D0;
constexpr int kDefaultCloneSource = 30; // version.dll Normalize()와 동일한 기본 donor index
constexpr uintptr_t kNameGetterOffset = 0x170E2D0;
constexpr uintptr_t kDescMapGetterOffset = 0x171E070;
constexpr uintptr_t kTraitEffectQueryOffset = 0x17AAD60;
constexpr uintptr_t kTraitEffectQuerySkipCallerOffset = 0x1C7A135;
constexpr uintptr_t kOfficerTraitQueryOffset = 0x170C080;
constexpr uintptr_t kTraitMessageSetterOffset = 0x13F91E0;
constexpr uintptr_t kTraitDataGetterOffset = 0x16E63F0;
constexpr uintptr_t kMonthlyTraitUpdateOffset = 0x1C79C90;
constexpr uintptr_t kTransferTraitEventOffset = 0x18A5E30;
constexpr uintptr_t kTraitEligibilityPatchAOffset = 0x17C03E7;
constexpr uintptr_t kTraitEligibilityPatchBOffset = 0x17C042F;

constexpr uint8_t kTraitEffectQueryPrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08
};
constexpr uint8_t kOfficerTraitQueryPrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08
};
constexpr uint8_t kTraitMessageSetterPrologue[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20
};
constexpr uint8_t kTraitDataGetterPrologue[] = {
    0x48, 0x83, 0xEC, 0x28, 0x8D, 0x42, 0xFF
};
constexpr uint8_t kMonthlyTraitUpdatePrologue[] = {
    0x4C, 0x8B, 0xDC, 0x53, 0x41, 0x54, 0x41, 0x57
};
constexpr uint8_t kTransferTraitEventPrologue[] = {
    0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56
};
constexpr uint8_t kTraitEligibilityExpected[] = {
    0x81, 0xFE, 0x95, 0x00, 0x00, 0x00
};
constexpr uintptr_t kDescTextPointerFallbackOffset = 0x2C34EC0;
constexpr uint32_t kDescNormalIndexBase = 0x2E73;
constexpr uint32_t kDescFormatIndexBase = 0x3005;
constexpr int kDescTableCustomIndexMin = 70;
constexpr int kDescTableCustomIndexMax = 199;

// version.dll descmap 설치 시 검증하는 +171E070 원본 5바이트와 동일.
constexpr uint8_t kDescMapGetterPrologue[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08
};

#pragma pack(push, 1)
struct TraitEffect {
  uint16_t type = 0;
  uint16_t value = 0;
  uint16_t param = 0;
};
#pragma pack(pop)

static_assert(sizeof(TraitEffect) == kEffectSize, "TraitEffect layout mismatch");

struct TraitConfigEntry {
  bool present = false;
  int id = 0;
  std::array<TraitEffect, kEffectCount> effects{};
};

struct TraitMetaEntry {
  bool present = false;
  bool hasCustomText = false;
  std::string name;
  std::string desc;
  int cloneSrc = -1;
  int bgTrait = -1;
  int grade = -1;
};

struct RuntimeState {
  std::array<TraitConfigEntry, kTraitCount> traits{};
  std::array<TraitMetaEntry, kTraitCount> meta{};

  std::filesystem::path configPath;
  std::filesystem::file_time_type configWriteTime{};
  bool hasWriteTime = false;
  bool configLoaded = false;
  bool configFromEmbedded = false;
  bool applyPending = false;

  uint32_t traitsPointerOffset = 0;
  bool offsetResolved = false;

  uintptr_t lastTableBase = 0;
  int sentinelIndex = -1;

  ULONGLONG lastTickMs = 0;
  ULONGLONG lastConfigProbeMs = 0;

  std::array<const wchar_t *, kTraitCount> nativeNamePtrs{};
  std::array<const wchar_t *, kTraitCount> nativeDescPtrs{};
  std::array<const wchar_t *, kTraitCount> nativeDescFormatPtrs{};

  bool nameHookInstalled = false;
  bool nameHookFailureLogged = false;
  uintptr_t originalNameGetter = 0;

  bool descHookInstalled = false;
  bool descHookFailureLogged = false;
  uintptr_t originalDescMapGetter = 0;

  bool descTablesSynced = false;
  bool descTableFailureLogged = false;
  uintptr_t descTextPointerAddress = 0;

  bool traitEffectHookInstalled = false;
  bool traitEffectHookFailureLogged = false;
  uintptr_t originalTraitEffectQuery = 0;

  bool officerTraitHookInstalled = false;
  bool officerTraitHookFailureLogged = false;
  uintptr_t originalOfficerTraitQuery = 0;

  bool traitMessageHookInstalled = false;
  bool traitMessageHookFailureLogged = false;
  uintptr_t originalTraitMessageSetter = 0;

  bool traitDataHookInstalled = false;
  bool traitDataHookFailureLogged = false;
  uintptr_t originalTraitDataGetter = 0;

  bool monthlyHookInstalled = false;
  bool monthlyHookFailureLogged = false;
  uintptr_t originalMonthlyUpdate = 0;

  bool transferHookInstalled = false;
  bool transferHookFailureLogged = false;
  uintptr_t originalTransferEvent = 0;

  bool eligibilityPatchAActive = false;
  bool eligibilityPatchBActive = false;
  bool eligibilityPatchAFailureLogged = false;
  bool eligibilityPatchBFailureLogged = false;
};

RuntimeState &State() {
  static RuntimeState state;
  return state;
}


using TraitNameGetter = const wchar_t *(__fastcall *)(void *);
using TraitDescMapGetter = const wchar_t *(__fastcall *)(void *, int, uint8_t);
using TraitEffectQuery = bool(__fastcall *)(void *, uint16_t);
using OfficerTraitQuery = int(__fastcall *)(void *, uint32_t);
using TraitMessageSetter = void(__fastcall *)(void *, uint32_t);
using TraitDataGetter = void *(__fastcall *)(void *, uint32_t);
using MonthlyTraitUpdate = int(__fastcall *)(void *);
using TransferTraitEvent = int(__fastcall *)(void *, void *, void *, void *, void *);

struct TraitEffectMatchCache {
  bool valid = false;
  uint16_t requestedTraitId = 0;
  uint16_t matchedCustomTraitId = 0;
  uintptr_t queryReturnRva = 0;
};

thread_local TraitEffectMatchCache g_traitEffectMatchCache;

bool IsExecutableAddress(uintptr_t address) {
  if (address < 0x10000)
    return false;

  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(reinterpret_cast<const void *>(address), &mbi, sizeof(mbi)) != sizeof(mbi))
    return false;
  if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
    return false;

  const DWORD protect = mbi.Protect & 0xFF;
  return protect == PAGE_EXECUTE ||
         protect == PAGE_EXECUTE_READ ||
         protect == PAGE_EXECUTE_READWRITE ||
         protect == PAGE_EXECUTE_WRITECOPY;
}

bool ReadMemorySafe(uintptr_t address, void *out, std::size_t size) {
  if (!address || !out || size == 0)
    return false;
  __try {
    std::memcpy(out, reinterpret_cast<const void *>(address), size);
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    std::memset(out, 0, size);
    return false;
  }
}

bool WritePointerSafe(uintptr_t address, uintptr_t value) {
  DWORD oldProtect = 0;
  void *dst = reinterpret_cast<void *>(address);
  if (!VirtualProtect(dst, sizeof(value), PAGE_EXECUTE_READWRITE, &oldProtect))
    return false;

  bool ok = true;
  __try {
    std::memcpy(dst, &value, sizeof(value));
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    ok = false;
  }

  DWORD ignored = 0;
  VirtualProtect(dst, sizeof(value), oldProtect, &ignored);
  if (ok)
    FlushInstructionCache(GetCurrentProcess(), dst, sizeof(value));
  return ok;
}

bool WriteMemoryProtected(uintptr_t address, const void *data, std::size_t size) {
  if (!address || !data || size == 0)
    return false;

  DWORD oldProtect = 0;
  void *dst = reinterpret_cast<void *>(address);
  if (!VirtualProtect(dst, size, PAGE_READWRITE, &oldProtect))
    return false;

  bool ok = true;
  __try {
    std::memcpy(dst, data, size);
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    ok = false;
  }

  DWORD ignored = 0;
  VirtualProtect(dst, size, oldProtect, &ignored);
  return ok;
}

bool WriteExecutableByteProtected(uintptr_t address, uint8_t value) {
  if (!address)
    return false;

  DWORD oldProtect = 0;
  void *dst = reinterpret_cast<void *>(address);
  if (!VirtualProtect(dst, sizeof(value), PAGE_EXECUTE_READWRITE, &oldProtect))
    return false;

  bool ok = true;
  __try {
    *reinterpret_cast<volatile uint8_t *>(address) = value;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    ok = false;
  }

  DWORD ignored = 0;
  VirtualProtect(dst, sizeof(value), oldProtect, &ignored);
  if (ok)
    FlushInstructionCache(GetCurrentProcess(), dst, sizeof(value));
  return ok;
}



const wchar_t *__fastcall CustomTraitNameGetter(void *traitObject) {
  uint16_t id = 0;
  if (traitObject) {
    __try {
      id = *reinterpret_cast<const uint16_t *>(
          reinterpret_cast<uintptr_t>(traitObject) + kTraitIdOffset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
      id = 0;
    }
  }

  RuntimeState &state = State();
  if (id >= 1 && id <= kTraitCount) {
    const wchar_t *custom = state.nativeNamePtrs[static_cast<std::size_t>(id - 1)];
    if (custom && custom[0] != L'\0')
      return custom;
  }

  const uintptr_t original = state.originalNameGetter;
  if (!original)
    return L"";
  return reinterpret_cast<TraitNameGetter>(original)(traitObject);
}

const wchar_t *__fastcall CustomTraitDescMapGetter(
    void *context, int traitId, uint8_t kind) {
  RuntimeState &state = State();

  // version.dll과 동일하게 반드시 원본을 먼저 호출합니다.
  // 원본 내부 상태 갱신/참조 처리를 건너뛰면 목록 종료 시 정리 경로가 멎을 수 있습니다.
  const uintptr_t original = state.originalDescMapGetter;
  if (original) {
    const wchar_t *originalResult =
        reinterpret_cast<TraitDescMapGetter>(original)(
            context, traitId, kind);
    if (originalResult)
      return originalResult;
  }

  // version.dll descmap fallback 범위는 ID 201~300.
  // 우리 JSON은 254까지이므로 201~254만 사용자 설명을 반환합니다.
  if (traitId < 201 || traitId > kTraitCount)
    return L"";

  const std::size_t index = static_cast<std::size_t>(traitId - 1);
  const wchar_t *custom =
      (kind == 2) ? state.nativeDescFormatPtrs[index]
                  : state.nativeDescPtrs[index];
  if (!custom || custom[0] == L'\0')
    return L"";

  // version.dll은 고정 포인터를 직접 반환하지 않고 TLS scratch buffer에 복사합니다.
  // kind==2: 최대 0x7FE wchar, 일반: 최대 0xFE wchar.
  thread_local std::array<wchar_t, 0x800> formatBuffer{};
  thread_local std::array<wchar_t, 0x100> normalBuffer{};

  wchar_t *buffer = nullptr;
  std::size_t maxChars = 0;
  if (kind == 2) {
    buffer = formatBuffer.data();
    maxChars = 0x7FE;
  } else {
    buffer = normalBuffer.data();
    maxChars = 0xFE;
  }

  const std::size_t length = std::min<std::size_t>(
      std::wcslen(custom), maxChars);
  std::memcpy(buffer, custom, length * sizeof(wchar_t));
  buffer[length] = L'\0';
  return buffer;
}


bool Utf8ToWide(const std::string &text, std::wstring &out) {
  out.clear();
  if (text.empty())
    return true;

  const int count = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS,
      text.data(), static_cast<int>(text.size()),
      nullptr, 0);
  if (count <= 0)
    return false;

  out.resize(static_cast<std::size_t>(count));
  return MultiByteToWideChar(
             CP_UTF8, MB_ERR_INVALID_CHARS,
             text.data(), static_cast<int>(text.size()),
             out.data(), count) == count;
}

const wchar_t *AllocateStableWideText(const std::wstring &wide) {
  if (wide.empty())
    return nullptr;

  const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
  void *memory =
      VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!memory)
    return nullptr;

  std::memcpy(memory, wide.c_str(), bytes);
  return reinterpret_cast<const wchar_t *>(memory);
}

const wchar_t *AllocateStableWideString(const std::string &utf8) {
  std::wstring wide;
  if (!Utf8ToWide(utf8, wide) || wide.empty())
    return nullptr;
  return AllocateStableWideText(wide);
}

const wchar_t *AllocateStableFormatWideString(const std::string &utf8) {
  std::wstring wide;
  if (!Utf8ToWide(utf8, wide) || wide.empty())
    return nullptr;

  // version.dll descmap은 kind==2 경로에서 '%'를 '%%'로 복제한 문자열을 사용합니다.
  std::wstring escaped;
  escaped.reserve(wide.size() + 8);
  for (wchar_t ch : wide) {
    escaped.push_back(ch);
    if (ch == L'%')
      escaped.push_back(L'%');
  }
  return AllocateStableWideText(escaped);
}

bool PublishNativeTexts(
    const std::array<TraitMetaEntry, kTraitCount> &meta) {
  RuntimeState &state = State();

  std::array<const wchar_t *, kTraitCount> nextNames{};
  std::array<const wchar_t *, kTraitCount> nextDescs{};
  std::array<const wchar_t *, kTraitCount> nextFormatDescs{};

  int nameCount = 0;
  int descCount = 0;

  for (int i = 0; i < kTraitCount; ++i) {
    if (!meta[i].present)
      continue;

    if (!meta[i].name.empty()) {
      const wchar_t *wide = AllocateStableWideString(meta[i].name);
      if (!wide) {
        AddLog(u8"[기재JSON/문구] 이름 UTF-8 변환/할당 실패: index=%d", i);
        return false;
      }
      nextNames[i] = wide;
      ++nameCount;
    }

    if (!meta[i].desc.empty()) {
      const wchar_t *wide = AllocateStableWideString(meta[i].desc);
      const wchar_t *formatWide =
          AllocateStableFormatWideString(meta[i].desc);
      if (!wide || !formatWide) {
        AddLog(u8"[기재JSON/문구] 설명 UTF-8 변환/할당 실패: index=%d", i);
        return false;
      }
      nextDescs[i] = wide;
      nextFormatDescs[i] = formatWide;
      ++descCount;
    }
  }

  // 게임 UI가 이전 반환 문자열 포인터를 보유할 수 있으므로 이전 VirtualAlloc 문자열은
  // 즉시 해제하지 않고 새 포인터 배열만 교체합니다.
  state.nativeNamePtrs = nextNames;
  state.nativeDescPtrs = nextDescs;
  state.nativeDescFormatPtrs = nextFormatDescs;

  AddLog(u8"[기재JSON/문구] JSON 문구 준비 완료: 이름 %d개 / 설명 %d개",
         nameCount, descCount);
  return true;
}

bool HasPublishedCustomNames() {
  RuntimeState &state = State();
  for (const wchar_t *name : state.nativeNamePtrs) {
    if (name && name[0] != L'\0')
      return true;
  }
  return false;
}

bool EnsureCustomNameHook() {
  RuntimeState &state = State();
  if (!HasPublishedCustomNames())
    return true;

  if (state.nameHookInstalled)
    return true;

  const uintptr_t gameBase =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase)
    return false;

  const uintptr_t target = gameBase + kNameGetterOffset;
  if (!IsExecutableAddress(target)) {
    if (!state.nameHookFailureLogged) {
      AddLog(u8"[기재JSON/이름] 원본 이름 getter가 실행 가능 메모리가 아닙니다: %p",
             reinterpret_cast<void *>(target));
      state.nameHookFailureLogged = true;
    }
    return false;
  }

  // kiero가 DX11 초기화 시 MinHook을 이미 초기화하지만,
  // 호출 순서 차이에 대비해 여기서도 안전하게 초기화를 보장합니다.
  const MH_STATUS initStatus = MH_Initialize();
  if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
    if (!state.nameHookFailureLogged) {
      AddLog(u8"[기재JSON/이름] MinHook 초기화 실패: %s",
             MH_StatusToString(initStatus));
      state.nameHookFailureLogged = true;
    }
    return false;
  }

  LPVOID original = nullptr;
  const MH_STATUS createStatus =
      MH_CreateHook(reinterpret_cast<LPVOID>(target),
                    reinterpret_cast<LPVOID>(&CustomTraitNameGetter),
                    &original);
  if (createStatus != MH_OK) {
    if (!state.nameHookFailureLogged) {
      AddLog(u8"[기재JSON/이름] 이름 getter 직접 훅 생성 실패: %s / target=%p",
             MH_StatusToString(createStatus),
             reinterpret_cast<void *>(target));
      state.nameHookFailureLogged = true;
    }
    return false;
  }

  if (!original || !IsExecutableAddress(reinterpret_cast<uintptr_t>(original))) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    if (!state.nameHookFailureLogged) {
      AddLog(u8"[기재JSON/이름] 이름 getter trampoline 검증 실패.");
      state.nameHookFailureLogged = true;
    }
    return false;
  }

  state.originalNameGetter = reinterpret_cast<uintptr_t>(original);

  const MH_STATUS enableStatus =
      MH_EnableHook(reinterpret_cast<LPVOID>(target));
  if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    state.originalNameGetter = 0;
    if (!state.nameHookFailureLogged) {
      AddLog(u8"[기재JSON/이름] 이름 getter 직접 훅 활성화 실패: %s",
             MH_StatusToString(enableStatus));
      state.nameHookFailureLogged = true;
    }
    return false;
  }

  state.nameHookInstalled = true;
  state.nameHookFailureLogged = false;
  AddLog(u8"[기재JSON/이름] 원본 게임 이름 getter 직접 훅 설치 완료: target=%p trampoline=%p",
         reinterpret_cast<void *>(target),
         reinterpret_cast<void *>(state.originalNameGetter));
  return true;
}


uintptr_t ResolveDescTextPointerAddress() {
  RuntimeState &state = State();
  if (state.descTextPointerAddress)
    return state.descTextPointerAddress;

  const uintptr_t gameBase =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase)
    return 0;

  MODULEINFO mi{};
  if (GetModuleInformation(GetCurrentProcess(),
                           reinterpret_cast<HMODULE>(gameBase),
                           &mi, sizeof(mi))) {
    const uintptr_t found =
        FindPattern(gameBase, gameBase + mi.SizeOfImage,
                    "B8 04 30 00 00");
    if (found) {
      bool has5823 = false;
      for (int off = 5; off < 15; ++off) {
        uint8_t op = 0;
        uint32_t imm = 0;
        if (ReadMemorySafe(found + off, &op, sizeof(op)) &&
            op == 0xB8 &&
            ReadMemorySafe(found + off + 1, &imm, sizeof(imm)) &&
            imm == 0x5823) {
          has5823 = true;
          break;
        }
      }

      if (has5823) {
        for (int off = 5; off < 40; ++off) {
          uint8_t op[3] = {};
          if (!ReadMemorySafe(found + off, op, sizeof(op)))
            continue;
          if (op[0] != 0x4C || op[1] != 0x8B || op[2] != 0x0D)
            continue;

          int32_t disp = 0;
          if (!ReadMemorySafe(found + off + 3, &disp, sizeof(disp)))
            continue;

          state.descTextPointerAddress =
              found + off + 7 + static_cast<int64_t>(disp);
          AddLog(u8"[기재JSON/설명TS] textPtrAddr AOB: +0x%llX",
                 static_cast<unsigned long long>(
                     state.descTextPointerAddress - gameBase));
          return state.descTextPointerAddress;
        }
      }
    }
  }

  state.descTextPointerAddress =
      gameBase + kDescTextPointerFallbackOffset;
  AddLog(u8"[기재JSON/설명TS] textPtrAddr fallback: +0x%llX",
         static_cast<unsigned long long>(
             kDescTextPointerFallbackOffset));
  return state.descTextPointerAddress;
}

bool SyncOneDescTable(uintptr_t tableBase,
                      uintptr_t bufferBase,
                      uintptr_t bufferLimit,
                      uint32_t indexBase,
                      bool formatted,
                      int &synced) {
  RuntimeState &state = State();
  uintptr_t cursor = 0;

  for (int i = kDescTableCustomIndexMin;
       i <= kDescTableCustomIndexMax; ++i) {
    const wchar_t *text =
        formatted ? state.nativeDescFormatPtrs[i]
                  : state.nativeDescPtrs[i];
    if (!text || text[0] == L'\0')
      continue;

    const std::size_t chars = std::wcslen(text);
    const std::size_t bytes =
        (chars + 1) * sizeof(wchar_t);

    const uintptr_t stringAddress = bufferBase + cursor;
    if (stringAddress < bufferBase ||
        stringAddress + bytes > bufferLimit)
      return false;

    if (!WriteMemoryProtected(
            stringAddress, text, bytes))
      return false;

    if (stringAddress < tableBase ||
        stringAddress - tableBase > 0xFFFFFFFFull)
      return false;

    const uint32_t relative =
        static_cast<uint32_t>(stringAddress - tableBase);
    const uintptr_t entryAddress =
        tableBase +
        static_cast<uintptr_t>(indexBase + i) *
            sizeof(uint32_t);

    if (!WriteMemoryProtected(
            entryAddress, &relative, sizeof(relative)))
      return false;

    cursor += bytes;
    cursor = (cursor + 3) & ~static_cast<uintptr_t>(3);
    ++synced;
  }

  return true;
}

bool SyncCustomDescTextTables() {
  RuntimeState &state = State();
  if (state.descTablesSynced)
    return true;

  const uintptr_t pointerAddress =
      ResolveDescTextPointerAddress();
  if (!pointerAddress)
    return false;

  uintptr_t textBase = 0;
  if (!ReadMemorySafe(
          pointerAddress, &textBase, sizeof(textBase)) ||
      textBase < 0x10000) {
    return false;
  }

  uint16_t headerType = 0;
  uint32_t tableOffset = 0;
  if (!ReadMemorySafe(
          textBase + 0x0A, &headerType, sizeof(headerType)) ||
      !ReadMemorySafe(
          textBase + 0x10, &tableOffset, sizeof(tableOffset)) ||
      headerType != 4 || tableOffset == 0) {
    return false;
  }

  const uintptr_t tableBase = textBase + tableOffset;

  MEMORY_BASIC_INFORMATION mbi{};
  if (VirtualQuery(reinterpret_cast<const void *>(textBase),
                   &mbi, sizeof(mbi)) != sizeof(mbi) ||
      mbi.State != MEM_COMMIT ||
      mbi.RegionSize < 0x28000) {
    return false;
  }

  const uintptr_t regionBegin =
      reinterpret_cast<uintptr_t>(mbi.BaseAddress);
  const uintptr_t regionEnd =
      regionBegin + mbi.RegionSize;

  const uintptr_t formatTableEnd =
      tableBase +
      static_cast<uintptr_t>(
          kDescFormatIndexBase +
          kDescTableCustomIndexMax + 1) *
          sizeof(uint32_t);
  uintptr_t formatBuffer =
      (regionEnd - 0x24000) &
      ~static_cast<uintptr_t>(0xF);
  if (formatBuffer < formatTableEnd)
    formatBuffer =
        (formatTableEnd + 0xF) &
        ~static_cast<uintptr_t>(0xF);
  if (formatBuffer + 0x1000 > regionEnd)
    return false;

  const uintptr_t normalTableEnd =
      tableBase +
      static_cast<uintptr_t>(
          kDescNormalIndexBase +
          kDescTableCustomIndexMax + 1) *
          sizeof(uint32_t);
  uintptr_t normalBuffer =
      (regionEnd - 0x28000) &
      ~static_cast<uintptr_t>(0xF);
  if (normalBuffer < normalTableEnd)
    normalBuffer =
        (normalTableEnd + 0xF) &
        ~static_cast<uintptr_t>(0xF);

  // version.dll은 normal buffer를 format 영역보다 앞쪽에 둡니다.
  if (normalBuffer + 0x400 > formatBuffer)
    return false;

  int normalSynced = 0;
  int formatSynced = 0;
  if (!SyncOneDescTable(
          tableBase, normalBuffer, formatBuffer,
          kDescNormalIndexBase, false, normalSynced) ||
      !SyncOneDescTable(
          tableBase, formatBuffer, regionEnd,
          kDescFormatIndexBase, true, formatSynced)) {
    if (!state.descTableFailureLogged) {
      AddLog(u8"[기재JSON/설명TS] 설명 텍스트 테이블 기록 실패.");
      state.descTableFailureLogged = true;
    }
    return false;
  }

  state.descTablesSynced = true;
  state.descTableFailureLogged = false;
  AddLog(u8"[기재JSON/설명TS] 동기화 완료: 일반 %d개 / 포맷 %d개 / table=%p",
         normalSynced, formatSynced,
         reinterpret_cast<void *>(tableBase));
  return true;
}

bool EnsureCustomDescHook() {
  RuntimeState &state = State();
  if (state.descHookInstalled)
    return true;

  bool hasDescriptions = false;
  for (const wchar_t *desc : state.nativeDescPtrs) {
    if (desc && desc[0] != L'\0') {
      hasDescriptions = true;
      break;
    }
  }
  if (!hasDescriptions)
    return true;

  const uintptr_t gameBase =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase)
    return false;

  const uintptr_t target = gameBase + kDescMapGetterOffset;

  uint8_t prologue[sizeof(kDescMapGetterPrologue)] = {};
  if (!ReadMemorySafe(target, prologue, sizeof(prologue)) ||
      std::memcmp(prologue, kDescMapGetterPrologue,
                  sizeof(kDescMapGetterPrologue)) != 0) {
    if (!state.descHookFailureLogged) {
      AddLog(u8"[기재JSON/설명] descmap 원본 바이트 불일치: +171E070");
      state.descHookFailureLogged = true;
    }
    return false;
  }

  const MH_STATUS initStatus = MH_Initialize();
  if (initStatus != MH_OK &&
      initStatus != MH_ERROR_ALREADY_INITIALIZED) {
    if (!state.descHookFailureLogged) {
      AddLog(u8"[기재JSON/설명] MinHook 초기화 실패: %s",
             MH_StatusToString(initStatus));
      state.descHookFailureLogged = true;
    }
    return false;
  }

  LPVOID original = nullptr;
  const MH_STATUS createStatus =
      MH_CreateHook(reinterpret_cast<LPVOID>(target),
                    reinterpret_cast<LPVOID>(&CustomTraitDescMapGetter),
                    &original);
  if (createStatus != MH_OK) {
    if (!state.descHookFailureLogged) {
      AddLog(u8"[기재JSON/설명] descmap 훅 생성 실패: %s / target=%p",
             MH_StatusToString(createStatus),
             reinterpret_cast<void *>(target));
      state.descHookFailureLogged = true;
    }
    return false;
  }

  if (!original ||
      !IsExecutableAddress(reinterpret_cast<uintptr_t>(original))) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    if (!state.descHookFailureLogged) {
      AddLog(u8"[기재JSON/설명] descmap trampoline 검증 실패.");
      state.descHookFailureLogged = true;
    }
    return false;
  }

  state.originalDescMapGetter =
      reinterpret_cast<uintptr_t>(original);

  const MH_STATUS enableStatus =
      MH_EnableHook(reinterpret_cast<LPVOID>(target));
  if (enableStatus != MH_OK &&
      enableStatus != MH_ERROR_ENABLED) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    state.originalDescMapGetter = 0;
    if (!state.descHookFailureLogged) {
      AddLog(u8"[기재JSON/설명] descmap 훅 활성화 실패: %s",
             MH_StatusToString(enableStatus));
      state.descHookFailureLogged = true;
    }
    return false;
  }

  state.descHookInstalled = true;
  state.descHookFailureLogged = false;
  AddLog(u8"[기재JSON/설명] 원본 게임 descmap 직접 훅 설치 완료: target=%p trampoline=%p",
         reinterpret_cast<void *>(target),
         reinterpret_cast<void *>(state.originalDescMapGetter));
  return true;
}



bool ReadTraitRecordId(uintptr_t record, uint16_t &id) {
  id = 0;
  if (record < 0x10000)
    return false;
  return ReadMemorySafe(record + kTraitIdOffset, &id, sizeof(id));
}

bool ReadTraitEffectType(uintptr_t record, std::size_t slot, uint16_t &type) {
  type = 0;
  if (record < 0x10000 || slot >= kEffectCount)
    return false;
  return ReadMemorySafe(
      record + kEffectOffset + slot * kEffectSize,
      &type, sizeof(type));
}

bool HasMatchingEffectType(uintptr_t customRecord, uintptr_t requestedRecord) {
  if (customRecord < 0x10000 || requestedRecord < 0x10000)
    return false;

  std::array<TraitEffect, kEffectCount> customEffects{};
  std::array<TraitEffect, kEffectCount> requestedEffects{};
  if (!ReadMemorySafe(customRecord + kEffectOffset,
                      customEffects.data(), sizeof(customEffects)) ||
      !ReadMemorySafe(requestedRecord + kEffectOffset,
                      requestedEffects.data(), sizeof(requestedEffects))) {
    return false;
  }

  return std::memcmp(customEffects.data(), requestedEffects.data(),
                     sizeof(customEffects)) == 0;
}

bool FindMatchingCustomTrait(
    void *officer, uint16_t requestedTraitId, uint16_t &matchedId) {
  matchedId = 0;
  RuntimeState &state = State();
  if (!officer ||
      !state.configLoaded ||
      requestedTraitId < 1 ||
      requestedTraitId > kTraitCount ||
      state.lastTableBase < 0x10000) {
    return false;
  }

  const uintptr_t requestedRecord =
      state.lastTableBase +
      static_cast<uintptr_t>(requestedTraitId) * kTraitStride;

  uint16_t requestedRecordId = 0;
  if (!ReadTraitRecordId(requestedRecord, requestedRecordId) ||
      requestedRecordId != requestedTraitId) {
    return false;
  }

  const uintptr_t officerBase =
      reinterpret_cast<uintptr_t>(officer);

  for (int slot = 0; slot < 3; ++slot) {
    uintptr_t heldRecord = 0;
    if (!ReadMemorySafe(
            officerBase + 0x88 +
                static_cast<uintptr_t>(slot) * sizeof(uintptr_t),
            &heldRecord, sizeof(heldRecord)) ||
        heldRecord < 0x10000) {
      continue;
    }

    uint16_t heldId = 0;
    if (!ReadTraitRecordId(heldRecord, heldId))
      continue;

    // version.dll은 71~300을 대상으로 하지만 현재 JSON/런타임 테이블은 254까지 사용합니다.
    if (heldId < 71 || heldId > kTraitCount)
      continue;

    if (!HasMatchingEffectType(heldRecord, requestedRecord))
      continue;

    matchedId = heldId;
    return true;
  }

  return false;
}

bool __fastcall CustomTraitEffectQuery(
    void *officer, uint16_t requestedTraitId) {
  RuntimeState &state = State();
  g_traitEffectMatchCache = {};

  const uintptr_t original = state.originalTraitEffectQuery;
  if (original &&
      reinterpret_cast<TraitEffectQuery>(original)(officer, requestedTraitId)) {
    return true;
  }

  const uintptr_t gameBase =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  const uintptr_t returnAddress =
      reinterpret_cast<uintptr_t>(_ReturnAddress());
  if (gameBase &&
      returnAddress == gameBase + kTraitEffectQuerySkipCallerOffset) {
    return false;
  }

  uint16_t matchedId = 0;
  if (!FindMatchingCustomTrait(officer, requestedTraitId, matchedId))
    return false;

  g_traitEffectMatchCache.valid = true;
  g_traitEffectMatchCache.requestedTraitId = requestedTraitId;
  g_traitEffectMatchCache.matchedCustomTraitId = matchedId;
  g_traitEffectMatchCache.queryReturnRva =
      gameBase && returnAddress >= gameBase ? returnAddress - gameBase : 0;
  return true;
}

bool EnsureTraitEffectHook() {
  RuntimeState &state = State();
  if (state.traitEffectHookInstalled)
    return true;

  const uintptr_t gameBase =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase)
    return false;

  const uintptr_t target =
      gameBase + kTraitEffectQueryOffset;

  uint8_t prologue[sizeof(kTraitEffectQueryPrologue)] = {};
  if (!ReadMemorySafe(target, prologue, sizeof(prologue)) ||
      std::memcmp(prologue, kTraitEffectQueryPrologue,
                  sizeof(kTraitEffectQueryPrologue)) != 0) {
    if (!state.traitEffectHookFailureLogged) {
      AddLog(u8"[기재JSON/효과] +17AAD60 원본 바이트 불일치. 메인 효과 훅 설치 보류.");
      state.traitEffectHookFailureLogged = true;
    }
    return false;
  }

  const MH_STATUS initStatus = MH_Initialize();
  if (initStatus != MH_OK &&
      initStatus != MH_ERROR_ALREADY_INITIALIZED) {
    if (!state.traitEffectHookFailureLogged) {
      AddLog(u8"[기재JSON/효과] MinHook 초기화 실패: %s",
             MH_StatusToString(initStatus));
      state.traitEffectHookFailureLogged = true;
    }
    return false;
  }

  LPVOID original = nullptr;
  const MH_STATUS createStatus =
      MH_CreateHook(
          reinterpret_cast<LPVOID>(target),
          reinterpret_cast<LPVOID>(&CustomTraitEffectQuery),
          &original);
  if (createStatus != MH_OK) {
    if (!state.traitEffectHookFailureLogged) {
      AddLog(u8"[기재JSON/효과] +17AAD60 훅 생성 실패: %s",
             MH_StatusToString(createStatus));
      state.traitEffectHookFailureLogged = true;
    }
    return false;
  }

  if (!original ||
      !IsExecutableAddress(
          reinterpret_cast<uintptr_t>(original))) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    if (!state.traitEffectHookFailureLogged) {
      AddLog(u8"[기재JSON/효과] +17AAD60 trampoline 검증 실패.");
      state.traitEffectHookFailureLogged = true;
    }
    return false;
  }

  state.originalTraitEffectQuery =
      reinterpret_cast<uintptr_t>(original);

  const MH_STATUS enableStatus =
      MH_EnableHook(reinterpret_cast<LPVOID>(target));
  if (enableStatus != MH_OK &&
      enableStatus != MH_ERROR_ENABLED) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    state.originalTraitEffectQuery = 0;
    if (!state.traitEffectHookFailureLogged) {
      AddLog(u8"[기재JSON/효과] +17AAD60 훅 활성화 실패: %s",
             MH_StatusToString(enableStatus));
      state.traitEffectHookFailureLogged = true;
    }
    return false;
  }

  state.traitEffectHookInstalled = true;
  state.traitEffectHookFailureLogged = false;

  AddLog(u8"[기재JSON/효과] TraitEffect 메인 훅 설치 완료: +17AAD60 target=%p trampoline=%p",
         reinterpret_cast<void *>(target),
         reinterpret_cast<void *>(
             state.originalTraitEffectQuery));
  return true;
}

struct TraitBridgeSite {
  uintptr_t queryReturnRva;
  uintptr_t consumerReturnRva;
  uint16_t requestedId;
};

// These pairs are direct call/return sites in the same SAN8RPK.exe build.
// The first call checks the base trait; the second consumes that trait ID.
constexpr TraitBridgeSite kMessageBridgeSites[] = {
    {0x1854D85, 0x1854E73, 28}, // conversation
    {0x1859DE5, 0x1859ECF, 1},  // conversation
    {0x1902E70, 0x1902EF5, 10}, // repeated action
    {0x1BCEF8C, 0x1BCF20F, 11}, // reinforcement report
    {0x1BCFF6E, 0x1BD0702, 202}, // strategy phase
    {0x1BD0954, 0x1BD0A16, 30}, // strategy phase
    {0x1E4C093, 0x1E4C208, 19}, // battle conclusion
    {kTransferTraitEventOffset, 0x18A5F81, 26} // transfer event
};
constexpr TraitBridgeSite kDataBridgeSites[] = {
    {0x19253E8, 0x192542F, 55} // conference result
};

bool TakeMatchedBridgeId(uint32_t requestedId, uintptr_t consumerReturnAddress,
                         const TraitBridgeSite *sites, std::size_t siteCount,
                         uint16_t &matchedId) {
  matchedId = 0;
  const TraitEffectMatchCache &cache = g_traitEffectMatchCache;
  RuntimeState &state = State();
  if (!cache.valid || !state.configLoaded ||
      requestedId != cache.requestedTraitId ||
      cache.matchedCustomTraitId < 71 ||
      cache.matchedCustomTraitId > kTraitCount) {
    return false;
  }

  const uintptr_t gameBase =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase || consumerReturnAddress < gameBase)
    return false;

  const uintptr_t consumerRva = consumerReturnAddress - gameBase;
  bool knownPair = false;
  for (std::size_t i = 0; i < siteCount; ++i) {
    if (sites[i].queryReturnRva == cache.queryReturnRva &&
        sites[i].consumerReturnRva == consumerRva &&
        sites[i].requestedId == requestedId) {
      knownPair = true;
      break;
    }
  }
  if (!knownPair) {
    thread_local unsigned diagnosticCount = 0;
    if (diagnosticCount < 12) {
      AddLog(u8"[기재JSON/호환] 미확인 ID 전달: query=+%llX consumer=+%llX id=%u",
             static_cast<unsigned long long>(cache.queryReturnRva),
             static_cast<unsigned long long>(consumerRva), requestedId);
      ++diagnosticCount;
    }
    return false;
  }

  const uintptr_t customRecord =
      state.lastTableBase +
      static_cast<uintptr_t>(cache.matchedCustomTraitId) * kTraitStride;
  uint16_t recordId = 0;
  if (!ReadTraitRecordId(customRecord, recordId) ||
      recordId != cache.matchedCustomTraitId) {
    return false;
  }

  matchedId = cache.matchedCustomTraitId;
  g_traitEffectMatchCache = {};
  return true;
}

int __fastcall CustomOfficerTraitQuery(void *officer, uint32_t requestedId) {
  RuntimeState &state = State();
  const uintptr_t original = state.originalOfficerTraitQuery;
  if (!original)
    return 0;

  const int originalResult =
      reinterpret_cast<OfficerTraitQuery>(original)(officer, requestedId);
  if (originalResult)
    return originalResult;

  // This direct officer query is only extended for the ID 11 path verified
  // in the original compatibility hook and the game's callers.
  if (requestedId != 11)
    return 0;

  g_traitEffectMatchCache = {};
  uint16_t matchedId = 0;
  if (!FindMatchingCustomTrait(officer, 11, matchedId))
    return 0;

  const uintptr_t gameBase =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  const uintptr_t returnAddress =
      reinterpret_cast<uintptr_t>(_ReturnAddress());
  g_traitEffectMatchCache.valid = true;
  g_traitEffectMatchCache.requestedTraitId = 11;
  g_traitEffectMatchCache.matchedCustomTraitId = matchedId;
  g_traitEffectMatchCache.queryReturnRva =
      gameBase && returnAddress >= gameBase ? returnAddress - gameBase : 0;
  return 1;
}

void __fastcall CustomTraitMessageSetter(void *message, uint32_t requestedId) {
  RuntimeState &state = State();
  const uintptr_t original = state.originalTraitMessageSetter;
  if (!original)
    return;

  uint16_t matchedId = 0;
  const bool bridge = TakeMatchedBridgeId(
      requestedId, reinterpret_cast<uintptr_t>(_ReturnAddress()),
      kMessageBridgeSites, std::size(kMessageBridgeSites), matchedId);

  // The original setter still updates the message object; only its input ID
  // changes after a confirmed custom-only match at a known call site.
  reinterpret_cast<TraitMessageSetter>(original)(
      message, bridge ? matchedId : requestedId);
  if (bridge) {
    AddLog(u8"[기재JSON/호환] 대화 효과 ID 연결: %u -> %u",
           requestedId, matchedId);
  }
}

void *__fastcall CustomTraitDataGetter(void *dataCenter, uint32_t requestedId) {
  RuntimeState &state = State();
  const uintptr_t original = state.originalTraitDataGetter;
  if (!original)
    return nullptr;

  void *result =
      reinterpret_cast<TraitDataGetter>(original)(dataCenter, requestedId);
  uint16_t matchedId = 0;
  if (!TakeMatchedBridgeId(
          requestedId, reinterpret_cast<uintptr_t>(_ReturnAddress()),
          kDataBridgeSites, std::size(kDataBridgeSites), matchedId)) {
    return result;
  }

  void *custom =
      reinterpret_cast<TraitDataGetter>(original)(dataCenter, matchedId);
  uint16_t actualId = 0;
  if (!ReadTraitRecordId(reinterpret_cast<uintptr_t>(custom), actualId) ||
      actualId != matchedId) {
    return result;
  }

  AddLog(u8"[기재JSON/호환] 효과 데이터 ID 연결: %u -> %u",
         requestedId, matchedId);
  return custom;
}

bool IsValidGameObject(void *object) {
  if (!object)
    return false;
  uintptr_t vtable = 0;
  uintptr_t validate = 0;
  if (!ReadMemorySafe(reinterpret_cast<uintptr_t>(object),
                      &vtable, sizeof(vtable)) ||
      !ReadMemorySafe(vtable + 0x48, &validate, sizeof(validate)) ||
      !IsExecutableAddress(validate)) {
    return false;
  }
  __try {
    return reinterpret_cast<bool(__fastcall *)(void *)>(validate)(object);
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

int __fastcall CustomMonthlyTraitUpdate(void *turn) {
  RuntimeState &state = State();
  const uintptr_t original = state.originalMonthlyUpdate;
  if (!original)
    return 0;

  const int result = reinterpret_cast<MonthlyTraitUpdate>(original)(turn);
  if (!state.configLoaded || state.lastTableBase < 0x10000)
    return result;

  const uintptr_t gameDataRoot = GetGameBaseFast();
  if (gameDataRoot < 0x10000)
    return result;

  // The game's monthly update walks these 1801 person pointers. The old
  // compatibility DLL repeats the loop after the original and adds signed
  // values for custom effect type 166 to person+0x300, capped at 99999.
  constexpr uintptr_t kFirstPersonOffset = 0x576C88;
  constexpr uintptr_t kEndPersonOffset = 0x57A4D0;
  constexpr uint16_t kMonthlyEffectType = 166;
  constexpr uint64_t kMaximumAmount = 99999;
  int updated = 0;

  for (uintptr_t slot = gameDataRoot + kFirstPersonOffset;
       slot < gameDataRoot + kEndPersonOffset;
       slot += sizeof(uintptr_t)) {
    uintptr_t officer = 0;
    if (!ReadMemorySafe(slot, &officer, sizeof(officer)) ||
        !IsValidGameObject(reinterpret_cast<void *>(officer))) {
      continue;
    }

    int amount = 0;
    for (int heldSlot = 0; heldSlot < 3; ++heldSlot) {
      uintptr_t record = 0;
      if (!ReadMemorySafe(
              officer + 0x88 +
                  static_cast<uintptr_t>(heldSlot) * sizeof(uintptr_t),
              &record, sizeof(record))) {
        continue;
      }

      uint16_t id = 0;
      if (!ReadTraitRecordId(record, id) || id < 71 || id > kTraitCount)
        continue;

      for (std::size_t effectSlot = 0; effectSlot < kEffectCount; ++effectSlot) {
        TraitEffect effect{};
        if (!ReadMemorySafe(record + kEffectOffset + effectSlot * kEffectSize,
                            &effect, sizeof(effect))) {
          continue;
        }
        if (effect.type != kMonthlyEffectType)
          continue;
        int16_t signedValue = 0;
        std::memcpy(&signedValue, &effect.value, sizeof(signedValue));
        amount += signedValue;
      }
    }

    if (amount <= 0)
      continue;

    uint32_t current = 0;
    if (!ReadMemorySafe(officer + 0x300, &current, sizeof(current)))
      continue;
    const uint32_t next = static_cast<uint32_t>(std::min<uint64_t>(
        static_cast<uint64_t>(current) + static_cast<uint32_t>(amount),
        kMaximumAmount));
    if (next != current &&
        WriteMemoryProtected(officer + 0x300, &next, sizeof(next))) {
      ++updated;
    }
  }

  if (updated)
    AddLog(u8"[기재JSON/호환] 월별 효과 보완: 장수 %d명", updated);
  return result;
}

int __fastcall CustomTransferTraitEvent(void *firstPerson, void *secondPerson,
                                         void *personList, void *firstCity,
                                         void *secondCity) {
  RuntimeState &state = State();
  const uintptr_t original = state.originalTransferEvent;
  if (!original)
    return 0;

  // The original DLL checks the second participant for base trait 26 before
  // this event runs. Its message setter later consumes the matched custom ID.
  const TraitEffectMatchCache previous = g_traitEffectMatchCache;
  g_traitEffectMatchCache = {};
  uint16_t matchedId = 0;
  if (state.configLoaded &&
      FindMatchingCustomTrait(secondPerson, 26, matchedId)) {
    g_traitEffectMatchCache.valid = true;
    g_traitEffectMatchCache.requestedTraitId = 26;
    g_traitEffectMatchCache.matchedCustomTraitId = matchedId;
    g_traitEffectMatchCache.queryReturnRva = kTransferTraitEventOffset;
  }

  const int result = reinterpret_cast<TransferTraitEvent>(original)(
      firstPerson, secondPerson, personList, firstCity, secondCity);
  g_traitEffectMatchCache = previous;
  return result;
}

bool InstallCompatibilityHook(uintptr_t offset, const uint8_t *prologue,
                              std::size_t prologueSize, void *detour,
                              uintptr_t &originalAddress, bool &installed,
                              bool &failureLogged, const char *role) {
  if (installed)
    return true;

  const uintptr_t gameBase =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase)
    return false;
  const uintptr_t target = gameBase + offset;

  std::array<uint8_t, 8> bytes{};
  if (prologueSize > bytes.size() ||
      !IsExecutableAddress(target) ||
      !ReadMemorySafe(target, bytes.data(), prologueSize) ||
      std::memcmp(bytes.data(), prologue, prologueSize) != 0) {
    if (!failureLogged) {
      AddLog(u8"[기재JSON/호환] %s +%llX 원본 바이트 불일치. 설치 보류.",
             role, static_cast<unsigned long long>(offset));
      failureLogged = true;
    }
    return false;
  }

  const MH_STATUS initStatus = MH_Initialize();
  if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
    if (!failureLogged) {
      AddLog(u8"[기재JSON/호환] %s MinHook 초기화 실패: %s",
             role, MH_StatusToString(initStatus));
      failureLogged = true;
    }
    return false;
  }

  LPVOID trampoline = nullptr;
  const MH_STATUS createStatus = MH_CreateHook(
      reinterpret_cast<LPVOID>(target), detour, &trampoline);
  if (createStatus != MH_OK) {
    if (!failureLogged) {
      AddLog(u8"[기재JSON/호환] %s 훅 생성 실패: %s",
             role, MH_StatusToString(createStatus));
      failureLogged = true;
    }
    return false;
  }

  if (!trampoline ||
      !IsExecutableAddress(reinterpret_cast<uintptr_t>(trampoline))) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    if (!failureLogged) {
      AddLog(u8"[기재JSON/호환] %s trampoline 검증 실패.", role);
      failureLogged = true;
    }
    return false;
  }

  originalAddress = reinterpret_cast<uintptr_t>(trampoline);
  const MH_STATUS enableStatus =
      MH_EnableHook(reinterpret_cast<LPVOID>(target));
  if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    originalAddress = 0;
    if (!failureLogged) {
      AddLog(u8"[기재JSON/호환] %s 훅 활성화 실패: %s",
             role, MH_StatusToString(enableStatus));
      failureLogged = true;
    }
    return false;
  }

  installed = true;
  failureLogged = false;
  AddLog(u8"[기재JSON/호환] %s 패치 완료: +%llX",
         role, static_cast<unsigned long long>(offset));
  return true;
}


bool MaintainTraitEligibilityPatch(
    uintptr_t offset, bool &active, bool &failureLogged, const char *role) {
  const uintptr_t gameBase =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase)
    return false;

  const uintptr_t address = gameBase + offset;
  std::array<uint8_t, sizeof(kTraitEligibilityExpected)> bytes{};
  if (!ReadMemorySafe(address, bytes.data(), bytes.size())) {
    if (!failureLogged) {
      AddLog(u8"[기재JSON/호환] %s 메모리 읽기 실패: +%llX",
             role, static_cast<unsigned long long>(offset));
      failureLogged = true;
    }
    active = false;
    return false;
  }

  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i == 2)
      continue;
    if (bytes[i] != kTraitEligibilityExpected[i]) {
      if (!failureLogged) {
        AddLog(u8"[기재JSON/호환] %s 원본 바이트 불일치: +%llX (패치 보류)",
               role, static_cast<unsigned long long>(offset));
        failureLogged = true;
      }
      active = false;
      return false;
    }
  }

  if (bytes[2] == 0x01) {
    active = true;
    failureLogged = false;
    return true;
  }

  if (bytes[2] != 0x95) {
    if (!failureLogged) {
      AddLog(u8"[기재JSON/호환] %s 임계값 상태 미확인: +%llX value=0x%02X",
             role, static_cast<unsigned long long>(offset),
             static_cast<unsigned>(bytes[2]));
      failureLogged = true;
    }
    active = false;
    return false;
  }

  const bool wasActive = active;
  if (!WriteExecutableByteProtected(address + 2, 0x01)) {
    if (!failureLogged) {
      AddLog(u8"[기재JSON/호환] %s 적용 실패: +%llX",
             role, static_cast<unsigned long long>(offset));
      failureLogged = true;
    }
    active = false;
    return false;
  }

  active = true;
  failureLogged = false;
  AddLog(wasActive
             ? u8"[기재JSON/호환] %s 복원 감지 -> 재적용: +%llX"
             : u8"[기재JSON/호환] %s 적용 완료: +%llX",
         role, static_cast<unsigned long long>(offset));
  return true;
}

void EnsureExtendedTraitCompatibility() {
  RuntimeState &state = State();
  InstallCompatibilityHook(
      kOfficerTraitQueryOffset, kOfficerTraitQueryPrologue,
      sizeof(kOfficerTraitQueryPrologue),
      reinterpret_cast<void *>(&CustomOfficerTraitQuery),
      state.originalOfficerTraitQuery, state.officerTraitHookInstalled,
      state.officerTraitHookFailureLogged, u8"장수 보유 판정");

  InstallCompatibilityHook(
      kTraitMessageSetterOffset, kTraitMessageSetterPrologue,
      sizeof(kTraitMessageSetterPrologue),
      reinterpret_cast<void *>(&CustomTraitMessageSetter),
      state.originalTraitMessageSetter, state.traitMessageHookInstalled,
      state.traitMessageHookFailureLogged, u8"대화 효과 연결");

  InstallCompatibilityHook(
      kTraitDataGetterOffset, kTraitDataGetterPrologue,
      sizeof(kTraitDataGetterPrologue),
      reinterpret_cast<void *>(&CustomTraitDataGetter),
      state.originalTraitDataGetter, state.traitDataHookInstalled,
      state.traitDataHookFailureLogged, u8"효과 데이터 연결");

  InstallCompatibilityHook(
      kMonthlyTraitUpdateOffset, kMonthlyTraitUpdatePrologue,
      sizeof(kMonthlyTraitUpdatePrologue),
      reinterpret_cast<void *>(&CustomMonthlyTraitUpdate),
      state.originalMonthlyUpdate, state.monthlyHookInstalled,
      state.monthlyHookFailureLogged, u8"월별 효과 보완");

  InstallCompatibilityHook(
      kTransferTraitEventOffset, kTransferTraitEventPrologue,
      sizeof(kTransferTraitEventPrologue),
      reinterpret_cast<void *>(&CustomTransferTraitEvent),
      state.originalTransferEvent, state.transferHookInstalled,
      state.transferHookFailureLogged, u8"특수 연출 연결");

  MaintainTraitEligibilityPatch(
      kTraitEligibilityPatchAOffset,
      state.eligibilityPatchAActive,
      state.eligibilityPatchAFailureLogged,
      u8"기재 사용 조건 A");
  MaintainTraitEligibilityPatch(
      kTraitEligibilityPatchBOffset,
      state.eligibilityPatchBActive,
      state.eligibilityPatchBFailureLogged,
      u8"기재 사용 조건 B");
}

bool ReadJsonIntValue(const std::string &text, const char *field, int &out) {
  const std::string key = std::string("\"") + field + "\"";
  std::size_t p = text.find(key);
  if (p == std::string::npos)
    return false;

  p = text.find(':', p + key.size());
  if (p == std::string::npos)
    return false;

  ++p;
  while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p])))
    ++p;

  std::size_t end = p;
  if (end < text.size() && (text[end] == '-' || text[end] == '+'))
    ++end;
  while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])))
    ++end;

  if (end == p || (end == p + 1 && (text[p] == '-' || text[p] == '+')))
    return false;

  try {
    out = std::stoi(text.substr(p, end - p));
    return true;
  } catch (...) {
    return false;
  }
}

bool ReadJsonStringValue(const std::string &text, const char *field, std::string &out) {
  const std::string key = std::string("\"") + field + "\"";
  std::size_t p = text.find(key);
  if (p == std::string::npos)
    return false;

  p = text.find(':', p + key.size());
  if (p == std::string::npos)
    return false;

  p = text.find('"', p + 1);
  if (p == std::string::npos)
    return false;

  ++p;
  out.clear();
  bool escaped = false;
  for (; p < text.size(); ++p) {
    const char c = text[p];
    if (escaped) {
      switch (c) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      default:
        out.push_back('\\');
        out.push_back(c);
        break;
      }
      escaped = false;
      continue;
    }

    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"')
      return true;
    out.push_back(c);
  }

  return false;
}

bool ParseTraitLine(const std::string &line, int &index, TraitConfigEntry &entry) {
  if (line.find("\"effects\"") == std::string::npos)
    return false;

  int id = 0;
  if (!ReadJsonIntValue(line, "index", index) ||
      !ReadJsonIntValue(line, "id", id) ||
      index < 0 || index >= kTraitCount ||
      id < 1 || id > kTraitCount) {
    return false;
  }

  const std::size_t effectsKey = line.find("\"effects\"");
  const std::size_t arrayStart = line.find('[', effectsKey);
  const std::size_t arrayEnd = line.find(']', arrayStart);
  if (arrayStart == std::string::npos || arrayEnd == std::string::npos)
    return false;

  TraitConfigEntry parsed;
  parsed.present = true;
  parsed.id = id;

  std::size_t cursor = arrayStart + 1;
  for (std::size_t slot = 0; slot < kEffectCount; ++slot) {
    const std::size_t open = line.find('{', cursor);
    const std::size_t close = line.find('}', open);
    if (open == std::string::npos || close == std::string::npos || close > arrayEnd)
      return false;

    const std::string obj = line.substr(open, close - open + 1);
    int type = 0;
    int value = 0;
    int param = 0;
    if (!ReadJsonIntValue(obj, "type", type) ||
        !ReadJsonIntValue(obj, "value", value) ||
        !ReadJsonIntValue(obj, "param", param) ||
        type < 0 || type > 0xFFFF ||
        value < 0 || value > 0xFFFF ||
        param < 0 || param > 0xFFFF) {
      return false;
    }

    parsed.effects[slot].type = static_cast<uint16_t>(type);
    parsed.effects[slot].value = static_cast<uint16_t>(value);
    parsed.effects[slot].param = static_cast<uint16_t>(param);
    cursor = close + 1;
  }

  entry = parsed;
  return true;
}

bool ParseCustomMetaLine(const std::string &line, int &index, TraitMetaEntry &entry) {
  const std::size_t q1 = line.find('"');
  if (q1 == std::string::npos)
    return false;
  const std::size_t q2 = line.find('"', q1 + 1);
  if (q2 == std::string::npos)
    return false;

  const std::string key = line.substr(q1 + 1, q2 - q1 - 1);
  if (key.empty())
    return false;
  for (unsigned char c : key) {
    if (!std::isdigit(c))
      return false;
  }

  try {
    index = std::stoi(key);
  } catch (...) {
    return false;
  }
  if (index < 0 || index >= kTraitCount)
    return false;

  TraitMetaEntry parsed;
  parsed.present = true;

  std::string name;
  std::string desc;
  ReadJsonStringValue(line, "name", name);
  ReadJsonStringValue(line, "desc", desc);
  parsed.name = name;
  parsed.desc = desc;
  parsed.hasCustomText = !name.empty() || !desc.empty();

  ReadJsonIntValue(line, "cloneSrc", parsed.cloneSrc);
  ReadJsonIntValue(line, "bgTrait", parsed.bgTrait);
  ReadJsonIntValue(line, "grade", parsed.grade);

  entry = parsed;
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

std::filesystem::path ResolveConfigPath() {
  wchar_t modulePath[MAX_PATH] = {};
  const DWORD len = GetModuleFileNameW(g_hModule, modulePath, MAX_PATH);
  if (len > 0 && len < MAX_PATH) {
    std::filesystem::path p =
        std::filesystem::path(modulePath).parent_path() /
        L"san8r_traits_config.json";
    std::error_code ec;
    if (std::filesystem::exists(p, ec) && !ec)
      return p;
  }

  std::error_code ec;
  const std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (!ec) {
    std::filesystem::path p = cwd / L"san8r_traits_config.json";
    if (std::filesystem::exists(p, ec) && !ec)
      return p;
  }

  return {};
}

bool LoadConfig(bool &changed) {
  changed = false;
  RuntimeState &state = State();

  const std::filesystem::path path = ResolveConfigPath();
  std::error_code ec;
  std::filesystem::file_time_type writeTime{};
  bool haveWriteTime = false;

  if (!path.empty()) {
    writeTime = std::filesystem::last_write_time(path, ec);
    haveWriteTime = !ec;
    if (state.configLoaded &&
        path == state.configPath &&
        haveWriteTime && state.hasWriteTime &&
        writeTime == state.configWriteTime) {
      return true;
    }
  } else if (state.configLoaded && state.configFromEmbedded) {
    return true;
  }

  auto parseText = [&](const std::string &jsonText,
                       std::array<TraitConfigEntry, kTraitCount> &traits,
                       std::array<TraitMetaEntry, kTraitCount> &meta,
                       int &traitCount,
                       int &customCount,
                       int &sentinel) -> bool {
    traits = {};
    meta = {};
    traitCount = 0;
    customCount = 0;
    sentinel = -1;

    std::istringstream file(jsonText);
    bool inCustomNames = false;
    std::string line;

    while (std::getline(file, line)) {
      if (!inCustomNames) {
        if (line.find("\"customNames\"") != std::string::npos) {
          inCustomNames = true;
          continue;
        }

        int index = -1;
        TraitConfigEntry entry;
        if (ParseTraitLine(line, index, entry)) {
          traits[index] = entry;
          ++traitCount;
        }
        continue;
      }

      int index = -1;
      TraitMetaEntry entry;
      if (ParseCustomMetaLine(line, index, entry)) {
        meta[index] = entry;
        ++customCount;
      }
    }

    if (traitCount == 0)
      return false;

    for (int i = 0; i < kTraitCount; ++i) {
      if (!traits[i].present)
        continue;
      if (traits[i].id != i + 1)
        return false;
    }

    for (int i = 0; i < kTraitCount; ++i) {
      if (traits[i].present && meta[i].present && meta[i].hasCustomText) {
        sentinel = i;
        break;
      }
    }
    if (sentinel < 0) {
      for (int i = 0; i < kTraitCount; ++i) {
        if (traits[i].present) {
          sentinel = i;
          break;
        }
      }
    }
    return true;
  };

  std::string jsonText;
  bool usingEmbedded = false;
  bool loaded = false;

  if (!path.empty()) {
    loaded = ReadUtf8TextFile(path, jsonText);
    if (!loaded) {
      AddLog(u8"[기재JSON] 외부 설정 읽기 실패. 내장 기본값으로 전환합니다.");
    }
  }

  if (!loaded) {
    usingEmbedded = true;
    loaded = LoadEmbeddedJsonResource(IDR_JSON_TRAITS_DEFAULT, jsonText);
    if (!loaded) {
      AddLog(u8"[기재JSON] 내장 기본 설정을 읽지 못했습니다.");
      return false;
    }
  }

  std::array<TraitConfigEntry, kTraitCount> traits{};
  std::array<TraitMetaEntry, kTraitCount> meta{};
  int traitCount = 0;
  int customCount = 0;
  int sentinel = -1;

  if (!parseText(jsonText, traits, meta, traitCount, customCount, sentinel)) {
    if (!usingEmbedded) {
      AddLog(u8"[기재JSON] 외부 설정 형식 오류. 내장 기본값으로 전환합니다.");
      usingEmbedded = true;
      if (!LoadEmbeddedJsonResource(IDR_JSON_TRAITS_DEFAULT, jsonText) ||
          !parseText(jsonText, traits, meta, traitCount, customCount, sentinel)) {
        AddLog(u8"[기재JSON] 내장 기본 설정 형식을 읽지 못했습니다.");
        return false;
      }
    } else {
      AddLog(u8"[기재JSON] 내장 기본 설정 형식을 읽지 못했습니다.");
      return false;
    }
  }

  if (!PublishNativeTexts(meta)) {
    AddLog(u8"[기재JSON/문구] 이름/설명 준비 실패. 기재 테이블 적용을 중지합니다.");
    return false;
  }

  state.traits = std::move(traits);
  state.meta = std::move(meta);
  state.configPath = path;
  state.configLoaded = true;
  state.configFromEmbedded = usingEmbedded;
  state.configWriteTime = writeTime;
  state.hasWriteTime = !path.empty() && haveWriteTime;
  state.sentinelIndex = sentinel;
  state.applyPending = true;
  state.descTablesSynced = false;
  state.descTableFailureLogged = false;
  changed = true;

  AddLog(u8"[기재JSON] 설정 로드: %s / traits %d개 / customNames %d개 / 감시 index %d",
         usingEmbedded ? u8"내장 기본값" : u8"외부 san8r_traits_config.json",
         traitCount, customCount, sentinel);
  return true;
}

uint32_t ResolveTraitsPointerOffset() {
  RuntimeState &state = State();
  if (state.offsetResolved)
    return state.traitsPointerOffset;

  state.offsetResolved = true;
  state.traitsPointerOffset = kTraitsPointerOffsetFallback;

  const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!exeBase)
    return state.traitsPointerOffset;

  MODULEINFO mi{};
  if (!GetModuleInformation(GetCurrentProcess(),
                            reinterpret_cast<HMODULE>(exeBase),
                            &mi, sizeof(mi))) {
    return state.traitsPointerOffset;
  }

  // version.dll이 사용하는 FindOffset AOB와 동일합니다.
  const std::string pattern =
      "48 8B ?? ?? ?? ?? ?? ?? 48 85 ?? 74 ?? 48 8B ?? 48 8B ?? "
      "FF ?? ?? 84 ?? 74 ?? 0F B7 ?? ?? 66 ?? ?? ?? ?? ?? ?? 48 8B";

  const uintptr_t found =
      FindPattern(exeBase, exeBase + mi.SizeOfImage, pattern);
  if (found && IsValidPtr(found + 4, sizeof(uint32_t))) {
    const uint32_t candidate =
        *reinterpret_cast<const uint32_t *>(found + 4);
    if (candidate > 0x1000 && candidate < 0x10000000)
      state.traitsPointerOffset = candidate;
  }

  AddLog(u8"[기재JSON] 기재 테이블 포인터 오프셋: 0x%X%s",
         state.traitsPointerOffset,
         state.traitsPointerOffset == kTraitsPointerOffsetFallback
             ? u8" (fallback)"
             : u8" (AOB)");
  return state.traitsPointerOffset;
}

bool IsEffectTableReady(uintptr_t tableBase) {
  if (!tableBase ||
      !IsValidPtr(tableBase + kTraitStride,
                  kTraitCount * kTraitStride)) {
    return false;
  }

  __try {
    const uint16_t firstId =
        *reinterpret_cast<const uint16_t *>(
            tableBase + kTraitStride + kTraitIdOffset);
    if (firstId != 1)
      return false;

    int nonZeroTypes = 0;
    const int sampleCount = 30;
    for (int i = 0; i < sampleCount; ++i) {
      const uintptr_t record =
          tableBase + static_cast<uintptr_t>(i + 1) * kTraitStride;
      for (std::size_t e = 0; e < kEffectCount; ++e) {
        const uint16_t type =
            *reinterpret_cast<const uint16_t *>(
                record + kEffectOffset + e * kEffectSize);
        if (type == 0)
          continue;
        if (type < 102 || type > 194)
          return false;
        ++nonZeroTypes;
      }
    }

    return nonZeroTypes >= 10;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

uintptr_t ResolveTraitTable(uintptr_t gameDataRoot) {
  if (gameDataRoot <= 0x10000)
    return 0;

  const uint32_t offset = ResolveTraitsPointerOffset();
  const uintptr_t ptrAddr = gameDataRoot + offset;
  if (!IsValidPtr(ptrAddr, sizeof(uintptr_t)))
    return 0;

  uintptr_t tableBase = 0;
  __try {
    tableBase = *reinterpret_cast<const uintptr_t *>(ptrAddr);
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return 0;
  }

  if (tableBase <= 0x10000 || !IsEffectTableReady(tableBase))
    return 0;
  return tableBase;
}

bool ConfigEntryMatches(uintptr_t tableBase, int index) {
  RuntimeState &state = State();
  if (index < 0 || index >= kTraitCount || !state.traits[index].present)
    return true;

  const uintptr_t record =
      tableBase + static_cast<uintptr_t>(index + 1) * kTraitStride;

  __try {
    const uint16_t id =
        *reinterpret_cast<const uint16_t *>(record + kTraitIdOffset);
    if (id != static_cast<uint16_t>(state.traits[index].id))
      return false;

    return std::memcmp(
               reinterpret_cast<const void *>(record + kEffectOffset),
               state.traits[index].effects.data(),
               kEffectCount * sizeof(TraitEffect)) == 0;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool ApplyConfig(uintptr_t tableBase) {
  RuntimeState &state = State();
  if (!state.configLoaded || !IsEffectTableReady(tableBase))
    return false;

  constexpr std::size_t kRegionSize = kTraitCount * kTraitStride;
  const uintptr_t firstRecord = tableBase + kTraitStride;

  if (!IsValidPtr(firstRecord, kRegionSize))
    return false;

  std::array<std::array<uint8_t, kTraitStride>, kTraitCount> records{};

  __try {
    std::memcpy(records.data(),
                reinterpret_cast<const void *>(firstRecord),
                kRegionSize);
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }

  // version.dll Normalize()의 핵심 동작:
  // custom text가 있는 슬롯은 cloneSrc의 0x40 레코드를 복제한 뒤 ID와 효과를 복구합니다.
  // cloneSrc가 자기 자신/범위 밖이면 donor index 30을 사용합니다.
  for (int i = 0; i < kTraitCount; ++i) {
    TraitMetaEntry &meta = state.meta[i];

    if (meta.present && meta.hasCustomText) {
      int source = meta.cloneSrc;
      if (source < 0 || source >= kTraitCount || source == i)
        source = kDefaultCloneSource;

      if (source >= 0 && source < kTraitCount && source != i)
        records[i] = records[source];
    }

    if (meta.present &&
        meta.bgTrait >= 0 && meta.bgTrait < kTraitCount &&
        meta.bgTrait != i) {
      std::memcpy(records[i].data() + kMetadataOffset,
                  records[meta.bgTrait].data() + kMetadataOffset,
                  kMetadataSize);
    }

    if (state.traits[i].present) {
      const uint16_t id = static_cast<uint16_t>(state.traits[i].id);
      std::memcpy(records[i].data() + kTraitIdOffset, &id, sizeof(id));
      std::memcpy(records[i].data() + kEffectOffset,
                  state.traits[i].effects.data(),
                  kEffectCount * sizeof(TraitEffect));
    } else {
      // JSON에 효과가 없더라도 테이블 슬롯의 ID만 정상화합니다.
      const uint16_t id = static_cast<uint16_t>(i + 1);
      std::memcpy(records[i].data() + kTraitIdOffset, &id, sizeof(id));
    }
  }

  DWORD oldProtect = 0;
  if (!VirtualProtect(reinterpret_cast<void *>(firstRecord),
                      kRegionSize, PAGE_READWRITE, &oldProtect)) {
    return false;
  }

  bool ok = true;
  __try {
    std::memcpy(reinterpret_cast<void *>(firstRecord),
                records.data(),
                kRegionSize);
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    ok = false;
  }

  DWORD unused = 0;
  VirtualProtect(reinterpret_cast<void *>(firstRecord),
                 kRegionSize, oldProtect, &unused);

  if (!ok)
    return false;

  if (state.sentinelIndex >= 0 &&
      !ConfigEntryMatches(tableBase, state.sentinelIndex)) {
    return false;
  }

  return true;
}

} // namespace

void TickTraitConfigRuntime() {
  RuntimeState &state = State();
  const ULONGLONG now = GetTickCount64();

  // Menu::Loops()는 약 30ms 주기이므로 이 기능은 500ms마다만 확인합니다.
  if (now - state.lastTickMs < 500ull)
    return;
  state.lastTickMs = now;

  static bool externalVersionDllChecked = false;
  static bool externalVersionDllDetected = false;
  if (!externalVersionDllChecked) {
    externalVersionDllChecked = true;
    externalVersionDllDetected = HasExternalGameVersionDll();
    if (externalVersionDllDetected) {
      AddLog(u8"[기재JSON] 게임 폴더의 외부 version.dll 감지. S8RCheats 내장 기재 런타임을 사용하지 않습니다.");
    }
  }
  if (externalVersionDllDetected)
    return;

  if (!state.configLoaded || now - state.lastConfigProbeMs >= 2000ull) {
    state.lastConfigProbeMs = now;
    bool changed = false;
    if (!LoadConfig(changed))
      return;
    if (changed)
      state.applyPending = true;
  }

  // 커스텀 ID가 원본 UI에서 조회되기 전에 이름 getter를 먼저 가로챕니다.
  // 이 훅이 실패하면 71~254 슬롯을 활성화하지 않아 원본 사실무장 편집 UI 프리징을 방지합니다.
  if (!EnsureCustomNameHook())
    return;

  // descmap 훅은 version.dll과 동일하게 "원본 먼저 호출 -> null일 때만 201+ fallback"입니다.
  if (!EnsureCustomDescHook())
    return;

  // ID 71~200은 version.dll DescTS처럼 게임의 일반/포맷 설명 테이블에 직접 동기화합니다.
  // 리소스가 아직 준비되지 않았으면 다음 tick에서 다시 시도하며 효과 테이블 적용은 막지 않습니다.
  SyncCustomDescTextTables();

  const uintptr_t gameDataRoot = GetGameBaseFast();
  const uintptr_t tableBase = ResolveTraitTable(gameDataRoot);
  if (!tableBase)
    return;

  if (state.lastTableBase != tableBase) {
    if (state.lastTableBase != 0) {
      AddLog(u8"[기재JSON] 기재 테이블 재생성 감지: %p -> %p",
             reinterpret_cast<void *>(state.lastTableBase),
             reinterpret_cast<void *>(tableBase));
    }
    state.lastTableBase = tableBase;
    state.applyPending = true;
  }

  if (!state.applyPending &&
      state.sentinelIndex >= 0 &&
      !ConfigEntryMatches(tableBase, state.sentinelIndex)) {
    AddLog(u8"[기재JSON] 기재 효과 초기화 감지(index %d). 재적용합니다.",
           state.sentinelIndex);
    state.applyPending = true;
  }

  if (state.applyPending) {
    if (ApplyConfig(tableBase)) {
      state.applyPending = false;
      AddLog(u8"[기재JSON] 기재 설정 적용 완료: table=%p",
             reinterpret_cast<void *>(tableBase));
    } else {
      return;
    }
  }

  // 설치 실패 시에도 다음 tick에서 다시 시도합니다.
  if (EnsureTraitEffectHook())
    EnsureExtendedTraitCompatibility();
}

} // namespace DX11Base