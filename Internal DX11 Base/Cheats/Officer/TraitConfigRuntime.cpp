#include "../../pch.h"

#include "../../Cheats.h"
#include "../../showlog.h"
#include "TraitConfigRuntime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
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
constexpr uint32_t kTraitsPointerOffsetFallback = 0x57A4B8;
constexpr int kDefaultCloneSource = 30; // version.dll Normalize()와 동일한 기본 donor index
constexpr uintptr_t kNameGetterOffset = 0x170E2D0;
constexpr uintptr_t kNameGetterPointerOffset = 0x170E2D6;
constexpr uint8_t kNameGetterStub[] = {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};

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
  bool applyPending = false;

  uint32_t traitsPointerOffset = 0;
  bool offsetResolved = false;

  uintptr_t lastTableBase = 0;
  int sentinelIndex = -1;

  ULONGLONG lastTickMs = 0;
  ULONGLONG lastConfigProbeMs = 0;

  std::array<const wchar_t *, kTraitCount> nativeNamePtrs{};
  bool nameHookInstalled = false;
  bool nameHookFailureLogged = false;
  uintptr_t originalNameGetter = 0;
};

RuntimeState &State() {
  static RuntimeState state;
  return state;
}


using TraitNameGetter = const wchar_t *(__fastcall *)(void *);

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

const wchar_t *AllocateStableWideString(const std::string &utf8) {
  std::wstring wide;
  if (!Utf8ToWide(utf8, wide) || wide.empty())
    return nullptr;

  const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
  void *memory = VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!memory)
    return nullptr;

  std::memcpy(memory, wide.c_str(), bytes);
  return reinterpret_cast<const wchar_t *>(memory);
}

