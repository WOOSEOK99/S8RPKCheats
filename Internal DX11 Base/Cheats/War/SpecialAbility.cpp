// ======================================================
// 특수 능력 (Special Unit Abilities) - 구현 뼈대
// ======================================================
#include "../../pch.h"
#include "../../Cheats.h"
#include "../../showlog.h"
#include "../../MenuState.h"
#include "SpecialAbility.h"

#include <vector>

#include "../System/SkillCountManager.h"

namespace DX11Base {

  // ── 공용 헬퍼 ──────────────────────────────────────
  // 포인터 체인을 따라 최종 주소를 반환합니다.
  static uintptr_t SAResolveChain(uintptr_t base, std::initializer_list<int> offsets) {
      uintptr_t current = base;
      for (int offset : offsets) {
          if (!IsValidPtr(current, 8)) return 0;
          uintptr_t next = *(uintptr_t*)current;
          if (!next) return 0;
          current = next + offset;
      }
      return current;
  }

  // ══════════════════════════════════════════════════════
  //  군악대 (Military Band)
  //  역할: 주변 아군 사기 회복 강화
  // ══════════════════════════════════════════════════════
  static bool g_gunakdaeApplied = false;

  void SetGunakdae(bool enable) {
      AddLog(enable ? u8"[군악대] 활성화" : u8"[군악대] 비활성화");
      g_gunakdaeApplied = enable;
  }

  // ══════════════════════════════════════════════════════
  //  무쌍 보명 (Peerless Infantry)
  // ══════════════════════════════════════════════════════
  static bool g_musangBomyeongApplied = false;
  void SetMusangBomyeong(bool enable) {
      AddLog(enable ? u8"[무쌍보병] 활성화" : u8"[무쌍보병] 비활성화");
      g_musangBomyeongApplied = enable;
  }

  // ══════════════════════════════════════════════════════
  //  불꽃기병 (Flame Cavalry)
  // ══════════════════════════════════════════════════════
  static bool g_flameKnightApplied = false;
  void SetFlameKnight(bool enable) {
      AddLog(enable ? u8"[불꽃기병] 활성화" : u8"[불꽃기병] 비활성화");
      g_flameKnightApplied = enable;
  }

  // ══════════════════════════════════════════════════════
  //  원격 궁병 (Ranged Archers)
  // ══════════════════════════════════════════════════════
  static bool g_rangedArcherApplied = false;
  void SetRangedArcher(bool enable) {
      AddLog(enable ? u8"[원격궁병] 활성화" : u8"[원격궁병] 비활성화");
      g_rangedArcherApplied = enable;
  }

  // ══════════════════════════════════════════════════════
  //  등갑군 (Rattan Armor Troops)
  // ══════════════════════════════════════════════════════
  static bool g_rattanArmorApplied = false;
  void SetRattanArmor(bool enable) {
      AddLog(enable ? u8"[등갑군] 활성화" : u8"[등갑군] 비활성화");
      g_rattanArmorApplied = enable;
  }

  // ══════════════════════════════════════════════════════
  //  총사령관 (Generalissimo)
  // ══════════════════════════════════════════════════════
  static bool g_generalIssimoApplied = false;
  void SetGeneralissimo(bool enable) {
      AddLog(enable ? u8"[총사령관] 활성화" : u8"[총사령관] 비활성화");
      g_generalIssimoApplied = enable;
  }

  // ══════════════════════════════════════════════════════
  //  기습부대 (Ambush Unit)
  // ══════════════════════════════════════════════════════
  static bool g_ambushUnitApplied = false;
  void SetAmbushUnit(bool enable) {
      AddLog(enable ? u8"[기습부대] 활성화" : u8"[기습부대] 비활성화");
      g_ambushUnitApplied = enable;
  }

  // ══════════════════════════════════════════════════════
  //  대군사 (Grand Strategist)
  // ══════════════════════════════════════════════════════
  static bool g_grandStrategistApplied = false;
  void SetGrandStrategist(bool enable) {
      AddLog(enable ? u8"[대군사] 활성화" : u8"[대군사] 비활성화");
      g_grandStrategistApplied = enable;
  }

  // ── 런타임 갱신 ──────────────────────────────────────
  // 전장에 배치된 무장들을 스캔하여 특수 능력을 활성화/비활성화합니다.
  struct STarget { uintptr_t off; uint8_t normal; uint8_t active; };

