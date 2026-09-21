# Step 08 - 항복권고 실패 후 공격 우선도 / 고의리 군주 항복 억제

## 0. 범위

이 문서는 `SAN8RPK_Viewer.exe`의 AI profile에서 항복 관련 세 부분을 분리해 분석합니다.

1. 항복권고 직전: 최근 실패 대상에 대한 **재권고 차단**
2. 항복권고 실패 직후: 실패 record를 만들고 **공격 우선도 상승 상태로 전환**
3. 실제 항복 score 계산: **의리가 높은 군주의 항복 억제**

Viewer UI의 직접 설명:

```text
AI 개선: 공격 후보 확장 · 포로 판정 · 고의리 군주 항복 억제
```

Viewer 상태 통계:

```text
권고 실패 / 전환 / 재권고 차단
```

---

## 1. 최종 cave 배치 - 확정

Step 03~05에서 확인한 최종 AI payload 배치:

```text
cave + 0x000 : REFUSAL_ATTACK
cave + 0x200 : HATRED_PRISON
cave + 0x400 : REFUSAL_PRE
cave + 0x600 : REFUSAL_POST
cave + 0x800 : HATRED_CLASSIFIER
cave + 0xA00 : SURRENDER_TEMPLATE
cave + 0x1000: stats / refusal record 영역
```

항복권고 실패 관련 기능은 공격 score 로직과 같은 stats memory를 공유합니다.

---

## 2. refusal_code marker 전체 매핑 - 확정

`refusal_code()` Python 3.10 bytecode를 직접 복원했습니다.

| marker | 실제 값 |
|---|---:|
| `0x1111111111111111` | stats = `cave+0x1000` |
| `0x2222222222222222` | `SAN8RPK.exe+0x017AF090` |
| `0x3333333333333333` | `SAN8RPK.exe+0x017A5130` |
| `0x4444444444444444` | `SAN8RPK.exe+0x0144D27A` |
| `0x5555555555555555` | `SAN8RPK.exe+0x02E98BC8` manager |
| `0x6666666666666666` | `SAN8RPK.exe+0x017121D0` |
| `0x7777777777777777` | `SAN8RPK.exe+0x01451EBD` |
| `0x8888888888888888` | `SAN8RPK.exe+0x01451EDD` |
| `0x9999999999999999` | `SAN8RPK.exe+0x01451ED9` |
| `0xAAAAAAAAAAAAAAAA` | `SAN8RPK.exe+0x00125AA0` |
| `0xBBBBBBBBBBBBBBBB` | `SAN8RPK.exe+0x0144CA10` |

`+17AF090`은 Step 03에서도 attack relation helper라는 profile 이름으로 사용됩니다.

나머지 native 함수의 정확한 공식 명칭은 아직 부여하지 않습니다.

---

# Part A - 항복권고 실패 record / 재권고 차단

## 3. PRE hook - 확정

Hook:

```text
SAN8RPK.exe + 0x01451EAD
```

cave:

```text
cave + 0x400
```

원본 16 bytes:

```text
C6 44 24 20 00
44 0F B6 CD
4D 8B 87 98 00 00 00
```

해석:

```asm
mov   byte ptr [rsp+20h], 0
movzx r9d, bpl
mov   r8, [r15+98h]
```

일반 경로에서 Viewer는 위 원본 동작을 다시 실행한 뒤:

```text
+1451EBD
```

로 복귀합니다.

---

## 4. PRE의 refusal record 검색 - 확정

먼저:

```asm
rcx = r15
call +17121D0
r9 = rax

r8 = [r14+10h]
```

두 포인터가 모두 있어야 계속합니다.

그 다음 `r8` 객체의:

```text
byte [r8+8]
```

를 ID/index로 사용합니다.

허용 범위:

```text
0 <= id <= 150
```

record 주소:

```text
record = stats + 0x100 + id * 0x20
```

즉 **32-byte record를 최대 151개** 보관하는 구조입니다.

