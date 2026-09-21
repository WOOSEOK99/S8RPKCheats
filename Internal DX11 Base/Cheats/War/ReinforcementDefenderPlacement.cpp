#include "../../pch.h"

#include "../../Cheats.h"
#include "../../MemoryUtils.h"
#include "../../showlog.h"
#include "ReinforcementDefenderPlacement.h"

#include <climits>
#include <cstdint>
#include <cstring>

namespace DX11Base {
  namespace {
    constexpr uintptr_t kDispatchTableOffset = 0x01D86D58;
    constexpr uintptr_t kAnchorTurnOffset = 0x01D86585;
    constexpr uintptr_t kAnchorArrivalCallOffset = 0x01D86C51;
    constexpr uintptr_t kAnchorOrderCallOffset = 0x01D86C60;
    constexpr uintptr_t kAnchorSpawnCallOffset = 0x01E38305;
    constexpr uintptr_t kSelectorCallOffset = 0x01E37C24;
    constexpr uintptr_t kSelectorSignatureOffset = 0x01E41891;

    constexpr uintptr_t kPassabilityOffset = 0x01D76440;
    constexpr uintptr_t kOriginalSelectorOffset = 0x01E41810;

    constexpr uint64_t kPlacementFailure = 0x8000000080000000ULL;

    const uint8_t kDispatchOriginal[8] =
        {0x13, 0x6C, 0xD8, 0x01, 0x5B, 0x6C, 0xD8, 0x01};
    const uint8_t kDispatchArrivalFirst[8] =
        {0x5B, 0x6C, 0xD8, 0x01, 0x13, 0x6C, 0xD8, 0x01};

    const uint8_t kAnchorTurnOriginal[5] =
        {0xFF, 0xC8, 0x83, 0xF8, 0x0E};
    const uint8_t kAnchorArrivalCallOriginal[5] =
        {0xE8, 0x5A, 0xAA, 0x0B, 0x00};
    const uint8_t kAnchorOrderCallOriginal[5] =
        {0xE8, 0x6B, 0x66, 0x0B, 0x00};
    const uint8_t kAnchorSpawnCallOriginal[5] =
        {0xE8, 0x56, 0xDE, 0xFF, 0xFF};
    const uint8_t kSelectorCallOriginal[5] =
        {0xE8, 0xE7, 0x9B, 0x00, 0x00};
    const uint8_t kSelectorSignature[7] =
        {0x48, 0x8B, 0x03, 0x44, 0x38, 0x60, 0x09};

    using OriginalSelectorFn = uint64_t (*)(void *);
    using PassabilityFn = uint8_t (*)(void *, void *);

    bool g_applied = false;
    uintptr_t g_stub = 0;
    uint8_t g_selectorCallPatched[5]{};
    OriginalSelectorFn g_originalSelector = nullptr;
    PassabilityFn g_passability = nullptr;

