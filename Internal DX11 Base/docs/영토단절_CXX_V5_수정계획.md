# 영토 단절 이동·배정·AI 차단 — C++ v5 수정 계획

> **이 문서를 이후 C++ 작업의 단일 기준 문서로 사용한다.**
>
> 새 채팅/새 세션에서는 과거 대화만 믿고 바로 수정하지 말고, 먼저 이 문서와 현재 `main` 상태를 다시 확인한 뒤 이어서 작업한다.

## 0. 작업 기준

- 저장소: `WOOSEOK99/S8RPKCheats`
- 기준 브랜치: `main`
- 계획 작성 직전 기준 HEAD: `ecec7ccd3ff6fe15d5b7dbe53588f60ac780d66e`
- 대상 기능: 영토 단절 이동·배정·AI 차단
- 주 수정 파일:
  - `Internal DX11 Base/Cheats/War/IsolatedTerritoryMovement.h`
  - `Internal DX11 Base/Cheats/War/IsolatedTerritoryMovementFeature.h`
  - 필요 시 `Internal DX11 Base/Cheats/War/IsolatedTerritoryMovementDiagnostic.h`

### 현재 기준 blob SHA

계획 작성 시점 `main`:

- `IsolatedTerritoryMovement.h`
  - `d6ffef80c288020a4ee02520f8f17270839255b4`
- `IsolatedTerritoryMovementFeature.h`
  - `5afd8060ea0ba1c94a8978514d57f857a5fbf41f`
- `IsolatedTerritoryMovementDiagnostic.h`
  - `e714db78ea42200d0b93942848eddc468bab1524`

SHA가 달라졌다고 자동으로 되돌리지 않는다. 현재 코드를 다시 읽고 이 문서와 대조한 뒤 아직 필요한 변경만 적용한다.

---

## 1. 현재 실제 문제

현재 C++ 구현은 다음 10개 지점을 사용한다.

- `0x1961B3C`
- `0x196280C`
- `0x1960F30`
- `0x144948B`
- `0x144ABE6`
- `0x17B0BA0`
- `0x1901F77`
- `0x1963D3F`
- `0x1961BD5`
- `0x19628D5`

이 중 `SAN8RPK.exe+17B0BA0`이 external `version.dll`과 충돌한다.

현재 `ValidateOriginalState()`는 이 주소의 14바이트가 정확히 아래 순정 값이어야 통과한다.

```text
48 85 C9 0F 84 EA 12 00 00 44 88 4C 24 20
```

하지만 `version.dll`이 존재하는 실제 실행에서는 이 지점이 다음 형태로 이미 후킹되어 있다.

```text
E9 <rel32> 90 90 90 90 44 88 4C 24 20
```

실제 preflight에서도 나머지 기존 지점들은 원본과 일치했고 `+17B0BA0`만 불일치했다.

현재 적용 코드는 validation 통과 후 다시 `+17B0BA0`을 `gHook6Stub`으로 absolute jump 패치한다.

따라서:

1. validation만 완화하면 안 된다.
2. `version.dll` 훅 위에 `gHook6Stub`을 덮어쓰면 안 된다.
3. 이 충돌은 Hook6의 소유 위치를 바꾸는 방식으로 해결한다.

---

## 2. 채택할 최종 방향 — docs v5 방식

기존의 `version.dll bridge` 내부 주소 추적 방식은 사용하지 않는다.

대신 `+17B0BA0` 공용 함수 자체를 전혀 수정하지 않고, 현재 Hook6가 return address로 구분하던 세 호출자 CALL을 직접 가로챈다.

### 기존 Hook6가 구분하던 세 호출자

```text
CALL 위치      반환 주소       원래 호출 대상
+144A321      +144A326        +17B0BA0
+144A8F5      +144A8FA        +17B0BA0
+145096D      +1450972        +17B0BA0
```

### 새 구조

```text
기존:
세 caller
  -> SAN8RPK+17B0BA0
  -> gHook6Stub에서 return address 판별
  -> version.dll과 충돌

변경:
세 caller CALL
  -> 새 AI same-force transfer wrapper
      -> 차단: RET
      -> 허용: SAN8RPK+17B0BA0으로 tail JMP
                -> 순정 또는 version.dll 기존 hook 그대로 실행
```

