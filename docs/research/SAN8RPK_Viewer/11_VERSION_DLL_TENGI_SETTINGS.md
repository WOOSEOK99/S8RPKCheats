# Step 11 - version.dll 전기 15종 / 수치 / 강제발동 구조

## 0. 분석 대상

업로드 파일:

- 파일명: `version.dll`
- 크기: 1,543,680 bytes
- SHA-256: `01bfb8941d58f231231244b5bd6b8073a97e9630b91ea2312db3b9c2bc982c0a`
- 형식: PE32+ x64 DLL
- 빌드 timestamp: 2026-06-12
- PDB 경로 문자열: `E:\SteamLibrary\steamapps\common\SAN8R\version.pdb`

실행하지 않고 정적 분석만 수행했습니다.

---

## 1. 전기 15종 테이블 - 확정

DLL 내부에는 15개 전기의 ID / 이름 / 기본 수치가 24-byte stride 테이블로 들어 있습니다.

테이블 내부 주소:

```text
version.dll + 0x12BA80 (VA 0x18012BA80 기준)
entry size = 0x18
entry count = 15
```

entry 구조:

```text
+0x00 DWORD  전기 ID
+0x04 padding
+0x08 QWORD  이름 문자열 포인터
+0x10 DWORD  기본 수치 (signed)
+0x14 padding
```

### 전체 목록

| 순번 | ID | 이름 | 기본 수치 | DLL 표시 방식 |
|---:|---:|---|---:|---|
| 1 | 1 | 결전 | 6 | 6개월 |
| 2 | 2 | 이민족습격 | 0 | 즉발 |
| 3 | 7 | 악적발호 | 6 | 6개월 |
| 4 | 8 | 의심암귀 | -30 | 효과 -30 |
| 5 | 9 | 민심혹란 | -50 | 효과 -50 |
| 6 | 10 | 붕벽 | 6 | 6개월 |
| 7 | 12 | 여세 | 6 | 6개월 |
| 8 | 14 | 피폐 | 6 | 6개월 |
| 9 | 16 | 권위고양 | 6 | 6개월 |
| 10 | 18 | 보장각성 | 6 | 6개월 |
| 11 | 19 | 기장각성 | 6 | 6개월 |
| 12 | 20 | 궁장각성 | 6 | 6개월 |
| 13 | 21 | 병격난무 | 6 | 6개월 |
| 14 | 22 | 전승기 | 6 | 6개월 |
| 15 | 23 | 중지성성 | 0 | 즉발 |

DLL의 표시 format도 직접 확인했습니다.

```text
양수: [%d] %s (%d개월)
음수: [%d] %s (효과 %d)
0   : [%d] %s (즉발)
```

따라서 사용자가 말한 “수치가 있는 전기”는 실제로 두 종류입니다.

- 기간형: 양수 개월
- 효과형: 음수 효과량

0은 별도 수치 수정 없이 즉발형입니다.

---

## 2. 강제발동 함수 입력 - 확정

DLL의 강제발동 함수는 개념적으로 다음 인자를 받습니다.

```text
ForceTengi(eventId, customValue)
```

UI에서는 ComboBox item data에:

```text
low 16-bit  = 전기 ID
high 16-bit = 기본 수치
```

를 packing해 둡니다.

선택 후:

- high word > 0이면 입력값을 1~120으로 clamp
- high word < 0이면 입력값을 -100~100으로 clamp
- high word == 0이면 customValue=0

으로 넘깁니다.

즉 DLL 자체 UI도 기간형/효과형/즉발형을 구분합니다.

---

## 3. 실제 게임의 전기 객체 획득 - 확정

게임 global manager:

```text
SAN8RPK.exe + 0x02E98BC8
```

에서 manager 포인터를 얻은 뒤 선택한 전기 ID로:

```text
eventPtr =
    *(manager + 0x58A670 + eventId * 8)
```

을 읽습니다.

