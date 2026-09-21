# Step 03 - AI 공격 후보 확장 / 목표세력 제한 해제 상세 해부

## 0. 이번 Step의 범위

이 문서는 `SAN8RPK_Viewer.exe`의 최종 AI payload에서 **공격 후보 선택 부분만** 분리해 분석한 기록입니다.

이번 단계에서 확인하는 것:

- 공격 hook의 정확한 RVA와 원본 바이트
- 최종 active attack payload가 어느 템플릿인지
- 후보가 기존 목표 조건 밖이어도 점수 계산까지 들어가는지
- 기존 목표에 대한 우선 보정이 남아 있는지
- S8RPKCheats의 현재 `+144D24C` 패치와 정확히 어떻게 다른지

이번 단계에서 의도적으로 미루는 것:

- 플레이어 우선 가산 제거의 정확한 별도 조건 → Step 04
- 항복권고 실패 후 공격점수 boost 블록 → Step 08
- `relation`, `score` native helper 함수의 내부 구현 → 대상 게임 EXE 원본 디스어셈블리 또는 런타임 진단 필요

---

## 1. 분석 소스 - 확정

### EXE

- SHA-256: `34abf52f9651d4313076b16042476e5646967aa55af65613f542d69883084c3d`
- 내부 모듈: `ai_profile.py`, `ai_patch.py`
- Viewer 내부 버전 문자열: `v0.26`

### 최종 attack template

`ai_profile`에는 초기에 `TEMPLATE`가 존재하지만, 이후 버전 확장에서 최종 `build_payload()`는 공격 부분에 **`REFUSAL_ATTACK`**을 사용합니다.

추출한 `REFUSAL_ATTACK` raw template:

- 크기: 399 bytes
- SHA-256: `c08f5c998d2cb20f399402a59ee0394e7320c249ca3f13c42e6d0df1f32b3b57`

초기 `TEMPLATE`는 222 bytes이며, 최종 템플릿은 여기에 항복권고 실패 후 공격 우선도 관련 블록이 추가된 형태입니다.

---

## 2. 최종 cave 메모리 배치 - 확정

`AIController.enable()`에서:

```text
VirtualAllocEx(..., 0x3000, ...)
build_payload(gameBase, cave + 0x1000)
```

즉:

- cave 전체 예약: `0x3000`
- code 영역: cave `+0x0000 ~ +0x0FFF`
- stats 영역 시작: cave `+0x1000`

최종 payload 구성은 Python bytecode상 다음 순서입니다.

```text
cave+0x000  attack payload (REFUSAL_ATTACK)
cave+0x200  prisoner payload
...
cave+0x400  refusal pre payload
cave+0x600  refusal post payload
...
cave+0x800  hatred classifier payload
...
cave+0xA00  surrender payload
```

공격 hook은 cave `+0x000`으로 들어옵니다.

---

## 3. 공격 hook 지점 - 확정

### Hook RVA

```text
SAN8RPK.exe + 0x0144D248
```

### Viewer가 검증하는 원본 16 bytes

```text
49 3B 4D 30
74 0A
48 8B 47 18
49 3B 45 38
75 22
```

명령어로 보면:

```asm
cmp rcx, [r13+30h]
je  +0Ah
mov rax, [rdi+18h]
cmp rax, [r13+38h]
jne +22h
```

Viewer는 이 16 bytes를 absolute indirect jump로 교체하여 cave `+0`으로 보냅니다.

`make_jump()` 형식:

```text
FF 25 00 00 00 00
<8-byte cave address>
90 90
```

총 16 bytes입니다.

### Cave 복귀점

attack payload의 `resume` fixup:

```text
SAN8RPK.exe + 0x0144D27A
```

따라서 Viewer는 단순히 16 bytes만 대신 실행하고 `+144D258`로 돌아오는 구조가 아닙니다.

**`+144D248`에서 진입한 뒤 원래 `+144D27A`까지의 판단/점수 구간을 cave에서 자체 수행하고, 그 뒤로 복귀합니다.**

이 차이가 현재 S8RPKCheats 구현과 매우 중요합니다.

---

## 4. 공격 payload fixup - 확정

초기 프로필의 이름과 최종 `refusal_code()` 치환값을 대조하면 공격 구간에서 사용되는 핵심 주소는 다음과 같습니다.

