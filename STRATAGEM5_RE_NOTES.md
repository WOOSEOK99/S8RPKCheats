# 5번째 책략 역분석 기록

> 이 문서는 5번째 책략(ID5) 추가 실험의 **저장소 기준점(source of truth)** 이다.
> 새 채팅/세션에서는 EXE/PDB 재업로드를 요청하기 전에 반드시 이 문서와
> `Internal DX11 Base/Cheats/War/StratagemSlotProbe.cpp`,
> `Internal DX11 Base/Cheats/War/Spell5HealProbe.cpp`를 먼저 확인한다.
> 최신 원인 확정과 입력 tag/등록 type 분리는 **30절**, 조기 설치 설계는 **28절**을 확인한다.
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


---

## 20. 2026-09-23 표시 전용 sidecar 결과 및 ID7 registry 실험

표시 전용 sidecar 생성 로그는 성공했지만 실제 화면에는 기존 4개만 보였다.

```text
표시 전용 5번째 버튼 생성 성공
UI-ID=-1
TrickID=5
실제 화면: 5번째 버튼 안 보임
```

따라서 `TrickSelectButton::ctor/Initialize/SetTrickID`만으로는
전투 UI 렌더 경로에 완전히 편입되지 않으며,
`CUIMaker::RegisterLayout` 등록이 필요하다고 판단한다.

### 다음 실험: registry 7 -> 8

앞 단계에서 확인한 `CUIMaker` 동적 구조를 이용한다.

안전 가드:

1. 기존 maker count가 정확히 7인지 확인
2. owner가 live layout과 일치하는지 확인
3. 내부 lookup 함수로 ID2~5가 실제 4개 TrickSelectButton과 정확히 일치하는지 확인
4. ID6 lookup도 `layout+0x290`의 확인된 ID6 control과 일치해야 진행
5. 기존 descriptor 7개를 8개 임시 배열로 복사
6. ID7 descriptor는 ID5(button4) descriptor를 clone
7. descriptor의 X/Y 필드는 ID2~5 값이 실제 `406,686,966,1246 / y=364` 패턴과 맞는 필드를 자동 검출
8. 새 ID7 X=1526, Y=364
9. `CUIMaker::InitLayouts(maker, descriptors, 8, layout)`
10. 기존 등록 control을 원래 ID/type으로 다시 등록
11. sidecar를 ID7/type 0x14로 등록
12. lookup(7), button+0x88==7, button+0x8C==1을 모두 검증

실패하거나 예외가 발생하면 maker의 원래 0x28-byte 상태를 즉시 복구한다.
실험 중 기존 7칸 storage는 해제하지 않아 rollback 포인터가 유효하도록 한다.
성공 시 old storage는 소량 leak되지만 실험 단계에서 안전한 수명 검증을 우선한다.

아직 callback / Dialog::Open index4 / GetTrickButton(4)는 연결하지 않는다.
따라서 ID7 등록 후 버튼이 보여도 클릭하지 않는다.


---

## 21. 2026-09-23 ID7 1차 실패 원인 대응 + 5버튼 압축 배치

`test: register fifth stratagem sidecar as UI ID7` 실게임 결과:

```text
표시 전용 sidecar 생성 성공
registry 확장 중 예외 발생
화면에는 기존 4개만 표시
```

또 실제 화면 기준으로 기존 4개는 한 줄 공간을 거의 사용하고 있어,
기존 `step=280` 그대로 5번째를 `x=1526`에 두는 것은 우측 영역을 벗어날 가능성이 높다.

### registry 예외 대응

이전 실험은 이미 초기화된 live `layout+0x140 CUIMaker`에
`InitLayouts(...,8,...)`를 직접 다시 호출했다.
이 경로는 예외가 발생했으므로 더 이상 live maker in-place 재초기화를 하지 않는다.

새 방식:

1. 기존 live maker는 읽기만 함
2. 별도의 zeroed 임시 maker(0x40 bytes)를 준비
3. 임시 maker에 `InitLayouts(...,8,layout)`
4. 기존 ID0~6 control을 임시 maker에 원래 type으로 재등록
5. sidecar를 ID7/type 0x14로 등록
6. lookup(7), sidecar ID/state 검증
7. 모든 검증이 끝난 뒤에만 임시 maker의 0x28-byte 상태를 live `layout+0x140`로 교체
8. 예외/실패 시 live maker는 건드리지 않거나 원래 0x28 bytes로 복구

추가로 예외 로그에 `stage=N`을 남겨 어느 호출에서 실패했는지 바로 구분한다.

### 5개 버튼 배치

이미지 리소스 자체는 아직 축소하지 않는다.
먼저 기존 네 버튼의 중심을 유지하면서 간격만 압축한다.

확정 기존 값:

```text
startX=406
step=280
y=364
```

새 계산:

```text
compactStep = oldStep * 11 / 14
oldCenter   = oldStart + oldStep * 3 / 2
compactStart= oldCenter - compactStep * 2
```

현재 빌드에서는:

```text
compactStep=220
x = 386, 606, 826, 1046, 1266
y = 364
```

즉 기존 4개와 5번째를 거의 같은 전체 폭 안에 넣는다.
프레임/이미지가 실제로 겹치는지는 ID7 등록 성공 후 화면으로 확인한다.
겹칠 경우 다음 단계에서 `TrickSelectButton`의 실제 scale/size setter를 찾아
5개 버튼만 동일 비율로 축소한다.


---

## 22. 2026-09-23 stage=5 원인 확정: InitLayouts 재호출 폐기

압축 배치 버전 실게임 로그:

```text
표시 전용 sidecar 생성 성공: x=1266
registry 임시 8칸 생성 시작
registry 확장 중 예외: stage=5
```

stage=5는 정확히 임시 maker에 `CUIMaker::InitLayouts(...,8,...)`를 호출하는 단계다.

따라서 `InitLayouts`는 단순히 `maker+0/+8/+10/+18`만 채우는 함수가 아니라
원래 UI 생성 컨텍스트/추가 상태를 전제로 하는 함수로 취급하고,
**런타임에서 재호출하지 않는다.**

### 새 방식

이미 정상 생성된 live maker에서 필요한 데이터는 이미 모두 존재한다.

- descriptor storage: 7 * 0x60
- pointer storage: 7 * 8
- count=7
- owner=layout
- 기존 ID0~6 control lookup 가능

따라서:

1. 게임 allocator로 새 descriptor storage `8*0x60` 직접 할당
2. 기존 7 descriptor 복사
3. ID7은 ID5 버튼 descriptor clone + 새 좌표 적용
4. 새 pointer storage `8*8` 직접 할당 후 zero
5. 임시 maker 0x28 bytes를 수동 구성:
   - +00=new descriptors
   - +08=new pointers
   - +10=8
   - +18=layout
   - +20=0
6. 기존 ID0~6을 `RegisterLayout`로 재등록
7. sidecar를 ID7로 등록
8. lookup(7) 및 button id/state 검증
9. 성공 후에만 live maker의 0x28-byte state 교체

즉 더 이상 `InitLayouts`를 호출하지 않는다.

