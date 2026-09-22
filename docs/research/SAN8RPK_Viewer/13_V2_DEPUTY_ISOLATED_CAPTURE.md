# Step 13 - V2.0 부장 포획 / 고립도시 포획 상세 해부

## 0. 범위

분석 대상:

```text
SAN8RPK_AI_V2.0.exe
SHA-256:
763e8657877aee222d0d1c90b20382f4b204431e1ffb400d7f968a29dc44d268
```

대상 모듈:

```text
deputy_profile
deputy_patch
isolated_profile
isolated_patch
```

이번 단계는 정적 분석입니다.

목표:

1. 부장 포획 hook / 원본 / payload 확인
2. 고립도시 포획 hook / 원본 / payload 확인
3. 장수 +0x310 필드 사용 방식 확인
4. 현재 S8RPKCheats 포로 관련 hook과 byte overlap 확인
5. 이식 순서 결정

---

# 1. 부장 포획 - profile 기본값

`deputy_profile` 내부 설명:

```text
Companion capture only after native commander capture succeeds.
```

### Hook RVA

```text
SAN8RPK.exe + 0x01E33056
```

### 원본 14 bytes

```text
4D 89 A6 10 03 00 00
B0 01
E9 A2 FE FF FF
```

명령어:

```asm
mov [r14+310h], r12
mov al,1
jmp SAN8RPK.exe+1E32F06
```

즉 hook 지점에 들어온 시점에는 게임 원래 로직이 이미
`[r14+0x310] = r12` 형태의 포획 결과를 기록합니다.

Viewer payload는 이 원본 store를 먼저 그대로 수행한 뒤
부장 보정 함수를 실행하고 원래 복귀점으로 이동합니다.

### 복귀 RVA

```text
SAN8RPK.exe + 0x01E32F06
```

### Guard

```text
+0x01E33025 / 63 bytes
SHA-256 98510c1cfa1650afa9371cce0001e4952498f48367b05c75a0a2395a26c4e576

+0x01E414D0 / 148 bytes
SHA-256 7bb7665e4820da4213a65d20feb4cbcbd3e18beb0e3a886464f5c45d9b72a8c2
```

---

# 2. 부장 포획 payload 구조 - 확정

payload template:

```text
528 bytes
config qword offset = 0x1F8
```

최종 fixup:

```text
cave+0x1F8 = SAN8RPK.exe base
cave+0x200 = stats pointer
cave+0x208 = SAN8RPK.exe + 0x01E32F06
```

entry는 먼저 원래 코드를 재현합니다.

```asm
mov [r14+310h],r12
```

그 뒤:

```text
commander = r14
captureOwner = r12
```

형태로 내부 helper를 호출합니다.

---

# 3. 장수 포인터 검증 helper - 확정

payload 내부 helper는 장수 포인터를 다음 조건으로 검증합니다.

### 장수 vtable

```text
SAN8RPK.exe base + 0x02688548
```

### 상태 객체 vtable

```text
SAN8RPK.exe base + 0x02687D28
```

### 상태 ID

```text
status object +0x08
1~7 범위
```

따라서 실제 게임의 활성 장수 객체인지 확인한 뒤에만
부장 보정을 수행합니다.

---

# 4. 부장 포획 조건 - 확정

payload에서 확인되는 조건:

```text
commander != captureOwner
commander +0x310 == captureOwner
commander force != null
captureOwner force != null
commander force != captureOwner force
두 force 객체 모두 정상 vtable
```

장수 구조:

```text
+0x18 = force pointer
+0x310 = 포획 결과와 연결되는 장수 pointer
```

hook 진입 직전에 게임 원래 코드가:

```text
commander+0x310 = captureOwner
```

를 수행하므로 Viewer는
**총대장 네이티브 포획 성공을 +0x310 결과로 확인한 뒤에만**
부장을 처리합니다.

---

# 5. 부장 최대 2명 구조 - 확정

payload는 manager의 전투 편성 record를 찾은 뒤:

```text
record +0x08 = commander
record +0x10 = deputy #1
record +0x18 = deputy #2
```

형태로 두 qword를 순회합니다.

즉 게시글의:

```text
총대장 1명 + 부장 최대 2명
```

구조가 payload에서 직접 확인됩니다.

부장마다:

```text
- commander 자신이 아님
- captureOwner 자신이 아님
- 정상 장수 객체
- commander와 같은 force
- deputy+0x310 == 0
```

을 만족하면:

```asm
mov [deputy+310h], captureOwner
```

를 수행합니다.

### 결론

부장용 별도 포획 확률을 새로 굴리는 것이 아닙니다.

> **총대장이 이미 포획된 경우 그 부대의 부장 최대 2명에게 동일한 포획 결과를 복제**

합니다.

따라서 바닐라 포획 결과를 보완하는 fix 성격이 강합니다.

---

# 6. 부장 포획 stats

payload는 내부 stats에서 최소 두 값을 증가시킵니다.

```text
stats+0x00 : 조건에 맞는 총대장 편성 record 처리 횟수
stats+0x08 : 실제 +0x310을 기록한 부장 수
```

정확한 UI 명칭은 별도 patch controller 분석 없이
여기서는 내부 진단 카운터로만 기록합니다.

---

# 7. 고립도시 포획 - profile 기본값

`isolated_profile` 내부 설명:

```text
Isolated city defeat: include resident defenders in the native prisoner list.
```

### Hook RVA

```text
SAN8RPK.exe + 0x01E57250
```

### 원본 17 bytes

```text
48 89 4C 24 08
55
53
56
57
41 54
41 55
41 56
41 57
```

게임 함수의 prologue입니다.

Viewer는 함수 시작을 cave로 보내고,
payload 마지막에서 원본 prologue를 실행하는 trampoline으로 이동한 뒤
원래 함수 `+17` 위치로 복귀합니다.

### trampoline

```text
cave + 0x600
```

---

# 8. 고립도시 payload 기본 구조

template:

```text
1264 bytes
```

payload 뒤를 `0x600`까지 INT3로 padding하고
그 위치에 원본 17 bytes + absolute return jump를 붙입니다.

주요 data slot:

```text
cave+0x4D0 = SAN8RPK.exe base
cave+0x4D8 = native prisoner-list append helper
cave+0x4E0 = trampoline
cave+0x4E8 = stats
```

native append helper는 profile의 guard/fixup 관계상:

```text
SAN8RPK.exe + 0x0001A0C0
```

가 **유력**합니다.

이 한 항목은 대상 게임 원본 코드 또는 런타임 진단으로 한 번 더 확정합니다.

---

# 9. 고립도시 판정 - 확정

payload는 manager에서 패배 도시와 양측 force 문맥을 얻은 뒤
도시 인접 슬롯을 순회합니다.

도시 객체에서 확인되는 연결 포인터 범위:

```text
city +0x20
city +0x28
city +0x30
city +0x38
city +0x40
city +0x48
```

각 인접 도시가 존재하면 해당 도시 owner force를 확인합니다.

### 핵심 조건

인접 도시 중 하나라도:

```text
owner force == 패배 세력
```

이면 payload는 즉시 종료합니다.

즉 V2.0의 “고립 도시”는:

> **패배 도시와 직접 인접한 도시들 중 패배 세력 소유 도시가 하나도 없는 상태**

로 구현되어 있습니다.

단순히 지도상 멀리 떨어졌는지,
다른 영토가 존재하는지로 판단하지 않습니다.

---

# 10. 고립도시 대상 장수 - 확정

고립이 확정되면 payload는 전체 장수 pointer 영역을 순회합니다.

후보 장수 조건:

```text
- 정상 장수 vtable
- force == 패배 세력
- city == 함락 도시
- 활성 상태
```

즉 해당 도시의 **잔류 수비측 장수**만 대상으로 합니다.

---

# 11. 기존 native prisoner list 재사용 - 확정

payload에는 “이 장수가 현재 native 결과 list에 있는가”를 확인하는
내부 helper가 존재합니다.

후보 장수가 아직 native list에 없다면:

1. 게임 native list append 함수를 호출
2. 다시 membership을 확인
3. 성공하면 장수의 포획 결과 필드를 설정

하는 순서입니다.

따라서 Viewer가 별도의 포로 컨테이너를 만드는 것이 아니라:

> **게임이 이미 사용 중인 전투 결과 포로 리스트에 고립도시 잔류 장수를 추가**

합니다.

---

# 12. 고립도시에서도 +0x310 사용 - 확정

native prisoner list 추가가 성공한 장수에게:

```text
officer +0x310 = captureOwner
```

를 기록합니다.

따라서 부장 포획과 고립도시 포획은 서로 다른 hook이지만
최종 장수 포획 결과에는 동일한 `+0x310` 필드를 사용합니다.

이 점 때문에 두 기능을 하나의:

```text
포로 처리 개선
```

