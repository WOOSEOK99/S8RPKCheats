# Step 12 - SAN8RPK AI V2.0 업데이트 기능 정리 및 이식 우선순위

## 0. 목적

2026-09-22 업로드된 `SAN8RPK_AI_V2.0.exe`와 사용자가 제공한 V2.0 개선내역을,
기존 `research/san8rpk-viewer-analysis` 문서와 대조해 정리합니다.

이번 문서는 **구현 전 탐색 지도**입니다.

원칙:

- 사용자 설명과 EXE에서 직접 확인된 정적 근거를 구분합니다.
- 기존 Viewer(v0.26 계열) 분석 결과를 그대로 V2.0에 대입하지 않습니다.
- 새 모듈은 원본 바이트/RVA/payload를 별도 문서에서 확인한 뒤 이식합니다.
- 실제 코드 이식은 기능별 브랜치에서 하나씩 진행합니다.

---

## 1. 분석 대상 EXE - 확정

파일:

```text
SAN8RPK_AI_V2.0.exe
```

정적 확인:

```text
형식      : PE32+ x86-64 GUI
파일 크기 : 9,446,979 bytes
SHA-256   : 763e8657877aee222d0d1c90b20382f4b204431e1ffb400d7f968a29dc44d268
PyInstaller Python tag : 310
CArchive entries       : 967
PYZ modules            : 128
```

기존 분석 대상 Viewer:

```text
파일 크기 : 9,423,704 bytes
SHA-256   : 34abf52f9651d4313076b16042476e5646967aa55af65613f542d69883084c3d
```

따라서 V2.0은 기존 분석본과 **다른 빌드**입니다.

---

# 2. V2.0에서 새로 확인된 모듈 - 확정

PYZ 내부에서 다음 모듈을 직접 확인했습니다.

```text
deputy_patch
deputy_profile
isolated_patch
isolated_profile
movement_patch
movement_profile
release_patch
release_profile
league_patch
league_profile
player_change
```

기존 계열 모듈도 유지됩니다.

```text
ai_profile
message_profile
totalwar_profile
```

즉 V2.0은 기존 AI/포로/결전 기능을 유지하면서
부장 포획, 고립 도시, 이동 제한, 석방 관계, 연합, 주인공 변경을
별도 모듈로 추가한 구조입니다.

---

# 3. 개선내용 1 - 주인공 소속도시 AI 개선

## 사용자 설명

- 주인공이 군주가 아닐 때 주인공 소속 도시의 군주/태수가 공격을 잘 가지 않음.
- 게임 시작 후 약 9개월 동안 공격하지 않는 현상.
- 호전도 옵션과 관계없이 비호전 계수처럼 행동.
- 공격 딜레이와 공격계수를 게임 시작 옵션의 호전도에 맞게 적용.

## V2.0 정적 상태

`ai_profile` 자체가 기존보다 커졌습니다.

기존 분석본:

```text
ai_profile decompressed size = 9,866 bytes
```

V2.0:

```text
ai_profile decompressed size = 12,679 bytes
```

기존 공격/포로/항복 payload 흔적도 유지됩니다.

```text
build_attack_payload
build_prisoner_payload
REFUSAL_ATTACK
REFUSAL_PRE
REFUSAL_POST
HATRED_PRISON
```

### 현재 판정

**유력 / 별도 해부 필요**

주인공 소속도시 AI 보정이 `ai_profile`에 추가된 것으로 보이나,
정확한 RVA와 9개월 gate/공격계수 계산은 아직 별도 분석이 필요합니다.

### 현재 S8RPKCheats와의 관계

현재 `AI 전투 개선`과 직접 기능 중복 가능성이 높으므로
먼저 이식하지 않습니다.

기존 AI 전투 개선 주소와 겹치는지 확인한 뒤 A/B 테스트가 필요합니다.

---

# 4. 개선내용 2 - 부장이 포로로 안 잡히는 현상 수정

## 사용자 설명

- 총대장은 포로가 되지만 부장 1~2명은 항상 빠져나가는 문제.
- 총대장이 포획된 경우 부장도 포획 대상이 되도록 수정.

## EXE 직접 근거 - 확정

신규 모듈:

```text
deputy_patch
deputy_profile
```

`deputy_profile` 내부 설명:

```text
Companion capture only after native commander capture succeeds.
```

즉:

> **게임 원래 총대장 포획이 성공한 경우에만 부장 포획 로직을 추가**

하는 구조입니다.

별도의 `deputy_profile`로 분리되어 있고
`HOOK_RVA`, `ORIGINAL`, `HOOKS`, `GUARDS`, 단일 `TEMPLATE` 구조가 확인됩니다.

### 현재 판정

**확정**

### 이식 난이도

**낮음~중간**

