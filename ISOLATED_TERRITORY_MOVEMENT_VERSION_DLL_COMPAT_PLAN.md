# 단절 영토 무장 이동 제한 — version.dll 호환 복원 계획

> 대상 기능: Cheat Engine 원본 CT Entry ID `92011` — **"단절된 영토 간 무장 이동 불가능"**  
> C++ 대상: `Internal DX11 Base/Cheats/War/IsolatedTerritoryMovement.h`  
> 목적: 원본 CT에 존재하는 `version.dll` 연계 호환 경로를 현재 C++ 구현에 **최소 변경으로 복원**한다.

---

## 0. 이 문서의 역할

이 문서는 채팅이 끊기거나 새 채팅으로 넘어가도 작업을 그대로 이어가기 위한 기준 문서다.

다음 채팅에서는 원본 CT 파일이 다시 없어도 우선 이 문서를 읽고 작업을 시작할 수 있어야 한다. 구현 중 원본 CT의 다른 부분을 새로 검증해야 할 때만 CT 재업로드를 요청한다.

### 새 채팅에서 반드시 먼저 할 일

1. 이 문서를 읽는다.
2. 현재 `main`의 아래 파일들을 다시 fetch한다.
   - `Internal DX11 Base/Cheats/War/IsolatedTerritoryMovement.h`
   - `Internal DX11 Base/Cheats/War/IsolatedTerritoryMovementFeature.h`
   - 필요 시 `Internal DX11 Base/Cheats/War/IsolatedTerritoryMovementDiagnostic.h`
3. 이 문서의 **기준 blob SHA**와 현재 SHA가 다른지 확인한다.
4. SHA가 달라도 무조건 되돌리지 않는다. 현재 코드와 이 계획을 다시 대조한 뒤, 아직 완료되지 않은 **다음 한 단계만** 수정한다.
5. 각 단계가 끝날 때 이 문서의 진행표와 실제 변경사항을 갱신한다.

### 작업 원칙

- 한 번에 전체를 리팩터링하지 않는다.
- 단계별로 작은 커밋을 만든다.
- 기존 9개 훅과 현재 이동 판정 로직은 가능한 한 그대로 둔다.
- `version.dll` 호환 때문에 `hid.dll`을 충돌 원인으로 취급하지 않는다.
- `ValidateOriginalState()`를 제거하거나 실패를 무시하는 방식으로 해결하지 않는다.
- `SAN8RPK.exe+17B0BA0`을 알 수 없는 외부 훅 위에 강제로 덮어쓰지 않는다.
- 인식 가능한 **정확한 version.dll bridge 형태**일 때만 호환 경로를 사용한다.
- 빌드/테스트는 사용자가 요청하지 않는 한 광범위하게 수행하지 않는다.

---

## 1. 분석 기준

### 원본 CT

이 문서는 2026-10-03 업로드된 다음 파일을 직접 읽어 작성했다.

- 파일명: `SAN8RPK-command-code-1.CT`
- CT Entry ID: `92011`
- 설명: `단절된 영토 간 무장 이동 불가능`
- SHA-256: `b25bffc80b379e15c2bf17a2835889a1b500522248c4df342e90287e83a9f763`
- MD5(CT 파일 자체): `070d794457ac7bebae02dde4dcb6babb`

CT 내부 기능명은 다음과 같다.

```text
고립 도시 이동 제한 (version.dll 연계)
```

### 현재 C++ 기준 blob SHA

문서 작성 시점 `main` 기준:

| 파일 | blob SHA |
|---|---|
| `Internal DX11 Base/Cheats/War/IsolatedTerritoryMovement.h` | `d6ffef80c288020a4ee02520f8f17270839255b4` |
| `Internal DX11 Base/Cheats/War/IsolatedTerritoryMovementFeature.h` | `5afd8060ea0ba1c94a8978514d57f857a5fbf41f` |
| `Internal DX11 Base/Cheats/War/IsolatedTerritoryMovementDiagnostic.h` | `e714db78ea42200d0b93942848eddc468bab1524` |
| `Internal DX11 Base/Cheats/Officer/TraitTextNameHook.cpp` | `f79a6a8e40ad023db0a65860179d3abb48fd4715` |

`TraitTextNameHook.cpp`는 이미 다른 기능에서 `version.dll 있음 / 없음` 백엔드를 나누는 선례가 있다는 참고용이다. 이동 제한 구현을 그 코드와 억지로 합치지는 않는다.

