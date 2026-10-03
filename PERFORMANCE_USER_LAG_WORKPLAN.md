# 사용자 렉 로그 기반 성능 조사·수정 작업계획

작성일: 2026-10-03  
저장소: `WOOSEOK99/S8RPKCheats`  
기준 브랜치: `main`  
기준 커밋: `996cb6e78eb5a6908a7572059393e76187a4d65c` (`v0.860 배포`)  
작업 브랜치: `perf/user-lag-investigation-20261003`  
작업 브랜치 확인 HEAD(문서 갱신 직전): `470f3e6f758c77fe03402186b5abef8e5d37d67b`  
문서 상태: **S00 완료 / S01 기능 회귀 검증 완료, ON release raw diagnostics 분리 잔여**  
빌드: **Release x64 사용자 환경 빌드 성공 확인**  
실게임: **S00 계측, S01 5번 책략 OFF/ON 실제 실행 확인**

---

## 0. 이 문서의 목적과 절대 작업 규칙

이 파일은 사용자 렉 조사 작업을 새 채팅에서도 이 문서 하나만 읽고 이어가기 위한 **단일 작업 지시서 + 상태 기록**이다. 과거 대화를 전제로 하지 말고, 항상 이 문서와 실제 Git 상태를 다시 확인한 뒤 작업한다.

### 반드시 지킬 규칙

1. **`main`을 직접 수정하지 않는다.** 모든 수정은 `perf/user-lag-investigation-20261003`에서 한다.
2. 매 작업 시작 시 작업 브랜치 HEAD와 `main` HEAD를 다시 확인한다.
3. 기존 작업 브랜치가 있으면 새 브랜치를 임의로 만들지 않는다.
4. 단계는 `S00 → S01 → S02 → S03 → S04 → S05 → S06` 순서를 유지한다.
5. 한 단계의 수정과 검증이 끝나기 전에 관련 없는 리팩터링을 섞지 않는다.
6. 각 단계는 가능한 한 독립 commit으로 남기고 diff를 확인한다.
7. `StratagemSlotProbe.cpp`, `*_impl.inc` 같은 대형 파일은 **전체 재작성 금지**. 최소 patch만 허용한다. line ending/인코딩/자동 formatting에 의한 대량 diff도 금지한다.
8. hook/callback/object ownership/thread lifecycle을 추측으로 바꾸지 않는다.
9. 성능 원인은 측정 없이 확정하지 않는다. startup 1회 spike와 지속 병목을 구분한다.
10. `코드 정적 검토`, `빌드 성공`, `실게임 검증`을 구분해서 기록한다.
11. force push, hard reset, history rewrite, 강제 ref 이동을 하지 않는다.
12. 단계 진행 후 이 문서의 상태와 정확한 재개 지점을 갱신한다.

---

## 1. 현재 Git 기준

- 저장소: `WOOSEOK99/S8RPKCheats`
- 기준 `main`: `996cb6e78eb5a6908a7572059393e76187a4d65c`
- 작업 브랜치: `perf/user-lag-investigation-20261003`
- 문서 갱신 직전 작업 HEAD: `470f3e6f758c77fe03402186b5abef8e5d37d67b`
- `main`은 2026-10-03 S01 검증 시점까지 기준 SHA에서 움직이지 않았다.

### 주요 작업 commit

- `9beda193a48137d424c80ec65aa3f327939a22dc` — 최초 작업계획 문서
- `a42922a28c78ae9d83a568d131597eedec51512f` — S00 diagnostics metric 확장
- `96d89352e59bc7c70839fb0998612175e384b802` — S00 startup/session/init 계측
- `f5dfb1bce8cb689ee722553890118bc66eaf0dc0` — S00 AddLog 동기 비용 계측
- `68a6a350f78241ef42cd71b74882d98020abbd85` — S00 MonthCapture scan 계측
- `f7055af35c4945111800afe62855c9845b2aa964` — S00 문서 상태 기록
- `c1a90a15e0677a12a10460938f43e811dafacdf0` — 파일 로그 체크 시 diagnostics 자동 활성화
- `74bd09f30aedec1928c6544d93e2d3d29b35ba39` — S01 5번 책략 OFF startup skip / 준비 완료 후 10초 대기 제거
- `470f3e6f758c77fe03402186b5abef8e5d37d67b` — `showlog.h` include guard 추가, S01 링크 오류 수정