이 방식에서는:

- `version.dll` 로드 여부를 확인할 필요 없음
- `version.dll` MD5 필요 없음
- `version.dll` 내부 RVA 필요 없음
- `version.dll+14FE38` 같은 trampoline slot 수정 없음
- wrapper return-address `+0x80` 보정 필요 없음
- `SAN8RPK.exe+17B0BA0` 원본 여부를 기능 활성화 조건으로 삼지 않음
- 단, 새로 패치하는 3개 CALL 위치와 기존 9개 지점은 엄격하게 검증함

---

## 3. 유지할 기존 C++ 로직

가능한 한 그대로 유지한다.

- `AreConnected()`
- `GetOwnerRoot()`
- `IsGameObjectValid()`
- `MovementContextAllowed()`
- `FilterConnectedList()`
- `MovementCheck78()`
- `MovementCheck910()`
- 기존 Hook1 / Hook2 / Hook3 / Hook45
- 기존 Hook7/8 thunk
- 기존 Hook9/10 thunk
- 현재 UI / Config / feature toggle 구조

이번 작업은 기능 재작성이나 대규모 리팩터링이 아니다.

---

## 4. 제거할 기존 Hook6 책임

### `IsolatedTerritoryMovement.h`

다음 요소는 새 설계에서 제거 또는 미사용 처리한다.

- `kHook6 = 0x17B0BA0`를 patch target으로 사용하는 로직
- `kHook6Original`을 `ValidateOriginalState()`의 필수 원본 검사로 사용하는 로직
- `gHook6Stub`
- `BuildHook6Stub()`
- `PatchAbsoluteJump(gBase + kHook6, ...)`

`+17B0BA0`은 새 구현에서 **원래 호출 대상 주소**로만 사용한다.

즉 상수 자체가 필요하면 `kNativeMovementEntry` 같은 의미 있는 이름으로 유지할 수 있지만, hook site 의미로 남기지 않는다.

---

## 5. 새 AI caller wrapper 설계

새 wrapper는 세 CALL에서 공통으로 사용한다.

입력 의미:

```text
RCX = actor / 무장
RDX = destination / 도착 도시
R8  = force / 이동에 전달되는 세력
```

현재 `MovementContextAllowed(context, destination, owner)`와 인자 의미가 대응하므로 이 함수를 재사용한다.

### wrapper 동작

1. 원래 호출 시점의 volatile general register 보존
2. RFLAGS 보존
3. XMM0~XMM5 보존
4. `MovementContextAllowed(RCX, RDX, R8)` 호출
5. 결과 검사
6. 허용이면 모든 상태 복원 후 `SAN8RPK.exe+17B0BA0`으로 tail JMP
7. 차단이면 모든 상태 복원 후 RET

### 중요 ABI 조건

`MovementContextAllowed()`의 반환형은 C++ `bool`이다.

프로젝트 실게임 검증에서 bool 반환은 **AL만 유효하다고 취급해야 한다.**

따라서 새 wrapper는 반드시:

```asm
test al, al
```

을 사용한다.

`test eax,eax`를 사용하지 않는다.

기존 `PrepareBoolAbiFix()`처럼 사후 바이트 교정에 의존하기보다, 새 wrapper builder에서 처음부터 `test al,al`를 생성하는 것을 우선한다.

---

## 6. 새 CALL hook 3개

추가 상수 예:

```text
kAiMovementCall1 = 0x144A321
kAiMovementCall2 = 0x144A8F5
kAiMovementCall3 = 0x145096D
```

계획 작성 시 docs v5 기준 원본 CALL 바이트:

```text
+144A321 : E8 7A 68 36 00
+144A8F5 : E8 A6 62 36 00
+145096D : E8 2E 02 36 00
```

세 CALL 모두 원래 목적지가 `+17B0BA0`인지 현재 코드와 함께 다시 검증한다.

### thunk / allocation

`PatchCall()`은 rel32 범위 제한이 있다.

새 wrapper는 세 CALL 모두에서 ±2GB 안에 있어야 한다.

기존 `CommitCode(..., nearAddress)` / `AllocNear()` 구조를 활용하되, 세 site가 모두 도달 가능한 주소인지 확인한다.

