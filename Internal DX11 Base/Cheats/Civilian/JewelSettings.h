#pragma once

namespace DX11Base {

// 정의된 보주 185개의 개방 비트를 한 번에 ON/OFF 합니다.
// 게임의 보주 개방 비트맵(GetGameBase()+0x71E0)을 원본 version.dll과 같은 마스크로 수정합니다.
bool SetAllJewelsOpen(bool enable);
bool AreAllJewelsOpen();

// UI/설정용: 실제 세이브 비트맵과 별개로 사용자가 전체 개방을 유지하길 원하는 상태.
bool IsAllJewelsOpenPreferred();
void SetAllJewelsOpenPreference(bool enable);

// 저장게임 로드가 보주 개방 비트맵을 덮어쓴 경우 자동 재적용.
void TickJewelSettings();

// 원본 version.dll의 "강제 전체" 동작에 해당합니다.
// UI에서는 의미가 더 분명하도록 "보조 보주 전체 사용"으로 표시합니다.
bool SetAllSecondaryJewelsEnabled(bool enable);
bool IsAllSecondaryJewelsEnabled();

} // namespace DX11Base
