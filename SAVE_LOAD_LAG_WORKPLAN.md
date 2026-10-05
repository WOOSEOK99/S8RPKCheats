# 저장게임 로드 렉 수정 작업계획

작성일: 2026-10-05  
저장소: `WOOSEOK99/S8RPKCheats`  
기준 브랜치: `main`  
시작 HEAD: `6291f9eb99f4531accb37b978f53089367175f49`  
작업 브랜치: `perf/save-load-lag-20261005`  
현재 상태: **S01/S02/S03 완료 / S04 로그 검증 완료 / 체감 렉 확인 대기**

## 목적

저장게임 로드 직후 발생하는 큰 렉을 로그와 실제 코드 기준으로 단계별 제거한다. 관련 없는 리팩터링은 하지 않는다.

## 확인된 원인

1. **기재 Step6 자동 적용 재시도 루프**
   - `version.dll` 사용 중 내장 기본기재 문구 편집이 영구적으로 지원되지 않는데도 1초 간격으로 최대 30회 재시도했다.
   - 각 실패에서 이름/설명 훅이 설치·해제되어 약 35초간 반복됐다.
   - 코드 위치: `Internal DX11 Base/Cheats/Officer/TraitTextEditorWindow.cpp`

2. **MonthCapture 전체 이미지 AOB 스캔**
   - 기존 `SetMonthCapture(true)`는 별도 스레드에서 실행 파일 전체 `SizeOfImage`를 `FindPattern`으로 검색했다.
   - 수정 전 실측 로그: 약 71,364,608 bytes, 2.5~5.0초.
   - V0.860 로그에서 정확 패턴 `88 86 D2 72 00 00`의 RVA `0x1BDCAD1`을 확인했다.
   - 코드 위치: `Internal DX11 Base/Cheats/System/MonthCapture.cpp`

## 단계

| 단계 | 상태 | 작업 |
|---|---|---|
| PLAN | 완료 | 브랜치 생성, 원인/수정 범위 기록 |
| S01 | 완료/실게임 확인 | `version.dll` 내장 기재 비지원 오류를 자동 재시도 대상에서 제외. 기존 기재 훅 유지, 수동 적용 기존 동작 유지 |
| S02 | 완료/실게임 확인 | MonthCapture V0.860 RVA + 정확 6바이트 검증 fast path. 실패 시 기존 전체 AOB scan fallback |
| S03 | 완료 | branch diff/변경량 검토 및 작업 문서 갱신 |
| S04 | 로그 검증 완료 | Release x64 실제 실행 로그로 S01/S02 동작 확인 |
| S05 | 조건부 | 사용자가 저장로드 렉을 아직 체감할 경우 MonthCapture 설치 구간(`AllocNear`/`ApplyJmp`)을 분리 계측 후 확인된 병목만 수정 |

## 완료 커밋

1. `4aced236dfd9c1f8719934453c063e600e11ab9a` — `docs: add save-load lag fix workplan`
2. `f3d70e78660dc0e27ab6e5594998d5785efa4704` — `fix: stop incompatible trait text auto-apply retries`
   - `TraitTextEditorWindow.cpp`만 변경
   - S01 diff: +16 / -3
3. `a5ebdd9d47d828b0586e7da13dbdb964ddd0614d` — `perf: add validated fast path for month capture hook`
   - `MonthCapture.cpp`만 변경
   - RVA `0x1BDCAD1`의 `88 86 D2 72 00 00` 검증 성공 시 전체 scan 생략
   - 검증 실패 시 기존 exact/wildcard AOB scan 그대로 fallback
4. `15db444b1fc4c2d36ebe50cc79cb6742ff8a86da` — `docs: record save-load lag validation state`
5. 런타임 검증 기록 커밋 — 이 문서 최신 HEAD 확인 필요

## 2026-10-05 사용자 실게임 로그 검증

Release x64 실행 로그에서 다음을 확인했다.

### S01 — 성공

- `[기재 편집/이름] ... 후크 ON (1개)` 1회.
- `[기재 문구/Step3] 일반 설명 치환 후크 적용: 1개` 1회.
- `[기재 문구/Step6] version.dll 사용 중: 내장 기본기재 문구 자동 적용 생략` 1회.
- 이후 로그 종료까지 기존처럼 적용/해제 및 이름 훅 ON/OFF가 반복되지 않았다.

따라서 **영구 비지원 상태를 최대 30회 재시도하던 문제는 제거 확인**했다.

### S02 — fast path 성공