---

## 5. refusal record 구조 - 확정

PRE/POST/ATTACK 세 payload를 함께 대조하면 record layout이 명확합니다.

| offset | 크기 | 내용 |
|---:|---:|---|
| `+0x00` | 8 | `r8` 객체 포인터 |
| `+0x08` | 8 | `r9` 객체 포인터 |
| `+0x10` | 8 | 당시 manager root 포인터 |
| `+0x18` | 4 | 시작 month-index |
| `+0x1C` | 4 | 종료 month-index = 시작 + 3 |

현재 날짜 계산:

```text
monthIndex =
    WORD(manager + 0x72D0) * 12
  + BYTE(manager + 0x72D2)
```

정확한 공식 필드명은 아직 미확정이지만, 계산 단위는 명백히 **연×12 + 월 형태의 월 인덱스**입니다.

---

## 6. record 유효기간은 정확히 3개월 - 확정

POST에서 record 생성 시:

```text
record.start = currentMonthIndex
record.end   = currentMonthIndex + 3
```

PRE에서 유효 조건:

```text
current >= start
current <  end
```

따라서 record는 **3개월 window**입니다.

게시글의:

> 항복권고를 실패하면 해당국가의 공격우선도를 대폭상승 시켜 높은확률로 다음달에 공격

이라는 설명은 “딱 다음 달 1회”를 하드코딩한 게 아니라, **실패 후 3개월 동안 공격 우선도를 크게 높이는 window**로 구현되어 있습니다.

---

## 7. 재권고 차단 - 확정

PRE에서 다음 조건이 모두 맞으면:

- record의 `+00 == r8`
- record의 `+08 == r9`
- record의 `+10 == 현재 manager root`
- 현재 날짜가 `[start,end)`
- 별도 player-associated force exclusion에 걸리지 않음

Viewer는:

```asm
lock inc qword ptr [stats+48h]
```

를 실행합니다.

Step 03의 stats mapping:

```text
stats+0x48 = skipped
Viewer UI = 재권고 차단
```

따라서 이 record가 살아 있는 동안 **같은 pair에 대한 항복권고를 다시 시도하지 않게 하는 기능**이 확정됩니다.

---

## 8. player-associated force exclusion - 구조 확정 / 의미 유력

PRE/POST/ATTACK 모두 같은 패턴을 사용합니다.

```asm
manager = *(base+2E98BC8)
ctx = [manager+E0h]

if ctx != 0 and [ctx+18h] == r8:
    special logic skip
```

SURRENDER_TEMPLATE에서도 같은 `manager+E0 -> +18` 포인터를 양쪽 force와 비교합니다.

따라서 이 포인터는 **현재 플레이어/주인공과 연관된 세력 포인터**일 가능성이 매우 높습니다.

Viewer의 기능 설명이 AI 개선이고, 이 special logic에서 해당 force를 반복적으로 제외하는 점도 이 해석과 맞습니다.

다만 `manager+E0` 객체의 공식 이름을 reader가 직접 붙이지 않았으므로 문서에서는 **player-associated force (유력)**로만 기록합니다.

---

# Part B - 권고 실패 후 record 생성과 전환

## 9. POST hook - 확정

Hook:

```text
SAN8RPK.exe + 0x01451ECD
```

cave:

```text
cave + 0x600
```

원본 16 bytes:

```text
85 ED
74 08
66 41 09 BF 20 01 00 00
49 8B 5E 10
```

해석:

```asm
test ebp,ebp
je   +8
or   word ptr [r15+120h], di
mov  rbx,[r14+10h]
```

Viewer cave는 refusal 처리 후 이 원본 의미를 다시 실행하고:

```text
+1451EDD
```

로 복귀합니다.

---

## 10. POST의 대상 필터 - 확정

POST는:

```asm
r9 = call +17121D0(r15)
r8 = [r14+10h]
```

를 얻고 다음을 제외합니다.

