# 사용자 렉 로그 기반 성능 조사·수정 작업계획

작성일: 2026-10-03  
저장소: `WOOSEOK99/S8RPKCheats`  
기준 브랜치: `main`  
기준 커밋: `996cb6e78eb5a6908a7572059393e76187a4d65c` (`v0.860 배포`)  
작업 브랜치: `perf/user-lag-investigation-20261003`  
문서 상태: **S00 계측 구현 완료 — 빌드/실게임 검증 대기**  
실게임 실행: 미실행  
빌드: 미실행  

---

## 0. 이 문서의 목적과 절대 작업 규칙

이 파일은 2026-10-03 사용자에게서 받은 `S8RPK_cheat.log`의 렉 제보를 바탕으로, **새로운 채팅/작업자가 이 문서 하나만 읽고 안전하게 작업을 재개할 수 있도록 만든 단일 작업 지시서이자 상태 기록 문서**다.

새 작업자는 과거 대화를 전제로 하지 말고 이 문서를 시작점으로 사용한다. 다만 문서에 적힌 과거 상태를 그대로 믿고 수정하지 말고, 작업을 시작할 때 실제 Git 상태와 현재 소스를 다시 확인한다.

### 반드시 지킬 규칙

1. **`main`을 직접 수정하지 않는다.** 기본 작업 브랜치는 반드시 `perf/user-lag-investigation-20261003`이다.
2. 작업 시작 시 `git status`, 현재 branch, HEAD, `main`의 최신 HEAD를 확인한다. 원격 작업 도구를 쓰는 경우에도 동일하게 branch/HEAD를 조회한다.
3. 이 브랜치가 존재하면 새 브랜치를 임의로 만들지 말고 이 브랜치를 이어서 사용한다. 별도 단계 브랜치가 꼭 필요하면 사용자 요청을 먼저 확인한다.
4. 단계는 아래 `S00 → S01 → ...` 순서로 진행한다. **한 단계의 수정과 검증이 끝나기 전에 다음 단계의 리팩터링을 섞지 않는다.**
5. 각 단계는 가능한 한 독립된 commit으로 남긴다. commit 전후에 diff와 변경 파일 수를 확인한다.
6. 대형 파일(`StratagemSlotProbe.cpp`, `*_impl.inc` 등)은 전체 재작성하지 말고 최소 patch로 수정한다. line ending/인코딩/자동 formatting에 의한 전체 diff를 금지한다.
7. 현재 동작을 이해하지 못한 hook, callback, 객체 ownership, thread lifecycle을 추측으로 바꾸지 않는다. 실제 호출 경로와 기존 정리 경로를 먼저 확인한다.
8. 성능 원인은 **측정 없이 확정하지 않는다.** 로그에서 의심되는 항목과 실제 CPU/FPS 병목을 구분한다.
9. 사용자가 직접 빌드/실게임 테스트한다고 명시한 경우 임의로 대신 실행하지 않는다. `코드 검토 완료`, `빌드 성공`, `실게임 검증 성공`을 구분해서 기록한다.
10. 단계 완료 후 이 파일의 `작업 상태`, `변경 파일`, `검증 결과`, `남은 불확실성`, `정확한 재개 지점`을 반드시 갱신한다. 새 채팅은 이 표를 보고 바로 이어간다.
11. force push, hard reset, history rewrite, branch 강제 이동, 대량 자동변환은 하지 않는다.
12. 기존 `PERFORMANCE_ANALYSIS_AND_FIX_PROMPT.md`, `PERFORMANCE_VALIDATION.md`는 참고자료일 뿐이다. 두 문서는 더 오래된 기준 커밋을 바탕으로 작성되었으므로 **이 문서와 현재 소스가 우선**이다.

---

## 1. 새 채팅에서 가장 먼저 할 일

새 작업자는 다음 순서를 그대로 수행한다.

```text
1) 이 문서 전체를 읽는다.
2) 저장소가 WOOSEOK99/S8RPKCheats인지 확인한다.
3) 현재 branch / HEAD / working tree를 확인한다.
4) perf/user-lag-investigation-20261003 브랜치로 이동한다.
5) main 최신 HEAD와 이 문서의 기준 커밋 996cb6e...의 관계를 확인한다.
6) 예상치 못한 기존 변경이 있으면 덮어쓰지 말고 diff부터 확인한다.
7) 아래 '현재 작업 상태'에서 첫 미완료 단계부터 시작한다.
8) 해당 단계의 '수정 전 확인'을 먼저 수행한다.
9) 필요한 최소 patch만 적용한다.
10) diff를 확인하고, 검증 결과를 이 문서에 기록한다.
```

`main`이 기준 커밋보다 앞으로 진행되어 있더라도 이 작업 브랜치를 임의 rebase/reset하지 않는다. 먼저 변경 내용을 비교해 현재 단계의 대상 코드가 달라졌는지 확인한다.

---

## 2. 현재 작업 상태 — 새 채팅의 재개 지점

| 단계 | 상태 | 목적 | 코드 변경 여부 | 검증 |
|---|---|---|---|---|
| PLAN | 완료 | 로그/현재 main 확인, 작업 브랜치와 본 문서 생성 | 문서만 | 브랜치/기준 HEAD 확인 완료 |
| S00 | 부분 완료 | v0.860 재현 가능성 확인 + 저비용 계측/식별 정보 보강 | 계측 4파일 수정 | 정적 diff 완료 / 빌드·실게임 미실행 |
| S01 | 미착수 | 5번 책략 시작 경로의 기능 활성화와 bridge/진단 분리 | 미수정 | 미실행 |
| S02 | 미착수 | 동기 파일 로그 hot path 제거/완화 | 미수정 | 미실행 |
| S03 | 미착수 | 지속 렉 후보의 실제 호출량/비용 계측 | 미수정 | 미실행 |
| S04 | 미착수 | MonthCapture 검색/worker lifecycle 보완 | 미수정 | 미실행 |
| S05 | BLOCKED | S03에서 확인된 실제 runtime 병목만 최소 수정 | 미수정 | S03 결과 필요 |
| S06 | 미착수 | 통합 회귀 검증 및 최종 diff 정리 | 미수정 | 미실행 |

