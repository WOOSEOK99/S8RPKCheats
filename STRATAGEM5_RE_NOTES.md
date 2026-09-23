# 5번째 책략 역분석 기록

> 이 문서는 5번째 책략(ID5) 추가 실험의 **저장소 기준점(source of truth)** 이다.
> 새 채팅/세션에서는 EXE/PDB 재업로드를 요청하기 전에 반드시 이 문서와
> `Internal DX11 Base/Cheats/War/StratagemSlotProbe.cpp`,
> `Internal DX11 Base/Cheats/War/Spell5HealProbe.cpp`를 먼저 확인한다.
>
> 바이너리를 다시 요청하는 경우는 **게임 빌드가 바뀌었거나**, 이 문서/코드에
> 아직 추출되지 않은 새 PDB 심볼이 반드시 필요한 경우로 제한한다.

## 1. 분석 대상 빌드 식별

2026-09-23 업로드본 기준.

### SAN8RPK.exe

- 파일 크기: `57,585,488 bytes`
- SHA-256: `000d450345728c23bebca581ab37b8bcf25266172767b5e6defb10a72a8c9457`
- PE Machine: `0x8664` (x64)
- PE timestamp: `0x69A67ED1`
- CodeView signature: `RSDS`
- PDB GUID: `B18A027E-19F4-4C35-8749-5B6F0FF814D8`
- PDB age: `1`
- embedded PDB path:
  `C:\\project\\branch\\pk_113\\Exe\\WIN_JP\\Game\\SAN8RPK.pdb`

### SAN8RPK.pdb

- 파일 크기: `257,314,816 bytes`
- SHA-256: `a166db2b4bbbccf16ceb09413d491d2b460e066a373c82a7e913149e1c21f56e`
- PDB 내부에서 위 GUID의 little-endian 바이트가 확인됨.
- GUID 직전 age 값도 `1`로 확인됨.
- 따라서 이 EXE와 PDB는 같은 빌드의 심볼 세트로 취급한다.

### 중요

아래의 **RVA 값은 이 빌드에 종속**된다.
향후 게임 업데이트가 있으면 먼저 EXE SHA-256/RSDS GUID/age를 비교한다.
빌드가 다르면 RVA를 그대로 재사용하지 않는다.

---

## 2. 최종 목표

기존 책략:

1. 의기저상
2. 신산화계
3. 초목개병
4. 사모위계

를 그대로 유지하면서 ID5를 실제 게임 내부 구조에 추가하여
플레이어와 AI 모두가 사용할 수 있게 만드는 것.

4번을 5번으로 치환하는 방식은 최종안으로 사용하지 않는다.

현재 ID5 실험 효과:

- 아군
- 범위 5
- 사기 +40
- 실제 영향 대상 병력 +2000
- 최대병력 상한까지 회복

---

## 3. TrickData 정의 구조

canonical definition table:

- stride = `0x20`
- ID1~ID10 행 존재

known payload fields:

- `+00 code1`
- `+02 code2`
- `+04 code3`
- `+06 target`
- `+08 effect1`
- `+0A power1`
- `+0C duration1`
- `+0E effect2`
- `+10 power2`
- `+12 duration2`
- `+14 range`
- `+16 unknown16`

PDB 기준 실제 `san8r::TrickData` object:

- object stride = `0x20`
- `+0x00` = 8-byte vptr
- payload = `+0x08 ~ +0x1F`

따라서 Spell5 쪽에서 보이는 payload 주소와 native TrickData object 주소는
정상적으로 **8 bytes 차이**가 날 수 있다.

관련 수정 commit:

- `87a25cc639847e5d07f2b991cdcff8b0db51aa39`

---

## 4. 전투 책략 횟수 구조

전투 side object에서 실게임 확인:

- `+0x10C` = ID1 횟수
- `+0x11C` = ID2 횟수
- `+0x12C` = ID3 횟수
- `+0x13C` = ID4 횟수
- `+0x14C` = ID5 횟수 후보 — 실제 쓰기 성공

현재 게임 파일에서는 기본 횟수가:

- ID1 = 1
- ID2 = 1
- ID3 = 1
- ID4 = 1
- ID5 = 0