- r8 null
- r9 null
- r8 == r9
- manager null
- player-associated force 조건
- `+17AF090(r9,r8)`가 true인 경우

마지막 helper는 Step 03에서도 attack relation helper로 불린 함수입니다.

즉 Viewer는 relation helper가 true인 pair에는 이 refusal 후 공격 전환을 만들지 않습니다.

정확히 어떤 관계가 true인지는 아직 helper 내부 미분석입니다.

---

## 11. 권고 실패 카운터 - 확정

필터를 통과하면 가장 먼저:

```asm
lock inc qword ptr [stats+38h]
```

Viewer stats:

```text
stats+0x38 = refused
UI = 권고 실패
```

따라서 이 hook 구간이 **항복권고 실패 후 처리 경로**라는 점은 Viewer 자체 통계와 직접 연결됩니다.

---

## 12. 추가 95 threshold gate - 확정 / 확률 의미 유력

refused++ 후:

```asm
ecx = 100
edx = 0
r8d = 0
call +125AA0

cmp eax,95
jae no_record
```

정확한 사실:

```text
native helper +125AA0(100,0,0)의 반환값이 95 이상이면 record를 만들지 않는다.
95 미만일 때만 다음 단계로 진행한다.
```

이 함수가 0~99 균등 난수를 반환하는 함수라면 **95% gate**가 됩니다.

하지만 `+125AA0`의 함수 본문을 현재 확보하지 않았으므로 “정확히 95% 확률”은 **유력**으로만 기록합니다.

---

## 13. record 생성 - 확정

threshold gate를 통과하면:

```text
id = byte [r8+8]
id <= 150
record = stats+0x100 + id*0x20
```

현재 monthIndex를 계산한 뒤:

```text
record+00 = r8
record+08 = r9
record+10 = manager
record+18 = currentMonthIndex
record+1C = currentMonthIndex + 3
```

를 기록합니다.

그 다음:

```asm
lock inc qword ptr [stats+40h]
```

Viewer UI:

```text
stats+0x40 = switched
UI = 전환
```

입니다.

---

## 14. '전환' native call - 확정 호출 / 의미 유력

record 생성 뒤 Viewer는:

```asm
xor ecx,ecx
mov rdx,r14
call +144CA10
```

를 실행합니다.

즉:

```text
+144CA10(0, r14)
```

형태의 native 호출이 있습니다.

Viewer 통계에서 이 시점 직전에 `switched++`가 증가하고, 게시글도 실패 후 공격 쪽으로 행동을 돌린다고 설명하므로 **AI 계획/행동 상태를 전환시키는 함수일 가능성이 높습니다.**

하지만 native 함수 본문을 아직 확인하지 않았으므로 공식 기능명은 확정하지 않습니다.

---

# Part C - 공격 우선도 ×5

## 15. REFUSAL_ATTACK이 같은 record를 사용 - 확정

Step 03의 attack payload에는 score helper 호출 직후 refusal record 조회가 들어 있습니다.

기본 score:

```asm
call +17A5130
; XMM0 = candidate score
```

그 다음 현재 candidate pair와 refusal record를 비교합니다.

record 조건은 PRE와 동일하게:

- ID 0~150
- `record+00` pair match
- `record+08` pair match
- `record+10 == current manager`
- player-associated force 제외
- 날짜가 `[start,end)`

입니다.

즉 **POST가 만든 3개월 record를 공격 후보 계산에서 그대로 소비**합니다.

---

## 16. 공격 boost의 최소 score 조건 - 확정

record가 유효해도 바로 5배하지 않습니다.

```asm
mov eax,3F99999Ah  ; float 1.2
movd xmm1,eax
comiss xmm0,xmm1
jb no_boost
```

즉:

```text
base score < 1.2 -> boost 없음
base score >= 1.2 -> boost 가능
```

입니다.

아예 의미 없는/매우 낮은 후보까지 5배해서 공격 대상으로 만드는 것을 막는 안전장치로 볼 수 있습니다.

