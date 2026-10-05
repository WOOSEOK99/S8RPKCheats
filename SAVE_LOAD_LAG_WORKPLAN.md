# 저장게임 로드 렉 수정 작업계획

작성일: 2026-10-05  
저장소: `WOOSEOK99/S8RPKCheats`  
기준 브랜치: `main`  
시작 HEAD: `6291f9eb99f4531accb37b978f53089367175f49`  
작업 브랜치: `perf/save-load-lag-20261005`  
현재 상태: **S01/S02/S03 완료 / 다음 S04 사용자 검증**

## 목적

저장게임 로드 직후 발생하는 큰 렉을 로그와 실제 코드 기준으로 단계별 제거한다. 관련 없는 리팩터링은 하지 않는다.

## 확인된 원인

1. **기재 Step6 자동 적용 재시도 루프**
   - `version.dll` 사용 중 내장 기본기재 문구 편집이 영구적으로 지원되지 않는데도 1초 간격으로 최대 30회 재시도했다.
   - 각 실패에서 이름/설명 훅이 설치·해제되어 약 35초간 반복됐다.
   - 코드 위치: `Internal DX11 Base/Cheats/Officer/TraitTextEditorWindow.cpp`

2. **MonthCapture 전체 이미지 AOB 스캔**
   - `SetMonthCapture(true)`가 별도 스레드에서 실행 파일 전체 `SizeOfImage`를 `FindPattern`으로 검색했다.
   - 실측 로그: 약 71,364,608 bytes, 2.5~5.0초.
   - V0.860 로그에서 정확 패턴 `88 86 D2 72 00 00`의 RVA는 `0x1BDCAD1`로 확인됐다.
   - 코드 위치: `Internal DX11 Base/Cheats/System/MonthCapture.cpp`

## 단계

| 단계 | 상태 | 작업 |
|---|---|---|
| PLAN | 완료 | 브랜치 생성, 원인/수정 범위 기록 |
| S01 | 완료 | 자동 적용에서 `version.dll` 내장 기재 비지원 오류를 재시도 대상에서 제외. 기존 기재 훅 유지, 수동 적용 기존 동작 유지 |
| S02 | 완료 | MonthCapture V0.860 RVA + 정확 6바이트 검증 fast path 추가. 검증 실패 시 기존 전체 AOB scan fallback 유지 |
| S03 | 완료 | branch diff/변경량 검토 및 작업 문서 갱신 |
| S04 | 사용자 검증 대기 | Release x64 빌드 및 실게임 저장파일 로드 테스트 |

## 완료 커밋

1. `4aced236dfd9c1f8719934453c063e600e11ab9a` — `docs: add save-load lag fix workplan`
2. `f3d70e78660dc0e27ab6e5594998d5785efa4704` — `fix: stop incompatible trait text auto-apply retries`
   - `TraitTextEditorWindow.cpp`만 변경
   - S01 diff: +16 / -3
3. `a5ebdd9d47d828b0586e7da13dbdb964ddd0614d` — `perf: add validated fast path for month capture hook`
   - `MonthCapture.cpp`만 변경
   - RVA `0x1BDCAD1`의 `88 86 D2 72 00 00` 검증 성공 시 전체 scan 생략
   - 검증 실패 시 기존 exact/wildcard AOB scan 그대로 fallback
4. 문서 상태 갱신 커밋 — 이 문서의 최신 HEAD 확인 필요

## 현재 branch diff

`main` 시작 HEAD `6291f9e` 대비 코드 변경은 아래 두 파일뿐이다.

- `Internal DX11 Base/Cheats/Officer/TraitTextEditorWindow.cpp`: +16 / -3
- `Internal DX11 Base/Cheats/System/MonthCapture.cpp`: +49 / -16
- 추가 문서: `SAVE_LOAD_LAG_WORKPLAN.md`

관련 없는 파일 변경은 확인되지 않았다.

## 수정 원칙 / 유지 사항

- `main`은 수정하지 않았다.
- 수동 기재 문구 `적용`의 기존 실패 동작은 유지했다.
- 기존 MonthCapture full scan 로직은 제거하지 않고 fallback으로 유지했다.
- fast path는 주소만 신뢰하지 않고 원본 6바이트를 확인한다.
- Windows Release 빌드 및 실게임 실행은 이 작업 환경에서 수행하지 않았다.

## 사용자 할 일 (S04)

1. `perf/save-load-lag-20261005` 브랜치 checkout.
2. Release x64 빌드.
3. 기존과 동일하게 `version.dll`을 둔 상태에서 문제 저장파일 로드.
4. 다음을 확인:
   - `[기재 문구/Step3]` 적용/해제와 이름 훅 ON/OFF가 30회 반복되지 않는지.
   - `[기재 문구/Step6] version.dll 사용 중: 내장 기본기재 문구 자동 적용 생략`이 1회만 나오는지.
   - `[MonthCapture] V0.860 고정 RVA 검증 성공`이 나오는지.
   - `MonthCaptureScan` 시간이 기존 2.5~5초에서 크게 줄었는지.
   - 기재 이름/설명 및 월 관련 기능에 회귀가 없는지.
5. 검증 후 로그 뒷부분을 제공.

## 다음 작업

사용자 S04 결과를 기준으로 처리한다.

- 빌드 실패: 해당 컴파일 오류만 최소 수정.
- fast path 성공 + 렉 해소: 최종 검증 기록 후 main 병합 준비.
- fast path fallback 발생: 해당 실행의 실제 hook RVA/바이트 확인 후 주소 테이블 보완 여부 판단.
- 렉 잔존: 새로운 Perf 수치에서 다음 병목만 추가 분석.

## 다른 채팅에서 재개 방법

새 채팅에서 다음 문장으로 시작한다.

> `S8RPKCheats의 SAVE_LOAD_LAG_WORKPLAN.md를 읽고 perf/save-load-lag-20261005 브랜치와 현재 HEAD/diff를 확인한 뒤 정확한 다음 Step부터 이어서 작업해줘.`

새 채팅은 과거 대화보다 **이 문서 + 실제 branch HEAD + diff**를 우선한다.

## 정확한 재개 지점

- 브랜치: `perf/save-load-lag-20261005`
- 시작 HEAD: `6291f9eb99f4531accb37b978f53089367175f49`
- 구현 완료 HEAD(문서 갱신 전): `a5ebdd9d47d828b0586e7da13dbdb964ddd0614d`
- 현재 단계: **S01/S02/S03 완료**
- 다음 단계: **S04 — 사용자 Release x64 빌드 + 실게임 저장파일 로드 검증**
