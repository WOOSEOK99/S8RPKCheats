#pragma once

#include <cstddef>
#include <cstdint>

namespace DX11Base {

// Cheat Engine 포인터 스캔: 1번 무장(슬롯 0, stride 0x3D0) 베이스
// SAN8RPK.exe+034C8630 -> +48 -> +8 -> +10 -> +0 -> +8 (= 무장 배열 첫 슬롯 주소)
bool TryResolveOfficerRosterArrayBase(std::uintptr_t exeBase, std::uintptr_t *outRosterBase);

} // namespace DX11Base
