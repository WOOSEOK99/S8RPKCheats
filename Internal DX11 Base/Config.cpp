#include "Cheats/Officer/TraitViewer.h"

#define SaveConfig SaveConfigBase
#define LoadConfig LoadConfigBase
#include "ConfigBase.inc"
#undef LoadConfig
#undef SaveConfig

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
  }

  void LoadConfig() {
    LoadConfigBase();

    bool savedAIWarImprove = false;
    if (LoadBoolConfigValue("bAIWarImprove", savedAIWarImprove)) {
      bAIWarImprove = savedAIWarImprove;
      SetAIWarImprove(savedAIWarImprove);
      AddLog(u8"[Config] AI 전투 개선 설정 로드: %s", savedAIWarImprove ? "ON" : "OFF");
    }

    // 이전 설정 파일에 키가 없으면 MenuState.cpp의 기본값(true)을 사용하고 실제 패치도 적용합니다.
    bool savedTraitViewer = true;
    if (LoadBoolConfigValue("bTraitViewer", savedTraitViewer))
      bTraitViewer = savedTraitViewer;

    if (!SetTraitViewer(bTraitViewer))
      bTraitViewer = IsTraitViewerApplied();
    AddLog(u8"[Config] 기재 화면 보이기 설정 로드: %s", bTraitViewer ? "ON" : "OFF");
  }
} // namespace DX11Base
