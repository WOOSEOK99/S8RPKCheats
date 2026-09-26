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
constexpr int kDefaultCloneSource = 30;
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
  return protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ ||
         protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
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
  __try { std::memcpy(dst, &value, sizeof(value)); }
  __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
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
  __try { std::memcpy(dst, data, size); }
  __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
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
  __try { *reinterpret_cast<volatile uint8_t *>(address) = value; }
  __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
  DWORD ignored = 0;
  VirtualProtect(dst, sizeof(value), oldProtect, &ignored);
  if (ok)
    FlushInstructionCache(GetCurrentProcess(), dst, sizeof(value));
  return ok;
}

const wchar_t *__fastcall CustomTraitNameGetter(void *traitObject) {
  uint16_t id = 0;
  if (traitObject) {
    __try { id = *reinterpret_cast<const uint16_t *>(reinterpret_cast<uintptr_t>(traitObject) + kTraitIdOffset); }
    __except (EXCEPTION_EXECUTE_HANDLER) { id = 0; }
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

const wchar_t *__fastcall CustomTraitDescMapGetter(void *context, int traitId, uint8_t kind) {
  RuntimeState &state = State();
  const uintptr_t original = state.originalDescMapGetter;
  if (original) {
    const wchar_t *originalResult = reinterpret_cast<TraitDescMapGetter>(original)(context, traitId, kind);
    if (originalResult)
      return originalResult;
  }
  if (traitId < 201 || traitId > kTraitCount)
    return L"";
  const std::size_t index = static_cast<std::size_t>(traitId - 1);
  const wchar_t *custom = (kind == 2) ? state.nativeDescFormatPtrs[index] : state.nativeDescPtrs[index];
  if (!custom || custom[0] == L'\0')
    return L"";
  thread_local std::array<wchar_t, 0x800> formatBuffer{};
  thread_local std::array<wchar_t, 0x100> normalBuffer{};
  wchar_t *buffer = kind == 2 ? formatBuffer.data() : normalBuffer.data();
  const std::size_t maxChars = kind == 2 ? 0x7FE : 0xFE;
  const std::size_t length = std::min<std::size_t>(std::wcslen(custom), maxChars);
  std::memcpy(buffer, custom, length * sizeof(wchar_t));
  buffer[length] = L'\0';
  return buffer;
}

bool Utf8ToWide(const std::string &text, std::wstring &out) {
  out.clear();
  if (text.empty()) return true;
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0) return false;
  out.resize(static_cast<std::size_t>(count));
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), count) == count;
}

const wchar_t *AllocateStableWideText(const std::wstring &wide) {
  if (wide.empty()) return nullptr;
  const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
  void *memory = VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!memory) return nullptr;
  std::memcpy(memory, wide.c_str(), bytes);
  return reinterpret_cast<const wchar_t *>(memory);
}

const wchar_t *AllocateStableWideString(const std::string &utf8) {
  std::wstring wide;
  if (!Utf8ToWide(utf8, wide) || wide.empty()) return nullptr;
  return AllocateStableWideText(wide);
}

const wchar_t *AllocateStableFormatWideString(const std::string &utf8) {
  std::wstring wide;
  if (!Utf8ToWide(utf8, wide) || wide.empty()) return nullptr;
  std::wstring escaped;
  escaped.reserve(wide.size() + 8);
  for (wchar_t ch : wide) {
    escaped.push_back(ch);
    if (ch == L'%') escaped.push_back(L'%');
  }
  return AllocateStableWideText(escaped);
}

bool PublishNativeTexts(const std::array<TraitMetaEntry, kTraitCount> &meta) {
  RuntimeState &state = State();
  std::array<const wchar_t *, kTraitCount> nextNames{};
  std::array<const wchar_t *, kTraitCount> nextDescs{};
  std::array<const wchar_t *, kTraitCount> nextFormatDescs{};
  int nameCount = 0;
  int descCount = 0;
  for (int i = 0; i < kTraitCount; ++i) {
    if (!meta[i].present) continue;
    if (!meta[i].name.empty()) {
      const wchar_t *wide = AllocateStableWideString(meta[i].name);
      if (!wide) return false;
      nextNames[i] = wide;
      ++nameCount;
    }
    if (!meta[i].desc.empty()) {
      const wchar_t *wide = AllocateStableWideString(meta[i].desc);
      const wchar_t *formatWide = AllocateStableFormatWideString(meta[i].desc);
      if (!wide || !formatWide) return false;
      nextDescs[i] = wide;
      nextFormatDescs[i] = formatWide;
      ++descCount;
    }
  }
  state.nativeNamePtrs = nextNames;
  state.nativeDescPtrs = nextDescs;
  state.nativeDescFormatPtrs = nextFormatDescs;
  AddLog(u8"[기재JSON/문구] JSON 문구 준비 완료: 이름 %d개 / 설명 %d개", nameCount, descCount);
  return true;
}

