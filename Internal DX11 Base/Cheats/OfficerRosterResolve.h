#pragma once

#include <cstddef>
#include <cstdint>

namespace DX11Base {

// Cheat Engine 포인터 스캔: 1번 무장(슬롯 0, stride 0x3D0) 베이스
// SAN8RPK.exe+034C8630 -> +48 -> +8 -> +10 -> +0 -> +8 (= 무장 배열 첫 슬롯 주소)
bool TryResolveOfficerRosterArrayBase(std::uintptr_t exeBase, std::uintptr_t *outRosterBase);

// Cheat Engine 포인터 스캔: 명품 배열(슬롯 1, stride 0x38) 베이스
// (SAN8R.exe+037B0000)+00 -> +20 -> +00 (= 명품 배열 첫 슬롯 주소)
bool TryResolveSpecialtyArrayBase(std::uintptr_t exeBase, std::uintptr_t *outSpecialtyBase);

} // namespace DX11Base