- `[MonthCapture] V0.860 고정 RVA 검증 성공: +0x1BDCAD1`
- 실제 hook offset `0x72D2` 확인.
- `MonthCaptureScan ... bytes=6 changes=1`

따라서 **약 68MiB 전체 이미지 AOB 검색은 이번 실행에서 실행되지 않았다.**

단, 기존 `MonthCaptureScan` metric은 검색만이 아니라 worker 시작부터 hook 설치 완료까지의 전체 경과시간을 기록하므로 이번에도 `2597.555ms`가 찍혔다.

로그 시각도 다음과 같다.

- `10:24:20` 검색 시작 / 고정 RVA 검증 성공 / hook 주소 확인
- `10:24:23` 실시간 월 캡처 설치 완료

즉 남은 약 2.6초를 **AOB scan 비용이라고 볼 수 없다.** 고정 주소 결정 이후의 `InstallMonthCave()` 내부(`AllocNear`, cave 작성, `ApplyJmp`) 또는 해당 worker의 스케줄링 대기가 포함될 수 있다. 현재 로그만으로 어느 항목인지 확정하지 않는다.

### 기타 관찰

- 파일 로그 I/O는 해당 10초 구간 `66 calls / total 1.999ms`로 큰 병목 근거가 없다.
- `SpeedHackUpdate`는 첫 구간에 `335.923ms` 1회 큰 값이 있으나 이후 10초 집계에서는 거의 0ms다. 지속 병목으로 단정하지 않는다.
- `StartupBridgePrepare`는 startup에서 약 `6038ms`였지만 이번 S01/S02 저장로드 검증과 별도 항목으로 유지한다.

## 현재 branch diff

시작 `main @ 6291f9e` 대비 의도된 코드 변경은 아래 두 파일뿐이다.

- `Internal DX11 Base/Cheats/Officer/TraitTextEditorWindow.cpp`
- `Internal DX11 Base/Cheats/System/MonthCapture.cpp`
- 추가 문서: `SAVE_LOAD_LAG_WORKPLAN.md`

관련 없는 리팩터링은 하지 않았다.

## 수정 원칙 / 유지 사항

- `main`은 수정하지 않는다.
- 수동 기재 문구 `적용`의 기존 실패 동작은 유지한다.
- 기존 MonthCapture full scan 로직은 제거하지 않고 fallback으로 유지한다.
- fast path는 주소만 신뢰하지 않고 원본 6바이트를 확인한다.
- 코드 수정과 실게임 검증을 구분한다.

## 사용자 할 일

- 이번 빌드에서 **문제 저장파일 로드 시 체감 렉이 기존보다 줄었는지만 확인**한다.
- 기재 이름/설명 및 월 관련 기능에 회귀가 없는지 확인한다.
- 렉이 아직 크면 동일 실행의 로그를 제공하면 S05로 진행한다.

## 다음 작업

- 체감 렉이 해소됐으면: 최종 diff 확인 후 main 병합 준비.
- 렉이 여전히 크면: **S05에서 MonthCapture 설치 구간을 `Resolve / AllocNear / ApplyJmp`로 분리 계측**한다. 측정 전에 `MemoryUtils.cpp`의 `AllocNear` 구현을 임의 변경하지 않는다.
- S05 결과로 `AllocNear`가 실제 병목이면 MonthCapture 범위에 한정한 안전한 개선을 우선 검토한다.

## 다른 채팅에서 재개 방법

새 채팅에서 다음 문장으로 시작한다.

> `S8RPKCheats의 SAVE_LOAD_LAG_WORKPLAN.md를 읽고 perf/save-load-lag-20261005 브랜치와 현재 HEAD/diff를 확인한 뒤 정확한 다음 Step부터 이어서 작업해줘.`

새 채팅은 과거 대화보다 **이 문서 + 실제 branch HEAD + diff**를 우선한다.

## 정확한 재개 지점

- 브랜치: `perf/save-load-lag-20261005`
- 시작 HEAD: `6291f9eb99f4531accb37b978f53089367175f49`
- S01/S02 구현 HEAD: `a5ebdd9d47d828b0586e7da13dbdb964ddd0614d`
- 사용자 로그 검증 전 문서 HEAD: `15db444b1fc4c2d36ebe50cc79cb6742ff8a86da`
- 현재 단계: **S04 로그 검증 완료**
- 다음 단계: **체감 렉이 남아 있으면 S05 — MonthCapture 설치 세부 계측**
