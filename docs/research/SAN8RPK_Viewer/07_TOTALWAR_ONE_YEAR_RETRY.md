# Step 07 - 결전 발생빈도 조정: 1년 재시도 gate

## 0. 범위

이 문서는 `SAN8RPK_Viewer.exe`의 `totalwar_profile.py`를 분리해서 분석한 기록입니다.

Viewer UI 기능명:

```text
결전 대기 1년 켜기
```

활성 상태 설명:

```text
켜짐 · 결전 대기 1년 · 다른 발동 조건은 유지
```

profile 자체 설명:

```text
TotalWarPoint only: one-year retry gate, native remaining conditions preserved.
```

따라서 이 Step의 핵심은 **결전을 강제 발생시키는지**가 아니라, 실제로 어떤 값을 1로 바꾸고 무엇을 그대로 남기는지 확인하는 것입니다.

---

## 1. profile 기본 정보 - 확정

`totalwar_profile.marshal310`

- 크기: 1,160 bytes
- SHA-256: `44297589e2ea88df80d9da40f92d80c20ff846f3b47b8e03f8226c356841dc37`

`totalwar_patch.marshal310`

- 크기: 454 bytes
- SHA-256: `7f437bebbd35e1da8a6c06ebe7a734aac9e7f32cb76062da3ad0161960d64848`

`totalwar_patch`는 공통 `AIController`를 상속해:

- exact-build 검증
- code cave 할당
- 원본 바이트 검증
- patch/restore
- 실패 시 rollback/recovery

기능을 그대로 재사용합니다.

별도 AI 판단 로직이나 stats 계산은 없습니다.

---

## 2. hook은 두 군데 - 확정

### Path A

Hook:

```text
SAN8RPK.exe + 0x01335961
```

Resume:

```text
SAN8RPK.exe + 0x01335971
```

원본 16 bytes:

```text
45 0F B6 59 38
45 0F B6 51 35
41 8D 43 FF
3C 0B
```

해석:

```asm
movzx r11d, byte ptr [r9+38h]
movzx r10d, byte ptr [r9+35h]
lea   eax, [r11-1]
cmp   al, 0Bh
```

---

### Path B

Hook:

```text
SAN8RPK.exe + 0x0132E4BD
```

Resume:

```text
SAN8RPK.exe + 0x0132E4CD
```

원본 16 bytes:

```text
45 0F B6 51 35
45 0F B6 59 38
41 8D 43 FF
3C 0B
```

해석:

```asm
movzx r10d, byte ptr [r9+35h]
movzx r11d, byte ptr [r9+38h]
lea   eax, [r11-1]
cmp   al, 0Bh
```

두 원본은 첫 두 load의 순서만 다르고 최종 레지스터 상태는 같습니다.

---

## 3. Viewer가 삽입하는 공통 TEMPLATE - 확정

`totalwar_template.bin`

- 크기: 29 bytes
- SHA-256: `1df97dc56963ed13d15823626fb7188107ef5c2160d6aa8f863d7acfdb9ece70`

바이트:

```text
45 0F B6 59 38
45 0F B6 51 35
41 80 79 08 01
75 06
41 BA 01 00 00 00
41 8D 43 FF
3C 0B
```

해석:

```asm
movzx r11d, byte ptr [r9+38h]
movzx r10d, byte ptr [r9+35h]

cmp   byte ptr [r9+08h], 1
jne   not_totalwar

mov   r10d, 1

not_totalwar:
lea   eax, [r11-1]
cmp   al, 0Bh
```

이 29 bytes 뒤에 absolute indirect jump가 붙어 각각 원래 resume 주소로 돌아갑니다.

---

## 4. 실제 변경점은 단 하나 - 확정

원래:

```text
r10d = byte [r9+0x35]
```

Viewer:

```text
r10d = byte [r9+0x35]

if byte [r9+0x08] == 1:
    r10d = 1
```

즉 Viewer가 추가하는 의미 있는 변화는:

> **특정 record의 +0x08 byte가 1일 때, +0x35에서 읽어온 값을 메모리에는 쓰지 않고 r10d에서만 1로 강제한다.**

입니다.

나머지:

- `r11d = [r9+0x38]`
- `eax = r11d - 1`
- `cmp al, 0x0B`

는 원래 동작을 그대로 재현합니다.

---

## 5. +0x08 == 1의 의미 - profile 기준 확정

게임 구조의 공식 필드명을 직접 확인한 것은 아니지만, profile 제작자는 모듈 docstring에서 명시적으로:

```text
TotalWarPoint only
```

라고 적었습니다.

그리고 template에서 유일한 대상 식별 조건은:

```asm
cmp byte ptr [r9+08h],1
```

입니다.

따라서 **이 profile의 의미상 `[r9+0x08] == 1`이 TotalWarPoint 대상을 식별하는 조건**으로 사용된다는 점은 확정할 수 있습니다.

공식 게임 구조체 필드명은 아직 미확정입니다.

---

## 6. +0x35의 의미 - 기능 수준 확정 / 공식 이름 미확정

profile 설명은:

```text
one-year retry gate
```

입니다.

그리고 template에서 TotalWarPoint일 때 **유일하게 1로 바뀌는 값**은:

```text
r10d <- [r9+0x35]
       ↓
r10d <- 1
```

입니다.

따라서 이 코드 경로에서 `+0x35`의 값은 **재시도 대기 기간 계산에 들어가는 값**이며, Viewer는 이를 1년으로 취급하게 만드는 것으로 볼 수 있습니다.

정확히 말하면 Viewer는:

- `[r9+0x35]` 메모리 자체를 1로 쓰지 않음
- 두 코드 경로에서 읽힌 뒤의 **레지스터 값만 1로 override**

합니다.

따라서 게임 데이터 원본을 영구 수정하는 기능이 아닙니다.

---

## 7. +0x38은 1~12 범위 값 - 강한 정적 증거

원본과 Viewer 양쪽 모두:

```asm
movzx r11d, byte ptr [r9+38h]
lea   eax, [r11-1]
cmp   al, 0Bh
```

를 수행합니다.

이는 unsigned 기준으로:

```text
(value - 1) <= 11
```

형태의 **1~12 범위 검사**를 만들 때 쓰이는 전형적인 패턴입니다.

따라서 `+0x38`은 월(month) 또는 월과 같은 1~12 범위 필드일 가능성이 매우 높습니다.

하지만 후속 conditional jump는 resume 이후 원래 게임 코드에 있으므로, 현재 자료만으로 공식 필드명을 `월`이라고 확정하지는 않습니다.

문서 분류:

- `1~12 range input`: **확정**
- `month field`: **유력**
- 공식 구조체 필드명: **미확정**

---

## 8. native 조건 flags를 그대로 보존 - 확정

중요한 점은 template 마지막 명령이:

```asm
cmp al,0Bh
```

라는 것입니다.

그 직후 Viewer는 조건을 새로 판단하지 않고 원래 코드 주소로 jump합니다.

즉 `cmp`가 만든 CPU flags를 그대로 가지고 원래 native 코드로 돌아갑니다.

따라서 Viewer는:

- 원래의 1~12 유효성/후속 조건분기 자체를 제거하지 않음
- TotalWarPoint에 필요한 interval 입력값 하나만 바꿈
- 후속 native 판단은 그대로 실행

합니다.

이것이 profile의:

```text
native remaining conditions preserved
```

와 정확히 일치합니다.

---

## 9. 두 hook에 같은 로직을 설치하는 이유

최종 `build_payload()`:

```python
first = build_v015_payload(base, stats)

second = (
    TEMPLATE
    + absolute_jump(base + 0x132E4CD)
)

return first.ljust(0x100, b"\xCC") + second
```

따라서 cave 배치는:

```text
cave + 0x000 : Path A payload
cave + 0x100 : Path B payload
```

입니다.

Path A:

```text
+1335961 -> cave+000 -> +1335971
```

Path B:

```text
+132E4BD -> cave+100 -> +132E4CD
```

두 곳 모두 **동일한 TotalWarPoint 1년 override**를 적용합니다.

다만 원본 게임의 두 함수 전체를 확보하지 않았기 때문에 각각의 공식 역할:

- 생성 시 검사
- 재평가 시 검사
- AI/플레이어 경로
- 서로 다른 scheduler 경로

중 무엇인지까지는 아직 확정하지 않습니다.

---

## 10. 왜 첫 결전은 기존과 비슷할 수 있는가

사용자 제공 설명:

> 새지구를 판 뒤 첫 결전은 기존과 비슷하고, 두 번째 결전 발생부터 바로 체감된다.

Viewer 내부 코드와 이 설명은 잘 맞습니다.

### 코드에서 확정되는 것

Viewer가 바꾸는 것은:

- TotalWarPoint record의 **retry gate 입력값**
- 그것도 1년으로 override

뿐입니다.

Viewer는:

- 결전 조건을 무조건 true로 만들지 않음
- 결전 포인트를 강제로 채우지 않음
- 다른 native 조건분기를 제거하지 않음
- 초기 결전 발생일을 직접 지정하지 않음

### 따라서 가능한 해석

첫 결전은 아직 **“재시도 대기”가 적용된 뒤의 재발생 케이스가 아니기 때문에** 원래 초기 조건의 영향을 그대로 많이 받을 수 있습니다.

첫 결전이 한 번 발생한 뒤 다음 기회를 계산할 때는 이 1년 override가 직접 체감되기 쉬워집니다.

이 설명은 profile의 `one-year retry gate`와 매우 잘 맞지만, 전체 이벤트 scheduler를 본 것은 아니므로 **코드에 기반한 유력한 해석**으로 기록합니다.

---

## 11. “바닐라보다 3배”는 코드에 직접 박힌 배수가 아니다

사용자 제공 설명에는:

> 초기버전에서는 바닐라보다 3배정도 잘 발생하도록 수정

이라는 표현이 있습니다.

하지만 현재 분석한 Viewer v0.26c의 `totalwar_profile`에는:

- `×3`
- `3배`
- 확률 배수

연산이 없습니다.

실제 변경은:

```text
retry input -> 1
```

입니다.

따라서 **“3배 정도”는 플레이 체감/테스트 결과 표현이지 현재 profile의 수학적 ×3 패치가 아닙니다.**

이 구분은 중요합니다.

---

## 12. code cave와 hook 형식 - 확정

공통 `AIController`는 `0x3000` bytes를 code cave로 예약합니다.

두 hook의 원본 길이는 각각 정확히 16 bytes이며, Viewer의 `make_jump()`는:

```text
FF 25 00 00 00 00
<8-byte absolute destination>
90 90
```

형식의 16-byte jump를 만듭니다.

따라서:

### Hook A

```text
+1335961
16-byte original
->
16-byte jump to cave+0
```

### Hook B

```text
+132E4BD
16-byte original
->
16-byte jump to cave+0x100
```

기능 해제 시 각각 원본 16 bytes를 그대로 복구합니다.

---

## 13. Guard 검증 - 확정

### Path A 주변

```text
RVA  +0x013358E0
size 264
SHA-256
4024c87461ec34b1f00ab30a703041de4e77e08af8be546b3e0359938e85ceae
```

### Path B 주변

```text
RVA  +0x0132E340
size 588
SHA-256
671dbd6df61a7ab184a9273d980fac063f815c2e44035445d25096d507a36636
```

따라서 다른 게임 빌드나 타 모드가 주변 코드를 바꾼 경우 Viewer는 적용을 거부합니다.

---

## 14. stats는 사용하지 않는다 - 확정

공통 `AIController.enable()`은:

```text
build_payload(base, cave+0x1000)
```

형태로 stats 주소를 전달합니다.