또 descriptor 좌표 변경만으로 이미 만들어진 버튼은 이동하지 않으므로
기존 4개와 sidecar 모두 virtual set-position(+0x90)을 호출해:

```text
386, 606, 826, 1046, 1266 / y=364
```

으로 실제 객체 위치도 압축 배치한다.


---

## 23. 2026-09-23 stage=6 원인 확정: RegisterLayout 5번째 인자 누락

수동 registry 확장 실게임 결과:

```text
sidecar 생성 성공
stage=6에서 예외
```

stage=6은 기존 ID0~6 control을 새 maker에 `CUIMaker::RegisterLayout`로
재등록하는 첫 구간이다.

원본 `Layout::Initialize` 호출부를 다시 대조하면:

```asm
mov dword ptr [rsp+20], 1 ; 5번째 인자
mov r9d, 0x14            ; type
mov r8, [button]          ; control
mov edx, id
lea rcx, [maker]
call CUIMaker::RegisterLayout
```

이다.

또 `CUIMaker::RegisterLayout` 내부에서도 실제로:

```asm
mov r8d, [rsp+80]
```

형태로 5번째 인자를 읽는다.

따라서 기존 실험의 함수 선언:

```cpp
void(maker,id,control,type)
```

은 잘못이었고, 실제 호출은:

```cpp
void(maker,id,control,type,flag)
```

이며 현재 원본과 동일하게 `flag=1`을 전달한다.

이번 수정에서는 기존 ID0~6 재등록과 ID7 등록 모두 5번째 인자 `1`을 전달한다.
stage=6에서 어느 ID에서 문제가 생기는지도 바로 확인할 수 있도록 재등록 직전 로그를 추가했다.


---

## 24. 2026-09-23 stage=6 재분석: synthetic maker 폐기, live maker 내부 상태 보존

5번째 인자를 추가한 뒤에도:

```text
기존 UI ID0 재등록 시작
stage=6 예외
```

가 발생했다.

따라서 5번째 인자 누락은 수정해야 했던 문제지만,
stage=6의 핵심 원인은 **synthetic/temp maker가 실제 CUIMaker의 전체 내부 상태를 갖고 있지 않다는 것**으로 확정한다.

`CUIMaker::RegisterLayout`는 maker의 첫 0x28 bytes뿐 아니라
내부 helper/list/container 상태를 간접 호출 경로에서 사용한다.
그러므로 새 maker를 얕게 구성하고 기존 ID0~6을 재등록하는 방식은 폐기한다.

### 새 방식

정상 생성된 live maker 자체는 보존한다.

변경하는 것은:

- `maker+0x00` descriptor storage pointer
- `maker+0x08` per-ID helper pointer table
- `maker+0x10` count: 7 -> 8

뿐이다.

새 storage:

- descriptors: 기존 7개 복사 + ID7=ID5 clone
- pointer table: 기존 7개 포인터 그대로 복사
- ID7 pointer helper는 ID5 슬롯을 clone(기존 값이 null이면 null)

maker의 `+0x18 owner`와 `+0x28 이후 내부 상태`는 전혀 건드리지 않는다.

그 다음 기존 ID0~6은 재등록하지 않고,
**새 ID7 하나만 원래 live maker에 RegisterLayout(..., flag=1)** 한다.

성공 후 기존 4개와 새 버튼을:

```text
386, 606, 826, 1046, 1266 / y=364
```

으로 실제 virtual set-position을 호출해 압축 배치한다.

이 방식은 기존 registry container/list를 그대로 유지하기 때문에
이전 temp-maker 실험보다 원본 UI 수명 구조에 훨씬 가깝다.


---

## 25. 2026-09-23 stage=7 원인 확정: RegisterLayout helper는 1회성/소모 구조

latest live experiment:

```text
existing pointer slots =
0,0,0,0,0,0,0

live maker ID7 only registration start
stage=7 exception
```

`CUIMaker::RegisterLayout` runtime code begins by reading:

```asm
mov rcx,[maker+0x08]
mov rcx,[rcx + id*8]
call <helper method>
```

Therefore `maker+0x08` is not a persistent control registry.
It is a per-ID helper/object table produced by `InitLayouts`.

By the time the battle UI is fully initialized, every original slot is already null.
This means RegisterLayout consumes/moves the helper during the first registration.

Thus merely extending:

- descriptor storage
- helper pointer table
- count

is insufficient. ID7 also requires a newly-created **type 0x14 (20) helper object** matching the original four TrickSelectButton layouts.

### Safe next step

Do not call RegisterLayout with a null helper again.

The next build dumps only the previously-unseen tail of:

`CUIMaker::InitLayouts RVA 0x01D13E60, +0x240..+0x500`

The goal is to isolate switch-case `type=0x14` and identify the exact helper constructor/builder invoked for button descriptors.

After that, construct only the one ID7 helper and call RegisterLayout once.
No existing ID0~6 registration is touched.


---

## 26. 2026-09-23 type20 helper tail 확보, 다음은 switch[20] 직접 해석

`InitLayouts +0x240..+0x500` 런타임 바이트는 확보했다.

그러나 `InitLayouts`는 descriptor type을:

```asm
cmp eax, 0x14
ja  default
mov ecx, [rdx + rax*4 + 0x01D149E8]
add rcx, rdx
jmp rcx
```

형태의 jump table로 분기한다.

여기서 `rdx = exeBase`이고 descriptor type `0x14`가
기존 TrickSelectButton layout의 type임은 이미 확인됐다.

따라서 tail 바이트만 보고 어느 handler가 type20인지 추측하지 않는다.
다음 진단은 런타임에서:

- jump table RVA `0x01D149E8`
- entry[20]
- `handler = exeBase + (int32_t)entry[20]`

를 직접 읽고,
**그 exact handler 0x180 bytes만** 덤프한다.

이 결과에서 helper builder/constructor call을 확정한 뒤,
ID7용 helper 하나만 생성한다.


---

## 27. 2026-09-23 type20 exact handler 확정 후 설계 전환: 최초 InitLayouts에서 ID7 helper 생성

> 정정(30절): switch[20]의 주소는 그대로 유효하지만, 실제 네 버튼의 최초 입력 tag는
> 20이 아니라 0이었다. 아래의 "버튼 입력도 type20" 전제는 최신 실측으로 폐기한다.

runtime jump table 직접 확인:

```text
switch[20] -> RVA 0x01D1445D
```

type20 exact handler는 시작하자마자:

```asm
mov ecx,0x140
call <allocator>
...
call <type20 helper ctor>
...
helper virtual initialize/configure...
```

형태로 별도 helper 객체를 만든다.

기존 UI가 완전히 초기화된 뒤에는 maker+0x08의 helper slots가 모두 0이므로
이 helper는 RegisterLayout 과정에서 1회성으로 소모된다.

### 새 방향

helper 생성 로직을 DLL에서 수동 재현하지 않는다.

대신 `TrickCommandDialogLayout::Initialize` 안의 원래
`CUIMaker::InitLayouts(maker, descriptors, 7, layout)` 호출만
near-call bridge로 가로챈다.

bridge는:

1. 원본 7 * 0x60 descriptor 복사
2. UI ID5의 type20 descriptor를 ID7에 clone
3. 원본 `CUIMaker::InitLayouts`를 count=8로 호출

한다.

따라서 ID0~6 helper와 함께 **ID7 type20 helper도 게임 원본 코드가 직접 생성**한다.

기존 버튼 생성/등록 루프는 여전히 ID2~5만 처리하므로,
초기화가 끝난 뒤 helper[0..6]은 소모되어 null이 되지만
helper[7]만 남아 있어야 한다.

그 시점에 sidecar TrickSelectButton을 만들고:

```text
RegisterLayout(maker, 7, sidecar, 0x14, 1)
```

을 한 번만 호출한다.

성공 후 실제 5개 버튼은:

```text
386,606,826,1046,1266 / y=364
```

으로 압축 배치한다.

이 단계에서도 callback / Dialog::Open index4 / GetTrickButton(4)는 아직 연결하지 않는다.

---

## 28. 2026-09-23 DLL 작업 스레드에서 최초 InitLayouts 브리지 조기 설치

기준: GitHub `feature/stratagem-5slot-probe` HEAD
`570e2e01eb6709c7786df85b8bc0add679544add`와 로컬 HEAD의 일치를 원격 조회로 확인.
작업 브랜치: `experiment/astra-stratagem5-ui`.

### 1. 현재 실패 원인: 확정 사실과 아직 남은 가설

기존 `EnsureTrickUiInitLayoutsBridgeHook()`의 호출자는
`SetStratagemFiveMetadataTest(true)` 안의 내부등록 성공 경로 두 곳뿐이었다.
전투 객체/수량/데이터가 이미 준비된 뒤에야 설치되며, DLL 시작 때는 설치하지 않았다.
따라서 그 전에 Initialize가 끝난 layout에는 설치 성공 로그가 나와도 소급 적용되지 않는다.

또한 기존 브리지는 count/descriptor 가드 실패 시 아무 진입 로그 없이 원본으로 통과했다.
**완료 로그 없음 + live count=7만으로는 미호출과 가드 실패를 구분할 수 없었다.**
설치가 늦었을 가능성은 높지만 실제 UI 생성 시점이나 다른 생성 경로는 아직 확정하지 않는다.

`MainThread_Initialize()`는 `LoadEarlyLogConfig()` 후 `Sleep(10000)`을 실행했다.
그 뒤 Engine/치트/D3D 훅/백그라운드 루프가 시작된다.
`Menu::Loops()`의 BattleMonitor 호출은 100ms 간격이며, 전투 포인터는 500ms 간격으로 갱신된다.
전투 모드 refresh는 유효한 부대 수와 Day 1~30을 확인한 뒤 수행한다.
이 흐름은 전투 UI 생성 **이전**을 보장하는 지점이 아니므로 여기로 설치를 옮기지 않는다.

### 2. 가장 안전한 해결 방향과 대안 비교

| 방법 | 필요한 변경/미확정 사항 | 판단 |
|---|---|---|
| DLL 작업 스레드에서 특정 InitLayouts call 조기 설치 | 기존 bridge 재사용, 빌드/인자 가드, 실제 호출 로그 | 이번 실험에 채택 |
| type20 handler `0x01D1445D`를 이용해 helper 한 개 직접 생성 | 함수 중간 switch 분기의 레지스터/스택 상태, allocator tag, ctor 후 virtual configure, 소유권/정리 규약 재현 | 더 큰 변경과 추가 런타임 근거 필요 |

handler 주소가 확정돼도 독립적으로 호출 가능한 함수라는 뜻은 아니다.
원래 InitLayouts가 최초 초기화에서 helper를 만들게 하는 쪽이 현재 근거로는 변경 범위가 작다.

### 3. 수정 함수/파일

- `Source.cpp::MainThread_Initialize`: loader lock 밖의 기존 작업 스레드에서
  `LoadEarlyLogConfig()` 직후 준비. 기존 10초 대기를 100ms × 100회로 나누고,
  준비 실패 시 그 대기 중에만 재시도한다. 성공 후에는 반복 호출하지 않는다.
- `StratagemSlotProbe.h/.cpp::PrepareStratagemFiveUiBridge`: 전투 데이터/메뉴 토글과
  독립적인 준비 함수. 실패 로그는 시작/최종에 출력하며 반복 재시도는 조용히 수행한다.
- `EnsureTrickUiInitLayoutsBridgeHook`: x64 PE timestamp와 RSDS GUID/age를 1절과 대조.
  `Layout::Initialize +0xB20..+0xB80`의 기존 0x60 범위에서 원본 InitLayouts 대상 CALL이
  정확히 하나인지, 직전 `mov r8d,7`이 있는지 검사하고 쓰기 직전 CALL 원본을 재검증한다.
  다른 CUIMaker 호출이나 InitLayouts 함수 전체를 후킹하지 않는다.
- `TrickUiInitLayoutsBridge`: 진입부터 hit/tick/thread/maker/owner/count를 기록.
  `maker == owner+0x140`, layout 범위, count=7, descriptor 범위, ID2~5 type20을 확인.
  가드 실패 시 원본 인자로 한 번 통과한다. 확장 호출 후 count=8과 helper7이 확인돼야
  `expanded` 카운터와 완료 로그를 남긴다.
- 원본 함수 포인터는 CALL 패치 **전에** atomic으로 게시한다. cave는 스택을 건드리지 않는
  tail jump를 사용해 별도 unwind 정보 없는 가짜 프레임을 만들지 않는다.
- 확장 중 SEH는 코드만 기록하고 전파한다. 부분 초기화 maker에 원래 count=7로
  재호출하던 기존 경로를 제거했다. 예외를 삼키고 정상 초기화처럼 계속하지 않는다.
- `SetStratagemFiveMetadataTest`: 조기 설치 상태를 보고하고, 준비 실패 시 내부등록을 중단.
  기존 호출 위치는 제거했다. 여기서 설치되는 경우는 늦은 fallback일 수 있다.
- `UpdateStratagemFiveUiRuntimeProbe`: live layout 상태와 누적 hits/expanded를 함께 출력.
  count=8, owner 일치, 유효한 helper7이 없으면 sidecar **할당 전** 중단한다.
  실제 등록 함수의 type/lookup/5인자 호출 검증도 그대로 유지한다.

### 4. 위험 요소와 복구 범위

- 아직 실게임에서 조기 설치/확장/렌더 결과를 확인하지 않았다. 코드가 늦게 풀리거나,
  DLL보다 먼저 UI가 생성되거나, 다른 경로가 사용되는 가능성이 남는다.
- CALL 수정은 기존 방식의 5바이트 메모리 쓰기다. 설치와 해당 명령 실행이 겹치는
  경우까지 동기화하는 패치는 아니므로, 실행 중 전투에 DLL을 주입하는 테스트는 피한다.
- 가드가 맞아도 원본 InitLayouts의 내부 실패는 게임 예외로 이어질 수 있다.
  그런 maker의 안전한 롤백 방법은 확인되지 않았다.
