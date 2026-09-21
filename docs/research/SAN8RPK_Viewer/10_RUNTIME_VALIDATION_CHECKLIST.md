# Step 10 - 게임 런타임 검증 체크리스트

## 0. 목적

이 문서는 Step 03~09의 정적 분석을 실제 게임에서 검증하기 위한 실행 순서와 합격 기준입니다.

목표는 단순히 게임이 안 튕기는지 확인하는 것이 아닙니다.

각 기능에 대해 다음을 단계별로 확인합니다.

- hook 진입이 맞는지
- 레지스터/포인터 역할이 맞는지
- 계산값이 예상과 맞는지
- 실제 게임 결과가 계산과 일치하는지
- 기존 S8RPKCheats 기능과 충돌하지 않는지

---

# 1. 공통 원칙

## 1.1 한 번에 한 기능만

첫 검증에서는 여러 Viewer 기능을 동시에 켜지 않습니다.

권장 순서:

~~~text
기존 기능 OFF
실험 기능 1개 ON
로그 확인
게임 결과 확인
OFF 후 원복 확인
~~~

기능 하나가 확정된 뒤에만 조합 테스트로 넘어갑니다.

## 1.2 먼저 read-only, 그 다음 write

구조가 완전히 확정되지 않은 기능은 반드시 다음 순서를 따릅니다.

~~~text
hook 진입 관찰
→ 레지스터/포인터 로그
→ 값 범위 확인
→ 실제 결과와 대조
→ 그 뒤 write
~~~

특히 AI 포로 판정과 항복권고 실패 PRE/POST는 처음부터 write하지 않습니다.

## 1.3 같은 세이브로 A/B 비교

가능하면 같은 세이브, 같은 월/턴 직전, 같은 설정에서 기능 OFF/ON을 비교합니다.

AI 난수 때문에 완전히 동일한 결과가 보장되지 않더라도 hook 흐름과 행동 경향은 비교할 수 있습니다.

## 1.4 공통 기록 항목

테스트 시작 시 다음을 남깁니다.

~~~text
게임 시나리오:
게임 연/월:
난이도:
플레이어 세력:
테스트 AI 세력:
사용한 S8RPKCheats commit:
활성화된 기존 기능:
활성화된 실험 기능:
~~~

---

# 2. 패치 적용 전 공통 안전검사

- [ ] 게임 EXE base 확인
- [ ] 예상 원본 바이트 일치
- [ ] 이미 다른 패치가 들어간 상태인지 구분
- [ ] hook 범위와 다른 기능 hook 범위가 겹치지 않음
- [ ] cave 할당 성공
- [ ] cave 대상 주소 유효
- [ ] return 주소가 instruction boundary
- [ ] disable 시 원본 복구 가능
- [ ] 여러 주소를 한 기능으로 묶을 경우 전부 사전 검증
- [ ] 중간 실패 시 rollback

하나라도 실패하면 해당 기능 전체를 적용하지 않습니다.

---

# 3. 기능 1 - AI 포로 결과 메시지 상세화

관련 분석: Step 06

RVA:

~~~text
+1E599BE
+1E5A82C
+1E5BA1F
+1E5BA4B
~~~

이 기능은 가장 먼저 검증하기 좋은 저위험 기능입니다.

## 3.1 +1E599BE 단독

- [ ] 원본 74 75 확인
- [ ] 90 90 적용
- [ ] AI vs AI 전투 발생
- [ ] 새로 표시되는 결과 메시지 종류 기록
- [ ] OFF 후 원래 메시지 동작 복구

목표: 등용/석방 중 어느 쪽인지 확정.

## 3.2 +1E5A82C 단독

- [ ] 원본 75 59 확인
- [ ] 90 90 적용
- [ ] AI vs AI 전투 발생
- [ ] 새로 표시되는 결과 메시지 종류 기록
- [ ] OFF 후 원복