한 wrapper를 세 site에서 공유할 수 있으면 공유한다.

범위가 보장되지 않으면 각 site 근처에 작은 near thunk를 두고 공통 wrapper로 absolute jump하는 방식을 사용한다.

임의로 호출 instruction 길이를 늘리거나 주변 코드를 덮지 않는다.

---

## 7. `ValidateOriginalState()` 변경 계획

기존 9개 기능 지점 검증은 유지한다.

### 삭제

```text
ReadEq(gBase + 0x17B0BA0, kHook6Original, 14)
```

### 추가

세 caller CALL 원본 검증:

```text
+144A321
+144A8F5
+145096D
```

결과적으로 기능 활성화 전 검증 대상은 총 12개 patch site가 된다.

중요:

- `+17B0BA0`이 순정인지 version hook인지에 따라 활성화를 거부하지 않는다.
- 그러나 새 3개 caller CALL이 이미 다른 코드로 변경되어 있으면 안전하게 거부한다.
- 다른 구현이 해당 caller CALL을 이미 소유하고 있다면 중첩 patch하지 않는다.

---

## 8. `PrepareStubs()` / bool ABI 수정 계획

### `PrepareStubs()`

- `gHook6Stub` 생성 제거
- 새 AI caller wrapper 또는 near thunk 준비 추가

### `IsolatedTerritoryMovementFeature.h`

현재 `PrepareBoolAbiFix()`는 다음 5개 stub을 수정한다.

```text
gHook1Stub
gHook2Stub
gHook3Stub
gHook45Stub
gHook6Stub
```

변경 후:

- 기존 4개 bool consumer 교정은 유지
- `gHook6Stub` offset 132 교정 제거
- 새 AI wrapper는 builder에서 직접 `test al,al` 생성

즉 새 wrapper에 사후 offset 기반 bool patch를 추가하지 않는 방향을 우선한다.

---

## 9. 적용 순서

최소 변경 원칙으로 다음 순서로 진행한다.

### Stage 0 — 계획/문서 정리

상태: 완료 예정

- 이 문서를 단일 C++ 기준 계획으로 추가
- 구형 `version.dll bridge` 계획 문서 제거
- v5 근거 자료는 유지

### Stage 1 — Hook6 제거 + 새 caller 상수/원본 검증 추가

목표:

- `+17B0BA0`을 patch site에서 제거
- 새 3 caller CALL 상수/원본 바이트 추가
- `ValidateOriginalState()`를 12개 patch site 기준으로 변경
- 아직 실제 새 wrapper 연결은 하지 않음

주의:

중간 커밋이 사용자 기능을 깨뜨리는 상태가 되지 않도록, 실제 소스 수정에서는 Stage 1과 Stage 2를 같은 커밋으로 묶는 것이 더 안전하면 묶는다.

### Stage 2 — 새 AI caller wrapper 생성

목표:

- `MovementContextAllowed()` 재사용
- register / flags / XMM 보존
- `test al,al`
- allow -> `+17B0BA0` tail JMP
- block -> RET

### Stage 3 — 실제 3 CALL patch 연결

목표:

- `+144A321`
- `+144A8F5`
- `+145096D`

을 새 wrapper 또는 near thunk로 연결한다.

기존 Hook1/2/3/4/5/7/8/9/10 patch는 그대로 유지한다.

### Stage 4 — bool ABI feature 정리

- `gHook6Stub` 관련 bool fix 제거
- 기존 4개 bool fix 유지
- 새 wrapper는 자체 `test al,al`

### Stage 5 — restore 안전성 강화

현재 `RestoreAllPatches()`는 활성화 이후 다른 프로그램이 patch site를 바꿔도 저장된 before 바이트를 다시 덮을 수 있다.

후속 안전 강화로 다음을 적용한다.

- 각 `PatchRecord`에 `after`도 기록
- 해제 시 현재 바이트가 `after`와 같을 때만 `before` 복원
- 현재가 다르면 외부 변경 충돌로 판단하고 강제 덮어쓰기 금지
- 부분 적용 실패 rollback도 가능한 범위에서 동일 원칙 사용

이 변경은 Hook6 제거 작업과 분리 가능하다.