이므로 플레이어측 후보 가드는 현재 `1/1/1/1 + ID5=0`을 사용한다.

관련 commit:

- `1467a9a16952165e4ceed00b2c1d974c31eefee7`

---

## 5. 전투 런타임 5칸 구조 — 실게임 확정

PDB 구조:

```text
san8r::war::Camp::Impl
  +0x10 m_pCampData
  +0x18 m_type

san8r::war::CampData
  +0x68 m_pTricks
```

`m_pTricks` 타입:

```text
std::array<const san8r::TrickData*, 5>
```

AI 관련 PDB 타입에도:

```text
std::array<san8r::war::unit_ai::Trick::Score::TrickCache, 5>
```

가 존재한다.

### 2026-09-23 실게임 확인

실제 로그에서:

```text
tricks[0..4] =
row1,row2,row3,row4,0

native rows =
row1,row2,row3,row4,row5
```

가 정확히 확인되었고,

```text
5슬롯 연결 성공
old=0
new=row5
```

까지 성공했다.

즉 다음은 확정:

- `Camp::Impl +0x10 -> CampData`
- `CampData +0x68 -> m_pTricks[5]`
- 첫 4칸은 native TrickData row1~row4
- 5번째는 기본 null
- 5번째에 native row5 연결 가능
- ID5 횟수 `+0x14C = 1`도 별도로 성공

관련 commit:

- `fb458126fa907b4706f5e1e1b0a9c7ce6fc71402`

---

## 6. UI 구조 — 준비 UI와 실전 UI를 구분할 것

이 부분은 새 채팅에서 혼동하지 말 것.

### A. 실전 전투 책략 창

핵심 클래스:

```text
san8r::war::ui::TrickCommandDialogLayout
```

PDB에서 확인:

- object size = `0x2A8`
- `+0x1E0` = `std::array<TrickSelectButton*, 4> m_pButtons`
- `+0x200`부터 다음 멤버가 시작함
- `Initialize`의 local `ButtonLayouts`도 `UIMaker::SLayout[4]` (0x40 bytes)

따라서:

**`m_pButtons[4]` 뒤에 단순히 5번째 포인터를 쓰면 안 된다.**
`+0x200`은 이미 다음 멤버 영역이다.

즉 일반적인 `cmp 4 -> 5` 한 바이트 수정만으로는 안전하게 확장할 수 없다.

### B. 전투 준비/군의 화면

관련 클래스:

```text
WarMeetingStrategyTrickLayout
```

여기서 확인된 데이터는 실전 버튼 배열과 별도이며,
`m_aData`가 **StrategyTrickData[10]** 구조다.

따라서 과거의
“WarMeetingStrategyTrickLayout의 4 제한 = 실전 전투 책략 UI 4 제한”
이라는 식의 단순 연결은 하지 않는다.

---

## 7. PDB에서 추출한 실전 책략 UI 함수 RVA

**빌드 지문이 1절과 일치할 때만 사용.**

| 함수 | RVA | 확인용 코드 크기 |
|---|---:|---:|
| `TrickCommandDialogLayout::GetTrickButton` | `0x01DAF060` | `0x13` |
| `TrickCommandDialogLayout::ResetBtnPos` | `0x01DAE9F0` | `0x20D` |
| `TrickCommandDialogLayout::AddControl` | `0x01DAF300` | `0x3E` |
| `TrickCommandDialogLayout::DelControl` | `0x01DAF2B0` | `0x41` |
| `TrickCommandDialogLayout::Initialize` | `0x01DAF350` | `0x1019` |
| `TrickCommandDialog::Open` | `0x01DF3CB0` | `0x241` |
| `TrickCommandDialog::Initialize` | `0x01DF3F20` | `0x4C9` |
| `TrickCommandDialog::OnTrickSelect` | `0x01DF37B0` | `0x4C` |

현재 코드의 `DumpTrickUiPdbProbe()`가 위 RVA를 런타임에서 읽어서:

- `cmp 3/4/5`
- `+0x1E0` 버튼 배열 참조
- `GetTrickButton` direct call

을 좁게 로그한다.

관련 commit:

- `e7baaa6b4e591855d561338ed2e23033908f2a9b`

### 정적 EXE 분석 주의

이 빌드의 EXE 파일에서 PDB RVA 위치를 정적 파일 바이트로 바로 해석했을 때
정상적인 함수 명령열로 신뢰할 수 없었다.

따라서 현재 방식은:

1. PDB에서 클래스/필드/RVA를 확보
2. 게임 실행 중 `exeBase + RVA`의 실제 메모리 코드를 읽음
3. 좁은 범위만 진단
4. 실제 로그로 검증 후 패치

순서를 사용한다.

---

## 8. 현재 ID5 데이터

현재 `SetSpell5HealProbe(true)`는 canonical ID5 row만 수정하며
4번 사모위계는 유지한다.

source:

- ID2 신산화계 clone

ID5:

- code1 = 5
- code2 = 5
- code3 = 5
- target = 1
- effect1 = 10
- power1 = 40
- duration1 = 0
- effect2 = 0
- power2 = 0
- duration2 = 0
- range = 5

병력 회복은 effect20을 쓰지 않는다.

현재 방식:

- morale +40 변화 또는 100 상한 도달을 감지
- 해당 전투부대 current troops +2000
- max troops 상한 적용

한계:

- polling heuristic
- 이미 morale=100인 대상은 현재 검출하지 못할 수 있음

---

## 9. 절대 반복하지 않을 실패 실험

### 탈락 — effect1 = 20

- 책략 선택 메뉴 프리징
- 재사용 금지

### 탈락 — effect2 = 20

- 병력 회복 안 됨

### 탈락 — 후보 #34 `cmp 4 -> 5`

- 패치 자체는 적용됐지만 UI는 여전히 4개
- 실제 UI 제한이 아님
- 다시 사용하지 말 것

### 탈락 — guessed object에 `CampData::GetTricks()` 직접 호출

- 프리징
- 다시 하지 말 것

### 탈락 — 광범위 writable/object/VirtualQuery 반복 스캔

- 프리징
- 다시 하지 말 것

현재 원칙:

- 정확한 PDB field/RVA 우선
- read-only 진단 우선
- 실게임 로그 확인 후에만 write
- 첫 4칸/포인터 등 강한 가드가 맞지 않으면 쓰지 않음

---

## 10. 컴파일/런타임 이력

MSVC `C2712`:

- destructor/unwind가 필요한 함수 안에서 `__try` 사용으로 발생
- SEH를 별도 static helper로 분리해서 해결

관련 commit:

- `9e2d9b060c8b47af1d232331ab30fc6801267935`

runtime freeze 대응:

- `03edd67f07761a40a13d1eae957735e81a501aa8`
- `f8e85a580a39cef3835cd635b740f29df5234ea8`

---

## 11. 현재 코드 위치

- `Internal DX11 Base/Cheats/War/StratagemSlotProbe.cpp`
- `Internal DX11 Base/Cheats/War/StratagemSlotProbe.h`
- `Internal DX11 Base/Cheats/War/Spell5HealProbe.cpp`
- `Internal DX11 Base/Cheats/War/Spell5HealProbe.h`
- `Internal DX11 Base/MenuSections.cpp`
- `Internal DX11 Base/BattleMonitor.cpp`

현재 작업 브랜치:

- `feature/stratagem-5slot-probe`

이 브랜치는 사용자 실게임 확인 전 main에 머지하지 않는다.

---

## 12. 현재 다음 액션

현재 코드에서는 `5번 책략 내부등록` 성공 직후 자동으로
`[책략5UIDBG]` 진단이 실행된다.

테스트 순서:

1. `5번 책략 데이터`
2. `5번 책략 횟수 1`
3. `5번 책략 내부등록`
4. `[책략5UIDBG]` 로그 전체 확인

다음 판단 목표:

- 실전 UI의 실제 4회 루프 위치
- `m_pButtons[4]` 접근 방식
- 버튼 생성/등록/삭제/재배치 흐름
- 5번째 버튼을 위해 객체 내부 배열을 확장해야 하는지
- 별도 외부 저장소 + 함수 훅으로 5번째 포인터를 우회 관리할 수 있는지