- 전투 결과 포획 흐름만 수정.
- 기존 총대장 포획 판정은 유지.
- 현재 S8RPKCheats의 도독 포로 처분과 목적이 다름.
- 독립 기능으로 테스트하기 좋음.

### 우선순위

**전투 관련 1순위 이식 후보**

---

# 5. 개선내용 3-A - 고립 도시 함락 시 수비 장수 전원 포로

## 사용자 설명

- 고립된 도시가 점령돼도 장수들이 먼 아군 도시로 퇴각하는 문제.
- 고립된 도시 함락 시 수성측 무장을 포로 처리.
- 영토 분단/고립을 이용한 확정 포획 전략 가능.

## EXE 직접 근거 - 확정

신규 모듈:

```text
isolated_patch
isolated_profile
```

`isolated_profile` 내부 설명:

```text
Isolated city defeat: include resident defenders in the native prisoner list.
```

즉:

> **고립 도시 패배 시 해당 도시 잔류 수비 장수를 게임 원래 포로 리스트에 추가**

하는 구조입니다.

`isolated_profile`은 별도 payload와 trampoline을 갖는 독립 hook입니다.

### 현재 판정

**확정**

### 이식 난이도

**중간**

부장 포획보다 payload가 크고,
도시 연결/거주 무장/포로 리스트 구조를 같이 확인해야 합니다.

### 우선순위

**전투 관련 2순위 이식 후보**

---

# 6. 개선내용 3-B - 고립 도시 이동 꼼수 차단

## 사용자 설명

플레이어 세력이 이동 명령으로 고립된 도시로 이동할 수 있던
버그/꼼수를 차단.

## EXE 직접 근거 - 확정

신규 모듈:

```text
movement_patch
movement_profile
```

내부 설명:

```text
v0.39: player and AI personnel movement limited to the transport road component.
```

즉 플레이어와 AI의 인사 이동을
**운송 도로 연결 component 내부로 제한**하는 기능입니다.

`movement_profile`은 여러 target/resume/caller를 가지는 비교적 큰 profile입니다.

### 현재 판정

**확정**

### 이식 난이도

**중간~높음**

전투 포획 기능보다 인사 이동 UI/AI 이동 여러 경로에 영향을 줄 가능성이 있어
초기 이식 대상으로는 뒤로 미룹니다.

---

# 7. 개선내용 4 - 주인공 변경

## 사용자 설명

장수 목록에서 대상을 선택하고 게임 도중 주인공을 변경.

## EXE 직접 근거 - 확정

신규 모듈:

```text
player_change
```

내부 설명:

```text
One-shot, verified 8-byte protagonist pointer replacement; no disk patch.
```

그리고 다음 구조가 확인됩니다.

```text
PlayerController
change
recover
SELECTION_RVA
SELECTION_BYTES
MANAGER_RVA
PERSON_TABLE
```

### 현재 판정

**확정**

### 이식 난이도

**낮음~중간**

다만 전투 관련 우선순위는 낮습니다.

---

# 8. 개선내용 5 - 포로 석방 시 조건부 관계 개선

## 사용자 설명

- 포로 석방 시 상성이 잘 맞는 경우 관계도 소폭 개선.
- 상성이 맞지 않는 장수는 큰 효과 없음.

## EXE 직접 근거 - 확정

신규 모듈:

```text
release_patch
release_profile
```

`release_profile` 내부 설명:

```text
Battle release friendship bonus, exact build, runtime only.
```

즉 전투 후 포로 석방 시 관계 보너스를 추가하는 독립 기능입니다.

### 기존 Step 05와 구분

기존 Step 05:

```text
AI가 포로를 석방할지 / 처형할지 결정하는 판정 공식
```

V2.0 release profile:

```text
실제로 석방된 이후 관계 보너스를 적용
```

서로 다른 기능입니다.

### 현재 판정

**확정**

### 이식 난이도

**중간**

전투 결과와 직접 연결되며 기존 포로 처리 구조와 결합하기 좋습니다.

### 우선순위

**전투 관련 3순위 이식 후보**

---

# 9. 개선내용 6 - 결전 / 악적발호 대기시간 및 지속기간

## 사용자 설명

- 결전과 악적발호 재발생 대기 3~5년 → 1년.
- 지속기간 6개월 → 9개월.

## EXE 직접 근거 - 확정

V2.0 `totalwar_profile` 내부 설명:

```text
TotalWarPoint and LogisticsBlockadePoint:
one-year retry gate and nine-month duration.
```

따라서 기존 Step 07의:

```text
TotalWarPoint only: one-year retry gate
```

보다 기능이 확장됐습니다.

추가로 V2.0 profile에는:

```text
DURATION_RVA
DURATION_ORIGINAL
DURATION_TEMPLATE
cooldown
```

이 확인됩니다.

### 현재 판정

**확정**

- 결전뿐 아니라 악적발호도 1년 retry 대상.
- 지속기간 9개월 patch가 추가됨.