- helper7은 최초 초기화 때 만들어져 내부등록 전까지 남는다. 미등록 helper 정리와
  sidecar 파괴/재사용, 창 재개방 때 위치 유지, 게임 스레드와 기존 probe 간 수명 동기화는
  여전히 실게임 확인 과제다. 이번 성공 기준은 새 프로세스의 첫 표시다.
- raw hook이 DLL 주소를 참조하므로 설치 성공 시 DLL을 프로세스 종료까지 pin한다.
  **이 실험 빌드는 실행 중 DLL unload/reload를 지원하지 않는다.**
  내부등록 OFF는 count=8 maker나 조기 bridge를 제거하지 않는다.
  완전 복구는 게임 종료 후 기존 DLL로 교체하고 재시작한다. EXE/세이브 파일은 수정하지 않는다.
- 기존 ID1~4 배열, UI ID6, callback/Open/GetTrickButton의 동작 확장은 건드리지 않는다.
  실제 다섯 번째 버튼 표시를 사용자가 확인한 뒤에 선택/사용 경로 작업을 진행한다.

### 5. 기존 실패 접근과의 차이

live maker 재초기화, 임시 maker 복제, count만 수정, null helper 등록을 하지 않는다.
이미 쓰인 ID0~6을 재등록하지 않고, 게임의 최초 InitLayouts 한 번이 helper7까지 생성한다.
새 준비 경로는 PE/CodeView 및 정해진 0x60 코드 범위만 읽는다. 새로운 broad scan은 없다.

### 실게임 테스트 순서

1. 치트 메뉴에서 **파일 로그 출력**을 켜고 설정을 저장한 뒤 게임을 완전히 종료한다.
   `LoadEarlyLogConfig()`가 읽을 `bFileLog=true`가 저장되어 있어야 시작 로그가 남는다.
2. 기존 DLL을 백업하고 이번 DLL을 **게임이 종료된 상태에서** 교체한다.
   로컬 검증 산출물은 `x64/Release/hid.dll`이며 다른 proxy DLL과 중복 로드하지 않는다.
3. 게임을 새로 실행한다. 전투 진입 전에 `S8RPK_cheat.log`에서
   `[Stratagem5UI] early bridge preparation before startup delay`와
   `[책략5UIHELPER] ... 브리지 설치 완료: callRVA=... tick=...`를 확인한다.
   빌드 가드/준비 실패만 있으면 이후 토글 실험을 진행하지 않고 로그를 보낸다.
4. 새 전투로 진입한다. 원래 UI 생성 시점에 `브리지 진입`과
   `초기 InitLayouts 7->8 완료: ... count=8 helper7=<non-null>`이 나오는지 확인한다.
   UI가 지연 생성된다면 이 로그는 6단계에서 나올 수도 있다.
5. 기존 순서대로 `5번 책략 데이터` → `5번 책략 횟수 1` → `5번 책략 내부등록`을 켠다.
   `상태(metadata-enable)`의 installed/hits/expanded/lastOwner도 보관한다.
6. 책략창을 연다. `상태(live-layout)`, `controlCount(+150)=8`,
   helper7 등록 시작, `ID7 정식 등록 성공` 로그를 확인하고
   **기존 4개와 별도의 다섯 번째 버튼이 실제 화면에 보이는지** 확인한다.
   알려진 좌표라면 `386,606,826,1046,1266 / y=364`가 출력된다.
7. 다섯 번째 버튼은 아직 클릭하지 않는다. 첫 표시 결과와 기존 4개의 표시/기능 이상 여부를
   기록한다. 등록 실패 후 OFF/ON 반복, 전투 재진입으로 강제 재시도하지 않는다.

성공 예상 순서(주소/tick/hit 값은 실행마다 다름):

```text
[Stratagem5UI] early bridge preparation before startup delay
[책략5UIHELPER] 책략 UI 초기 InitLayouts 7->8 브리지 설치 완료: callRVA=... tick=... thread=...
[책략5UIHELPER] 브리지 진입: hit=1 ... count=7
[책략5UIHELPER] 초기 InitLayouts 7->8 완료: ... count=8 helper7=...
[책략5UIHELPER] 상태(live-layout): installed=1 ... hits=1 expanded=1 lastOwner=...
[책략5UICAP] Layout=... controlCount(+150)=8 / buttons=...
[책략5UITEST] 원본이 만든 helper7로 ID7 등록 시작: ...
[책략5UITEST] ID7 정식 등록 성공. helper7 소모 및 5버튼 압축 배치 완료.
```

실패하면 **새 실행 시작부터 첫 표시 시도까지의 로그 전체**를 보낸다.
최소 포함 태그: `[Stratagem5UI]`, `[책략5UIHELPER]`, `[책략5UICAP]`,
`[책략5UITEST]`, `[책략5PDBDBG]`. 설치/진입/완료가 없는 경우도 그대로 보존한다.

| 관찰 | 다음 판단 |
|---|---|
| installed=1, hits=0, live count=7 | 설치 이전 생성 또는 다른 경로. 아직 둘 중 하나로 단정하지 않음 |
| hits>0, expanded=0 | 진입 인자/descriptor 가드 또는 확장 결과 실패 로그 확인 |
| expanded>0인데 live count=7 | lastOwner와 현재 Layout 비교; 다른 인스턴스/초기화 경로 조사 |
| count=8, helper7=0, 등록 성공 로그 없음 | helper 소비/파괴 시점 조사. null로 등록하지 않음 |
| ID7 등록 성공인데 화면은 4개 | 렌더/표시 경로가 남은 문제. 선택 callback 확장으로 넘어가지 않음 |

### 로컬 검증 범위

- GitHub connector 빌드가 아니라 이 작업 PC의 Visual Studio 18 MSBuild로
  `Release|x64`, `ProxyType=hid`, `TargetName=hid` 빌드 및 링크 종료 코드 0을 확인했다.
  로그: `stratagem5-build.log` (로컬 ignored 파일).
- 로컬 `artifacts/stratagem5_bridge_smoke.cpp`에서 production bridge를 직접 포함하고,
  게임 함수만 stub으로 대체하여 7개 검사를 통과했다: 원본 7개 descriptor 보존/ID7 clone,
  count 불일치 통과, owner 불일치 통과, type 불일치 거부, null helper 성공 처리 방지,
  SEH 전파 시 원본 호출 1회, 다른 EXE 빌드 거부. 실제 hook 설치는 호출하지 않는다.
- 이 검증은 게임의 원본 InitLayouts ABI, helper 생성/소유권, 렌더 성공을 입증하지 않는다.
  **실게임 검증은 대기 중이며 최종 5번 선택/사용 기능은 아직 완료되지 않았다.**

---

## 29. 2026-09-23 조기 브리지 진입 확인, 초기 실패 원인 지연 출력

### 1. 이번 실게임에서 확정된 것

`eeea7dd` 테스트 로그:

```text
상태(metadata-enable): installed=1 installTick=10274218 hits=1 expanded=0
lastOwner=000002546B3B4250
Layout=000002546B3B4250 controlCount(+150)=7
Dialog=000002540586ED30 dialog+08(layout)=000002546B3B4250 match=1
표시 실험 중단: ... count=7 helper7=0 hits=1 expanded=0
```

