# 저장게임 로드 렉 수정 작업계획

작성일: 2026-10-05  
저장소: `WOOSEOK99/S8RPKCheats`  
기준 브랜치: `main`  
시작 HEAD: `6291f9eb99f4531accb37b978f53089367175f49`  
작업 브랜치: `perf/save-load-lag-20261005`  
현재 상태: **S01~S05 구현 완료 / 사용자 재검증 대기**

## 목적

저장게임 로드 직후 발생하는 순간 렉을 로그와 실제 코드 기준으로 단계별 제거한다. 관련 없는 리팩터링은 하지 않는다.

## 확인된 원인

1. `version.dll` 사용 중 기재 Step6 자동 적용이 영구 비지원 상태를 최대 30회 재시도하던 문제.
2. 로드 직후 여러 기능이 게임 EXE 전체 이미지를 동시에 `FindPattern()`으로 검색하던 문제.
   - MonthCapture
   - SystemMonth
   - TengiCave
   - Battleunitcapture

## 단계

| 단계 | 상태 | 작업 |
|---|---|---|
| S01 | 완료/실게임 확인 | 기재 Step6 30회 재시도 제거 |
| S02 | 완료/실게임 확인 | MonthCapture V0.860 고정 RVA + 바이트 검증 fast path |
| S03 | 완료 | diff/문서 점검 |
| S04 | 완료 | 사용자 로그로 S01/S02 동작 확인 |
| S05 | 구현 완료/재검증 대기 | 로드 직후 동시에 실행되는 SystemMonth/Tengi/BattleUnit 전체 EXE scan도 검증된 RVA fast path로 전환 |

## 완료 커밋

- `4aced236` — `docs: add save-load lag fix workplan`
- `f3d70e78` — `fix: stop incompatible trait text auto-apply retries`
- `a5ebdd9d` — `perf: add validated fast path for month capture hook`
- `15db444b` — `docs: record save-load lag validation state`
- `72973c5c` — `docs: record save-load lag runtime validation`
- `914b8757` — `perf: add validated fast path for system month hook`
- `d3e0a91a` — `perf: add validated fast path for tengi hook`
- `73ca6750` — `perf: add validated fast path for battle unit hook`
- `abc8e477` — `chore: keep battle unit fast-path diff minimal`

## 사용자 로그에서 확인된 사실

- 기재 자동 적용 반복은 제거됨.
- MonthCapture는 `RVA 0x1BDCAD1` 검증 성공, `bytes=6`으로 전체 68MiB scan 미실행 확인.
- 그런데 로드 직후 같은 2~3초 구간에 `SystemMonth`, `TengiCave`, `Battleunitcapture`도 각각 전체 EXE `FindPattern()`을 동시에 실행하고 있었음.
- 최신 로그에서도 MonthCapture fast path 성공 후 `SystemMonth`, `Tengi`, `BattleUnit` 설치가 10:31:00~10:31:03에 몰림.
- 따라서 남은 순간 렉을 MonthCapture 단독 원인으로 확정하지 않고, 같은 구조의 3개 스캔을 추가 제거함.

## S05 구현

### SystemMonth
- V0.860 RVA: `0x6609BF`
- 검증 바이트: `88 48 6C 41 C7 06 01 00 00 00`
- 검증 성공 시 full AOB scan 생략, 실패 시 기존 `FindPattern()` fallback.

### TengiCave
- V0.860 RVA: `0x1338830`
- 검증 바이트: `41 88 87 18 4B 1E 00`
- 검증 성공 시 full AOB scan 생략, 실패 시 기존 `FindPattern()` fallback.

### Battleunitcapture
- V0.860 실제 hook RVA: `0x1E3A5D4`
- 검증 바이트: `0F B6 80 80 00 00 00`
- 검증 성공 시 full AOB scan 생략, 실패 시 기존 패턴 검색 +1 fallback.

모든 fast path는 주소만 신뢰하지 않고 원본 바이트를 확인한다.

## 현재 사용자 할 일

1. `perf/save-load-lag-20261005` 최신 HEAD로 Release x64 빌드.
2. 동일 저장파일 로드.
3. 순간 렉 체감 비교.
4. 로그에서 다음 확인:
   - `[SystemMonth] V0.860 고정 RVA 검증 성공`
   - `[Tengi] V0.860 고정 RVA 검증 성공`
   - `[BattleUnitCapture] V0.860 고정 RVA 검증 성공`
   - `[MonthCapture] V0.860 고정 RVA 검증 성공`
5. 새 로그 뒷부분 제공.

## 다음 작업

- 순간 렉이 크게 줄면 최종 diff 검토 후 main 병합 준비.
- 여전히 순간 렉이 남으면 `AllocNear`/hook 설치 자체 또는 다른 로드 초기화 경로를 계측한다.
- 측정 전 `MemoryUtils.cpp` 전역 동작을 임의 변경하지 않는다.

## 다른 채팅에서 재개 방법

> `S8RPKCheats의 SAVE_LOAD_LAG_WORKPLAN.md를 읽고 perf/save-load-lag-20261005 브랜치와 현재 HEAD/diff를 확인한 뒤 정확한 다음 Step부터 이어서 작업해줘.`

## 정확한 재개 지점

- 브랜치: `perf/save-load-lag-20261005`
- 시작 HEAD: `6291f9eb99f4531accb37b978f53089367175f49`
- S05 코드 HEAD: `abc8e4771139e441dab306b15f7dab2675fa75dc`
- 현재 단계: **S05 구현 완료 / 사용자 Release x64 재검증 대기**