즉 게임 내부에는 ID를 index로 하는 **전기 객체 포인터 테이블**이 있습니다.

예:

```text
ID 1  결전
ID 2  이민족습격
ID 7  악적발호
...
ID 23 중지성성
```

---

## 4. 전기 개별 수치 필드 - 확정

강제발동 시 customValue가 0이 아니면 DLL은:

```text
WORD [eventPtr + 0x12]
```

를 읽어 원래 값을 저장한 뒤 customValue를 임시로 씁니다.

즉:

```text
전기 객체 +0x12 = 강제발동 UI가 조절하는 16-bit signed 수치
```

입니다.

이 값의 의미는 테이블 기본값의 sign에 따라:

- 기간형 전기: 개월
- 효과형 전기: 효과량

으로 사용됩니다.

강제 전기가 종료되면 DLL은 저장해둔 원래 `+0x12` 값을 다시 복원합니다.

따라서 이 DLL은 원본 전기 definition을 영구 변경하지 않고 **발동 중에만 임시 override**합니다.

---

## 5. 전기 발동 조건 hook - 확정

DLL은 게임 코드:

```text
SAN8RPK.exe + 0x0132E340
```

의 16 bytes를 trampoline으로 교체합니다.

강제발동 상태에서 hook의 의미는 다음과 같습니다.

```text
selectedEventPtr == 0
    -> 원래 게임 판정 코드 실행

selectedEventPtr != 0
    -> 현재 검사 대상 RCX == selectedEventPtr 이면 true 반환
    -> 다른 전기이면 false 반환
```

개념 코드:

```cpp
if (!selectedEvent)
    return OriginalEventCheck(event);

if (event == selectedEvent)
    return true;

return false;
```

중요한 점:

> 강제발동 중에는 선택한 전기 하나만 허용하고 다른 전기 후보는 모두 false로 만듭니다.

따라서 이 hook은 “전기 후보/발동 가능 판정” 흐름이라는 강한 증거가 됩니다.

---

## 6. 게이지 강제 100 - 확정

강제발동 준비 시 DLL은 manager의:

```text
manager + 0x1EBDE0
```

byte를 100으로 만듭니다.

이는 현재 S8RPKCheats의 전기 게이지 100 처리와 같은 목적의 값으로 볼 수 있습니다.

DLL 상태 문자열도:

```text
전기 발동 준비: 슬롯 %d — 계절 전환 대기
```

라고 되어 있습니다.

즉:

1. 전기 후보를 하나로 제한
2. 게이지를 100
3. 다음 계절/평정 처리에서 게임 native 흐름으로 발동

시키는 구조입니다.

---

## 7. 발동/종료 감지 - 확정

DLL은:

```text
manager + 0x1EBDF8
```

의 상태값을 감시합니다.

상태 machine:

```text
준비 상태
  ↓
+1EBDF8 == 1
  ↓
전기 발동 완료 — 종료 대기 중
  ↓
+1EBDF8 == 0
  ↓
전기 종료
  ↓
eventPtr+0x12 원래 수치 복원
```

내부 문자열:

```text
전기 발동 완료 — 종료 대기 중
전기 종료 + 기간 복원
```

과 일치합니다.

---

## 8. 우리가 원하는 “게임 중 전기 ON/OFF”와의 관계

이번 DLL 분석으로 확정된 것은:

- 15개 전기의 ID
- 전기 객체 포인터 테이블
- 전기별 수치 `+0x12`
- 전기 후보 판정 hook
- 강제발동 방식

입니다.

하지만 **게임 시작 시 설정화면의 “사용 / 사용 안 함”을 저장하는 원본 메모리 위치는 이 DLL이 사용하지 않습니다.**

즉 version.dll만으로는:

```text
게임 설정에서 결전 ON
게임 설정에서 악적발호 OFF
...
```

를 어디에 저장하는지 직접 확정할 수 없습니다.

따라서 이 값을 추측해서 write하면 안 됩니다.

