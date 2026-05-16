#include "BattleMonitor.h"
#include "Cheats.h"
#include "Cheats/Officer/SelectOfficercapture.h"
#include "Cheats/Officer/StatMonitor.h"
#include "Cheats/System/MonthCapture.h"
#include "Cheats/System/SkillCountManager.h"
#include "Cheats/System/SystemMonth.h"
#include "Cheats/System/TengiCave.h"
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
#include "Cheats/War/Terrainignore.h"
#include "MenuState.h"
#include "pch.h"
#include "showlog.h"

namespace DX11Base {
  // 전역 캐시 주소 (모든 함수에서 공유)
  static uintptr_t s_cachedUnitListBase = 0;
  static uintptr_t s_cachedDayBaseAddr = 0;
  static uintptr_t s_cachedDefenderAddr = 0;
  static DWORD s_lastResolveTick = 0;

  // 프리징 방지를 위한 SEH(예외 처리) 기반의 안전한 포인터 체인 추적
  static uintptr_t ResolveChain(uintptr_t base, std::initializer_list<int> offsets) {
    if (base == 0)
      return 0;
    uintptr_t current = base;

    __try {
      for (int offset : offsets) {
        if (current == 0 || current < 0x10000)
          return 0; // 널 포인터 및 비정상 주소 방어
        if (!IsValidPtr(current, 8))
          return 0;

        uintptr_t next = *(uintptr_t *)current;
        if (next == 0 || next < 0x10000)
          return 0;
        current = next + offset;
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      // 메모리 접근 위반 발생 시 즉시 탈출하여 프리징/크래시 방지
      return 0;
    }
    return current;
  }

  // 전투 유닛들의 전법 횟수를 커스텀 설정값으로 덮어씁니다.
  void UpdateBattleUnitSkills(bool silent = false) {
    uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
    if (!exeBase)
      return;

    // SiegeWarfare.cpp에서 검증된 체인 사용
    uintptr_t unitListBase = ResolveChain(exeBase + 0x02E99460, {0x28, 0x250, 0x1D8, 0, 0x180, 0});

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

  void MonitorBattleStatus() {
    static bool s_isWarModsApplied = false;
    static float s_lastSeenTime = 0.0f;
    static int s_lastAppliedDay = -1;
    float currentTime = (float)GetTickCount64() / 1000.0f;

    // 0. 기반 주소 체크 (게임 로딩/메뉴 시 자동 초기화)
    uintptr_t gameBase = DX11Base::GetGameBase();
    if (gameBase == 0) {
      if (s_isWarModsApplied) {
        s_isWarModsApplied = false;
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

    // 2. 극심한 CPU 스로틀 방지를 위한 체인 주소 500ms 갱신 지연 캐시
    DWORD currentTick = GetTickCount();

    if (currentTick - s_lastResolveTick >= 500 || (!s_cachedUnitListBase && !s_cachedDayBaseAddr)) {
      s_lastResolveTick = currentTick;
      uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);
      // [루아 대조] unitListBase: 6단계([] 6개), dayAddr: 8단계([] 8개), defender: 8단계([] 8개)
      s_cachedUnitListBase = ResolveChain(exeBase + 0x02E99460, {0x28, 0x250, 0x1D8, 0, 0x180, 0});
      s_cachedDayBaseAddr = ResolveChain(exeBase + 0x02E99460, {0x28, 0x250, 0x218, 0, 0x3D8, 0x478, 0, 0});
      s_cachedDefenderAddr = ResolveChain(exeBase + 0x03510578, {0x100, 0x80, 0, 8, 0xC8, 8, 0x18, 8});
    }

    uintptr_t dayAddr = s_cachedDayBaseAddr ? (s_cachedDayBaseAddr + 0x28) : 0;
    bool isSiegeActive = (dayAddr != 0 && IsValidPtr(dayAddr, 1));
    bool battleActive = (addr1 != 0 || addr2 != 0) || isSiegeActive;

    if (battleActive) {
      // [전투 중] 주소가 포착됨 또는 날짜 주소 확인됨
      s_lastSeenTime = currentTime;
      uintptr_t exeBase = (uintptr_t)GetModuleHandle(NULL);

      uintptr_t unitListBase = s_cachedUnitListBase;
      uintptr_t dayBaseAddr = s_cachedDayBaseAddr;
      uintptr_t defenderAddr = s_cachedDefenderAddr;

      int unitCountTotal = 0;
      __try {
        if (unitListBase && IsValidPtr(unitListBase - 0x08, 1)) {
          unitCountTotal = *(unsigned char *)(unitListBase - 0x08);
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        unitCountTotal = 0;
      }

      uint8_t defenderForce = 0;
      __try {
        if (defenderAddr && IsValidPtr(defenderAddr, 1)) {
          defenderForce = *(uint8_t *)defenderAddr;
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        defenderForce = 0;
      }

      int currentDay = -1;
      __try {
        if (dayAddr && IsValidPtr(dayAddr, 1)) {
          currentDay = (int)(*(unsigned char *)dayAddr);
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        currentDay = -1;
      }

      // 아직 리프레시를 안 했다면 실행
      if (!s_isWarModsApplied) {
        AddLog(u8"[자동화] 전투 감지(%llX) -> 모든 전쟁 모드 리프레시", addr1);

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

        s_isWarModsApplied = true;
        // 캐싱 빌드: 전투 진입 시 1회 전체 부대 스캔 → 이후 UpdateSpecialAbilities는 캐시 매칭만 수행
        __try {
          DX11Base::InitializeBattleCache((int)unitCountTotal, unitListBase, exeBase);
          DX11Base::InitBattleEnvCache(exeBase);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        // 커스텀 전법 횟수 적용 (전투 리프레시 시 1회 수행)
        __try {
          UpdateBattleUnitSkills(false);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        s_lastAppliedDay = currentDay;
      }

      // [추가] 1일차가 시작될 때 한 번 더 적용 (포진 등이 끝나고 실제 전투 시작 시 초기화 대응)
      if (currentDay == 1 && s_lastAppliedDay != 1) {
        AddLog(u8"[자동화] 1일차 감지 -> 전법 횟수 재주입");
        UpdateBattleUnitSkills(true); // 로그 중복 방지를 위해 silent 모드
        s_lastAppliedDay = 1;
      }
      if (currentDay != -1) {
        s_lastAppliedDay = currentDay;
      }

      // [실시간 기능 타임 슬라이싱 (부하 분산)]
      // MonitorBattleStatus가 100ms 주기로 호출되므로,
      // 한 틱에 하나씩만 번갈아 실행하여 CPU 스파이크(렉)를 1/3로 줄입니다.
      static int s_tickPhase = 0;

      if (s_tickPhase == 0) {
        // [전투 환경 업데이트 - 날씨/일자/지형 등]
        __try {
          DX11Base::UpdateBattleEnvironment(exeBase, dayBaseAddr, unitListBase);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
      } else if (s_tickPhase == 1) {
        // [공성전 업데이트]
        __try {
          DX11Base::UpdateSiegeWarfare(dayAddr, unitCountTotal, defenderForce, unitListBase);
          DX11Base::UpdateSiegeWarfare2(dayAddr, unitCountTotal, defenderForce, unitListBase);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
      } else if (s_tickPhase == 2) {
        // [특수 기능 실시간 체크]
        __try {
          UpdateSpecialAbilities(unitCountTotal, unitListBase, exeBase);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
      }

      s_tickPhase = (s_tickPhase + 1) % 3;

      // [핵심: 하트비트] 읽은 주소를 즉시 비웁니다.
      DX11Base::g_battleUnitAddr1 = 0;
      DX11Base::g_battleUnitAddr2 = 0;
    } else {
      // [비전투 중] 주소가 0이고 날짜 주소도 없음
      if (s_isWarModsApplied && (currentTime - s_lastSeenTime > 3.0f)) {
        AddLog(u8"[자동화] 상태 초기화 (다음 전투 대기)");
        s_isWarModsApplied = false;
        s_lastSeenTime = 0;
        s_lastAppliedDay = -1;

        // 캐시 해제
        __try {
          DX11Base::ClearBattleCache();
          DX11Base::ClearBattleEnvCache();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        // 전투가 끝나면 특수능력 룰을 원래 데이터(normal)로 안전하게 복구합니다.
        UpdateSpecialAbilities(0, 0, (uintptr_t)GetModuleHandle(NULL));
      }
    }
  }

  void MonitorTechStatus() {
    static uint8_t s_lastAppliedMonth = 0xFF;
    static bool s_wasCouncil = false;

    uint8_t sm = GetSystemMonthValue();
    uint8_t rm = GetCurrentMonth();

    // [2026-04-12] 신규 포착 정보: gameBase+0xD0 (00: 시작메뉴, 05:평정, 07:내정)
    uintptr_t gameBase = DX11Base::GetGameBase();
    uint8_t gameState = 0;
    bool isCouncil = false;
    if (gameBase && IsValidPtr(gameBase + 0xD0, 1)) {
      gameState = *(uint8_t *)(gameBase + 0xD0);
      isCouncil = (gameState == 0x05);
    }

    if (isCouncil) {
      if (s_lastAppliedMonth != sm) {
        if (bDefBuilding) {
          SetDefBuildingBoost(false);
          SetDefBuildingBoost(true);
        }
        UpdateOfficerStats99To100();
        s_lastAppliedMonth = sm;
      }
    } else {
      if (s_lastAppliedMonth != 0xFF) {
        s_lastAppliedMonth = 0xFF;
      }
    }

    // 평정 -> 도시 생활(내정 등) 전환 시점 감지
    if (!isCouncil && s_wasCouncil) {
      if (bCancelCastleEvent) {
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
    }
    s_wasCouncil = isCouncil;

    UpdateBattleMapAuto(isCouncil);
    UpdateAutoSpecialtyDistribution(isCouncil);
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