### 정확한 현재 재개 지점

**S00 계측 코드는 연결되어 있다. 다음 작업은 `perf/user-lag-investigation-20261003`의 최신 HEAD를 확인한 뒤 빌드/실게임으로 계측이 정상 동작하는지 검증하는 것이다. 이 검증 전에는 S01의 기능 변경을 섞지 않는다.**

검증 시 환경 변수 `S8RPK_PERF_DIAGNOSTICS=1`과 파일 로그 또는 화면 로그 중 하나를 활성화해야 `[Perf:T00]` 결과가 보인다.

---

## 3. 제보 로그에서 확인된 사실

원본 로그 파일명: `S8RPK_cheat.log`  
로그 총 길이: 504줄  
로그 날짜: 2026-10-03

### 매우 중요한 버전 차이

제보 로그의 실행 버전 문자열은 `V0.850`으로 기록되어 있다. 반면 이 계획의 현재 `main` 기준은 `v0.860 배포` 커밋 `996cb6e...`이다.

따라서 **제보 로그가 v0.860의 실행 로그라고 가정하면 안 된다.** 첫 단계에서 v0.860에서도 동일 패턴이 남아 있는지 재확인해야 한다. 다만 현재 v0.860 소스에서도 아래에서 설명하는 핵심 구조 두 가지, 즉 5번 책략의 조기 강제 활성화 경로와 동기식 `AddLog` 파일 쓰기 구조가 실제로 남아 있음을 정적 확인했다.

### 로그 타임라인 요약

#### A. 5번 책략 UI probe가 설정 로드보다 먼저 대량 실행됨

첫 번째 묶음:

```text
18:38:48 [Stratagem5UI] early bridge preparation before startup delay
18:38:49 [책략5UIHELPER] ... 브리지 설치 완료
18:38:50 [책략5UILAYOUT] ... 훅 설치 완료
18:38:50 [책략5UIRESET] ... 훅 설치 완료
18:38:50~18:38:51 [책략5UISELPROBE]/[책략5UISIG]/[책략5UICBTGT]/[책략5UICBCODE] 대량 raw dump
18:38:52 [책략5STATE] ON 예약
18:38:52 [Stratagem5UI] unified ID5 experiment armed before battle UI
```

두 번째 묶음에서도 거의 동일한 흐름이 반복된다.

```text
18:43:08 [Stratagem5UI] early bridge preparation before startup delay
18:43:09~18:43:10 bridge/layout/reset/probe 시작
18:43:10 한 초에 100줄을 넘는 UISIG/UICBTGT/UICBCODE 계열 출력
18:43:11 [책략5STATE] ON 예약
18:43:11 [Stratagem5UI] unified ID5 experiment armed before battle UI
```

그런데 정식 설정 로드는 그 뒤에 이루어지며:

```text
18:43:23 [책략5STATE] OFF 예약
18:43:23 [Config] 5번 책략 활성화 설정 로드: OFF
```

즉 제보 실행에서는 **사용자 설정이 OFF인데도 설정 로드 전에 5번 책략 준비/진단/ON 요청이 먼저 실행**됐다.

이 현상은 현재 v0.860 `Internal DX11 Base/Source_impl.inc`에도 정적으로 확인된다. `MainThread_Initialize()`에서 설정 로드 전에 다음 순서가 존재한다.

```cpp
LoadEarlyLogConfig();
AddLog("[Stratagem5UI] early bridge preparation before startup delay");
PrepareStratagemFiveUiBridge();
SetStratagemFiveFeature(true);
AddLog("[Stratagem5UI] unified ID5 experiment armed before battle UI");
```

추가 정적 확인 결과, startup loop는 단순히 ‘최대 10초 재시도’가 아니다. 현재 구현은 `stratagemUiBridgeReady`가 첫 호출에서 이미 `true`여도 `Sleep(100)`을 100회 모두 수행한다. 즉 **bridge 준비 성공 여부와 무관하게 MainThread가 약 10초를 의도적으로 대기한 뒤 일반 초기화/D3D 단계로 진행한다.** bridge가 실패한 동안에만 `PrepareStratagemFiveUiBridge(false)`가 추가 호출된다. 이 고정 대기는 S01의 최우선 수정 검토 대상이다.

#### B. 파일 로그 자체가 렉을 증폭시킬 가능성이 있음

현재 v0.860 `Internal DX11 Base/showlog.cpp`의 `AddLog()`는 `g_logMutex`를 잡은 상태에서 파일 로그 한 줄마다 다음 작업을 수행한다.

```text
filesystem::exists
filesystem::file_size
ofstream open(app)
시간 변환
UTF-8 정규화
stringstream 생성
write
close
```

즉 대량 probe가 1초에 100줄 이상 발생하면, 각 줄마다 파일 open/write/close와 filesystem 조회가 반복될 수 있다. SSD/백신/동기화 환경에 따라 순간 hitch를 증폭시킬 수 있다.

또 `showLoveLogs()`는 `g_logMutex`를 잡은 상태에서 로그 필터링과 `ImGui::TextUnformatted()`까지 수행한다. 파일 writer와 UI 로그 렌더링이 같은 mutex를 사용한다.

**주의:** 이것이 전체 플레이의 지속 렉 단일 원인이라고 확정한 것은 아니다. 시작/진단 구간의 순간 hitch 후보로 우선순위가 높다는 의미다.

#### C. MonthCapture는 큰 범위를 검색하지만 현재 코드는 worker에서 실행됨

제보 로그:

```text
18:43:53 [MonthCapture] 월 캡처 검색 시작... (00007FF6F4680000 ~ 00007FF6F8A8F000)
18:43:54 [MonthCapture] 실시간 월 캡처 설치 완료.
```

주소 범위는 약 68 MiB다. 하지만 현재 v0.860 `Cheats/System/MonthCapture.cpp`의 `SetMonthCapture(true)`는 `CreateThread()`로 별도 worker를 만들어 `FindPattern()`을 실행한다. 따라서 **현재 코드 기준으로 메인 스레드 동기 68 MiB 스캔이라고 진단하면 안 된다.**

다만 다음은 남은 점검 대상이다.

- 검색 중 CPU/메모리 대역폭 경쟁
- 동일 세션에서 중복 worker가 생기는지
- `CloseHandle(hThread)` 후 worker ownership이 없어 unload/toggle과 경합하는지
- exact pattern 실패 시 wildcard fallback의 비용

이 항목은 S04에서 다루되, S01/S02보다 우선하지 않는다.

#### D. 로그가 조용한 구간에도 지속 렉이 있었다면 별도 runtime 원인이 필요함

두 번째 실행에서 초기 hook 적용이 끝난 `18:43:55` 이후 `18:46:17`까지 약 2분 22초 동안 로그 출력은 사실상 조용하다.

따라서 사용자가 이 구간에도 계속 렉을 느꼈다면 `책략5` dump나 파일 로그만으로는 설명이 부족하다. 다음의 실제 호출 비용을 S03에서 측정해야 한다.

- `SpeedHack_Update`
- `Menu::Loops`
- D3D Present/Overlay
- `Battleunitcapture` 관련 hook
- `DomesticsMult` hook
- `RoninMonitor`의 실제 전체 스캔 호출 횟수
- 기타 게임 thread에서 실행되는 고빈도 callback

#### E. 전체 초기화가 두 번 보이지만 중복 init 버그로 단정할 수 없음

로그에는 약 18:39와 18:43에 DLL 초기화부터 다시 시작하는 두 묶음이 있다. 이것이 같은 프로세스의 중복 초기화인지, 사용자가 게임을 종료/재실행한 것인지 로그만으로 구분할 수 없다.

현재 로그에는 PID/프로세스 시작 식별자가 없다. S00에서 시작 로그에 아래 식별 정보를 추가하는 것을 우선 검토한다.

```text
PID
DLL module address 또는 instance id
게임 exe base
renderer
빌드/치트 버전
session generation
```

---

## 4. 현재 main에서 정적으로 확인한 코드 사실

이 절은 기준 커밋 `996cb6e...`에서 실제 확인한 내용이다. 수정 전 다시 소스를 확인할 것.

### 4.1 `Source_impl.inc`: 5번 책략을 설정 로드 전에 강제로 ON 요청

대상: `Internal DX11 Base/Source_impl.inc`  
함수: `MainThread_Initialize`

현재 흐름:

```text
LoadEarlyLogConfig
→ PrepareStratagemFiveUiBridge
→ SetStratagemFiveFeature(true)
→ 100 × Sleep(100ms) 고정 대기
   └─ bridge가 아직 준비되지 않았을 때만 PrepareStratagemFiveUiBridge(false) 추가 호출
→ 일반 초기화 / D3D / Config 처리
```

따라서 첫 bridge 호출이 즉시 성공해도 약 10초 startup 대기는 유지된다. 이 구조는 사용자 설정 OFF와 독립적으로 먼저 실행된다. 단, early bridge가 실제로 battle UI 생성 전에 반드시 설치되어야 하는 이유가 있을 수 있으므로 **bridge 설치 자체를 무조건 삭제하지 않는다.**

### 4.2 `showlog.cpp`: 파일 로그는 producer thread에서 동기 I/O

대상: `Internal DX11 Base/showlog.cpp`  
함수: `AddLog`, `showLoveLogs`

확인 사항:

- 로그 파일 한 줄마다 `exists/file_size/open/write/close`
- 파일 I/O를 `g_logMutex` 안에서 수행
- UI 로그도 동일 mutex 사용
- `g_loveLogs`는 최대 1000개지만 초과 시 `vector.erase(begin())`
- 로그 화면은 mutex를 잡고 모든 로그를 ImGui에 제출
- 로그 옵션이 모두 OFF면 빠른 return은 이미 존재함 — 이 장점은 유지해야 함

### 4.3 `MonthCapture.cpp`: 검색은 worker thread

대상: `Internal DX11 Base/Cheats/System/MonthCapture.cpp`  
함수: `SetMonthCapture`

확인 사항:

- `CreateThread`에서 module 전체에 `FindPattern`
- exact signature 우선, 실패 시 wildcard fallback
- thread handle은 생성 직후 `CloseHandle`
- stop flag는 `volatile bool`
- worker는 shared global 상태를 갱신

따라서 시작 메인 스레드 직접 정지보다는 worker lifecycle과 CPU 경쟁 관점에서 본다.

### 4.4 `RoninMonitor.cpp`: 전체 5102슬롯 스캔 함수는 존재하지만 무조건 매 프레임이라고 단정 금지

대상: `Internal DX11 Base/Cheats/Officer/RoninMonitor.cpp`

확인된 전체 스캔 함수:

- `ScanRonins`
- `CaptureOfficerChangeBaseline`
- `ScanOfficerChangesAndNotify`

코드는 페이지 단위 `IsValidPtr` 캐시와 상태 baseline을 사용한다. 현재 구조만 보고 ‘매 프레임 5102명 스캔’이라고 단정하지 않는다. S03에서 호출 횟수와 실제 시간부터 측정한다.

### 4.5 기존 성능 계측 기반이 이미 있음

`Internal DX11 Base/PerformanceDiagnostics.h`와 기존 `PERFORMANCE_VALIDATION.md`에는 `S8RPK_PERF_DIAGNOSTICS=1` 기반 저비용 계측 구조가 있다.