---

## 9. 중간 단계로 가능한 안전한 차단 방식

`+132E340` hook은 전기 후보를 확인하므로, 원래 판정을 실행한 뒤 **사용자가 OFF한 전기만 veto**하는 방식은 설계 가능합니다.

개념:

```cpp
bool result = OriginalEventCheck(event);

if (!result)
    return false;

if (IsCheatDisabledEvent(event))
    return false;

return true;
```

이 방식의 장점:

- 게임 원래 조건 유지
- 게임에서 허용된 전기를 치트에서 추가로 끌 수 있음
- 게임 도중 즉시 변경 가능

한계:

- 게임 시작 설정에서 이미 OFF인 전기를 치트 checkbox ON만으로 되살릴 수는 없음
- ON은 “게임 원래 설정/조건을 따르도록 허용”의 의미가 됨

완전한 양방향 ON/OFF를 하려면 원래 설정 저장 위치를 추가로 찾아야 합니다.

---

## 10. 완전한 ON/OFF를 찾기 위한 read-only 진단 계획

목표:

```text
게임 설정 ON/OFF 값의 실제 저장 위치 찾기
```

가장 안전한 방법은 같은 시나리오에서 설정 하나만 다르게 만든 두 상태를 비교하는 것입니다.

예:

### A 상태

```text
악적발호 ON
```

### B 상태

```text
악적발호 OFF
```

그리고 게임 시작 직후 read-only로:

- manager 주변 후보 설정 영역
- eventPtr 객체 주변 bytes
- event 관련 pointer가 참조하는 설정 데이터

를 비교합니다.

한 전기만 차이가 나는 offset을 찾은 뒤:

1. 다른 전기에서도 같은 패턴 확인
2. 실제 ON/OFF와 값 일치 확인
3. 3회 이상 재현

후에만 write 대상으로 확정합니다.

---

## 11. 팝업 UI 최종 후보

원본 ON/OFF 저장 위치까지 확정되면:

```text
[전기 설정]

☑ 결전              기간  6개월
☑ 이민족습격        즉발
☐ 악적발호          기간  6개월
☑ 의심암귀          효과 -30
☑ 민심혹란          효과 -50
☑ 붕벽              기간  6개월
...
☑ 중지성성          즉발

[결전 재발생 대기 1년]
[닫기]
```

처럼 만들 수 있습니다.

수치 UI는 타입에 따라:

- 기간형: 1~120개월
- 효과형: -100~100
- 즉발형: 숫자 없음

으로 제한할 근거도 이번 DLL에서 확보했습니다.

---

## 12. 결전 1년 기능과의 충돌 주의

Viewer Step 07의 두 번째 결전 hook:

```text
+132E4BD
```

은 이번 version.dll의 후보 판정 hook:

```text
+132E340
```

과 같은 게임 함수 영역에 있습니다.

Viewer는 `+132E340`부터 588 bytes를 guard hash로 검사합니다.

따라서 version.dll 방식의 hook이 먼저 설치되어 있다면 Viewer 원본 구현의 guard는 불일치할 수 있습니다.

S8RPKCheats에 둘 다 넣을 경우 외부 구현을 그대로 두 개 복사하지 않고 **우리 내부에서 충돌 검증 후 통합 관리**해야 합니다.

---

## 13. 현재 결론

이번 version.dll에서 전기 팝업 구현에 필요한 핵심 데이터 중:

### 확정 완료

- 전기 15종 ID/이름
- 기간형 / 효과형 / 즉발형 구분
- 기본값
- event pointer table
- event `+0x12` numeric field
- candidate check hook
- gauge/running state

### 아직 미확정

- 게임 시작 설정의 실제 전기 ON/OFF 저장 위치

따라서 다음 실제 코드 작업은:

> **ON/OFF 주소를 추측해서 쓰는 게 아니라 read-only 비교 진단부터 추가**

하는 것이 맞습니다.