목표: 남은 등용/석방 매핑 확정.

## 3.3 복수 처형 pair

+1E5BA1F와 +1E5BA4B만 함께 적용합니다.

테스트:
- [ ] 1명 처형
- [ ] 2명 처형
- [ ] 3명 이상 처형
- [ ] 플레이어 참여 전투
- [ ] AI vs AI 전투

로그 권장:

~~~text
R14B
계산 EDX = 62 + 5*R14B
실제 표시 메시지
실제 처형자 수
~~~

합격 기준:
- 실제 처형자 명단이 누락 없이 표시
- 1명 케이스가 바닐라 메시지와 깨지지 않음
- 플레이어 전투 메시지 회귀 없음

---

# 4. 기능 2 - 결전 재시도 1년

관련 분석: Step 07

Hook:

~~~text
+1335961
+132E4BD
~~~

## 4.1 먼저 read-only 진단

두 hook에서 다음을 기록합니다.

~~~text
R9
byte [r9+08]
byte [r9+35]
byte [r9+38]
현재 연/월
hook A/B 구분
~~~

확인 목표:
- [ ] +08 == 1 record가 실제 결전 관련 record인지
- [ ] +35 값이 재시도 간격과 일치하는지
- [ ] +38 값이 1~12 범위인지
- [ ] hook A와 B가 언제 각각 호출되는지

## 4.2 write 적용

Viewer 방식처럼 메모리 +35는 쓰지 않고 r10d 값만 1로 override합니다.

검증:
- [ ] 첫 결전 발생시기 기록
- [ ] 첫 결전 종료 시점 기록
- [ ] 다음 결전 후보 시점 기록
- [ ] 두 번째 결전까지 걸린 개월 수 기록
- [ ] 다른 전기 발생 조건 영향 여부 확인

합격 기준:
- 결전 이외 전기 조건이 강제로 true 되지 않음
- 첫 결전은 native 조건을 따름
- 재시도 간격만 짧아지는 양상이 확인됨

---

# 5. 기능 3 - 고의리 군주 항복 억제

관련 분석: Step 08

Hook: +193C86E

## 5.1 read-only 로그

~~~text
대상 force ID
대상 군주 ID/이름
군주 의리 H
XMM6 before
player-associated force 포인터
r13
rdx
~~~

확인 목표:
- [ ] rdx+0xC0가 실제 대상 군주
- [ ] +0x5E & 0xF가 UI 의리값과 일치
- [ ] 플레이어 관련 force에서는 보정 경로가 skip됨

## 5.2 공식 검증

~~~text
H < 11:
  변화 없음

H >= 11:
  after = max(0, min(before,100) - 20*(H-10))
~~~

테스트 샘플:
- [ ] H=10
- [ ] H=11
- [ ] H=12
- [ ] H=13
- [ ] H=14
- [ ] H=15

합격 기준:
- hook 계산값과 실제 항복 결과 흐름이 모순되지 않음
- 플레이어 직접 항복 이벤트에 부작용 없음
- UI/이벤트 메시지 이상 없음

---

# 6. 기능 4 - 플레이어 우선 가산 제거

관련 분석: Step 04

Viewer:

~~~text
A = +144BB60
B = +144BC8B
~~~

현재 S8RPKCheats:

~~~text
C = +1464B91
~~~

## 6.1 테스트 시나리오

한 AI 세력 주변에:
- 플레이어 세력
- 플레이어보다 약한 AI 세력
- 가능하면 전력이 비슷한 제3 AI 세력

을 둡니다.

AI 군주 호전도:
- [ ] 보통
- [ ] 호전
- [ ] 극호전

## 6.2 조합표

~~~text
A OFF / B OFF / C OFF
A ON  / B OFF / C OFF
A OFF / B ON  / C OFF
A ON  / B ON  / C OFF
A OFF / B OFF / C ON
A ON  / B ON  / C ON
~~~

기록:

~~~text
AI 목표
플레이어 선택 여부
약한 AI 선택 여부
전쟁 선언/출병 결과
해당 월 반복 횟수
~~~

목표:
- 어느 주소가 호전/극호전 가산에 대응하는지
- 현재 C와 Viewer A/B 효과가 같은지
- A+B+C가 과보정인지

---

# 7. 기능 5 - AI 공격 후보 확장

관련 분석: Step 03

Viewer hook: +144D248
현재 patch: +144D24C

## 절대 조건

둘을 동시에 켜지 않습니다.

Viewer hook 범위 안에 현재 +144D24C 패치가 포함됩니다.

## 7.1 진단 항목

공격 후보마다:

~~~text
후보 force/city ID
기존 목표 A 일치 여부
기존 목표 B 일치 여부
relation helper 결과
base score
refusal boost 여부
1.1 target bonus 여부
final score
현재 best score
후보 갱신 여부
~~~

stats:

~~~text
examined
outside
selected
friendly
boosted
~~~

## 7.2 핵심 검증

- [ ] 기존 목표 A/B 밖 후보에서 outside 증가
- [ ] outside 후보도 score 함수 호출
- [ ] 기존 목표 A score가 ×1.1
- [ ] final score가 best보다 높을 때 r15 후보 갱신
- [ ] 관계 필터 대상은 선택되지 않음

## 7.3 실제 플레이 비교

같은 세이브에서:

~~~text
바닐라
현재 +144D24C 방식
Viewer +144D248 방식
~~~

을 비교합니다.

대표 시나리오:
- 조조 / 여포 / 공융·장양 인접 상황
- 손책 / 유표 / 유요 인접 상황
- 목표세력보다 약한 제3세력이 존재하는 상황

합격 기준:
- 기존 목표 이외 세력도 실제 공격 후보에 진입
- 기존 목표가 완전히 무시되지 않음
- AI가 이상한 우호/동맹 대상을 공격하지 않음
- 프리징/무한루프 없음

---

# 8. 기능 6 - AI 포로 처형 공식

관련 분석: Step 05

Hook:

~~~text
+1E52A8D
+1E54190
~~~

이 기능은 read-only 진단을 충분히 한 뒤에만 write합니다.

## 8.1 첫 진단에서 반드시 찍을 값

~~~text
RDI pointer
RSI pointer
WORD [rdi+8]
WORD [rsi+8]
이 두 ID의 실제 장수 이름
[rdi+5D]
[rsi+5D]
[rsi+5E] & 0xF
[rsi+30]
rdi status pointer / status id
rdi force pointer
force+0xC0 ruler pointer
incoming ECX
~~~

목표:
- [ ] RDI가 포로인지
- [ ] RSI가 처분 주체인지
- [ ] ECX 값 범위가 무엇인지

를 실제 게임에서 확정합니다.

## 8.2 계산 로그

각 포로마다:

~~~text
compat A
compat B
distance d
honor H
ruler flag R
+30 flag F
T before relation helper
relation helper result
T after relation helper
incoming ECX
predicted result
actual result
~~~

## 8.3 공식 대조

기본:

~~~text
d = min(abs(A-B),150-abs(A-B))
T = 100-d
~~~

그 뒤 Step 05의 의리/군주/관계 보정을 적용합니다.

테스트 케이스:
- [ ] 상성이 매우 가까운 포로
- [ ] 상성이 매우 먼 포로
- [ ] 의리 낮음
- [ ] 의리 11 이상
- [ ] 일반 장수 포로
- [ ] 군주 포로
- [ ] 관계 helper true
- [ ] 관계 helper false

## 8.4 기존 도독 기능과 조합

Viewer AI 포로 기능 단독 검증 후:
- [ ] 도독 포로 직접 처분 OFF
- [ ] 도독 포로 직접 처분 ON

두 경우를 비교합니다.