새 진단 시스템을 중복으로 만들지 말고 가능한 경우 이 기반을 재사용한다. 계측 clock은 SpeedHack 영향에서 분리되어야 한다.

---

## 5. 우선순위

| 우선순위 | 항목 | 현재 판단 |
|---|---|---|
| P0 | 설정 OFF인데도 5번 책략 early feature ON + 대량 runtime probe | 로그와 현재 소스 양쪽에서 확인. 가장 먼저 분리해야 함 |
| P0 | startup의 100 × `Sleep(100)` 고정 대기 | 현재 v0.860 소스에서 확인. bridge 성공 여부와 무관하게 약 10초 대기 |
| P0/P1 | `AddLog`의 동기 파일 I/O + global mutex | 대량 probe의 hitch를 증폭할 수 있음. 현재 소스에서 확인 |
| P1 | 지속 플레이 runtime hook/thread 비용 | 로그만으로 미확정. 반드시 계측 후 수정 |
| P2 | MonthCapture 전체 모듈 scan | 현재 worker 실행 확인. 지속 렉 단일 원인으로 단정 금지 |
| P2 | Ronin 5102슬롯 scan | 함수는 존재하지만 호출 빈도 미확정. 계측 우선 |
| 보류 | 전체 초기화 두 번 | 프로세스 재실행인지 중복 init인지 PID가 없어 판별 불가 |

---

# S00. 기준 재확인 + 재현 식별/저비용 계측

## 목표

기능 동작을 바꾸기 전에 **제보가 v0.850에서 발생했다는 점을 분리하고, v0.860의 동일 조건에서 어떤 단계가 실제로 오래 걸리는지 확인할 수 있게 한다.**

## 수정 전 확인

1. 현재 branch가 `perf/user-lag-investigation-20261003`인지 확인.
2. working tree가 깨끗한지 확인.
3. 현재 `main` HEAD와 기준 `996cb6e...` 차이를 확인.
4. `PerformanceDiagnostics.h`, `Source_impl.inc`, `showlog.cpp`, `StratagemSlotProbe.cpp`의 현재 상태를 다시 읽는다.
5. 기존 성능 계측이 이미 main에 반영된 항목을 확인하고 중복 코드를 만들지 않는다.

## 권장 수정

기능 변경 없이 아래만 추가/보강한다.

### S00-A. 프로세스/세션 식별 로그

DLL 시작 시 한 번만 다음을 한 줄로 출력한다.

```text
[Session] pid=... dll=HID.DLL module=... gameBase=... version=... renderer=... generation=...
```

`gameBase`가 아직 없으면 초기 시작 시에는 exe base만 기록하고, GameBase 확보 시 generation과 함께 한 번 더 기록해도 된다.

목적은 ‘18:39와 18:43이 같은 프로세스 중복 init인가?’를 다음 로그에서 판단하기 위함이다.

### S00-B. 시작 단계 duration/counter

기존 `PerformanceDiagnostics` 기반을 재사용해 다음을 1회 요약한다.

- `PrepareStratagemFiveUiBridge` 총 호출 횟수
- 첫 성공까지 실제 시간
- startup bridge retry 횟수
- 5번 책략 probe 계열 진단 실행 횟수와 로그 line 수
- `InitCheats` 시도 횟수
- MonthCapture scan duration

**프레임/프로브마다 새 파일 로그를 추가하지 않는다.** 카운터만 메모리에 누적하고 1회/10초 요약으로 기록한다.

### S00-C. AddLog 자체 비용 계측

`bFileLog=ON`일 때만 다음을 저비용으로 합산한다.

- `AddLog` 호출 수
- producer가 `g_logMutex` 대기한 총/최대 시간
- 파일 I/O에 쓴 총/최대 시간

측정 코드가 원래 병목보다 더 비싸지 않도록 per-call 동적 allocation/추가 파일 쓰기를 넣지 않는다.

## 변경하지 말 것

- 아직 `SetStratagemFiveFeature(true)`를 제거하지 않는다. 그것은 S01.
- logger 구조를 아직 async로 바꾸지 않는다. 그것은 S02.
- MonthCapture 패턴 검색 알고리즘을 아직 바꾸지 않는다.
- runtime hook을 감으로 최적화하지 않는다.

## 검증

사용자가 실게임 테스트 가능할 때 같은 세이브/같은 장면에서 최소 다음을 수집한다.

```text
v0.860 / 파일로그 OFF
v0.860 / 파일로그 ON
5번 책략 설정 OFF
5번 책략 설정 ON
```

각 경우 startup hitch 여부, idle FPS/frametime, 약 1분 로그를 보관한다.

## 완료 조건

- 로그 하나만 보고 PID/session을 구분할 수 있음.
- 5번 책략 bridge/probe와 AddLog 비용을 숫자로 볼 수 있음.
- 기능 결과는 기존과 동일함.
- 이 문서 상태표를 갱신하고 commit.

---

# S01. 5번 책략: early bridge / 기능 활성화 / 진단을 분리

## 목표

**사용자가 5번 책략을 OFF로 저장했다면 startup에서 실제 기능을 강제로 ON 요청하지 않게 한다.** 단, UI 생성 시점 때문에 필요한 최소 early bridge 준비는 보존한다.

## 핵심 위험

`PrepareStratagemFiveUiBridge`를 단순 삭제하면 battle UI가 생성된 뒤에는 필요한 hook을 설치할 타이밍을 놓칠 수 있다. 또한 `UpdateSpell5TargetDiagnostics()` 등 이름에 ‘Diagnostics’가 있어도 실제 병력 회복 기능을 포함한 경로가 있으므로 이름만 보고 제거하면 안 된다.

## 수정 전 확인

다음 실제 호출 관계를 추적한다.

