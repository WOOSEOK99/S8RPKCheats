// ======================================================
// 전투 환경 및 조건 설정 (Battle Environment Settings)
// ======================================================
#include "../../pch.h"
#include "../../Cheats.h"
#include "../../MenuState.h"
#include "../../showlog.h"
#include "BattleEnvironment.h"
#include "../System/SkillCountManager.h"
#include <vector>

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

  // --- [신규] 함선 병기화 (지상에서 함선전법 활성화) ---
#if 0
  static void ApplyShipWeaponization(uintptr_t exeBase, uintptr_t tacticBase) {
      if (!tacticBase) return;

      static DWORD lastShipTick = 0;
      DWORD currentTick = GetTickCount();
      if (currentTick - lastShipTick < 300) return;
      lastShipTick = currentTick;

      static uint8_t s_lastAppliedVal = 0xFF;

      // 1. 트리거 지형 확인 (Lua: SAN8RPK.exe+03510578 + ...)
      uintptr_t terrainPtr = SAResolveChainLocal(exeBase + 0x03510578, { 0x100, 0x78, 0x0, 0x50, 0x108, 0x50, 0x8, 0x40, 0x10, 0x8 });
      if (!IsValidPtr(terrainPtr, 1)) {
          static bool log1 = false; if(!log1) { AddLog(u8"[함선 디버그] 접근 실패: terrainPtr invalid"); log1 = true; }
          return;
      }
      uint8_t terrain = *(uint8_t*)terrainPtr;

      if (terrain == 11 || terrain == 12) {
          uintptr_t targets[] = { 0x9A4, 0xA24, 0xAA4, 0xB24, 0xBA4 };
          for (int k = 0; k < 5; k++) WriteByteIfDiff(tacticBase + targets[k], 4);
          if (s_lastAppliedVal != 4) {
              AddLog(u8"[함선 디버그] 현재 지형 = 물(%d). 전법 4 복구", terrain);
              s_lastAppliedVal = 4;
          }
          return;
      }

      // 2. 트리거 무장 확인 (Lua: side.base = SAN8RPK.exe+03510578)
      uintptr_t sideRootAddr = SAResolveChainLocal(exeBase + 0x03510578, { 0x100, 0x78, 0x0, 0x50, 0x108, 0x50, 0x8, 0x18 });
      bool triggered = false;
      int foundOfficerID = 0;

      if (!IsValidPtr(sideRootAddr, 8)) {
          static bool log2 = false; if(!log2) { AddLog(u8"[함선 디버그] 접근 실패: sideRootAddr invalid"); log2 = true; }
      } else {
          uintptr_t sideRootDeref = *(uintptr_t*)sideRootAddr;
          if (!IsValidPtr(sideRootDeref, 0x20)) {
              static bool log3 = false; if(!log3) { AddLog(u8"[함선 디버그] 접근 실패: sideRootDeref invalid"); log3 = true; }
          } else {
              uintptr_t moffs[] = { 0x08, 0x10, 0x18 };
              for (int k = 0; k < 3; k++) {
                  uintptr_t offPtr = *(uintptr_t*)(sideRootDeref + moffs[k]);
                  if (IsValidPtr(offPtr, 0x10)) {
                      int officerID = (int)(*(unsigned short*)(offPtr + 0x08));
                      if (officerID > 0) {
                          // 매번 새로운 무장을 Hover/Select 할 때마다 무조건 1회 로깅!
                          static int lastLogHoverOff = 0;
                          int checkboxState = GetTargetSkillCount(officerID, 0x1009);
                          
                          if (lastLogHoverOff != officerID) {
                              AddLog(u8"[함선 디버그] UI 스캔 완료 -> 무장ID: %d, 체크박스: %d, 현재지형: %d", officerID, checkboxState, terrain);
                              lastLogHoverOff = officerID;
                          }

                          if (checkboxState > 0) {
                              triggered = true;
                              foundOfficerID = officerID;
                              break;
                          }
                      }
                  }
              }
          }
      }

      // 3. 값 대입
      uint8_t targetVal = triggered ? 8 : 4;
      if (triggered && s_lastAppliedVal != 8) {
          AddLog(u8"[함선병기] 지상 발동! (무장:%d, 지형:%d)", foundOfficerID, terrain);
          s_lastAppliedVal = 8;
      } else if (!triggered && s_lastAppliedVal == 8) {
          AddLog(u8"[함선병기] 해제 (조건 무장 아님, 지형:%d)", terrain);
          s_lastAppliedVal = 4;
      }

      uintptr_t targets[] = { 0x9A4, 0xA24, 0xAA4, 0xB24, 0xBA4 };
      for (int k = 0; k < 5; k++) {
          WriteByteIfDiff(tacticBase + targets[k], targetVal);
      }

  }
#endif

  void UpdateBattleEnvironment(uintptr_t exeBase, uintptr_t dayBaseAddr, uintptr_t unitListBase) {
      if (!exeBase) return;

      static DWORD lastEnvTick = 0;
      DWORD currentTick = GetTickCount();
      if (currentTick - lastEnvTick < 200) return;
      lastEnvTick = currentTick;

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
              uint8_t targetDay = 25;
              if (totalUnits < 20) targetDay = 15;
              else if (totalUnits < 30) targetDay = 20;

              WriteByteIfDiff(finalDayAddr, targetDay);
          }
      } else {
          WriteByteIfDiff(finalDayAddr, 30); // 기본값 복구
      }

      // 2. 책략/마스터 파라미터 구조체 베이스 주소
      uintptr_t tacticBase = SAResolveChainLocal(exeBase + 0x02ED7A10, { 0x110, 0x120, 0x40, 0x168, 0 });
      if (!tacticBase) return;

      // 3. 함선 병기화 처리 (유저 요청으로 임시 비활성화)
      // ApplyShipWeaponization(exeBase, tacticBase);

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

          uintptr_t terrainBase = SAResolveChainLocal(exeBase + 0x034C8630, { 0, 8, 0x10, 0, 0 });
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

          uintptr_t terrainBase = SAResolveChainLocal(exeBase + 0x034C8630, { 0, 8, 0x10, 0, 0 });
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
