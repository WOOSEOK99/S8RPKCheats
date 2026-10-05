# 보조 보주 선택형 강제 사용 구현 작업 문서

## 작업 기준

- 저장소: `WOOSEOK99/S8RPKCheats`
- 작업 브랜치: `feature/jewel-force-selection`
- 시작 기준 `main` HEAD: `1a3909d95ad0b5e5959abd45a96bc71298bbb6ae`
- 기능 구현 코드 HEAD(이 문서 갱신 직전): `0ce39e8d8c32e56111cdb983bd46d533720728a5`
- 빌드/런타임 테스트: **사용자 수행**. 이 작업에서는 빌드/게임 실행을 하지 않는다.
- 변경 원칙: 기존 보주 전체 개방과 기존 보조 보주 판정 훅의 안전 조건을 유지하고, 선택 기능에 필요한 범위만 변경한다.

## 기능 정의

### 보주 개방

- 원래 게임에서 담력을 소비하여 보주를 해금하는 상태다.
- 기존 `보주 전체 개방` 치트는 담력 없이 정의된 보주를 전체 해금한다.
- 상세 UI에서는 게임의 실제 개방 비트를 읽어 `개방됨 / 안됨`으로 표시한다.
- 메모리를 확인할 수 없으면 `안됨`으로 추측하지 않고 `확인 불가`로 표시한다.

### 보조 보주 강제 사용

- 원래 게임은 보조 보주 한 개만 선택해 사용할 수 있다.
- 강제 사용 대상으로 선택한 다른 보조 보주들도 기존 판정 훅을 통해 동시에 적용되게 한다.
- 강제 대상으로 선택되어 있어도 실제로 아직 개방되지 않은 보주는 발동시키지 않는다.
- 사용자가 나중에 담력으로 그 보주를 개방하면 추가 설정 없이 강제 사용 대상이 된다.
- `보주 전체 개방`과 `보조 보주 강제 사용`은 독립 기능으로 유지한다.

## version.dll 검증 결과

사용자가 제공한 실제 `version.dll`을 정적 분석했다.

- SHA-256: `01bfb8941d58f231231244b5bd6b8073a97e9630b91ea2312db3b9c2bc982c0a`
- PE32+ x86-64 DLL
- 보주 정의 테이블 파일 오프셋: `0x10F0F0`
- 테이블 VA: `0x18010FCF0`
- 엔트리 수: **185개**
- 엔트리 크기: **24 bytes**
- 엔트리 구성:
  - `+0x00`: raw jewel ID
  - `+0x08`: UTF-16 이름 문자열 포인터
  - `+0x10`: UTF-16 계통 문자열 포인터
- secondary ID: `raw jewel ID - 0x40`
- raw ID 범위: `66..257`
- 범위 내 비정의 raw ID: `94, 115, 136, 157, 178, 199, 220`

확인된 계통 및 개수:

| 계통 | 개수 |
|---|---:|
| 공통 | 30 |
| 재야 | 20 |
| 두령 | 20 |
| 일반 | 20 |
| 군사 | 20 |
| 태수 | 20 |
| 도독 | 20 |
| 군주 | 20 |
| 악한 | 5 |
| 무인 | 5 |
| 문인 | 5 |
| **합계** | **185** |

전체 `secondaryId / rawJewelId / 이름 / 계통` 매핑은 다음 파일에 코드로 기록했다.

- `Internal DX11 Base/Cheats/Civilian/JewelSettingsDefinitions.inc`

ID와 이름/계통을 화면 순서에서 추측하지 않고 위 DLL 테이블에서 직접 추출했다.

## 기존 런타임 근거 유지

기존 `JewelSettings.cpp`에서 확인된 version.dll 재현 로직은 유지한다.

- 보주 개방 비트맵: `gameBase + 0x71E0`
- 보조 보주 판정 함수: `SAN8RPK.exe + 0x17ABBF0`
- 판정 함수 예상 시작 바이트: `48 89 5C 24 08`
- 보조 ID -> raw ID: `rawJewelId = jewelId + 0x40`
- 런타임 객체 확인: `gameBase + sizeof(uintptr_t) * (jewelId + 0xB11DE)`
- 기존 ID별 런타임 데이터 캐시 유지
- 선택되지 않았거나 강제 조건이 실패하면 게임 원본 판정 함수로 전달

## 구현된 구조

### 강제 사용 선택 상태

전역 bool 하나 대신 512-bit 고정 비트셋을 사용한다.

- `std::array<std::atomic<uint64_t>, 8>`
- 훅 hot path 조회: O(1)
- mutex 없음
- 동적 할당 없음
- 보주 목록 순회 없음

전체/계통 선택은 별도 truth state를 만들지 않고 동일한 개별 비트를 일괄 변경한다.

### 개방 상태 표시

상세 창이 열려 있을 때만 개방 비트맵 34 bytes를 snapshot 한다.

- 약 250ms 간격
- UI는 snapshot으로 185개 상태 계산
- 프레임마다 185개 주소를 별도로 읽지 않음
- 별도 background thread 추가 없음

### UI

내정 화면의 기존 `보조 보주 모두 사용` 체크박스를 `보조 보주 설정` 버튼으로 변경했다.

별도 창에서 제공:

- 현재 선택 수 / 185
- 전체 선택 / 전체 해제
- 이름 검색
- 계통별 접이식 목록
- 계통별 전체 선택 / 전체 해제
- 각 보주의 실제 `개방됨 / 안됨 / 확인 불가`
- 보주 이름
- 개별 `강제 사용` 체크박스

잠긴 보주도 강제 대상으로 미리 체크할 수 있으나 실제 훅은 기존 개방 여부 검사를 유지한다.