---

## 2. 현재 발생한 실제 증상

`version.dll` 없이 현재 치트만 사용할 때는 기능이 동작한다.

외부 `version.dll`을 함께 사용할 때 다음 로그가 반복된다.

```text
[영토단절] 적용 보류: CT 92011 원본 코드 상태 불일치
```

현재 `SetIsolatedTerritoryMovement(true)`는 `ValidateOriginalState()`가 실패하면 `PrepareStubs()` 및 실제 패치 전에 즉시 반환한다.

따라서 현재 증상은 **우리 훅을 설치한 뒤 망가지는 문제라기보다, 설치 전에 원본 검사에서 거부되는 문제**다.

원본 CT 분석 결과 그 이유는 `hook6`, 즉 `SAN8RPK.exe+17B0BA0`에 대해 CT는 `version.dll`이 먼저 설치한 알려진 훅을 정상 호환 상태로 인정하지만, 현재 C++은 오직 순정 바이트만 허용하기 때문이다.

---

## 3. 현재 C++의 10개 훅

현재 C++의 주소는 CT 92011의 10개 hook과 대응한다.

| Hook | RVA | 현재 처리 |
|---|---:|---|
| 1 | `0x1961B3C` | absolute jump |
| 2 | `0x196280C` | absolute jump |
| 3 | `0x1960F30` | absolute jump |
| 4 | `0x144948B` | CALL patch |
| 5 | `0x144ABE6` | CALL patch |
| 6 | `0x17B0BA0` | absolute jump — **version.dll 충돌 핵심** |
| 7 | `0x1901F77` | CALL patch |
| 8 | `0x1963D3F` | CALL patch |
| 9 | `0x1961BD5` | CALL patch |
| 10 | `0x19628D5` | CALL patch |

현재 hook6 원본 전체 14바이트:

```text
48 85 C9 0F 84 EA 12 00 00 44 88 4C 24 20
```

첫 9바이트:

```text
48 85 C9 0F 84 EA 12 00 00
```

현재 `ValidateOriginalState()`는 hook6의 14바이트가 위 값과 정확히 같아야 통과한다.

현재 적용 코드도 항상 다음 의미로 동작한다.

```text
PatchAbsoluteJump(SAN8RPK.exe+17B0BA0, gHook6Stub, 14)
```

이 방식은 `version.dll`이 해당 주소를 이미 소유한 상태와 호환되지 않는다.

---

## 4. 원본 CT가 DLL 환경을 확인하는 방식

원본 CT는 같은 게임 폴더에서 로드된 모듈을 열거하고 다음 두 DLL을 확인한다.

```text
version.dll
hid.dll
```

CT에 기록된 분석 대상 MD5:

```text
version.dll = 05861023e39184a5252dd41221c418c5
hid.dll     = 40dd894172a7ec34a76f83a43583e307
```

그리고 둘 다 존재해야 한다고 assert한다.

중요:

- 여기서 `hid.dll`은 사용자가 만든 이 프로젝트/프록시 쪽 구성요소다.
- 이번 충돌의 외부 대상은 `version.dll`이다.
- C++ 포팅에서 `hid.dll`을 외부 충돌 모드로 취급하지 않는다.
- 현재 프로젝트 바이너리는 계속 변경되므로 CT의 과거 `hid.dll` MD5를 새 C++ 코드에서 자기 자신 검증용으로 하드코딩하지 않는다.

### version.dll CT 서명

원본 CT는 MD5 이후에도 아래 메모리 서명을 검사한다.

```text
version.dll+B5C70
48 8B C4 53 55 56 57 48 83 EC 58 4C 89 60 08 4C
8B E2 4C 89 68 10 48 8B D9 4C 89 70 18 45 0F B6

version.dll+B5CBC
4C 8B 15 75 A1 09 00

version.dll+B5D20
41 FF D2

version.dll+B5F59
0F B7 05 40 BF 09 00 66 89 42 40 0F B6 05 37 BF 09
00 88 42 42 66 C7 42 43 0F 84 48 89 74 24 58 48 8D
72 40 48 63 15 21 BF 09 00 48 2B D6 48 03 15 02 BF
09 00 4A 8D 04 02 49 3B C1 0F 87 1A 01 00 00 89 56 05
```

### C++ 포팅 정책

최초 호환 구현에서는 전체 파일 MD5 계산을 새로 추가하기보다:

1. 실제 로드된 `version.dll` 모듈을 찾고,
2. 필요하면 게임 실행 파일과 같은 폴더인지 확인하고,
3. 위 관련 코드 서명과 아래 bridge 구조를 모두 검증하는 방식

을 우선한다.

이것은 구현 범위를 작게 유지하기 위한 포팅 정책이다. 단순 `version.dll` 파일 존재 여부만 보고 신뢰해서는 안 된다.

---

## 5. CT의 핵심 `bridge()` 로직

### 5.1 순정 상태

CT는 먼저:

```text
a = SAN8RPK.exe + 0x17B0BA0
```

에서 첫 9바이트가 다음과 같으면 bridge가 없다고 판단한다.

```text
48 85 C9 0F 84 EA 12 00 00
```

이 경우 기존 순정 경로를 사용한다.

### 5.2 version.dll이 이미 훅한 상태

순정 첫 9바이트가 아니면 CT는 14바이트를 읽고 정확히 다음 형태인지 검사한다.

```text
E9 <rel32> 90 90 90 90 44 88 4C 24 20
```

즉:

- offset `+0`: `E9`
- offset `+1..+4`: rel32
- offset `+5..+8`: `90 90 90 90`
- offset `+9..+13`: `44 88 4C 24 20`

이 형식이 아니면:

```text
장수 이동 후킹 형식을 인식하지 못했습니다.
```

로 거부한다.

### 5.3 E9 목적지의 소유자가 version.dll인지 검증

```text
stub = a + 5 + rel32
```

CT는 `stub`이 다음 형식이어야 한다고 요구한다.

```text
stub+0 : FF 25 00 00 00 00
stub+6 : qword == version.dll + 0xB5C70
```

또한:

```text
qword(version.dll + 0x151E98) == stub
```

이어야 한다.

즉 단순히 E9가 있다고 version 호환으로 간주하지 않고, 훅 소유자와 version.dll 내부 상태까지 확인한다.

### 5.4 version.dll의 기존 trampoline 검증

```text
trampoline = stub + 0x40
```

CT가 확인하는 내용:

```text
trampoline 첫 5바이트 == 48 85 C9 0F 84
```

그 뒤 rel32 분기의 실제 목적지:

```text
trampoline + 9 + disp32 == SAN8RPK.exe + 0x17B1E93
```

그리고 trampoline의 복귀 점프:

```text
trampoline+9  : FF 25 00 00 00 00
qword(+15)    : SAN8RPK.exe + 0x17B0BA9
```

이어야 한다.

### 5.5 bridge slot

CT가 실제로 갈아끼우는 핵심 포인터:

```text
version.dll + 0x14FE38
```

비활성 상태의 정상 값:

```text
version.dll+14FE38 -> trampoline
```

CT movement 활성 상태의 정상 값:

```text
version.dll+14FE38 -> movement cave + 1187
```

현재 C++ 포팅에서는 `movement cave + 1187`에 대응하는 진입점이 `gHook6Stub`이다.

따라서 최종 C++ 동작은 개념적으로 다음과 같아야 한다.

```text
[Vanilla]
SAN8RPK.exe+17B0BA0
    -> gHook6Stub 직접 패치

[VersionDllBridge]
SAN8RPK.exe+17B0BA0
    -> version.dll의 기존 E9를 그대로 유지
    -> version.dll wrapper
    -> version.dll+14FE38 슬롯
    -> gHook6Stub
```

**VersionDllBridge에서는 `SAN8RPK.exe+17B0BA0`을 덮어쓰면 안 된다.**

---

## 6. CT의 `normalize()`가 중요한 이유

원본 CT는 원본 검사 시 현재 메모리와 기대값을 단순 비교하지 않는다.

`normalize()`는 이미 활성화된 자기 패치를 원래 바이트로 논리 복원한 뒤 검증하고, 특히 검사 범위가 `0x17B0BA0`을 포함할 때 `bridge()`가 정상 version.dll bridge를 인식하면 해당 부분을 순정 9바이트로 **논리적으로 치환**한다.

version.dll 훅의 14바이트 끝 5바이트는 순정과 동일한:

```text
44 88 4C 24 20
```

이므로, 첫 9바이트만 순정으로 normalize하면 전체 14바이트 guard가 순정 기대값과 같아진다.

현재 C++의 문제는 이 논리 정규화가 없고:

```text
ReadEq(gBase + kHook6, kHook6Original, 14)
```

