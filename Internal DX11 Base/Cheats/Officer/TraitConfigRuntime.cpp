#include "TraitConfigRuntime.h"
#include "TraitTextEditorData.h"

// Keep the implementation body intact, but expose it under an internal entry
// point so this translation unit can enforce the runtime source policy first.
#define TickTraitConfigRuntime TickTraitConfigRuntimeImpl
#include "TraitConfigRuntime_impl.inc"
#undef TickTraitConfigRuntime

namespace DX11Base {

namespace {

std::filesystem::path ResolveDefaultTraitJsonPath() {
  std::filesystem::path preferred;
  wchar_t modulePath[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(g_hModule, modulePath, MAX_PATH);
  std::error_code ec;
  if (length > 0 && length < MAX_PATH) {
    preferred = std::filesystem::path(modulePath).parent_path() / L"S8RPK_traits_default.json";
    if (std::filesystem::is_regular_file(preferred, ec) && !ec)
      return preferred;
  }
  const auto cwd = std::filesystem::current_path(ec);
  if (!ec) {
    const auto path = cwd / L"S8RPK_traits_default.json";
    if (std::filesystem::is_regular_file(path, ec) && !ec)
      return path;
    if (preferred.empty())
      preferred = path;
  }
  return preferred;
}

} // namespace

bool LoadDefaultTraitJsonFromFile(std::string &out) {
  return ReadUtf8TextFile(ResolveDefaultTraitJsonPath(), out);
}

} // namespace DX11Base

namespace DX11Base {
namespace {

std::array<std::string, kTraitCount> g_editorNameOverrides{};
std::array<std::string, kTraitCount> g_editorDescOverrides{};
std::vector<TraitTextEditRow> g_embeddedEditorRows;
bool g_embeddedEditorRowsInitialized = false;
bool g_embeddedEditorRowsInitFailureLogged = false;

void SetTextError(std::string *error, const std::string &text) {
  if (error)
    *error = text;
}

bool Utf8ToWideText(const std::string &text, std::wstring &out) {
  if (text.find('\0') != std::string::npos)
    return false;
  if (text.empty()) {
    out.clear();
    return true;
  }

  const int count = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      nullptr, 0);
  if (count <= 0)
    return false;