### Config

기존 설정과 호환한다.

- 기존 `bAllJewelsOpen` 유지
- 기존 `bAllSecondaryJewels` 키도 호환 목적으로 유지
- 신규 개별 선택 truth state:
  - `secondaryJewelForceMask`
  - 8 x 64-bit = 고정 길이 128자리 hex 문자열

로드 규칙:

1. `secondaryJewelForceMask`가 있으면 이를 우선한다.
2. 없으면 기존 `bAllSecondaryJewels`를 migration 한다.
   - true -> 정의된 185개 모두 선택
   - false/없음 -> 모두 해제
3. mask에 정의되지 않은 ID 비트가 있어도 로드시 제거한다.
4. mask 형식이 잘못되면 안전하게 전체 해제한다.

## 변경 파일

- `Internal DX11 Base/Cheats/Civilian/JewelSettings.cpp`
- `Internal DX11 Base/Cheats/Civilian/JewelSettings.h`
- `Internal DX11 Base/Cheats/Civilian/JewelSettingsDefinitions.inc` 신규
- `Internal DX11 Base/Config.cpp`
- `Internal DX11 Base/MenuSectionsDomestic.cpp`
- `JEWEL_FORCE_SELECTION_WORKPLAN.md`

별도 `.cpp`를 추가하지 않았기 때문에 `.vcxproj/.filters` 변경은 필요하지 않았다.

# Step 진행 상태

## Step 0 - 기준점 고정 및 작업 문서 생성

**완료**

- 기준 `main` HEAD 확인
- `feature/jewel-force-selection` 생성
- 작업 문서 생성

## Step 1 - version.dll 메타데이터 복원

**완료**

- 실제 사용 DLL SHA 확인
- 185개 테이블 위치/구조 확인
- secondary/raw ID, 이름, 계통 전체 추출
- 계통별 수량 검증
- 코드 정적 테이블로 저장

## Step 2 - ID별 강제 사용 core

**완료 - 정적 코드 작성 완료, 런타임 미검증**

- 512-bit atomic mask
- 개별/계통/전체 선택
- 훅 전역 bool 조건을 per-ID O(1) 조회로 변경
- 기존 open/runtime/original fallback 유지

## Step 3 - 개방 상태 snapshot

**완료 - 정적 코드 작성 완료, 런타임 미검증**

- 34-byte snapshot
- 250ms UI 갱신
- `개방됨 / 안됨 / 확인 불가`

## Step 4 - 상세 UI

**완료 - 정적 코드 작성 완료, 런타임 미검증**

- 별도 ImGui 창
- 전체/계통/개별 선택
- 검색
- 개방 상태/이름/강제 사용 표시
- 내정 메뉴 버튼 연결

## Step 5 - Config 저장/로드 및 migration

**완료 - 정적 코드 작성 완료, 런타임 미검증**

- `secondaryJewelForceMask`
- 구버전 `bAllSecondaryJewels` migration

## Step 6 - 정적 diff 검토

**완료**

`main` 시작점 대비 변경 파일은 위 6개로 한정되어 있다.

비교 당시 통계:

- `JewelSettings.cpp`: +329 / -7
- `JewelSettings.h`: +34 / -2
- `JewelSettingsDefinitions.inc`: +189
- `Config.cpp`: +94 / -8
- `MenuSectionsDomestic.cpp`: +6 / -10
- 작업 문서 추가/갱신

빌드/실행은 사용자 요청에 따라 수행하지 않았다.

## Step 7 - 사용자 빌드/런타임 검증

**대기 - 다음 정확한 재개 지점**

사용자가 확인할 항목:

1. 솔루션 빌드 성공 여부
2. 내정 메뉴의 `보조 보주 설정` 버튼 및 상세 창 표시
3. 이름/계통/개방 상태가 실제 게임과 일치하는지
4. `일반 + 재야 + 두령` 등 복수 계통 선택 후 효과가 동시에 적용되는지
5. 강제 대상으로 체크했지만 아직 잠긴 보주는 발동하지 않는지
6. 해당 보주를 담력으로 개방한 후 추가 설정 없이 강제 적용되는지
7. `보주 전체 개방` 사용 시 잠긴 보주가 개방되고 강제 대상으로 체크된 보주가 함께 적용되는지
8. 일부 보주만 체크한 상태에서 게임 재시작 후 선택이 복원되는지
9. 세이브 로드/시나리오 변경 후 동작 유지 여부
10. 프레임 드랍, 멈춤, 과도한 로그가 없는지

빌드 오류나 런타임 로그가 나오면 정확한 오류/로그를 기준으로 이 브랜치에서 이어서 수정한다.

## 별도 개선 후보 - 이번 브랜치에 섞지 않음

현재 기존 `SetAllJewelsOpen(false)`는 정의된 개방 비트를 실제로 끄므로, 사용자가 담력으로 정상 개방한 보주까지 닫을 수 있는지 별도 검토가 필요하다.

이 문제는 이번 선택형 보조 보주 강제 사용과 독립적이므로 이번 변경에는 포함하지 않았다.

# 다음 채팅에서 재개 절차

대화가 끊긴 경우 다음 채팅에서는 과거 대화만 믿지 말고 반드시 다음 순서로 확인한다.

1. 저장소 `WOOSEOK99/S8RPKCheats`
2. 브랜치 `feature/jewel-force-selection`
3. 실제 branch HEAD
4. `JEWEL_FORCE_SELECTION_WORKPLAN.md`
5. `main`과 branch diff
6. 사용자가 제공한 빌드 결과 또는 런타임 결과

**현재 정확한 재개 지점은 Step 7 사용자 빌드/런타임 검증 결과 확인이다.**