**중요:** `+0x200`에 5번째 포인터를 직접 쓰는 방식은 금지.

---

## 13. 새 채팅 인수인계 규칙

새 세션에서 이 작업을 이어갈 때:

1. 최신 GitHub `feature/stratagem-5slot-probe`를 먼저 확인한다.
2. 이 `STRATAGEM5_RE_NOTES.md`를 읽는다.
3. `StratagemSlotProbe.cpp`의 현재 진단 코드를 확인한다.
4. 위에서 이미 확정/탈락된 내용을 다시 처음부터 분석하지 않는다.
5. EXE/PDB가 대화에 없어도 **기존에 문서화된 구조/RVA를 다시 물어보지 않는다.**
6. 새 PDB 심볼이 정말 필요할 때만 바이너리 재업로드가 필요하다고 말한다.
7. 게임 업데이트가 의심되면 먼저 빌드 지문(SHA-256/RSDS GUID/age) 비교부터 한다.


---

## 14. 2026-09-23 1차 실전 UI 코드 로그 분석

실게임 런타임 코드에서 다음이 직접 확인됨.

### TrickCommandDialogLayout::GetTrickButton

RVA `0x01DAF060`

```asm
cmp edx, 4
jae out_of_range
mov eax, edx
mov rax, [rcx + rax*8 + 0x1E0]
ret
```

즉 index 0~3만 허용하고, 실제 포인터 배열은 `this+0x1E0`에 존재한다.

### TrickCommandDialog::Open

RVA `0x01DF3CB0` 내부에서:

```asm
cmp edi, 4
jae ...
...
mov rsi, [rax + rsi*8 + 0x1E0]
```

직접 배열 접근과 4개 제한이 함께 존재한다.

또한 현재 선택 index 계열 값에도:

```asm
cmp eax, 4
jb ...
```

가 존재한다.

### TrickCommandDialog::Initialize

RVA `0x01DF3F20` 내부에서 2개의 4 제한이 확인됨.

1)

```asm
cmp r12d, 4
jae ...
```

2)

```asm
inc edi
add r14, 8
cmp edi, 4
jb <loop>
```

따라서 초기화에도 실제 4회 루프가 있다.

### TrickCommandDialogLayout::Initialize

RVA `0x01DAF350` 내부에서:

- `lea rdi, [rsi+0x1E0]`
- local count = `4`
- `this+0x1E0` 참조가 여러 곳 존재

가 확인됨.

### 결론

실전 UI의 4 제한은 단일 지점이 아니다.

현재 확인된 제한/직접 접근:

- `Layout::GetTrickButton`
- `Dialog::Open`
- `Dialog::Initialize`
- `Layout::Initialize`
- `Layout::ResetBtnPos`

따라서 단순 `4 -> 5` 전역 패치는 금지.

특히 `m_pButtons`는 `+0x1E0 ~ +0x1FF`의 고정 4포인터 배열이며
`+0x200`부터 다른 멤버가 시작하므로 5번째 포인터를 연속 저장할 수 없다.

현재 유력 방향:

- 기존 4개 `m_pButtons`는 그대로 유지
- 5번째 `TrickSelectButton*`만 외부 sidecar 저장소에 보관
- `GetTrickButton(index==4)` 등 접근 함수를 후킹해서 sidecar 반환
- `Open / Initialize / ResetBtnPos`의 직접 배열 접근은 필요한 곳만 별도 우회
- 실제 버튼 객체 생성 방식은 추가 진단 후 확정

2026-09-23 추가 진단 commit:

- `6aa25fb3e6b7585d8f6fae57fdd6310f680cad49`

이 커밋은 쓰기 패치가 아니라, 위 함수의 실제 버튼 생성/배치 흐름을 보기 위해
좁은 범위의 런타임 코드 바이트를 `[책략5UIRANGE]`로 추가 출력한다.


---

## 15. 2026-09-23 2차 실전 UI 상세 로그 분석

2차 `[책략5UIRANGE]` 로그와 같은 빌드의 PDB 심볼을 함께 대조한 결과.

### Layout::ResetBtnPos — 4개 재배치 루프 확정