만 수행한다는 것이다.

### 구현 시 주의

`ValidateOriginalState()`에서 그냥 hook6 검사를 삭제하면 안 된다.

허용 상태는 오직 두 가지다.

```text
1. VanillaExact
   14바이트가 완전한 순정 값

2. VersionDllBridgeExact
   CT bridge()와 동일한 구조 검증을 모두 통과한 알려진 version.dll bridge
```

그 외는 `Unknown/Conflict`로 실패해야 한다.

---

## 7. Hook6의 return-address +0x80 보정

이 부분을 빼면 bridge 연결만 성공해도 이동 판정 컨텍스트가 잘못될 수 있다.

현재 `BuildHook6Stub()`는 volatile register를 저장한 뒤 다음 명령으로 호출자 return address를 읽는다.

```text
48 8B 84 24 C8 00 00 00
mov rax,[rsp+0xC8]
```

그 값을 다음 게임 return address들과 비교한다.

```text
SAN8RPK.exe + 0x144A326
SAN8RPK.exe + 0x144A8FA
SAN8RPK.exe + 0x1450972
```

원본 CT는 movement cave를 만들 때 위 `mov rax,[rsp+C8]` 패턴을 찾아 helper CALL로 교체한다.

CT 주석:

```text
The version.dll wrapper moves the original caller's return address by 0x80.
```

CT helper 의미:

```text
helper 내부에서 [rsp+D0] 읽기
    ↓
그 값이 version.dll+B5D23인지 비교
    ↓ 아니면 그대로 반환
    ↓ 맞으면
[rsp+150]을 읽어서 반환
```

helper는 `CALL`로 들어가기 때문에 helper 내부 stack offset은 원래 stub보다 8바이트 커진다.

따라서 **현재 C++ `BuildHook6Stub()` 안에서 helper CALL 없이 직접 같은 의미를 구현한다면** 대응 offset은 다음과 같다.

```text
기본 return address : [rsp+C8]
version wrapper 감지 후 실제 원래 caller : [rsp+148]
```

`0x148 - 0xC8 = 0x80`이다.

반대로 CT처럼 별도 helper를 `CALL`해서 구현한다면 helper 내부에서는 CT와 동일하게:

```text
[rsp+D0]
[rsp+150]
```

을 사용해야 한다.

### version wrapper 표식

비교 기준은:

```text
version.dll + 0xB5D23
```

이다.

CT 서명상 `version.dll+B5D20`에는:

```text
41 FF D2     ; call r10
```

가 있고, `B5D23`은 그 호출 직후 return address이므로 wrapper 경유 여부를 판별하는 표식으로 쓰인다.

### 절대 주의

`BuildHook6Stub()`에 helper CALL을 추가하지 않으면서 `D0/150`을 그대로 사용하면 stack offset이 8바이트 틀어진다.

직접 구현이면 `C8/148`, helper CALL 방식이면 `D0/150`이다.

---

## 8. 현재 C++의 관련 구조와 유지할 부분

### `IsolatedTerritoryMovement.h`

현재 이미 다음이 구현되어 있다.

- 10개 hook 주소 및 원본 바이트
- `gHook1Stub` ~ `gHook6Stub`
- `gHook78Thunk`, `gHook910Thunk`
- `MovementContextAllowed()`
- `MovementCheck78()` / `MovementCheck910()`
- 일반 `RecordAndWrite()`
- `PatchAbsoluteJump()` / `PatchCall()`
- `RestoreAllPatches()`
- 적용/해제 상태 `gApplied`

이번 작업은 이 구조를 최대한 유지한다.

### `IsolatedTerritoryMovementFeature.h`

`PrepareBoolAbiFix()`가 `PrepareStubs()`를 먼저 호출하고 생성된 5개 stub에서 `test eax,eax`를 `test al,al`로 교정한다.

hook6 stub도 여기서 적용 전에 미리 생성될 수 있다는 점에 유의한다.

version.dll 관련 정보가 `BuildHook6Stub()` 생성 시 필요하다면 `gBase`뿐 아니라 version module 상태가 먼저 해석되어야 한다. 또는 stub 자체가 런타임 상수로 version 주소를 포함하도록 생성 순서를 조정해야 한다.

기존 bool ABI 수정은 이번 작업 범위에서 깨뜨리지 않는다.

### `IsolatedTerritoryMovementDiagnostic.h`

현재 읽기 전용 진단은 10개 hook을 모두 순정 바이트와만 비교한다.