---

## 2. 현재 작업 상태

| 단계 | 상태 | 목적 | 검증 상태 |
|---|---|---|---|
| PLAN | 완료 | 로그 분석, 브랜치/작업계획 생성 | 완료 |
| S00 | **완료** | v0.860 식별 + 저비용 계측 | Release x64 빌드 및 실게임 로그 출력 확인 |
| S01 | **부분 완료** | 5번 책략 startup 기능/bridge/진단 분리 | OFF/ON 기능 실게임 검증 완료. ON release raw diagnostics 분리만 잔여 |
| S02 | 미착수 | 동기 파일 로그 hot path 제거/완화 | 미실행 |
| S03 | 미착수 | 지속 렉 후보 실제 호출량/비용 계측 | 기존 metric에서 참고 spike만 확보 |
| S04 | 미착수 | MonthCapture worker/search lifecycle | duration 계측만 완료 |
| S05 | BLOCKED | 측정으로 확인된 runtime 병목만 수정 | S03 결과 필요 |
| S06 | 미착수 | 통합 회귀/최종 diff | 미실행 |

### 정확한 현재 재개 지점

**S01을 먼저 마무리한다.**

1. branch/main HEAD를 다시 확인한다.
2. `Internal DX11 Base/Cheats/War/StratagemSlotProbe.cpp`의 `PrepareStratagemFiveUiBridge()`에서 일반 release startup에 필요 없는 다음 deep diagnostics 호출을 기능 hook과 분리한다.
   - `LogFifthUiSignalCallsites()` → `[책략5UISIG]`
   - `LogFifthUiCallbackTargets()` → `[책략5UICBTGT]`
   - `LogFifthUiCallbackCodeTargets()` → `[책략5UICBCODE]`
   - `EnsureFifthUiOnTrickSelectBoundHook()` 안의 `[책략5UISELPROBE]` raw dump도 기능 판정 로직과 출력 로직을 구분한다.
3. **중요:** 단순히 `AddLog`만 숨기고 동일한 코드 scan/dump 계산을 그대로 돌리지 않는다. 기능에 불필요한 deep diagnostic 작업 자체가 실행되지 않아야 한다.
4. 대형 `StratagemSlotProbe.cpp`는 전체 파일 재작성하지 않는다. 원격 도구가 최소 patch를 지원하지 않으면 무리하게 contents 전체 replacement를 하지 말고 중단/기록한다.
5. 기능 훅은 유지한다.
   - InitLayouts 7→8 bridge
   - Layout post-buttons
   - ResetBtnPos 5버튼 재배치
   - OnTrickSelect runtime hook
   - pre-callback/callback loop
   - GetTrickButton/Open sidecar
   - model/count/Camp/회복 및 lifetime gate
6. 수정 후 Release x64 빌드.
7. 5번 책략 ON으로 전투에서 5번 책략이 계속 정상인지 확인하고 startup 로그에 `UISIG/UICBTGT/UICBCODE/UISELPROBE` 대량 raw dump가 사라졌는지 확인한다.
8. 그 검증까지 끝나야 S01 완료로 변경하고 S02로 이동한다.

---

## 3. 최초 제보 로그에서 확인된 사실

최초 사용자 제보 로그는 `V0.850`이었다. 현재 작업 기준은 `V0.860`이므로 처음부터 동일 코드라고 가정하지 않는다.

최초 로그의 핵심 현상:

- 5번 책략 저장 설정은 OFF인데 설정 로드 전 early bridge/probe/ON 요청이 먼저 실행됐다.
- 한 초에 100줄이 넘는 `UISIG/UICBTGT/UICBCODE` 계열 로그 burst가 있었다.
- meaningful exception/error 반복 루프는 없었다.
- raw log byte throughput 자체는 심각한 지속 렉을 설명할 수준이 아니었다.
- 로그가 거의 없는 구간에도 사용자가 렉을 느꼈다면 별도 runtime 병목이 필요하다.
- 초기화가 두 묶음 보였지만 당시 PID가 없어 같은 프로세스 중복 init인지 재실행인지 판별할 수 없었다.