합격 기준:
AI 자동 처분과 플레이어 도독 직접 처분 흐름이 서로 침범하지 않아야 합니다.

---

# 9. 기능 7 - 항복권고 실패 후 공격 우선

관련 분석: Step 08

이 기능은 가장 마지막에 검증합니다.

이유:
- PRE hook
- POST hook
- 3개월 record
- attack cave
- native 전환 call
- Viewer 원본 hook overlap 위험

## 9.1 +1451ED9 jump는 그대로 쓰지 않음

확정된 위험:

~~~text
POST hook range:
+1451ECD ~ +1451EDC

PRE active-record skip:
+1451ED9
~~~

skip 주소가 POST trampoline 내부이므로 그대로 사용하지 않습니다.

## 9.2 Phase A - POST read-only

기록:

~~~text
r8
r9
r14
r15
manager
player-associated force
relation helper result
+125AA0 return
현재 year/month
~~~

확인 목표:
- [ ] 실제 항복권고 실패 시 POST hook 진입
- [ ] r8/r9가 어느 세력 pair인지
- [ ] player-associated exclusion 의미
- [ ] +125AA0 반환 범위

아직 record write 금지.

## 9.3 Phase B - record만 만들기

~~~text
record = sharedState + 0x100 + id*0x20

+00 r8
+08 r9
+10 manager
+18 start monthIndex
+1C end monthIndex
~~~

기간:

~~~text
end = start + 3
~~~

검증:
- [ ] 실패 직후 record 생성
- [ ] 1개월 후 유효
- [ ] 2개월 후 유효
- [ ] 3개월 경계에서 만료
- [ ] 다른 세력 ID record와 충돌하지 않음

## 9.4 Phase C - 재권고 차단

안전 재설계 후보:

~~~asm
stats.skipped++
mov rbx,[r14+10h]
jmp +1451EDD
~~~

검증:
- [ ] record 활성 중 동일 pair 재권고 안 함
- [ ] record 만료 후 다시 권고 가능
- [ ] 다른 세력에는 권고 가능
- [ ] stack/register 깨짐 없음

## 9.5 Phase D - attack score ×5

공격 후보 계산에서:

~~~text
record pair match
기간 유효
base score >= 1.2
~~~

일 때만:

~~~text
score *= 5.0
~~~

로그:

~~~text
base score
record match
date match
boost eligibility
score after ×5
target bonus ×1.1
final score
best candidate
~~~

합격 기준:
- 공격 강제가 아니라 후보 score 상승으로 동작
- 조건이 안 맞으면 원래 score 유지
- 3개월 후 boost 사라짐

---

# 10. 회귀 테스트

각 새 기능이 단독으로 통과한 뒤 공통 회귀 테스트를 합니다.

## AI 전쟁

- [ ] AI가 공백지를 정상 점령
- [ ] AI가 우호/동맹 세력을 비정상 공격하지 않음
- [ ] AI 출병이 멈추지 않음
- [ ] 월 넘김 프리징 없음
- [ ] 10년 이상 데모 진행 가능

## 포로

- [ ] 플레이어 전투 포로 처리 정상
- [ ] AI vs AI 포로 처리 정상
- [ ] 도독 직접 처분 정상
- [ ] 처형/석방 메시지 정상

## 전기/결전

- [ ] 일반 전기 발생 정상
- [ ] 결전 이외 전기가 과도하게 증가하지 않음
- [ ] 전기 취소/발생 플래그 기능 정상
- [ ] 첫 결전/후속 결전 모두 진행 가능

## 항복

- [ ] 플레이어 항복권고 정상
- [ ] AI끼리 항복권고 정상
- [ ] 고의리 군주 보정 적용
- [ ] 항복 실패 후 다른 AI 행동이 멈추지 않음

---

# 11. 장기 데모 테스트

권장 시나리오:

~~~text
영웅집결
군웅할거
~~~

기록 시점:

~~~text
5년
10년
20년
30년
~~~