브리지가 한 번 실행됐고, 그 owner 주소와 현재 live layout 주소가 일치한다.
따라서 이번 시도의 핵심 문제를 계속 "설치됐지만 브리지에 한 번도 안 들어감"으로
분류하지 않는다. 다만 객체 주소 일치만으로 다른 시점의 주소 재사용까지 배제하는 것은 아니다.

현재 문제는 **해당 호출에서 확장 성공 카운터가 올라가지 않은 이유**다.
인자 검사, descriptor 복사/type 검사, 확장 후 count/helper 검사 중 어느 분기인지는
사용자가 제공한 로그만으로 확정할 수 없다. 기존 count=7 상태에서 sidecar 할당/등록은
예정대로 중단됐으며, 데이터/수량/기존 네 버튼 상태는 정상으로 관찰됐다.

### 2. 실패 이유가 로그에 없는 이유와 해결 방향

제공된 로그는 디버그 모드 활성화부터 시작한다. `showlog.cpp::AddLog()`는
디버그와 파일 로그가 모두 꺼져 있으면 메시지를 저장하지 않고 반환한다.
초기 실패 메시지는 파일 로그가 켜져 있었다면 파일에만 남을 수도 있다.
이번 작업 공간에는 해당 게임 로그가 없어 초기 분기를 직접 확인하지 못했다.

기존 atomic 카운터는 남아 있어서 `hits=1/expanded=0`만 나중에 볼 수 있었다.
이를 해결하기 위해 마지막 초기 호출의 인자·중단 이유·복사한 입력 일부·반환 결과를
DLL의 고정 크기 저장소에 보관하고 `LogTrickUiBridgeStatus()`에서 재출력한다.
저장 자체는 로그 옵션과 무관하다. 가드를 완화하거나 다른 초기화 경로를 추가하지 않는다.

### 3. 수정 함수/파일

- `StratagemSlotProbe.cpp`: `TrickUiInitTrace` 및 SRW lock으로 보호하는 값 복사 저장소 추가.
  lock은 복사 때만 잡으며 게임 함수 호출·AddLog 호출 동안에는 잡지 않는다.
- `TrickUiInitLayoutsBridge`: 각 기존 분기에서 reason을 저장한다. descriptor를 이미
  읽은 경우에만 그 사본에서 `+0x000,+0x060,...,+0x240` 각 위치의 앞 0x20 bytes를 보관한다.
  예외 코드도 재초기화 없이 기록하고 전파한다.
- `LogTrickUiBridgeStatus`: 내부등록 및 live layout 확인 때 `[책략5UIINIT]`로 저장값을 출력.
  descriptor 원래 주소는 숫자로만 보여주고, 나중에 그 주소를 다시 역참조하지 않는다.
- `STRATAGEM5_RE_NOTES.md`: 이번 관찰과 아래 판정 기준 기록.

| reason | 의미 |
|---|---|
| `entered` / `calling-expanded` | 해당 호출이 아직 진행 중인 시점의 스냅샷일 수 있음 |
| `missing-original` | 원본 함수 주소가 준비되지 않음 |
| `owner-range` | owner의 0x2A8 범위 검증 실패 |
| `maker-owner` | maker가 owner+0x140과 다름 |
| `input-count` | 전달 count가 7이 아님 |
| `descriptor-range` / `descriptor-copy` | 입력 descriptor 범위/읽기 실패 |
| `descriptor-types` | 현재 가정한 +0x0C0/+0x120/+0x180/+0x1E0의 type이 모두 20은 아님 |
| `expanded-result` | 원본 count=8 호출 후 count/helper7 검증 실패 |
| `init-exception` | 원본 확장 호출에서 예외; 원본 함수 재호출 없음 |
| `expanded-ok` | count=8 및 유효한 helper7 확인 |

### 4. 위험 요소와 아직 미확정인 입력 해석

`InitLayouts`의 **결과 저장소**가 count×0x60이라는 점과
**입력 배열의 stride/필드 배치/배열 순서가 UI ID와 일치**한다는 것은 구분해야 한다.
현재 코드는 후자도 0x60/첫 dword type/ID순서라고 가정하고 검사한다.
이번 로그에는 그 검사값이 없으므로 잘못된 stride나 순서를 실패 원인으로 단정하지 않는다.
새 출력의 `가정 stride=60`도 확정 구조 선언이 아니라 기존 검사의 실제 읽기 위치를 뜻한다.
type 검사 실패라면 저장된 값과 원본 소비 코드를 대조한 뒤 다음 변경을 결정한다.

이번 변경은 초기 실패를 관찰하기 위한 것이다. 확장 알고리즘과 등록 조건은 그대로이며,
다섯 번째 버튼 표시나 선택/사용이 해결됐다는 의미가 아니다. 28절의 수명/실게임 제한도 유지된다.

### 5. 기존 실패 접근과의 차이 및 다음 테스트

같은 maker의 InitLayouts 재호출, count만 수정, null helper 등록, type 가드 제거를 하지 않는다.
새 게임 메모리 탐색도 없다. 이미 읽은 입력 값의 일부를 보관하여 사라졌던 초기 증거를 얻는다.

1. 게임을 완전히 종료한 뒤 이번 DLL로 교체하고 재실행한다.
2. 전투에서 디버그를 켜고 기존 데이터 → 횟수 1 → 내부등록 순서로 진행한다.
   파일 로그를 켜두면 초기 원문도 남지만, 꺼져 있었어도 저장된 원인은 내부등록 시 출력된다.
3. `[책략5UIINIT] 저장된 초기 호출(metadata-enable): ... reason=...`부터
   `초기 입력 +...`, `초기 확장 반환/예외`까지 **UIINIT 전체 줄**을 보낸다.
4. 책략창을 열어 나온 `상태(live-layout)`과 Layout/count 줄도 함께 보낸다.
   이번에도 다섯 번째 버튼이 보이기 전에는 callback/Open/GetTrickButton 확장을 진행하지 않는다.

지연 출력 예상 형식(실제 reason은 다음 실게임에서 판정):

```text
[책략5UIINIT] 저장된 초기 호출(metadata-enable): hit=1 reason=<실제 분기> ... maker=... owner=... owner+140일치=... descriptors=... count=...
[책략5UIINIT] 초기 입력 +000 (가정 stride=60, 앞20): ...
...
[책략5UIINIT] 초기 입력 +240 (가정 stride=60, 앞20): ...
```

입력 복사 전에 거부됐다면 `초기 입력` 줄이 없는 것이 정상이다.
로컬 smoke test는 기존 7개에 **초기 로그 OFF 후 원인/입력 사본 재출력**과
**이전 호출이 최신 스냅샷을 덮어쓰지 않음**을 추가하여 9개가 통과했다.
이 PC의 MSBuild `Release|x64` / `hid` 빌드·링크도 종료 코드 0을 확인했다.

---

## 30. 2026-09-23 실패 원인 확정: 초기 입력 tag=0과 등록 인자 type=20의 혼동