bool HasPublishedCustomNames() {
  RuntimeState &state = State();
  for (const wchar_t *name : state.nativeNamePtrs)
    if (name && name[0] != L'\0') return true;
  return false;
}

bool EnsureCustomNameHook() {
  RuntimeState &state = State();
  if (!HasPublishedCustomNames()) return true;
  if (state.nameHookInstalled) return true;
  const uintptr_t gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase) return false;
  const uintptr_t target = gameBase + kNameGetterOffset;
  if (!IsExecutableAddress(target)) return false;
  const MH_STATUS initStatus = MH_Initialize();
  if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) return false;
  LPVOID original = nullptr;
  const MH_STATUS createStatus = MH_CreateHook(reinterpret_cast<LPVOID>(target), reinterpret_cast<LPVOID>(&CustomTraitNameGetter), &original);
  if (createStatus != MH_OK) return false;
  if (!original || !IsExecutableAddress(reinterpret_cast<uintptr_t>(original))) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    return false;
  }
  state.originalNameGetter = reinterpret_cast<uintptr_t>(original);
  const MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(target));
  if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    state.originalNameGetter = 0;
    return false;
  }
  state.nameHookInstalled = true;
  AddLog(u8"[기재JSON/이름] 원본 게임 이름 getter 직접 훅 설치 완료: target=%p trampoline=%p", reinterpret_cast<void *>(target), reinterpret_cast<void *>(state.originalNameGetter));
  return true;
}

uintptr_t ResolveDescTextPointerAddress() {
  RuntimeState &state = State();
  if (state.descTextPointerAddress) return state.descTextPointerAddress;
  const uintptr_t gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase) return 0;
  state.descTextPointerAddress = gameBase + kDescTextPointerFallbackOffset;
  return state.descTextPointerAddress;
}

bool SyncCustomDescTextTables() { return true; }

bool EnsureCustomDescHook() {
  RuntimeState &state = State();
  if (state.descHookInstalled) return true;
  bool hasDescriptions = false;
  for (const wchar_t *desc : state.nativeDescPtrs) if (desc && desc[0] != L'\0') { hasDescriptions = true; break; }
  if (!hasDescriptions) return true;
  const uintptr_t gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase) return false;
  const uintptr_t target = gameBase + kDescMapGetterOffset;
  uint8_t prologue[sizeof(kDescMapGetterPrologue)] = {};
  if (!ReadMemorySafe(target, prologue, sizeof(prologue)) || std::memcmp(prologue, kDescMapGetterPrologue, sizeof(kDescMapGetterPrologue)) != 0) return false;
  const MH_STATUS initStatus = MH_Initialize();
  if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) return false;
  LPVOID original = nullptr;
  const MH_STATUS createStatus = MH_CreateHook(reinterpret_cast<LPVOID>(target), reinterpret_cast<LPVOID>(&CustomTraitDescMapGetter), &original);
  if (createStatus != MH_OK) return false;
  if (!original || !IsExecutableAddress(reinterpret_cast<uintptr_t>(original))) { MH_RemoveHook(reinterpret_cast<LPVOID>(target)); return false; }
  state.originalDescMapGetter = reinterpret_cast<uintptr_t>(original);
  const MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(target));
  if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) { MH_RemoveHook(reinterpret_cast<LPVOID>(target)); state.originalDescMapGetter = 0; return false; }
  state.descHookInstalled = true;
  AddLog(u8"[기재JSON/설명] 원본 게임 descmap 직접 훅 설치 완료: target=%p trampoline=%p", reinterpret_cast<void *>(target), reinterpret_cast<void *>(state.originalDescMapGetter));
  return true;
}

bool ReadTraitRecordId(uintptr_t record, uint16_t &id) {
  id = 0;
  if (record < 0x10000) return false;
  return ReadMemorySafe(record + kTraitIdOffset, &id, sizeof(id));
}