따라서 version.dll 환경에서 hook6 FAIL은 현재로서는 정상적으로 예상된다.

진단 파일을 당장 대규모 수정할 필요는 없지만 최종 단계에서 hook6을:

```text
VANILLA / VERSION_BRIDGE / UNKNOWN
```

으로 표시하도록 개선할 수 있다.

---

## 9. 안전한 단계별 구현 계획

중간 커밋이 위험한 상태가 되지 않도록, **version bridge를 실제 활성화하는 것은 필요한 구성요소가 준비된 뒤 한 번에 한다.**

### Stage 0 — 분석 문서화

상태: **완료**

작업:

- 원본 CT 92011의 DLL 검사, `bridge()`, `normalize()`, movement cave return-address 보정을 문서화
- 현재 C++과 차이 기록
- 다음 단계와 금지사항 기록

소스 코드 변경: 없음

---

### Stage 1 — version.dll / Hook6 환경 Resolver 추가 (읽기 전용)

상태: 미완료

목표:

실제 적용 방식은 전혀 바꾸지 않고, 현재 hook6 상태를 안전하게 분류하는 코드만 추가한다.

권장 개념 구조:

```text
Hook6Environment
- Vanilla
- VersionDllBridge
- Unknown
```

필요 정보 구조 예:

```text
VersionBridgeInfo
- versionBase
- hookStub
- trampoline
- slotAddress
- slotValue
```

Resolver가 확인할 것:

1. `SAN8RPK.exe+17B0BA0` 14바이트 순정 여부
2. 순정이 아니면 로드된 `version.dll` 확보
3. 가능하면 게임 exe와 같은 폴더에서 로드된 모듈인지 확인
4. 필요한 version.dll 코드 서명 확인
5. `E9 rel32 + NOP4 + 44 88 4C 24 20` 형식 확인
6. E9 목적지 stub 확인
7. `stub -> version+B5C70` 확인
8. `qword(version+151E98) == stub`
9. `stub+40` trampoline 검증
10. trampoline 분기 목적지 `game+17B1E93` 확인
11. trampoline 복귀 주소 `game+17B0BA9` 확인
12. `version+14FE38` slot이 현재 trampoline인지 확인

Stage 1에서는 `ValidateOriginalState()`의 통과 조건을 바꾸지 않는다.

즉 version.dll 환경에서는 여전히 기능 적용이 보류되어야 한다. 대신 로그로 정확히:

```text
Hook6: Vanilla
Hook6: VersionDllBridge
Hook6: Unknown
```

정도를 확인할 수 있게 한다.

완료 조건:

- version.dll 없는 실행에서 `Vanilla` 판별
- 분석 대상 version.dll 실행에서 `VersionDllBridge` 판별
- 임의/알 수 없는 변형은 `Unknown`으로 거부
- 메모리 쓰기 없음

---

### Stage 2 — Hook6 return-address version wrapper 보정 준비

상태: 미완료

목표:

`BuildHook6Stub()`가 순정 호출과 version.dll wrapper 호출을 모두 올바르게 해석하도록 한다.

중요: 이 단계에서도 아직 version bridge를 `ValidateOriginalState()`에서 허용하지 않는다. 따라서 사용자 기능 동작은 기존과 같아야 한다.

구현 방향은 둘 중 하나를 선택한다.

#### 권장: 현재 C++ builder 안에서 직접 분기

개념:

```text
rax = [rsp+C8]
if (version bridge 사용 가능한 상태 && rax == version+B5D23)
    rax = [rsp+148]
```

그 뒤 기존 세 게임 return address 비교 로직을 그대로 사용한다.

장점:

- CT helper blob을 별도로 만들 필요 없음
- 현재 `CodeBuilder` 구조에 자연스럽게 들어감
- stack offset 의미가 명확함

단, `version.dll`이 없을 때는 기존 `mov rax,[rsp+C8]`와 의미가 완전히 같아야 한다.

완료 조건:

- Vanilla 코드 경로 의미 변화 없음
- version wrapper marker 처리 코드가 존재
- 아직 bridge 실제 적용은 하지 않음

---

### Stage 3 — version bridge slot 패치/복원 기반 준비

상태: 미완료

목표:

`version.dll+14FE38` qword slot을 안전하게 기록/변경/복원할 준비를 한다. 아직 hook6 validation을 version bridge에서 통과시키지 않아 실제 활성화는 하지 않는다.

