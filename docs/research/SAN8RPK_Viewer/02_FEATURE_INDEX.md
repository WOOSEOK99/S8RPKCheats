# Step 02 - 기능별 모듈/RVA 인덱스

이 문서는 EXE에서 추출한 Python 3.10 marshal 데이터의 정수 상수를 스캔하여 만든 **탐색 지도**입니다.

주소 존재 자체는 확정이지만, 일부 주소의 정확한 역할은 이후 디스어셈블리 분석 전까지 미확정입니다.

## A. ai_profile

확인된 RVA 상수:

```text
+0x0144D248
+0x017AF090
+0x017A5130
+0x0144D27A
+0x01E52A8D
+0x01E528A1
+0x02E98BC8
+0x017121D0
+0x01451EBD
+0x01451EDD
+0x01451ED9
+0x0144CA10
+0x01451EAD
+0x01451ECD
+0x01712670
+0x017B0AB0
+0x017B4570
+0x017A8270
+0x01E541A8
+0x01E54190
+0x0144BB60
+0x0144BC8B
+0x0193C86E
+0x0193C880
```

### 1차 그룹화

| 그룹 | RVA | 현재 상태 |
|---|---|---|
| 공격 메인 hook | `+144D248` | 확정 |
| 공격 복귀 지점 후보 | `+144D27A` | 유력 |
| 공격 helper | `+17AF090`, `+17A5130` | 존재 확정 / 의미 미확정 |
| AI 포로 hook | `+1E52A8D` | 유력 |
| 포로 복귀/관련 | `+1E528A1` | 존재 확정 |
| 포로/관계 추가 hook | `+1E54190` | 유력 |
| 포로/관계 복귀 | `+1E541A8` | 존재 확정 |
| 항복권고 실패 계열 | `+1451EAD`, `+1451ECD` | 유력 |
| 해당 복귀 후보 | `+1451EBD`, `+1451EDD`, `+1451ED9` | 존재 확정 |
| 항복 확률/성향 계열 | `+193C86E` | 유력 |
| 해당 복귀 후보 | `+193C880` | 존재 확정 |
| static NOP patch | `+144BB60`, `+144BC8B` | 확정 |
| 기타 helper/global | `+2E98BC8`, `+17121D0`, `+1712670`, `+17B0AB0`, `+17B4570`, `+17A8270`, `+144CA10` | 의미 미확정 |

`ai_profile` 내부에는 `PRISON_TEMPLATE`, `HATRED_CLASSIFIER`, `HATRED_PRISON`, `SURRENDER_TEMPLATE`, `REFUSAL_ATTACK`, `REFUSAL_PRE`, `REFUSAL_POST` 같은 명칭도 존재합니다. 따라서 공격뿐 아니라 포로/혐오관계/항복권고가 하나의 프로필에 묶여 있습니다.

## B. message_profile

내부 설명:

```text
Exact-build, display-only branches. No code cave or decision changes.
```

확인된 RVA와 최종 치환:

| RVA | 원본 | 변경 | 1차 역할 |
|---:|---|---|---|
| `+0x01E599BE` | `74 75` | `90 90` | AI 결과 메시지 표시 조건 우회 (등용/석방 중 하나, 유력) |
| `+0x01E5A82C` | `75 59` | `90 90` | AI 결과 메시지 표시 조건 우회 (등용/석방 중 하나, 유력) |
| `+0x01E5BA1F` | `0F 84 D1 00 00 00` | `41 0F B6 C6 90 90` | 복수 처형 상세 메시지용 카운트/인덱스 전달 (유력) |
| `+0x01E5BA4B` | `BA 43 00 00 00` | `8D 54 80 3E 90` | `EDX = 5*EAX + 62` 동적 메시지 ID 선택 |

이 모듈은 **AI 의사결정을 바꾸지 않고 포로 결과 메시지 표시 경로만 변경**합니다. code cave도 사용하지 않습니다.

상세 분석: [Step 06](06_AI_EXECUTION_MESSAGE_DETAIL.md)

## C. totalwar_profile

내부 설명:

```text
TotalWarPoint only: one-year retry gate, native remaining conditions preserved.
```

확인된 RVA:

```text
+0x01335961  hook A
+0x01335971  resume A
+0x0132E4BD  hook B
+0x0132E4CD  resume B
```

두 hook 모두 같은 논리를 삽입합니다.

```text
r11d = byte [r9+0x38]
r10d = byte [r9+0x35]

if byte [r9+0x08] == 1:
    r10d = 1

eax = r11d - 1
cmp al, 11
```

