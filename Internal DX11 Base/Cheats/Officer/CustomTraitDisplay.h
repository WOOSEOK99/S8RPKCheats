#pragma once

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../EmbeddedJsonResources.h"

namespace DX11Base {

extern HMODULE g_hModule;

struct CustomTraitDisplayInfo {
  std::string name;
  std::string desc;
  int grade = 0; // 1=황금, 2=녹색, 3=적색
};

namespace CustomTraitDisplayDetail {

inline bool IsDecimalKey(const std::string &s) {
  if (s.empty())
    return false;
  for (unsigned char c : s) {
    if (!std::isdigit(c))
      return false;
  }
  return true;
}

inline bool ReadJsonStringValue(const std::string &line, const char *field, std::string &out) {
  const std::string key = std::string("\"") + field + "\"";
  size_t p = line.find(key);
  if (p == std::string::npos)
    return false;

  p = line.find(':', p + key.size());
  if (p == std::string::npos)
    return false;

  p = line.find('"', p + 1);
  if (p == std::string::npos)
    return false;

  ++p;
  out.clear();
  bool escaped = false;
  for (; p < line.size(); ++p) {
    const char c = line[p];
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
        // 현재 공식 파일은 한글을 UTF-8 원문으로 저장하므로
        // 알 수 없는 escape는 원문 보존을 우선합니다.
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

inline bool ReadJsonIntValue(const std::string &line, const char *field, int &out) {
  const std::string key = std::string("\"") + field + "\"";
  size_t p = line.find(key);
  if (p == std::string::npos)
    return false;

  p = line.find(':', p + key.size());
  if (p == std::string::npos)
    return false;

  ++p;
  while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p])))
    ++p;

  size_t end = p;
  if (end < line.size() && (line[end] == '-' || line[end] == '+'))
    ++end;
  while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end])))
    ++end;
  if (end == p || (end == p + 1 && (line[p] == '-' || line[p] == '+')))
    return false;

  try {
    out = std::stoi(line.substr(p, end - p));
    return true;
  } catch (...) {
    return false;
  }
}

struct Cache {
  std::unordered_map<int, CustomTraitDisplayInfo> byIndex;
  std::filesystem::file_time_type writeTime{};
  bool hasWriteTime = false;
  bool loaded = false;
  bool fromEmbedded = false;
  ULONGLONG lastCheckMs = 0;
};

inline Cache &GetCache() {
  static Cache cache;
  return cache;
}

inline bool HasExternalGameVersionDll() {
  wchar_t exePath[MAX_PATH] = {};
  const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  if (len == 0 || len >= MAX_PATH)
    return false;

  std::error_code ec;
  const std::filesystem::path p =
      std::filesystem::path(exePath).parent_path() / L"version.dll";
  return std::filesystem::is_regular_file(p, ec) && !ec;
}

inline std::filesystem::path ResolveConfigPath() {
  char modulePath[MAX_PATH] = {};
  if (GetModuleFileNameA(g_hModule, modulePath, MAX_PATH)) {
    std::filesystem::path p = std::filesystem::path(modulePath).parent_path() / "san8r_traits_config.json";
    std::error_code ec;
    if (std::filesystem::exists(p, ec) && !ec)
      return p;
  }

  // 프록시 DLL과 실행 파일의 작업 폴더가 다른 경우를 위한 보조 경로.
  std::error_code ec;
  std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (!ec) {
    std::filesystem::path p = cwd / "san8r_traits_config.json";
    if (std::filesystem::exists(p, ec) && !ec)
      return p;
  }
  return {};
}

