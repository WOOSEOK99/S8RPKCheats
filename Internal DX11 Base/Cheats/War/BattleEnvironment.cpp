// ======================================================
// 전투 환경 및 조건 설정 (Battle Environment Settings)
// ======================================================
#include "../../pch.h"
#include "../../Cheats.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include "BattleEnvironment.h"
#include "../System/SkillCountManager.h"

namespace DX11Base {

  static uintptr_t SAResolveChainLocal(uintptr_t base, std::initializer_list<int> offsets) {
      uintptr_t current = base;
      for (int offset : offsets) {
          if (!IsValidPtr(current, 8)) return 0;
          uintptr_t next = *(uintptr_t*)current;
          if (!next) return 0;
          current = next + offset;
      }
      return current;
  }

  // ──────────────────────────────────────────────────────
  // [포인터 캐시] tacticBase, terrainBase
  // 전투 중 고정되는 베이스 주소를 진입 1회만 해소하여 저장.
  // UpdateBattleEnvironment와 ApplyShipWeaponization는 이 지역 변수를 바로 사용.
  // ──────────────────────────────────────────────────────
  static uintptr_t g_cachedTacticBase  = 0;
  static uintptr_t g_cachedTerrainBase = 0;

  void InitBattleEnvCache(uintptr_t exeBase) {
      g_cachedTacticBase  = SAResolveChainLocal(exeBase + 0x02ED7A10, { 0x110, 0x120, 0x40, 0x168, 0 });
      g_cachedTerrainBase = SAResolveChainLocal(exeBase + 0x034C8630, { 0, 8, 0x10, 0, 0 });
      AddLog(u8"[BattleEnv 캐시] tacticBase=0x%llX terrainBase=0x%llX",
             g_cachedTacticBase, g_cachedTerrainBase);
  }

  void ClearBattleEnvCache() {
      g_cachedTacticBase  = 0;
      g_cachedTerrainBase = 0;
  }

  static void WriteByteIfDiff(uintptr_t addr, uint8_t val) {
      if (!addr) return;
      if (*(uint8_t*)addr != val) {
          DWORD old;
          if (VirtualProtect((LPVOID)addr, 1, PAGE_EXECUTE_READWRITE, &old)) {
              *(uint8_t*)addr = val;
              VirtualProtect((LPVOID)addr, 1, old, &old);
          }
      }
  }

  static void WriteFiveBytesIfDiff(uintptr_t addr, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5) {
      if (!addr) return;
      uint8_t* p = (uint8_t*)addr;
      if (p[0] != b1 || p[1] != b2 || p[2] != b3 || p[3] != b4 || p[4] != b5) {
          DWORD old;
          if (VirtualProtect((LPVOID)addr, 5, PAGE_EXECUTE_READWRITE, &old)) {
              p[0] = b1; p[1] = b2; p[2] = b3; p[3] = b4; p[4] = b5;
              VirtualProtect((LPVOID)addr, 5, old, &old);
          }
      }
  }

  static short GetTerrainBonus(uint8_t troopType, uint8_t terrain) {
      if (troopType < 1 || troopType > 12) return 0;
      if (terrain < 1 || terrain > 17) return 0;
      return g_TerrainBonusTable[troopType][terrain];
  }

  static void WriteInt16IfDiff(uintptr_t addr, short val) {
      if (!addr) return;
      if (*(short*)addr != val) {
          DWORD old;
          if (VirtualProtect((LPVOID)addr, 2, PAGE_EXECUTE_READWRITE, &old)) {
              *(short*)addr = val;
              VirtualProtect((LPVOID)addr, 2, old, &old);
          }
      }
  }

