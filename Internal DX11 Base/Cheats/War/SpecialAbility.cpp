// ======================================================
// 특수 능력 (Special Unit Abilities) - 구현 뼈대
// ======================================================
#include "SpecialAbility.h"
#include "../../Cheats.h"
#include "../../MenuState.h"
#include "../../pch.h"
#include "../../showlog.h"

#include <array>
#include <cstring>
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

  struct SARegionEntry {
    uintptr_t start = 0;
    uintptr_t end = 0;
    bool readable = false;
  };

  struct SARegionCache {
    static constexpr size_t kCapacity = 16;
    std::array<SARegionEntry, kCapacity> entries{};
    size_t next = 0;
  };

  // UpdateSpecialAbilities 1회 호출 안에서만 VirtualQuery 결과를 재사용합니다.
  // 호출 사이에는 보존하지 않으며, 영역 경계를 넘는 검사는 기존 IsValidPtr로 폴백합니다.
  static bool SAIsValidPtr(uintptr_t addr, SIZE_T size, SARegionCache &cache) {
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
    const bool regionEndValid = regionEnd >= regionStart;
    const bool readable = regionEndValid && mbi.State == MEM_COMMIT &&
                          !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));

    auto &slot = cache.entries[cache.next++ % SARegionCache::kCapacity];
    slot.start = regionStart;
    slot.end = regionEndValid ? regionEnd : regionStart;
    slot.readable = readable;

    if (!readable)
      return false;
    if (endAddr < regionEnd)
      return true;

    return IsValidPtr(addr, size);
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

  // Native callbacks handle one unit; no background roster is retained.
  struct UnitAbilities {
    int leaderID = 0;
    bool hasGunakdae = false;
    bool hasMussang = false;
    bool hasFireCavalry = false;
    bool hasArcher = false;
    bool hasDeunggab = false;
    bool hasCommander = false;
    bool hasSneakAttack = false;
    bool hasTactician = false;
    bool hasMusin = false;
  };

  // Keep fault-prone reads in a separate SEH scope with no settings lock.
  static bool ReadUnitOfficerIds(uintptr_t unitData, int (&ids)[3]) {
    __try {
      if (!IsValidPtr(unitData, 0x600))
        return false;

      const uintptr_t members = *reinterpret_cast<const uintptr_t *>(unitData + 0x18);
      if (!IsValidPtr(members, 0x20))
        return false;

      for (int i = 0; i < 3; ++i) {
        const uintptr_t officer = *reinterpret_cast<const uintptr_t *>(members + 8 + i * 8);
        if (officer && IsValidPtr(officer, 0x10))
          ids[i] = *reinterpret_cast<const uint16_t *>(officer + 8);
      }

      return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return false;
    }
  }

  static UnitAbilities ReadUnitAbilities(uintptr_t unitData) {
    UnitAbilities result;
    int ids[3] = {-1, -1, -1};
    if (!ReadUnitOfficerIds(unitData, ids))
      return result;

    if (ids[0] >= 0)
      result.leaderID = ids[0];

    std::lock_guard<std::mutex> lock(g_skillCountMutex);
    for (int id : ids) {
      if (id < 0)
        continue;

      const auto found = g_customSkillCounts.find(id);
      if (found == g_customSkillCounts.end())
        continue;

      const auto has = [&](uintptr_t key) {
        const auto skill = found->second.find(key);
        return skill != found->second.end() && skill->second > 0;
      };

      result.hasGunakdae |= has(0x1000);
      result.hasMussang |= has(0x1001);
      result.hasFireCavalry |= has(0x1002);
      result.hasArcher |= has(0x1003);
      result.hasDeunggab |= has(0x1004);
      result.hasCommander |= has(0x1005);
      result.hasSneakAttack |= has(0x1006);
      result.hasTactician |= has(0x1007);
      result.hasMusin |= has(0x1008);
    }

    return result;
  }

  // 부대 초기화/로드와 전투 진입 시 같은 무신 적용 경로를 사용합니다.
  static void ApplyMusin(uintptr_t unitData, const UnitAbilities &abilities) {
    if (!abilities.hasMusin)
      return;

    SARegionCache regionCache{};
    __try {
      const uintptr_t SKILL_PTR_OFF  = 0x5D8;
      const uintptr_t SKILL_REC_SIZE = 0x28;
      const uintptr_t SKILL_LIMIT_OFF = 0x14;
      const uintptr_t INDEX_OFF       = 0x0C;
      const int MAX_SKILLS = 45;

      do {
        if (!SAIsValidPtr(unitData, 0x600, regionCache))
          break;

        if (!SAIsValidPtr(unitData + SKILL_PTR_OFF, 8, regionCache))
          break;

        uintptr_t skillStart = *(uintptr_t *)(unitData + SKILL_PTR_OFF);
        if (!SAIsValidPtr(skillStart, SKILL_REC_SIZE, regionCache))
          break;

        int skillCount = 0;
        for (int j = 0; j < MAX_SKILLS; j++) {
          uintptr_t skillRec = skillStart + j * SKILL_REC_SIZE;
          if (!SAIsValidPtr(skillRec + INDEX_OFF, 4, regionCache)) break;
          if ((int)*(uint32_t *)(skillRec + INDEX_OFF) != j) break;
          uint8_t firstByte = *(uint8_t *)skillRec;
          if (firstByte != 0x58 && firstByte != 0xD8) break;

          uintptr_t limitAddr = skillRec + SKILL_LIMIT_OFF;
          if (!SAIsValidPtr(limitAddr, 1, regionCache)) continue;
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
          AddLog(u8"[특수기능] 무신: 부대장 %d 전법 %d개 병종 제약 지속 해제", abilities.leaderID, skillCount);
      } while (false);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return;
    }
  }

  static bool s_musinBattleInitialized = false;

  using SkillInitFn = uintptr_t (__fastcall *)(uintptr_t);
  using SkillLoadFn = bool (__fastcall *)(uintptr_t, uintptr_t);

  static SkillInitFn s_skillInit = nullptr;
  static SkillLoadFn s_skillLoad = nullptr;

  // Keep RAII locking separate from SEH-protected game memory access.
  static bool CopyUnitSkillCounts(int leader, int (&counts)[256]) {
    std::lock_guard<std::mutex> lock(g_skillCountMutex);
    const auto configured = g_customSkillCounts.find(leader);
    if (configured == g_customSkillCounts.end())
      return false;

    for (const auto &entry : configured->second) {
      // Native skill IDs are bytes; other keys describe special abilities.
      if (entry.first >= 0x138 && entry.first < 0x238)
        counts[entry.first - 0x138] = entry.second;
    }

    return true;
  }

  static void ApplyUnitSkillCounts(uintptr_t container) {
    __try {
      // REA: container = unitData+0x5D0, *container = WarUnit,
      // WarUnit+8 = unitData; begin/end describe native 0x28-byte records.
      const uintptr_t unit = *reinterpret_cast<const uintptr_t *>(container);
      const uintptr_t data = *reinterpret_cast<const uintptr_t *>(unit + 8);
      ApplyMusin(data, ReadUnitAbilities(data));

      const uintptr_t members = *reinterpret_cast<const uintptr_t *>(data + 0x18);
      if (!members)
        return;

      int leader = 0;
      for (uintptr_t offset : {uintptr_t(8), uintptr_t(0x10), uintptr_t(0x18)}) {
        const uintptr_t officer = *reinterpret_cast<const uintptr_t *>(members + offset);
        if (officer) {
          leader = *reinterpret_cast<const uint16_t *>(officer + 8);
          break;
        }
      }

      int counts[256]{};
      if (!CopyUnitSkillCounts(leader, counts))
        return;

      // The settings lock has been released before reading or writing game records.
      const uintptr_t begin = *reinterpret_cast<const uintptr_t *>(container + 8);
      const uintptr_t end = *reinterpret_cast<const uintptr_t *>(container + 0x10);
      for (uintptr_t record = begin; record != end; record += 0x28) {
        const uintptr_t skill = *reinterpret_cast<const uintptr_t *>(record);
        const uint8_t id = *reinterpret_cast<const uint8_t *>(skill + 8);
        if (counts[id] > 0)
          *reinterpret_cast<uint8_t *>(record + 0x10) = static_cast<uint8_t>(counts[id]);
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return;
    }
  }

  static uintptr_t __fastcall OnSkillInit(uintptr_t container) {
    const uintptr_t result = s_skillInit(container);
    ApplyUnitSkillCounts(container);
    return result;
  }

  static bool __fastcall OnSkillLoad(uintptr_t container, uintptr_t archive) {
    const bool loading = *reinterpret_cast<const int *>(archive + 0x20) == 1;
    const bool result = s_skillLoad(container, archive);
    if (loading && result)
      ApplyUnitSkillCounts(container);

    return result;
  }

  void InitializeBattleCache(int, uintptr_t, uintptr_t exeBase) {
    if (s_skillInit)
      return;

    // Validate the two entry points once before installing the native hooks.
    constexpr uint8_t initBytes[] = {
        0x48, 0x89, 0x4c, 0x24, 0x08, 0x53, 0x57, 0x41,
        0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
    constexpr uint8_t loadBytes[] = {
        0x40, 0x53, 0x55, 0x41, 0x54, 0x41, 0x57, 0x48,
        0x83, 0xec, 0x28, 0x48, 0x8b, 0x01, 0x48, 0x8b};

    void *init = reinterpret_cast<void *>(exeBase + 0x1E0E0B0);
    void *load = reinterpret_cast<void *>(exeBase + 0x1E0DE30);
    if (std::memcmp(init, initBytes, sizeof(initBytes)) != 0 ||
        std::memcmp(load, loadBytes, sizeof(loadBytes)) != 0)
      return;

    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED)
      return;

    if (MH_CreateHook(init, reinterpret_cast<void *>(&OnSkillInit),
                      reinterpret_cast<void **>(&s_skillInit)) != MH_OK)
      return;

    if (MH_CreateHook(load, reinterpret_cast<void *>(&OnSkillLoad),
                      reinterpret_cast<void **>(&s_skillLoad)) != MH_OK) {
      MH_RemoveHook(init);
      s_skillInit = nullptr;
      return;
    }

    if (MH_EnableHook(init) != MH_OK || MH_EnableHook(load) != MH_OK) {
      MH_DisableHook(init);
      MH_RemoveHook(load);
      MH_RemoveHook(init);
      s_skillInit = nullptr;
      s_skillLoad = nullptr;
      return;
    }

    AddLog(u8"[전법] 부대 초기화/로드 시점에 설정 적용 연결 완료");
  }

  // Kept for existing battle/session reset callers; there is no roster cache.
  void ClearBattleCache() {
    s_musinBattleInitialized = false;
  }

  static uintptr_t ScanActiveUnitAbilities(uintptr_t exeBase,
                                           bool &hasGunakdae, bool &hasMussang,
                                           bool &hasFireCavalry, bool &hasArcher,
                                           bool &hasCommander, bool &hasSneakAttack,
                                           bool &hasTactician, UnitAbilities &abilities) {
    {
      std::lock_guard<std::mutex> lock(g_skillCountMutex);
      if (g_customSkillCounts.empty())
        return 0;
    }

    // SAResolveChain은 매 스텝에서 *(current) + offset 순으로 연산.
    // 마지막 오프셋 0은 "역참조 후 +0" → 최종 역참조를 수행하여
    // activeUnitData 값 자체를 직접 반환함. 반드시 유지해야 함.
    uintptr_t activeUnitData = SAResolveChain(
      exeBase + 0x03510578, {0x100, 0x78, 0, 0x50, 0x108, 0x50, 0x8, 0});
    if (!activeUnitData)
      return 0;

    abilities = ReadUnitAbilities(activeUnitData);
    hasGunakdae = abilities.hasGunakdae;
    hasMussang = abilities.hasMussang;
    hasFireCavalry = abilities.hasFireCavalry;
    hasArcher = abilities.hasArcher;
    hasCommander = abilities.hasCommander;
    hasSneakAttack = abilities.hasSneakAttack;
    hasTactician = abilities.hasTactician;
    return activeUnitData;
  }

  void UpdateSpecialAbilities(int unitCountTotal, uintptr_t unitListBase, uintptr_t exeBase) {
    if (!exeBase)
      return;

    // 훅 설치 전에 생성된 부대도 전투 진입 시 한 번 적용합니다.
    if (!s_musinBattleInitialized && unitCountTotal > 0 && unitListBase) {
      for (int i = 0; i < unitCountTotal; ++i) {
        __try {
          const uintptr_t unitData =
              *reinterpret_cast<const uintptr_t *>(unitListBase + 0x08 + (uintptr_t)i * 0x10);
          ApplyMusin(unitData, ReadUnitAbilities(unitData));
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
      }
      s_musinBattleInitialized = true;
    }

    static DWORD lastUpdateTick = 0;
    static DWORD lastBuffVerifyTick = 0;
    DWORD currentTick = GetTickCount();
    SARegionCache regionCache{};

    // 무신 설정이 있는 현재 행동 부대만 유지 확인합니다.
    bool runActiveSkillUpdate = (unitCountTotal > 0 && currentTick - lastUpdateTick >= 500);
    if (runActiveSkillUpdate)
      lastUpdateTick = currentTick;

    // 공유 마스터 데이터 버프는 부대 전환 시 즉시 적용하고,
    // 게임이 값을 되돌리는 예외 상황 대비 안전 재검사만 3초마다 수행합니다.
    bool verifySharedBuffs = (unitCountTotal > 0 && currentTick - lastBuffVerifyTick >= 3000);
    if (verifySharedBuffs)
      lastBuffVerifyTick = currentTick;

    bool hasGunakdae = false;
    bool hasMussang = false;
    bool hasFireCavalry = false;
    bool hasArcher = false;
    bool hasCommander = false;
    bool hasSneakAttack = false;
    bool hasTactician = false;

    // 1. 현재 선택/행동 부대의 무장 ID로 설정을 직접 조회
    //    반환값: 현재 활성 unitData 주소 (등갑군 섹션에서 재사용)
    UnitAbilities activeAbilities;
    uintptr_t activeUnitData = unitCountTotal > 0 ? ScanActiveUnitAbilities(
      exeBase, hasGunakdae, hasMussang, hasFireCavalry, hasArcher, hasCommander,
      hasSneakAttack, hasTactician, activeAbilities) : 0;

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

    // 현재 행동 부대의 능력 상태가 바뀌면 즉시 적용합니다.
    // 상태 변화가 없을 때는 3초 안전검사만 수행하여 90여 개 대상 주소 반복 검사를 줄입니다.
    bool injectBuffs = (verifySharedBuffs || stateChangedGunakdae || stateChangedMussang || stateChangedFireCavalry ||
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
        // [크래시 방지] 호출 단위 영역 캐시로 유효성을 유지하면서 중복 VirtualQuery를 줄입니다.
        if (!SAIsValidPtr(targetAddr, 1, regionCache))
          continue;
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

        STarget mussangTargets[] = {{0x238, 1, 2}, {0x25A, 1, 2}, {0x27C, 1, 2}, {0x3B8, 1, 5}, {0x3C6, 1, 5},
                                    {0x3DA, 1, 5}, {0x3E8, 1, 5}, {0x3FC, 1, 5}, {0x40A, 1, 5}};
        applyBuffTargets(p, u8"무쌍보병", hasMussang, stateChangedMussang, mussangTargets,
                         sizeof(mussangTargets) / sizeof(STarget));

        STarget fireCavalryTargets[] = {
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

        STarget archerTargets[] = {
                                   {0x72C, 2, 3},
                                   {0x74E, 2, 3},
                                   {0x770, 2, 3},
                                   {0x7AC, 2, 3},
                                   {0x7CE, 2, 3},
                                   {0x7F0, 2, 3},
                                   {0x82C, 2, 3},
                                   {0x84E, 2, 3},
                                   {0x870, 2, 3},
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

        STarget sneakAttackTargets[] = {
                                        {0x347, 10, 50},
                                        {0x369, 10, 70},
                                        {0x38B, 10, 100},
                                        {0x5C7, 10, 50},
                                        {0x5E9, 10, 70},
                                        {0x60B, 10, 100},
                                        {0xDC7, 10, 50},
                                        {0xDE9, 10, 70},
                                        {0xE0B, 10, 100}};
        applyBuffTargets(p, u8"기습부대", hasSneakAttack, stateChangedSneakAttack, sneakAttackTargets,
                         sizeof(sneakAttackTargets) / sizeof(STarget));

        STarget tacticianTargets[] = {
                                      {0xC38, 2, 3},
                                      {0xC46, 2, 3},
                                      {0xC5A, 2, 3},
                                      {0xC68, 2, 3},
                                      {0xC7C, 2, 3},
                                      {0xC8A, 2, 3},
                                      {0xCB8, 5, 6},
                                      {0xCDA, 5, 6},
                                      {0xCFC, 5, 6},
                                      {0xD38, 4, 8},
                                      {0xD5A, 4, 8},
                                      {0xD7C, 4, 8},
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

      if (p2) {
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
    static int s_prevBurnLeader = -1;
    static DWORD lastBurnTick = 0;

    if (unitCountTotal == 0) {
      s_prevBurnLeader = -1;
    } else if (activeUnitData && currentTick - lastBurnTick >= 300) {
      lastBurnTick = currentTick;

      do {
        const auto &cu = activeAbilities;

        int leaderID = cu.leaderID;
        if (leaderID <= 0 || leaderID == s_prevBurnLeader) break;
        s_prevBurnLeader = leaderID;

        if (!cu.hasDeunggab) break;

        if (!SAIsValidPtr(activeUnitData, 0x280, regionCache)) break;
        bool isBurning = false;
        if (SAIsValidPtr(activeUnitData + 0x40, 8, regionCache)) {
          uintptr_t ptr40 = *(uintptr_t *)(activeUnitData + 0x40);
          if (SAIsValidPtr(ptr40, 0x40, regionCache) && *(uint8_t *)(ptr40 + 0x38) == 1)
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
      } while (false);
    }

    // 행동 부대의 설정 변경은 기존 주기로 반영합니다.
    if (runActiveSkillUpdate && activeUnitData)
      ApplyMusin(activeUnitData, activeAbilities);

    s_lastGunakdaeState = hasGunakdae;
    s_lastMussangState = hasMussang;
    s_lastFireCavalryState = hasFireCavalry;
    s_lastArcherState = hasArcher;
    s_lastCommanderState = hasCommander;
    s_lastSneakAttackState = hasSneakAttack;
    s_lastTacticianState = hasTactician;
  }

} // namespace DX11Base