inline bool ReloadIfNeeded() {
  Cache &cache = GetCache();
  const ULONGLONG now = GetTickCount64();
  if (cache.loaded && now - cache.lastCheckMs < 2000)
    return !cache.byIndex.empty();
  cache.lastCheckMs = now;

  const std::filesystem::path path = ResolveConfigPath();
  std::error_code ec;
  std::filesystem::file_time_type currentWriteTime{};
  bool haveWriteTime = false;

  if (!path.empty()) {
    currentWriteTime = std::filesystem::last_write_time(path, ec);
    haveWriteTime = !ec;
    if (cache.loaded && !cache.fromEmbedded &&
        haveWriteTime && cache.hasWriteTime &&
        currentWriteTime == cache.writeTime) {
      return !cache.byIndex.empty();
    }
  } else if (cache.loaded && cache.fromEmbedded) {
    return !cache.byIndex.empty();
  }

  std::string jsonText;
  bool fromEmbedded = false;
  if (!path.empty()) {
    if (!ReadUtf8TextFile(path, jsonText))
      jsonText.clear();
  }

  if (jsonText.empty()) {
    if (HasExternalGameVersionDll()) {
      cache.loaded = true;
      cache.fromEmbedded = false;
      cache.byIndex.clear();
      cache.hasWriteTime = false;
      return false;
    }

    if (!LoadEmbeddedJsonResource(IDR_JSON_TRAITS_DEFAULT, jsonText)) {
      cache.loaded = true;
      cache.fromEmbedded = false;
      cache.byIndex.clear();
      cache.hasWriteTime = false;
      return false;
    }
    fromEmbedded = true;
  }

  std::unordered_map<int, CustomTraitDisplayInfo> parsed;
  std::istringstream file(jsonText);
  std::string line;
  bool inCustomNames = false;

  while (std::getline(file, line)) {
    if (!inCustomNames) {
      if (line.find("\"customNames\"") != std::string::npos)
        inCustomNames = true;
      continue;
    }

    const size_t q1 = line.find('"');
    if (q1 == std::string::npos)
      continue;
    const size_t q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos)
      continue;

    const std::string key = line.substr(q1 + 1, q2 - q1 - 1);
    if (!IsDecimalKey(key))
      continue;

    CustomTraitDisplayInfo info;
    ReadJsonStringValue(line, "name", info.name);
    ReadJsonStringValue(line, "desc", info.desc);
    ReadJsonIntValue(line, "grade", info.grade);

    try {
      parsed[std::stoi(key)] = std::move(info);
    } catch (...) {
    }
  }

  cache.byIndex.swap(parsed);
  cache.loaded = true;
  cache.fromEmbedded = fromEmbedded;
  cache.hasWriteTime = !fromEmbedded && haveWriteTime;
  if (cache.hasWriteTime)
    cache.writeTime = currentWriteTime;
  return !cache.byIndex.empty();
}

} // namespace CustomTraitDisplayDetail

// 게임의 기재 ID는 1부터 시작하고 customNames 키는 0부터 시작하므로 ID - 1을 사용합니다.
inline bool GetCustomTraitDisplayInfo(uint16_t traitId, CustomTraitDisplayInfo &out) {
  if (traitId == 0)
    return false;

  CustomTraitDisplayDetail::ReloadIfNeeded();
  const int customIndex = static_cast<int>(traitId) - 1;
  auto &map = CustomTraitDisplayDetail::GetCache().byIndex;
  auto it = map.find(customIndex);
  if (it == map.end())
    return false;

  out = it->second;
  return !out.name.empty() || !out.desc.empty();
}

inline bool HasCustomTraitConfigFile() {
  if (!CustomTraitDisplayDetail::ResolveConfigPath().empty())
    return true;
  if (CustomTraitDisplayDetail::HasExternalGameVersionDll())
    return false;
  std::string embedded;
  return LoadEmbeddedJsonResource(IDR_JSON_TRAITS_DEFAULT, embedded);
}

// customNames에 실제 이름이 등록된 항목만 "사용 가능한 커스텀 기재"로 취급합니다.
// JSON key는 0-base, 게임 기재 ID는 1-base입니다.
inline std::vector<uint16_t> GetDefinedCustomTraitIds() {
  CustomTraitDisplayDetail::ReloadIfNeeded();

  std::vector<uint16_t> ids;
  auto &map = CustomTraitDisplayDetail::GetCache().byIndex;
  ids.reserve(map.size());

  for (const auto &entry : map) {
    if (entry.first < 0 || entry.first >= 0xFFFF)
      continue;
    if (entry.second.name.empty())
      continue;

    const uint32_t traitId = static_cast<uint32_t>(entry.first) + 1u;
    if (traitId == 0 || traitId > 0xFFFF)
      continue;
    ids.push_back(static_cast<uint16_t>(traitId));
  }

  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return ids;
}

} // namespace DX11Base
