# 보조 보주 선택형 강제 사용 구현 작업 문서

## 작업 기준

- 저장소: `WOOSEOK99/S8RPKCheats`
- 작업 브랜치: `feature/jewel-force-selection`
- 시작 기준 main HEAD: `1a3909d95ad0b5e5959abd45a96bc71298bbb6ae`
- 빌드/런타임 테스트: 사용자가 직접 수행. 이 작업에서는 임의로 빌드/실행하지 않는다.
- 변경 원칙: 기존 보주 전체 개방/보조 보주 훅 동작을 최대한 유지하고, 필요한 범위만 최소 변경한다.

## 기능 의미

### 보주 개방

- 게임 원래 시스템에서 담력을 소비하여 보주를 해금하는 상태이다.
- 기존 `보주 전체 개방` 치트는 담력 없이 정의된 보주를 개방하는 기능이다.
- 새 상세 화면에서는 각 보주의 실제 게임 개방 상태를 `개방됨` / `안됨`으로 명확히 표시한다.
- 메모리를 읽을 수 없는 경우에는 거짓으로 `안됨`이라 표시하지 않고 `확인 불가`로 표시한다.

### 보조 보주 강제 사용

- 원래 게임은 보조 보주를 한 개만 선택하여 사용할 수 있다.
- 강제 사용 기능은 선택된 다른 보조 보주 효과도 동시에 사용되도록 판정을 우회한다.
- 강제 사용 대상으로 선택되어 있어도 아직 개방되지 않은 보주는 강제 적용하지 않는다.
- 사용자가 나중에 담력으로 해당 보주를 개방하면 추가 설정 없이 강제 사용 대상이 된다.
- `보주 전체 개방`과 `보조 보주 강제 사용`은 서로 다른 기능으로 유지한다.

## 현재 확인된 런타임 근거

현재 `Internal DX11 Base/Cheats/Civilian/JewelSettings.cpp`에 기존 version.dll 분석 결과가 반영되어 있다.

- 정의된 보주: 185개
- 보주 개방 비트맵: `gameBase + 0x71E0`
- 보조 보주 판정 함수: `SAN8RPK.exe + 0x17ABBF0`
- 판정 함수 예상 시작 바이트: `48 89 5C 24 08`
- 보조 ID -> raw jewel ID: `rawJewelId = jewelId + 0x40`
- 런타임 보주 객체 확인 테이블: `gameBase + sizeof(uintptr_t) * (jewelId + 0xB11DE)`
- 기존 훅은 정의 여부, 개방 여부, 런타임 객체 존재 여부를 확인한 뒤 강제 TRUE를 반환하고, 조건 불충족 시 원본 판정 함수로 넘긴다.
- 기존 hot path에는 ID별 런타임 데이터 캐시가 존재하며 이 최적화는 유지한다.

## 목표 UI

기존 내정 화면에는 긴 목록을 직접 넣지 않는다.

기존 영역 예시:

- 담력
- 보주 교체 무제한
- 보주 전체 개방
- `보조 보주 설정...` 버튼

별도 `보조 보주 설정` 창에서 관리한다.

### 상단

- 선택 수 / 정의된 보주 수
- 전체 선택
- 전체 해제
- 이름 검색

### 계통별 그룹

계통별 접이식 섹션으로 표시한다.

예상 계통은 실제 version.dll 데이터를 확인한 뒤 확정한다.

- 공통
- 군주
- 일반
- 재야
- 두령
- 군사
- 태수
- 도독
- 문인
- 무인
- 악한
- 기타 실제 정의 계통

각 그룹에 다음을 표시한다.

| 개방 상태 | 이름 | 강제 사용 |
|---|---|---|
| 개방됨 | 실제 이름 | 체크박스 |
| 안됨 | 실제 이름 | 체크박스 |

계통 헤더에는 `선택 수 / 계통 보주 수`, `전체 선택`, `전체 해제`를 둔다.

강제 사용 체크박스는 잠긴 보주에도 선택 가능하게 한다. 잠긴 동안에는 훅에서 강제 적용되지 않고, 추후 개방되면 자동 적용된다.

## 데이터 모델

정확한 version.dll 분석 결과를 기준으로 정적 정의 테이블을 만든다.