현재 S00의 `[Session] pid=... init=...` 로그로 앞으로 이 문제는 구분 가능하다.

---

## 4. S00 완료 결과

### 적용된 계측

`PerformanceDiagnostics.h` 기존 구조를 재사용했다. clock은 `QueryUnbiasedInterruptTime` 기반이고 10초 집계 방식이다.

추가 metric:

- `StartupBridgePrepare`
- `InitCheatsAttempt`
- `MonthCaptureScan`
- `AddLogCall`
- `AddLogMutexWait`
- `AddLogFileIo`

`AddLog` 자체 계측의 재귀를 피하기 위해 `PerfRecordNoReport()`를 추가했다.

### diagnostics 활성화 방식

일반 사용자는 환경 변수를 설정할 필요 없다.

- **파일 로그 체크박스 ON → diagnostics 자동 ON**
- 환경 변수 `S8RPK_PERF_DIAGNOSTICS`는 개발자 강제 활성화 경로로만 유지

### 실게임 검증

Release x64 실제 빌드 후 정상 로그에서 다음이 확인됐다.

- `[Session] ... version=V0.860`
- `[Perf:T00] diagnostics=ON build=Release arch=x64`
- `StartupBridgePrepare`, `InitCheatsAttempt`, `AddLog*`, `MonthCaptureScan` 출력

따라서 S00은 완료로 본다.

---

## 5. S01 현재 결과

### 5.1 수정 내용

`Source.cpp` wrapper를 이용해 대형 `Source_impl.inc` 직접 수정은 피했다.

현재 동작:

- startup 초기에 저장된 `bStratagemFiveEnabled`만 가볍게 읽는다.
- 저장 설정 OFF면 early 5번 책략 bridge를 건너뛴다.
- OFF인데 기존 `SetStratagemFiveFeature(true)`가 실제 ON 요청을 만들지 않도록 차단한다.
- bridge가 준비된 뒤 기존 startup `Sleep(100) × 100` 고정 약 10초 대기를 계속하지 않는다.
- startup에 `[Stratagem5UI] early saved setting=ON/OFF`를 남긴다.

초기 S01에서 `#define AddLog T01AddLog`로 인해 `showlog.h` 재포함 선언이 `T01AddLog(const char*, ...)`로 바뀌어 LNK2001이 발생했다. `showlog.h`에 `#pragma once`를 추가해 해결했고 이후 Release x64 빌드가 성공했다.

### 5.2 OFF 실게임 검증 — 성공

확인된 흐름:

```text
[Session] ... version=V0.860
[Stratagem5UI] early saved setting=OFF
[Stratagem5UI] early bridge skipped: saved setting OFF
같은 초에 System/D3D startup 진행
...
[책략5STATE] OFF 예약
[Config] 5번 책략 활성화 설정 로드: OFF
```

OFF startup에서는 기존의 조기 `ON 예약`, `unified ID5 experiment armed`, `UISIG/UICBTGT/UICBCODE` dump가 사라졌다.

즉 **최초 제보에서 가장 명확했던 'OFF인데도 5번 책략 heavy startup 경로 실행' 문제는 v0.860 작업 브랜치에서 실게임으로 제거 확인**했다.

### 5.3 ON 실게임 검증 — 기능 성공

사용자가 실제 전투에서 **5번 책략 정상 동작**을 확인했다.

로그에서도 다음이 확인됐다.

- `early saved setting=ON`
- bridge hook 설치 성공
- 다음 전투 적용 lifecycle 정상
- native helper 7→8 확장 성공
- sidecar 5번째 버튼 생성
- ID7 등록 성공
- GetTrickButton/Open/callback hook READY
- 전투에서 원본 책략 `N=2` 확인
- `ID5 entry=2 available 0 -> 1`
- `N=2 -> total=3` model 합성 성공
- runtime stage `0 -> 6 ready=1`
- 사용자가 실제 5번 책략 사용 정상 확인