하지만 `totalwar_profile.build_v015_payload()`와 `build_payload()`는 전달받은 `stats`를 실제 template에 넣지 않습니다.

즉 결전 기능은:

- 발생 횟수 counter 없음
- retry counter 없음
- 별도 runtime 통계 없음

입니다.

Viewer의 `show_totalwar_status()`도 stats 대신:

```text
켜짐 · 결전 대기 1년 · 다른 발동 조건은 유지
```

라는 상태만 표시합니다.

---

## 15. 현재 S8RPKCheats와의 충돌 여부

기준 `main`:

```text
96dfc11508bb9bfdc429f656f15530b13cb56045
```

현재 저장소에서 다음 Viewer RVA의 직접 사용은 확인되지 않았습니다.

```text
+1335961
+1335971
+132E4BD
+132E4CD
```

관련 시스템 기능도 확인했습니다.

- `TengiCave.cpp`
- `SystemMonth.cpp`
- `MonthCapture.cpp`

현재 이 파일들은 위 RVA를 사용하지 않습니다.

특히 기존 `전기 주소 캡처` 기능은 별도 hook으로 전기 데이터 주소를 캡처하는 기능이며, 이번 Viewer의 **결전 재시도 gate 코드**와 직접 주소 충돌하지 않습니다.

따라서 현재 확인 범위에서는 **직접 주소 충돌 없음**입니다.

---

## 16. 나중에 이식할 경우 최소 구현

이 기능은 비교적 독립적으로 이식할 수 있습니다.

필요한 것은 실질적으로:

1. `+1335961` 16-byte hook
2. `+132E4BD` 16-byte hook
3. 작은 공통 cave 로직
4. 원본 바이트/주변 signature 검증
5. 해제 시 원복

입니다.

단순히 `+0x35` 메모리를 찾아서 항상 1로 쓰는 방식은 Viewer와 다릅니다.

Viewer 방식의 장점은:

- TotalWarPoint일 때만 적용
- 해당 코드가 실제 계산될 때만 override
- 원본 데이터는 그대로 유지
- 다른 전기/이벤트에 영향 최소화

입니다.

---

## 17. 이식 전 read-only 검증 권장

실제 게임에서 먼저 확인할 값:

```text
[r9+0x08]
[r9+0x35]
[r9+0x38]
```

두 hook에서 read-only 로그를 찍어서:

- `+08 == 1`일 때 실제로 결전 레코드인지
- 바닐라 `+35` 값이 몇인지
- `+38`이 실제 월 값인지
- 첫 결전 전/후 `+35` 입력이 어떻게 쓰이는지

를 확인하면 구조가 거의 완전히 확정됩니다.

특히 `+35` 값을 게임 데이터에 write하지 말고, Viewer처럼 **레지스터 override만 테스트**하는 것이 안전합니다.

---

## 18. Step 07 결론

### 확정

Viewer의 결전 기능은 두 코드 경로에 동일한 작은 hook을 설치합니다.

핵심 변경:

```text
if [r9+0x08] == 1:
    effective_value_from_[r9+0x35] = 1
```

그리고:

```text
[r9+0x38]의 1~12 범위 검사
+ 이후 native 조건
```

는 그대로 유지합니다.

따라서 **결전을 강제로 발동하는 기능이 아니라, TotalWarPoint의 재시도 대기를 1년으로 줄이는 기능**이라는 Viewer 내부 설명과 코드가 일치합니다.

### 유력

- `+0x35`: 결전 재시도 기간의 연 단위 입력값
- `+0x38`: 월 또는 월에 준하는 1~12 필드
- 첫 결전이 바닐라와 비슷하고 두 번째부터 빨라지는 이유는 초기 발동 조건은 그대로이고 재시도 gate만 줄이기 때문

### 미확정

- `r9` 구조체의 공식 이름
- `+08 / +35 / +38`의 게임 공식 필드명
- 두 hook A/B가 각각 scheduler의 어느 단계인지

이 세 가지는 실제 게임 read-only 진단으로 확정하면 됩니다.
