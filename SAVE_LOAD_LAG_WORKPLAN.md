# 저장게임 로드 렉 수정 작업계획

작성일: 2026-10-05  
저장소: `WOOSEOK99/S8RPKCheats`  
기준 브랜치: `main`  
시작 HEAD: `6291f9eb99f4531accb37b978f53089367175f49`  
작업 브랜치: `perf/save-load-lag-20261005`  
현재 상태: **PLAN 완료 / 다음 S01**

## 목적

저장게임 로드 직후 발생하는 큰 렉을 로그와 실제 코드 기준으로 단계별 제거한다. 관련 없는 리팩터링은 하지 않는다.

## 확인된 원인

1. **기재 Step6 자동 적용 재시도 루프**
   - `version.dll` 사용 중 내장 기본기재 문구 편집이 영구적으로 지원되지 않는데도 1초 간격으로 최대 30회 재시도한다.
   - 각 실패에서 이름/설명 훅이 설치·해제되어 약 35초간 반복된다.
   - 코드 위치: `Internal DX11 Base/Cheats/Officer/TraitTextEditorWindow.cpp`

2. **MonthCapture 전체 이미지 AOB 스캔**
   - `SetMonthCapture(true)`가 별도 스레드에서 실행 파일 전체 `SizeOfImage`를 `FindPattern`으로 검색한다.
   - 실측 로그: 약 71,364,608 bytes, 2.5~5.0초.
   - V0.860 로그에서 정확 패턴 `88 86 D2 72 00 00`의 RVA는 반복해서 `0x1BDCAD1`로 확인됨.
   - 코드 위치: `Internal DX11 Base/Cheats/System/MonthCapture.cpp`

## 단계

| 단계 | 상태 | 작업 |
|---|---|---|
| PLAN | 완료 | 브랜치 생성, 원인/수정 범위 기록 |
| S01 | 진행 예정 | 자동 적용에서 `version.dll`의 내장 기재 비지원 오류를 재시도 대상에서 제외. 기존 72개 기재 훅은 유지, 수동 적용 오류 동작은 유지 |
| S02 | 진행 예정 | MonthCapture에서 V0.860 확인 RVA + 정확 6바이트 검증 fast path 추가. 검증 실패 시에만 기존 전체 AOB scan fallback |
| S03 | 진행 예정 | branch diff/변경량 검토, 작업 문서 상태 갱신 |
| S04 | 사용자 검증 | Release x64 빌드 및 실게임 저장파일 로드 테스트 |

## 수정 원칙

- `main` 직접 수정 금지.
- 한 Step당 독립 commit을 우선한다.
- 대형 파일 전체 재작성/포맷팅 금지, 필요한 로직만 최소 변경한다.
- 기존 hook/lifecycle을 임의로 재설계하지 않는다.
- MonthCapture fast path는 **주소를 무조건 신뢰하지 않고 원본 6바이트를 검증**한 뒤 사용한다.
- fast path 검증 실패 시 기존 검색 로직을 그대로 fallback으로 유지한다.
- 여기서는 Windows Release 빌드/실게임 실행 성공을 주장하지 않는다. 최종 런타임 검증은 사용자 환경에서 한다.

## 예상 커밋

1. `docs: add save-load lag fix workplan`
2. `fix: stop incompatible trait text auto-apply retries`
3. `perf: add validated fast path for month capture hook`
4. `docs: record save-load lag validation state`

## 사용자 검증 항목

- Release x64 빌드.
- `version.dll`을 사용하는 기존 환경에서 저장게임 로드.
- 로그에 `기재 문구/Step3` 적용/해제 및 이름 훅 ON/OFF가 30회 반복되지 않는지 확인.
- `MonthCaptureScan` 시간이 기존 수초에서 크게 줄었는지 확인.
- 기재 이름/설명 및 월 관련 기능에 회귀가 없는지 확인.
- 검증 후 로그 뒷부분을 제공.

## 다른 채팅에서 재개 방법

새 채팅에서 다음 문장으로 시작한다.

> `S8RPKCheats의 SAVE_LOAD_LAG_WORKPLAN.md를 읽고 perf/save-load-lag-20261005 브랜치와 현재 HEAD/diff를 확인한 뒤 정확한 다음 Step부터 이어서 작업해줘.`

새 채팅은 과거 대화보다 **이 문서 + 실제 branch HEAD + diff**를 우선한다.

## 재개 지점

- 브랜치: `perf/save-load-lag-20261005`
- 시작 HEAD: `6291f9eb99f4531accb37b978f53089367175f49`
- 현재 단계: **PLAN 완료**
- 다음 작업: **S01 — TraitTextEditorWindow.cpp 자동 적용 재시도 수정**