bool ReadTraitEffectType(uintptr_t record, std::size_t slot, uint16_t &type) {
  type = 0;
  if (record < 0x10000 || slot >= kEffectCount) return false;
  return ReadMemorySafe(record + kEffectOffset + slot * kEffectSize, &type, sizeof(type));
}

bool HasMatchingEffectType(uintptr_t customRecord, uintptr_t requestedRecord) {
  std::array<uint16_t, kEffectCount> requestedTypes{};
  std::size_t requestedCount = 0;
  for (std::size_t i = 0; i < kEffectCount; ++i) {
    uint16_t type = 0;
    if (!ReadTraitEffectType(requestedRecord, i, type)) return false;
    if (type != 0) requestedTypes[requestedCount++] = type;
  }
  if (requestedCount == 0) return false;
  for (std::size_t i = 0; i < kEffectCount; ++i) {
    uint16_t customType = 0;
    if (!ReadTraitEffectType(customRecord, i, customType)) return false;
    if (customType == 0) continue;
    for (std::size_t j = 0; j < requestedCount; ++j)
      if (customType == requestedTypes[j]) return true;
  }
  return false;
}

bool FindMatchingCustomTrait(void *officer, uint16_t requestedTraitId, uint16_t &matchedId) {
  matchedId = 0;
  RuntimeState &state = State();
  if (!officer || !state.configLoaded || requestedTraitId < 1 || requestedTraitId > kTraitCount || state.lastTableBase < 0x10000) return false;
  const uintptr_t requestedRecord = state.lastTableBase + static_cast<uintptr_t>(requestedTraitId) * kTraitStride;
  uint16_t requestedRecordId = 0;
  if (!ReadTraitRecordId(requestedRecord, requestedRecordId) || requestedRecordId != requestedTraitId) return false;
  const uintptr_t officerBase = reinterpret_cast<uintptr_t>(officer);
  for (int slot = 0; slot < 3; ++slot) {
    uintptr_t heldRecord = 0;
    if (!ReadMemorySafe(officerBase + 0x88 + static_cast<uintptr_t>(slot) * sizeof(uintptr_t), &heldRecord, sizeof(heldRecord)) || heldRecord < 0x10000) continue;
    uint16_t heldId = 0;
    if (!ReadTraitRecordId(heldRecord, heldId)) continue;
    if (heldId < 71 || heldId > kTraitCount) continue;
    if (!HasMatchingEffectType(heldRecord, requestedRecord)) continue;
    matchedId = heldId;
    return true;
  }
  return false;
}

bool __fastcall CustomTraitEffectQuery(void *officer, uint16_t requestedTraitId) {
  RuntimeState &state = State();
  g_traitEffectMatchCache = {};
  const uintptr_t gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
  const uintptr_t callerRva = gameBase && returnAddress >= gameBase ? returnAddress - gameBase : 0;

  const uintptr_t original = state.originalTraitEffectQuery;
  const bool originalResult = original && reinterpret_cast<TraitEffectQuery>(original)(officer, requestedTraitId);

  if (requestedTraitId == 11 || requestedTraitId == 26) {
    thread_local unsigned diagCount = 0;
    if (diagCount < 160) {
      std::array<uint16_t, 3> held{};
      const uintptr_t officerBase = reinterpret_cast<uintptr_t>(officer);
      for (std::size_t slot = 0; slot < held.size(); ++slot) {
        uintptr_t record = 0;
        if (officerBase >= 0x10000 && ReadMemorySafe(officerBase + 0x88 + slot * sizeof(uintptr_t), &record, sizeof(record)) && record >= 0x10000)
          ReadTraitRecordId(record, held[slot]);
      }
      AddLog(u8"[기재직접추적] req=%u original=%d held=%u,%u,%u officer=%p caller=+%llX",
             static_cast<unsigned>(requestedTraitId), originalResult ? 1 : 0,
             static_cast<unsigned>(held[0]), static_cast<unsigned>(held[1]), static_cast<unsigned>(held[2]),
             officer, static_cast<unsigned long long>(callerRva));
      ++diagCount;
    }
  }

  if (originalResult)
    return true;
  if (gameBase && returnAddress == gameBase + kTraitEffectQuerySkipCallerOffset)
    return false;

  uint16_t matchedId = 0;
  if (!FindMatchingCustomTrait(officer, requestedTraitId, matchedId))
    return false;

  if (requestedTraitId == 11 || requestedTraitId == 26) {
    AddLog(u8"[기재직접추적] FALLBACK TRUE req=%u matched=%u caller=+%llX",
           static_cast<unsigned>(requestedTraitId), static_cast<unsigned>(matchedId),
           static_cast<unsigned long long>(callerRva));
  }

  g_traitEffectMatchCache.valid = true;
  g_traitEffectMatchCache.requestedTraitId = requestedTraitId;
  g_traitEffectMatchCache.matchedCustomTraitId = matchedId;
  g_traitEffectMatchCache.queryReturnRva = callerRva;
  return true;
}