### 현재 S8RPKCheats와의 관계

현재 프로젝트에는 이미:

```text
결전 발생 주기 단축
```

기능이 있으므로,
새 V2.0 기능을 그대로 추가하지 말고 기존 구현과 비교해서:

1. 악적발호까지 확장할지
2. 지속기간 9개월을 별도 체크박스로 둘지
3. 기존 결전 1년 로직과 같은 주소/효과인지

를 확인해야 합니다.

---

# 10. 개선내용 7 - AI 공백지 점령 대기시간 2년 → 1년

## 사용자 설명

- AI가 게임 시작 후 2년이 지나야 공백지를 점령하던 제한.
- 1년으로 단축.
- 3개월/6개월은 초기 군량 차이 때문에 특정 세력이 지나치게 선점.
- 7월 군량수입까지 한 번 기다리는 1년이 적합.

## V2.0 정적 상태

`ai_profile`에 새 AI 전략 보정이 추가된 것은 확실하지만
이 항목의 정확한 RVA/바이트는 아직 별도 해부가 필요합니다.

기존 S8RPKCheats에는:

```text
+145BDAB
B8 01 00 00 00 -> B8 00 00 00 00
```

공백지 관련 보완이 이미 존재합니다.

### 현재 판정

**유력 / 직접 대조 필요**

기존 기능과 의미가 같다고 단정하면 안 됩니다.

V2.0의 “2년 → 1년”은
현재 프로젝트의 “일부 군주의 공백지 점령 제한 플래그 무력화”와
다른 gate일 가능성이 있습니다.

---

# 11. 개선내용 8 - 연합

## 사용자 설명

1. 게임 시작 옵션의 연합 빈도가 플레이어 대상 연합뿐 아니라
   AI 세력 대상 연합 결성에도 적용.
2. 연합군과 타깃 세력 사이에서 군주 포획 시 처형 확률 대폭 증가.

## EXE 직접 근거 - 연합 결성 옵션 부분 확정

신규 모듈:

```text
league_patch
league_profile
```

`league_profile` 내부 설명:

```text
v0.42: share player coalition thresholds/probabilities with AI targets.
```

즉:

> 플레이어 대상 연합에 사용하던 threshold/probability를
> AI 대상 연합에도 공유

하는 기능은 정적 근거가 확실합니다.

profile 내부에도:

```text
threshold
probability
```

가 직접 확인됩니다.

### 군주 포획 시 처형 확률 증가

**미확정**

사용자 변경내역에는 명시되어 있으나,
현재 단계에서는 어느 payload가 해당 처형 보정을 담당하는지
독립적으로 확정하지 않았습니다.

기존 `ai_profile`의 포로 판정과 연합 상태를 연결했을 가능성이 있으므로
별도 분석이 필요합니다.

---

# 12. V2.0 message_profile 변화 - 중요

기존 Step 06의 `message_profile`은 단순 표시 branch 중심이었습니다.

V2.0의 `message_profile`은 크게 확장됐습니다.

V2.0 decompressed size:

```text
6,830 bytes
```

내부 설명:

```text
V2.0: AI ruler execution dialogue only, after native result summaries.
Snapshot lifetime is one disposition call; outcome lists remain authoritative.
```

따라서 기존 Step 06의 단순 등용/석방/복수처형 표시 패치와
동일한 구조로 보면 안 됩니다.

### 현재 판정

**V2.0 기준 재분석 필요**

특히 AI가 군주를 처형했을 때
native 결과 요약 뒤에 별도 대화를 붙이는 구조로 발전한 것으로 보입니다.

---

# 13. 기존 Viewer 분석 중 V2.0에서도 유지되는 큰 축

V2.0 `ai_profile`에서 기존 핵심 payload 이름들이 유지됩니다.

```text
build_attack_payload
build_prisoner_payload
REFUSAL_ATTACK
REFUSAL_PRE
REFUSAL_POST
HATRED_PRISON
```

따라서 다음 기존 분석은 기본 뼈대로 계속 참고할 수 있습니다.

- Step 03: 공격 후보 확장
- Step 04: 플레이어 우선 가산 제거
- Step 05: AI 포로 석방/처형 판정
- Step 08: 항복권고 실패/고의리 항복 억제

단 V2.0 `ai_profile` 자체가 변경됐으므로
주소/payload/hash는 다시 검증해야 합니다.

---

# 14. 전투 관련 이식 우선순위

사용자 요청 기준:

> 하나씩, 추가하기 쉬운 것부터, 특히 전투 관련 우선.

## 1순위 - 부장 포획

```text
deputy_patch / deputy_profile
```

이유:

- 전투 결과 체감이 직접적.
- 별도 독립 profile.
- “총대장 포획 성공 후 부장 추가”라는 조건이 명확.
- 기존 포획 판정을 대체하지 않고 후처리하는 구조.
- 기능 ON/OFF A/B 테스트가 쉬움.

### 권장 작업 방식

1. V2.0 `deputy_profile`의 정확한 HOOK_RVA/ORIGINAL/guard 추출
2. 현재 SAN8RPK.exe 동일 빌드 여부 확인
3. read-only 진단으로 총대장/부장 포인터 확인
4. 게임에서 총대장 포획 상황 재현
5. 부장 리스트 추가 write 적용

---

## 2순위 - 고립 도시 함락 전원 포로

```text
isolated_patch / isolated_profile
```

이유:

- 전투 결과에 직접 영향.
- 전략적 체감이 큼.
- 별도 독립 profile.
- 포로 리스트에 추가하는 목적이 명확.

주의:

- 도시 고립 판정
- 도시 소속 장수 열거
- 기존 native 포로 list

세 구조를 동시에 만지므로 부장 포획보다 복잡합니다.

---

## 3순위 - 포로 석방 관계 개선

```text
release_patch / release_profile
```

이유:

- 전투 직후 포로 처리 흐름.
- 별도 독립 profile.
- 기존 Step 05 AI 처형 판정과 기능이 분리됨.

주의:

- 관계도 write가 들어가기 때문에
  상성 계산과 관계 필드/함수 의미를 먼저 확인해야 합니다.

---

## 4순위 - AI 군주 처형 상세 대화

```text
message_profile
```

전투 결과 표시 계열이라 안전성은 상대적으로 높지만,
V2.0에서 profile이 크게 바뀌어 Step 06을 다시 해부해야 합니다.

게임 규칙 자체보다 표시 기능이라
실제 전투 밸런스 기능보다 우선순위는 낮게 둡니다.

---

## 5순위 - 연합군 vs 타깃 군주 포획 처형 보정

사용자 변경내역에는 존재하지만
현재 정확한 hook/payload가 확정되지 않았습니다.

따라서 바로 write하지 않고:

1. league profile
2. AI prisoner profile
3. 연합 상태 구조

를 연결하는 read-only 분석부터 진행합니다.

---

# 15. 전투 외 후순위

## 주인공 변경

기술적으로 비교적 독립적이지만 전투 관련이 아니므로 후순위.

## 이동 도로 제한

영향 범위가 플레이어/AI 인사 이동 전체라 회귀 위험이 큼.

## 주인공 소속도시 AI 개선

현재 AI 전투 개선과 충돌/중복 가능성이 높아 별도 A/B 분석 필요.

## AI 공백지 대기 1년

현재 공백지 패치와 의미 중복 여부를 먼저 확인해야 함.

## 결전/악적발호 1년 + 9개월

이미 프로젝트에 결전 주기 단축 기능이 있으므로
새 기능 추가보다 기존 기능 확장/정리 방식이 적합.

---

# 16. 추천 실제 구현 순서

```text
Step A  부장 포획
Step B  고립 도시 함락 전원 포로
Step C  포로 석방 관계 개선
Step D  AI 군주 처형 상세 표시
Step E  연합 전투 군주 처형 보정
Step F  고립 도시 이동 제한
Step G  주인공 소속도시 AI 개선
Step H  공백지 1년 gate
Step I  결전/악적발호 지속기간 통합
Step J  주인공 변경
```

전투 기능은 각각:

```text
정적 분석
  ↓
read-only 진단
  ↓
실게임 확인
  ↓
write 구현
  ↓
사용자 검증
  ↓
merge
```

순서로 진행합니다.

---

# 17. 다음 작업

가장 먼저:

> **V2.0 deputy_profile 상세 해부**

를 진행하는 것이 적합합니다.

확인할 항목:

- 정확한 HOOK_RVA
- 원본 16 bytes
- resume 주소
- payload에서 총대장/부장 구조를 읽는 방식
- 부장 최대 2명 구조
- “native commander capture succeeds”를 어떤 값으로 확인하는지
- 기존 포로 리스트에 부장을 어떤 형식으로 append하는지
- 현재 S8RPKCheats 포로 관련 hook과 byte overlap 여부

이 항목들이 정리되면 별도 기능 브랜치에서
`부장 포획` 체크박스로 구현합니다.

---

# 18. 현재 결론

V2.0은 기존 Viewer의 단순 버전업이 아니라
전투/포로/이동/연합 계열 기능이 상당히 확장된 빌드입니다.

특히 전투 관련 신규 기능 중에는:

```text
부장 포획
고립 도시 전원 포로
포로 석방 관계 개선
AI 군주 처형 상세 표시
```

가 각각 독립 모듈로 분리되어 있어
S8RPKCheats에 하나씩 단계적으로 이식하기 좋습니다.

첫 이식 후보는 **부장 포획**으로 잡습니다.