따라서 S01의 기능 회귀는 현재 확인되지 않았다.

### 5.4 S01 잔여 문제 — ON startup deep diagnostics

ON 로그에서는 다음 release 진단이 아직 무조건 실행된다.

- `[책략5UISELPROBE]` raw bytes
- `[책략5UISIG]`
- `[책략5UICBTGT]`
- `[책략5UICBCODE]`

`StartupBridgePrepare` 실측:

```text
calls=1
total≈7922ms
```

다만 같은 구간 `AddLogFileIo`는 약 45ms / 198회 수준이므로 **7.9초 전체를 파일 I/O 탓으로 돌리면 안 된다.** hook 설치/scan/deep diagnostic 내부 작업을 분리해서 봐야 한다.

S01 완료 전 기능에 불필요한 deep diagnostics 자체를 일반 release 경로에서 gate해야 한다.

---

## 6. 현재 성능 기준선에서 확인한 것

### 파일 로그

정상 PC 기준:

- per-line 파일 I/O 대체로 약 0.2~0.6ms
- mutex wait는 대부분 0
- 대량 5번 책략 startup 로그에서도 파일 I/O 누적은 수십 ms 수준

따라서 현재 정상 PC에서는 파일 로그가 **지속 렉의 단일 주원인으로 보이지 않는다.** 하지만 현재 구현이 producer thread에서 `exists/file_size/open/write/close`를 global mutex 아래 수행한다는 구조적 문제는 남아 있어 S02 대상은 유지한다.

### SpeedHackUpdate

서로 다른 정상 PC 테스트에서 startup/초기 안정화 구간에 다음 1회성 큰 max가 관측됐다.

- 약 `262ms`
- 약 `449ms`
- ON 테스트에서는 약 `1110ms`

하지만 이후 10초 구간에서는 여러 번 호출되어도 `total=0ms`에 가까운 구간이 반복됐다. 따라서 현재 증거로는 **지속 SpeedHack 병목이 아니라 초기 1회성 spike 후보**다. S03에서 affected-user 로그로 비교한다.

### MonthCapture

실측:

- module 약 `68 MiB`
- 정상 PC에서 약 `2.77~3.39초`
- 현재 worker thread에서 실행

메인 스레드 동기 3초 정지라고 단정하지 않는다. CPU/메모리 대역폭 경쟁과 detached worker lifecycle은 S04 대상이다.

---

## 7. S02 계획 — 동기 파일 로그 hot path

S01 완료 후 진행한다.

목표:

- 게임 hook/worker/UI thread가 로그 1줄 때문에 파일 open/write/close를 직접 기다리지 않게 한다.

수정 전 반드시 확인:

- `AddLog` 반환 즉시 파일 반영이 필요한 호출자가 있는지
- unload/종료 경로
- loader lock에서 worker join하지 않는지

권장 방향:

- 파일/화면 로그 모두 OFF면 기존 빠른 return 유지
- bounded queue
- writer 1개
- 파일 handle을 가능한 유지하고 batch write
- queue full이면 game thread를 오래 block하지 않고 drop/coalesce + drop count
- UI 로그 렌더 중 producer mutex 장기 보유 제거
- logger worker ownership과 정상 shutdown 명확화

실제 정상 PC에서는 logger 비용이 작았으므로 과도한 리팩터링보다 안전성이 우선이다.

---

## 8. S03 계획 — 지속 렉 후보 실측

**실제 렉 사용자의 새 diagnostics 로그가 핵심이다.** 정상 PC 로그는 기준선일 뿐이다.

최소 계측/비교 대상:

- `Menu::Loops`
- Present/Overlay
- `SpeedHackUpdate`
- RoninMonitor update/full 5102 scan
- Battleunit capture hook
- Domestics hook
- 필요한 경우 FindPattern scanned bytes/time

판정 원칙:

- calls가 많다는 것과 total time이 크다는 것을 구분
- max 1회 startup spike와 지속 total 증가를 구분
- 고빈도 hook에 per-call 파일 로그 추가 금지

S05는 S03에서 실제 병목이 확인되기 전까지 BLOCKED다.

---

## 9. S04 계획 — MonthCapture lifecycle

