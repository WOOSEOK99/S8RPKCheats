// ======================================================
// 특수 능력 (Special Unit Abilities) - 구현 뼈대
// ======================================================
#include "SpecialAbility.h"
#include "../../Cheats.h"
#include "../../MenuState.h"
#include "../../pch.h"
#include "../../showlog.h"

#include <vector>

#include "../System/SkillCountManager.h"

namespace DX11Base {

  // ── 공용 헬퍼 ──────────────────────────────────────
  static uintptr_t SAResolveChain(uintptr_t base, std::initializer_list<int> offsets) {
    uintptr_t current = base;
    for (int offset : offsets) {
      if (!IsValidPtr(current, 8)) return 0;
      uintptr_t next = *(uintptr_t *)current;
      if (!next) return 0;
      current = next + offset;
    }
    return current;
  }

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
  struct STarget {
    uintptr_t off;
    uint8_t normal;
    uint8_t active;
  };

  // ══════════════════════════════════════════════════════
  //  [캐싱 아키텍처]
  //  전투 진입 시 1회 스캔 → g_BattleUnits 벡터에 부대별 특기 플래그 저장
  //  실시간 루프에서는 현재 활성 부대 주소만 읽어 O(n) 매칭 → 초경량 실행
  // ══════════════════════════════════════════════════════

  // 부대 1개의 고유 주소 + 특기 플래그 묶음
  struct CachedUnit {
    uintptr_t unitAddress;  // unitListBase에서 읽은 unitData 포인터 (키)
    int       leaderID;     // 부대장 무장 ID (로깅/등갑군용)
    bool hasGunakdae;       // 군악대   0x1000
    bool hasMussang;        // 무쌍보병 0x1001
    bool hasFireCavalry;    // 불꽃기병 0x1002
    bool hasArcher;         // 원격궁병 0x1003
    bool hasDeunggab;       // 등갑군   0x1004
    bool hasCommander;      // 총사령관 0x1005
    bool hasSneakAttack;    // 기습부대 0x1006
    bool hasTactician;      // 대군사   0x1007
    bool hasMusin;          // 무신     0x1008
  };

  static std::vector<CachedUnit> g_BattleUnits;
  static bool g_CacheReady = false;

  // ──────────────────────────────────────────────────────
  // InitializeBattleCache — 전투 진입 시 1회 호출
  // ──────────────────────────────────────────────────────
  void InitializeBattleCache(int unitCountTotal, uintptr_t unitListBase, uintptr_t exeBase) {
    g_BattleUnits.clear();
    g_CacheReady = false;
    if (!exeBase || unitCountTotal <= 0 || !unitListBase) return;
    g_BattleUnits.reserve(unitCountTotal);

    for (int i = 0; i < unitCountTotal; i++) {
      if (!IsValidPtr(unitListBase + 0x08 + (uintptr_t)i * 0x10, 8)) continue;
      uintptr_t unitData = *(uintptr_t *)(unitListBase + 0x08 + (uintptr_t)i * 0x10);
      if (!IsValidPtr(unitData, 0x600)) continue;
      if (!IsValidPtr(unitData + 0x18, 8)) continue;
      uintptr_t memberPtr = *(uintptr_t *)(unitData + 0x18);
      if (!IsValidPtr(memberPtr, 0x100)) continue;

      CachedUnit cu = {};
      cu.unitAddress = unitData;

      for (uintptr_t moff : {(uintptr_t)0x08, (uintptr_t)0x10, (uintptr_t)0x18}) {
        if (!IsValidPtr(memberPtr + moff, 8)) continue;
        uintptr_t offPtr = *(uintptr_t *)(memberPtr + moff);
        if (!IsValidPtr(offPtr, 0x10)) continue;
        int id = (int)(*(unsigned short *)(offPtr + 0x08));
        if (id <= 0) continue;
        if (moff == 0x08) cu.leaderID = id;
        if (GetTargetSkillCount(id, 0x1000) > 0) cu.hasGunakdae    = true;
        if (GetTargetSkillCount(id, 0x1001) > 0) cu.hasMussang     = true;
        if (GetTargetSkillCount(id, 0x1002) > 0) cu.hasFireCavalry = true;
        if (GetTargetSkillCount(id, 0x1003) > 0) cu.hasArcher      = true;
        if (GetTargetSkillCount(id, 0x1004) > 0) cu.hasDeunggab    = true;
        if (GetTargetSkillCount(id, 0x1005) > 0) cu.hasCommander   = true;
        if (GetTargetSkillCount(id, 0x1006) > 0) cu.hasSneakAttack = true;
        if (GetTargetSkillCount(id, 0x1007) > 0) cu.hasTactician   = true;
        if (GetTargetSkillCount(id, 0x1008) > 0) cu.hasMusin       = true;
      }
      g_BattleUnits.push_back(cu);
    }
    g_CacheReady = true;
    AddLog(u8"[캐시] 전투 부대 캐시 빌드 완료: %d개", (int)g_BattleUnits.size());
  }