    template <typename T>
    bool ReadValue(uintptr_t address, T &out) {
      if (!address || !IsValidPtr(address, sizeof(T)))
        return false;

      __try {
        out = *reinterpret_cast<const T *>(address);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    // 맵 타일 전체 범위를 한 번 검증한 뒤 hot loop에서 사용하는 빠른 읽기.
    // 매 필드마다 VirtualQuery를 호출하지 않고 SEH로 잘못된 접근만 차단합니다.
    template <typename T>
    bool ReadValueFast(uintptr_t address, T &out) {
      if (!address)
        return false;

      __try {
        out = *reinterpret_cast<const T *>(address);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    bool ReadBytes(uintptr_t address, uint8_t *out, size_t size) {
      if (!out || !size || !IsValidPtr(address, size))
        return false;

      __try {
        memcpy(out, reinterpret_cast<const void *>(address), size);
        return true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
      }
    }

    bool BytesEqual(uintptr_t address, const uint8_t *expected, size_t size) {
      uint8_t current[16]{};
      if (!expected || size > sizeof(current))
        return false;
      return ReadBytes(address, current, size) &&
             memcmp(current, expected, size) == 0;
    }

    bool WriteBytes(uintptr_t address, const uint8_t *bytes, size_t size) {
      if (!bytes || !size || !IsValidPtr(address, size))
        return false;

      DWORD oldProtect = 0;
      if (!VirtualProtect(reinterpret_cast<LPVOID>(address), size,
                          PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

      bool success = false;
      __try {
        memcpy(reinterpret_cast<void *>(address), bytes, size);
        success = true;
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        success = false;
      }

      DWORD ignored = 0;
      VirtualProtect(reinterpret_cast<LPVOID>(address), size, oldProtect, &ignored);
      if (success) {
        FlushInstructionCache(GetCurrentProcess(),
                              reinterpret_cast<LPCVOID>(address), size);
      }
      return success;
    }

    bool BuildRelativeCall(uintptr_t from, uintptr_t to, uint8_t out[5]) {
      const int64_t rel =
          static_cast<int64_t>(to) - static_cast<int64_t>(from + 5);
      if (rel < INT32_MIN || rel > INT32_MAX)
        return false;

      out[0] = 0xE8;
      const int32_t rel32 = static_cast<int32_t>(rel);
      memcpy(out + 1, &rel32, sizeof(rel32));
      return true;
    }

    uint64_t CallOriginal(void *reinforcement) {
      return g_originalSelector ? g_originalSelector(reinforcement)
                                : kPlacementFailure;
    }

    uint64_t ReinforcementSelectorHook(void *reinforcement) {
      const uintptr_t reinforcementAddr =
          reinterpret_cast<uintptr_t>(reinforcement);
      if (!reinforcementAddr)
        return CallOriginal(reinforcement);

      uintptr_t unitState = 0;
      if (!ReadValue(reinforcementAddr + 0x08, unitState) || !unitState)
        return CallOriginal(reinforcement);

      uintptr_t ownerBackref = 0;
      if (!ReadValue(unitState, ownerBackref) ||
          ownerBackref != reinforcementAddr)
        return CallOriginal(reinforcement);

      uint8_t state5D = 0;
      if (!ReadValue(unitState + 0x5D, state5D) || state5D != 3)
        return CallOriginal(reinforcement);

      uint8_t state5C = 0;
      if (!ReadValue(unitState + 0x5C, state5C) ||
          (state5C != 1 && state5C != 2))
        return CallOriginal(reinforcement);

      uintptr_t sideHolder = 0;
      uintptr_t sideObject = 0;
      uint8_t side = 0;
      if (!ReadValue(unitState + 0x30, sideHolder) || !sideHolder ||
          !ReadValue(sideHolder, sideObject) || !sideObject ||
          !ReadValue(sideObject + 0x18, side) || side != 1)
        return CallOriginal(reinforcement);

      uintptr_t battleHolder = 0;
      uintptr_t battleObject = 0;
      uintptr_t battleContainer = 0;
      if (!ReadValue(unitState + 0x10, battleHolder) || !battleHolder ||
          !ReadValue(battleHolder, battleObject) || !battleObject ||
          !ReadValue(battleObject + 0x50, battleContainer) || !battleContainer)
        return CallOriginal(reinforcement);

      uintptr_t unitList = 0;
      uint32_t unitCount = 0;
      uintptr_t unitEntries = 0;
      if (!ReadValue(battleContainer + 0x108, unitList) || !unitList ||
          !ReadValue(unitList + 0x18, unitCount) ||
          unitCount == 0 || unitCount > 0x1000 ||
          !ReadValue(unitList + 0x10, unitEntries) || !unitEntries)
        return CallOriginal(reinforcement);

      uintptr_t commanderTile = 0;
      for (uint32_t i = 0; i < unitCount; ++i) {
        const uintptr_t entry = unitEntries + static_cast<uintptr_t>(i) * 0x10;

        uintptr_t unit = 0;
        if (!ReadValue(entry + 0x08, unit) || !unit)
          continue;

        uintptr_t unitBackref = 0;
        if (!ReadValue(unit, unitBackref) || unitBackref != entry)
          continue;

        uint8_t unitState5D = 0;
        if (!ReadValue(unit + 0x5D, unitState5D) || unitState5D != 2)
          continue;

        uint16_t commanderMarker = 0;
        if (!ReadValue(unit + 0x38, commanderMarker) || commanderMarker == 0)
          continue;

        uintptr_t unitSideHolder = 0;
        uintptr_t unitSideObject = 0;
        uint8_t unitSide = 0;
        if (!ReadValue(unit + 0x30, unitSideHolder) || !unitSideHolder ||
            !ReadValue(unitSideHolder, unitSideObject) || !unitSideObject ||
            !ReadValue(unitSideObject + 0x18, unitSide) || unitSide != 1)
          continue;

        uintptr_t tile = 0;
        if (!ReadValue(unit + 0x40, tile) || !tile)
          continue;

        if (commanderTile)
          return CallOriginal(reinforcement);
        commanderTile = tile;
      }

      if (!commanderTile)
        return CallOriginal(reinforcement);

      uintptr_t mapObject = 0;
      uint32_t tileCount = 0;
      uintptr_t tileBase = 0;
      if (!ReadValue(battleContainer + 0x18, mapObject) || !mapObject ||
          !ReadValue(mapObject + 0x198, tileCount) ||
          tileCount == 0 || tileCount > 0x4E20 ||
          !ReadValue(mapObject + 0x1A0, tileBase) || !tileBase)
        return CallOriginal(reinforcement);

      const size_t tileBytes = static_cast<size_t>(tileCount) * 0x40;
      const uintptr_t tileEnd = tileBase + tileBytes;
      if (!IsValidPtr(tileBase, tileBytes))
        return CallOriginal(reinforcement);

      if (commanderTile < tileBase || commanderTile >= tileEnd ||
          ((commanderTile - tileBase) & 0x3F) != 0)
        return CallOriginal(reinforcement);

      int32_t commanderQ = 0;
      int32_t commanderR = 0;
      if (!ReadValue(commanderTile + 0x08, commanderQ) ||
          !ReadValue(commanderTile + 0x0C, commanderR))
        return CallOriginal(reinforcement);

      int32_t bestDistance = INT_MAX;
      uintptr_t bestTile = 0;

      for (uint32_t i = 0; i < tileCount; ++i) {
        const uintptr_t tile = tileBase + static_cast<uintptr_t>(i) * 0x40;

        uintptr_t occupantA = 0;
        uintptr_t occupantB = 0;
        uintptr_t tileInfo = 0;
        uint8_t tileType = 0;
        if (!ReadValueFast(tile + 0x18, occupantA) || occupantA != 0 ||
            !ReadValueFast(tile + 0x20, occupantB) || occupantB != 0 ||
            !ReadValueFast(tile, tileInfo) || !tileInfo ||
            !ReadValueFast(tileInfo + 0x09, tileType) || tileType == 0x0A)
          continue;

        int32_t tileQ = 0;
        int32_t tileR = 0;
        if (!ReadValueFast(tile + 0x08, tileQ) ||
            !ReadValueFast(tile + 0x0C, tileR))
          continue;

        int64_t dq = static_cast<int64_t>(tileQ) - commanderQ;
        int64_t dr = static_cast<int64_t>(tileR) - commanderR;
        int64_t ds = dq + dr;
        if (dq < 0)
          dq = -dq;
        if (dr < 0)
          dr = -dr;
        if (ds < 0)
          ds = -ds;

        int64_t distance = dq;
        if (dr > distance)
          distance = dr;
        if (ds > distance)
          distance = ds;

        if (distance == 0 || distance >= bestDistance)
          continue;

        if (!g_passability)
          continue;

        const uint8_t passable =
            g_passability(reinterpret_cast<void *>(tile), reinforcement);
        if (!passable)
          continue;

        bestTile = tile;
        bestDistance = static_cast<int32_t>(distance);

        // 총대장 타일 자체(distance 0)는 제외했으므로 1이 가능한 최소 거리입니다.
        // 거리 1의 합법 타일을 찾았으면 더 가까운 후보가 존재할 수 없어 즉시 종료합니다.
        if (bestDistance == 1)
          break;
      }

      if (!bestTile)
        return kPlacementFailure;

      uintptr_t tileInfo = 0;
      uint64_t result = 0;
      if (!ReadValue(bestTile, tileInfo) || !tileInfo ||
          !ReadValue(tileInfo, result))
        return CallOriginal(reinforcement);

      return result;
    }

    bool ValidateTargets(uintptr_t exeBase) {
      const uintptr_t dispatch = exeBase + kDispatchTableOffset;
      const bool dispatchKnown =
          BytesEqual(dispatch, kDispatchOriginal, sizeof(kDispatchOriginal)) ||
          BytesEqual(dispatch, kDispatchArrivalFirst,
                     sizeof(kDispatchArrivalFirst));
      if (!dispatchKnown) {
        AddLog(u8"[원군수비배치] 적용 거부: 디스패치 테이블 값 불일치 (+1D86D58)");
        return false;
      }

      struct Check {
        uintptr_t offset;
        const uint8_t *bytes;
        size_t size;
        const char *name;
      };
      const Check checks[] = {
          {kAnchorTurnOffset, kAnchorTurnOriginal,
           sizeof(kAnchorTurnOriginal), "turn"},
          {kAnchorArrivalCallOffset, kAnchorArrivalCallOriginal,
           sizeof(kAnchorArrivalCallOriginal), "arrival"},
          {kAnchorOrderCallOffset, kAnchorOrderCallOriginal,
           sizeof(kAnchorOrderCallOriginal), "order"},
          {kAnchorSpawnCallOffset, kAnchorSpawnCallOriginal,
           sizeof(kAnchorSpawnCallOriginal), "spawn"},
          {kSelectorCallOffset, kSelectorCallOriginal,
           sizeof(kSelectorCallOriginal), "selector-call"},
          {kSelectorSignatureOffset, kSelectorSignature,
           sizeof(kSelectorSignature), "selector-signature"},
      };

      for (const auto &check : checks) {
        if (!BytesEqual(exeBase + check.offset, check.bytes, check.size)) {
          AddLog(u8"[원군수비배치] 적용 거부: %s 검증 바이트 불일치 (+%llX)",
                 check.name,
                 static_cast<unsigned long long>(check.offset));
          return false;
        }
      }

      if (!IsValidPtr(exeBase + kPassabilityOffset, 1) ||
          !IsValidPtr(exeBase + kOriginalSelectorOffset, 1)) {
        AddLog(u8"[원군수비배치] 적용 거부: 게임 함수 주소 검증 실패");
        return false;
      }
      return true;
    }

    bool InstallHook(uintptr_t exeBase) {
      const uintptr_t hookAddress = exeBase + kSelectorCallOffset;
      g_originalSelector = reinterpret_cast<OriginalSelectorFn>(
          exeBase + kOriginalSelectorOffset);
      g_passability = reinterpret_cast<PassabilityFn>(
          exeBase + kPassabilityOffset);

      g_stub = AllocNear(hookAddress, 0x40);
      if (!g_stub) {
        AddLog(u8"[원군수비배치] 중계 스텁 메모리 할당 실패");
        return false;
      }

      uint8_t stub[12] = {0x48, 0xB8};
      const uintptr_t hookFunction =
          reinterpret_cast<uintptr_t>(&ReinforcementSelectorHook);
      memcpy(stub + 2, &hookFunction, sizeof(hookFunction));
      stub[10] = 0xFF;
      stub[11] = 0xE0;

      if (!WriteBytes(g_stub, stub, sizeof(stub)) ||
          !BuildRelativeCall(hookAddress, g_stub, g_selectorCallPatched) ||
          !WriteBytes(hookAddress, g_selectorCallPatched,
                      sizeof(g_selectorCallPatched))) {
        VirtualFree(reinterpret_cast<LPVOID>(g_stub), 0, MEM_RELEASE);
        g_stub = 0;
        g_originalSelector = nullptr;
        g_passability = nullptr;
        AddLog(u8"[원군수비배치] 후킹 설치 실패");
        return false;
      }

      return true;
    }
  } // namespace

  bool IsReinforcementDefenderPlacementApplied() {
    return g_applied;
  }

  bool SetReinforcementDefenderPlacement(bool enable) {
    const uintptr_t exeBase =
        reinterpret_cast<uintptr_t>(GetModuleHandle(NULL));
    if (!exeBase) {
      AddLog(u8"[원군수비배치] SAN8RPK.exe 베이스 주소를 찾지 못했습니다.");
      return false;
    }

    const uintptr_t hookAddress = exeBase + kSelectorCallOffset;

    if (enable) {
      if (g_applied &&
          BytesEqual(hookAddress, g_selectorCallPatched,
                     sizeof(g_selectorCallPatched)))
        return true;

      if (!ValidateTargets(exeBase) || !InstallHook(exeBase))
        return false;

      g_applied = true;
      AddLog(u8"[원군수비배치] 활성화 - 수비측 원군을 총대장 근처의 가장 가까운 합법 빈 타일에 배치");
      return true;
    }

    if (!g_applied)
      return true;

    if (!BytesEqual(hookAddress, g_selectorCallPatched,
                    sizeof(g_selectorCallPatched))) {
      AddLog(u8"[원군수비배치] 해제 거부: 후킹 지점에 외부 변경이 감지되었습니다.");
      return false;
    }

    if (!WriteBytes(hookAddress, kSelectorCallOriginal,
                    sizeof(kSelectorCallOriginal))) {
      AddLog(u8"[원군수비배치] 원본 호출 복구 실패");
      return false;
    }

    AddLog(u8"[원군수비배치] 해제 - 원본 배치 함수 호출 복구");

    if (g_stub) {
      VirtualFree(reinterpret_cast<LPVOID>(g_stub), 0, MEM_RELEASE);
      g_stub = 0;
    }

    g_originalSelector = nullptr;
    g_passability = nullptr;
    g_applied = false;
    return true;
  }
} // namespace DX11Base