---

## 17. 공격 score는 정확히 ×5 - 확정

조건 통과 시:

```asm
mov eax,40A00000h  ; float 5.0
movd xmm1,eax
mulss xmm0,xmm1
```

즉:

```text
XMM0 = XMM0 * 5.0
```

입니다.

그리고:

```asm
lock inc qword ptr [stats+50h]
```

가 실행됩니다.

stats mapping:

```text
stats+0x50 = boosted
```

Viewer UI 문자열에는 현재 이 숫자를 직접 출력하지 않지만 내부 통계에는 별도로 존재합니다.

---

## 18. 기존 목표 1.1배와 중첩 가능 - 확정

refusal ×5 블록 다음에 Step 03의 기존 목표 보정이 실행됩니다.

```text
refusal boost: ×5.0

그 뒤 기존 목표 A이면:
×1.1
```

따라서 둘 다 만족하는 후보는 계산 순서상:

```text
base score × 5.0 × 1.1
= base score × 5.5
```

까지 올라갈 수 있습니다.

Viewer는 “무조건 다음달 공격” 명령을 직접 넣는 게 아니라 **공격 후보 score 경쟁에서 매우 강한 우위를 주는 방식**입니다.

---

## 19. 게시글의 '다음달 공격'과 실제 코드

게시글:

> 항복권고를 실패하면 해당국가의 공격우선도를 대폭상승 시켜 높은확률로 다음달에 공격가도록 수정

정적 코드에서 확인되는 실제 구현:

1. 실패 event 감지
2. 일정 threshold gate 통과
3. 3개월 refusal record 생성
4. 같은 대상 재권고 차단
5. native 전환 함수 호출
6. 3개월 동안 해당 pair의 attack score ×5
7. 원래 공격 후보 경쟁은 계속 유지

따라서 **“다음달 공격”을 강제하는 패치가 아니라 다음 공격 계획에서 선택될 가능성을 크게 올리는 구현**입니다.

이 구분이 중요합니다.

---

# Part D - 고의리 군주 항복 억제

## 20. SURRENDER hook - 확정

Hook:

```text
SAN8RPK.exe + 0x0193C86E
```

cave:

```text
cave + 0xA00
```

Viewer가 검증하는 원본 16 bytes:

```text
48 8B 8D E0 00 00 00
48 85 C9
74 06
F3 0F 2C C6
```

해석:

```asm
mov rcx,[rbp+E0h]
test rcx,rcx
je   +6
cvttss2si eax,xmm6
```

원래 바로 뒤의:

```asm
mov [rcx],eax
```

까지 Viewer cave에서 재실행한 다음:

```text
SAN8RPK.exe + 0x0193C880
```

로 복귀합니다.

즉 Viewer는 최종적으로 원래 코드가 쓰려던 `XMM6` 값을 **쓰기 직전에 보정**합니다.

---

## 21. 적용 대상은 force의 군주 - 확정

SURRENDER_TEMPLATE은:

```asm
rdx = [stack argument]
...
rax = [rdx+0C0h]
```

를 사용합니다.

Step 05에서 Viewer reader를 통해:

```text
force + 0xC0 = ruler person pointer
```

임을 이미 교차 확인했습니다.

따라서:

```text
rdx = 항복 판정 대상 force
rax = 그 force의 현재 군주
```

라는 구조는 매우 강하게 확정됩니다.

군주가 없으면 보정하지 않습니다.

---

## 22. '강직한 성격'의 실제 구현 필드는 의리 - 확정

군주 포인터에서:

```asm
movzx edx,byte ptr [rax+5Eh]
and   edx,0Fh
cmp   edx,0Bh
jl    no_change
```

Step 05에서:

```text
person +0x5E low nibble = 의리
```

를 Viewer reader로 직접 확인했습니다.

따라서 게시글에서 말한:

> 일부 강직한 성격의 군주

는 Viewer 코드상 별도의 “강직 성격” 필드가 아니라:

> **의리값 H >= 11인 군주**

로 구현되어 있습니다.

---

## 23. 항복 score 공식 - 확정

원래 항복 score/probability 계열 값은 `XMM6`에 들어 있습니다.

H = 군주 의리:

```text
H = ruler(+0x5E) & 0x0F
```

H < 11:

```text
변경 없음
```

H >= 11:

먼저:

```asm
xmm6 = min(xmm6, 100.0)
```

그 뒤:

```text
xmm6 -= (H - 10) * 20
xmm6 = max(xmm6, 0)
```

즉 공식:

```text
S' = max(
        0,
        min(S,100) - 20*(H-10)
     )
```

입니다.

---

## 24. 의리별 감산표 - 확정

| 의리 H | 감산 | 원래 score가 100 이상일 때 최대 잔여 |
|---:|---:|---:|
| 0~10 | 0 | 변경 없음 |
| 11 | -20 | 80 |
| 12 | -40 | 60 |
| 13 | -60 | 40 |
| 14 | -80 | 20 |
| 15 | -100 | 0 |

따라서 의리 15 군주는 이 hook이 적용되는 상황에서는 `XMM6`가 최종적으로 0까지 내려갑니다.

“미세하게 확률 조절”이라는 게시글 표현과 비교하면 실제 보정폭은 의리 14~15에서는 상당히 큽니다.

다만 `XMM6`가 최종적으로 0~100의 정확한 % 확률인지, 여러 평가값 중 하나인지는 원본 전체 함수를 확인하기 전까지 **score/probability 계열 값**으로 표현합니다.

---

## 25. AI vs AI에만 적용하려는 필터 - 유력

SURRENDER_TEMPLATE은 manager에서:

```text
playerAssociatedForce = [ [manager+E0] + 18 ]
```

형태의 force 포인터를 얻은 뒤:

```asm
if playerAssociatedForce == r13: no_change
if playerAssociatedForce == rdx: no_change
```

합니다.

즉 항복 판정의 두 force 중 어느 쪽이든 이 특별 force와 같으면 의리 보정을 적용하지 않습니다.

Step 08의 refusal/attack boost에서도 같은 force를 제외합니다.

따라서 이 시스템은 **플레이어가 직접 관련된 항복/권고에는 개입하지 않고 AI끼리의 관계만 바꾸려는 설계**로 보는 것이 매우 강합니다.

공식 manager 필드명이 아직 없으므로 의미는 유력으로 둡니다.

---

# Part E - 정적 분석에서 발견된 overlap 위험

## 26. REFUSAL_PRE의 특수 jump 목적지 - 확정

PRE에서 살아 있는 refusal record를 발견하면:

```asm
stats.skipped++
jmp SAN8RPK.exe+1451ED9
```

합니다.

즉 의도는 원래 항복권고 처리의 일정 구간을 건너뛰어 재권고를 차단하는 것으로 보입니다.

---

## 27. 그런데 +1451ED9는 POST hook 범위 안이다 - 확정

POST hook:

```text
start = +1451ECD
length = 16 bytes
range = +1451ECD ~ +1451EDC
```

PRE skip 목적지:

```text
+1451ED9
```

즉:

```text
+1451ED9 ∈ [+1451ECD, +1451EDC]
```

입니다.

AIController는 POST hook의 16 bytes를 다음 trampoline으로 덮습니다.

```text
FF 25 00 00 00 00
<8-byte cave address>
90 90
```

따라서 **두 hook이 동시에 활성화된 최종 Viewer 상태에서 +1451ED9는 원래 코드가 아니라 trampoline의 중간 바이트가 됩니다.**

---

## 28. overlap은 그대로 이식하면 안 되는 버그 후보

정적 분석상 이 분기는 위험합니다.

원래 POST 원본의 `+1451ED9`는:

```asm
mov rbx,[r14+10h]
```

가 시작하는 위치입니다.