```text
MainThread_Initialize
PrepareStratagemFiveUiBridge
SetStratagemFiveFeature
Config에서 5번 책략 설정을 읽는 위치
RefreshStratagemFiveBattleRuntime
UpdateStratagemFiveUiRuntimeProbe
UpdateSpell5TargetDiagnostics
BattleMonitor의 5번 책략 호출부
```

그리고 상태를 최소 다음 세 종류로 구분할 수 있는지 확인한다.

```text
infrastructure/bridge installed
user requested feature enabled
diagnostics/probe enabled
```

## 수정 방향

1. `MainThread_Initialize()`의 무조건 `SetStratagemFiveFeature(true)`를 제거하거나 **실제 기능 enable이 아닌 infrastructure 준비 호출**로 대체한다.
2. 사용자 ON/OFF 의도는 Config 로드 후 실제 설정을 기준으로 적용한다.
3. early timing이 필요한 hook은 기능 ON/OFF와 분리하여 최소한만 설치한다.
4. `UISELPROBE`, `UISIG`, `UICBTGT`, `UICBCODE` 등의 raw disassembly/table dump는 일반 release startup 경로에서 실행하지 않는다.
5. 위 진단이 문제 해결용으로 필요하면 명시적 diagnostics gate(`S8RPK_PERF_DIAGNOSTICS` 또는 별도 debug flag) 뒤로 옮긴다.
6. 진단 로그가 OFF일 때 **로그 문자열만 안 찍고 동일한 대규모 메모리 탐색은 그대로 하는 구조**가 되지 않게 한다. 기능에 불필요한 탐색 자체를 gate한다.
7. 이미 검증된 고정 RVA/signature를 쓸 수 있는 부분이 있다면 매 시작마다 callback table 전체를 dump하여 알아내는 방식을 반복하지 않는다. 단, 게임 버전별 안전 검증은 유지한다.
8. 기능 ON 시 필요한 5번째 버튼, model/registry, 횟수, 회복 기능, 세대 보호는 그대로 유지한다.
9. 생성된 게임 UI 객체의 ownership이 불분명하면 임의 `free/delete`를 추가하지 않는다.
10. 현재 `100 × Sleep(100ms)` 루프는 bridge가 준비된 뒤에도 계속 대기하므로, early bridge가 준비되는 즉시 대기를 종료할 수 있는지 호출 타이밍과 원래 의도를 확인한다. 단순히 10초를 삭제하기 전에 battle UI 생성보다 bridge가 먼저 준비되어야 한다는 안전 조건을 보존한다.

## 예상 수정 파일

실제 호출 확인 후 최소 범위로 결정한다. 현재 후보:

```text
Internal DX11 Base/Source_impl.inc
Internal DX11 Base/Cheats/War/StratagemSlotProbe.cpp
Internal DX11 Base/Cheats/War/StratagemSlotProbe.h
Internal DX11 Base/Cheats/War/Spell5HealProbe.cpp
Internal DX11 Base/BattleMonitor.cpp
Internal DX11 Base/ConfigBase.inc
```

모두를 반드시 바꾸라는 뜻이 아니다. 필요 없는 파일은 건드리지 않는다.

## 검증

### OFF 설정

기대 로그:

```text
bridge가 반드시 필요하다면 최소 설치 성공 로그만 허용
Config: 5번 책략 ... OFF
```

OFF 상태에서 다음이 없어야 한다.

```text
startup의 SetStratagemFiveFeature(true) 효과
[책략5STATE] ON 예약
unified ID5 experiment armed before battle UI
UICBTGT/UICBCODE/UISIG raw dump (diagnostics OFF일 때)
```

### ON 설정

- 다음 전투부터 기존대로 적용되는지
- 5번째 버튼 생성/선택/횟수 반영
- 4번 책략 native 데이터가 손상되지 않는지
- 병력 회복 옵션이 설정값대로 동작하는지
- 전투 종료/다음 전투/저장 로드 후 stale pointer가 없는지

가능하면 책략창 100회 열기/닫기 시 live allocation/Private Bytes가 선형 증가하지 않는지 본다.

## 완료 조건

- OFF에서는 기능 활성화와 무거운 진단이 startup에서 발생하지 않음.
- ON 기능 회귀 없음.
- startup timing dependency는 보존 또는 실제 검증으로 대체됨.
- diff가 5번 책략 관련 최소 파일에 한정됨.

---

# S02. 동기 파일 로그 hot path 제거

## 목표

게임 hook/worker/UI thread가 로그 한 줄을 남기기 위해 파일 open/write/close와 filesystem 조회를 기다리지 않게 한다.

## 수정 전 확인

`showlog.cpp` 외에 `AddLog`의 호출자가 로그 반환 즉시 파일에 반드시 존재해야 하는 계약을 갖는지 검색한다. 일반 진단 로그라면 그런 계약을 만들지 않는다.

정상 unload와 프로세스 종료의 기존 cleanup 경로를 확인한다. logger worker를 추가할 경우 loader lock 안에서 join하지 않는다.

## 권장 구현

### producer

`AddLog()`는:

1. 파일/화면 로그 모두 OFF면 현재처럼 즉시 return.
2. 메시지 formatting 후 메모리 UI queue와 파일 queue에 최소 작업만 수행.
3. 파일 로그는 **bounded queue**에 넣고 빠르게 return.
4. queue가 가득 차면 게임 thread를 오래 기다리지 말고 drop/coalesce하며 누적 drop 수를 기록.

### writer

하나의 writer가:

- 파일을 필요할 때 열고 가능한 동안 유지
- BOM 처리 1회
- timestamp/UTF-8 변환을 가능한 writer 측에서 수행
- 여러 메시지를 batch write
- 파일 rotate/크기 상한을 둘 경우 기존 사용성을 해치지 않는 작은 정책으로 구현
- 종료 시 무한 대기하지 않는 제한된 flush

### UI 로그