필수 안전 조건:

적용 직전:

```text
slot == verified trampoline
```

이어야 한다.

해제 직전:

```text
slot == gHook6Stub
```

인지 확인한 뒤 원래 trampoline으로 복원하는 방식을 권장한다.

다른 프로그램이 활성 중 slot을 바꿨다면 무조건 덮어쓰지 말고 실패 로그를 남긴다.

현재 `RestoreAllPatches()`는 저장된 before 바이트를 무조건 다시 쓰므로, 이 bridge slot에 대해서는 최소한 별도 현재값 검증을 두는 것이 원본 CT의 안전 철학과 맞는다.

전체 patch 시스템을 대규모 리팩터링하지 않는다.

완료 조건:

- slot 8바이트 patch/restore 코드 준비
- 현재값 검증 준비
- 아직 실제 version bridge 적용 활성화 안 함

---

### Stage 4 — validation + Hook6 적용 경로를 원자적으로 연결

상태: 미완료

이 단계에서 처음으로 version.dll 환경에서 기능을 실제 활성화한다.

반드시 **validation 허용과 적용 분기를 같은 커밋에서 함께 연결**한다.

이유:

validation만 먼저 완화하면 현재 코드가 곧바로:

```text
PatchAbsoluteJump(game+17B0BA0, ...)
```

를 실행해 version.dll의 기존 훅을 파괴할 수 있기 때문이다.

최종 분기:

```text
Hook6 == Vanilla
    -> 기존 PatchAbsoluteJump(game+17B0BA0, gHook6Stub, 14)

Hook6 == VersionDllBridge
    -> game+17B0BA0은 절대 쓰지 않음
    -> version.dll+14FE38 qword를 trampoline -> gHook6Stub으로 변경

Hook6 == Unknown
    -> 적용 거부
```

`ValidateOriginalState()`의 hook6 조건 역시 같은 resolver 결과를 사용한다.

나머지 9개 hook은 기존 검증/패치 방식을 유지한다.

완료 조건:

- 순정 환경 기존 동작 유지
- version.dll 환경에서 `17B0BA0` 바이트는 version.dll 훅 그대로 유지
- slot만 `gHook6Stub`으로 연결됨
- 알 수 없는 hook6 형태는 계속 실패

---

### Stage 5 — 해제/부분 실패/진단 강화

상태: 미완료

확인할 것:

- Vanilla 해제: 기존처럼 게임 원본 복원
- Version bridge 해제: slot을 기존 trampoline으로 복원
- 해제 전 slot 현재값 검증
- 적용 중 후속 hook patch 실패 시 slot까지 rollback
- `gApplied`, patch 기록 상태가 실패 후 일관된지 확인
- 필요 시 Diagnostic에서 hook6 상태를 `VANILLA / VERSION_BRIDGE / UNKNOWN`으로 출력

기능과 무관한 리팩터링은 하지 않는다.

---

### Stage 6 — 실게임 검증 및 문서 종료

상태: 미완료

최소 테스트 매트릭스:

| 환경 | 기대 결과 |
|---|---|
| 현재 `hid.dll` / external `version.dll` 없음 | 기존과 동일하게 적용/해제 성공 |
| 현재 `hid.dll` + 분석 대상 `version.dll` | bridge 방식으로 적용/해제 성공 |
| 알 수 없는 프로그램이 `17B0BA0` 변경 | 안전하게 적용 거부 |
| ON -> OFF -> ON 반복 | 원복 및 재적용 정상 |
| version bridge 활성 중 slot 외부 변경 | 해제 시 강제 덮어쓰기보다 충돌 감지 |

추가로 version.dll 환경에서 확인할 핵심 메모리:

```text
SAN8RPK.exe+17B0BA0
- ON/OFF 동안 version.dll의 E9 hook 형태가 보존되어야 함

version.dll+14FE38
- OFF: verified trampoline
- ON : gHook6Stub
- OFF: verified trampoline
```

최종 성공 후 이 문서에 실제 구현 커밋 SHA와 실게임 검증 결과를 추가한다.

---

## 10. 절대 하지 말아야 할 수정

### 금지 1: ValidateOriginalState 제거

```text
원본 불일치여도 그냥 적용
```

방식 금지.

이 검증은 충돌 방지 장치다. 없애는 것이 아니라 **CT가 정상으로 인정하던 version bridge 상태를 정확하게 추가**해야 한다.