  out.resize(static_cast<std::size_t>(count));
  return MultiByteToWideChar(
             CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
             out.data(), count) == count;
}

std::vector<std::string> EditorFormatTokens(const std::string &text) {
  std::vector<std::string> out;
  std::size_t pos = 0;
  while (pos < text.size()) {
    const std::size_t p = text.find('%', pos);
    if (p == std::string::npos)
      break;
    if (p + 1 < text.size() && text[p + 1] == '%') {
      pos = p + 2;
      continue;
    }

    std::size_t i = p + 1;
    while (i < text.size() && std::string("-+ #0").find(text[i]) != std::string::npos)
      ++i;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
      ++i;
    if (i < text.size() && text[i] == '.') {
      ++i;
      while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
        ++i;
    }

    if (i < text.size() && std::string("diuoxXfFeEgGaAcsp").find(text[i]) != std::string::npos) {
      out.emplace_back(text.substr(p, i - p + 1));
      pos = i + 1;
    } else {
      pos = p + 1;
    }
  }
  return out;
}

bool ReplaceJsonText(std::string &line, const char *field, const std::string &text) {
  const std::string key = std::string("\"") + field + "\"";
  const auto keyPos = line.find(key);
  if (keyPos == std::string::npos)
    return false;
  const auto colon = line.find(':', keyPos + key.size());
  if (colon == std::string::npos)
    return false;
  const auto begin = line.find('"', colon + 1);
  if (begin == std::string::npos)
    return false;
  auto end = begin + 1;
  for (; end < line.size(); ++end) {
    if (line[end] == '\\')
      ++end;
    else if (line[end] == '"')
      break;
  }
  if (end >= line.size())
    return false;

  std::string quoted = "\"";
  for (unsigned char ch : text) {
    switch (ch) {
    case '"': quoted += "\\\""; break;
    case '\\': quoted += "\\\\"; break;
    case '\b': quoted += "\\b"; break;
    case '\f': quoted += "\\f"; break;
    case '\n': quoted += "\\n"; break;
    case '\r': quoted += "\\r"; break;
    case '\t': quoted += "\\t"; break;
    default:
      if (ch < 0x20)
        return false;
      quoted.push_back(static_cast<char>(ch));
      break;
    }
  }
  quoted += '"';
  line.replace(begin, end - begin + 1, quoted);
  return true;
}

bool PublishEmbeddedTextsWithOverrides(
    const std::array<TraitMetaEntry, kTraitCount> &baseMeta,
    std::string *error = nullptr) {
  auto effective = baseMeta;
  for (int i = 0; i < kTraitCount; ++i) {
    if (!g_editorNameOverrides[i].empty()) {
      effective[i].present = true;
      effective[i].name = g_editorNameOverrides[i];
      effective[i].hasCustomText = true;
    }
    if (!g_editorDescOverrides[i].empty()) {
      effective[i].present = true;
      effective[i].desc = g_editorDescOverrides[i];
      effective[i].hasCustomText = true;
    }
  }

  if (!PublishNativeTexts(effective)) {
    SetTextError(error, "내장 기본기재 이름/설명 포인터 준비에 실패했습니다.");
    return false;
  }

  RuntimeState &state = State();
  state.descTablesSynced = false;
  state.descTableFailureLogged = false;
  return true;
}

bool EnsureEmbeddedEditorRows() {
  if (g_embeddedEditorRowsInitialized)
    return true;

  std::vector<EmbeddedTraitTextInfo> catalog;
  std::string error;
  if (!GetEmbeddedTraitTextCatalog(catalog, &error)) {
    if (!g_embeddedEditorRowsInitFailureLogged) {
      AddLog(u8"[기재 편집/내장] 목록 준비 실패: %s", error.c_str());
      g_embeddedEditorRowsInitFailureLogged = true;
    }
    return false;
  }

  g_embeddedEditorRows.clear();
  g_embeddedEditorRows.reserve(catalog.size());
  for (const auto &item : catalog) {
    TraitTextEditRow row;
    row.oldName = item.name;
    row.oldDesc = item.desc;
    row.embedded = true;
    row.traitId = item.traitId;
    g_embeddedEditorRows.push_back(std::move(row));
  }

  g_embeddedEditorRowsInitialized = true;
  g_embeddedEditorRowsInitFailureLogged = false;
  return true;
}

bool EnsureEmbeddedTraitConfigLoaded() {
  RuntimeState &state = State();
  if (state.configLoaded && state.configFromEmbedded)
    return true;

  std::string jsonText;
  if (!LoadEmbeddedJsonResource(IDR_JSON_TRAITS_DEFAULT, jsonText)) {
    AddLog(u8"[기재JSON] 외부 S8RPK_traits_default.json을 읽지 못했습니다.");
    return false;
  }

  std::array<TraitConfigEntry, kTraitCount> traits{};
  std::array<TraitMetaEntry, kTraitCount> meta{};
  int traitCount = 0;
  int customCount = 0;
  int sentinel = -1;

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

  if (traitCount == 0) {
    AddLog(u8"[기재JSON] 내장 기본 설정 형식을 읽지 못했습니다.");
    return false;
  }

  for (int i = 0; i < kTraitCount; ++i) {
    if (!traits[i].present)
      continue;
    if (traits[i].id != i + 1) {
      AddLog(u8"[기재JSON] 내장 기본 설정의 기재 ID 배열이 올바르지 않습니다: index=%d id=%d",
             i, traits[i].id);
      return false;
    }
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

  if (!PublishEmbeddedTextsWithOverrides(meta)) {
    AddLog(u8"[기재JSON/문구] 이름/설명 준비 실패. 내장 기본기재 적용을 중지합니다.");
    return false;
  }

  state.traits = std::move(traits);
  state.meta = std::move(meta);
  state.configPath.clear();
  state.configLoaded = true;
  state.configFromEmbedded = true;
  state.hasWriteTime = false;
  state.sentinelIndex = sentinel;
  state.applyPending = true;
  state.descTablesSynced = false;
  state.descTableFailureLogged = false;

  AddLog(u8"[기재JSON] version.dll 없음 -> 외부 S8RPK_traits_default.json 사용 / traits %d개 / customNames %d개 / 감시 index %d",
         traitCount, customCount, sentinel);
  return true;
}

} // namespace

bool GetEmbeddedTraitTextCatalog(
    std::vector<EmbeddedTraitTextInfo> &out,
    std::string *error) {
  out.clear();

  std::string jsonText;
  if (!LoadEmbeddedJsonResource(IDR_JSON_TRAITS_DEFAULT, jsonText)) {
    SetTextError(error, "외부 S8RPK_traits_default.json을 읽지 못했습니다.");
    return false;
  }

  std::istringstream file(jsonText);
  bool inCustomNames = false;
  std::string line;
  while (std::getline(file, line)) {
    if (!inCustomNames) {
      if (line.find("\"customNames\"") != std::string::npos)
        inCustomNames = true;
      continue;
    }

    int index = -1;
    TraitMetaEntry entry;
    if (!ParseCustomMetaLine(line, index, entry) ||
        !entry.hasCustomText || entry.name.empty()) {
      continue;
    }

    EmbeddedTraitTextInfo info;
    info.traitId = index + 1;
    info.name = entry.name;
    info.desc = entry.desc;
    out.push_back(std::move(info));
  }

  if (out.empty()) {
    SetTextError(error, "내장 기본기재 customNames에서 편집 가능한 이름을 찾지 못했습니다.");
    return false;
  }

  return true;
}

bool ApplyEmbeddedTraitTextOverrides(
    const std::vector<EmbeddedTraitTextOverride> &overrides,
    std::string *error) {
  if (overrides.empty())
    return ClearEmbeddedTraitTextOverrides(error);

  if (HasExternalGameVersionDll()) {
    SetTextError(error, "version.dll 사용 중에는 내장 기본기재 문구 편집을 적용할 수 없습니다.");
    return false;
  }

  if (!EnsureEmbeddedTraitConfigLoaded()) {
    SetTextError(error, "내장 기본기재 런타임을 준비하지 못했습니다.");
    return false;
  }

  std::array<std::string, kTraitCount> nextNames{};
  std::array<std::string, kTraitCount> nextDescs{};
  for (const auto &item : overrides) {
    if (item.traitId < 1 || item.traitId > kTraitCount) {
      SetTextError(error, "내장 기본기재 편집 ID가 범위를 벗어났습니다.");
      return false;
    }
    const int index = item.traitId - 1;
    nextNames[index] = item.name;
    nextDescs[index] = item.desc;
  }

  const auto previousNames = g_editorNameOverrides;
  const auto previousDescs = g_editorDescOverrides;
  g_editorNameOverrides = std::move(nextNames);
  g_editorDescOverrides = std::move(nextDescs);

  if (!PublishEmbeddedTextsWithOverrides(State().meta, error)) {
    g_editorNameOverrides = previousNames;
    g_editorDescOverrides = previousDescs;
    PublishEmbeddedTextsWithOverrides(State().meta, nullptr);
    return false;
  }

  AddLog(u8"[기재 편집/내장] ID 기반 이름/설명 오버레이 적용 (%zu개)", overrides.size());
  return true;
}

bool ClearEmbeddedTraitTextOverrides(std::string *error) {
  g_editorNameOverrides = {};
  g_editorDescOverrides = {};

  RuntimeState &state = State();
  if (!state.configLoaded || !state.configFromEmbedded || HasExternalGameVersionDll())
    return true;

  if (!PublishEmbeddedTextsWithOverrides(state.meta, error))
    return false;

  AddLog(u8"[기재 편집/내장] ID 기반 이름/설명 오버레이 해제");
  return true;
}

std::vector<TraitTextEditRow>& GetEmbeddedTraitTextEditRows() {
  EnsureEmbeddedEditorRows();
  return g_embeddedEditorRows;
}

std::string GetEmbeddedTraitTextStoragePath() {
  return ResolveDefaultTraitJsonPath().string();
}

bool LoadEmbeddedTraitTextEdits(std::string *error) {
  if (!EnsureEmbeddedEditorRows()) {
    SetTextError(error, "내장 기본기재 편집 목록을 준비하지 못했습니다.");
    return false;
  }
  std::string jsonText;
  if (!LoadDefaultTraitJsonFromFile(jsonText)) {
    SetTextError(error, "기본 기재 JSON을 읽지 못했습니다.");
    return false;
  }
  ResetEmbeddedTraitTextEdits();

  std::istringstream file(jsonText);
  bool inCustomNames = false;
  std::string line;
  while (std::getline(file, line)) {
    if (!inCustomNames) {
      if (line.find("\"customNames\"") != std::string::npos)
        inCustomNames = true;
      continue;
    }
    int index = -1;
    TraitMetaEntry entry;
    if (!ParseCustomMetaLine(line, index, entry))
      continue;
    for (auto &row : g_embeddedEditorRows) {
      if (row.traitId != index + 1)
        continue;
      if (entry.name != row.oldName)
        row.newName = entry.name;
      if (entry.desc != row.oldDesc)
        row.newDesc = entry.desc;
      break;
    }
  }
  return true;
}

bool SaveEmbeddedTraitTextEdits(std::string *error) {
  if (!EnsureEmbeddedEditorRows()) {
    SetTextError(error, "내장 기본기재 편집 목록을 준비하지 못했습니다.");
    return false;
  }
  for (std::size_t i = 0; i < g_embeddedEditorRows.size(); ++i) {
    if (!ValidateEmbeddedTraitTextRow(i, error))
      return false;
  }

  std::string jsonText;
  if (!LoadDefaultTraitJsonFromFile(jsonText)) {
    SetTextError(error, "기본 기재 JSON을 읽지 못했습니다.");
    return false;
  }
  std::istringstream input(jsonText);
  std::ostringstream output;
  bool inCustomNames = false;
  std::size_t updatedCount = 0;
  std::string line;
  while (std::getline(input, line)) {
    if (!inCustomNames) {
      if (line.find("\"customNames\"") != std::string::npos)
        inCustomNames = true;
    } else {
      int index = -1;
      TraitMetaEntry entry;
      if (ParseCustomMetaLine(line, index, entry)) {
        for (const auto &row : g_embeddedEditorRows) {
          if (row.traitId != index + 1)
            continue;
          if ((!row.newName.empty() && !ReplaceJsonText(line, "name", row.newName)) ||
              (!row.newDesc.empty() && !ReplaceJsonText(line, "desc", row.newDesc))) {
            SetTextError(error, "기재 JSON의 이름/설명 형식이 올바르지 않습니다.");
            return false;
          }
          ++updatedCount;
          break;
        }
      }
    }
    output << line;
    if (!input.eof())
      output << '\n';
  }
  if (updatedCount != g_embeddedEditorRows.size()) {
    SetTextError(error, "기재 JSON에서 편집 대상 ID를 모두 찾지 못했습니다.");
    return false;
  }

  const auto path = ResolveDefaultTraitJsonPath();
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    SetTextError(error, "기재 JSON 저장 파일을 열 수 없습니다: " + path.string());
    return false;
  }
  file << output.str();
  file.flush();
  if (!file.good()) {
    SetTextError(error, "S8RPK_traits_default.json 저장 중 쓰기 오류가 발생했습니다.");
    return false;
  }
  return true;
}

void ResetEmbeddedTraitTextEdits() {
  if (!EnsureEmbeddedEditorRows())
    return;
  for (auto &row : g_embeddedEditorRows) {
    row.newName.clear();
    row.newDesc.clear();
  }
}

bool HasEmbeddedTraitTextEdits() {
  if (!EnsureEmbeddedEditorRows())
    return false;
  for (const auto &row : g_embeddedEditorRows) {
    if ((!row.newName.empty() && row.newName != row.oldName) ||
        (!row.newDesc.empty() && row.newDesc != row.oldDesc)) {
      return true;
    }
  }
  return false;
}

bool ValidateEmbeddedTraitTextRow(std::size_t index, std::string *error) {
  if (!EnsureEmbeddedEditorRows() || index >= g_embeddedEditorRows.size()) {
    SetTextError(error, "내장 기본기재 인덱스가 범위를 벗어났습니다.");
    return false;
  }

  const auto &row = g_embeddedEditorRows[index];
  std::wstring wide;
  if (!Utf8ToWideText(row.newName, wide)) {
    SetTextError(error, row.oldName + ": 새 기재명이 올바른 UTF-8 문자열이 아닙니다.");
    return false;
  }
  if (wide.size() > 5) {
    SetTextError(error, row.oldName + ": 새 기재명은 최대 5글자입니다.");
    return false;
  }

  if (!Utf8ToWideText(row.newDesc, wide)) {
    SetTextError(error, row.oldName + ": 새 설명이 올바른 UTF-8 문자열이 아닙니다.");
    return false;
  }
  if (wide.size() > 512) {
    SetTextError(error, row.oldName + ": 문구는 UTF-16 기준 512자 이내로 입력하세요.");
    return false;
  }

  if (!row.newDesc.empty() && row.newDesc != row.oldDesc) {
    const auto originalTokens = EditorFormatTokens(row.oldDesc);
    const auto newTokens = EditorFormatTokens(row.newDesc);
    if (!newTokens.empty() && newTokens != originalTokens) {
      SetTextError(error, row.oldName +
          ": %d 등의 순서와 종류를 원문과 같게 유지하세요. 수치를 모두 생략하는 것은 가능합니다.");
      return false;
    }
  }
  return true;
}

void TickTraitConfigRuntime() {
  static bool externalPairLogged = false;

  // version.dll과 san8r_traits_config.json은 외부 기재 런타임 한 세트입니다.
  // version.dll이 있으면 S8RCheats는 기재 테이블에 개입하지 않고 외부 DLL에 맡깁니다.
  if (HasExternalGameVersionDll()) {
    if (!externalPairLogged) {
      AddLog(u8"[기재JSON] 외부 version.dll 감지 -> version.dll + san8r_traits_config.json 세트에 기재 처리를 맡깁니다.");
      externalPairLogged = true;
    }
    return;
  }
  externalPairLogged = false;

  // version.dll이 없을 때는 같은 폴더에 san8r_traits_config.json이 남아 있어도
  // 단독 설정으로 취급하지 않습니다. 외부 기본기재 파일을 사용합니다.
  if (!EnsureEmbeddedTraitConfigLoaded())
    return;

  // 기존 구현의 주기적 외부 JSON 탐색을 건너뛰되, 이름/설명/효과 훅과
  // 기재 테이블 재생성 감지/재적용 로직은 그대로 유지합니다.
  State().lastConfigProbeMs = GetTickCount64();
  TickTraitConfigRuntimeImpl();
}

} // namespace DX11Base