- `g_loveLogs`의 앞 원소 `vector.erase(begin())` 반복을 ring/deque 등 bounded 구조로 대체할 수 있음.
- ImGui에 로그를 출력하는 동안 producer mutex를 계속 잡지 않는다. snapshot을 만든 뒤 렌더링한다.
- 기존 최대 1000개 의미는 유지한다.

## 절대 주의

- logger worker를 detached로 만들고 DLL unload 후 코드에 재진입하게 하면 안 된다.
- `DllMain` loader lock에서 긴 join을 하지 않는다.
- UTF-8/CP949 처리와 BOM을 깨뜨리지 않는다.
- 이 단계에서 다른 기능의 로그 문구를 대량 정리하지 않는다.

## 검증

파일 로그 ON/OFF로 각각:

```text
일반 idle
5번 책략 diagnostics stress
초당 수백 로그 stress(테스트 경로가 있을 때)
디스크 쓰기 지연/실패
정상 DLL unload 가능 시 unload
프로세스 종료
```

확인할 것:

- queue가 무한 증가하지 않음
- 로그 메시지 손상 없음
- 파일 로그 OFF 비용이 사실상 기존 빠른 return 수준
- game hook이 파일 I/O를 직접 기다리지 않음
- writer thread/handle 누적 없음

---

# S03. 지속 렉 후보 실제 계측

## 목표

startup hitch를 S01/S02로 줄여도 ‘게임 플레이 내내 렉’이 남는 경우, **실제 고빈도 path를 수치로 찾는다.** 이 단계에서는 원인을 추측해 고치지 않는다.

## 계측 대상

기존 `PerformanceDiagnostics`에 가능한 한 다음을 합산 counter/timer로 추가한다.

```text
Menu::Loops                 calls / total / max
Present/Overlay             calls / total / max
SpeedHack_Update            calls / total / max (기존 계측 확인)
RoninMonitor update         calls
Ronin full 5102 scan        calls / total / max
Battleunit capture hook     calls / sampled total
Domestics hook              calls / sampled total
IsValidPtr                  calls (필요할 때만 샘플링)
FindPattern                 calls / scanned bytes / total
```

고빈도 hook에서는 매 호출 파일 로그를 남기지 않는다. atomic counter 또는 thread-local/sample 방식 등 계측 자체의 오버헤드를 제한한다.

## 테스트 매트릭스

가능한 한 같은 세이브/카메라/해상도/그래픽/FPS 제한에서:

| 조건 | 최소 관측 | 목적 |
|---|---:|---|
| 치트 로드 / UI 숨김 | 1~3분 | 기본 상시 비용 |
| UI 펼침 | 1~3분 | Present/UI 비용 |
| 전략 화면 idle | 1~3분 | worker 비용 |
| 전투 | 1~3분 | battle hook 비용 |
| 배속 1x | 1~3분 | 기준 |
| 배속 2x/5x | 각 1~3분 | 치트 timer 호출률 변화 |
| 파일 로그 OFF/ON | 각 동일 조건 | logger 영향 |

가능하면 CPU뿐 아니라 frametime, Private Bytes, Working Set도 같이 기록한다.

## 결과 판정

- **호출 수가 많은 것**과 **총 시간이 큰 것**을 구분한다.
- max 한 번만 큰 startup 작업과 지속적으로 total이 커지는 작업을 구분한다.
- GPU 사용률 상승을 CPU scanner 하나로 직접 설명하지 않는다.

## S05 진입 조건

S03 결과에서 실제 총시간/frametime 영향이 확인된 항목만 S05에서 수정한다. 확인되지 않은 후보는 건드리지 않는다.

---

# S04. MonthCapture scan 및 worker lifecycle 보완

## 목표

현재 worker로 실행되는 월 캡처 검색을 **중복/수명 문제 없이 한 세션에 필요한 만큼만 수행**하도록 한다. 이 단계는 S01/S02보다 우선하지 않는다.

## 수정 전 확인

1. `SetMonthCapture(true/false)` 호출 위치와 호출 빈도.
2. 세이브 로드/게임 세대 변경 시 재설치가 필요한지.
3. hook address가 모듈 수명 동안 고정인지.
4. 기존 T07 worker lifecycle 패턴을 main에서 재사용할 수 있는지.
5. 정상 DLL unload에서 현재 detached scan thread가 살아남을 가능성.

## 수정 방향

- 동일 module/build/session에서 이미 성공한 exact signature 결과는 검증 후 재사용 가능 여부를 검토.
- exact signature가 성공하면 wildcard fallback은 실행하지 않음 — 현재 장점 유지.
- 실패를 tight loop로 반복하지 않도록 backoff/한도 유지.
- `volatile bool`만으로 수명 안전성이 보장된다고 가정하지 않는다. stop/generation 또는 기존 공통 worker ownership 모델을 사용한다.
- worker handle을 닫아 ownership을 잃은 상태에서 DLL unload가 가능한 구조라면 정리한다.
- scanner를 메인 thread로 옮기지 않는다.
- S03/S00 측정에서 비용이 미미하면 복잡한 scanner 최적화는 하지 않는다.

## 완료 조건

- 한 세션에 불필요한 중복 scan 없음.
- unload/toggle 중 오래된 worker가 hook을 뒤늦게 설치하지 않음.
- 월 읽기/연회/중개 등 의존 기능 회귀 없음.

---

# S05. S03에서 확인된 runtime 병목만 수정

**현재 BLOCKED. S03 측정 전에는 시작하지 않는다.**

가능한 분기 예시는 아래와 같지만, 실제 측정 결과가 없는 항목은 수정하지 않는다.

### RoninMonitor가 실제로 과호출된 경우

- 기존 평정/내정 상태 전환 기반 의미를 보존.
- 같은 상태에서 불필요하게 5102슬롯 scan이 반복되는 원인만 제거.
- scan 결과 cache/generation을 사용하되 사망/등용 알림 누락이 없게 함.