| marker | profile 이름 | 실제 값 |
|---|---|---:|
| `0x1111111111111111` | stats | `cave + 0x1000` |
| `0x2222222222222222` | relation | `SAN8RPK.exe + 0x017AF090` |
| `0x3333333333333333` | score | `SAN8RPK.exe + 0x017A5130` |
| `0x4444444444444444` | resume | `SAN8RPK.exe + 0x0144D27A` |
| `0x5555555555555555` | 후속 refusal/global 용 | `SAN8RPK.exe + 0x02E98BC8` |

`relation`, `score`라는 이름은 추측이 아니라 **`ai_profile.FIXUPS`에 실제로 들어 있는 이름**입니다.

다만 두 native 함수의 내부 의미는 아직 대상 게임 EXE를 직접 디스어셈블하지 않았으므로 여기서는 이름 이상의 의미를 확정하지 않습니다.

---

## 5. stats 11개 필드 역매핑 - 확정

`AIController.stats()`는 cave `+0x1000`에서 `<11Q`를 읽습니다.

Viewer의 `show_ai_status()` Python bytecode가 이 11개 값을 다음 변수명으로 unpack합니다.

| index | stats offset | Viewer 변수명 | UI 노출 |
|---:|---:|---|---|
| 0 | `+0x00` | `examined` | 직접 표시 안 함 |
| 1 | `+0x08` | `outside` | **추가 후보** |
| 2 | `+0x10` | `selected` | **갱신** |
| 3 | `+0x18` | `friendly` | 직접 표시 안 함 |
| 4 | `+0x20` | `captive` | 직접 표시 안 함 |
| 5 | `+0x28` | `released` | 석방 |
| 6 | `+0x30` | `executed` | 처형 |
| 7 | `+0x38` | `refused` | 권고 실패 |
| 8 | `+0x40` | `switched` | 전환 |
| 9 | `+0x48` | `skipped` | 재권고 차단 |
| 10 | `+0x50` | `boosted` | 현재 상태 문자열에는 직접 표시 안 함 |

Viewer의 실제 상태 문자열:

```text
공격·포로 AI 켜짐 (플레이어 우선 가산 제거)
· 추가 후보 %d / 갱신 %d
· 석방 %d / 처형 %d
· 권고 실패 %d / 전환 %d / 재권고 차단 %d
```

따라서 cave의 `stats+0x08` 증가가 **'추가 후보'**, `stats+0x10` 증가가 **'갱신'**임을 직접 연결할 수 있습니다.

---

## 6. 공격 payload 제어 흐름 - 확정

아래는 최종 `REFUSAL_ATTACK` 중 Step 08용 refusal boost 구간을 제외한 핵심 흐름입니다.

### 6.1 후보 검사 시작

```asm
lock inc qword ptr [stats+00h] ; examined++

test rcx, rcx
je resume

mov rdx, [r13+10h]
test rdx, rdx
je resume
```

즉 후보 검사 진입 횟수는 `examined`으로 누적됩니다.

### 6.2 동일/관계 필터

```asm
cmp rcx, rdx
je friendly_skip

call relation(rcx, rdx)
test al, al
jne friendly_skip
```

skip 시:

```asm
lock inc qword ptr [stats+18h] ; friendly++
jmp resume
```

여기서 `friendly`라는 명칭은 Viewer Python 코드의 실제 변수명입니다.

정확히 어떤 관계 범주를 `relation()`이 true로 반환하는지는 아직 helper 내부 미분석이므로 동맹/우호 등으로 임의 단정하지 않습니다.

---

## 7. '목표 밖 후보'를 버리지 않는 핵심 - 확정

관계 필터를 통과한 뒤 Viewer는 후보의 두 컨텍스트를 기존 목표 컨텍스트와 비교합니다.

```asm
mov rax, [rdi+90h]
mov rax, [rax+10h]
cmp rax, [r13+30h]
je known_target

mov rax, [rdi+18h]
cmp rax, [r13+38h]
je known_target

lock inc qword ptr [stats+08h] ; outside++
```

중요한 점은 **`outside++` 후에도 resume 하지 않는다는 것**입니다.

즉 두 기존 목표 조건을 모두 만족하지 않는 후보도 제거하지 않고 그대로 아래의 `score()` 호출로 진행합니다.

```asm
mov rdx, rdi
mov rcx, rsi
call score
```

이것이 게시글의:

> AI가 목표세력을 제외한 다른 국가도 공격 가능

설명과 직접 대응되는 가장 강한 정적 증거입니다.

다만 현재 단계에서는 `[r13+30]`, `[r13+38]`의 게임 자료구조 필드명을 직접 확인하지 않았으므로 문서에서는 **기존 목표 컨텍스트 A/B**라고 부릅니다.

