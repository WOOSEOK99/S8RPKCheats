# Step 05 - AI 포로 처형 조건 상세 해부

## 0. 범위

이 문서는 `SAN8RPK_Viewer.exe`의 최종 AI profile에서 **포로 석방/처형 판정 부분**만 분리한 정적 분석 기록입니다.

이번 Step에서 확정한 것:

- 포로 판정 hook 주소와 원본 바이트
- 최종 `HATRED_PRISON` payload 구조
- 장수 구조 `+0x5D` / `+0x5E`의 의미
- 상성 거리 계산
- 의리값에 따른 임계값 보정
- 한쪽 인물이 군주인지 판정하는 방식
- 별도 관계 classifier hook
- 최종 석방/처형 카운터가 어느 조건에서 증가하는지

아직 미확정인 것:

- `+1712670`, `+17B0AB0`, `+17B4570`, `+17A8270` native 함수의 정확한 게임 내부 명칭
- hook 진입 시 `rdi` / `rsi`의 공식 역할명
- 최종 비교에 쓰이는 incoming `ecx`가 정확히 어떤 난수/판정값인지

---

## 1. Viewer 자체 설명 - 확정

Viewer UI에는 AI 기능 설명이 다음과 같이 들어 있습니다.

```text
AI 개선: 공격 후보 확장 · 포로 판정 · 고의리 군주 항복 억제
```

AI 상태창은:

```text
공격·포로 AI 켜짐 (플레이어 우선 가산 제거)
...
석방 %d / 처형 %d
```

라고 표시합니다.

따라서 포로 판정 변경은 게시글 설명만이 아니라 Viewer 자체 기능 구성요소입니다.

---

## 2. 최종 포로 hook - 확정

### Hook RVA

```text
SAN8RPK.exe + 0x01E52A8D
```

### 원본 16 bytes

```text
3B CD
48 8B 6C 24 60
0F 9C C0
E9 05 FE FF FF
CC
```

해석:

```asm
cmp  ecx, ebp
mov  rbp, [rsp+60h]
setl al
jmp  0x01E528A1
int3
```

원래 코드도 마지막에 `ecx`와 `ebp`를 비교한 뒤 앞쪽 `+1E528A1`로 되돌아가는 구조입니다.

Viewer는 이 16 bytes를 cave jump로 교체하고, 최종 cave에서도 계산을 마친 뒤 동일한:

```text
SAN8RPK.exe + 0x01E528A1
```

로 복귀합니다.

즉 기존의 단순 `ecx vs ebp` 비교에서 **`ebp` 대신 Viewer가 새 임계값을 계산하는 형태**로 이해할 수 있습니다.

---

## 3. 최종 payload 위치 - 확정

최종 `build_payload()` 기준:

```text
cave + 0x000 : AI 공격
cave + 0x200 : 포로 판정 HATRED_PRISON
cave + 0x400 : 항복권고 pre
cave + 0x600 : 항복권고 post
cave + 0x800 : HATRED_CLASSIFIER
cave + 0xA00 : 항복 확률 보정
```

포로 hook `+1E52A8D`는 cave `+0x200`으로 들어갑니다.

---

## 4. 최종 HATRED_PRISON marker 매핑 - 확정

`hatred_code()` Python bytecode를 복원하면:

| marker | 실제 값 |
|---|---|
| `1111111111111111` | stats = `cave + 0x1000` |
| `2222222222222222` | `SAN8RPK.exe + 0x01E528A1` |
| `3333333333333333` | `SAN8RPK.exe + 0x02E98BC8` |
| `4444444444444444` | `SAN8RPK.exe + 0x01712670` |
| `5555555555555555` | `SAN8RPK.exe + 0x017B0AB0` |
| `6666666666666666` | `SAN8RPK.exe + 0x017B4570` |
| `7777777777777777` | `SAN8RPK.exe + 0x017A8270` |
| `8888888888888888` | `SAN8RPK.exe + 0x01E541A8` |

`+2E98BC8`은 Viewer의 read-only `reader.py`에서도 **`MANAGER_RVA`**로 정의되어 있습니다.

---

## 5. +0x5D / +0x5E 의미 교차 확인 - 확정

Viewer의 장수 조회기 `reader.py`는 장수 구조 앞 96 bytes를 읽은 뒤 다음처럼 값을 추출합니다.

```text
data[93]        -> compatibility
data[94] & 0xF -> honor
```

UI 열 제목:

```text
honor         = 의리
compatibility = 상성
```

따라서:

```text
장수 +0x5D = 상성
장수 +0x5E 하위 4비트 = 의리
```