### 1. 현재 실패 원인

`de38bcb` 사용자 파일 로그에서 확정:

```text
15:13:20 브리지 설치 완료: callRVA=1DAFE95
15:14:03 브리지 진입: maker=0000014CC15A9390 owner=0000014CC15A9250 count=7
15:14:03 초기 7칸 descriptor type 검증 실패: 0/0/0/0
15:14:16 reason=descriptor-types owner+140일치=1
15:14:30 Layout=0000014CC15A9250 controlCount(+150)=7
```

설치 시점과 해당 CALL 경로는 이번 실행에서 확인됐다.
이번 실패는 **초기 입력 첫 dword에 등록 인자 20을 요구한 잘못된 가드** 때문이다.
가드가 원본 count=7로 통과시켰으므로 helper7 생성과 ID7 등록 단계에는 도달하지 않았다.

입력에서 관찰된 7행의 첫 5 dword(십진수):

| 행/입력 offset | +00 tag | +04 | +08 | +0C | +10 |
|---|---:|---:|---:|---:|---:|
| 0 / +000 | 12 | 260 | 314 | 1400 | 420 |
| 1 / +060 | 13 | 260 | 75 | 1400 | 659 |
| 2 / +0C0 | 0 | 406 | 364 | 268 | 148 |
| 3 / +120 | 0 | 686 | 364 | 268 | 148 |
| 4 / +180 | 0 | 966 | 364 | 268 | 148 |
| 5 / +1E0 | 0 | 1246 | 364 | 268 | 148 |
| 6 / +240 | 0 | 480 | 536 | 960 | 152 |

행2~5의 +04/+08은 ResetBtnPos의 기존 네 버튼 좌표와 정확히 맞는다.
이 실측은 해당 입력의 0x60 간격과 네 버튼 행 대응을 뒷받침한다.
`RegisterLayout`의 원본 호출 인자 `R9D=0x14`는 별도 확인된 값이며 그대로 유지한다.
또 입력 행0의 첫값은 12인데 초기화/등록이 끝난 maker 저장소 첫값은 20이었다.
따라서 **서로 다른 단계의 첫값/호출 인자를 같은 의미로 취급하지 않는다.**
switch[20] handler 주소를 다시 추측하거나, 입력 tag를 강제로 20으로 고치지 않는다.

### 2. 가장 안전한 해결 방향

기존 최초 InitLayouts 경로에서 실제 입력 행5 전체 0x60 bytes를 새 행7로 그대로 복사한다.
입력 가드는 단순히 20→0만 바꾸지 않고 위 7개 행의 첫 5 dword를 모두 검증한다.
+0x14 이후에는 이번 로그에도 달라지는 값이 있으므로 의미를 추측하거나 초기화하지 않는다.

원본 InitLayouts가 count=8로 한 번 반환한 직후, 원본 버튼들이 helper를 소비하기 전에
ID2/3/4/5/7 helper를 읽어 다음을 확인한다.

- count=8, maker owner 일치, descriptor/helper 저장소 범위 유효.
- ID7 helper가 유효하고 원본 네 helper와 별개 객체이며 vtable이 모두 같음.
- 생성 직후 행5와 행7의 첫 5 dword가 같음.

통과한 maker/owner/저장소/helper7/vtable/행7 사본을 저장한다.
실제 sidecar 생성 전과 등록 직전에 이 값들이 현재 객체와 여전히 같은지 다시 확인한다.
등록할 때는 descriptor의 첫값을 인자로 사용하지 않고 원본 그대로
`RegisterLayout(maker,7,sidecar,20,1)`을 호출한다.

### 3. 수정 함수/파일

- `StratagemSlotProbe.cpp::TrickUiInitLayoutsBridge`: 실측 입력 가드와 최초 helper 클래스 비교.
- `TrickUiInitTrace` / `LogTrickUiBridgeStatus`: helper 5개의 vtable 및 등록 전 첫값을 보관/출력.
- `ValidatePreparedFifthUiHelper`: 준비 당시와 현재의 owner/helper/descriptor 동일성 검사.
- `UpdateStratagemFiveUiRuntimeProbe`: 위 검사를 sidecar 생성 전에도 적용.
- `ExpandMakerAndRegisterFifthSidecarSeh`: 등록 전 첫값==20 검사를 위 동일성 검사로 대체.
  등록 인자는 고정 20/1로 유지하고, ID7 점유 시 중단하며, 성공 판정 때 helper7 소모도 확인.
- `STRATAGEM5_RE_NOTES.md`: 이번 근거와 이전 type20 입력 전제 정정.

### 4. 위험 요소

이번 입력 가드는 확인된 좌표/레이아웃에 한정된다. 다른 입력 형식이면 원본 7칸으로 통과한다.
helper vtable 일치는 클래스 비교 근거이며 전체 ABI/소유권/렌더 경로까지 입증하지는 않는다.
검증 실패 후 maker를 되감거나 재초기화하지 않는다. 확장 호출 예외는 이전처럼 전파한다.
등록 성공 뒤 실제 다섯 번째 표시와 기존 네 버튼 보존은 여전히 실게임 확인 대상이다.
프로세스 재시작/수명 제한과 callback/Open/GetTrickButton 미연결 상태는 유지된다.

### 5. 기존 실패 접근과의 차이

준비가 끝난 maker를 다시 초기화하거나 기존 ID0~6을 재등록하지 않는다.
실제 원본 입력을 그대로 복제하고 게임이 최초 한 번 생성한 helper의 일치 여부를 검증한다.
null helper, 다른 owner/클래스, 초기화 이후 바뀐 descriptor로는 등록하지 않는다.

### 다음 실게임 테스트

1. 게임을 완전히 종료하고 이번 `hid.dll`로 교체한다. 파일 로그 ON 상태로 재실행한다.
2. 전투 진입 후 기존대로 데이터 → 횟수 1 → 내부등록 순서로 켜고 책략창을 연다.
3. 아래 성공 로그와 실제 다섯 번째 버튼 표시/기존 네 버튼 유지 여부를 확인한다.
   다섯 번째 선택/사용은 아직 연결 전이므로 클릭하지 않는다.

```text
[책략5UIHELPER] 초기 입력 형식 검증 성공: 버튼 tag=0, 등록 type=20. ID5 입력을 ID7로 그대로 복제.
[책략5UIHELPER] 초기 InitLayouts 7->8 완료: ... count=8 helper7=...
[책략5UIHELPER] ID7 helper 클래스 검증 성공: 기존 ID2~5와 vtbl 일치.
[책략5UITEST] 원본이 만든 helper7로 ID7 등록 시작: ... descriptorTag=... registerType=20
[책략5UITEST] ID7 정식 등록 성공. helper7 소모 및 5버튼 압축 배치 완료.
```

`descriptorTag`의 생성 후 값은 관찰값으로 출력하며 0/20을 임의로 강제하지 않는다.
실패 시 `[책략5UIHELPER]`, `[책략5UIINIT]`, `[책략5UITEST]`, live Layout/count 로그를 보낸다.
`descriptor-input-shape`면 실패 행/입력값, `helper-class-mismatch`면 5개 vtable과 등록 전
첫값, 등록 실패면 stage/lookup/id/state/helper7이 다음 판단 근거다.