즉 `+0x08 == 1`인 TotalWarPoint 대상에 대해서만 `+0x35`에서 읽어온 retry 입력값을 레지스터상 `1`로 강제합니다. 메모리 원본은 수정하지 않고, `+0x38` 값과 원래 후속 조건분기용 flags는 그대로 유지합니다.

상세 분석: [Step 07](07_TOTALWAR_ONE_YEAR_RETRY.md)

## D. 기능별 후속 문서 예정

### AI 공격

분석해야 할 것:

- `+144D248` 원본 블록 전체 의미
- payload가 판단하는 target/relation/score
- `+144BB60`, `+144BC8B`의 실제 자료구조 의미
- 플레이어 우선 가산 제거 위치
- 목표세력 외 공격 허용 조건
- 약한 세력 우선 점수 방식

### AI 처형

분석해야 할 것:

- `+1E52A8D`, `+1E54190` 두 hook의 역할 분담
- 포로/군주 포인터 구조
- 상성값 및 군주 내면/성격 필드
- 원수/혐오의 native 판정 함수
- 최종 확률식 및 난수 비교

### AI 처형 로그

분석해야 할 것:

- 네 분기가 각각 어떤 메시지 케이스인지
- 플레이어 관여 전투용 상세 로그 흐름을 AI끼리 전투에도 재사용하는지
- UI 표시만 바꾸는지 재확인

### 결전

분석해야 할 것:

- 두 hook의 날짜/포인트 필드
- '1년' 값이 어떤 단위로 저장되는지
- 첫 결전과 두 번째 이후 결전이 다른 이유

### 항복권고

상세 분석 완료: [Step 08](08_SURRENDER_REFUSAL_AND_HONOR.md)

핵심 확인값:

- `+1451EAD`: 재권고 차단용 PRE hook
- `+1451ECD`: 권고 실패 기록용 POST hook
- refusal record: `stats+0x100 + id*0x20`
- 유효기간: 현재 `year*12+month`부터 **3개월**
- 유효 record가 있으면 재권고 차단
- 같은 record가 공격 후보와 맞으면 기본 공격 score가 **1.2 이상일 때 ×5**
- `+193C86E`: 고의리 군주 항복 억제 hook
- 대상 세력 군주의 `의리(+0x5E low nibble)`가 11 이상이면 surrender score를 단계적으로 감산
- H=11/12/13/14/15 → 최대치 기준 각각 -20/-40/-60/-80/-100
- Viewer의 “강직한 군주” 구현은 별도 성격 필드가 아니라 **의리값** 사용

주의:
`REFUSAL_PRE`의 skip 목적지 `+1451ED9`가 `REFUSAL_POST`의 16-byte hook 범위 `+1451ECD~+1451EDC` 안에 들어갑니다. 정적 분석상 overlap 위험/버그 후보이므로 그대로 이식하지 않습니다.


## E. 이식/충돌 종합

Step 03~08의 결과를 현재 S8RPKCheats `main`과 대조한 종합표는 [Step 09](09_PORTING_AND_CONFLICT_MAP.md)에 정리했습니다.

핵심:
- Viewer 공격 후보 확장 `+144D248`은 현재 `+144D24C` 패치와 **직접 중첩**.
- Viewer 플레이어 우선 가산 제거 `+144BB60/+144BC8B`는 현재 `+1464B91`과 주소는 다르지만 **기능 목적 중복 가능성**.
- Viewer AI 포로 판정과 현재 `도독 포로 직접 처분`은 주소/목적이 분리됨.
- 메시지 상세화와 결전 1년 gate는 현재 확인 범위에서 독립 이식 가능.
- 항복권고 PRE/POST는 Viewer 자체 hook overlap 위험 때문에 그대로 복사하지 않고 재설계 필요.


## F. 런타임 검증 기준

정적 분석 내용을 실제 게임에서 확정하기 위한 체크리스트는 [Step 10](10_RUNTIME_VALIDATION_CHECKLIST.md)에 정리했습니다.

핵심 원칙:
- 공격 후보 확장은 기존 +144D24C와 동시 적용 금지.
- AI 포로 공식은 먼저 read-only 로그로 rdi/rsi/ECX/T 의미를 확정.
- 항복권고 PRE/POST는 overlap 위험 때문에 원본 Viewer jump를 그대로 사용하지 않음.
- 메시지/결전/고의리 항복 억제는 독립 기능부터 단독 검증.