RVA `0x01DAE9F0` 후반:

```asm
mov ebp, 4
add rsi, 0x1E0

loop:
  mov rcx, [rsi]
  test rcx, rcx
  je   next
  mov rax, [rcx]
  mov r8d, r14d
  mov edx, edi
  call qword ptr [rax+0x90]

next:
  add edi, ebx
  add rsi, 8
  sub rbp, 1
  jne loop
```

즉 기존 네 버튼을 `this+0x1E0`부터 순차 읽고,
각 버튼의 virtual `+0x90`에 계산된 위치를 전달한다.

### Layout::Initialize — 버튼 실제 생성 루프 확정

핵심 흐름:

```asm
lea rdi, [rsi+0x1E0]       ; m_pButtons 시작
mov qword ptr [rbp-0x40],4 ; 반복 횟수
...
; 0x1D8 bytes 객체 할당
...
call TrickSelectButton::TrickSelectButton
mov [rdi], rax
...
call TrickSelectButton::Initialize
...
call CUIMaker::RegisterLayout
...
add rdi,8
add r13,4
add r12,0x10
sub qword ptr [rbp-0x40],1
jne <button loop>
```

PDB/심볼로 확인한 직접 대상:

- `TrickSelectButton::TrickSelectButton(eTYPE)` RVA `0x01E7FEC0`
- `TrickSelectButton::Initialize(int,int,ICmnControl*)` RVA `0x01E7FF40`
- `TrickSelectButton::SetTrickID(TRICK_ID)` RVA `0x01E7FD90`
- `CUIMaker::RegisterLayout` RVA `0x01D16AA0`

버튼 객체 할당 크기:

- `0x1D8`

생성 루프가 참조하는 정적 4개 dword 테이블:

- RVA `0x0270D198`
- raw values = `2,3,4,5`

현재 이 네 값의 정확한 enum 의미는 아직 단정하지 않는다.
다음 진단에서 생성자와 Initialize 인자 구성 코드와 함께 확정한다.

### Dialog::Open — 데이터 루프는 5까지 돌 수 있으나 UI만 4에서 차단

실제 루프 종료 조건은 전투 데이터 쪽 count 값과 비교한다.

반면 각 반복에서:

```asm
cmp edi,4
jae skip_button_logic
mov rax,[dialog+layout]
mov rsi,[rax+rsi*8+0x1E0]
...
call TrickSelectButton::SetTrickID
...
```

형태로 index 4 이상을 UI 처리에서 제외한다.

따라서 **전투 데이터가 5개인 것 자체가 Open 루프를 막는 것은 아니다.**
5번째 반복도 존재할 수 있지만 현재는 UI 버튼 로직만 통째로 건너뛴다.

### Dialog::Initialize — 버튼별 시그널 연결도 4회

`m_pButtons[index]`를 가져온 뒤 각 버튼에 세 종류의 callback을 연결하고:

- focus 계열
- kill-focus 계열
- select 계열

마지막에:

```asm
inc edi
add r14,8
cmp edi,4
jb <loop>
```

로 끝난다.

callback 등록 대상 함수는 PDB 기준:

- `CSignalComponent::AddSig` RVA `0x01EDDC90`

따라서 5번째 버튼을 단순히 생성하는 것만으로는 부족하고,
5번 index를 캡처하는 동일한 callback 연결도 필요하다.

### 현재 설계 판단

기존 객체 내부 `m_pButtons[4]`는 확장할 수 없으므로 sidecar 방향은 유지한다.

필요 구성:

1. 5번째 `TrickSelectButton` 객체를 별도 생성
2. sidecar에 포인터 보관
3. `GetTrickButton(4)`는 sidecar 반환
4. `Dialog::Open`의 index 4도 sidecar를 사용
5. `Dialog::Initialize`에서 5번용 callback 3종 연결
6. `ResetBtnPos`에서 5번째 버튼 위치도 갱신
7. 종료/파괴 시 sidecar 수명 처리

다만 실제 버튼 생성 전에 아래 두 가지를 먼저 확정한다.

- `Layout::Initialize`가 기존 4개에 넘기는 `ButtonLayouts[4]`의 실제 값/패턴
- `TrickSelectButton` 생성자 및 `Initialize`가 요구하는 eTYPE/레이아웃 인자