bool EnsureTraitEffectHook() {
  RuntimeState &state = State();
  if (state.traitEffectHookInstalled) return true;
  const uintptr_t gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase) return false;
  const uintptr_t target = gameBase + kTraitEffectQueryOffset;
  uint8_t prologue[sizeof(kTraitEffectQueryPrologue)] = {};
  if (!ReadMemorySafe(target, prologue, sizeof(prologue)) || std::memcmp(prologue, kTraitEffectQueryPrologue, sizeof(kTraitEffectQueryPrologue)) != 0) return false;
  const MH_STATUS initStatus = MH_Initialize();
  if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) return false;
  LPVOID original = nullptr;
  const MH_STATUS createStatus = MH_CreateHook(reinterpret_cast<LPVOID>(target), reinterpret_cast<LPVOID>(&CustomTraitEffectQuery), &original);
  if (createStatus != MH_OK) return false;
  if (!original || !IsExecutableAddress(reinterpret_cast<uintptr_t>(original))) { MH_RemoveHook(reinterpret_cast<LPVOID>(target)); return false; }
  state.originalTraitEffectQuery = reinterpret_cast<uintptr_t>(original);
  const MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(target));
  if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) { MH_RemoveHook(reinterpret_cast<LPVOID>(target)); state.originalTraitEffectQuery = 0; return false; }
  state.traitEffectHookInstalled = true;
  AddLog(u8"[기재JSON/효과] TraitEffect 메인 훅 설치 완료: +17AAD60 target=%p trampoline=%p", reinterpret_cast<void *>(target), reinterpret_cast<void *>(state.originalTraitEffectQuery));
  return true;
}

int __fastcall CustomOfficerTraitQuery(void *officer, uint32_t requestedId) {
  RuntimeState &state = State();
  const uintptr_t original = state.originalOfficerTraitQuery;
  if (!original) return 0;
  const int originalResult = reinterpret_cast<OfficerTraitQuery>(original)(officer, requestedId);
  if (originalResult) return originalResult;
  if (requestedId != 11) return 0;
  uint16_t matchedId = 0;
  if (!FindMatchingCustomTrait(officer, 11, matchedId)) return 0;
  return 1;
}

void __fastcall CustomTraitMessageSetter(void *message, uint32_t requestedId) {
  RuntimeState &state = State();
  if (state.originalTraitMessageSetter) reinterpret_cast<TraitMessageSetter>(state.originalTraitMessageSetter)(message, requestedId);
}

void *__fastcall CustomTraitDataGetter(void *dataCenter, uint32_t requestedId) {
  RuntimeState &state = State();
  if (!state.originalTraitDataGetter) return nullptr;
  return reinterpret_cast<TraitDataGetter>(state.originalTraitDataGetter)(dataCenter, requestedId);
}

int __fastcall CustomMonthlyTraitUpdate(void *turn) {
  RuntimeState &state = State();
  return state.originalMonthlyUpdate ? reinterpret_cast<MonthlyTraitUpdate>(state.originalMonthlyUpdate)(turn) : 0;
}

int __fastcall CustomTransferTraitEvent(void *firstPerson, void *secondPerson, void *personList, void *firstCity, void *secondCity) {
  RuntimeState &state = State();
  return state.originalTransferEvent ? reinterpret_cast<TransferTraitEvent>(state.originalTransferEvent)(firstPerson, secondPerson, personList, firstCity, secondCity) : 0;
}