### Stage 6 — 진단 개선

필요 시 `IsolatedTerritoryMovementDiagnostic.h`에서:

- 기존 9개 patch site
- 새 AI caller 3개
- `+17B0BA0` 현재 바이트는 참고용 read-only 출력

을 구분해서 표시한다.

`+17B0BA0`이 순정이 아니라고 FAIL 처리하지 않는다.

---

## 10. 적용/해제 시 안전 조건

최소 필수 조건:

### 활성화 전

- 12개 patch site 원본 검증
- stub/thunk 준비 성공
- 실제 write 직전에도 가능하면 현재 바이트 재확인

### 활성화 중 실패

- 이미 적용한 patch를 역순 rollback
- rollback 실패 시 명확한 오류 로그
- `gApplied=false` 유지

### 비활성화

1차 구현에서는 기존 구조를 유지할 수 있으나, 최종적으로는 patch ownership 검증을 추가한다.

외부 프로그램이 활성화 후 같은 site를 변경했다면 무조건 원본으로 덮어쓰지 않는다.

---

## 11. 스레드 정지 정책

docs v5 CE 구현은 patch 중 모든 게임 스레드를 잠시 정지하고 수정 범위 안에 RIP가 없는지 확인한다.

이 방식은 안전성이 높지만 현재 C++ 최소 수정의 필수 1차 범위로 강제하지 않는다.

우선순위:

1. Hook6/version 충돌 제거
2. 새 3 caller hook 정상화
3. patch ownership restore 강화
4. 필요 시 thread suspend / RIP guard 도입

스레드 suspend를 추가할 경우 기존 lifecycle을 충분히 확인한 뒤 별도 단계로 구현한다.

---

## 12. 절대 하지 말아야 할 것

### 금지 1

`ValidateOriginalState()`에서 `+17B0BA0` 검사만 무시하고 기존 Hook6 patch를 그대로 유지하지 않는다.

### 금지 2

`+17B0BA0`의 version.dll E9 hook 위에 absolute jump를 덮지 않는다.

### 금지 3

새 C++ 구현에서 version.dll MD5 / 내부 RVA / trampoline slot을 다시 의존하지 않는다.

### 금지 4

`hid.dll`을 외부 충돌 DLL로 취급하지 않는다.

### 금지 5

새 AI wrapper에서 `test eax,eax`를 사용하지 않는다. C++ bool은 `AL` 기준으로 처리한다.

### 금지 6

세 caller CALL 중 이미 다른 구현이 수정한 곳을 강제로 덮어쓰지 않는다.

### 금지 7

이번 수정과 무관한 War 기능/UI/Config/trait 코드 리팩터링을 섞지 않는다.

### 금지 8

사용자 요청 없이 대규모 formatting, 파일 이동, 이름 변경을 하지 않는다.

---

## 13. 검증 기준

### 코드 검증

- `+17B0BA0`에 대한 patch 코드가 없어야 함
- `+17B0BA0`에 대한 원본 일치 필수 validation이 없어야 함
- 새 3 caller CALL 원본 검증이 있어야 함
- allow path는 `+17B0BA0`으로 tail JMP
- block path는 RET
- 새 wrapper의 bool 결과 검사는 `test al,al`

### 최소 실게임 테스트 매트릭스

| 환경 | 기대 결과 |
|---|---|
| external `version.dll` 없음 | 기존 기능과 동일하게 이동/배정/AI 제한 동작 |
| external `version.dll` 있음 | 기능 활성화 성공, VERSION의 `+17B0BA0` hook 보존 |
| 새 3 caller 중 하나가 외부 수정됨 | 안전하게 활성화 거부 |
| ON -> OFF -> ON | 반복 적용/복원 정상 |
| 동일 세력 연결 도시 | 기존 이동 처리 계속 |
| 동일 세력 단절 도시 | 대상 AI 이동 차단 |
| 세력 변경/포로/등용 등 적용 범위 밖 상황 | 본 도로 필터가 임의로 차단하지 않음 |

### 중요한 메모리 확인

external `version.dll` 환경에서 ON/OFF 전후:

```text
SAN8RPK.exe+17B0BA0
```