는 EXE 내부의 조회 코드로 직접 확인됩니다.

---

## 6. 군주 판정 구조 - 확정

Viewer의 `reader.py`는 장수 `+0x10`, `+0x18`, `+0x20`을 각각:

```text
status_ptr
force_ptr
city_ptr
```

로 읽습니다.

또 세력 객체의:

```text
force + 0xC0
```

를 현재 군주 장수 포인터로 읽습니다.

샘플 snapshot에서도:

```text
status_id 1 = 군주
```

가 직접 확인됩니다.

최종 포로 payload는 `rdi` 쪽 인물에 대해:

```asm
mov rax,[rdi+10h]
...
cmp byte ptr [rax+8],1
```

또는:

```asm
mov rax,[rdi+18h]
...
cmp [rax+0C0h],rdi
```

를 검사합니다.

따라서 이 분기는 확실히:

> **rdi 쪽 인물이 현재 군주인가?**

를 판정합니다.

포로 처리 함수라는 문맥상 `rdi=포로, rsi=처분 주체`일 가능성이 높지만, 레지스터 역할명은 게임 함수 전체를 확인하기 전까지 **유력**으로만 기록합니다.

---

## 7. 기본 상성 거리 계산 - 확정

payload 시작:

```asm
movzx eax, byte ptr [rdi+5Dh]
movzx edx, byte ptr [rsi+5Dh]
sub eax,edx
abs(eax)

mov edx,150
sub edx,eax
cmp eax,edx
cmovg eax,edx
```

즉 상성은 0~149 원형 좌표로 취급하며 거리:

```text
d = min(abs(A-B), 150-abs(A-B))
```

를 계산합니다.

그 다음:

```asm
r8d = 100 - d
```

이 값을 이후 처형 판정의 기본 임계값으로 사용합니다.

따라서:

- 상성이 가까울수록 기본값이 큼
- 상성이 멀수록 기본값이 작음

입니다.

이 값이 최종적으로 `incoming ECX`와 비교되지만 ECX의 정확한 의미가 아직 미확정이므로 여기서는 이를 **판정 임계값 T**라고 부릅니다.

---

## 8. 의리값 H - 확정

```asm
movzx edx, byte ptr [rsi+5Eh]
and edx,0Fh
```

따라서:

```text
H = [rsi+0x5E] & 0x0F
```

이고 Viewer 조회기의 명칭대로 **의리**입니다.

---

## 9. 최종 임계값 공식 - 확정

변수:

```text
A = [rdi+0x5D] 상성
B = [rsi+0x5D] 상성
d = min(abs(A-B), 150-abs(A-B))
H = [rsi+0x5E] & 0x0F
R = rdi 쪽 인물이 군주인가
F = [rsi+0x30] == 1
```

초기값:

```text
T = 100 - d
```

### R = false

의리 H가 11 미만:

```text
T += H * 3
```

의리 H가 11 이상:

```text
T += H * 5
```

### R = true

먼저:

```text
T -= 25
```

그 뒤 H < 11:

```text
T += H
```

H >= 11:

```text
T += H * 6
```

### +0x30 보정

```text
if [rsi+0x30] == 1:
    T += 10
```

`+0x30`의 정확한 필드명은 현재 자료만으로 확정하지 못했습니다.

### clamp

```text
T = clamp(T, 0, 100)
```

### 군주 특수 보정

clamp 결과가 정확히 100이고 `R=true`이면:

```text
T = T + H - 15
  = 85 + H
```

즉 H=15이면 100 유지, H가 낮을수록 100 아래로 내려갑니다.

---

## 10. 관계 helper 체인 - 구조 확정 / 의미 미확정

payload는 위 계산 후 다음 native 함수 체인을 호출합니다.

```asm
rcx = *(base + 0x2E98BC8) + 0x72C8
edx = WORD [rsi+8]   ; 한쪽 장수 ID
r8d = WORD [rdi+8]   ; 다른쪽 장수 ID

call base+0x1712670
ecx = al
call base+0x17B0AB0
```

즉:

1. Manager 내부 `+0x72C8` 영역
2. 두 장수 ID
3. `+1712670`
4. 반환 byte를 `+17B0AB0`에 전달

하는 관계 판정 체인입니다.

두 함수의 정확한 이름은 아직 미확정입니다.

---

## 11. 관계 helper 결과에 따른 추가 보정 - 확정

두 번째 helper `+17B0AB0`의 반환값이 **0일 때만** 추가 보정합니다.

H < 11:

```text
T = 0
```

H = 11, 12, 13:

```text
T = floor(T / 2)
```

H = 14:

```text
T = floor(T * 66 / 100)
```

H = 15:

```text
T = floor(T * 75 / 100)
```

반환값이 0이 아니면 이 추가 축소는 하지 않습니다.

이 helper의 boolean 의미가 정확히 무엇인지는 아직 이름을 붙이지 않습니다.

---

## 12. 최종 석방/처형 카운터 - 확정

Viewer는 hook 진입 시:

```asm
lock inc qword ptr [stats+20h]
```

를 수행합니다.

Viewer 상태 변수 매핑:

```text
stats+0x20 = captive
stats+0x28 = released
stats+0x30 = executed
```

최종 판정:

```asm
cmp ecx,r8d
setge al

test al,al
je release_counter

lock inc [stats+30h] ; executed
jmp done

release_counter:
lock inc [stats+28h] ; released
```

따라서 Viewer 내부 의미는:

```text
incoming ECX >= T  -> executed++
incoming ECX <  T  -> released++
```

입니다.

다만 `ECX`가 정확히 `0~99 난수`인지, 다른 점수값인지에 대한 upstream 코드는 이번 자료에 없으므로 **정확한 확률 %로 환산하지 않습니다.**

---

## 13. 초기 PRISON_TEMPLATE과 최종 HATRED_PRISON 차이

Viewer profile에는 발전 과정의 이전 템플릿도 남아 있습니다.

### 초기 PRISON_TEMPLATE

크기:

```text
178 bytes
```

주요 요소:

- 상성 원형거리
- 의리값
- rdi 군주 여부
- +0x30 flag
- clamp
- 최종 ECX 비교

관계 native helper 호출은 없습니다.

### 최종 HATRED_PRISON

크기:

```text
378 bytes
```

추가 요소:

- 군주 케이스 계산 변경
- 관계 helper 체인
- helper 결과에 따른 의리별 추가 임계값 축소
- 군주+T=100 특수 보정

그리고 `build_prisoner_payload()` 자체를 나중에 다시 정의하여 최종 빌드에서는 **HATRED_PRISON이 사용됩니다.**

즉 EXE 안에 초기 공식이 남아 있어도 실제 최종 payload는 더 발전된 378-byte 버전입니다.

---

## 14. 별도 HATRED_CLASSIFIER hook - 확정

Viewer는 포로 최종 판정 hook 외에 별도 hook을 하나 더 설치합니다.

### RVA

```text
SAN8RPK.exe + 0x01E54190
```

### 원본 16 bytes

```text
C7 44 24 44 01 01 00 00
4C 8D 44 24 44
48 8B D3
```

해석:

```asm
mov dword ptr [rsp+44h],101h
lea r8,[rsp+44h]
mov rdx,rbx
```

Viewer는 cave `+0x800`의 `HATRED_CLASSIFIER`로 보냅니다.

복귀:

```text
SAN8RPK.exe + 0x01E541A8
```

즉 hook start에서 0x18 bytes 뒤로 복귀합니다.

---

## 15. HATRED_CLASSIFIER 제어 흐름 - 확정

classifier cave의 핵심:

```asm
mov [rsp+44h],101h
sub rsp,30h
mov [rsp+20h],101h

lea r8,[rsp+20h]
mov rdx,rbx
mov rcx,r14
call +17B4570
test al,al
je end

movzx edx,byte ptr [r14+5Eh]
and edx,0Fh
cmp edx,0Bh
jl end

mov rcx,r14
mov rdx,rbx
call +17A8270
test al,al
jne end

rcx = manager+72C8
edx = id(r14)
r8d = id(rbx)
call +1712670
ecx = al
call +17B0AB0
test al,al
setne al

end:
...
jmp +1E541A8
```

즉 최종 true가 되려면 최소한:

1. `+17B4570(r14,rbx,...)`가 true
2. `r14 의리 >= 11`
3. `+17A8270(r14,rbx)`가 false
4. 관계 helper chain `+1712670 -> +17B0AB0`가 nonzero

조건을 모두 통과해야 합니다.

이 함수가 정확히 무엇을 분류하는지는 profile 이름이 `HATRED_CLASSIFIER`라는 점까지는 확정이지만, 각 helper의 의미는 아직 미확정입니다.

---

## 16. '군주 내면/의리 + 상성' 설명과의 대응

사용자 제공 게시글 설명:

> 군주의 내면과 포로와의 상성에 맞게 처형하도록 변경

EXE 내부에서 직접 확인되는 값은:

- 상성: `+0x5D`
- 의리: `+0x5E & 0xF`
- 한쪽 인물의 군주 여부
- 장수 간 관계 helper