bool PublishNativeNames(const std::array<TraitMetaEntry, kTraitCount> &meta) {
  RuntimeState &state = State();
  std::array<const wchar_t *, kTraitCount> next{};

  int published = 0;
  for (int i = 0; i < kTraitCount; ++i) {
    if (!meta[i].present || meta[i].name.empty())
      continue;

    const wchar_t *wide = AllocateStableWideString(meta[i].name);
    if (!wide) {
      AddLog(u8"[기재JSON/이름] UTF-8 이름 변환/할당 실패: index=%d", i);
      return false;
    }

    next[i] = wide;
    ++published;
  }

  // 이전 문자열은 게임 UI가 포인터를 보유할 수 있으므로 해제하지 않습니다.
  // 새 포인터 배열만 원자 크기 포인터 쓰기로 교체합니다.
  state.nativeNamePtrs = next;
  AddLog(u8"[기재JSON/이름] JSON 이름 준비 완료: %d개", published);
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

  const uintptr_t gameBase =
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
  if (!gameBase)
    return false;

  const uintptr_t stubAddress = gameBase + kNameGetterOffset;
  uint8_t stub[sizeof(kNameGetterStub)] = {};
  if (!ReadMemorySafe(stubAddress, stub, sizeof(stub)) ||
      std::memcmp(stub, kNameGetterStub, sizeof(stub)) != 0) {
    if (!state.nameHookFailureLogged) {
      AddLog(u8"[기재JSON/이름] 이름 getter 스텁 검증 실패 (+170E2D0). 사용자 기재 적용을 보류합니다.");
      state.nameHookFailureLogged = true;
    }
    return false;
  }

  const uintptr_t pointerAddress = gameBase + kNameGetterPointerOffset;
  uintptr_t currentTarget = 0;
  if (!ReadMemorySafe(pointerAddress, &currentTarget, sizeof(currentTarget)))
    return false;

  const uintptr_t hookTarget =
      reinterpret_cast<uintptr_t>(&CustomTraitNameGetter);

  if (currentTarget == hookTarget) {
    state.nameHookInstalled = true;
    state.nameHookFailureLogged = false;
    return true;
  }

  if (state.nameHookInstalled) {
    // 다른 기능이 이름 getter를 바꿨다면 서로 덮어쓰지 않습니다.
    if (currentTarget != state.originalNameGetter) {
      if (!state.nameHookFailureLogged) {
        AddLog(u8"[기재JSON/이름] 이름 getter가 다른 패치로 변경되어 재설치를 보류합니다: %p",
               reinterpret_cast<void *>(currentTarget));
        state.nameHookFailureLogged = true;
      }
      return false;
    }
  } else {
    if (!IsExecutableAddress(currentTarget)) {
      if (!state.nameHookFailureLogged) {
        AddLog(u8"[기재JSON/이름] 원본 이름 getter 주소가 실행 가능 메모리가 아닙니다: %p",
               reinterpret_cast<void *>(currentTarget));
        state.nameHookFailureLogged = true;
      }
      return false;
    }
    state.originalNameGetter = currentTarget;
  }

  if (!WritePointerSafe(pointerAddress, hookTarget)) {
    if (!state.nameHookFailureLogged) {
      AddLog(u8"[기재JSON/이름] 이름 getter 포인터 교체 실패. 사용자 기재 적용을 보류합니다.");
      state.nameHookFailureLogged = true;
    }
    return false;
  }

  state.nameHookInstalled = true;
  state.nameHookFailureLogged = false;
  AddLog(u8"[기재JSON/이름] 원본 UI 이름 훅 설치 완료: original=%p hook=%p",
         reinterpret_cast<void *>(state.originalNameGetter),
         reinterpret_cast<void *>(hookTarget));
  return true;
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
  parsed.hasCustomText = !name.empty() || !desc.empty();

  ReadJsonIntValue(line, "cloneSrc", parsed.cloneSrc);
  ReadJsonIntValue(line, "bgTrait", parsed.bgTrait);
  ReadJsonIntValue(line, "grade", parsed.grade);

  entry = parsed;
  return true;
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
  if (path.empty()) {
    if (state.configLoaded) {
      AddLog(u8"[기재JSON] san8r_traits_config.json이 없어 런타임 적용을 중지합니다.");
    }
    state.configLoaded = false;
    state.applyPending = false;
    state.configPath.clear();
    state.hasWriteTime = false;
    state.sentinelIndex = -1;
    return false;
  }

  std::error_code ec;
  const auto writeTime = std::filesystem::last_write_time(path, ec);
  const bool haveWriteTime = !ec;

  if (state.configLoaded &&
      path == state.configPath &&
      haveWriteTime && state.hasWriteTime &&
      writeTime == state.configWriteTime) {
    return true;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open())
    return false;

  std::array<TraitConfigEntry, kTraitCount> traits{};
  std::array<TraitMetaEntry, kTraitCount> meta{};

  bool inCustomNames = false;
  int traitCount = 0;
  int customCount = 0;
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

  if (traitCount == 0) {
    AddLog(u8"[기재JSON] traits 항목을 읽지 못했습니다. 파일 형식을 확인하세요.");
    return false;
  }

  // 잘못된 파일을 기재 테이블 전체에 쓰는 것을 막기 위해 index/id 대응을 확인합니다.
  for (int i = 0; i < kTraitCount; ++i) {
    if (!traits[i].present)
      continue;
    if (traits[i].id != i + 1) {
      AddLog(u8"[기재JSON] index=%d / id=%d 불일치. 적용을 중지합니다.",
             i, traits[i].id);
      return false;
    }
  }

  int sentinel = -1;
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

  if (!PublishNativeNames(meta)) {
    AddLog(u8"[기재JSON/이름] JSON 이름 준비 실패. 기재 테이블 적용을 중지합니다.");
    return false;
  }

  state.traits = std::move(traits);
  state.meta = std::move(meta);
  state.configPath = path;
  state.configLoaded = true;
  state.configWriteTime = writeTime;
  state.hasWriteTime = haveWriteTime;
  state.sentinelIndex = sentinel;
  state.applyPending = true;
  changed = true;

  AddLog(u8"[기재JSON] 설정 로드: traits %d개 / customNames %d개 / 감시 index %d",
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

  if (!state.applyPending)
    return;

  if (ApplyConfig(tableBase)) {
    state.applyPending = false;
    AddLog(u8"[기재JSON] 기재 설정 적용 완료: table=%p",
           reinterpret_cast<void *>(tableBase));
  }
}

} // namespace DX11Base
