#define SaveConfig SaveConfigBase
#define LoadConfig LoadConfigBase
#include "ConfigBase.inc"
#undef LoadConfig
#undef SaveConfig

namespace DX11Base {
  static void SaveAIWarImproveConfigValue() {
    std::ifstream in(GetConfigPath(), std::ios::binary);
    if (!in.is_open())
      return;

    std::ostringstream ss;
    ss << in.rdbuf();
    in.close();
    std::string data = ss.str();

    // 혹시 이전 실행에서 키가 남아 있으면 중복되지 않도록 기존 줄을 제거합니다.
    const std::string key = "\"bAIWarImprove\"";
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

    const std::string line = std::string("  \"bAIWarImprove\": ") +
                             (bAIWarImprove ? "true" : "false") + ",\n";
    data.insert(insertPos, line);

    std::ofstream out(GetConfigPath(), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
      return;
    out << data;
  }

  static bool LoadAIWarImproveConfigValue(bool &value) {
    std::ifstream file(GetConfigPath());
    if (!file.is_open())
      return false;

    std::string line;
    while (std::getline(file, line)) {
      if (line.find("\"bAIWarImprove\"") != std::string::npos) {
        value = (line.find("true") != std::string::npos);
        return true;
      }
    }
    return false;
  }

  void SaveConfig() {
    SaveConfigBase();
    SaveAIWarImproveConfigValue();
  }

  void LoadConfig() {
    LoadConfigBase();

    bool savedValue = false;
    if (!LoadAIWarImproveConfigValue(savedValue))
      return;

    bAIWarImprove = savedValue;
    SetAIWarImprove(savedValue);
    AddLog(u8"[Config] AI 전투 개선 설정 로드: %s", savedValue ? "ON" : "OFF");
  }
} // namespace DX11Base