  // ── [함선 병기화] 체크박스(0x1009 플래그) 기반 작동 ─────────────────────
  //   active unit의 무장 중 0x1009 플래그가 설정된 무장이 있고,
  //   그 부대가 물(11,12) 지형이 아닌 경우 함선전법(값=8) 활성화
  // ─────────────────────────────────────────────────────────────────────
  static void ApplyShipWeaponization(uintptr_t exeBase) {
      static DWORD lastShipTick = 0;
      DWORD currentTick = GetTickCount();
      if (currentTick - lastShipTick < 300) return;
      lastShipTick = currentTick;

      static uint8_t s_lastAppliedVal = 0xFF;

      // 캐시된 tacticBase 사용 (SAResolveChainLocal 제거)
      uintptr_t tacticBase = g_cachedTacticBase;
      if (!tacticBase) {
          // 아직 캐시가 준비되지 않았으면 폴백 해소 시도
          tacticBase = SAResolveChainLocal(exeBase + 0x02ED7A10, { 0x110, 0x120, 0x40, 0x168, 0 });
          if (!tacticBase) {
              static bool logTactic = false;
              if (!logTactic) { AddLog(u8"[함선병기] tacticBase 포인터 없음"); logTactic = true; }
              return;
          }
      }

      // Active unit 포인터 획득 (trailing 0 필수)
      uintptr_t activeUnitPtr = SAResolveChainLocal(exeBase + 0x03510578,
          { 0x100, 0x78, 0x0, 0x50, 0x108, 0x50, 0x8, 0 });

      static bool logNoUnit = false;
      if (!IsValidPtr(activeUnitPtr, 0x50)) {
          if (!logNoUnit) { AddLog(u8"[함선병기] activeUnitPtr 없음"); logNoUnit = true; }
          return;
      }
      logNoUnit = false;

      // 지형 확인
      uintptr_t ptr40   = *(uintptr_t*)(activeUnitPtr + 0x40);
      if (!IsValidPtr(ptr40, 0x20)) return;
      uintptr_t ptr40_10 = *(uintptr_t*)(ptr40 + 0x10);
      if (!IsValidPtr(ptr40_10, 0x10)) return;
      uint8_t terrain = *(uint8_t*)(ptr40_10 + 0x8);

      if (terrain == 11 || terrain == 12) {
          uintptr_t tgts[] = { 0x9A4, 0xA24, 0xAA4, 0xB24, 0xBA4 };
          for (int k = 0; k < 5; k++) WriteByteIfDiff(tacticBase + tgts[k], 4);
          if (s_lastAppliedVal != 4) { s_lastAppliedVal = 4; }
          return;
      }

      uintptr_t memberPtr = *(uintptr_t*)(activeUnitPtr + 0x18);
      if (!IsValidPtr(memberPtr, 0x20)) return;

      bool triggered = false;
      int foundOfficerID = 0;
      uintptr_t officerSlots[] = { 0x8, 0x10, 0x18 };

      for (int k = 0; k < 3 && !triggered; k++) {
          uintptr_t offPtr = *(uintptr_t*)(memberPtr + officerSlots[k]);
          if (!IsValidPtr(offPtr, 0x10)) continue;
          int officerID = (int)(*(unsigned short*)(offPtr + 0x8));
          if (officerID <= 0) continue;
          int flag = GetTargetSkillCount(officerID, 0x1009);
          if (flag > 0) { triggered = true; foundOfficerID = officerID; }
      }

      uint8_t targetVal = triggered ? 8 : 4;
      if (triggered && s_lastAppliedVal != 8) {
          AddLog(u8"[함선병기] 지상 발동! (무장ID:%d, 지형:%d)", foundOfficerID, terrain);
          s_lastAppliedVal = 8;
      } else if (!triggered && s_lastAppliedVal == 8) {
          AddLog(u8"[함선병기] 해제");
          s_lastAppliedVal = 4;
      }

      uintptr_t tgts[] = { 0x9A4, 0xA24, 0xAA4, 0xB24, 0xBA4 };
      for (int k = 0; k < 5; k++) WriteByteIfDiff(tacticBase + tgts[k], targetVal);
  }