입니다.

따라서 게시글의 큰 방향은 코드와 잘 맞습니다.

다만 Viewer가 실제로 사용하는 명칭은 **'의리'**이므로 문서에서는 '내면'보다 `의리(+0x5E)`를 우선 사용합니다.

---

## 17. 하드코딩 장수 목록 여부

최종 Viewer-added `HATRED_PRISON` / `HATRED_CLASSIFIER` 자체에는:

- 특정 장수 ID 목록
- 특정 이름 목록

이 들어 있지 않습니다.

추가 로직은 장수 구조 필드와 native 관계 helper를 사용합니다.

따라서 Viewer가 새로 추가한 부분은 **개별 장수 ID 하드코딩이 아니라 데이터 기반 판정**입니다.

단, 원래 게임 코드의 다른 위치에 남아 있는 기존 특례까지 완전히 제거했는지는 이 payload만으로는 확정할 수 없습니다.

---

## 18. 현재 S8RPKCheats와의 관계

현재 S8RPKCheats 검색에서 다음 Viewer 포로 RVA는 사용하지 않습니다.

```text
+1E52A8D
+1E54190
+1712670
+17B0AB0
```

현재 프로젝트의 `도독 포로 직접 처분`은 **플레이어 도독 승전 후 처분 선택권**을 추가하는 별도 기능이며, Viewer의 **AI 포로 자동 석방/처형 판단 공식**과는 성격이 다릅니다.

따라서 주소 충돌은 현재 확인되지 않았습니다.

향후 Viewer 포로 로직을 이식하더라도 기존 도독 기능과 기능 목적은 분리해서 다루는 것이 맞습니다.

---

## 19. Guard 영역 - 확정

초기/최종 포로 로직 관련 guard:

| RVA | size | SHA-256 |
|---:|---:|---|
| `+1E52740` | 864 | `af64f07a1c0a895430aae404aae2b9441577846a4ba2f911ecc8433af1b2aacc` |
| `+1711B00` | 7 | `3fe825b42d30a9c9430d8fca20b1d2cb83f3e4347381df2ab306af2a84fc9aba` |
| `+175B1A1` | 52 | `04fea700fe18f9652b411f477e5bfe16a81c997c193088062364e7e5c70c7e70` |
| `+1E54140` | 112 | `d6c31dafe3ccd6d9010100d68147a88bd5d92a5afa5720e59f28cf18050a6b30` |
| `+17B4570` | 339 | `a06654ad7d5387899d1f62bdc73b3a00c9ea28b9855e5ea90b2d5369f2d5e237` |
| `+17A8270` | 192 | `c8773aec69fbe172f9412e23c071fb7408b3ee38357c5afdeea669b48fc88a5b` |
| `+1712670` | 128 | `4a01e26e24bcd21ea63d45270d0813481f80fcfa5b8fd765e38a2aac9de844af` |
| `+17B0AB0` | 232 | `a4e5e4ba929d43929f9f101d99d6f090aa732a18aa01df5bb2211c776bd45720` |

Viewer는 이러한 주변 코드 hash를 검증하고 맞지 않으면 적용을 거부합니다.

---

## 20. Step 05 결론

### 확정

Viewer의 최종 AI 포로 판정은 단순한 특정 장수 목록이 아니라:

- 양쪽 장수의 **상성**
- `rsi` 쪽 **의리**
- `rdi` 쪽 **군주 여부**
- `rsi+0x30` flag
- 두 장수의 **native 관계 판정**

을 이용해 새 임계값을 계산합니다.

최종적으로:

```text
ECX >= 계산 임계값 -> Viewer 통계상 처형
ECX <  계산 임계값 -> Viewer 통계상 석방
```

으로 분류합니다.

### 유력

함수 문맥과 게시글 설명을 합치면:

- `rdi`는 포로
- `rsi`는 포로 처분을 결정하는 군주/AI 측 인물

일 가능성이 높습니다.

이 역할 매핑은 실제 게임 함수 진입 레지스터를 런타임 read-only 로그로 한 번 확인하면 확정할 수 있습니다.

### 다음에 필요한 것

나중에 이식 단계 전에 다음을 확인해야 합니다.

1. hook 진입 시 `rdi/rsi` 장수 ID와 이름을 read-only 로그로 출력
2. incoming `ecx` 값 범위 로그
3. 계산한 `T`, 의리 H, 상성거리 d, 군주 flag, 관계 helper 결과 로그
4. 실제 게임에서 석방/처형 결과와 비교

이 네 가지가 맞으면 Viewer 공식을 거의 그대로 재현할 수 있습니다.