다음 코드는 이 부분만 추가로 읽는 **write 없는 3차 진단**으로 진행한다.


---

## 16. 2026-09-23 3차 로그: 버튼 클래스 자체는 ID5를 지원

실게임 로그에서 다음이 확인됨.

### TrickSelectButton::SetTrickID

RVA `0x01E7FD90` 런타임 코드에서 ID 범위를 10/11 기준으로 검사한다.
따라서 버튼 객체 자체가 `ID5`를 거부하는 구조는 아니다.

### TrickSelectButton 생성자

RVA `0x01E7FEC0`.

`Layout::Initialize`의 호출부를 다시 대조하면 생성자 호출 직전 EDX를
별도 설정하지 않는다. 따라서 과거 메모의 “ctor(eTYPE)” 해석은 사용하지 않는다.
현재 확인된 호출 형태는 **기본 생성자 `TrickSelectButton(this)`** 로 취급한다.

### TrickSelectButton::Initialize

RVA `0x01E7FF40`.

함수 시작에서:

- EDX -> 첫 번째 int
- R8D -> 두 번째 int
- R9 -> parent/control pointer

를 보존한 뒤 base 초기화에 전달한다.
즉 기존 버튼 생성 루프의 호출은 실질적으로:

```text
button->Initialize(layoutX, layoutY, TrickCommandDialogLayout*)
```

형태로 볼 수 있다.

### 기존 4개 UI 등록 ID

`Layout::Initialize`가 참조하는 static dword[4]가 실게임에서:

```text
2, 3, 4, 5
```

로 확인됨.

이 값은 Trick ID가 아니라 `CUIMaker::RegisterLayout`에 전달되는 UI/control ID다.

### 다음 단계

실제 sidecar 생성 전에 live `TrickCommandDialogLayout*`와 기존 4개 버튼의:

- 실제 UI ID `+0x88`
- layout control count `+0x150`
- 주요 상태값
- ResetBtnPos가 사용하는 실제 좌표 세트

를 한 번 캡처한다.

이를 위해 `ResetBtnPos +0x1C8`의 확정 명령
`add rsi,0x1E0`에서 RSI(layout)를 저장하는 build-guarded read-only capture hook을 사용한다.


---

## 17. 2026-09-23 live layout 캡처 결과

`test: capture live fifth-button layout state` 실게임 결과:

```text
Layout = live TrickCommandDialogLayout*
controlCount(+0x150) = 7
buttons[0..3] = 4개 모두 유효
ResetPos A/B = x1 826, y 364, x2 1106, step 280
button UI IDs(+0x88) = 2,3,4,5
button state(+0x8C) = 모두 1
```

네 버튼의 vtable도 모두 동일했고, `TrickSelectButton` 실객체로 일치한다.

### 중요한 새 제약

버튼 UI ID가 2~5인데 layout의 `controlCount(+0x150)`가 7이다.
따라서 단순히 “다음 번호니까 ID 6”으로 5번째를 등록하면
이미 존재하는 다른 layout control과 충돌할 가능성이 있다.

5번째 버튼 생성 전에 반드시:

1. `TrickCommandDialog*` live pointer를 캡처
2. `dialog+0x08 == captured layout` 검증
3. layout `+0x140` registration 영역의 실제 구조/점유 ID 확인

을 수행한다.

다음 진단은 `Dialog::Open +0x130`에서 R15(dialog)를 build-guarded하게
캡처하고, layout `+0x140..+0x1DF`의 작은 고정 영역 및 그 내부 테이블에서
기존 4개 button pointer의 위치만 찾는다. 광범위 메모리 스캔은 하지 않는다.


---

## 18. 2026-09-23 UI ID/실좌표 확정

`test: verify remaining UI IDs and live button spacing` 실게임 결과:

```text
ResetBtnPos live:
startX = 406
y      = 364
step   = 280
5번째 예상 X = 1526
```

따라서 과거 정적 값 차이로 계산했던 280은 실제 `ResetBtnPos`의 EBX(step)와도 일치한다.
기존 4개 버튼의 실좌표는 이 루프 기준으로:

- button0: x=406
- button1: x=686
- button2: x=966
- button3: x=1246
- sidecar 후보: x=1526

이다.

### UI/control ID 점유

live control 확인:

```text
buttons[0..3] IDs = 2,3,4,5
layout+0x290 control ID = 6, state=1
layout+0x280 control ID = 0
layout+0x288 = 다른 타입의 포인터(동일 필드 해석 금지)
layout+0x2A0 control ID = 0
controlCount(+0x150) = 7
```

따라서 **ID6은 이미 다른 정상 컨트롤이 사용 중**이다.
5번째 TrickSelectButton에 ID6을 재사용해서는 안 된다.

현재 등록 가능한 ID 범위는 0..6으로 보이며, 새 버튼을 정식 등록하려면
ID7을 위해 registry를 8칸으로 확장하거나, registry 등록을 우회하는 방식이 필요하다.

### 다음 확인

`Layout::Initialize`에서 `layout+0x140` registry 초기화 시:

```asm
mov r9, rsi
mov r8d, 7
lea rdx, <local descriptors>
lea rcx, [rsi+0x140]
call 0x01D13E60
```

형태가 확인되어 있다.

또 버튼 등록은 PDB 기준 `CUIMaker::RegisterLayout` RVA `0x01D16AA0`을 사용한다.

다음 진단은 이 두 함수의 런타임 코드만 읽어서:

1. count=7이 내부 고정 배열인지 동적 저장소인지
2. ID7을 추가 등록하려면 저장소 재할당이 필요한지
3. registry 등록 없이 parent child-control만으로 표시 가능한 구조인지

를 판단한다.

이 진단이 끝나기 전에는 `controlCount 7 -> 8`을 쓰지 않는다.


---

## 19. 2026-09-23 CUIMaker registry 동적 구조 확정 및 첫 sidecar 생성 단계

`CUIMaker::InitLayouts` 런타임 코드를 해석한 결과 registry는 고정 7칸 배열이 아니다.

```text
count = R8D
descriptor storage = game allocator(count * 0x60)
pointer storage    = game allocator(count * 8)
maker+0x00 = descriptor storage
maker+0x08 = pointer storage
maker+0x10 = count
maker+0x18 = owner/layout
```

현재 `layout+0x140`가 이 maker 구조이며 따라서:

- `layout+0x140` = descriptor storage
- `layout+0x148` = pointer storage
- `layout+0x150` = count (=7)

이다.

`CUIMaker::RegisterLayout`도:

```asm
test id,id
js fail
cmp id,[maker+0x10]
jge fail
...
control+0x8C = 1
control+0x88 = id
```

형태로 동적 count만 검사한다.

따라서 향후 ID7용 count=8 확장은 구조적으로 가능하다.
다만 기존 registry를 즉시 재할당하면 destructor/기존 control 참조까지 함께 고려해야 하므로
첫 실제 UI 실험에서는 registry를 건드리지 않는다.

### 첫 실제 5번째 버튼 생성 테스트

다음 테스트는 live `TrickCommandDialogLayout` 캡처 후:

1. 게임 자체 allocator를 사용해 정확히 `0x1D8` 할당
2. `TrickSelectButton::ctor` 호출
3. `TrickSelectButton::Initialize(x=1526,y=364,parent=layout)` 호출
4. 기존 버튼 생성 후처리 일부를 동일하게 적용
5. `SetTrickID(5)`
6. 기존 0..6 registry와 충돌하지 않도록 UI ID는 `-1` sentinel
7. `RegisterLayout`과 signal callback은 아직 호출하지 않음

으로 진행한다.

이 단계의 목적은 **독립 sidecar TrickSelectButton이 실제 전투 UI에 렌더링 가능한지**만 검증하는 것이다.
버튼이 보여도 아직 클릭하지 않는다.

성공 후 다음 단계:

- GetTrickButton(4) -> sidecar
- Dialog::Open index4 -> sidecar
- Dialog::Initialize callback loop index4 -> sidecar
- registry를 안전하게 8칸으로 확장하여 ID7 정식 등록

순으로 기능화한다.
