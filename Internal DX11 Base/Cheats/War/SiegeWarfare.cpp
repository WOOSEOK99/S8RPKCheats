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
  // HEAL_RATE는 전역 변수 iSiegeHealRate를 사용합니다.

  // ───────────────────────────────────────────────
  //  포인터 해석 헬퍼
  // ───────────────────────────────────────────────
  static uintptr_t ResolveChain(uintptr_t base, std::initializer_list<int> offsets) {
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
    // [루아 대조] dayAddr: 8단계([] 8개)
    uintptr_t p8 = ResolveChain(exeBase + 0x02E99460, {0x28, 0x250, 0x218, 0, 0x3D8, 0x478, 0, 0});
    if (!p8)
      return false;

    uintptr_t dayAddr = p8 + 0x28;
    return IsValidPtr(dayAddr, 1);
  }

  static bool ApplyMoveCostOnce() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    // resolvePointer('"SAN8RPK.exe"+034C8630', {0, 0x8, 0x10, 0, 0x4B2F18})
    uintptr_t startAddr = ResolveChain(exeBase + 0x034C8630, {0, 0x8, 0x10, 0, 0x4B2F18});
    if (!startAddr) {
      AddLog(u8"[DEBUG-ADDR] ApplyMoveCost: startAddr 실패");
      return false;
    }

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
    if (!castleAddr) {
      AddLog(u8"[DEBUG] ApplyShallow: castleAddr 실패");
      return false;
    }

    uintptr_t startAddr = ResolveChain(exeBase + 0x035100B8, {0, 0x78, 0, 0x50, 0x18, 0x1A0, 0x10});
    if (!startAddr) {
      AddLog(u8"[DEBUG] ApplyShallow: startAddr 실패");
      return false;
    }

    uintptr_t shallowAddrValue = ResolveChain(exeBase + 0x034C8630, {0, 0x8, 0x10, 0, 0x1AA4B0});
    if (!shallowAddrValue) {
      AddLog(u8"[DEBUG] ApplyShallow: shallowAddrValue 실패");
      return false;
    }

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

    if (castleMatches.empty()) {
      AddLog(u8"[DEBUG] ApplyShallow: 성 매칭 결과 없음");
      return false;
    }

    Match first = castleMatches[0];
    int firstRow = first.index / 20;

    // Lua shallowOdd / shallowEven: firstBaseAddr(castleMatches[0]) 기준 단일 적용
    auto writeShallow = [&](int delta) {
      uintptr_t target = first.addr + (delta * 0x40);
      if (IsValidPtr(target, 8)) {
        // [크래시 방지] 타일 테이블이 쓰기 보호된 경우 AV 크래시 방지용 VirtualProtect
        DWORD old, tmp;
        if (VirtualProtect((LPVOID)target, 8, PAGE_READWRITE, &old)) {
          *(uintptr_t *)target = shallowAddrValue;
          VirtualProtect((LPVOID)target, 8, old, &tmp);
        }
      }
    };

    if (firstRow % 2 == 1) { // 홀수줄 (shallowOdd)
      static const int shallowOdd[] = {-21, -20, -19, -18, -17, -16, -1,  5,   19,  24,  39,
                                       45,  59,  64,  79,  85,  99,  100, 101, 102, 103, 104};
      for (int idx : shallowOdd)
        writeShallow(idx);
    } else { // 짝수줄 (shallowEven)
      static const int shallowEven[] = {-20, -19, -18, -17, -16, -15, -1,  5,   20,  25,  39,
                                        45,  60,  65,  79,  85,  100, 101, 102, 103, 104, 105};
      for (int idx : shallowEven)
        writeShallow(idx);
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

  void UpdateSiegeWarfare(uintptr_t dayAddr, int unitCount, uint8_t defenderForce, uintptr_t unitListBase) {
    if (!bSiegeWarfare)
      return;

#if 0 // debug
    // [입구 로그] 날짜값과 내부 상태 포함 (5초 주기)
    static DWORD lastEntryLogTick1 = 0;
    DWORD nowTick1 = GetTickCount();
    if (nowTick1 - lastEntryLogTick1 > 5000) {
        uint8_t currentDay = IsValidPtr(dayAddr, 1) ? *(uint8_t*)dayAddr : 0;
        AddLog(u8"[DEBUG-ENTRY-1] 날짜:%d, 부대수:%d, 수비군:%d, 지형적용:%d, 이동력적용:%d", 
               (int)currentDay, unitCount, (int)defenderForce, (int)g_siegeShallowApplied, (int)g_siegeMoveCostApplied);
        lastEntryLogTick1 = nowTick1;
    }
#endif

    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);

    if (!dayAddr || !IsValidPtr(dayAddr, 1)) {
      if (g_siegePrevDay != -2) {
        AddLog(u8"[공성전] 날짜 주소가 유효하지 않습니다.");
        g_siegePrevDay = -2;
      }
      g_siegeMoveCostApplied = false;
      g_siegeShallowApplied = false;
      return;
    }

    uint8_t dayVal = *(uint8_t *)dayAddr;
    if (dayVal < 1 || dayVal > 30) {
      // 날짜값이 일시적으로 0이나 255(엉망)가 되더라도 지형 적용 상태를 초기화하지 않음
      // 초기화는 SetSiegeWarfare(false)에서 수행됨
      return;
    }

    static DWORD lastSiegeAttemptTick = 0;
    DWORD currentTick = GetTickCount();

    if (!g_siegeMoveCostApplied) {
      if (currentTick - lastSiegeAttemptTick >= 1000) {
        lastSiegeAttemptTick = currentTick;
        if (ApplyMoveCostOnce()) {
          g_siegeMoveCostApplied = true;
          AddLog(u8"[공성전] 이동력 소모(60)가 성공적으로 적용되었습니다.");
        } else {
          // AddLog(u8"[공성전] 이동력 주소를 찾을 수 없습니다."); // 스팸 방지를 위해 주석 처리
          return;
        }
      } else {
        return; // 아직 1초가 지나지 않았다면 대기
      }
    }

    if (!g_siegeShallowApplied) {
      if (currentTick - lastSiegeAttemptTick >= 1000) {
        lastSiegeAttemptTick = currentTick;
        if (ApplyShallowTerrainOnce()) {
          g_siegeShallowApplied = true;
          // g_siegePrevDay = dayVal; // 여기서 설정하면 당일 회복 루틴이 스킵되므로 제거
          AddLog(u8"[공성전] 성 주변 지형이 여울로 변경되었습니다.");
        } else {
          // AddLog(u8"[DEBUG] 지형 변경 시도 실패 (성 주소를 찾지 못함)"); // 스팸 방지
          return;
        }
      } else {
        return;
      }
    }

    if (dayVal == g_siegePrevDay)
      return;
    g_siegePrevDay = dayVal;

    int healedCount = 0;

    // [DEBUG] 루프 진입 전 핵심 수치 로그 (날짜 업데이트 전으로 이동)
    static int s_lastDebugDay = -1;
    if (dayVal != s_lastDebugDay) {
      AddLog(u8"[DEBUG] %d일차 회복 루틴 진입: 부대수=%d, 수비군ID=%d", (int)dayVal, (int)unitCount,
             (int)defenderForce);
      s_lastDebugDay = dayVal;
    }

    if (!unitListBase)
      return;

    for (int i = 0; i < unitCount; ++i) {
      uintptr_t varOffset = 0x8 + (i * 0x10);
      uintptr_t unitPtrAddr = unitListBase + varOffset;
      if (!IsValidPtr(unitPtrAddr, 8))
        continue;
      uintptr_t unitPtr = *(uintptr_t *)unitPtrAddr;
      if (!unitPtr)
        continue;

      // force: [[[unitPtr + 0x18] + 0x8] + 0x18] + 0x8
      uintptr_t forceAddr = ResolveChain(unitPtr + 0x18, {0x8, 0x18, 0x8});
      // max: [[unitPtr + 0x18] + 0x0
      uintptr_t maxAddr = ResolveChain(unitPtr + 0x18, {0});
      // cur: [unitPtr + 0x38]
      uintptr_t curAddr = unitPtr + 0x38;
      // terrain: [[unitPtr + 0x40] + 0x10] + 0x8
      uintptr_t terrainAddr = ResolveChain(unitPtr + 0x40, {0x10, 0x8});

      // [DEBUG] 조건문 진입 전 개별 주소 확인 (필요 시 주석 해제)
      /*
      if (dayVal == s_lastDebugDay && i < 3) {
        AddLog(u8"[DEBUG] 부대[%d] 개별주소: force=%p, max=%p, cur=%p, terrain=%p", i, (void *)forceAddr,
               (void *)maxAddr, (void *)curAddr, (void *)terrainAddr);
      }
      */

      if (IsValidPtr(forceAddr, 1) && IsValidPtr(maxAddr, 2) && IsValidPtr(curAddr, 2) && IsValidPtr(terrainAddr, 1)) {
        uint8_t forceVal = *(uint8_t *)forceAddr;
        uint16_t maxVal = *(uint16_t *)maxAddr;
        uint16_t curVal = *(uint16_t *)curAddr;
        uint8_t terrainVal = *(uint8_t *)terrainAddr;

        if (forceVal == defenderForce && terrainVal == SHALLOW_TERRAIN_VALUE && curVal < maxVal) {
          int heal = (int)(maxVal * ((float)iSiegeHealRate / 100.0f));
          if (heal < 1)
            heal = 1;

          int newCur = (int)curVal + heal;
          if (newCur > (int)maxVal)
            newCur = (int)maxVal;

          if (curVal != (uint16_t)newCur) {
            *(uint16_t *)curAddr = (uint16_t)newCur;
            healedCount++;
          }
        } else {
          // [DEBUG] 조건 불일치 시 로그 (첫 3부대만, 5초 주기)
          static DWORD lastMismatchLog = 0;
          if (i < 3 && GetTickCount() - lastMismatchLog > 5000) {
            // terrainVal이 10이 아니거나 force가 다를 때
            if (terrainVal == SHALLOW_TERRAIN_VALUE || forceVal == defenderForce) {
              AddLog(u8"[DEBUG-MISMATCH] 부대[%d] 세력:%d(수비:%d), 지형:%d(목표:%d)", i, (int)forceVal,
                     (int)defenderForce, (int)terrainVal, SHALLOW_TERRAIN_VALUE);
              lastMismatchLog = GetTickCount();
            }
          }
        }
      }
    }

    if (healedCount > 0) {
      AddLog(u8"[공성전] %d일차: 수비군 %d부대 체력 회복 완료.", dayVal, healedCount);
    }
  }

  // ─────────────────────────────────────────────────
  //  성 주변 2칸 여울 변형
  // ─────────────────────────────────────────────────
  static bool g_siege2MoveCostApplied = false;
  static bool g_siege2ShallowApplied = false;
  static int g_siege2PrevDay = -1;

  void ResetSiegeBattleRuntime() {
    // A load abandons the previous battle; never restore through its pointers.
    g_siegeMoveCostApplied = false;
    g_siegeShallowApplied = false;
    g_siegePrevDay = -1;
    g_siege2MoveCostApplied = false;
    g_siege2ShallowApplied = false;
    g_siege2PrevDay = -1;
  }

  static bool ApplyShallowTerrain2Once() {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);

    uintptr_t castleAddr = ResolveChain(exeBase + 0x034C8630, {0, 0x8, 0x10, 0, 0x1AA510});
    if (!castleAddr) {
      AddLog(u8"[DEBUG] ApplyShallow2: castleAddr 실패");
      return false;
    }

    uintptr_t startAddr = ResolveChain(exeBase + 0x035100B8, {0, 0x78, 0, 0x50, 0x18, 0x1A0, 0x10});
    if (!startAddr) {
      AddLog(u8"[DEBUG] ApplyShallow2: startAddr 실패");
      return false;
    }

    uintptr_t shallowAddrValue = ResolveChain(exeBase + 0x034C8630, {0, 0x8, 0x10, 0, 0x1AA4B0});
    if (!shallowAddrValue) {
      AddLog(u8"[DEBUG] ApplyShallow2: shallowAddrValue 실패");
      return false;
    }

    struct Match {
      uintptr_t addr;
      int index;
    };
    std::vector<Match> castleMatches;

    for (int i = 0; i < 600; ++i) {
      uintptr_t tableAddr = startAddr + (i * 0x40);
      if (IsValidPtr(tableAddr, 8)) {
        uintptr_t val = *(uintptr_t *)tableAddr;
        if (val == castleAddr)
          castleMatches.push_back({tableAddr, i});
      }
    }

    if (castleMatches.empty()) {
      AddLog(u8"[DEBUG] ApplyShallow2: 성 매칭 결과 없음");
      return false;
    }

    Match first = castleMatches[0];
    int firstRow = first.index / 20;

    auto writeShallow = [&](uintptr_t baseAddr, int delta) {
      uintptr_t target = baseAddr + (delta * 0x40);
      if (IsValidPtr(target, 8)) {
        // [크래시 방지] 타일 테이블이 쓰기 보호된 경우 AV 크래시 방지용 VirtualProtect
        DWORD old, tmp;
        if (VirtualProtect((LPVOID)target, 8, PAGE_READWRITE, &old)) {
          *(uintptr_t *)target = shallowAddrValue;
          VirtualProtect((LPVOID)target, 8, old, &tmp);
        }
      }
    };

    // Lua shallowOdd / shallowEven 배열을 firstBaseAddr 기준으로 전부 적용
    // (castleMatches[0].addr 하나를 기준으로 모든 delta 적용)
    if (firstRow % 2 == 1) { // 홀수줄 (shallowOdd)
      static const int shallowOdd[] = {
          -41, -40, -39, -38, -37, -36, -35,      // 최외곽 상단
          -22, -21, -20, -19, -18, -17, -16, -15, // 외곽 상단
          -2,  -1,  5,   6,   18,  19,  24,  25,  // 성 좌우 인접
          38,  39,  45,  46,  58,  59,  64,  65,  // 중단 좌우
          78,  79,  85,  86,                      // 하단 좌우
          98,  99,  100, 101, 102, 103, 104, 105, // 외곽 하단
          119, 120, 121, 122, 123, 124, 125       // 최외곽 하단
      };
      for (int idx : shallowOdd)
        writeShallow(first.addr, idx);
    } else { // 짝수줄 (shallowEven)
      static const int shallowEven[] = {
          -41, -40, -39, -38, -37, -36, -35,      // 최외곽 상단
          -21, -20, -19, -18, -17, -16, -15, -14, // 외곽 상단
          -2,  -1,  5,   6,   19,  20,  25,  26,  // 성 좌우 인접
          38,  39,  45,  46,  59,  60,  65,  66,  // 중단 좌우
          78,  79,  85,  86,                      // 하단 좌우
          99,  100, 101, 102, 103, 104, 105, 106, // 외곽 하단
          119, 120, 121, 122, 123, 124, 125       // 최외곽 하단
      };
      for (int idx : shallowEven)
        writeShallow(first.addr, idx);
    }

    return true;
  }

  void SetSiegeWarfare2(bool enable) {
    if (!enable) {
      AddLog(u8"[2칸 공성전] 비활성화 - 이동력 소모 복구");
      uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
      uintptr_t startAddr = ResolveChain(exeBase + 0x034C8630, {0, 0x8, 0x10, 0, 0x4B2F18});
      if (startAddr) {
        for (int i = 0; i < 12; ++i) {
          uintptr_t target = startAddr + (i * 0x28);
          if (IsValidPtr(target, 1))
            *(uint8_t *)target = 255;
        }
      }
      g_siege2MoveCostApplied = false;
      g_siege2ShallowApplied = false;
      g_siege2PrevDay = -1;
    } else {
      AddLog(u8"[2칸 공성전] 활성화");
    }
  }

  void UpdateSiegeWarfare2(uintptr_t dayAddr, int unitCount, uint8_t defenderForce, uintptr_t unitListBase) {
    if (!bSiegeWarfare2)
      return;

#if 0 // debug
    // [입구 로그] 날짜값과 내부 상태 포함 (5초 주기)
    static DWORD lastEntryLogTick2 = 0;
    DWORD nowTick2 = GetTickCount();
    if (nowTick2 - lastEntryLogTick2 > 5000) {
        uint8_t currentDay = IsValidPtr(dayAddr, 1) ? *(uint8_t*)dayAddr : 0;
        AddLog(u8"[DEBUG-ENTRY-2] 날짜:%d, 부대수:%d, 수비군:%d, 지형적용:%d, 이동력적용:%d", 
               (int)currentDay, unitCount, (int)defenderForce, (int)g_siege2ShallowApplied, (int)g_siege2MoveCostApplied);
        lastEntryLogTick2 = nowTick2;
    }
#endif

    if (!dayAddr || !IsValidPtr(dayAddr, 1)) {
      g_siege2MoveCostApplied = false;
      g_siege2ShallowApplied = false;
      return;
    }

    uint8_t dayVal = *(uint8_t *)dayAddr;
    if (dayVal < 1 || dayVal > 30) {
      // 날짜값이 불안정해도 지형 적용 상태를 유지 (SetSiegeWarfare2에서 초기화 관리)
      return;
    }

    static DWORD lastSiege2AttemptTick = 0;
    DWORD currentTick2 = GetTickCount();

    if (!g_siege2MoveCostApplied) {
      if (currentTick2 - lastSiege2AttemptTick >= 1000) {
        lastSiege2AttemptTick = currentTick2;
        if (ApplyMoveCostOnce()) {
          g_siege2MoveCostApplied = true;
          AddLog(u8"[2칸 공성전] 이동력 소모(60) 적용 완료");
        } else
          return;
      } else {
        return;
      }
    }

    if (!g_siege2ShallowApplied) {
      if (currentTick2 - lastSiege2AttemptTick >= 1000) {
        lastSiege2AttemptTick = currentTick2;
        if (ApplyShallowTerrain2Once()) {
          g_siege2ShallowApplied = true;
          // g_siege2PrevDay        = dayVal; // 당일 회복을 위해 제거
          AddLog(u8"[2칸 공성전] 성 주변 2칸 지형이 여울로 변경되었습니다.");
        } else
          return;
      } else {
        return;
      }
    }

    if (dayVal == g_siege2PrevDay)
      return;
    g_siege2PrevDay = dayVal;

    if (!unitListBase)
      return;

    int healedCount = 0;
    for (int i = 0; i < unitCount; ++i) {
      uintptr_t unitPtrAddr = unitListBase + 0x8 + (i * 0x10);
      if (!IsValidPtr(unitPtrAddr, 8))
        continue;
      uintptr_t unitPtr = *(uintptr_t *)unitPtrAddr;
      if (!unitPtr)
        continue;

      uintptr_t forceAddr = ResolveChain(unitPtr + 0x18, {0x8, 0x18, 0x8});
      uintptr_t maxAddr = ResolveChain(unitPtr + 0x18, {0});
      uintptr_t curAddr = unitPtr + 0x38;
      uintptr_t terrainAddr = ResolveChain(unitPtr + 0x40, {0x10, 0x8});

      if (IsValidPtr(forceAddr, 1) && IsValidPtr(maxAddr, 2) && IsValidPtr(curAddr, 2) && IsValidPtr(terrainAddr, 1)) {
        uint8_t forceVal = *(uint8_t *)forceAddr;
        uint16_t maxVal = *(uint16_t *)maxAddr;
        uint16_t curVal = *(uint16_t *)curAddr;
        uint8_t terrainVal = *(uint8_t *)terrainAddr;

        if (forceVal == defenderForce && terrainVal == SHALLOW_TERRAIN_VALUE && curVal < maxVal) {
          int heal = (int)(maxVal * ((float)iSiegeHealRate / 100.0f));
          if (heal < 1)
            heal = 1;
          int newCur = (int)curVal + heal;
          if (newCur > (int)maxVal)
            newCur = (int)maxVal;

          if (curVal != (uint16_t)newCur) {
            *(uint16_t *)curAddr = (uint16_t)newCur;
            ++healedCount;
          }
        }
      }
    }

    if (healedCount > 0)
      AddLog(u8"[2칸 공성전] %d일차: 수비군 %d부대 체력 회복 완료.", dayVal, healedCount);
  }

} // namespace DX11Base