즉 제작자의 의도는 아마:

> 재권고 차단 시 POST의 refusal 기록 로직은 건너뛰되, 마지막 원본 명령 `mov rbx,[r14+10]`은 실행하고 계속 진행

이었을 가능성이 있습니다.

하지만 최종 profile에서는 그 주소 자체가 POST trampoline으로 덮입니다.

### 이식 시 안전한 후보 방식

Viewer 코드를 그대로 복제하지 말고:

```asm
; PRE skip path
mov rbx,[r14+10h]   ; 원래 +1451ED9 명령을 cave에서 직접 재현
jmp +1451EDD        ; POST hook 뒤로 복귀
```

처럼 구성하는 것이 정적으로는 더 안전해 보입니다.

**단, 이것은 아직 구현 제안일 뿐 확정 수정이 아닙니다.**

실제 게임에서 Viewer의 `재권고 차단` 카운터가 증가하는 상황을 재현하거나 원본 함수 전체를 디스어셈블한 뒤 최종 결정해야 합니다.

---

# Part F - 현재 S8RPKCheats와 비교

## 29. 현재 저장소와 직접 주소 충돌 없음

기준 main:

```text
96dfc11508bb9bfdc429f656f15530b13cb56045
```

현재 S8RPKCheats에서 다음 Viewer hook RVA 직접 사용은 확인되지 않았습니다.

```text
+1451EAD
+1451ECD
+193C86E
```

따라서 현재 구현과의 **직접 byte overlap은 없음**입니다.

향후 Viewer식 AI 공격 개선을 이식한다면 Step 03의 `+144D248` attack cave 안에 refusal ×5 block도 포함되므로, 항복권고 기능만 별도로 떼어낼지 공격 개선과 함께 묶을지 설계가 필요합니다.

---

## 30. Guard 검증 - 확정

항복권고 실패 관련:

| RVA | size | SHA-256 |
|---:|---:|---|
| `+1451E60` | 168 | `b56606d1c5d31002af3933eabf796b04c1ab3f5967528e91a07c38ed6b04a13c` |
| `+17121D0` | 56 | `6e0101651e23057521eede3eff0992af516efd1506209293059a1dfcec87c4be` |
| `+125AA0` | 48 | `fda86f3a3383d33fbef1047c74ce05325c38bd8f66f2b616d323b21676f0274d` |

항복 score 관련:

| RVA | size | SHA-256 |
|---:|---:|---|
| `+193B410` | 5366 | `68f1f77535ab5ec36e4d8d8de6b03260d516c3f99c84ad4e95b407fc710780ac` |

Viewer는 주변 코드 hash가 다르면 적용을 중단합니다.

---

## 31. Step 08 결론

### 항복권고 실패 후 공격

확정된 전체 흐름:

```text
항복권고 실패
  ↓
refused++
  ↓
native threshold gate
  ↓
3개월 refusal record 생성
  ↓
switched++
  ↓
native 전환 함수 호출
  ↓
같은 대상 재권고 차단
  ↓
같은 대상이 공격 후보가 되면
base score >= 1.2 에서 ×5
```

즉 “다음달 공격을 직접 명령”하는 기능이 아니라 **3개월 동안 항복권고 대신 공격 쪽으로 강하게 기울이는 AI 상태**입니다.

### 고의리 군주 항복 억제

확정 공식:

```text
H = 군주 의리

H < 11:
    변화 없음

H >= 11:
    S = min(S,100)
    S -= (H-10)*20
    S = max(S,0)
```

따라서 Viewer의 “강직한 군주”는 실제로 **의리 11~15**를 기준으로 구현됩니다.

### 특별 주의

`REFUSAL_PRE -> +1451ED9` 분기는 최종 POST trampoline과 주소가 겹칩니다.

이 부분은 **Viewer 자체의 잠재적 overlap 버그 후보**이며, 나중에 S8RPKCheats로 옮길 때 절대 그대로 복사하지 않습니다.