```cpp
enum class JewelCategory : uint8_t {
    // version.dll 확인 후 확정
};

struct JewelDefinition {
    uint16_t secondaryId;
    uint16_t rawJewelId;
    const char* name;
    JewelCategory category;
};
```

주의:

- 이름 순서나 화면 순서에서 ID를 추측하지 않는다.
- `secondaryId`, `rawJewelId`, 이름, 계통을 version.dll에서 직접 확인한 값으로 기록한다.
- 기존 185개 정의 마스크와 신규 테이블을 교차 검증한다.

## 강제 사용 상태 모델

전체/계통/개별 상태를 별도 bool 세트로 중복 저장하지 않는다.

실제 truth source는 ID별 선택 비트셋 하나로 둔다.

권장:

```cpp
constexpr size_t kSecondaryJewelWordCount = 8; // 512 bits
std::array<std::atomic<uint64_t>, kSecondaryJewelWordCount> g_forcedSecondaryJewels;
```

특성:

- 훅 hot path에서 O(1) 조회
- mutex 없음
- 동적 할당 없음
- 컨테이너 순회 없음
- 전체 선택/계통 선택은 개별 비트를 일괄 변경하는 UI 명령으로 처리

## 훅 변경 원칙

기존 전역 조건:

```cpp
g_allSecondaryJewelsEnabled
```

을 ID별 조건으로 바꾼다.

개념:

```cpp
if (IsSecondaryJewelForceEnabled(jewelId) &&
    IsDefinedSecondaryJewelId(jewelId)) {
    // 기존 open/runtime 검사 유지
}
```

다음 기존 로직은 그대로 유지한다.

- `GetGameBaseFast()` 사용
- 개방 비트 확인
- 런타임 객체 캐시 확인
- 조건 실패 시 원본 함수 호출
- 훅 설치 전 예상 바이트 검증

훅 lifecycle도 최대한 유지한다.

- 강제 대상이 최초로 필요할 때 훅 설치
- 실행 중 반복적인 MinHook remove/recreate는 하지 않는다.
- 선택 대상이 0개이면 훅은 즉시 원본 함수로 통과시킨다.

## 개방 상태 표시 최적화

상세 창이 열려 있을 때만 개방 상태를 갱신한다.

- `gameBase + 0x71E0`의 34바이트 비트맵을 한 번 snapshot
- UI는 snapshot으로 185개 상태를 계산
- 필요 시 약 250ms 수준으로 저빈도 갱신
- 185개 주소를 프레임마다 개별 검사하지 않는다.
- 별도 background thread를 만들지 않는다.

## 설정 저장

기존 설정:

- `bAllJewelsOpen`
- `bAllSecondaryJewels`

신규 강제 사용 선택은 compact mask로 저장한다.

예시 키:

```json
"secondaryJewelForceMask": "...hex..."
```

### 구버전 migration

신규 mask가 없을 때만 기존 `bAllSecondaryJewels`를 해석한다.

- `true`: 정의된 185개를 모두 선택
- `false`: 모두 선택 해제

신규 mask 저장 이후에는 신규 값을 우선한다.

기존 `bAllJewelsOpen`은 별도 기능으로 유지한다.

## 예정 변경 파일

예상 범위:

- `Internal DX11 Base/Cheats/Civilian/JewelSettings.cpp`
- `Internal DX11 Base/Cheats/Civilian/JewelSettings.h`
- `Internal DX11 Base/Cheats/Civilian/JewelSettingsWindow.cpp` 신규
- `Internal DX11 Base/Cheats/Civilian/JewelSettingsWindow.h` 신규
- `Internal DX11 Base/MenuSectionsDomestic.cpp`
- `Internal DX11 Base/MenuState.cpp/.h` 필요 시 최소 수정
- `Internal DX11 Base/Config.cpp`
- `Internal DX11 Base/Internal DX11 Base.vcxproj`
- `Internal DX11 Base/Internal DX11 Base.vcxproj.filters`

불필요한 리팩터링, formatting, unrelated warning 수정은 하지 않는다.

# Step 계획 / 진행 상태

## Step 0 - 기준점 고정 및 작업 문서 생성

상태: **완료**

- `main` 시작 HEAD 확인
- `feature/jewel-force-selection` 브랜치 생성
- 이 작업 문서 생성

## Step 1 - version.dll 보주 메타데이터 복원

상태: **대기 - 입력 자료 필요**

필요 자료:

- 현재 실제 게임에서 사용 중인 `version.dll`

복원 대상:

- 185개 `secondaryId`
- 각 `rawJewelId`
- 이름
- 계통
- version.dll 내부 전체/계통/개별 강제 판정 데이터 구조가 추가로 존재하면 함께 기록

검증:

- 현재 코드의 185개 마스크와 ID 집합 일치 여부 확인
- 화면 이름/계통과 DLL 정의 일치 여부 확인

## Step 2 - ID별 강제 사용 core 구현

상태: **미착수**

- 고정 비트셋 추가
- 개별 선택 API 추가
- 전체/계통 선택 API 추가
- 기존 훅의 전역 bool 조건을 ID별 O(1) 검사로 변경
- 기존 runtime cache/open 검사/original fallback 유지

## Step 3 - 개방 상태 snapshot API 구현

상태: **미착수**

- 34바이트 snapshot
- 개별 보주 `개방됨/안됨/확인 불가` 조회 API
- UI가 열려 있을 때만 저빈도 갱신

## Step 4 - 보조 보주 설정 UI 구현

상태: **미착수**

- 별도 ImGui 창
- 전체 선택/해제
- 이름 검색
- 계통별 접이식 섹션
- 계통별 선택/해제
- 개별 강제 체크
- 실제 개방 상태 표시
- 내정 메뉴에는 설정 창 진입 버튼만 최소 추가

## Step 5 - Config 저장/로드 및 migration

상태: **미착수**

- force mask 문자열 저장/로드
- 기존 `bAllSecondaryJewels` migration
- `bAllJewelsOpen` 동작과 분리 유지

## Step 6 - 프로젝트 파일 등록 및 정적 검토

상태: **미착수**

- 신규 .cpp/.h를 vcxproj/filters에 등록
- 변경 diff가 계획된 파일에 한정되는지 확인
- 빌드하지 않음

## Step 7 - 사용자 빌드/런타임 검증

상태: **사용자 수행 예정**

사용자가 확인할 항목:

1. 솔루션 빌드 성공 여부
2. 보조 보주 설정 창 정상 표시
3. 이름/계통/개방 상태 표시 정확성
4. `일반 + 재야 + 두령` 등 복수 계통 선택 후 동시 효과 적용 여부
5. 강제 대상으로 선택했지만 잠긴 보주는 발동하지 않는지
6. 담력으로 해당 보주를 개방한 뒤 자동으로 강제 적용되는지
7. `보주 전체 개방` 사용 시 잠긴 보주가 즉시 개방되고 강제 대상이면 함께 적용되는지
8. 설정 저장 후 게임 재시작 시 선택 상태 복원 여부
9. 세이브 로드/시나리오 변경 후 기존 기능 회귀 여부
10. 프레임 드랍/멈춤/과도한 로그 발생 여부

## 별도 개선 후보 - 이번 구현과 섞지 않음

현재 `SetAllJewelsOpen(false)`가 정의된 보주 비트를 실제로 끄는 경로는 사용자가 담력으로 정상 개방한 보주까지 닫을 수 있는지 별도 검토가 필요하다.

이 문제는 선택형 보조 보주 강제 사용 구현과 독립적이므로 이번 기능 변경에 섞지 않는다. 필요 시 별도 브랜치/패치로 처리한다.

# 다음 채팅에서 재개하는 방법

새 채팅에서는 먼저 다음을 확인한다.

1. 저장소 `WOOSEOK99/S8RPKCheats`
2. 브랜치 `feature/jewel-force-selection`
3. 이 문서 `JEWEL_FORCE_SELECTION_WORKPLAN.md`
4. branch HEAD와 `git`/GitHub 실제 상태
5. 사용자가 첨부한 `version.dll`이 있으면 Step 1부터 계속 진행

과거 채팅 설명을 기준으로 바로 수정하지 말고, 반드시 이 문서와 실제 branch 내용을 다시 확인한 뒤 이어서 작업한다.

# 현재 정확한 재개 지점

**Step 1 시작 전.**

현재 저장소에는 version.dll의 185개 ID 마스크와 판정 로직은 남아 있으나, UI에 필요한 `이름 <-> secondary/raw ID <-> 계통` 전체 매핑은 확인되지 않았다. 따라서 이 값을 추측해 구현하지 않는다.

다음 입력은 현재 사용 중인 `version.dll`이다.