의 기존 version hook 바이트가 변하지 않아야 한다.

새 3 caller CALL만 우리 구현으로 바뀌어야 한다.

---

## 14. 현재 검증 상태

v5 자료 기준:

- CE 7.2 Lua 구문: PASS
- mock lifecycle: 56건
- graph/source/assignment emulator: 76건
- AI callsite ABI emulator: 63건
- DLL entry / internals preserved in mock: 확인
- live CE activation / 실제 게임 진행 검증: 아직 미확인

따라서 v5 설계는 정적/에뮬레이션 수준의 근거는 충분하지만, C++ 포팅 후 실게임 검증이 최종 완료 조건이다.

---

## 15. 반드시 남겨둘 근거 자료

다음 자료는 삭제하지 않는다.

```text
Internal DX11 Base/docs/v5_개발자안내.txt
Internal DX11 Base/docs/v5_검증상태.txt
Internal DX11 Base/docs/validation_v5.json
Internal DX11 Base/docs/static_findings.json
Internal DX11 Base/docs/live_preflight.json
Internal DX11 Base/docs/data_v5.json
Internal DX11 Base/docs/runtime_v5.lua
Internal DX11 Base/docs/ai_call_wrapper_v5.asm
Internal DX11 Base/docs/삼8PK_영토단절_이동배정_AI차단_v5_DLL독립.CT
Internal DX11 Base/docs/삼8PK_영토단절_이동배정_AI차단_v5_DLL독립.cea
```

이 파일들은 설계 근거/원본 데이터/검증 자료이므로 새 C++ 구현이 완료되기 전까지 보존한다.

---

## 16. 새 채팅에서 반드시 먼저 할 일

다른 채팅창에서 작업을 이어갈 때 아래 순서대로 한다.

1. 이 파일을 읽는다.
   - `Internal DX11 Base/docs/영토단절_CXX_V5_수정계획.md`
2. 현재 `main` HEAD를 확인한다.
3. 아래 세 파일의 현재 blob SHA를 다시 확인한다.
   - `IsolatedTerritoryMovement.h`
   - `IsolatedTerritoryMovementFeature.h`
   - `IsolatedTerritoryMovementDiagnostic.h`
4. 현재 코드에서 `BuildHook6Stub`, `ValidateOriginalState`, `PrepareStubs`, `RestoreAllPatches`, `PrepareBoolAbiFix` 상태를 다시 읽는다.
5. `docs/v5_개발자안내.txt`, `data_v5.json`, `ai_call_wrapper_v5.asm`을 필요 범위만 다시 확인한다.
6. 이미 완료된 stage를 실제 코드와 commit으로 확인한다.
7. 아직 완료되지 않은 다음 stage만 최소 diff로 진행한다.
8. 빌드/실게임 테스트는 사용자가 직접 한다고 하면 임의 실행하지 않는다.
9. 단계 완료 후 이 문서 하단 진행표와 변경 기록을 갱신한다.

---

## 17. 진행표

- [x] Stage 0 — v5 방향 검토 및 단일 C++ 계획 작성
- [ ] Stage 1 — Hook6 patch/validation 제거 + 새 caller 3개 정의
- [ ] Stage 2 — AI caller wrapper 구현 (`test al,al`)
- [ ] Stage 3 — 새 caller 3개 CALL patch 연결
- [ ] Stage 4 — `PrepareBoolAbiFix()`에서 Hook6 의존 제거
- [ ] Stage 5 — restore ownership 검증 강화
- [ ] Stage 6 — diagnostic 갱신
- [ ] Stage 7 — 실게임 검증 및 최종 문서화

---

## 18. 변경 기록

### 2026-10-03 — 계획 작성

- 기준 HEAD: `ecec7ccd3ff6fe15d5b7dbe53588f60ac780d66e`
- 결론: 기존 version.dll bridge 복원 방식 대신 docs v5의 caller-hook DLL 독립 설계를 채택
- 소스 코드 변경: 없음
- 빌드/테스트: 수행하지 않음

향후 각 stage 완료 시 아래에 반드시 기록한다.

- 날짜
- stage
- 변경 파일
- 핵심 변경
- commit SHA
- 빌드 여부
- 실게임 테스트 여부
- 남은 문제