### Battleunit/내정 hook이 hot path인 경우

- hook 안에서 allocation, formatting, logging, VirtualQuery 등 고비용 작업이 있는지 먼저 확인.
- hook은 필요한 최소 데이터만 캡처하고 무거운 후처리는 안전한 낮은 빈도 경로로 이동할 수 있는지 검토.
- 게임이 hook 반환 전에 결과를 필요로 하는 작업은 임의 deferred하지 않는다.

### Present/Overlay가 hot path인 경우

- 실제 표시 요소가 없을 때 불필요한 유지 작업을 줄임.
- 알림/독립 창 등 메뉴와 별개로 보여야 할 UI를 실수로 끄지 않음.
- 무거운 game data scan을 render path에서 제거할 때 lifetime과 update latency를 보존.

### SpeedHack 관련 호출률이 문제인 경우

- 기존 real-clock 분리와 T01 계열 개선이 main에 이미 반영됐는지 먼저 확인.
- 시간 API hook의 게임 동작을 임의로 바꾸지 않고, 치트 내부 scheduler만 실제 시간 기준으로 유지.

---

# S06. 통합 검증, 문서 갱신, 제출 준비

## 코드 diff 검증

최종적으로 반드시 확인한다.

```text
base: main 또는 실제 작업 시작 기준 SHA
head: perf/user-lag-investigation-20261003
```

확인 항목:

- 의도한 파일 외 변경 없음
- 대형 파일 전체 재format 없음
- CRLF/LF 대량 변환 없음
- generated/binary 파일 불필요 변경 없음
- `main` 직접 commit 없음

## 최소 기능 회귀 시나리오

1. 치트 로드, 설정 파일 로드.
2. 5번 책략 OFF startup.
3. 5번 책략 ON → 다음 전투 적용.
4. 전투 종료 → 다음 전투.
5. 저장게임 로드.
6. 책략창 반복 open/close.
7. 파일 로그 OFF/ON.
8. UI 숨김/펼침.
9. 전략 화면 idle.
10. 전투 idle/action.
11. MonthCapture 의존 연·월 읽기 및 관련 기능.
12. 정상 종료 및 가능하면 정상 unload.

## 성능 판정

다음처럼 표현한다.

```text
정적 확인: 코드 구조가 제거/변경됨
계측 확인: 호출 수/시간이 감소함
실게임 확인: 동일 조건에서 frametime/FPS/CPU가 개선됨
```

세 가지를 섞어서 ‘해결됨’이라고 쓰지 않는다. 실게임 측정이 없으면 성능 개선 수치를 주장하지 않는다.

---

## 6. 이번 작업에서 의도적으로 건드리지 않는 것

S03에서 별도 증거가 나오기 전에는 아래를 이유 없이 손대지 않는다.

- AI 전투 로직 자체
- 포로 관리/원군/도시 기능의 게임 밸런스
- D3D renderer 전면 리팩터링
- 모든 memory scanner 공통 재작성
- SpeedHack 알고리즘 전면 교체
- `StratagemSlotProbe.cpp` 전체 구조 리팩터링
- 기존 T00~T07 성능 브랜치의 작업을 다시 통째로 적용
- 5번 책략 객체 ownership을 확인하지 않은 상태의 임의 free

---

## 7. 단계별 작업 기록 템플릿

각 단계가 끝날 때 아래 형식을 이 절 아래에 추가한다.

```markdown
### YYYY-MM-DD / S0X

- 작업 브랜치: perf/user-lag-investigation-20261003
- 시작 HEAD: <sha>
- 종료 HEAD: <sha 또는 commit 전이면 미커밋>
- 변경 파일:
  - path
- 확인한 사실:
  - ...
- 핵심 변경:
  - ...
- 의도적으로 변경하지 않은 것:
  - ...
- diff 확인:
  - 파일 수 / +/- lines
- 빌드:
  - 미실행 / 성공 / 실패 + 실제 명령과 결과
- 실게임 테스트:
  - 미실행 / 결과
- 남은 불확실성:
  - ...
- 다음 재개 지점:
  - S0X의 어떤 항목부터
```

---

## 8. 작업 기록

### 2026-10-03 / PLAN

- 작업 브랜치: `perf/user-lag-investigation-20261003`
- 기준 `main` HEAD: `996cb6e78eb5a6908a7572059393e76187a4d65c`
- 기준 commit message: `v0.860 배포`
- 변경 파일:
  - `PERFORMANCE_USER_LAG_WORKPLAN.md` 신규 추가
- 확인한 사실:
  - 제보 로그는 `V0.850` 실행 로그다.
  - 현재 `main`은 `v0.860 배포`다.
  - 현재 v0.860에도 startup에서 `PrepareStratagemFiveUiBridge()` 후 `SetStratagemFiveFeature(true)`를 설정 로드 전에 호출하는 코드가 남아 있다.
  - 현재 `AddLog()`는 파일 로그 한 줄마다 global mutex 안에서 filesystem 조회 + open/write/close를 수행한다.
  - 현재 MonthCapture 전체 module scan은 worker thread에서 실행한다.
  - 기존 `perf/T00`~`perf/T07` 계열 branch가 존재하며, main에도 일부 성능 개선 흔적이 이미 포함되어 있다. 이전 계획을 처음부터 재적용하면 안 된다.
- 코드 변경:
  - 없음.
- 빌드:
  - 미실행.
- 실게임 테스트:
  - 미실행.
- 남은 불확실성:
  - v0.860에서 사용자 렉이 동일하게 재현되는지 미확인.
  - 로그의 두 초기화 묶음이 같은 PID인지 다른 실행인지 미확인.
  - 지속 렉의 실제 hot path 미확인.