확인/수정 대상:

- 같은 세션에서 중복 scan 여부
- save/load generation에서 재설치 필요성
- worker handle을 즉시 `CloseHandle`하여 ownership을 잃는 현재 구조
- `volatile bool` stop flag의 수명 안전성
- unload/toggle 후 오래된 worker가 늦게 hook을 설치할 가능성

scanner를 메인 thread로 옮기지 않는다.

---

## 10. S05 / S06

### S05

S03에서 실제 total time/frametime 영향이 확인된 runtime path만 수정한다. 추측 후보는 수정하지 않는다.

### S06

최종 확인:

- base=`main@996cb6e...`
- head=`perf/user-lag-investigation-20261003`
- 의도한 파일 외 변경 없음
- 대형 파일 전체 재format 없음
- generated/binary 불필요 변경 없음
- main 직접 commit 없음

최소 회귀:

1. 5번 책략 OFF startup
2. 5번 책략 ON → 다음 전투 적용
3. 전투 종료 → 다음 전투
4. 저장게임 load
5. 책략창 반복 open/close
6. 파일 로그 OFF/ON
7. UI 숨김/펼침
8. 전략/전투 idle
9. MonthCapture 의존 기능
10. 정상 종료/가능하면 정상 unload

성능 결과는 항상 `정적 확인 / 계측 확인 / 실게임 확인`을 구분해 기록한다.

---

## 11. 마지막 작업 기록

### 2026-10-03 / S00

- 상태: **완료**
- Release x64 빌드: 성공 확인
- 실게임 diagnostics: 성공 확인
- 파일 로그 체크박스만 ON하면 diagnostics 자동 활성화 확인
- 정상 PC 기준선 확보

### 2026-10-03 / S01

- 시작 기준: `c1a90a15e0677a12a10460938f43e811dafacdf0`
- 주요 코드 commit: `74bd09f30aedec1928c6544d93e2d3d29b35ba39`
- 링크 수정 commit: `470f3e6f758c77fe03402186b5abef8e5d37d67b`
- 변경 파일:
  - `Internal DX11 Base/Source.cpp`
  - `Internal DX11 Base/showlog.h`
- OFF 테스트: **성공**
  - 저장 OFF 조기 감지
  - early bridge skip
  - 잘못된 startup ON 예약 제거
  - 기존 약 10초 고정 startup 대기 제거
  - OFF에서 raw 5번 책략 dump 없음
- ON 테스트: **기능 성공**
  - 실제 전투 5번 책략 정상 동작 사용자 확인
  - sidecar/ID7/model/count/runtime READY 로그 확인
- 남은 불확실성/작업:
  - ON release startup의 deep raw diagnostics가 남아 있음.
  - `StartupBridgePrepare≈7.92s`의 세부 구성은 아직 분리 계측되지 않음.
  - 대형 `StratagemSlotProbe.cpp`는 반드시 최소 patch로만 변경해야 함.
- 다음 재개 지점:
  - **S01 잔여: 기능 훅은 보존하고 `UISELPROBE/UISIG/UICBTGT/UICBCODE` deep diagnostics의 일반 release 실행 자체를 gate → Release x64 build → ON 전투 회귀 확인 → S01 완료 처리.**

---

## 12. 완료 판단 기준

전체 작업은 다음을 만족해야 끝난다.

1. 5번 책략 OFF startup에서 불필요한 feature ON 및 heavy probe 없음.
2. 5번 책략 ON 기능 회귀 없음.
3. 일반 release에서 개발용 deep raw diagnostics가 무조건 실행되지 않음.
4. 파일 logger가 game producer를 불필요하게 오래 block하지 않음.
5. 지속 렉은 실제 affected-user 계측으로 hot path를 식별하고 근거 기반으로 수정.
6. MonthCapture worker 수명/중복 문제 없음.
7. 최종 diff가 작고 관련 파일에 한정됨.
8. 빌드/실게임을 실제로 하지 않은 항목은 성공했다고 표현하지 않음.
9. 이 문서만 읽어도 branch, 상태, 검증 결과, 다음 수정 지점을 알 수 있음.
