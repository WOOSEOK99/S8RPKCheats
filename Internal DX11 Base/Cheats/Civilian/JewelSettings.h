#pragma once

#include <cstdint>
#include <string>

namespace DX11Base {

enum class JewelCategory : uint8_t {
  Common,
  Ronin,
  Leader,
  Normal,
  Strategist,
  Governor,
  Viceroy,
  Ruler,
  Evil,
  Warrior,
  Scholar,
};

struct JewelDefinition {
  uint16_t secondaryId;
  uint16_t rawJewelId;
  const char *name;
  JewelCategory category;
};

// 정의된 보주 185개의 개방 비트를 한 번에 ON/OFF 합니다.
// 게임의 보주 개방 비트맵(GetGameBase()+0x71E0)을 원본 version.dll과 같은 마스크로 수정합니다.
bool SetAllJewelsOpen(bool enable);
bool AreAllJewelsOpen();

// UI/설정용: 실제 세이브 비트맵과 별개로 사용자가 전체 개방을 유지하길 원하는 상태.
bool IsAllJewelsOpenPreferred();
void SetAllJewelsOpenPreference(bool enable);

// 저장게임 로드가 보주 개방 비트맵을 덮어쓴 경우 자동 재적용.
void TickJewelSettings();

// 보조 보주 강제 사용 선택.
// 전체 선택 API는 기존 설정과의 호환을 위해 유지하며, 내부적으로는 185개 개별 선택 비트를 변경합니다.
bool SetAllSecondaryJewelsEnabled(bool enable);
bool IsAllSecondaryJewelsEnabled();

// 개별 선택 상태를 config에 저장/복원하기 위한 고정 길이 hex mask.
std::string GetSecondaryJewelForceMaskHex();
bool LoadSecondaryJewelForceMaskHex(const std::string &maskHex);

// 상세 선택 UI.
void OpenJewelSettingsWindow();
void DrawJewelSettingsWindow(float scale);

} // namespace DX11Base
