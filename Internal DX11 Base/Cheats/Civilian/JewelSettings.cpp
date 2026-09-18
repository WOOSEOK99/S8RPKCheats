#include "../../pch.h"
#include "JewelSettings.h"

#include "../../Cheats.h"
#include "../../Hooking/MinHook.h"
#include "../../showlog.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace DX11Base {
namespace {

// version.dll의 정의 테이블(185개)을 그대로 비트 마스크로 변환한 값.
// 개방 비트는 raw jewel ID 자체를 bit index로 사용합니다.
constexpr std::array<uint8_t, 34> kDefinedJewelOpenMask = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFC, 0xFF, 0xFF, 0xBF, 0xFF, 0xFF, 0xF7, 0xFF,
    0xFF, 0xFE, 0xFF, 0xDF, 0xFF, 0xFF, 0xFB, 0xFF,
    0x7F, 0xFF, 0xFF, 0xEF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x03, 0x00,
};

// "강제" 판정 함수의 ID는 raw jewel ID - 0x40이며, 내부 비트는 (ID-1)을 사용합니다.
// 아래 마스크 역시 같은 185개 정의만 포함합니다.
constexpr std::array<uint8_t, 64> kDefinedSecondaryJewelMask = {
    0xFE, 0xFF, 0xFF, 0xDF, 0xFF, 0xFF, 0xFB, 0xFF,
    0x7F, 0xFF, 0xFF, 0xEF, 0xFF, 0xFF, 0xFD, 0xFF,
    0xBF, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xFF, 0xFF,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr uintptr_t kJewelOpenBitmapOffset = 0x71E0;
constexpr uintptr_t kSecondaryJewelJudgeOffset = 0x17ABBF0;
constexpr uintptr_t kSecondaryJewelObjectTableIndexBase = 0xB11DE;
constexpr uint16_t kRawJewelIdBias = 0x40;
constexpr uint16_t kMaxSecondaryJewelId = 500;

using SecondaryJewelJudgeFn = bool(__fastcall *)(uint16_t jewelId);

std::atomic_bool g_allSecondaryJewelsEnabled{false};
SecondaryJewelJudgeFn g_originalSecondaryJewelJudge = nullptr;
bool g_secondaryJewelHookInstalled = false;

// 0 = 미확인, 1 = 런타임 데이터 없음, 2 = 런타임 데이터 존재.
// 원 version.dll도 ID별 캐시를 사용해 hot path에서 반복 포인터 검증을 피합니다.
std::array<uint8_t, kMaxSecondaryJewelId + 1> g_secondaryRuntimeCache{};
uintptr_t g_secondaryRuntimeCacheBase = 0;

bool IsDefinedSecondaryJewelId(uint16_t jewelId) {
  if (jewelId < 1 || jewelId > kMaxSecondaryJewelId)
    return false;

  const uint32_t bitIndex = static_cast<uint32_t>(jewelId - 1);
  const uint32_t byteIndex = bitIndex >> 3;
  const uint8_t bitMask = static_cast<uint8_t>(1u << (bitIndex & 7));
  return (kDefinedSecondaryJewelMask[byteIndex] & bitMask) != 0;
}

bool IsJewelOpenFast(uintptr_t gameBase, uint16_t rawJewelId) {
  const uint32_t byteIndex = static_cast<uint32_t>(rawJewelId) >> 3;
  if (byteIndex >= kDefinedJewelOpenMask.size())
    return false;

  const uintptr_t address = gameBase + kJewelOpenBitmapOffset + byteIndex;
  const uint8_t bitMask = static_cast<uint8_t>(1u << (rawJewelId & 7));

  __try {
    return ((*reinterpret_cast<const uint8_t *>(address)) & bitMask) != 0;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool ProbeSecondaryJewelRuntimeData(uintptr_t gameBase, uint16_t jewelId) {
  const uintptr_t slot =
      gameBase + sizeof(uintptr_t) *
                     (static_cast<uintptr_t>(jewelId) + kSecondaryJewelObjectTableIndexBase);

  __try {
    const uintptr_t first = *reinterpret_cast<const uintptr_t *>(slot);
    if (!first)
      return false;

    const uintptr_t second = *reinterpret_cast<const uintptr_t *>(first);
    return second != 0;
  }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool HasSecondaryJewelRuntimeDataCached(uintptr_t gameBase, uint16_t jewelId) {
  if (jewelId > kMaxSecondaryJewelId)
    return false;

  // 세이브/시나리오 전환 등으로 gameBase가 바뀌면 캐시를 다시 계산합니다.
  if (g_secondaryRuntimeCacheBase != gameBase) {
    g_secondaryRuntimeCache.fill(0);
    g_secondaryRuntimeCacheBase = gameBase;
  }

  uint8_t &cached = g_secondaryRuntimeCache[jewelId];
  if (cached == 0)
    cached = ProbeSecondaryJewelRuntimeData(gameBase, jewelId) ? 2 : 1;

  return cached == 2;
}

bool __fastcall HookSecondaryJewelJudge(uint16_t jewelId) {
  // 원 version.dll과 동일하게 "강제 경로"를 먼저 검사하고,
  // 강제 조건이 충족되지 않을 때만 게임 원본 판정으로 넘깁니다.
  if (g_allSecondaryJewelsEnabled.load(std::memory_order_relaxed) &&
      IsDefinedSecondaryJewelId(jewelId)) {
    const uintptr_t gameBase = GetGameBaseFast();
    if (gameBase) {
      const uint16_t rawJewelId =
          static_cast<uint16_t>(jewelId + kRawJewelIdBias);

      if (IsJewelOpenFast(gameBase, rawJewelId) &&
          HasSecondaryJewelRuntimeDataCached(gameBase, jewelId)) {
        return true;
      }
    }
  }

  return g_originalSecondaryJewelJudge
             ? g_originalSecondaryJewelJudge(jewelId)
             : false;
}

bool EnsureSecondaryJewelHook() {
  if (g_secondaryJewelHookInstalled)
    return true;

  HMODULE exe = GetModuleHandleW(L"SAN8RPK.exe");
  if (!exe)
    exe = GetModuleHandleW(nullptr);
  if (!exe) {
    AddLog(u8"[보주] SAN8RPK.exe 모듈을 찾지 못했습니다.");
    return false;
  }

  const uintptr_t target =
      reinterpret_cast<uintptr_t>(exe) + kSecondaryJewelJudgeOffset;
  constexpr uint8_t kExpectedEntry[5] = {0x48, 0x89, 0x5C, 0x24, 0x08};

  if (!IsValidPtr(target, sizeof(kExpectedEntry)) ||
      std::memcmp(reinterpret_cast<const void *>(target), kExpectedEntry,
                  sizeof(kExpectedEntry)) != 0) {
    AddLog(u8"[보주] 보조 보주 판정 함수 바이트가 예상과 다릅니다: SAN8RPK.exe+0x17ABBF0");
    return false;
  }

  const MH_STATUS createStatus =
      MH_CreateHook(reinterpret_cast<LPVOID>(target),
                    reinterpret_cast<LPVOID>(&HookSecondaryJewelJudge),
                    reinterpret_cast<LPVOID *>(&g_originalSecondaryJewelJudge));
  if (createStatus != MH_OK) {
    AddLog(u8"[보주] 보조 보주 훅 생성 실패: MH_STATUS=%d",
           static_cast<int>(createStatus));
    return false;
  }

  const MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(target));
  if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
    MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    g_originalSecondaryJewelJudge = nullptr;
    AddLog(u8"[보주] 보조 보주 훅 활성화 실패: MH_STATUS=%d",
           static_cast<int>(enableStatus));
    return false;
  }

  g_secondaryJewelHookInstalled = true;
  AddLog(u8"[보주] 보조 보주 판정 훅 적용: SAN8RPK.exe+0x17ABBF0");
  return true;
}

} // namespace

bool SetAllJewelsOpen(bool enable) {
  const uintptr_t gameBase = GetGameBase();
  if (!gameBase) {
    AddLog(u8"[보주] 게임 기준 주소를 찾지 못해 전체 개방을 변경하지 못했습니다.");
    return false;
  }

  const uintptr_t bitmapAddress = gameBase + kJewelOpenBitmapOffset;
  if (!IsValidPtr(bitmapAddress, kDefinedJewelOpenMask.size())) {
    AddLog(u8"[보주] 개방 비트맵 주소가 유효하지 않습니다: %p",
           reinterpret_cast<void *>(bitmapAddress));
    return false;
  }

  auto *bitmap = reinterpret_cast<uint8_t *>(bitmapAddress);
  for (size_t i = 0; i < kDefinedJewelOpenMask.size(); ++i) {
    if (enable)
      bitmap[i] = static_cast<uint8_t>(bitmap[i] | kDefinedJewelOpenMask[i]);
    else
      bitmap[i] = static_cast<uint8_t>(bitmap[i] & ~kDefinedJewelOpenMask[i]);
  }

  AddLog(enable ? u8"[보주] 정의된 보주 185개 전체 개방 ON"
                : u8"[보주] 정의된 보주 185개 전체 개방 OFF");
  return true;
}

bool SetAllSecondaryJewelsEnabled(bool enable) {
  if (enable && !EnsureSecondaryJewelHook())
    return false;

  if (enable) {
    g_secondaryRuntimeCache.fill(0);
    g_secondaryRuntimeCacheBase = 0;
  }

  g_allSecondaryJewelsEnabled.store(enable, std::memory_order_relaxed);
  AddLog(enable ? u8"[보주] 보조 보주 전체 사용 ON"
                : u8"[보주] 보조 보주 전체 사용 OFF");
  return true;
}

bool IsAllSecondaryJewelsEnabled() {
  return g_allSecondaryJewelsEnabled.load(std::memory_order_relaxed);
}

} // namespace DX11Base