### 금지 2: version.dll이 있으면 무조건 hook6 허용

파일/모듈 이름만 같다고 신뢰하지 않는다.

bridge shape, stub owner, trampoline, slot을 검증한다.

### 금지 3: version bridge 위에 absolute jump 강제 덮어쓰기

```text
PatchAbsoluteJump(game+17B0BA0, ...)
```

는 Vanilla에서만 한다.

### 금지 4: `hid.dll`을 외부 충돌 DLL로 취급

`hid.dll`은 사용자 프로젝트 쪽이다. 이번 bridge 호환 대상은 external `version.dll`이다.

### 금지 5: +0x80 보정을 빼고 slot만 연결

version wrapper는 원래 caller return address 위치를 옮긴다. caller 판정이 틀어질 수 있으므로 return-address 보정을 bridge 활성화 전에 준비해야 한다.

### 금지 6: CT helper의 stack offset을 문맥 없이 그대로 복사

- 현재 Hook6 stub에서 직접 처리: `C8 / 148`
- 별도 helper를 CALL해서 처리: `D0 / 150`

이 차이를 지킨다.

---

## 11. 구현 시 예상되는 최소 수정 범위

주 수정 파일:

```text
Internal DX11 Base/Cheats/War/IsolatedTerritoryMovement.h
```

가능한 보조 수정:

```text
Internal DX11 Base/Cheats/War/IsolatedTerritoryMovementDiagnostic.h
```

`IsolatedTerritoryMovementFeature.h`는 `PrepareStubs()` 호출 순서 때문에 version 상태 준비 시점 조정이 필요할 경우에만 최소 수정한다.

현재 예상으로는 다른 War 기능, UI, Config, trait 관련 코드를 수정할 이유가 없다.

---

## 12. 다음 채팅용 핵심 요약

새 채팅에서 이 부분만 읽어도 방향을 잃지 않아야 한다.

### 문제

현재 C++은 hook6 `SAN8RPK.exe+17B0BA0`이 순정 14바이트가 아니면 `ValidateOriginalState()`에서 실패한다.

external `version.dll`은 이 주소에 이미 자기 E9 hook을 설치하므로 현재 로그:

```text
[영토단절] 적용 보류: CT 92011 원본 코드 상태 불일치
```

가 발생한다.

### 원본 CT의 해법

CT 92011은 해당 E9가 **알려진 version.dll bridge인지 엄격하게 검증**한다.

정상 version bridge라면:

- game `+17B0BA0`을 건드리지 않는다.
- `version.dll+14FE38` 함수 포인터 slot을 기존 trampoline에서 movement cave/hook6 stub으로 바꾼다.
- validation 시 그 bridge를 논리적으로 순정 hook6로 normalize한다.
- version wrapper return address `version.dll+B5D23`을 감지하고 원래 caller 위치 `+0x80`을 보정한다.

### 다음 작업

**Stage 1만 수행한다.**

즉 `version.dll` / Hook6 환경을 `Vanilla / VersionDllBridge / Unknown`으로 안전하게 분류하는 **읽기 전용 resolver**를 최소 변경으로 추가한다.

아직 `ValidateOriginalState()`를 완화하거나 실제 slot을 패치하지 않는다.

Stage 1 완료 후:

1. 변경 내용을 이 문서에 기록
2. 진행표 체크
3. 해당 단계만 별도 커밋
4. 사용자에게 실게임 로그 확인 포인트를 전달

---

## 13. 진행표

- [x] Stage 0 — 원본 CT / 현재 C++ 차이 문서화
- [ ] Stage 1 — version.dll / Hook6 읽기 전용 resolver
- [ ] Stage 2 — Hook6 return-address +0x80 보정 준비
- [ ] Stage 3 — bridge slot patch/restore 기반 준비
- [ ] Stage 4 — validation + 실제 bridge 적용 연결
- [ ] Stage 5 — rollback / restore / diagnostic 강화
- [ ] Stage 6 — 실게임 검증 및 최종 문서화

---

## 14. Stage별 변경 기록

### Stage 0

- 날짜: 2026-10-03
- 상태: 완료
- 변경: 이 계획 문서 추가
- C++ 소스 변경: 없음
- 근거: 업로드된 원본 CT `92011` 직접 분석 + 현재 `main` 소스 대조

향후 각 단계 완료 시 아래에 커밋 SHA, 변경 파일, 테스트 결과를 누적한다.