---

## 8. 기존 목표는 완전히 버리지 않고 10% 우대 - 확정

score 함수 호출 후, 후보의 첫 번째 컨텍스트가 기존 목표 A와 같으면:

```asm
mov rax, [rdi+90h]
mov rax, [rax+10h]
cmp rax, [r13+30h]
jne no_bonus

mov eax, 3F8CCCCDh  ; float 1.1
movd xmm1, eax
mulss xmm0, xmm1
```

즉 **기존 목표 A는 score에 1.1배 보정**을 받습니다.

따라서 Viewer의 공격 후보 확장은:

- 기존 목표만 공격하도록 hard filter 하는 방식은 해제
- 다른 후보도 score 경쟁에 참가
- 기존 목표 후보에는 10% 우대 유지

라는 구조입니다.

게시글의 표현처럼 기존 목표를 완전히 무시하는 것이 아니라, **독점권은 없애고 우선도만 일부 남기는 방식**으로 해석할 수 있습니다.

---

## 9. 최종 후보 갱신 - 확정

score는 `xmm0`로 반환되고 현재 최고 score는 `xmm6`에 있습니다.

```asm
comiss xmm0, xmm6
jbe resume

movaps xmm6, xmm0
mov r15, rdi
lock inc qword ptr [stats+10h] ; selected++
```

따라서 Viewer 상태창의 **'갱신'**은 최종 공격 횟수가 아니라:

> 후보 순회 중 새 후보가 현재 최고 score를 넘어 선택 후보가 교체된 횟수

로 읽는 것이 정확합니다.

`r15 = rdi`가 새 best candidate 저장 동작입니다.

---

## 10. 항복권고 실패 boost 구간과의 경계

최종 `REFUSAL_ATTACK`에는 `score()` 호출 뒤, 기존 목표 1.1배 보정 전에 추가 블록이 존재합니다.

해당 블록은:

- 별도 stats-backed record table 조회
- 게임 날짜와 유효 구간 비교
- 최소 score 조건 확인
- 특정 조건에서 score를 **5.0배**
- `stats+0x50 (boosted)` 증가

를 수행합니다.

이 로직은 게시글의:

> 항복권고 실패 후 해당 국가의 공격우선도를 대폭 상승

부분과 연결되는 것으로 매우 유력합니다.

하지만 이 부분은 **Step 08에서 레코드 형식과 날짜 계산까지 별도로 해부**합니다.

이번 Step에서는 공격 후보 확장 자체와 혼동하지 않습니다.

---

## 11. S8RPKCheats 현재 패치와 정확한 차이

현재 S8RPKCheats:

```text
SAN8RPK.exe+144D24C
74 0A  ->  EB 0A
```

원본 문맥:

```asm
+144D248  cmp rcx, [r13+30h]
+144D24C  je  +0Ah
+144D24E  mov rax, [rdi+18h]
+144D252  cmp rax, [r13+38h]
+144D256  jne +22h
```

우리 패치는 `+144D24C`의 조건부 jump를 무조건 jump로 바꿉니다.

즉 첫 비교 결과와 관계없이 `+144D258` 쪽 원래 게임 경로로 들어가게 합니다.

### Viewer는 다름

Viewer는:

1. `+144D248` 16 bytes 전체를 cave jump로 변경
2. relation 필터를 cave에서 직접 수행
3. 기존 목표 컨텍스트 밖 후보도 `outside++` 후 score 계산
4. score helper 직접 호출
5. 기존 목표에 1.1배 보정
6. 최고 score와 비교해 `r15` 후보를 직접 갱신
7. 원래 `+144D27A`로 복귀

합니다.

따라서 두 구현은 같은 코드 지점을 건드리지만 **동작의 깊이가 다릅니다.**

| 항목 | 현재 S8RPKCheats | Viewer |
|---|---|---|
| 진입점 | `+144D24C` | `+144D248` |
| 방식 | 2-byte branch 변경 | 16-byte trampoline + cave |
| 기존 두 목표 비교 | 첫 분기를 강제 | 둘 다 검사하되 밖 후보도 허용 |
| relation helper | 원래 게임 흐름에 의존 | cave에서 직접 호출 |
| score helper | 원래 게임 흐름에 의존 | cave에서 직접 호출 |
| 목표 밖 후보 통계 | 없음 | `outside++` |
| best 후보 갱신 통계 | 없음 | `selected++` |
| 기존 목표 우대 | 원래 흐름에 의존 | 명시적으로 score × 1.1 |
| 복귀 | 원래 중간 흐름 | `+144D27A` |

