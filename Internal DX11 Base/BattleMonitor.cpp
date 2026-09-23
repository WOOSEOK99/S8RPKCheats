#include "BattleMonitor.h"
#include "Cheats.h"
#include "Cheats/Officer/SelectOfficercapture.h"
#include "Cheats/Officer/StatMonitor.h"
#include "Cheats/Officer/SpecialAbilityAutoAssign.h"
#include "Cheats/Officer/AIOfficerGrowth.h"
#include "Cheats/System/MonthCapture.h"
#include "Cheats/System/SkillCountManager.h"
#include "Cheats/System/SystemMonth.h"
#include "Cheats/System/TengiCave.h"
#include "Cheats/Civilian/CityInfoWindow.h"
#include "Cheats/War/BattleEnvironment.h"
#include "Cheats/War/BattleMapShuffle.h"
#include "Cheats/War/Battleunitcapture.h"
#include "Cheats/War/Catapult.h"
#include "Cheats/War/Celestia.h"
#include "Cheats/War/Defbuildingboost.h"
#include "Cheats/War/Dongto.h"
#include "Cheats/War/Selfheal.h"
#include "Cheats/War/SiegeWarfare.h"
#include "Cheats/War/SpecialAbility.h"
#include "Cheats/War/Spell5HealProbe.h"
#include "Cheats/War/StratagemSlotProbe.h"
#include "Cheats/War/StratagemGaugeMax.h"
#include "Cheats/War/Terrainignore.h"
#include "MenuState.h"
#include "pch.h"
#include "NotificationManager.h"
#include "debug.h"
#include "showlog.h"

namespace DX11Base {
  // 전역 캐시 주소 (모든 함수에서 공유)
  static uintptr_t s_cachedUnitListBase = 0;
  static uintptr_t s_cachedDayBaseAddr = 0;
  static uintptr_t s_cachedDefenderAddr = 0;
  static DWORD s_lastResolveTick = 0;

  struct ResolveRegionEntry {
    uintptr_t start = 0;
    uintptr_t end = 0; // exclusive
    bool readable = false;
  };

  struct ResolveRegionCache {
    static constexpr size_t kCapacity = 16;
    ResolveRegionEntry entries[kCapacity]{};
    size_t next = 0;
  };

  // 한 번의 전투 포인터 갱신 안에서 같은 VirtualQuery 영역의 결과를 재사용합니다.
  // 캐시는 호출마다 새로 만들어지므로 다음 500ms 갱신까지 오래된 메모리 상태를 유지하지 않습니다.
  static bool IsValidPtrForResolve(uintptr_t addr, SIZE_T size, ResolveRegionCache &cache) {
    if (!addr || size == 0)
      return false;

    const uintptr_t endAddr = addr + size - 1;
    if (endAddr < addr)
      return false;

    for (const auto &entry : cache.entries) {
      if (entry.start != 0 && addr >= entry.start && endAddr < entry.end)
        return entry.readable;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi))
      return false;

