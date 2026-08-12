# 부대 편성 시 크래시 원인 분석

> **현상**: 거의 모든 기능을 켠 상태에서 전투를 위해 부대 편성(포진) 화면 진입 시 게임이 윈도우로 팅기는(크래시/강제 종료) 현상

---

## 🔴 위험도 높음 — 크래시 직접 유발 가능성

### 1. `BattleMonitor.cpp` — 전투 감지 오탐지 및 조기 리프레시 실행

**위치**: `MonitorBattleStatus()`, L233, L273~L325

부대 편성 화면(포진 화면)은 전투 시작 전 단계이지만, 현재 `battleActive` 판정 로직은 **unitListBase나 dayAddr이 유효하기만 해도 전투 중으로 판단**합니다.

```cpp
// L233: 포진 화면에서도 unitListBase가 준비되면 true가 될 수 있음
bool battleActive = (addr1 != 0 || addr2 != 0) || isDateValid || isUnitListValid;
```

포진 화면에서 `battleActive == true`가 되면 즉시 `s_isWarModsApplied == false` 조건으로 진입해:
- **SelfHeal, Dongto, TerrainIgnore, DefBuilding, Catapult, Celestial, SiegeWarfare** 등을 Set(false) → Set(true) 순으로 한꺼번에 리프레시하고,
- `UpdateBattleEnvironment()`를 `force=true`로 강제 실행하며,
- `InitBattleEnvCache()`를 호출하고,
- `InitializeBattleCache()`로 부대 캐시를 빌드하며,
- `UpdateBattleUnitSkills()`까지 실행합니다.

이 모든 작업이 **포진 화면의 메모리 레이아웃이 아직 전투 메모리 구조로 전환되지 않은 시점**에 실행되면, 이후 각 기능들이 잘못된 포인터에 접근하여 크래시를 유발합니다.

---

### 2. `BattleEnvironment.cpp` — `UpdateBattleEnvironment()` force 호출 시 미검증 포인터 접근

**위치**: `UpdateBattleEnvironment()`, L183~L184

```cpp
uint8_t day = *(uint8_t*)battleDayAddr;  // IsValidPtr 체크 없이 직접 역참조!
if (day < 1 || day > 30) return;
```

`dayBaseAddr + 0x28` 주소의 유효성을 `IsValidPtr`로 검증하지 않고 직접 역참조합니다. 포진 화면에서 `dayBaseAddr`이 반환됐지만 내용이 아직 초기화되지 않은 상태라면 **AV(Access Violation) 크래시** 발생.

`ApplyShipWeaponization()` 내부도 유사 — `activeUnitPtr`, `ptr40`, `ptr40_10`를 체인으로 따라가며 `terrain`을 읽는데, 여기서도 중간에 `if (!IsValidPtr(...)) return` 없이 진행하는 경로가 존재합니다.

---

### 3. `SiegeWarfare.cpp` — `ApplyShallowTerrainOnce()` 600-셀 순회 중 무효 포인터 쓰기

**위치**: `ApplyShallowTerrainOnce()`, L90~L98, L109~L126

```cpp
for (int i = 0; i < 600; ++i) {
    uintptr_t tableAddr = startAddr + (i * 0x40);
    if (IsValidPtr(tableAddr, 8)) {
        uintptr_t val = *(uintptr_t*)tableAddr;
        if (val == castleAddr) {
            castleMatches.push_back({tableAddr, i});
        }
    }
}
```

그 이후 `writeShallow` 람다:
```cpp
auto writeShallow = [&](int delta) {
    uintptr_t target = first.addr + (delta * 0x40);
    if (IsValidPtr(target, 8)) {
        *(uintptr_t*)target = shallowAddrValue;  // 직접 쓰기! VirtualProtect 없음
    }
};
```

`shallowAddrValue`를 `VirtualProtect` 없이 직접 씁니다. 포진 화면에서 공성전 기능이 활성화된 채로 `UpdateSiegeWarfare()`가 호출되면, 맵 타일 포인터 테이블에 직접 쓰기 접근 시 **쓰기 보호 AV 크래시** 발생 가능.

`ApplyShallowTerrain2Once()`도 동일한 구조 (L311~L390).

---

### 4. `SpecialAbility.cpp` — `applyBuffTargets` 람다에서 `IsValidPtr` 없이 직접 읽기

**위치**: L252

```cpp
// 성능 최적화: 현재 값이 이미 목표값과 같으면 VirtualProtect 및 쓰기 건너뜀
if (*(uint8_t*)targetAddr == targetVal)  // IsValidPtr 없이 직접 역참조!
    continue;
```

`targetAddr = baseAddr + t.off` 계산 결과에 대해 `IsValidPtr`을 호출하지 않고 즉시 역참조합니다. `s_cachedP`나 `s_cachedP2`가 포진 화면 전환 시 무효화됐는데 캐시 갱신이 지연되면 크래시 유발.

---

## 🟡 위험도 중간 — 조건부 크래시 가능성