  // ──────────────────────────────────────────────────────
  // ClearBattleCache — 전투 종료 시 호출
  // ──────────────────────────────────────────────────────
  void ClearBattleCache() {
    g_BattleUnits.clear();
    g_CacheReady = false;
    AddLog(u8"[캐시] 전투 부대 캐시 해제");
  }

  // ──────────────────────────────────────────────────────
  // ScanActiveUnitAbilities — 캐시 매칭 버전 (초경량)
  //  체인 7단계로 ptrToUnitData 획득 → *(uintptr_t*)ptrToUnitData = activeUnitData
  //  g_BattleUnits에서 주소 매칭 → 플래그 즉시 대입, GetTargetSkillCount 호출 없음
  // ──────────────────────────────────────────────────────
  static uintptr_t ScanActiveUnitAbilities(uintptr_t exeBase,
                                           bool &hasGunakdae, bool &hasMussang,
                                           bool &hasFireCavalry, bool &hasArcher,
                                           bool &hasCommander, bool &hasSneakAttack,
                                           bool &hasTactician) {
    if (!g_CacheReady || g_BattleUnits.empty()) return 0;

    // SAResolveChain은 매 스텝에서 *(current) + offset 순으로 연산.
    // 마지막 오프셋 0은 "역참조 후 +0" → 최종 역참조를 수행하여
    // activeUnitData 값 자체를 직접 반환함. 반드시 유지해야 함.
    uintptr_t activeUnitData = SAResolveChain(
      exeBase + 0x03510578, {0x100, 0x78, 0, 0x50, 0x108, 0x50, 0x8, 0});
    if (!activeUnitData) return 0;

    for (const auto &cu : g_BattleUnits) {
      if (cu.unitAddress == activeUnitData) {
        hasGunakdae    = cu.hasGunakdae;
        hasMussang     = cu.hasMussang;
        hasFireCavalry = cu.hasFireCavalry;
        hasArcher      = cu.hasArcher;
        hasCommander   = cu.hasCommander;
        hasSneakAttack = cu.hasSneakAttack;
        hasTactician   = cu.hasTactician;
        return activeUnitData; // 등갑군 섹션에서 재사용
      }
    }
    return 0;
  }