bool InstallCompatibilityHook(uintptr_t offset, const uint8_t *prologue, std::size_t prologueSize, void *detour, uintptr_t &originalAddress, bool &installed, bool &failureLogged, const char *role) {
  if (installed) return true;
  const uintptr_t gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase) return false;
  const uintptr_t target = gameBase + offset;
  std::array<uint8_t, 8> bytes{};
  if (prologueSize > bytes.size() || !IsExecutableAddress(target) || !ReadMemorySafe(target, bytes.data(), prologueSize) || std::memcmp(bytes.data(), prologue, prologueSize) != 0) return false;
  const MH_STATUS initStatus = MH_Initialize();
  if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) return false;
  LPVOID trampoline = nullptr;
  const MH_STATUS createStatus = MH_CreateHook(reinterpret_cast<LPVOID>(target), detour, &trampoline);
  if (createStatus != MH_OK) return false;
  if (!trampoline || !IsExecutableAddress(reinterpret_cast<uintptr_t>(trampoline))) { MH_RemoveHook(reinterpret_cast<LPVOID>(target)); return false; }
  originalAddress = reinterpret_cast<uintptr_t>(trampoline);
  const MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(target));
  if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) { MH_RemoveHook(reinterpret_cast<LPVOID>(target)); originalAddress = 0; return false; }
  installed = true;
  failureLogged = false;
  AddLog(u8"[기재JSON/호환] %s 패치 완료: +%llX", role, static_cast<unsigned long long>(offset));
  return true;
}

bool MaintainTraitEligibilityPatch(uintptr_t, bool &, bool &, const char *) { return true; }

void EnsureExtendedTraitCompatibility() {
  RuntimeState &state = State();
  InstallCompatibilityHook(kOfficerTraitQueryOffset, kOfficerTraitQueryPrologue, sizeof(kOfficerTraitQueryPrologue), reinterpret_cast<void *>(&CustomOfficerTraitQuery), state.originalOfficerTraitQuery, state.officerTraitHookInstalled, state.officerTraitHookFailureLogged, u8"장수 보유 판정");
  InstallCompatibilityHook(kTraitMessageSetterOffset, kTraitMessageSetterPrologue, sizeof(kTraitMessageSetterPrologue), reinterpret_cast<void *>(&CustomTraitMessageSetter), state.originalTraitMessageSetter, state.traitMessageHookInstalled, state.traitMessageHookFailureLogged, u8"대화 효과 연결");
  InstallCompatibilityHook(kTraitDataGetterOffset, kTraitDataGetterPrologue, sizeof(kTraitDataGetterPrologue), reinterpret_cast<void *>(&CustomTraitDataGetter), state.originalTraitDataGetter, state.traitDataHookInstalled, state.traitDataHookFailureLogged, u8"효과 데이터 연결");
  InstallCompatibilityHook(kMonthlyTraitUpdateOffset, kMonthlyTraitUpdatePrologue, sizeof(kMonthlyTraitUpdatePrologue), reinterpret_cast<void *>(&CustomMonthlyTraitUpdate), state.originalMonthlyUpdate, state.monthlyHookInstalled, state.monthlyHookFailureLogged, u8"월별 효과 보완");
  InstallCompatibilityHook(kTransferTraitEventOffset, kTransferTraitEventPrologue, sizeof(kTransferTraitEventPrologue), reinterpret_cast<void *>(&CustomTransferTraitEvent), state.originalTransferEvent, state.transferHookInstalled, state.transferHookFailureLogged, u8"특수 연출 연결");
}

bool ReadJsonIntValue(const std::string &, const char *, int &) { return false; }
bool ReadJsonStringValue(const std::string &, const char *, std::string &) { return false; }
bool ParseTraitLine(const std::string &, int &, TraitConfigEntry &) { return false; }
bool ParseCustomMetaLine(const std::string &, int &, TraitMetaEntry &) { return false; }
bool HasExternalGameVersionDll() { return false; }
std::filesystem::path ResolveConfigPath() { return {}; }
bool LoadConfig(bool &) { return false; }
uint32_t ResolveTraitsPointerOffset() { return kTraitsPointerOffsetFallback; }
bool IsEffectTableReady(uintptr_t) { return true; }
uintptr_t ResolveTraitTable(uintptr_t) { return State().lastTableBase; }
bool ConfigEntryMatches(uintptr_t, int) { return true; }
bool ApplyConfig(uintptr_t) { return true; }

} // namespace

void TickTraitConfigRuntime() {
  RuntimeState &state = State();
  const ULONGLONG now = GetTickCount64();
  if (now - state.lastTickMs < 500ull) return;
  state.lastTickMs = now;
  if (EnsureTraitEffectHook()) EnsureExtendedTraitCompatibility();
}

} // namespace DX11Base