모듈로 관리하는 것이 자연스럽습니다.

---

# 13. 현재 S8RPKCheats 포로 hook과 충돌 비교

현재 `GovernorPrisonerDisposal.cpp`:

```text
main       +0x01E53BB2
collect    +0x01E5CE39
relations  +0x01E5DE47
```

V2.0 신규:

```text
부장 포획     +0x01E33056
고립도시 포획 +0x01E57250
```

### byte overlap

**없음**

직접 hook 주소가 서로 떨어져 있습니다.

현재 도독 포로 직접 처분 기능은:

- 플레이어 도독의 처분 권한/UI 흐름 확장

V2.0 신규 두 기능은:

- 전투 결과에서 포획 대상 누락 보완

이므로 목적도 다릅니다.

### 현재 판정

**병행 가능성이 높음**

실게임에서는 둘을 같이 켠 회귀 테스트를 최종적으로 수행합니다.

---

# 14. 기본 적용 여부

사용자 결정:

> 부장 포획 / 고립도시 포획은 별도 체크박스 없이 기본 적용.

정적 분석 결과도 이에 적합합니다.

이유:

### 부장 포획

원래 총대장 포획이 성공했을 때만
같은 편성의 부장을 같은 결과로 보정합니다.

### 고립도시 포획

패배 세력의 인접 소유 도시가 존재하면 아무것도 하지 않고,
실제로 고립된 경우에만 해당 도시 잔류 장수를 native 포로 리스트에 추가합니다.

둘 다 임의의 치트 확률을 추가하는 형태보다
기존 전투 결과의 누락을 보완하는 성격이 강합니다.

---

# 15. 이식 순서

한 번에 두 hook을 넣지 않습니다.

## Step 1

```text
부장 포획
```

이유:

- hook 1개
- 원본 14 bytes
- payload 528 bytes
- 별도 native 함수 호출 없음
- 최대 부장 2명 구조 명확
- 실패 시 영향 범위가 작음

먼저 별도 브랜치에서 기본 적용하고 실게임 확인합니다.

## Step 2

```text
고립도시 함락 포획
```

부장 포획 검증 후 추가합니다.

이유:

- 더 큰 payload
- 도시 인접 판정
- 전체 장수 순회
- native prisoner list membership / append
- trampoline

이 함께 들어가므로 회귀 범위가 더 큽니다.

---

# 16. 부장 포획 실게임 검증 기준

테스트:

```text
적 부대:
총대장 A
부장 B
부장 C
```

총대장 A가 실제 바닐라 포획 판정에 성공하는 전투를 만듭니다.

확인:

```text
A 포획
B 포획
C 포획
```

그리고 다음 반대 조건도 확인합니다.

```text
A가 포획되지 않음
-> B/C도 강제 포획되면 안 됨
```

추가:

- 부장 0명
- 부장 1명
- 부장 2명
- 서로 다른 세력 포인터 이상치 없음
- 도독 포로 직접 처분 ON/OFF

---

# 17. 고립도시 실게임 검증 기준

### 고립 도시

패배 도시와 인접한 패배 세력 도시:

```text
0개
```

기대:

```text
함락 도시 잔류 장수 -> 포로 list 포함
```

### 비고립 도시

패배 세력 소유 인접 도시:

```text
1개 이상
```

기대:

```text
Viewer 보정 미동작
기존 게임 퇴각/포로 판정 유지
```

추가:

- 명마 보유 장수
- 군주/태수/일반 장수
- 출진 중 장수와 도시 잔류 장수 구분
- 이미 native 포로 list에 들어간 장수 중복 추가 없음
- 도독 포로 직접 처분과 병행

---

# 18. 결론

### 부장 포획

정적 구조 **확정**.

```text
native commander capture success
  ↓
commander+0x310 already set
  ↓
같은 부대 deputy #1/#2 확인
  ↓
deputy+0x310 = same captureOwner
```

첫 구현 대상으로 적합합니다.

### 고립도시 포획

핵심 조건/흐름 **확정**.

```text
도시 함락
  ↓
패배 세력 소유 인접 도시 존재?
  ├─ YES -> 아무 보정 없음
  └─ NO  -> 함락 도시 잔류 패배 세력 장수 검색
             ↓
           native prisoner list에 없는 장수 append
             ↓
           +0x310 포획 결과 설정
```

native append helper RVA 하나는 **유력** 상태이므로
실제 이식 전 원본/런타임에서 최종 확인합니다.