로컬에서는 실측 prefix를 넣은 smoke test 13개가 통과했다. 원본/입력 보존, 잘못된 tag/좌표
거부, helper 클래스 불일치·소모·교체·descriptor 변조 시 등록 차단 등을 확인했다.
게임 원본 InitLayouts/등록/화면 표시를 대체 함수 검사로 검증했다고 해석하지 않는다.
이 PC의 MSBuild `Release|x64` / `hid` 빌드·링크도 종료 코드 0을 확인했다.


---

## 31. 2026-09-23 ID7 정식 등록 성공 후 화면은 여전히 4개 — Dialog::Open 표시 경로 확장

아스트라 브랜치 실게임에서 다음 단계까지 성공했다.

```text
expanded=1
maker count=8
ID7 helper class == 기존 ID2~5 helper class
RegisterLayout(maker,7,sidecar,20,1) 성공
helper7 소모 확인
lookup(7) == sidecar
sidecar UI ID=7
state=1
```

즉 **registry/helper 문제는 통과했다.**

하지만 실제 책략창에는 여전히 기존 네 버튼만 보였다.

### 남은 표시 병목

이미 14~15절에서 확인한 `TrickCommandDialog::Open`의 UI 처리:

```asm
cmp edi,4
jae skip_button_logic
...
mov rsi,[rax+rsi*8+0x1E0]
...
SetTrickID(...)
```

데이터 루프는 5번째까지 진행할 수 있지만 index 4의 버튼 UI 처리만 건너뛴다.

단순 `cmp 4 -> 5`만 하면 index 4에서:

```text
layout + 0x1E0 + 4*8 = layout + 0x200
```

을 읽게 되므로 금지한다. `+0x200`은 m_pButtons 다음 멤버다.

### 이번 최소 표시 실험

`Dialog::Open` 함수 범위(0x241 bytes) 안에서 다음 쌍을 **정확히 한 개만** 찾는다.

1. `cmp edi,4` + 바로 이어지는 `jae skip`
2. 그 분기 내부의 정확한
   `mov rsi,[rax+rsi*8+0x1E0]`

둘이 유일하게 확인될 때만 패치한다.

패치 순서:

1. direct array load를 cave로 먼저 교체한다.
2. cave에서 index 0~3은 원래 load를 그대로 실행한다.
3. index 4는 `g_fifthUiSidecarButton`을 RSI에 넣는다.
4. sidecar가 null이면 원래 JAE skip target으로 복귀한다.
5. 위 우회가 먼저 활성화된 뒤에만 해당 단일 `cmp edi,4`의 immediate를 5로 바꾼다.

따라서 index 4에서 `layout+0x200`을 읽는 순간은 없다.

ID7 등록 직후에는 RegisterLayout이 초기 visibility를 바꿀 가능성도 고려해
sidecar의 virtual visible setter(+0x108)를 다시 true로 호출한다.

### 이번 성공 기준

ID7 등록 후:

```text
[책략5UIOPEN] Dialog::Open index4 sidecar 표시 훅 설치 성공: ...
[책략5UITEST] Dialog::Open index4 sidecar 표시 훅=READY
```

를 확인한 뒤 **책략창을 닫았다가 다시 연다.**

목표는 다섯 번째 카드가 화면에 보이는지만 확인하는 것이다.
callback / GetTrickButton(4) / 실제 선택·사용은 아직 연결하지 않는다.
따라서 다섯 번째가 보여도 클릭하지 않는다.


---

## 32. 2026-09-23 마우스가 4개만 인식 — GetTrickButton 완전 대체 1차

ID7 registry와 Dialog::Open index4 sidecar 우회는 성공했고 화면에도 다섯 번째 자리 흔적이 생겼다.
그러나 마우스 반응은 기존 4개뿐이었다.

이번 단계부터 조각별 단순 count 확장보다 작은 함수는 완전 대체한다.

### GetTrickButton 완전 대체

원본은 0x13 bytes뿐이다.

```asm
cmp edx,4
jae out
mov eax,edx
mov rax,[rcx+rax*8+1E0]
ret
out:
xor eax,eax
ret
```

지원 빌드의 전체 0x13 bytes가 정확히 일치할 때만 entry를 near-JMP cave로 바꾼다.

새 동작:

- index 0..3: 원본 배열 접근 그대로
- index 4: `g_fifthUiSidecarButton`
- index >4: null

따라서 +0x200을 절대 읽지 않는다.

### callback 다음 단계 준비

실제 마우스 select/focus callback은 `Dialog::Initialize`에서 4회만 연결된다.
이번 빌드는 해당 함수 안에서 이미 RE로 확인한 loop tail:

```asm
inc edi
add r14,8
cmp edi,4
jb loop
```

의 정확한 런타임 후보 수와 주변 바이트를 `[책략5UICB]`로 함께 출력한다.

이 결과로 다음 패치에서는 loop의 5번째 iteration에서 r14를 +0x200으로 보내지 않고
`&g_fifthUiSidecarButton` 슬롯으로 돌려 게임 원본 callback body를 그대로 한 번 더 실행시키는
tail detour를 설치한다.


---

## 33. 2026-09-23 핵심 관문 — Dialog::Initialize 원본 callback body를 5번째에 그대로 실행

직전 GetTrickButton 완전 대체만으로는 화면/마우스 반응이 달라지지 않았다.
이 결과는 예상 가능한데, 실제 focus/kill-focus/select 시그널은
`TrickCommandDialog::Initialize`의 별도 4회 루프에서 버튼마다 연결되기 때문이다.

과거 실게임 덤프를 다시 정확히 역어셈블한 결과 callback 구간은 다음과 같다.

```asm
; r12 = 0, edi = 0, r14 = 0x1E0
+1FD cmp r12d,4
+203 mov rax,[rsi+8]          ; layout
+207 mov rbx,[r14+rax]        ; m_pButtons[index]
...
; focus callback AddSig
; kill-focus callback AddSig
; select callback AddSig
...
+355 inc edi
+357 add r14,8
+35B cmp edi,4
+35E lea rcx,...
+365 jb +203
```

특히 callback closure를 만들 때 `edi` 값 자체를 캡처하므로,
원본 body를 index 4로 한 번 더 실행시키는 것이 가장 원본에 가까운 방식이다.

### 조기 설치가 필요한 이유

Dialog::Initialize는 사용자가 실험 버튼을 누르기 전에 이미 실행된다.
따라서 ID7 등록 후 뒤늦게 loop를 패치해도 callback은 생기지 않는다.

이번 변경은 DLL 시작 직후 세 훅을 모두 설치한다.

1. 기존 InitLayouts 7→8 bridge
2. Dialog::Initialize +0x1C4 pre-callback hook
3. callback loop의 sidecar-safe 5회 확장

pre-callback hook은 Layout::Initialize가 끝난 직후, callback loop 시작 전에 호출된다.
이 시점에 원본 helper7가 아직 남아 있으므로:

- descriptor 좌표에서 start/step/y를 읽음
- sidecar TrickSelectButton 생성
- helper7로 ID7 RegisterLayout
- GetTrickButton/Open sidecar hook 설치

까지 끝낸다.

그 뒤 callback loop는 `cmp edi,4 -> 5`로 한 번 더 돈다.
단, index 4에서 `layout+0x200`을 읽지 않는다.

```text
index 0..3 -> 원본 [layout+0x1E0 + index*8]
index 4    -> g_fifthUiSidecarButton
sidecar null -> 원본 callback loop 즉시 종료
```

따라서 다섯 번째 iteration에서 게임 원본 focus/kill-focus/select callback body가
그대로 실행되고 closure에는 index 4가 캡처된다.

### 이번 테스트 성공 기준

전투 진입 후 별도의 책략창 선행 오픈 없이도 로그에:

```text
[책략5UICB] callback 직전 sidecar 준비 완료
[책략5UITEST] ID7 정식 등록 성공
```

가 나타나야 한다.

이후 기존 테스트대로 ID5 데이터/횟수/내부등록을 적용하고 책략창을 열어
**5번째 위치에 마우스를 올렸을 때 hover/focus 반응이 생기는지만 먼저 확인한다.**

이번에도 hover가 전혀 없다면 다음 병목은 callback이 아니라 parent의 hit-test/child-control
등록 경로로 좁혀진다. 그 경우 계속 진행 여부를 다시 판단한다.


---

## 34. 2026-09-23 callback 준비 성공 이후: sidecar 생성 시점을 Layout::Initialize 내부로 이동

실게임 로그에서 InitLayouts 7->8, helper7, sidecar 생성, ID7 등록,
GetTrickButton/Open sidecar 우회, callback 직전 준비까지 모두 성공했다.

그러나 화면/마우스 반응이 여전히 없었다.

따라서 다음 가설은 **sidecar 생성 시점이 너무 늦다**는 것이다.
원본 4개 버튼은 `TrickCommandDialogLayout::Initialize` 내부에서 생성된 뒤,
같은 함수의 후속 child/control 초기화와 finalize 단계를 함께 통과한다.
기존 sidecar는 이 함수가 끝난 뒤 Dialog::Initialize에서 생성했으므로
그 후속 lifecycle에서 빠졌을 가능성이 있다.

이번 변경은 원본 4버튼 생성 루프가 끝난 직후인
`Layout::Initialize +0xCF3`의 정확한 direct call을 build-guarded hook한다.

그 call 직전에:

- helper7/count8 검증
- sidecar 생성
- ID7 RegisterLayout
- Open/GetTrickButton sidecar hook 준비

를 끝낸 뒤, 원래 +0xCF3 call과 나머지 Layout::Initialize 코드를 그대로 진행한다.

또 callback loop의 index4 sidecar 분기 실제 진입 횟수를 카운트한다.

성공 기준:

```text
[책략5UILAYOUT] Layout::Initialize 내부에서 5번째 생성/ID7 등록 완료
[책략5UICB] live callback index4Hits=1 이상
```

두 조건이 맞는데도 hover/render가 없으면 다음 병목은 별도 parent child/hit-test container로 좁혀진다.


---

## 35. 2026-09-23 Layout post-buttons hook 오프셋 수정

실게임 로그에서 조기 훅 준비가 다음 상태였다.

```text
init=1
layoutPost=0
preCallback=1
callbackLoop=1
```

원인은 `Layout::Initialize` post-buttons hook 주소를 +0xCF3으로 잡은 단순 오프셋 오류였다.

기존 런타임 덤프:

```text
+CEB : 0F 85 A5 FE FF FF
+CF1 : E8 8A 4E 26 FE
```

따라서 direct CALL의 실제 시작은 +0xCF1이다.
이번 수정은 hook 위치만 +0xCF3 -> +0xCF1로 바로잡는다.


---

## 36. 2026-09-23 최초 시각/입력 성공 — 5번째 카드 렌더 + hover 확인

실게임 화면에서 처음으로 sidecar 카드가 실제 카드 그래픽/텍스트와 함께 렌더되었고,
마우스를 올렸을 때 hover/focus 반응도 확인됐다.

이는 다음이 실제로 연결되었다는 뜻이다.

- Layout 초기화 수명 안의 sidecar
- ID7 registry
- Dialog::Open index4
- Dialog::Initialize index4 callback 3종
- parent hit-test/input 경로

따라서 "5번째 버튼을 정상 UI 객체로 편입"하는 핵심 관문은 통과했다.

현재 남은 시각 문제는 원본 ResetBtnPos가 기존 4개만 280 간격으로 다시 배치하면서,
sidecar와 4번째 버튼이 겹치는 것이다.

이번 변경은 ResetBtnPos의 4개 원본 배치가 끝난 직후(+0x1F2)에
5개 전체를 다시 균등 배치한다.

현재 빌드 기준 간격은 280의 7/8 = 245:

```text
336, 581, 826, 1071, 1316 / y=364
```

으로 잡아 1280폭 화면에서 5개가 거의 한 줄에 맞도록 한다.
이 재배치는 ResetBtnPos가 호출될 때마다 마지막에 실행되므로
게임이 원본 4개 위치를 다시 덮어써도 최종 상태는 5개 배치가 된다.

다음 확인은:
1. 다섯 카드가 모두 분리되어 보이는지
2. 5번째 hover 유지
3. 그 뒤 5번째 클릭 -> 상세 패널/선택 상태가 ID5로 넘어가는지


---

## 37. 2026-09-23 5번째 렌더/hover/클릭 성공, 선택 전환은 4개 제한에 막힘

실게임에서 다음이 확인됐다.

- 5개 카드 분리 렌더 성공
- 5번째 hover/focus 반응 성공
- 5번째 클릭 시 버튼 자체의 체크/선택 표현 성공
- 그러나 하단 설명은 4번째 사모위계에 머물고 실제 선택 전환도 진행되지 않음
- callback loop index4Hits=1, ID7 등록/sidecar/Open/GetTrickButton 모두 정상

따라서 버튼 객체/입력 문제가 아니라 **Dialog 선택 처리의 별도 4개 bounds check**가
다음 병목으로 판단된다.

PDB에서 이미 확인된 작은 함수:

`TrickCommandDialog::OnTrickSelect`
- RVA 0x01DF37B0
- size 0x4C

이번 패치는 이 함수 전체 0x4C를 런타임 검증하여:

1. direct `m_pButtons +0x1E0` 접근이 없어야 함
2. `Layout::GetTrickButton` direct call이 정확히 1개여야 함
3. `cmp r32,4`가 정확히 1개여야 함

세 조건이 모두 맞을 때만 그 단일 immediate 4를 5로 확장한다.

GetTrickButton(4)는 이미 sidecar를 반환하도록 완전 대체되어 있으므로,
이 경로는 +0x200을 읽지 않는다.

성공 로그:

```text
[책략5UISEL] OnTrickSelect index 범위 4->5 확장 성공
```

이번 확인 목표는 5번째 클릭 시 하단 설명/현재 선택이 ID5로 전환되는지다.