- 다음 재개 지점:
  - **S00: 현재 branch/HEAD 재확인 → 기존 PerformanceDiagnostics 상태 확인 → 기능을 바꾸지 않는 식별/계측부터 시작.**

### 2026-10-03 / S00 구현 진행

- 작업 브랜치: `perf/user-lag-investigation-20261003`
- 시작 HEAD: `9beda193a48137d424c80ec65aa3f327939a22dc`
- 계측 코드 HEAD(문서 갱신 전): `68a6a350f78241ef42cd71b74882d98020abbd85`
- 변경 파일:
  - `Internal DX11 Base/PerformanceDiagnostics.h`
  - `Internal DX11 Base/Source.cpp`
  - `Internal DX11 Base/showlog.cpp`
  - `Internal DX11 Base/Cheats/System/MonthCapture.cpp`
- 확인한 사실:
  - `main`은 작업 시작 시점에도 `996cb6e...`로 변함없었다.
  - 기존 `PerformanceDiagnostics.h`는 `S8RPK_PERF_DIAGNOSTICS=1`, `QueryUnbiasedInterruptTime`, 10초 집계 보고를 이미 제공하므로 새 진단 프레임워크를 만들 필요가 없었다.
  - `Source_impl.inc`의 startup loop는 bridge가 첫 호출에서 성공해도 `Sleep(100)`을 100회 모두 수행한다. 즉 약 10초 고정 대기다.
  - Config 로드는 `Menu::Loops()`에서 p1이 유효해질 때 지연되므로 startup의 `SetStratagemFiveFeature(true)`가 사용자 설정 OFF보다 먼저 실행되는 구조가 현재도 맞다.
- 핵심 변경:
  - 기존 진단 프레임워크에 `StartupBridgePrepare`, `InitCheatsAttempt`, `MonthCaptureScan`, `AddLogCall`, `AddLogMutexWait`, `AddLogFileIo` metric을 추가했다.
  - `AddLog` 자체를 계측해도 10초 보고가 다시 `AddLog`를 호출하여 재귀하지 않도록 `PerfRecordNoReport()`를 추가했다.
  - `Source.cpp`의 기존 wrapper 구조를 활용해 대형 `Source_impl.inc`를 수정하지 않고 `LoadEarlyLogConfig`, `PrepareStratagemFiveUiBridge`, `InitCheats` 호출을 계측 wrapper로 연결했다.
  - startup 로그에 `[Session] pid=... init=... dll=... module=... exeBase=... version=... renderer=pending` 식별자를 추가했다.
  - `AddLog`에서 mutex 대기 시간, 전체 호출 시간, 파일 I/O 시간/기록 bytes를 측정하도록 했다. 파일/화면 로그가 모두 OFF일 때의 기존 빠른 return은 그대로 유지했다.
  - MonthCapture worker에서 전체 scan duration과 module image bytes, 설치 성공 여부를 기존 10초 집계 metric에 기록하도록 했다.
- 의도적으로 변경하지 않은 것:
  - `SetStratagemFiveFeature(true)`는 아직 그대로다.
  - startup의 100 × `Sleep(100)` 고정 대기도 아직 그대로다.
  - logger를 비동기로 바꾸지 않았다.
  - MonthCapture worker ownership/stop flag를 아직 바꾸지 않았다.
  - `StratagemSlotProbe.cpp`와 `Source_impl.inc` 대형 파일은 수정하지 않았다.
- diff 확인:
  - 기준 `main@996cb6e...` 대비 문서 포함 5개 파일만 변경됨.
  - 코드 변경은 `MonthCapture.cpp` +13, `PerformanceDiagnostics.h` +46 변화, `Source.cpp` +72, `showlog.cpp` +32 변화 수준이며 대형 파일 전체 재format은 없음.
- 빌드:
  - **미실행.** 현재 작업 환경에서 Windows/MSVC Release x64 빌드를 실제 실행하지 않았다.
- 실게임 테스트:
  - **미실행.** 계측 수치와 기능 회귀는 아직 확인되지 않았다.
- 남은 불확실성:
  - 계측 wrapper가 실제 MSVC 빌드에서 문제없이 컴파일되는지 미확인.
  - v0.860 실게임에서 `[Session]`, `[Perf:T00]`가 기대대로 출력되는지 미확인.
  - AddLog I/O가 실제 사용자 환경에서 얼마만큼 hitch에 기여하는지 미확인.
  - startup 10초 고정 대기를 줄여도 early bridge timing이 안전한지는 S01에서 검증 필요.
- 다음 재개 지점:
  - **S00 검증: branch 최신 HEAD 확인 → Release x64 빌드 → `S8RPK_PERF_DIAGNOSTICS=1`로 v0.860 실행 → 파일로그 OFF/ON 및 5번 책략 OFF/ON 조건의 로그 수집. 검증 전 S01 기능 변경 금지.**

---

## 9. 완료 판단 기준

이 작업은 단순히 코드를 ‘최적화했다’고 끝내지 않는다. 다음을 만족해야 한다.

1. 5번 책략 OFF startup에서 불필요한 실제 feature ON 요청과 대량 진단 probe가 사라졌다는 코드/로그 증거가 있다.
2. 파일 로그 producer가 매 줄 동기 open/write/close 때문에 게임 hook을 붙잡지 않는다.
3. 지속 렉이 있었다면 S03 계측으로 실제 hot path를 식별했고, 수정은 그 증거에 기반한다.
4. MonthCapture를 포함한 worker가 세션/종료 수명에서 안전하다.
5. 기존 기능의 ON/OFF, 전투 전환, 저장 로드, UI 동작에 회귀가 없다.
6. 최종 diff가 작고 관련 파일에 한정된다.
7. 빌드/실게임을 실제로 하지 않았다면 성공했다고 표현하지 않는다.
8. 이 문서의 상태표와 마지막 작업 기록만 읽어도 다음 작업자가 정확한 branch와 재개 지점을 알 수 있다.
