#include "SiegeWarfare.h"
#include "../../Cheats.h"
#include "../../MenuState.h"
#include "../../pch.h"
#include "../../showlog.h"
#include <vector>

namespace DX11Base {

  static bool g_siegeMoveCostApplied = false;
  static bool g_siegeShallowApplied = false;
  static int g_siegePrevDay = -1;

  const int SHALLOW_TERRAIN_VALUE = 10;
  const float HEAL_RATE = 0.10f;

  // ───────────────────────────────────────────────
  //  포인터 해석 헬퍼
  // ───────────────────────────────────────────────
  static uintptr_t ResolveChain(uintptr_t base, const std::vector<int> &offsets) {
    uintptr_t current = base;
    for (int offset : offsets) {
      if (!IsValidPtr(current, 8))
        return 0;
      uintptr_t next = *(uintptr_t *)current;
      if (!next)
        return 0;
      current = next + offset;
    }
    return current;
  }

  bool IsSiegeBattleActive() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t dayAddr = ResolveChain(exeBase + 0x02E99460, {0x28, 0x250, 0x218, 0, 0x3D8, 0x478, 0, 0x28});
    return (dayAddr != 0 && IsValidPtr(dayAddr, 1));
  }

  static bool ApplyMoveCostOnce() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    // resolvePointer('"SAN8RPK.exe"+034C8630', {0, 0x8, 0x10, 0, 0x4B2F18})
    uintptr_t startAddr = ResolveChain(exeBase + 0x034C8630, {0, 0x8, 0x10, 0, 0x4B2F18});
    if (!startAddr)
      return false;

    for (int i = 0; i < 12; ++i) {
      uintptr_t target = startAddr + (i * 0x28);
      if (IsValidPtr(target, 1)) {
        *(uint8_t *)target = 60;
      }
    }
    return true;
  }

  static bool ApplyShallowTerrainOnce() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);

    // castleAddr = resolvePointer('"SAN8RPK.exe"+034C8630', {0, 0x8, 0x10, 0, 0x1AA510})
    uintptr_t castleAddr = ResolveChain(exeBase + 0x034C8630, {0, 0x8, 0x10, 0, 0x1AA510});
    AddLog(u8"[DEBUG] 지형변경 시도 - castleAddr=%p", (void *)castleAddr);
    if (!castleAddr)
      return false;

    // startAddr = resolvePointer('"SAN8RPK.exe"+035100B8', {0, 0x78, 0, 0x50, 0x18, 0x1A0, 0x10})
    uintptr_t startAddr = ResolveChain(exeBase + 0x035100B8, {0, 0x78, 0, 0x50, 0x18, 0x1A0, 0x10});
    if (!startAddr)
      return false;

    // shallowAddr = resolvePointer('"SAN8RPK.exe"+034C8630', {0, 0x8, 0x10, 0, 0x1AA4B0})
    uintptr_t shallowAddrValue = ResolveChain(exeBase + 0x034C8630, {0, 0x8, 0x10, 0, 0x1AA4B0});
    if (!shallowAddrValue)
      return false;

    struct Match {
      uintptr_t addr;
      int index;
    };
    std::vector<Match> castleMatches;

    for (int i = 0; i < 600; ++i) {
      uintptr_t tableAddr = startAddr + (i * 0x40);
      if (IsValidPtr(tableAddr, 8)) {
        uintptr_t val = *(uintptr_t *)tableAddr;
        if (val == castleAddr) {
          castleMatches.push_back({tableAddr, i});
        }
      }
    }

    if (castleMatches.empty())
      return false;

    Match first = castleMatches[0];
    int firstRow = first.index / 20;

    auto writeShallow = [&](uintptr_t baseAddr, int delta) {
      uintptr_t target = baseAddr + (delta * 0x40);
      if (IsValidPtr(target, 8)) {
        *(uintptr_t *)target = shallowAddrValue;
      }
    };

    if (firstRow % 2 == 1) { // 홀수줄
      std::vector<int> firstIndices = {-21, -20, -19, -18, -17, -16, -1, 5};
      for (int idx : firstIndices)
        writeShallow(first.addr, idx);

      if (castleMatches.size() >= 23) {
        Match last = castleMatches[22]; // Lua 23rd match is index 22
        std::vector<int> lastIndices = {-5, 1, 15, 16, 17, 18, 19, 20};
        for (int idx : lastIndices)
          writeShallow(last.addr, idx);
      }
    } else { // 짝수줄
      std::vector<int> firstIndices = {-20, -19, -18, -17, -16, -15, -1, 5};
      for (int idx : firstIndices)
        writeShallow(first.addr, idx);

      if (castleMatches.size() >= 23) {
        Match last = castleMatches[22];
        std::vector<int> lastIndices = {-5, 1, 16, 17, 18, 19, 20, 21};
        for (int idx : lastIndices)
          writeShallow(last.addr, idx);
      }
    }

    // 추가 룰
    struct Rule {
      int order;
      int delta;
    };
    Rule rules[] = {{5, -1}, {9, -1}, {14, -1}, // Lua order 6, 10, 15 -> Index 5, 9, 14
                    {5, 4},  {9, 5},  {14, 4}};

    for (auto &r : rules) {
      if (castleMatches.size() > (size_t)r.order) {
        writeShallow(castleMatches[r.order].addr, r.delta);
      }
    }

    return true;
  }

  void SetSiegeWarfare(bool enable) {
    if (!enable) {
      AddLog(u8"[공성전] 기능이 비활성화되었습니다. 이동력 소모를 원상복구합니다.");
      // 비활성화 시 move cost 원상복구
      uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
      uintptr_t startAddr = ResolveChain(exeBase + 0x034C8630, {0, 0x8, 0x10, 0, 0x4B2F18});
      if (startAddr) {
        for (int i = 0; i < 12; ++i) {
          uintptr_t target = startAddr + (i * 0x28);
          if (IsValidPtr(target, 1)) {
            *(uint8_t *)target = 255;
          }
        }
      }
      g_siegeMoveCostApplied = false;
      g_siegeShallowApplied = false;
      g_siegePrevDay = -1;
    } else {
      AddLog(u8"[공성전] 기능이 활성화되었습니다.");
    }
  }

  void UpdateSiegeWarfare() {
    if (!bSiegeWarfare)
      return;

    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);

    // dayExpr = '[[[[[[[["SAN8RPK.exe"+02E99460]+28]+250]+218]+0]+3D8]+478]+0]+28'
    uintptr_t dayAddr = ResolveChain(exeBase + 0x02E99460, {0x28, 0x250, 0x218, 0, 0x3D8, 0x478, 0, 0x28});
    if (!dayAddr || !IsValidPtr(dayAddr, 1)) {
      if (g_siegePrevDay != -2) {
        AddLog(u8"[공성전] 날짜 주소를 찾을 수 없습니다. (전투 중이 아닐 수 있음)");
        g_siegePrevDay = -2;
      }
      g_siegeMoveCostApplied = false;
      g_siegeShallowApplied = false;
      return;
    }

    uint8_t dayVal = *(uint8_t *)dayAddr;
    if (dayVal < 1 || dayVal > 30) {
      g_siegePrevDay = -1;
      g_siegeMoveCostApplied = false;
      g_siegeShallowApplied = false;
      return;
    }

    if (!g_siegeMoveCostApplied) {
      if (ApplyMoveCostOnce()) {
        g_siegeMoveCostApplied = true;
        AddLog(u8"[공성전] 이동력 소모(60)가 성공적으로 적용되었습니다.");
      } else {
        AddLog(u8"[공성전] 이동력 주소를 찾을 수 없습니다.");
        return;
      }
    }

    if (!g_siegeShallowApplied) {
      if (ApplyShallowTerrainOnce()) {
        g_siegeShallowApplied = true;
        g_siegePrevDay = dayVal;
        AddLog(u8"[공성전] 성 주변 지형이 여울로 변경되었습니다.");
      } else {
        AddLog(u8"[DEBUG] 지형 변경 시도 실패 (성 주소를 찾지 못함)");
      }
      // return; // 제거: 지형 변경 시도 후에도 회복 로직이 돌 수 있도록 함
    }

    if (dayVal == g_siegePrevDay)
      return;
    g_siegePrevDay = dayVal;

    // Healing logic
    // countExpr = '[[[[[["SAN8RPK.exe"+02E99460]+28]+250]+1D8]+0]+180]-8'
    uintptr_t countAddr = ResolveChain(exeBase + 0x02E99460, {0x28, 0x250, 0x1D8, 0, 0x180, 0});
    if (!countAddr) {
      AddLog(u8"[DEBUG] countAddr 해석 실패");
      return;
    }
    countAddr -= 8;
    if (!IsValidPtr(countAddr, 1))
      return;
    uint8_t unitCount = *(uint8_t *)countAddr;
    int healedCount = 0;

    // defenderExpr = '[[[[[[[["SAN8RPK.exe"+03510578]+100]+80]+0]+8]+C8]+8]+18]+8'
    uintptr_t defenderAddr = ResolveChain(exeBase + 0x03510578, {0x100, 0x80, 0, 8, 0xC8, 8, 0x18, 8});
    if (!defenderAddr || !IsValidPtr(defenderAddr, 1)) {
      AddLog(u8"[DEBUG] defenderAddr 해석 실패");
      return;
    }
    uint8_t defenderForce = *(uint8_t *)defenderAddr;

    // unitListBase offsets: {0, 0x28, 0x250, 0x1D8, 0, 0x180}
    uintptr_t unitListBase = ResolveChain(exeBase + 0x02E99460, {0x28, 0x250, 0x1D8, 0, 0x180, 0});

    // [DEBUG] 루프 진입 전 핵심 수치 로그 (날짜 업데이트 전으로 이동)
    static int s_lastDebugDay = -1;
    if (dayVal != s_lastDebugDay) {
      AddLog(u8"[DEBUG] %d일차 회복 루틴 진입: 부대수=%d, 수비군ID=%d", (int)dayVal, (int)unitCount, (int)defenderForce);
      AddLog(u8"[DEBUG] 주소 확인: countAddr=%p, defenderAddr=%p, unitListBase=%p", (void *)countAddr,
             (void *)defenderAddr, (void *)unitListBase);
      s_lastDebugDay = dayVal;
    }

    if (!unitListBase) {
      AddLog(u8"[DEBUG] unitListBase 해석 실패");
      return;
    }

    for (int i = 0; i < unitCount; ++i) {
      uintptr_t varOffset = 0x8 + (i * 0x10);
      uintptr_t unitPtrAddr = unitListBase + varOffset;
      if (!IsValidPtr(unitPtrAddr, 8))
        continue;
      uintptr_t unitPtr = *(uintptr_t *)unitPtrAddr;
      if (!unitPtr)
        continue;

      // force: [[unitPtr + 0x18] + 0x8] + 0x18] + 0x8
      uintptr_t forceAddr = ResolveChain(unitPtr + 0x18, {0x8, 0x18, 0x8});
      // max: [[unitPtr + 0x18] + 0x0
      uintptr_t maxAddr = ResolveChain(unitPtr + 0x18, {0});
      // cur: [unitPtr + 0x38
      uintptr_t curAddr = unitPtr + 0x38;
      // terrain: [[unitPtr + 0x40] + 0x10] + 0x8
      uintptr_t terrainAddr = ResolveChain(unitPtr + 0x40, {0x10, 0x8});

      // [DEBUG] 조건문 진입 전 개별 주소 확인 (날짜가 바뀌었을 때만 앞의 3부대 출력)
      if (dayVal == s_lastDebugDay && i < 3) {
        AddLog(u8"[DEBUG] 부대[%d] 개별주소: force=%p, max=%p, cur=%p, terrain=%p", i, (void *)forceAddr,
               (void *)maxAddr, (void *)curAddr, (void *)terrainAddr);
      }

      if (IsValidPtr(forceAddr, 1) && IsValidPtr(maxAddr, 2) && IsValidPtr(curAddr, 2) && IsValidPtr(terrainAddr, 1)) {
        uint8_t forceVal = *(uint8_t *)forceAddr;
        uint16_t maxVal = *(uint16_t *)maxAddr;
        uint16_t curVal = *(uint16_t *)curAddr;
        uint8_t terrainVal = *(uint8_t *)terrainAddr;

        // 매일 첫 업데이트 시 모든 부대 상태 출력 (디버깅)
        if (dayVal != g_siegePrevDay) {
          AddLog(u8"[DEBUG] 부대[%d]: 세력=%d(수비군:%d), 지형=%d(여울:10)", 
                 i, (int)forceVal, (int)defenderForce, (int)terrainVal);
        }

        if (forceVal == defenderForce && terrainVal == SHALLOW_TERRAIN_VALUE) {
          int heal = (int)(maxVal * HEAL_RATE);
          if (heal < 1)
            heal = 1;

          int newCur = (int)curVal + heal;
          if (newCur > (int)maxVal)
            newCur = (int)maxVal;

          *(uint16_t *)curAddr = (uint16_t)newCur;
          healedCount++;
        }
      }
    }

    if (healedCount > 0) {
      AddLog(u8"[공성전] %d일차: 수비군 %d부대 체력 회복 완료.", dayVal, healedCount);
    }
  }

} // namespace DX11Base
