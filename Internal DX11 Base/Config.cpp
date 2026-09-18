#define SaveConfig SaveConfigBase
#define LoadConfig LoadConfigBase
#include "ConfigBase.inc"
#undef LoadConfig
#undef SaveConfig

#include "Cheats/Officer/TraitViewerFeature.h"
#include "Cheats/Civilian/JewelSettings.h"

namespace DX11Base {
  static void UpsertBoolConfigValue(const char *name, bool value) {
    std::ifstream in(GetConfigPath(), std::ios::binary);
    if (!in.is_open())
      return;

    std::ostringstream ss;
    ss << in.rdbuf();
    in.close();
    std::string data = ss.str();

    const std::string key = std::string("\"") + name + "\"";
    size_t existing = data.find(key);
    if (existing != std::string::npos) {
      size_t lineStart = data.rfind('\n', existing);
      lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
      size_t lineEnd = data.find('\n', existing);
      if (lineEnd == std::string::npos)
        lineEnd = data.size();
      else
        ++lineEnd;
      data.erase(lineStart, lineEnd - lineStart);
    }

    const size_t configEnd = data.find("\"config_end\"");
    if (configEnd == std::string::npos)
      return;

    size_t insertPos = data.rfind('\n', configEnd);
    insertPos = (insertPos == std::string::npos) ? configEnd : insertPos + 1;

    const std::string line = std::string("  \"") + name + "\": " +
                             (value ? "true" : "false") + ",\n";
    data.insert(insertPos, line);

    std::ofstream out(GetConfigPath(), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
      return;
    out << data;
  }

  static bool LoadBoolConfigValue(const char *name, bool &value) {
    std::ifstream file(GetConfigPath());
    if (!file.is_open())
      return false;

    const std::string key = std::string("\"") + name + "\"";
    std::string line;
    while (std::getline(file, line)) {
      if (line.find(key) != std::string::npos) {
        value = (line.find("true") != std::string::npos);
        return true;
      }
    }
    return false;
  }

  void SaveConfig() {
    SaveConfigBase();
    UpsertBoolConfigValue("bAIWarImprove", bAIWarImprove);
    UpsertBoolConfigValue("bTraitViewer", bTraitViewer);
    UpsertBoolConfigValue("bAllJewelsOpen", IsAllJewelsOpenPreferred());
    UpsertBoolConfigValue("bAllSecondaryJewels", IsAllSecondaryJewelsEnabled());
  }

  void LoadConfig() {
    LoadConfigBase();

    bool savedAIWarImprove = false;
    if (LoadBoolConfigValue("bAIWarImprove", savedAIWarImprove)) {
      bAIWarImprove = savedAIWarImprove;
      SetAIWarImprove(savedAIWarImprove);
      AddLog(u8"[Config] AI 전투 개선 설정 로드: %s", savedAIWarImprove ? "ON" : "OFF");
    }

    // 이전 설정 파일에 키가 없으면 기본값(true)으로 실제 패치까지 적용합니다.
    bool savedTraitViewer = true;
    const bool hasTraitViewerSetting = LoadBoolConfigValue("bTraitViewer", savedTraitViewer);
    bTraitViewer = savedTraitViewer;
    if (!SetTraitViewerFeature(bTraitViewer))
      bTraitViewer = IsTraitViewerFeatureApplied();
    AddLog(u8"[Config] 기재 화면 보이기 설정 로드%s: %s",
           hasTraitViewerSetting ? "" : "(기본값)", bTraitViewer ? "ON" : "OFF");

    bool savedAllJewelsOpen = false;
    const bool hasAllJewelOpenSetting =
        LoadBoolConfigValue("bAllJewelsOpen", savedAllJewelsOpen);
    SetAllJewelsOpenPreference(savedAllJewelsOpen);
    AddLog(u8"[Config] 보주 전체 개방 설정 로드%s: %s",
           hasAllJewelOpenSetting ? "" : "(기본값)",
           savedAllJewelsOpen ? "ON" : "OFF");

    bool savedAllSecondaryJewels = false;
    const bool hasAllSecondaryJewelSetting =
        LoadBoolConfigValue("bAllSecondaryJewels", savedAllSecondaryJewels);
    if (!SetAllSecondaryJewelsEnabled(savedAllSecondaryJewels)) {
      savedAllSecondaryJewels = IsAllSecondaryJewelsEnabled();
    }
    AddLog(u8"[Config] 보조 보주 전체 사용 설정 로드%s: %s",
           hasAllSecondaryJewelSetting ? "" : "(기본값)",
           savedAllSecondaryJewels ? "ON" : "OFF");
  }
} // namespace DX11Base