각 시점에서:
- 생존 세력 수
- 최대 세력 도시 수
- 전쟁 빈도
- 결전 횟수
- AI 포로 처형 수
- 항복 횟수
- 게임 정지/프리징 여부

를 기록합니다.

목표는 Viewer 게시글의 체감 수치를 그대로 재현하는 것이 아니라, 우리 구현이 안정적으로 AI 확장 행동을 바꾸는지 확인하는 것입니다.

---

# 12. 확정 판정 기준

정적 분석에서 유력 또는 미확정으로 남긴 항목은 다음 조건을 만족해야 확정으로 승격합니다.

### 구조/포인터
실제 게임 ID/이름과 3회 이상 일치.

### 계산식
로그 입력값으로 계산한 예상값과 hook 내부 실제값 일치.

### 결과
예상 분기와 실제 게임 결과가 반복해서 일치.

### 주소 역할
기능 ON/OFF A/B 결과가 해당 설명과 일치.

한 번 우연히 맞은 결과는 확정으로 취급하지 않습니다.

---

# 13. 실패 유형별 해석

## 원본 바이트 불일치

가능성:
- 게임 빌드 차이
- 이미 다른 패치 적용
- 주소 오판

조치: write 금지.

## hook은 타는데 예상 포인터가 아님

가능성:
- 레지스터 역할 해석 오류
- 같은 함수의 다른 호출 맥락

조치: read-only 로그를 더 모으고 의미 재분석.

## 계산값은 맞는데 결과가 다름

가능성:
- 이후 native 조건 존재
- 해당 값이 최종 확률이 아니라 중간 score
- 다른 후속 override 존재

조치: 공식을 바로 폐기하지 말고 후속 제어 흐름 추적.

## 프리징/무한루프

가능성:
- 잘못된 resume
- instruction boundary 오류
- register/stack 보존 실패
- hook끼리 overlap
- native helper 호출 ABI 오류

조치: 즉시 기능 OFF/원복 후 해당 hook 단독 진단.

---

# 14. 테스트 기록 템플릿

각 테스트는 다음 형식으로 남깁니다.

~~~text
[Test ID]
기능:
날짜:
S8RPKCheats commit:
게임 시나리오:
게임 연/월:
세이브 기준점:

활성 기능:
비활성 기능:

Hook:
원본 바이트:
적용 바이트:

입력/레지스터:
- ...

계산:
- ...

예상 결과:
실제 결과:

PASS / FAIL / 보류

비고:
~~~

별도 raw 템플릿:
raw/10_RUNTIME_TEST_RECORD_TEMPLATE.txt

---

# 15. 실제 구현 시작 순서

1. AI 포로 결과 메시지 상세화
2. 결전 재시도 1년
3. 고의리 군주 항복 억제
4. 플레이어 우선 가산 제거 A/B
5. AI 공격 후보 확장
6. AI 포로 판정
7. 항복권고 실패 후 공격 우선

각 단계는 별도 브랜치 → 빌드 → 게임 확인 → 확정 순서로 진행합니다.

---

# 16. Step 10 결론

Step 00~09의 정적 분석으로 Viewer의 주요 기능 구조는 충분히 분리됐습니다.

이제부터 중요한 것은 추가 추측보다 실제 게임에서 하나씩 확정하는 것입니다.

특히:
- 메시지 상세화는 바로 독립 테스트 가능
- 결전 1년은 read-only 필드 확인 후 적용
- 고의리 항복 억제는 군주/의리/XMM6 로그 후 적용
- 공격 후보 확장은 기존 +144D24C를 끄고 비교
- AI 포로 판정은 RDI/RSI/ECX 의미를 먼저 확정
- 항복권고 PRE/POST는 Viewer overlap을 그대로 복제하지 않음

을 기본 원칙으로 삼습니다.

이 문서를 이후 기능별 실제 구현 작업의 검증 기준표로 사용합니다.