### 5. `BattleEnvironment.cpp` — `g_cachedTerrainBase`에 매우 큰 오프셋 접근

**위치**: L258~L325 (terrainBase 활용 구간)

`terrainBase + 0x1AA5A4` 같이 **약 1.7MB 오프셋**을 더한 주소에 씁니다. `terrainBase` 자체가 유효하더라도 해당 오프셋 위치가 실제로 매핑된 영역인지는 `IsValidPtr`로 검증하지 않습니다.

포진 화면에서 `terrainBase`가 이전 전투의 캐시 값으로 남아있지만 게임이 메모리를 재배치했다면, `terrainBase + 0x1AA5A4`는 무효 주소가 될 수 있습니다.

---

### 6. `FactionLordBonus.cpp` — 별도 스레드의 `unordered_map` 충돌

**위치**: `FactionLordBonusThread()`, `RunOnce()` 내 `g_prevAssigned` unordered_map

```cpp
static DWORD WINAPI FactionLordBonusThread(LPVOID) {
    RunOnce();  // g_prevAssigned 수정
    while (g_factionLordBonusEnabled) {
        Sleep(5000);
        RunOnce();  // 메인 스레드와 동시 접근 가능
    }
}
```

`g_prevAssigned`는 `static` 변수이며 메인 스레드(ImGui 렌더 루프)와 별도 스레드(`FactionLordBonusThread`) 양쪽에서 접근하는데, **mutex 보호가 없습니다**. STL `unordered_map`은 스레드 안전하지 않으므로 포진 화면 진입 타이밍과 5초 주기 갱신이 겹치면 crach.

---

### 7. `BattleMonitor.cpp` — `InitializeBattleCache()` 미완성 시 `UpdateBattleUnitSkills()` 즉시 호출

**위치**: L328~L334

```cpp
if (!s_isCacheBuilt && unitCountTotal > 0) {
    __try {
        DX11Base::InitializeBattleCache(unitCountTotal, unitListBase, exeBase);
        UpdateBattleUnitSkills(false);  // 캐시 빌드와 즉시 연속 호출
        s_isCacheBuilt = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
```

`InitializeBattleCache()`는 유닛 리스트 전체를 순회하며 `GetTargetSkillCount(id, ...)` (mutex 내부)를 여러 번 호출합니다. 이어서 `UpdateBattleUnitSkills()`도 동일한 mutex를 잡으려 합니다. 두 함수가 내부에서 중첩 호출될 경우 **mutex deadlock** 가능성이 있습니다 (단, 이건 `std::lock_guard` 비재귀 mutex 기준).

---

## 🟢 안전 — 이미 잘 보호된 부분

- `BattleMonitor.cpp`의 `ResolveChain()`: `__try/__except`로 감싸져 있음.
- `SiegeWarfare.cpp`의 `UpdateSiegeWarfare()`, `UpdateSiegeWarfare2()`: 날짜 유효성 검사 이후 처리.
- `SpecialAbility.cpp`의 `InitializeBattleCache()`, `ScanActiveUnitAbilities()`: 각 포인터별 `IsValidPtr` 체크.

---

## 📋 권장 수정 사항

| 우선순위 | 위치 | 수정 내용 |
|---------|------|----------|
| **최우선** | `BattleEnvironment.cpp` L183 | `*(uint8_t*)battleDayAddr` 앞에 `IsValidPtr(battleDayAddr, 1)` 추가 |
| **최우선** | `BattleMonitor.cpp` L273 | 포진 화면과 전투 진입을 구분하는 추가 조건 (예: `unitCountTotal > 0` && `dayAddr 1~30` 동시 만족) 필요 |
| **높음** | `SiegeWarfare.cpp` L111~L113 | `writeShallow` 람다에 `VirtualProtect` 추가 또는 쓰기 전 `VirtualQuery`로 보호 확인 |
| **높음** | `SpecialAbility.cpp` L252 | `*(uint8_t*)targetAddr` 참조 전 `IsValidPtr(targetAddr, 1)` 추가 |
| **중간** | `FactionLordBonus.cpp` L191~L200 | `g_prevAssigned` 접근에 `std::mutex` 추가 |
| **중간** | `BattleEnvironment.cpp` L258~L325 | `terrainBase + 큰 오프셋` 접근 전 `IsValidPtr` 추가 |

---

## 💡 디버깅 방법 제안

1. **로그 기반 좁히기**: `AddLog`를 전투 감지 분기 진입 직전 (`L272`), 각 Set(false/true) 호출 직전에 추가하여 어떤 기능 리프레시에서 멈추는지 확인
2. **임시 안전장치**: `MonitorBattleStatus()`에서 `s_isWarModsApplied` 리프레시 블록을 `unitCountTotal > 0` 조건이 만족된 이후에만 실행되도록 지연
3. **SiegeWarfare 비활성화 테스트**: 공성전(1칸/2칸) 기능만 끄고 부대 편성 재시도 → 문제 재현 여부로 #3번 원인 확인