  void UpdateSpecialAbilities(int unitCountTotal, uintptr_t unitListBase, uintptr_t exeBase) {
    if (!exeBase)
      return;

    // ── 타이머 통합 ────────────────────────────────────────
    // 상위 BattleMonitor의 s_tickPhase 스케줄러가 이 함수를 ~300ms 주기로
    // 호출하므로, 여기서는 500ms 주기인 전체 스캔(무신 등)의 진입 여부만
    // 하나의 타이머로 통제합니다. 버프 주입도 동일 조건(runGlobalScan)을
    // 재사용하여 중복 타이머 및 600ms 밀림 현상을 제거합니다.
    // ──────────────────────────────────────────────────────
    static DWORD lastUpdateTick = 0;
    DWORD currentTick = GetTickCount();

    // 전체 부대 스캔 + 버프 주입: 500ms 주기
    bool runGlobalScan = (unitCountTotal > 0 && currentTick - lastUpdateTick >= 500);
    if (runGlobalScan)
      lastUpdateTick = currentTick;

    bool hasGunakdae = false;
    bool hasMussang = false;
    bool hasFireCavalry = false;
    bool hasArcher = false;
    bool hasCommander = false;
    bool hasSneakAttack = false;
    bool hasTactician = false;

    // 1. 현재 선택/행동 중인 부대 스캔 (캐시 매칭, GetTargetSkillCount 호출 없음)
    //    반환값: 현재 활성 unitData 주소 (등갑군 섹션에서 재사용)
    uintptr_t activeUnitData = ScanActiveUnitAbilities(
      exeBase, hasGunakdae, hasMussang, hasFireCavalry, hasArcher, hasCommander,
      hasSneakAttack, hasTactician);

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

    if (stateChangedGunakdae)
      AddLog(u8"[특수기능] 군악대 배치 스캔 방금 됨 -> %s", hasGunakdae ? u8"활성(ON)" : u8"비활성(OFF)");
    if (stateChangedMussang)
      AddLog(u8"[특수기능] 무쌍보병 배치 스캔 방금 됨 -> %s", hasMussang ? u8"활성(ON)" : u8"비활성(OFF)");
    if (stateChangedFireCavalry)
      AddLog(u8"[특수기능] 불꽃기병 배치 스캔 방금 됨 -> %s", hasFireCavalry ? u8"활성(ON)" : u8"비활성(OFF)");
    if (stateChangedArcher)
      AddLog(u8"[특수기능] 원격궁병 배치 스캔 방금 됨 -> %s", hasArcher ? u8"활성(ON)" : u8"비활성(OFF)");
    if (stateChangedCommander)
      AddLog(u8"[특수기능] 총사령관 배치 스캔 방금 됨 -> %s", hasCommander ? u8"활성(ON)" : u8"비활성(OFF)");
    if (stateChangedSneakAttack)
      AddLog(u8"[특수기능] 기습부대 배치 스캔 방금 됨 -> %s", hasSneakAttack ? u8"활성(ON)" : u8"비활성(OFF)");
    if (stateChangedTactician)
      AddLog(u8"[특수기능] 대군사 배치 스캔 방금 됨 -> %s", hasTactician ? u8"활성(ON)" : u8"비활성(OFF)");

    // 상태가 변경되었으면 타이머(runGlobalScan)를 기다리지 않고 즉시 버프를 주입합니다.
    bool injectBuffs = (runGlobalScan || stateChangedGunakdae || stateChangedMussang || stateChangedFireCavalry ||
                        stateChangedArcher || stateChangedCommander || stateChangedSneakAttack || stateChangedTactician);

    // 인젝터 유틸리티 람다 (공용)
    auto applyBuffTargets = [&](uintptr_t baseAddr, const char *name, bool isActive, bool isChanged, STarget *targets,
                                int count) {
      bool allSuccess = true;
      DWORD lastErr = 0;
      uintptr_t failedAddr = 0;

      for (int i = 0; i < count; ++i) {
        auto &t = targets[i];
        uintptr_t targetAddr = baseAddr + t.off;
        uint8_t targetVal = isActive ? t.active : t.normal;

        // 성능 최적화: 현재 값이 이미 목표값과 같으면 VirtualProtect 및 쓰기 건너뜀
        if (*(uint8_t *)targetAddr == targetVal)
          continue;

        DWORD old;
        if (VirtualProtect((LPVOID)targetAddr, 1, PAGE_EXECUTE_READWRITE, &old)) {
          *(uint8_t *)targetAddr = targetVal;
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
    //    전투 진입 후 1회 해소 후 재사용 — 상태 변경 시마다 5단계 체인 해소 제거
    if (exeBase && injectBuffs) {
      static uintptr_t s_cachedP  = 0;
      static uintptr_t s_cachedP2 = 0;
      if (!s_cachedP)
        s_cachedP  = SAResolveChain(exeBase + 0x02ED7A10, {0x110, 0x120, 0x40, 0x168, 0});
      if (!s_cachedP2)
        s_cachedP2 = SAResolveChain(exeBase + 0x034C8630, {0x0, 0x8, 0x10, 0x0, 0});
      uintptr_t p  = s_cachedP;
      uintptr_t p2 = s_cachedP2;
      if (p) {
        STarget gunakdaeTargets[] = {{0xEB2, 10, 15}, {0xED4, 15, 20}, {0xEFC, 1, 9},
                                     {0xF32, 5, 10},  {0xF54, 10, 15}, {0xF7C, 1, 9}};
        applyBuffTargets(p, u8"군악대", hasGunakdae, stateChangedGunakdae, gunakdaeTargets,
                         sizeof(gunakdaeTargets) / sizeof(STarget));

        // --- (B) 무쌍 보명 타겟 (강격 관통 및 맹돌 훨윈드) ---
        STarget mussangTargets[] = {{0x238, 1, 2}, {0x25A, 1, 2}, {0x27C, 1, 2}, {0x3B8, 1, 5}, {0x3C6, 1, 5},
                                    {0x3DA, 1, 5}, {0x3E8, 1, 5}, {0x3FC, 1, 5}, {0x40A, 1, 5}};
        applyBuffTargets(p, u8"무쌍보병", hasMussang, stateChangedMussang, mussangTargets,
                         sizeof(mussangTargets) / sizeof(STarget));

        // --- (C) 불꽃기병 타겟 (연격, 기사 불지르기) ---
        STarget fireCavalryTargets[] = {// 연격 불지르기
                                        {0x4BC, 0, 15},
                                        {0x4BE, 0, 1},
                                        {0x4C6, 0, 1},
                                        {0x4C7, 0, 50},
                                        {0x4DE, 0, 15},
                                        {0x4E0, 0, 1},
                                        {0x4E8, 0, 1},
                                        {0x4E9, 0, 70},
                                        {0x500, 0, 15},
                                        {0x502, 0, 1},
                                        {0x50A, 0, 1},
                                        {0x50B, 0, 100},
                                        // 기사 불지르기
                                        {0x63C, 0, 15},
                                        {0x63E, 0, 1},
                                        {0x646, 0, 1},
                                        {0x647, 0, 50},
                                        {0x65E, 0, 15},
                                        {0x660, 0, 1},
                                        {0x668, 0, 1},
                                        {0x669, 0, 70},
                                        {0x680, 0, 15},
                                        {0x682, 0, 1},
                                        {0x68A, 0, 1},
                                        {0x68B, 0, 100}};
        applyBuffTargets(p, u8"불꽃기병", hasFireCavalry, stateChangedFireCavalry, fireCavalryTargets,
                         sizeof(fireCavalryTargets) / sizeof(STarget));

        // --- (D) 원격 궁병 타겟 (사거리 보정) ---
        STarget archerTargets[] = {// 제사,난사,화시,원사 최대 사거리+1
                                   {0x72C, 2, 3},
                                   {0x74E, 2, 3},
                                   {0x770, 2, 3},
                                   {0x7AC, 2, 3},
                                   {0x7CE, 2, 3},
                                   {0x7F0, 2, 3},
                                   {0x82C, 2, 3},
                                   {0x84E, 2, 3},
                                   {0x870, 2, 3},
                                   // 시람은, 최소 사거리-1, 최대 사거리+1
                                   {0x8AC, 3, 4},
                                   {0x8CE, 3, 4},
                                   {0x8F0, 3, 4},
                                   {0x92B, 3, 2},
                                   {0x94D, 3, 2},
                                   {0x96F, 3, 2},
                                   {0x92C, 3, 4},
                                   {0x94E, 3, 4},
                                   {0x970, 3, 4}};
        applyBuffTargets(p, u8"원격궁병", hasArcher, stateChangedArcher, archerTargets,
                         sizeof(archerTargets) / sizeof(STarget));

        // --- (F) 기습부대 타겟 (상태이상기 확률 마개조) ---
        STarget sneakAttackTargets[] = {// 교란 확률 증가 1,2,3레벨
                                        {0x347, 10, 50},
                                        {0x369, 10, 70},
                                        {0x38B, 10, 100},
                                        // 급습 확률 증가 1,2,3레벨
                                        {0x5C7, 10, 50},
                                        {0x5E9, 10, 70},
                                        {0x60B, 10, 100},
                                        // 요격 확률 증가 1,2,3레벨
                                        {0xDC7, 10, 50},
                                        {0xDE9, 10, 70},
                                        {0xE0B, 10, 100}};
        applyBuffTargets(p, u8"기습부대", hasSneakAttack, stateChangedSneakAttack, sneakAttackTargets,
                         sizeof(sneakAttackTargets) / sizeof(STarget));

        // --- (G) 대군사 타겟 (전법 위력 및 범위 마개조) ---
        STarget tacticianTargets[] = {// 열화 범위 증가
                                      {0xC38, 2, 3},
                                      {0xC46, 2, 3},
                                      {0xC5A, 2, 3},
                                      {0xC68, 2, 3},
                                      {0xC7C, 2, 3},
                                      {0xC8A, 2, 3},
                                      // 격류 범위 증가
                                      {0xCB8, 5, 6},
                                      {0xCDA, 5, 6},
                                      {0xCFC, 5, 6},
                                      // 낙석 범위 증가
                                      {0xD38, 4, 8},
                                      {0xD5A, 4, 8},
                                      {0xD7C, 4, 8},
                                      // 요격 범위 증가
                                      {0xDB8, 1, 5},
                                      {0xDC6, 1, 5},
                                      {0xDDA, 1, 5},
                                      {0xDE8, 1, 5},
                                      {0xDFC, 1, 5},
                                      {0xE0A, 1, 5}};
        applyBuffTargets(p, u8"대군사", hasTactician, stateChangedTactician, tacticianTargets,
                         sizeof(tacticianTargets) / sizeof(STarget));

      } else {
        if (stateChangedGunakdae || stateChangedMussang || stateChangedFireCavalry || stateChangedArcher ||
            stateChangedSneakAttack || stateChangedTactician) {
          AddLog(u8"[특수기능] 실패: 마스터 데이터 주소 포인터(0x02ED7A10 체인) 찾지 못함");
        }
      }

      // 2-2. 총사령관 버프 메모리 주입 (캐시된 p2 재사용)
      if (p2) {
        // --- (E) 총사령관 타겟 ---
        STarget commanderTargets[] = {{0x4B2F0C, 0, 1}, {0x4B2F34, 0, 1}, {0x4B2F5C, 0, 1}, {0x4B2F84, 0, 1},
                                      {0x4B2FAC, 0, 1}, {0x4B2FD4, 0, 1}, {0x4B2FFC, 0, 1}, {0x4B3024, 0, 1}};
        applyBuffTargets(p2, u8"총사령관", hasCommander, stateChangedCommander, commanderTargets,
                         sizeof(commanderTargets) / sizeof(STarget));
      } else {
        if (stateChangedCommander) {
          AddLog(u8"[특수기능] 실패: 총사령관 데이터 주소 포인터(0x034C8630 체인) 찾지 못함");
        }
      }
    }

    // 3. 등갑군 (턴 시작 시 화염 디버프 추가 피해)
    //    [캐시 활용] GetTargetSkillCount 대신 cu.hasDeunggab 참조
    //    activeUnitData는 ScanActiveUnitAbilities 반환값 재사용
    static int s_prevBurnLeader = -1;
    static DWORD lastBurnTick = 0;

    if (unitCountTotal == 0) {
      s_prevBurnLeader = -1;
    } else if (g_CacheReady && activeUnitData && currentTick - lastBurnTick >= 300) {
      lastBurnTick = currentTick;

      // 캐시에서 현재 활성 부대 조회
      for (const auto &cu : g_BattleUnits) {
        if (cu.unitAddress != activeUnitData) continue;

        int leaderID = cu.leaderID;
        if (leaderID <= 0 || leaderID == s_prevBurnLeader) break;
        s_prevBurnLeader = leaderID;

        if (!cu.hasDeunggab) break;

        // 화염 상태(Burn) 확인 — activeUnitData 직접 사용
        if (!IsValidPtr(activeUnitData, 0x280)) break;
        bool isBurning = false;
        if (IsValidPtr(activeUnitData + 0x40, 8)) {
          uintptr_t ptr40 = *(uintptr_t *)(activeUnitData + 0x40);
          if (IsValidPtr(ptr40, 0x40) && *(uint8_t *)(ptr40 + 0x38) == 1)
            isBurning = true;
        }
        uint8_t ab1 = *(uint8_t *)(activeUnitData + 0x238);
        uint8_t ab2 = *(uint8_t *)(activeUnitData + 0x270);
        if (ab1 == 9 || ab2 == 9) isBurning = true;

        if (isBurning) {
          uint16_t troop  = *(uint16_t *)(activeUnitData + 0x38);
          uint8_t  morale = *(uint8_t  *)(activeUnitData + 0x80);
          int newTroop  = (troop  > 1100) ? (int)troop  - 1000 : 100;
          int newMorale = (morale >   10) ? (int)morale -   10 : 0;
          *(uint16_t *)(activeUnitData + 0x38) = (uint16_t)newTroop;
          *(uint8_t  *)(activeUnitData + 0x80) = (uint8_t )newMorale;
          AddLog(u8"[특수기능] 등갑군(무장 %d 부대) 화염 취약 패널티 발동! (병력 -1000, 전의 -10)", leaderID);
        }
        break;
      }
    }

    // 4. 무신 (부대 전용 — 전법 병종 제약 해제)
    //    [캐시 활용] GetTargetSkillCount 대신 cu.hasMusin 참조
    //    unitData는 cu.unitAddress 재사용 → unitListBase 순회 불필요
    if (runGlobalScan && g_CacheReady) {
      const uintptr_t SKILL_PTR_OFF  = 0x5D8;
      const uintptr_t SKILL_REC_SIZE = 0x28;
      const uintptr_t SKILL_LIMIT_OFF = 0x14;
      const uintptr_t INDEX_OFF       = 0x0C;
      const int MAX_SKILLS = 45;

      for (const auto &cu : g_BattleUnits) {
        if (!cu.hasMusin) continue;

        uintptr_t unitData = cu.unitAddress;
        if (!IsValidPtr(unitData, 0x600)) continue;

        if (!IsValidPtr(unitData + SKILL_PTR_OFF, 8)) continue;
        uintptr_t skillStart = *(uintptr_t *)(unitData + SKILL_PTR_OFF);
        if (!IsValidPtr(skillStart, SKILL_REC_SIZE)) continue;

        int skillCount = 0;
        for (int j = 0; j < MAX_SKILLS; j++) {
          uintptr_t skillRec = skillStart + j * SKILL_REC_SIZE;
          if (!IsValidPtr(skillRec + INDEX_OFF, 4)) break;
          if ((int)*(uint32_t *)(skillRec + INDEX_OFF) != j) break;
          uint8_t firstByte = *(uint8_t *)skillRec;
          if (firstByte != 0x58 && firstByte != 0xD8) break;

          uintptr_t limitAddr = skillRec + SKILL_LIMIT_OFF;
          if (!IsValidPtr(limitAddr, 1)) continue;
          if (*(uint8_t *)limitAddr != 0) {
            DWORD old;
            if (VirtualProtect((LPVOID)limitAddr, 1, PAGE_EXECUTE_READWRITE, &old)) {
              *(uint8_t *)limitAddr = 0;
              VirtualProtect((LPVOID)limitAddr, 1, old, &old);
              skillCount++;
            }
          }
        }
        if (skillCount > 0)
          AddLog(u8"[특수기능] 무신: 부대장 %d 전법 %d개 병종 제약 지속 해제", cu.leaderID, skillCount);
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