  void UpdateSpecialAbilities(int unitCountTotal, uintptr_t unitListBase, uintptr_t exeBase) {
      if (!exeBase) return;

      static DWORD lastUpdateTick = 0;
      DWORD currentTick = GetTickCount();
      // 전체 부대 스캔(무신 등)은 매우 무거우므로 500ms 주기로만 수행
      bool runGlobalScan = (unitCountTotal > 0 && currentTick - lastUpdateTick >= 500);
      if (runGlobalScan) lastUpdateTick = currentTick;

      bool hasGunakdae = false;
      bool hasMussang = false;
      bool hasFireCavalry = false;
      bool hasArcher = false;
      bool hasCommander = false;
      bool hasSneakAttack = false;
      bool hasTactician = false;

      // 1. 현재 선택/행동 중인 부대(Active Unit) 스캔 (200ms 스로틀)
      static DWORD lastActiveScanTick = 0;
      if (currentTick - lastActiveScanTick >= 200) {
          lastActiveScanTick = currentTick;
          uintptr_t activeUnitPtr = SAResolveChain(exeBase + 0x03510578, { 0x100, 0x78, 0, 0x50, 0x108, 0x50, 0x8, 0 });
          if (activeUnitPtr) {
              uintptr_t memberPtr = *(uintptr_t*)(activeUnitPtr + 0x18);
              if (IsValidPtr(memberPtr, 0x100)) {
                  for (uintptr_t moff : { (uintptr_t)0x08, (uintptr_t)0x10, (uintptr_t)0x18 }) {
                      uintptr_t offPtr = *(uintptr_t*)(memberPtr + moff);
                      if (IsValidPtr(offPtr, 0x10)) {
                          int officerID = (int)(*(unsigned short*)(offPtr + 0x08));
                          if (officerID > 0) {
                              if (GetTargetSkillCount(officerID, 0x1000) > 0) hasGunakdae = true;
                              if (GetTargetSkillCount(officerID, 0x1001) > 0) hasMussang = true;
                              if (GetTargetSkillCount(officerID, 0x1002) > 0) hasFireCavalry = true;
                              if (GetTargetSkillCount(officerID, 0x1003) > 0) hasArcher = true;
                              if (GetTargetSkillCount(officerID, 0x1005) > 0) hasCommander = true;
                              if (GetTargetSkillCount(officerID, 0x1006) > 0) hasSneakAttack = true;
                              if (GetTargetSkillCount(officerID, 0x1007) > 0) hasTactician = true;
                          }
                      }
                  }
              }
          }
      }

      static bool s_lastGunakdaeState = false;
      static bool s_lastMussangState = false;
      static bool s_lastFireCavalryState = false;
      static bool s_lastArcherState = false;
      static bool s_lastCommanderState = false;
      static bool s_lastSneakAttackState = false;
      static bool s_lastTacticianState = false;
      
      bool stateChangedGunakdae = (hasGunakdae != s_lastGunakdaeState);
      bool stateChangedMussang = (hasMussang != s_lastMussangState);
      bool stateChangedFireCavalry = (hasFireCavalry != s_lastFireCavalryState);
      bool stateChangedArcher = (hasArcher != s_lastArcherState);
      bool stateChangedCommander = (hasCommander != s_lastCommanderState);
      bool stateChangedSneakAttack = (hasSneakAttack != s_lastSneakAttackState);
      bool stateChangedTactician = (hasTactician != s_lastTacticianState);
      
      if (stateChangedGunakdae) AddLog(u8"[특수기능] 군악대 배치 스캔 방금 됨 -> %s", hasGunakdae ? u8"활성(ON)" : u8"비활성(OFF)");
      if (stateChangedMussang) AddLog(u8"[특수기능] 무쌍보병 배치 스캔 방금 됨 -> %s", hasMussang ? u8"활성(ON)" : u8"비활성(OFF)");
      if (stateChangedFireCavalry) AddLog(u8"[특수기능] 불꽃기병 배치 스캔 방금 됨 -> %s", hasFireCavalry ? u8"활성(ON)" : u8"비활성(OFF)");
      if (stateChangedArcher) AddLog(u8"[특수기능] 원격궁병 배치 스캔 방금 됨 -> %s", hasArcher ? u8"활성(ON)" : u8"비활성(OFF)");
      if (stateChangedCommander) AddLog(u8"[특수기능] 총사령관 배치 스캔 방금 됨 -> %s", hasCommander ? u8"활성(ON)" : u8"비활성(OFF)");
      if (stateChangedSneakAttack) AddLog(u8"[특수기능] 기습부대 배치 스캔 방금 됨 -> %s", hasSneakAttack ? u8"활성(ON)" : u8"비활성(OFF)");
      if (stateChangedTactician) AddLog(u8"[특수기능] 대군사 배치 스캔 방금 됨 -> %s", hasTactician ? u8"활성(ON)" : u8"비활성(OFF)");

      // 인젝터 유틸리티 람다 (공용)
      auto applyBuffTargets = [&](uintptr_t baseAddr, const char* name, bool isActive, bool isChanged, STarget* targets, int count) {
          bool allSuccess = true;
          DWORD lastErr = 0;
          uintptr_t failedAddr = 0;

          for (int i=0; i<count; ++i) {
              auto& t = targets[i];
              uintptr_t targetAddr = baseAddr + t.off;
              uint8_t targetVal = isActive ? t.active : t.normal;

              // 성능 최적화: 현재 값이 이미 목표값과 같으면 VirtualProtect 및 쓰기 건너뜀
              if (*(uint8_t*)targetAddr == targetVal) continue;

              DWORD old;
              if (VirtualProtect((LPVOID)targetAddr, 1, PAGE_EXECUTE_READWRITE, &old)) {
                  *(uint8_t*)targetAddr = targetVal;
                  VirtualProtect((LPVOID)targetAddr, 1, old, &old);
              } else {
                  allSuccess = false;
                  lastErr = GetLastError();
                  failedAddr = targetAddr;
                  break;
              }
          }
          if (isChanged) {
              if (allSuccess) {
                  AddLog(u8"[특수기능] %s 수치 주입 완료! (베이스: 0x%llX)", name, baseAddr);
              } else {
                  AddLog(u8"[특수기능] 주입 오류: %s 주소 0x%llX VP실패 (코드: %lu)", name, failedAddr, lastErr);
              }
          }
      };

      // 2. 버프 메모리 주입 (기존 0x02ED7A10 체인 그룹)
      static DWORD lastBuffInjectionTick = 0;
      if (exeBase && currentTick - lastBuffInjectionTick >= 500) {
          lastBuffInjectionTick = currentTick;
          uintptr_t p = SAResolveChain(exeBase + 0x02ED7A10, { 0x110, 0x120, 0x40, 0x168, 0 });
          if (p) {
              // --- (A) 군악대 타겟 (분기, 고무) ---
              STarget gunakdaeTargets[] = {
                  { 0xEB2, 10, 15 }, { 0xED4, 15, 20 }, { 0xEFC, 1, 9 },
                  { 0xF32, 5, 10 }, { 0xF54, 10, 15 }, { 0xF7C, 1, 9 }
              };
              applyBuffTargets(p, u8"군악대", hasGunakdae, stateChangedGunakdae, gunakdaeTargets, sizeof(gunakdaeTargets)/sizeof(STarget));

              // --- (B) 무쌍 보명 타겟 (강격 관통 및 맹돌 훨윈드) ---
              STarget mussangTargets[] = {
                  { 0x238, 1, 2 }, { 0x25A, 1, 2 }, { 0x27C, 1, 2 },
                  { 0x3B8, 1, 5 }, { 0x3C6, 1, 5 }, { 0x3DA, 1, 5 },
                  { 0x3E8, 1, 5 }, { 0x3FC, 1, 5 }, { 0x40A, 1, 5 }
              };
              applyBuffTargets(p, u8"무쌍보병", hasMussang, stateChangedMussang, mussangTargets, sizeof(mussangTargets)/sizeof(STarget));

              // --- (C) 불꽃기병 타겟 (연격, 기사 불지르기) ---
              STarget fireCavalryTargets[] = {
                  // 연격 불지르기
                  { 0x4BC, 0, 15 }, { 0x4BE, 0, 1 }, { 0x4C6, 0, 1 }, { 0x4C7, 0, 50 },
                  { 0x4DE, 0, 15 }, { 0x4E0, 0, 1 }, { 0x4E8, 0, 1 }, { 0x4E9, 0, 70 },
                  { 0x500, 0, 15 }, { 0x502, 0, 1 }, { 0x50A, 0, 1 }, { 0x50B, 0, 100 },
                  // 기사 불지르기
                  { 0x63C, 0, 15 }, { 0x63E, 0, 1 }, { 0x646, 0, 1 }, { 0x647, 0, 50 },
                  { 0x65E, 0, 15 }, { 0x660, 0, 1 }, { 0x668, 0, 1 }, { 0x669, 0, 70 },
                  { 0x680, 0, 15 }, { 0x682, 0, 1 }, { 0x68A, 0, 1 }, { 0x68B, 0, 100 }
              };
              applyBuffTargets(p, u8"불꽃기병", hasFireCavalry, stateChangedFireCavalry, fireCavalryTargets, sizeof(fireCavalryTargets)/sizeof(STarget));

              // --- (D) 원격 궁병 타겟 (사거리 보정) ---
              STarget archerTargets[] = {
                  // 제사,난사,화시,원사 최대 사거리+1
                  { 0x72C, 2, 3 }, { 0x74E, 2, 3 }, { 0x770, 2, 3 },
                  { 0x7AC, 2, 3 }, { 0x7CE, 2, 3 }, { 0x7F0, 2, 3 },
                  { 0x82C, 2, 3 }, { 0x84E, 2, 3 }, { 0x870, 2, 3 },
                  // 시람은, 최소 사거리-1, 최대 사거리+1
                  { 0x8AC, 3, 4 }, { 0x8CE, 3, 4 }, { 0x8F0, 3, 4 },
                  { 0x92B, 3, 2 }, { 0x94D, 3, 2 }, { 0x96F, 3, 2 },
                  { 0x92C, 3, 4 }, { 0x94E, 3, 4 }, { 0x970, 3, 4 }
              };
              applyBuffTargets(p, u8"원격궁병", hasArcher, stateChangedArcher, archerTargets, sizeof(archerTargets)/sizeof(STarget));

              // --- (F) 기습부대 타겟 (상태이상기 확률 마개조) ---
              STarget sneakAttackTargets[] = {
                  // 교란 확률 증가 1,2,3레벨
                  { 0x347, 10, 50 }, { 0x369, 10, 70 }, { 0x38B, 10, 100 },
                  // 급습 확률 증가 1,2,3레벨
                  { 0x5C7, 10, 50 }, { 0x5E9, 10, 70 }, { 0x60B, 10, 100 },
                  // 요격 확률 증가 1,2,3레벨
                  { 0xDC7, 10, 50 }, { 0xDE9, 10, 70 }, { 0xE0B, 10, 100 }
              };
              applyBuffTargets(p, u8"기습부대", hasSneakAttack, stateChangedSneakAttack, sneakAttackTargets, sizeof(sneakAttackTargets)/sizeof(STarget));

              // --- (G) 대군사 타겟 (전법 위력 및 범위 마개조) ---
              STarget tacticianTargets[] = {
                  // 열화 범위 증가
                  { 0xC38, 2, 3 }, { 0xC46, 2, 3 }, { 0xC5A, 2, 3 }, { 0xC68, 2, 3 }, { 0xC7C, 2, 3 }, { 0xC8A, 2, 3 },
                  // 격류 범위 증가
                  { 0xCB8, 5, 6 }, { 0xCDA, 5, 6 }, { 0xCFC, 5, 6 },
                  // 낙석 범위 증가
                  { 0xD38, 4, 8 }, { 0xD5A, 4, 8 }, { 0xD7C, 4, 8 },
                  // 요격 범위 증가
                  { 0xDB8, 1, 5 }, { 0xDC6, 1, 5 }, { 0xDDA, 1, 5 }, { 0xDE8, 1, 5 }, { 0xDFC, 1, 5 }, { 0xE0A, 1, 5 }
              };
              applyBuffTargets(p, u8"대군사", hasTactician, stateChangedTactician, tacticianTargets, sizeof(tacticianTargets)/sizeof(STarget));

          } else {
              if (stateChangedGunakdae || stateChangedMussang || stateChangedFireCavalry || stateChangedArcher || stateChangedSneakAttack || stateChangedTactician) {
                  AddLog(u8"[특수기능] 실패: 마스터 데이터 주소 포인터(0x02ED7A10 체인) 찾지 못함");
              }
          }

          // 2-2. 총사령관 버프 메모리 주입 (새로운 0x034C8630 체인 그룹)
          uintptr_t p2 = SAResolveChain(exeBase + 0x034C8630, { 0x0, 0x8, 0x10, 0x0, 0 });
          if (p2) {
              // --- (E) 총사령관 타겟 ---
              STarget commanderTargets[] = {
                  { 0x4B2F0C, 0, 1 }, { 0x4B2F34, 0, 1 }, { 0x4B2F5C, 0, 1 }, { 0x4B2F84, 0, 1 },
                  { 0x4B2FAC, 0, 1 }, { 0x4B2FD4, 0, 1 }, { 0x4B2FFC, 0, 1 }, { 0x4B3024, 0, 1 }
              };
              applyBuffTargets(p2, u8"총사령관", hasCommander, stateChangedCommander, commanderTargets, sizeof(commanderTargets)/sizeof(STarget));
          } else {
              if (stateChangedCommander) {
                  AddLog(u8"[특수기능] 실패: 총사령관 데이터 주소 포인터(0x034C8630 체인) 찾지 못함");
              }
          }
      }

      // 3. 등갑군 (턴 시작 시 화염 디버프 추가 피해)
      // 이 로직은 매 프레임 스캔하되, '현재 행동 중인 부대'가 변경되었을 때 1회만 발동
      static int s_prevBurnLeader = -1;
      
      // 전투가 종료되면(파라미터가 0으로 들어옴) 리더 트래커 초기화
      static DWORD lastBurnTick = 0;
      if (unitCountTotal == 0) {
          s_prevBurnLeader = -1;
      } else if (exeBase && currentTick - lastBurnTick >= 300) {
          lastBurnTick = currentTick;
          uintptr_t activeUnitPtr = SAResolveChain(exeBase + 0x03510578, { 0x100, 0x78, 0, 0x50, 0x108, 0x50, 0x8, 0 });
          if (activeUnitPtr) {
              uintptr_t memberPtr = *(uintptr_t*)(activeUnitPtr + 0x18);
              if (IsValidPtr(memberPtr, 0x100)) {
                  int leaderID = -1;
                  uintptr_t offPtr = *(uintptr_t*)(memberPtr + 0x08); // 부대장
                  if (IsValidPtr(offPtr, 0x10)) {
                      leaderID = (int)(*(unsigned short*)(offPtr + 0x08));
                  }

                  if (leaderID > 0 && leaderID != s_prevBurnLeader) {
                      s_prevBurnLeader = leaderID;

                      // 등갑군 스킬 검사 (부대장, 부장 모두)
                      bool isDeunggab = false;
                      for (uintptr_t moff : { (uintptr_t)0x08, (uintptr_t)0x10, (uintptr_t)0x18 }) {
                          uintptr_t subOff = *(uintptr_t*)(memberPtr + moff);
                          if (IsValidPtr(subOff, 0x10)) {
                              int subID = (int)(*(unsigned short*)(subOff + 0x08));
                              if (subID > 0 && GetTargetSkillCount(subID, 0x1004) > 0) {
                                  isDeunggab = true;
                                  break;
                              }
                          }
                      }

                      if (isDeunggab) {
                          // 화염 상태(Burn) 확인 로직
                          bool isBurning = false;
                          uintptr_t ptr40 = *(uintptr_t*)(activeUnitPtr + 0x40);
                          if (IsValidPtr(ptr40, 0x40)) {
                              uint8_t burnFlag = *(uint8_t*)(ptr40 + 0x38);
                              if (burnFlag == 1) isBurning = true;
                          }
                          uint8_t ab1 = *(uint8_t*)(activeUnitPtr + 0x238);
                          uint8_t ab2 = *(uint8_t*)(activeUnitPtr + 0x270);
                          if (ab1 == 9 || ab2 == 9) isBurning = true;

                          if (isBurning) {
                              // 병력 및 전의 감소
                              uint16_t troop = *(uint16_t*)(activeUnitPtr + 0x38);
                              uint8_t morale = *(uint8_t*)(activeUnitPtr + 0x80);

                              int newTroop = troop - 1000;
                              if (newTroop < 100) newTroop = 100; // 최소 병력 100
                              *(uint16_t*)(activeUnitPtr + 0x38) = (uint16_t)newTroop;

                              int newMorale = morale - 10;
                              if (newMorale < 0) newMorale = 0; // 최소 전의 0
                              *(uint8_t*)(activeUnitPtr + 0x80) = (uint8_t)newMorale;

                              AddLog(u8"[특수기능] 등갑군(무장 %d 부대) 화염 취약 패널티 발동! (병력 -1000, 전의 -10)", leaderID);
                          }
                      }
                  }
              }
          }
      }

      // 4. 무신 (부대 전용 - 해당 장수 소속 부대의 전법 병종 제약 해제)
      // [성능 최적화] 500ms 주기로만 실행하여 CPU 점유율 대폭 완화
      if (runGlobalScan && unitCountTotal > 0 && unitListBase > 0) {
          const uintptr_t SKILL_PTR_OFF   = 0x5D8;
          const uintptr_t SKILL_REC_SIZE  = 0x28;
          const uintptr_t SKILL_LIMIT_OFF = 0x14; // 병종 제한 필드
          const uintptr_t INDEX_OFF       = 0x0C;
          const int       MAX_SKILLS      = 45;

          int totalChanged = 0;
          for (int i = 0; i < unitCountTotal; i++) {
              uintptr_t unitData = *(uintptr_t*)(unitListBase + 0x08 + i * 0x10);
              if (!IsValidPtr(unitData, 0x600)) continue;

              uintptr_t memberPtr = *(uintptr_t*)(unitData + 0x18);
              if (!IsValidPtr(memberPtr, 0x100)) continue;

              // 무신(0x1008) 체크
              bool hasMusin = false;
              int leaderID = 0; // 로깅을 위한 부대장 ID
              for (uintptr_t moff : { (uintptr_t)0x08, (uintptr_t)0x10, (uintptr_t)0x18 }) {
                  uintptr_t offPtr = *(uintptr_t*)(memberPtr + moff);
                  if (IsValidPtr(offPtr, 0x10)) {
                      int subID = (int)(*(unsigned short*)(offPtr + 0x08));
                      if (moff == 0x08) leaderID = subID;
                      if (subID > 0 && GetTargetSkillCount(subID, 0x1008) > 0) {
                          hasMusin = true;
                      }
                  }
              }

              if (!hasMusin) continue;

              // 전법 레코드 순회 → 제약이 걸려있으면(미해제) 실시간 해제
              uintptr_t skillStart = *(uintptr_t*)(unitData + SKILL_PTR_OFF);
              if (!IsValidPtr(skillStart, SKILL_REC_SIZE)) continue;

              int skillCount = 0;
              for (int j = 0; j < MAX_SKILLS; j++) {
                  uintptr_t skillRec = skillStart + j * SKILL_REC_SIZE;
                  uint32_t recIdx = *(uint32_t*)(skillRec + INDEX_OFF);
                  if ((int)recIdx != j) break;
                  uint8_t firstByte = *(uint8_t*)(skillRec);
                  if (firstByte != 0x58 && firstByte != 0xD8) break;

                  uintptr_t limitAddr = skillRec + SKILL_LIMIT_OFF;
                  if (*(uint8_t*)limitAddr != 0) {
                      DWORD old;
                      if (VirtualProtect((LPVOID)limitAddr, 1, PAGE_EXECUTE_READWRITE, &old)) {
                          *(uint8_t*)limitAddr = 0;
                          VirtualProtect((LPVOID)limitAddr, 1, old, &old);
                          skillCount++;
                      }
                  }
              }

              if (skillCount > 0) {
                  AddLog(u8"[특수기능] 무신: 부대장 %d 전법 %d개 병종 제약 지속 해제적용", leaderID, skillCount);
              }
          }
      }

      s_lastGunakdaeState = hasGunakdae;
      s_lastMussangState = hasMussang;
      s_lastFireCavalryState = hasFireCavalry;
      s_lastArcherState = hasArcher;
      s_lastCommanderState = hasCommander;
      s_lastSneakAttackState = hasSneakAttack;
      s_lastTacticianState = hasTactician;
  }

} // namespace DX11Base