---

## 12. 동시 적용 충돌 - 확정

Viewer의 16-byte hook 범위:

```text
+144D248 ~ +144D257
```

현재 S8RPKCheats가 쓰는 첫 패치:

```text
+144D24C ~ +144D24D
```

즉 **완전히 겹칩니다.**

따라서 나중에 Viewer 방식 공격 후보 확장을 이식할 경우:

- 기존 `+144D24C : 74 0A -> EB 0A`를 켠 상태에서 Viewer hook을 추가하면 안 됨.
- 동일 토글 안에서 어느 방식을 사용할지 하나만 선택해야 함.
- Viewer 방식으로 교체한다면 기존 첫 패치는 제거/대체 대상임.

우리의 다른 두 AI 전투 개선 패치:

- `+1464B91`
- `+145BDAB`

은 이번 attack hook과 직접 주소가 겹치지 않습니다. 기능 중복 여부는 Step 04와 후속 분석에서 판별합니다.

---

## 13. 함께 설치되는 static NOP 2개 - 의미 미확정

Viewer 최종 `ai_profile`은 같은 AI 기능 활성화 시 다음 static patch도 설치합니다.

### +144BB60

```text
원본: 48 B8 00 88 9F 2A 03 00 00 00 48 03 F0
적용: 90 × 13
```

### +144BC8B

```text
원본: 48 81 C6 00 F1 53 65
적용: 90 × 7
```

둘 다 `+144BA40`부터 736 bytes짜리 guard hash 영역 안에 있습니다.

현재 확보한 Viewer EXE만으로는 이 두 명령이 어떤 게임 자료구조를 이동시키는지 확정할 수 없습니다.

따라서 지금은:

- AI feature와 함께 적용되는 static patch라는 사실만 확정
- 공격 후보 확장용이라고 임의 명명하지 않음
- Step 04 또는 게임 원본 디스어셈블리 확보 시 다시 연결

로 둡니다.

---

## 14. Guard 검증 - 확정

공격 관련 Viewer guard:

| RVA | size | SHA-256 |
|---:|---:|---|
| `+144D0E2` | 426 | `5f4831b7ca17070c153034d472fc0490f309e972509b150eaedb1079ddf171ec` |
| `+17AF090` | 170 | `c7477a9233bc2c5689c58309ffeee45ace225527433e0781d8ddf2ef63a18f2d` |
| `+17A5130` | 282 | `364edeb14758c11b1a7050dd183d4a978aafce8eba2c00b4047ed2127414234a` |
| `+144BA40` | 736 | `60c495cf393e8a5a71f4e8c7357cf5979ba5d248d6c27f42778b078baa95fa8f` |

Viewer는 hook 원본 바이트뿐 아니라 이 주변 함수/영역 hash도 확인합니다.

이는 다른 게임 빌드나 타 모드가 해당 함수를 수정했을 때 적용을 중단하기 위한 보호입니다.

---

## 15. Step 03 결론

### 확정 가능한 핵심

Viewer의 **'공격 후보 확장'**은 단순히 공격 횟수 제한 하나를 해제하는 기능이 아닙니다.

실제 payload는:

1. 후보를 검사
2. 동일/관계 필터를 유지
3. 기존 목표 A/B에 해당하지 않는 후보를 **제외하지 않고 `outside` 후보로 기록**
4. 그 후보도 동일한 `score()` 경쟁에 참여
5. 기존 목표 A에는 **1.1배 우대**
6. score가 현재 최고값보다 높으면 실제 선택 후보를 갱신

합니다.

따라서 게시글의:

> 목표세력 외의 다른 국가도 공격 가능

이라는 설명은 정적 코드와 잘 맞습니다.

### 현재 우리 기능과의 관계

현재 S8RPKCheats의 `+144D24C` branch 패치는 이 문제를 더 단순한 방식으로 우회합니다.

Viewer는 같은 원본 분기 블록을 더 큰 cave 로직으로 대체하므로, **향후 이식한다면 기존 첫 패치를 대체하는 형태**가 되어야 합니다.

### 아직 남은 것

- `relation(+17AF090)` 내부 판정
- `score(+17A5130)` 정확한 점수 공식
- 두 static NOP patch의 의미
- 플레이어 우선 가산 제거의 실제 지점

이 네 항목은 다음 Step들에서 이어서 분석합니다.