  void UpdateBattleEnvironment(uintptr_t exeBase, uintptr_t dayBaseAddr, uintptr_t unitListBase, bool force) {
      if (!exeBase) return;

      static DWORD lastEnvTick = 0;
      DWORD currentTick = GetTickCount();
      if (!force && currentTick - lastEnvTick < 200) return;
      lastEnvTick = currentTick;

      // 1. 함선 병기화 처리 (tacticBase 내부에서 자체 획득, 다른 게이트에 막히지 않도록 최상단 배치)
      ApplyShipWeaponization(exeBase);

      if (!dayBaseAddr) return;
      uintptr_t battleDayAddr = dayBaseAddr + 0x28;
      uintptr_t finalDayAddr  = dayBaseAddr + 0x2C;
      
      uint8_t day = *(uint8_t*)battleDayAddr;
      if (day < 1 || day > 30) return;

      //--------------------------------------------------
      // 전투 일자 변경 처리
      //--------------------------------------------------
      if (bDateAlways15) {
          WriteByteIfDiff(finalDayAddr, 15);
      } else if (bDateDynamic) {
          if (unitListBase && IsValidPtr(unitListBase - 0x08, 1)) {
              uint8_t totalUnits = *(uint8_t*)(unitListBase - 0x08);
              uint8_t targetDay = 25; // 20부대 이상 기본 25일
              if (totalUnits < 15) targetDay = 15;
              else if (totalUnits < 20) targetDay = 20;

              WriteByteIfDiff(finalDayAddr, targetDay);
          }
      } else {
          WriteByteIfDiff(finalDayAddr, 30); // 기본값 복구
      }

      // 캐시된 tacticBase 사용, 없으면 폴백 해소 시도
      static bool logTacticGate = false;
      uintptr_t tacticBase = g_cachedTacticBase;
      if (!tacticBase) {
          tacticBase = SAResolveChainLocal(exeBase + 0x02ED7A10, { 0x110, 0x120, 0x40, 0x168, 0 });
          if (!tacticBase) {
              if (!logTacticGate) { AddLog(u8"[BattleEnv] tacticBase 없음 - 날씨 처리 스킵"); logTacticGate = true; }
              return;
          }
      }
      logTacticGate = false;

      //--------------------------------------------------
      // 날씨 전법 변경 처리 (공통 / 대폭)
      //--------------------------------------------------
      uintptr_t weatherP = SAResolveChainLocal(exeBase + 0x02E99460, { 0x28, 0xE8, 0x1A8, 0x218, 0xF8, 0x218, 0 });
      uintptr_t windP    = SAResolveChainLocal(exeBase + 0x02E99460, { 0x28, 0x250, 0x1D8, 0x3C0, 0, 0x50, 0 });
      
      uint8_t weather = weatherP ? *(uint8_t*)(weatherP + 0x559) : 0;
      uint8_t wind    = windP ? *(uint8_t*)(windP + 0xF9) : 0;

      // --- [대폭 변경 전용] ---
      if (bWeatherSkillComplex && weatherP && windP) {
          uint8_t fireEffTarget = 25;
          uint8_t igniteTarget = 0;
          if (weather == 0 && wind == 0) { // 맑음+무풍
              fireEffTarget = 2;
              igniteTarget = 2;
          }

          WriteByteIfDiff(tacticBase + 0xC2E, fireEffTarget);
          WriteByteIfDiff(tacticBase + 0xC50, fireEffTarget);
          WriteByteIfDiff(tacticBase + 0xC72, fireEffTarget);

          // 캐시된 terrainBase 사용, 없으면 폴백
          uintptr_t terrainBase = g_cachedTerrainBase;
          if (!terrainBase) terrainBase = SAResolveChainLocal(exeBase + 0x034C8630, { 0, 8, 0x10, 0, 0 });
          if (terrainBase) {
              WriteByteIfDiff(terrainBase + 0x1AA3A1, igniteTarget); // ROAD
              WriteByteIfDiff(terrainBase + 0x1AA3E1, igniteTarget); // WASTELAND
              WriteByteIfDiff(terrainBase + 0x1AA441, igniteTarget); // SWAMP
              WriteByteIfDiff(terrainBase + 0x1AA461, igniteTarget); // MOUNTAIN
              WriteByteIfDiff(terrainBase + 0x1AA561, igniteTarget); // BRIDGE
              WriteByteIfDiff(terrainBase + 0x1AA5A1, igniteTarget); // ROCK

              if (weather == 0) { // 맑음
                  WriteFiveBytesIfDiff(terrainBase + 0x1AA3A4, 19, 19, 0, 20, 30);
                  WriteFiveBytesIfDiff(terrainBase + 0x1AA3E4, 19, 19, 0, 20, 30);
                  WriteFiveBytesIfDiff(terrainBase + 0x1AA444, 19, 19, 0, 20, 30);
                  WriteFiveBytesIfDiff(terrainBase + 0x1AA464, 19, 19, 0, 20, 30);
                  WriteFiveBytesIfDiff(terrainBase + 0x1AA564, 19, 19, 0, 20, 30);
                  WriteFiveBytesIfDiff(terrainBase + 0x1AA5A4, 19, 19, 0, 20, 30);
              } else {
                  WriteFiveBytesIfDiff(terrainBase + 0x1AA3A4, 0, 0, 0, 100, 100);
                  WriteFiveBytesIfDiff(terrainBase + 0x1AA3E4, 0, 0, 0, 100, 100);
                  WriteFiveBytesIfDiff(terrainBase + 0x1AA444, 0, 0, 0, 100, 100);
                  WriteFiveBytesIfDiff(terrainBase + 0x1AA464, 0, 0, 0, 100, 100);
                  WriteFiveBytesIfDiff(terrainBase + 0x1AA564, 0, 0, 0, 100, 100);
                  WriteFiveBytesIfDiff(terrainBase + 0x1AA5A4, 0, 0, 0, 100, 100);
              }
          }

          // 낙석계 (강풍 = 2)
          uint8_t rockTarget = 27;
          if (wind == 3) rockTarget = 2;
          WriteByteIfDiff(tacticBase + 0xD2E, rockTarget);
          WriteByteIfDiff(tacticBase + 0xD50, rockTarget);
          WriteByteIfDiff(tacticBase + 0xD72, rockTarget);

          // 복병계 (폭우, 눈 = 2)
          uint8_t ambTarget = 26;
          if (weather == 3 || weather == 4) ambTarget = 2;
          WriteByteIfDiff(tacticBase + 0xDAE, ambTarget);
          WriteByteIfDiff(tacticBase + 0xDD0, ambTarget);
          WriteByteIfDiff(tacticBase + 0xDF2, ambTarget);
      } else {
          // 대폭 변경 전용 속성들의 원상복구
          WriteByteIfDiff(tacticBase + 0xC2E, 25);
          WriteByteIfDiff(tacticBase + 0xC50, 25);
          WriteByteIfDiff(tacticBase + 0xC72, 25);

          WriteByteIfDiff(tacticBase + 0xD2E, 27);
          WriteByteIfDiff(tacticBase + 0xD50, 27);
          WriteByteIfDiff(tacticBase + 0xD72, 27);

          WriteByteIfDiff(tacticBase + 0xDAE, 26);
          WriteByteIfDiff(tacticBase + 0xDD0, 26);
          WriteByteIfDiff(tacticBase + 0xDF2, 26);

          // 캐시된 terrainBase 사용, 없으면 폴백
          uintptr_t terrainBase = g_cachedTerrainBase;
          if (!terrainBase) terrainBase = SAResolveChainLocal(exeBase + 0x034C8630, { 0, 8, 0x10, 0, 0 });
          if (terrainBase) {
              WriteByteIfDiff(terrainBase + 0x1AA3A1, 0); 
              WriteByteIfDiff(terrainBase + 0x1AA3E1, 0); 
              WriteByteIfDiff(terrainBase + 0x1AA441, 0); 
              WriteByteIfDiff(terrainBase + 0x1AA461, 0); 
              WriteByteIfDiff(terrainBase + 0x1AA561, 0); 
              WriteByteIfDiff(terrainBase + 0x1AA5A1, 0); 

              WriteFiveBytesIfDiff(terrainBase + 0x1AA3A4, 0, 0, 0, 100, 100);
              WriteFiveBytesIfDiff(terrainBase + 0x1AA3E4, 0, 0, 0, 100, 100);
              WriteFiveBytesIfDiff(terrainBase + 0x1AA444, 0, 0, 0, 100, 100);
              WriteFiveBytesIfDiff(terrainBase + 0x1AA464, 0, 0, 0, 100, 100);
              WriteFiveBytesIfDiff(terrainBase + 0x1AA564, 0, 0, 0, 100, 100);
              WriteFiveBytesIfDiff(terrainBase + 0x1AA5A4, 0, 0, 0, 100, 100);
          }
      }

      // --- [간단 / 대폭 공통] ---
      if ((bWeatherSkillSimple || bWeatherSkillComplex) && weatherP) {
          uint8_t fireTarget = 70; // 화계 기본 (70)
          if (weather == 2 || weather == 3 || weather == 4) fireTarget = 123;
          
          uint8_t waterTarget = 9; // 수계 기본 (9)
          if (weather == 2 || weather == 3) waterTarget = 2; // 격류 가능 조건 제한 해제

          WriteByteIfDiff(tacticBase + 0x826, fireTarget);
          WriteByteIfDiff(tacticBase + 0x9A6, fireTarget);
          WriteByteIfDiff(tacticBase + 0xC26, fireTarget);
          
          WriteByteIfDiff(tacticBase + 0xCAE, waterTarget);
          WriteByteIfDiff(tacticBase + 0xCD0, waterTarget);
          WriteByteIfDiff(tacticBase + 0xCF2, waterTarget);
      } else {
          // 비활성 시 공통 속성의 원상복구
          WriteByteIfDiff(tacticBase + 0x826, 70);
          WriteByteIfDiff(tacticBase + 0x9A6, 70);
          WriteByteIfDiff(tacticBase + 0xC26, 70);

          WriteByteIfDiff(tacticBase + 0xCAE, 9);
          WriteByteIfDiff(tacticBase + 0xCD0, 9);
          WriteByteIfDiff(tacticBase + 0xCF2, 9);
      }
      //--------------------------------------------------
      // 지형 능력 변경 처리
      //--------------------------------------------------
      static bool s_terrainBonusActive = false;
      if (bTerrainAbilityAtkDef || bTerrainAbilityAll) {
          s_terrainBonusActive = true;
          if (unitListBase && IsValidPtr(unitListBase - 0x08, 1)) {
              uint8_t unitCount = *(uint8_t*)(unitListBase - 0x08);
              for (int i = 0; i < unitCount; i++) {
                  uintptr_t unitPtr = *(uintptr_t*)(unitListBase + 0x8 + i * 0x10);
                  if (!IsValidPtr(unitPtr, 8)) continue;

                  // Get Troop / Terrain
                  uintptr_t troopP1 = IsValidPtr(unitPtr + 0x18, 8) ? *(uintptr_t*)(unitPtr + 0x18) : 0;
                  uintptr_t troopP2 = (troopP1 && IsValidPtr(troopP1 + 0x20, 8)) ? *(uintptr_t*)(troopP1 + 0x20) : 0;
                  
                  uintptr_t terrP1 = IsValidPtr(unitPtr + 0x40, 8) ? *(uintptr_t*)(unitPtr + 0x40) : 0;
                  uintptr_t terrP2 = (terrP1 && IsValidPtr(terrP1 + 0x10, 8)) ? *(uintptr_t*)(terrP1 + 0x10) : 0;
                  
                  if (troopP2 && terrP2) {
                      uint8_t troopType = *(uint8_t*)(troopP2 + 0x8);
                      uint8_t terrain = *(uint8_t*)(terrP2 + 0x8);

                      short baseStat = *(short*)(unitPtr + 0x658);
                      short bonus = GetTerrainBonus(troopType, terrain);

                      int finalStat = (baseStat * (100 + bonus)) / 100;
                      if (finalStat < 0) finalStat = 0;
                      if (finalStat > 32767) finalStat = 32767;

                      WriteInt16IfDiff(unitPtr + 0x600, (short)finalStat);

                      short baseMight = *(short*)(unitPtr + 0x65C);
                      short baseInt   = *(short*)(unitPtr + 0x660);

                      if (bTerrainAbilityAll) {
                          int finalMight = (baseMight * (100 + bonus)) / 100;
                          if (finalMight < 0) finalMight = 0;
                          if (finalMight > 32767) finalMight = 32767;

                          int finalInt = (baseInt * (100 + bonus)) / 100;
                          if (finalInt < 0) finalInt = 0;
                          if (finalInt > 32767) finalInt = 32767;

                          WriteInt16IfDiff(unitPtr + 0x604, (short)finalMight);
                          WriteInt16IfDiff(unitPtr + 0x608, (short)finalInt);
                      } else {
                          // 공방만 활성화 시, 무력/지력은 기본값 유지
                          WriteInt16IfDiff(unitPtr + 0x604, baseMight);
                          WriteInt16IfDiff(unitPtr + 0x608, baseInt);
                      }
                  }
              }
          }
      } else if (s_terrainBonusActive) {
          s_terrainBonusActive = false;
          // 비활성 시 원상 복구 (공방, 무력, 지력 모두)
          if (unitListBase && IsValidPtr(unitListBase - 0x08, 1)) {
              uint8_t unitCount = *(uint8_t*)(unitListBase - 0x08);
              for (int i = 0; i < unitCount; i++) {
                  uintptr_t unitPtr = *(uintptr_t*)(unitListBase + 0x8 + i * 0x10);
                  if (IsValidPtr(unitPtr, 8)) {
                      short baseStat  = *(short*)(unitPtr + 0x658);
                      short baseMight = *(short*)(unitPtr + 0x65C);
                      short baseInt   = *(short*)(unitPtr + 0x660);
                      
                      WriteInt16IfDiff(unitPtr + 0x600, baseStat);
                      WriteInt16IfDiff(unitPtr + 0x604, baseMight);
                      WriteInt16IfDiff(unitPtr + 0x608, baseInt);
                  }
              }
          }
      }
  }
} // namespace DX11Base