    const uintptr_t regionStart = (uintptr_t)mbi.BaseAddress;
    const uintptr_t regionEnd = regionStart + mbi.RegionSize;
    const bool regionEndValid = (regionEnd >= regionStart);
    const bool readable = regionEndValid && mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));

    ResolveRegionEntry &slot = cache.entries[cache.next++ % ResolveRegionCache::kCapacity];
    slot.start = regionStart;
    slot.end = regionEndValid ? regionEnd : regionStart;
    slot.readable = readable;

    if (!readable)
      return false;
    if (endAddr < regionEnd)
      return true;

    // 드문 영역 경계 횡단은 기존 검사로 폴백하여 검증 의미를 유지합니다.
    return IsValidPtr(addr, size);
  }

  static bool ReadPtrForResolve(uintptr_t addr, uintptr_t &out, ResolveRegionCache &cache) {
    out = 0;
    if (addr < 0x10000 || !IsValidPtrForResolve(addr, sizeof(uintptr_t), cache))
      return false;

    __try {
      out = *(uintptr_t *)addr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      out = 0;
      return false;
    }
    return out >= 0x10000;
  }

  // 프리징 방지를 위한 SEH(예외 처리) + 호출 단위 메모리 영역 캐시 기반 포인터 체인 추적
  static uintptr_t ResolveChainCached(uintptr_t base, std::initializer_list<int> offsets, ResolveRegionCache &cache) {
    if (base == 0)
      return 0;
    uintptr_t current = base;

    for (int offset : offsets) {
      uintptr_t next = 0;
      if (!ReadPtrForResolve(current, next, cache))
        return 0;
      current = next + offset;
    }
    return current;
  }

  // 단독 호출용 폴백. 기존 호출부의 동작은 유지하면서 동일한 검증 경로를 사용합니다.
  static uintptr_t ResolveChain(uintptr_t base, std::initializer_list<int> offsets) {
    ResolveRegionCache cache{};
    return ResolveChainCached(base, offsets, cache);
  }

  // unitList/day는 같은 0x02E99460 루트와 첫 전투 데이터 포인터를 공유합니다.
  // 500ms 갱신 주기 자체는 유지하되, 공통 포인터는 한 번만 읽고 같은 메모리 영역의 VirtualQuery 결과를 재사용합니다.
  static void ResolveBattlePointers(uintptr_t exeBase, bool needDefender) {
    ResolveRegionCache cache{};

    uintptr_t sharedBattleRoot = ResolveChainCached(exeBase + 0x02E99460, {0x28, 0x250}, cache);
    uintptr_t battleDataRoot = 0;
    if (sharedBattleRoot && ReadPtrForResolve(sharedBattleRoot, battleDataRoot, cache)) {
      s_cachedUnitListBase = ResolveChainCached(battleDataRoot + 0x1D8, {0, 0x180, 0}, cache);
      s_cachedDayBaseAddr = ResolveChainCached(battleDataRoot + 0x218, {0, 0x3D8, 0x478, 0, 0}, cache);
    } else {
      s_cachedUnitListBase = 0;
      s_cachedDayBaseAddr = 0;
    }

    // 수비군 주소는 공성 기능에서만 사용하므로 기능이 꺼져 있으면 8단계 체인을 해석하지 않습니다.
    if (needDefender) {
      s_cachedDefenderAddr = ResolveChainCached(exeBase + 0x03510578, {0x100, 0x80, 0, 8, 0xC8, 8, 0x18, 8}, cache);
    } else {
      s_cachedDefenderAddr = 0;
    }
  }

  // 전투 유닛들의 전법 횟수를 커스텀 설정값으로 덮어씁니다.
  void UpdateBattleUnitSkills(bool silent = false, uintptr_t cachedUnitListBase = 0) {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return;

    // MonitorBattleStatus에서 이미 해석한 주소가 있으면 재사용하고, 없을 때만 폴백 해석합니다.
    uintptr_t unitListBase = cachedUnitListBase;
    if (!unitListBase)
      unitListBase = ResolveChain(exeBase + 0x02E99460, {0x28, 0x250, 0x1D8, 0, 0x180, 0});

    if (!unitListBase) {
      if (!silent)
        AddLog(u8"[DEBUG] ApplySkill: unitBase를 찾을 수 없음");
      return;
    }

    uintptr_t countAddr = unitListBase - 0x08;
    if (!IsValidPtr(countAddr, 1))
      return;
    int unitCountTotal = *(unsigned char *)countAddr;
    if (unitCountTotal <= 0 || unitCountTotal > 60) {
      return;
    }

    if (!silent)
      AddLog(u8"[DEBUG] ApplySkill: 유닛 총 %d개 감지됨", unitCountTotal);

    for (int i = 0; i < unitCountTotal; i++) {
      uintptr_t unitData = *(uintptr_t *)(unitListBase + 0x08 + i * 0x10);
      if (!IsValidPtr(unitData, 0x600))
        continue;

      uintptr_t memberPtr = *(uintptr_t *)(unitData + 0x18);
      if (!IsValidPtr(memberPtr, 0x100))
        continue;

      // 무장 ID 읽기 (Lua: readOfficerId, readSmallInteger(ptr+0x08))
      int officerID = -1;
      uintptr_t moffs[] = {0x08, 0x10, 0x18};
      for (int k = 0; k < 3; k++) {
        uintptr_t offPtr = *(uintptr_t *)(memberPtr + moffs[k]);
        if (IsValidPtr(offPtr, 0x10)) {
          officerID = (int)(*(unsigned short *)(offPtr + 0x08));
          break;
        }
      }
      if (officerID <= 0)
        continue;

      // 이 무장에 커스텀 설정이 있는지 확인
      {
        std::lock_guard<std::mutex> lock(g_skillCountMutex);
        if (!g_customSkillCounts.count(officerID))
          continue;
      }

      // skillStart 읽기 (Lua: readPointer(unitData + SKILL_PTR_OFF), SKILL_PTR_OFF=0x5D8)
      uintptr_t skillStart = *(uintptr_t *)(unitData + 0x5D8);
      if (!IsValidPtr(skillStart, 0x28))
        continue;

      // 스킬 레코드 순회 (Lua: for j=0,MAX_SKILLS-1)
      for (int j = 0; j < 45; j++) {
        uintptr_t rec = skillStart + j * 0x28;
        if (!IsValidPtr(rec, 0x20))
          break;

        // 유효성 검증 (Lua: isValidSkillRec - idx 순차 확인 + 첫 바이트 0x58/0xD8)
        int idx = *(int *)(rec + 0x0C);
        if (idx != j)
          break;
        unsigned char b = *(unsigned char *)rec;
        if (b != 0x58 && b != 0xD8)
          break;

        // 스킬 ID 읽기 (Lua: skillCore=readPointer(rec+0x00), readBytes(skillCore+0x08,1))
        uintptr_t skillCore = *(uintptr_t *)rec;
        if (!IsValidPtr(skillCore, 0x10))
          continue;
        int skillId = (int)(*(unsigned char *)(skillCore + 0x08));
        if (skillId <= 0)
          continue;

        // UI 오프셋 계산: skillId + 0x138 (예: 분기=26 -> 26+0x138=0x152 ✓)
        uintptr_t targetOffset = (uintptr_t)skillId + 0x138;

        std::lock_guard<std::mutex> lock(g_skillCountMutex);
        if (!g_customSkillCounts.count(officerID))
          break;
        if (!g_customSkillCounts[officerID].count(targetOffset))
          continue;

        int targetVal = g_customSkillCounts[officerID][targetOffset];
        if (targetVal <= 0)
          continue;

        // 횟수 쓰기 (Lua: writeBytes(rec + SKILL_COUNT_OFF, SET_COUNT), SKILL_COUNT_OFF=0x10)
        unsigned char oldVal = *(unsigned char *)(rec + 0x10);
        DWORD oldP;
        if (VirtualProtect((LPVOID)(rec + 0x10), 1, PAGE_EXECUTE_READWRITE, &oldP)) {
          *(unsigned char *)(rec + 0x10) = (unsigned char)targetVal;
          VirtualProtect((LPVOID)(rec + 0x10), 1, oldP, &oldP);
          unsigned char newVal = *(unsigned char *)(rec + 0x10);
          if (!silent)
            AddLog(u8"[DEBUG] 무장[%d] 스킬ID:%d (Offset:0x%llX) 적용:%d (기존:%d 확인:%d)", officerID, skillId,
                   targetOffset, targetVal, (int)oldVal, (int)newVal);
        }
      }
    }
  }

  // [C2712 오류 해결용 헬퍼] __try가 있는 함수에서 std::string 객체 생성을 피하기 위해 분리
  static void NotifyBattleDay(int remain) {
      char nbuf[128];
      sprintf_s(nbuf, u8"남은 전투 일자 : %d 일", remain);
      AddNotification(std::string(nbuf));
  }

  void MonitorBattleStatus() {
    static bool s_isWarModsApplied = false;
    static bool s_isCacheBuilt = false;
    static float s_lastSeenTime = 0.0f;
    static int s_lastAppliedDay = -1;
    static int s_lastNotifiedDay = -1;

    // 디버그 모드 전용 전투 상태 변화 스냅샷.
    // 디버그를 다시 켤 때 현재 상태를 즉시 한 번 출력하도록 OFF 시 초기화합니다.
    static bool s_debugSnapshotValid = false;
    static uint8_t s_debugLastGameState = 0xFF;
    static int s_debugLastDay = -999;
    static int s_debugLastUnits = -999;
    static bool s_debugLastBattleActive = false;
    static bool s_debugLastApplied = false;
    if (!bShowDebug)
      s_debugSnapshotValid = false;

    float currentTime = (float)GetTickCount64() / 1000.0f;

    // 0. 기반 주소 체크 (게임 로딩/메뉴 시 자동 초기화)
    uintptr_t gameBase = DX11Base::GetGameBase();
    if (gameBase == 0) {
      if (s_isWarModsApplied) {
        s_isWarModsApplied = false;
        s_isCacheBuilt = false;
        s_lastSeenTime = 0;
        s_lastAppliedDay = -1;
        DX11Base::g_battleUnitAddr1 = 0;
        DX11Base::g_battleUnitAddr2 = 0;
      }
      return;
    }

    // 1. 현재 캡처된 주소 확인
    uintptr_t addr1 = DX11Base::g_battleUnitAddr1;
    uintptr_t addr2 = DX11Base::g_battleUnitAddr2;

    // 2. 체인 주소 500ms 갱신 캐시
    DWORD currentTick = GetTickCount();
    static bool s_initialResolveDone = false;
    const bool needDefender = (bSiegeWarfare || bSiegeWarfare2);

    if (currentTick - s_lastResolveTick >= 500 || !s_initialResolveDone) {
      s_lastResolveTick = currentTick;
      s_initialResolveDone = true;
      uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
      ResolveBattlePointers(exeBase, needDefender);
    }

    uintptr_t unitListBase = s_cachedUnitListBase;
    uintptr_t dayBaseAddr = s_cachedDayBaseAddr;
    uintptr_t dayAddr = dayBaseAddr ? (dayBaseAddr + 0x28) : 0;

    // 날짜와 부대 수는 한 틱에서 한 번만 검증/읽고 이후 로직에서 재사용합니다.
    int currentDay = -1;
    bool isDateValid = false;
    __try {
      if (dayAddr > 0x10000 && IsValidPtr(dayAddr, 1)) {
        currentDay = (int)(*(unsigned char *)dayAddr);
        isDateValid = (currentDay > 0 && currentDay <= 30);
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      currentDay = -1;
      isDateValid = false;
    }

    int unitCountTotal = 0;
    bool isUnitListValid = false;
    __try {
      if (unitListBase > 0x10000 && IsValidPtr(unitListBase - 0x08, 1)) {
        unitCountTotal = *(unsigned char *)(unitListBase - 0x08);
        isUnitListValid = (unitCountTotal > 0 && unitCountTotal <= 60);
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      unitCountTotal = 0;
      isUnitListValid = false;
    }

    // 전투 활성화 조건: 훅 포착 OR 올바른 날짜 유효 OR 올바른 부대 리스트 유효
    bool battleActive = (addr1 != 0 || addr2 != 0) || isDateValid || isUnitListValid;

    if (bShowDebug) {
      uint8_t gameState = 0xFF;
      __try {
        if (IsValidPtr(gameBase + 0xD0, 1))
          gameState = *(uint8_t *)(gameBase + 0xD0);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        gameState = 0xFF;
      }

      const bool changed = !s_debugSnapshotValid || gameState != s_debugLastGameState || currentDay != s_debugLastDay ||
                           unitCountTotal != s_debugLastUnits || battleActive != s_debugLastBattleActive ||
                           s_isWarModsApplied != s_debugLastApplied;
      if (changed) {
        AddLog(u8"[BattleDebug] State:0x%02X Day:%d Units:%d Addr1:%p Addr2:%p Active:%d Applied:%d",
               (unsigned int)gameState, currentDay, unitCountTotal, (void *)addr1, (void *)addr2,
               battleActive ? 1 : 0, s_isWarModsApplied ? 1 : 0);
        s_debugLastGameState = gameState;
        s_debugLastDay = currentDay;
        s_debugLastUnits = unitCountTotal;
        s_debugLastBattleActive = battleActive;
        s_debugLastApplied = s_isWarModsApplied;
        s_debugSnapshotValid = true;
      }
    }

    if (battleActive) {
      s_lastSeenTime = currentTime;
      uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);

      // 아직 리프레시를 안 했다면 실행
      // [크래시 방지] 포진 화면 오탐 방지: unitCountTotal > 0 AND currentDay가 유효(1~30)해야
      // 실제 전투 진입으로 판단. 둘 중 하나만 준비된 시점(포진 중)에는 리프레시 보류.
      if (!s_isWarModsApplied && unitCountTotal > 0 && currentDay >= 1 && currentDay <= 30) {
        AddLog(u8"[자동화] 전투 감지(%llX) -> 모든 전쟁 모드 리프레시 (부대:%d, 현재일:%d)", addr1, unitCountTotal, currentDay);

        __try {
          // 켜져 있는 기능들에 대해 원본 복구 후 다시 적용 (Refresh)
          if (bSelfHeal) {
            DX11Base::SetSelfHeal(false);
            DX11Base::SetSelfHeal(true);
          }
          if (bDongto) {
            DX11Base::SetDongto(false);
            DX11Base::SetDongto(true);
          }
          if (bTerrainIgnore) {
            DX11Base::SetTerrainIgnore(false);
            DX11Base::SetTerrainIgnore(true);
          }
          if (bDefBuilding) {
            DX11Base::SetDefBuildingBoost(false);
            DX11Base::SetDefBuildingBoost(true);
          }
          if (bCatapult) {
            DX11Base::SetCatapultCheat(false);
            DX11Base::SetCatapultCheat(true);
          }
          if (bCelestial) {
            DX11Base::SetCelestialMod(false);
            DX11Base::SetCelestialMod(true);
          }
          if (bSiegeWarfare) {
            DX11Base::SetSiegeWarfare(false);
            DX11Base::SetSiegeWarfare(true);
          }
          else if (bSiegeWarfare2) {
            DX11Base::SetSiegeWarfare2(false);
            DX11Base::SetSiegeWarfare2(true);
          }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        // 전투 진입 즉시 환경(날짜 등) 업데이트 실행하여 알림에 정확한 데이터 반영
        __try {
            DX11Base::UpdateBattleEnvironment(exeBase, dayBaseAddr, unitListBase, true);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        s_isWarModsApplied = true;
        // 환경 변수 캐싱
        __try {
          DX11Base::InitBattleEnvCache(exeBase);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        s_lastAppliedDay = currentDay;
      }

      // 부대가 스폰된 시점에 1회 한정으로 캐시 빌드 및 커스텀 전법 횟수 주입
      if (!s_isCacheBuilt && unitCountTotal > 0) {
          __try {
            DX11Base::InitializeBattleCache((int)unitCountTotal, unitListBase, exeBase);
            UpdateBattleUnitSkills(false, unitListBase);
            s_isCacheBuilt = true;
          } __except (EXCEPTION_EXECUTE_HANDLER) {}
      }

      // 1일차가 시작될 때 한 번 더 적용 (포진 등이 끝나고 실제 전투 시작 시 초기화 대응)
      if (currentDay == 1 && s_lastAppliedDay != 1) {
        AddLog(u8"[자동화] 1일차 감지 -> 전법 횟수 재주입");
        UpdateBattleUnitSkills(true, unitListBase);
        s_lastAppliedDay = 1;
      }
      if (currentDay != -1) {
        s_lastAppliedDay = currentDay;
      }

      // [특수 기능 실시간 체크] 매 틱(100ms) 실행
      if (unitListBase > 0x10000) {
          __try {
            UpdateSpecialAbilities(unitCountTotal, unitListBase, exeBase);
          } __except (EXCEPTION_EXECUTE_HANDLER) {}

          // 5번 책략 대상 진단: 읽기 전용으로 전의 변화를 추적합니다.
          __try {
            UpdateSpell5TargetDiagnostics((int)unitCountTotal, unitListBase);
          } __except (EXCEPTION_EXECUTE_HANDLER) {}
      }

      // 5번 책략 데이터는 세이브/게임 세대가 바뀌면 새 TrickData 테이블에 재적용합니다.
      __try {
        DX11Base::RefreshSpell5HealProbe();
      } __except (EXCEPTION_EXECUTE_HANDLER) {}

      // 5번 책략: 체크 의도는 유지하되 현재 전투 세대의 포인터에만 붙입니다.
      __try {
        DX11Base::RefreshStratagemFiveBattleRuntime();
      } __except (EXCEPTION_EXECUTE_HANDLER) {}

      // 5번 책략 UI: ResetBtnPos 훅이 잡은 live layout을 한 번만 읽기 진단합니다.
      __try {
        DX11Base::UpdateStratagemFiveUiRuntimeProbe();
      } __except (EXCEPTION_EXECUTE_HANDLER) {}

      // 책략 게이지 테스트: 캡처된 진영 객체가 유효할 때만 +0x154를 10000으로 유지합니다.
      if (bMaxAttackStratagemGauge || bMaxDefenseStratagemGauge) {
        __try {
          DX11Base::UpdateStratagemGaugeMax();
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
      }

      // [환경/공성전] 2-phase 분산 (100ms 틱 기준)
      static int s_tickPhase = 0;
      if (s_tickPhase == 0) {
        if (unitListBase > 0x10000 && dayBaseAddr > 0x10000) {
            __try {
              DX11Base::UpdateBattleEnvironment(exeBase, dayBaseAddr, unitListBase);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
      } else if (s_tickPhase == 1) {
        // 공성 기능이 켜졌을 때만 defender 주소를 읽고 공성 업데이트를 호출합니다.
        if (needDefender && unitListBase > 0x10000 && dayBaseAddr > 0x10000) {
            uint8_t defenderForce = 0;
            __try {
              uintptr_t defenderAddr = s_cachedDefenderAddr;
              if (defenderAddr > 0x10000 && IsValidPtr(defenderAddr, 1)) {
                defenderForce = *(uint8_t *)defenderAddr;
              }

              if (bSiegeWarfare)
                DX11Base::UpdateSiegeWarfare(dayAddr, unitCountTotal, defenderForce, unitListBase);
              if (bSiegeWarfare2)
                DX11Base::UpdateSiegeWarfare2(dayAddr, unitCountTotal, defenderForce, unitListBase);
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
      }
      s_tickPhase = (s_tickPhase + 1) % 2;

      // 날짜 변경 시 알림 팝업 출력
      if (currentDay > 0 && currentDay != s_lastNotifiedDay) {
          // 유동 날짜 기능 사용 시 부대 정보가 아직 로드되지 않았으면 다음 틱으로 미룸
          if (bDateDynamic && !isUnitListValid) {
              // 부대 데이터가 로드될 때까지 알림을 보류합니다.
          } else {
              DX11Base::UpdateBattleEnvironment(exeBase, dayBaseAddr, unitListBase, true);

              int finalDay = GetFinalDay();
              int remain = (finalDay >= currentDay) ? (finalDay - currentDay) : 0;
              NotifyBattleDay(remain);
              s_lastNotifiedDay = currentDay;
          }
      }

      // [핵심: 하트비트] 읽은 주소를 즉시 비웁니다.
      DX11Base::g_battleUnitAddr1 = 0;
      DX11Base::g_battleUnitAddr2 = 0;
    } else {
      // [비전투 중] 주소가 0이고 날짜 주소도 없음
      if (s_isWarModsApplied && (currentTime - s_lastSeenTime > 3.0f)) {
        AddLog(u8"[자동화] 상태 초기화 (다음 전투 대기)");
        s_isWarModsApplied = false;
        s_isCacheBuilt = false;
        s_lastSeenTime = 0;
        s_lastAppliedDay = -1;
        s_lastNotifiedDay = -1;

        // 캐시 해제
        __try {
          DX11Base::ClearBattleCache();
          DX11Base::ClearBattleEnvCache();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        // 전투가 끝나면 5번 책략의 전투 인스턴스 상태도 함께 폐기합니다.
        // 사용자 ON 요청과 process-wide hook은 유지하므로 다음 전투에서 자동 재결합됩니다.
        __try {
          DX11Base::ResetStratagemFiveBattleRuntime();
          DX11Base::ResetSpell5HealProbeBattleRuntime();
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        // 전투가 끝나면 특수능력 룰을 원래 데이터(normal)로 안전하게 복구합니다.
        UpdateSpecialAbilities(0, 0, (uintptr_t)GetModuleHandle(NULL));
      }
    }
  }

  void MonitorTechStatus() {
    static uint8_t s_lastAppliedMonth = 0xFF;
    static uint8_t s_lastRelevantGameState = 0xFF;

    uint8_t sm = GetSystemMonthValue();

    // gameBase+0xD0에서 평정(0x05) / 내정(0x07)만 라이프사이클 상태로 사용합니다.
    // 0x00/0x02/0x04/0x06/0x08 등은 화면·전투 전환 중에도 나타나므로 마지막 유효 상태를 유지합니다.
    uintptr_t gameBase = DX11Base::GetGameBase();
    uint8_t gameState = 0xFF;
    bool hasGameState = false;
    if (gameBase && IsValidPtr(gameBase + 0xD0, 1)) {
      gameState = *(uint8_t *)(gameBase + 0xD0);
      hasGameState = true;
    }

    const bool isRelevantState = hasGameState && (gameState == 0x05 || gameState == 0x07);
    const uint8_t previousRelevantState = s_lastRelevantGameState;
    if (isRelevantState)
      s_lastRelevantGameState = gameState;

    const bool hasRelevantState = (s_lastRelevantGameState == 0x05 || s_lastRelevantGameState == 0x07);
    const bool isCouncil = (s_lastRelevantGameState == 0x05);
    const bool councilToDomestic = isRelevantState && gameState == 0x07 && previousRelevantState == 0x05;

    // 평정 중 월 1회 실행. 중간 상태에서는 s_lastAppliedMonth를 리셋하지 않아
    // 전투 종료 후 0x04 -> 0x05 같은 복귀를 새 평정으로 오인하지 않습니다.
    if (isRelevantState && gameState == 0x05) {
      if (s_lastAppliedMonth != sm) {
        unsigned short scenarioYear = 0;
        uint8_t scenarioMonth = 0;
        const bool hasScenarioDate =
            ReadScenarioDate(&scenarioYear, &scenarioMonth) &&
            scenarioYear > 0 &&
            scenarioMonth >= 1 &&
            scenarioMonth <= 12;

        AddLog(
            u8"[특수능력/연말자동/DBG] 평정 진입: uiMonth=%u scenario=%s%u년%u월 state=0x%02X auto=%s lastMonth=%u",
            (unsigned)sm,
            hasScenarioDate ? "" : "INVALID ",
            (unsigned)scenarioYear,
            (unsigned)scenarioMonth,
            (unsigned)gameState,
            bAnnualSpecialAbilityAutoAssign ? "ON" : "OFF",
            (unsigned)s_lastAppliedMonth);

        UpdateOfficerStats99To100();

        // 능력치 한계돌파와 동일한 평정 진입 타이밍에서 실행합니다.
        // 연 1회 자동 특수능력은 실제 시나리오 날짜가 1월인 평정에 들어온 순간 시작합니다.
        // GetSystemMonthValue()는 평정 전환 순간 0일 수 있으므로 연말 판정에는 사용하지 않습니다.
        if (bAnnualSpecialAbilityAutoAssign &&
            bAIOfficerAutoGrowth &&
            hasScenarioDate &&
            scenarioMonth == 1 &&
            DX11Base::WasAIOfficerGrowthAppliedForYear(scenarioYear)) {
          AddLog(
              u8"[특수능력/연말자동/DBG] %u년 AI 성장 완료 확인 -> AutoAssignSpecialAbilitiesFromCouncil 호출",
              (unsigned)scenarioYear);
          DX11Base::AutoAssignSpecialAbilitiesFromCouncil();
        } else if (bAnnualSpecialAbilityAutoAssign) {
          if (!bAIOfficerAutoGrowth) {
            AddLog(
                u8"[특수능력/연말자동/DBG] AI 자동성장 OFF -> 특수능력 자동 판정 미실행");
          } else if (!hasScenarioDate || scenarioMonth != 1) {
            AddLog(
                u8"[특수능력/연말자동/DBG] 자동 ON이지만 실제 시나리오 월=%u -> 1월 자동 판정 미실행",
                (unsigned)scenarioMonth);
          } else {
            AddLog(
                u8"[특수능력/연말자동/DBG] %u년 AI 성장 완료가 확인되지 않아 특수능력 자동 판정 미실행",
                (unsigned)scenarioYear);
          }
        }

        DX11Base::RunAutoCityExchange();
        s_lastAppliedMonth = sm;
      }
    } else if (isRelevantState && gameState == 0x07) {
      s_lastAppliedMonth = 0xFF;
    }

    // 방어건물강화는 여기서 미리 적용하지 않습니다.
    // 실제 전투가 Day 1~30 + Units 1~60으로 확정될 때 MonitorBattleStatus()에서 리프레시합니다.

    // 실제 평정(0x05) -> 내정(0x07) 전환일 때만 평정 종료 처리합니다.
    if (councilToDomestic && bCancelCastleEvent) {
      uintptr_t captAddr = DX11Base::GetCapturedTengiAddr();
      if (captAddr != 0) {
        uintptr_t addr80 = captAddr - 0x10;
        uintptr_t addr90 = captAddr;
        uintptr_t addrA0 = captAddr + 0x10;

        // 포인터 유효성 검사
        if (DX11Base::IsValidPtr(addr80, 2) && DX11Base::IsValidPtr(addr90, 1) && DX11Base::IsValidPtr(addrA0, 2)) {
          if (*(uint8_t *)(addr80) == 0x90 && *(uint8_t *)(addr80 + 1) == 0xE0 && *(uint8_t *)(addr90) == 0xE0 &&
              *(uint8_t *)(addrA0) == 0x28 && *(uint8_t *)(addrA0 + 1) == 0xCB) {

            // 조건 일치시 전기 취소와 동일하게 완전히 초기화
            DX11Base::CancelTengi();
            AddLog(u8"[자동화] 평정 종료: 중지 성성 전기를 취소했습니다.");
          }
        }
      }
    }

    if (hasRelevantState) {
      // 전투/화면 전환 중간값에서는 마지막 0x05/0x07 상태를 그대로 전달합니다.
      // 따라서 0x04 같은 순간 상태로 전투맵을 복구했다가 다시 셔플하는 오탐이 발생하지 않습니다.
      UpdateBattleMapAuto(isCouncil);
      UpdateAutoSpecialtyDistribution(isCouncil);
    }
  }

  // 전투 상태 반환 함수 추가 (외부 모듈에서 현재 전투중인지 판별할 때 사용)
  bool IsInBattle() {
    float currentTime = (float)GetTickCount64() / 1000.0f;
    static float s_lastKnownSeenTime = 0.0f;

    // Check global addr or day pointer directly to update heartbeat if needed without MonitorBattleStatus side effects
    if (DX11Base::g_battleUnitAddr1 != 0 || DX11Base::g_battleUnitAddr2 != 0 || DX11Base::IsSiegeBattleActive()) {
      s_lastKnownSeenTime = currentTime;
      return true;
    }

    // Heartbeat timeout is 1.25s
    if ((currentTime - s_lastKnownSeenTime) < 1.25f) {
      return true;
    }
    return false;
  }

  int GetBattleDay() {
    if (!s_cachedDayBaseAddr) return -1;
    uintptr_t dayAddr = s_cachedDayBaseAddr + 0x28;
    if (IsValidPtr(dayAddr, 1)) {
        return (int)(*(uint8_t*)dayAddr);
    }
    return -1;
  }

  int GetFinalDay() {
    if (!s_cachedDayBaseAddr) return -1;
    uintptr_t finalAddr = s_cachedDayBaseAddr + 0x2C;
    if (IsValidPtr(finalAddr, 1)) {
        return (int)(*(uint8_t*)finalAddr);
    }
    return -1;
  }

} // namespace DX11Base
