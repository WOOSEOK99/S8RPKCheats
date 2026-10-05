# CT4 92011 정밀 분석 — 원본 의미, 이식 차이, A/B 진단 설계

분석일: 2026-09-28. 게임 코드 변경·실게임 실행·커밋·머지를 수행하지 않았다.

분석 기준:

- 로컬 HEAD와 GitHub 원격 `feature/isolated-territory-movement` 모두 `9f5c5523aa04670f7ba6e57cf2ffe9d3a1ef758b`.
- `x64/Release/SAN8RPK-command-code-1.CT`. 사용자가 요청의 `-1(2).CT`와 같은 파일이라고 확인했다.
- CT SHA-256: `b25bffc80b379e15c2bf17a2835889a1b500522248c4df342e90287e83a9f763`.
- ID 92011 전체: Lua 623줄, blob 8,810바이트, relocation 23개, game guard 18개, hook 10개, DLL signature 4개. XML에서 추출하고 코드 영역과 guard를 역어셈블했다.
- 비교 소스: [IsolatedTerritoryMovement.h](../../Internal%20DX11%20Base/Cheats/War/IsolatedTerritoryMovement.h), [PrisonerCaptureManagement.cpp](../../Internal%20DX11%20Base/Cheats/War/PrisonerCaptureManagement.cpp), [기존 정적 진단](../../Internal%20DX11%20Base/Cheats/War/IsolatedTerritoryMovementDiagnostic.h).
- 근거: [원본 Lua](ct4_original.lua), [역어셈블리](ct4_disassembly.txt), [manifest](manifest.json), [MSVC 출력](abi_probe.asm), [오프라인 재현 결과](abi_repro_result.txt), [version.dll 출력](version_wrapper_disassembly.txt).

**확정**은 정적 바이트·소스 또는 명시한 오프라인 재현으로 입증한 사실이다. **강한 후보**는 실게임 증상을 설명하지만 해당 실행 로그가 없는 원인이다. **추정**은 객체나 UI의 이름 등 추가 근거가 필요한 해석이다. 이 보고서의 `source/destination`은 특별한 설명이 없으면 CT BFS의 첫째/둘째 인자이며, 화면에서 사용자가 선택한 출발/도착 도시라는 이름까지 입증했다는 뜻은 아니다.

## A. 현재 증상에 대한 결론

**확정: 이식에는 C++ `bool` 반환과 기계어 분기의 ABI 불일치가 있다.**

원본 blob의 BFS·리스트·문맥 helper는 `EAX=0/1`을 만든다. C++의 `AreConnected`, `FilterConnectedList`, `MovementContextAllowed`는 `bool`을 반환한다. 그런데 생성 스텁 1/2/3/4/5/6은 여전히 `85 C0` (`test eax,eax`)를 사용한다. `bool`의 유효 반환값은 AL이며 RAX의 미사용 비트는 보장되지 않는다. [Microsoft x64 반환 규약](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170#return-values).

실제 현행 헤더를 MSVC 14.51.36231, `/O2 /std:c++20`로 별도 컴파일했다. `AreConnected`의 false 출구는 `xor al,al`이고, EAX 전체를 지우지 않는다. 게임이 아닌 가짜 객체로 현행 함수 자체를 호출한 결과:

```text
isolated_to_mainland  AL=0  test_EAX=1  expected=0
mainland_to_isolated  AL=0  test_EAX=1  expected=0
mainland_to_mainland2 AL=1  test_EAX=1  expected=1
same_city            AL=1  test_EAX=1  expected=1
```

**강한 후보: 실제로는 false인 연결 판정을 스텁이 true로 소비해 일부 경로가 통과한다.** 다만 이 오프라인 재현은 양쪽 단절 방향에서 모두 발생한다. 실게임의 “단절지→본토만 차단”은 방향마다 지나가는 훅/분기 차이와 결합해야 설명된다. 실제 로드한 DLL의 최종 최적화 출력과 A/B 로그가 없으므로, 이 결함을 실게임의 유일한 원인이라고 확정하지 않는다.

추가로 확정한 사항:

- 양방향 실험은 `PrisonerCaptureManagement.cpp:54`에서 **gHook78Thunk만** 바꾼다. 1~6·9/10에는 적용되지 않는다. 실험 실패로 전체 BFS 방향성을 배제할 수 없다.
- 9/10의 “동일 소유자 연결 이웃 존재” 검사는 원본도 같다. 단순 오역으로 보고 출발지–목적지 검사로 바꾸면 안 된다.
- 4/5는 현재도 마지막 CALL 5바이트만 바꾸지만, 앞의 인자 구성 20바이트는 원래 위치에서 실행된다. 이것만으로 필터 누락 또는 인자 유실이라고 볼 수 없다.
- 현재 C++은 version.dll 없이 적용 가능한 구조다. 다만 이미 후킹된 +17B0BA0에 대한 optional bridge는 구현하지 않았다.

## B. 원본 CT4 전체 실행 구조

원본 Lua의 실제 초기화와 요구하는 최종 설계를 구분해야 한다.

1. CE 7.2 이상·64비트, 프로세스와 EXE base, PE timestamp `1772519121` 및 image size `71364608` 확인.
2. 프로세스 생성 시각까지 포함한 session identity로 재접속·PID 재사용을 구분. 이전 활성 패치의 복구 기록 유지.
3. 같은 게임 폴더의 version.dll/hid.dll을 찾아 MD5 검사. **Lua 414줄은 두 DLL이 모두 있어야 한다고 assert한다.** 416줄은 version signature를 항상 검사한다. feature dependency가 nil인 것과 DLL 비필수는 별개다.
4. guard 18개 검사. 활성 패치와 인식된 version 훅은 원본으로 정규화한 뒤 비교한다.
5. EXE 근처에 `0x20000`바이트를 할당하고 blob 및 23개 relocation을 쓴다. 해당 relocation은 모두 EXE base 기준이다.
6. hook6 caller 읽기를 blob+F00 helper 호출로 바꾸어 version wrapper의 caller 이동을 보정한다. 현재 CT Lua는 이 보정 코드에도 version 주소를 항상 넣는다.
7. 1~6은 14바이트 absolute JMP와 필요한 NOP, 7~10은 rel32 CALL. 다만 version이 hook6을 이미 소유하면 EXE entry 대신 version+14FE38의 포인터 slot만 교체한다.
8. 실제 쓰기 전 스레드를 정지하고 RIP이 수정 구간에 없는지 확인한다. 원본 재검사·쓰기 후 검증·instruction cache flush·보호 복원, 실패 시 시도한 쓰기를 역순 복구한다.
9. disable은 자기 패치가 여전히 설치되어 있는지 검사한 뒤 before 값을 복구한다. bridge면 원래 trampoline 포인터를 복구한다. cave는 실행 중인 복귀 주소를 위해 프로세스 종료까지 보유한다.

런타임 helper 계층:

| blob offset | 확인된 역할 |
|---|---|
| +628 | 객체 null 확인 후 `vtable+48` 호출, AL을 `movzx eax,al`로 정규화 |
| +64A | `[object+90]` holder 및 `[holder+10]` root 각각 유효성 검사 |
| +677 | listRef+8의 index 포인터와 EXE 전역 table로 head node 복원 |
| +6B2 | 같은 root의 도시 포인터 6개를 따라 BFS, 큐 256개 |
| +7B1 | 객체 리스트의 각 object+20 도시를 target과 비교하고 불연결 항목 제거 |
| +874 | context·owner 일치 조건을 만족하는 동일 세력 이동에만 BFS 적용 |
| +2000 | native 판정 후 mode5==0인 경우 object+20→두 번째 인자 연결 판정 |
| +2200 | native 판정 후 두 번째 인자 도시의 비자기 연결 이웃 존재 판정 |

이들은 교체 가능한 같은 helper가 아니다. 동일 세력 이동인지, 객체 리스트인지, 도시의 외부 연결 가능성을 보는지에 따라 입력과 예외 허용 정책이 다르다.

## C. 10개 훅 역할 표

아래 구조적 동작·주소·인자는 확정이다. “사용자/AI”의 정확한 구분과 화면의 명령명은 CT에 포함된 코드 조각만으로 확정하지 않는다.

| # | RVA / CT 범위 / blob | 원본 명령 및 시점 | CT 인자·검사 | 결과와 목적 |
|---|---|---|---|---|
| 1 | +1961B3C / 20B / +0 | `cmp [r15+60],0; jne`; `rax=[r15+38]`; null 검사. 도시 열거 루프 내부 | mode==0, rax!=0일 때 BFS(`rax`,`rdi`) | false→+1961C6F로 다음 도시. true→`cmp rdi,rax` 후 +1961C18. 후보 도시 리스트 필터. mode!=0은 +1961B58→9번 경로 |
| 2 | +196280C / 18B / +E7 | EXE+2C643A0 읽기, `[r8+C]==1` 검사 | BFS(`[r14+38]`,`rsi`) | false이면 +B9D 계열 메시지 계산 후 +196298A; 조건 불충족은 +1962983. 선택 도시 관련 UI 설명/상태 갱신의 강한 근거, 최종 이동 함수는 아님 |
| 3 | +1960F30 / 17B / +1F8 | 함수 prologue, `eax=20D0` | 진입 caller가 +1962B1C일 때만 BFS(`rcx`,`rdx`) | false면 5번째 인자 `[entry_rsp+28]`가 가리키는 DWORD=0, EAX=0으로 조기 반환. 그 외 원본 prologue 후 +1960F41. 선택 결과/후속 목록 계산 단계의 강한 후보 |
| 4 | +1449477 / 25B / +2E1 | `r9d=0; r8=[rax+20]; rdx=&[rsp+48]; rcx=[r12+C0]; call +193DB10` | filter(listRef=rdx,target=r8,base=EXE) **먼저** 실행 | 리스트가 남으면 native +193DB10, 비면 생략. 두 경우 모두 +1449490. 도시 그 자체 리스트라고 확정하면 안 됨 |
| 5 | +144ABD2 / 25B / +3C2 | `r9d=0; r8=[rcx+20]; rdx=&[rsp+38]; rcx=[r12+C0]; call +193DB10` | 4와 같은 filter | native 호출 여부를 결정하고 +144ABEB. 4와 반대 이동 방향이라는 증거는 없음 |
| 6 | +17B0BA0 / 14B / +4A3 | `test rcx,rcx; je +17B1E93; [rsp+20]=r9b` | caller whitelist 후 helper(context=rcx,destination=rdx,owner=r8) | 조건에 해당하는 단절 이동이면 즉시 RET. 그 외 +17B0BAE 또는 null 분기. 실행 함수 진입을 막는 층. 차단 시 bool 반환값을 따로 만들지 않음 |
| 7 | +1901F77 / 5B / +2000 | `call +1902120` | native 먼저. AL!=0, 5번째 DWORD==0, RDX!=0이면 BFS(`[savedRCX+20]`,savedRDX) | native 거절은 유지하고 연결성 추가. call 직전 인자 구성은 해당 guard에 없어 역할 상세 미확정 |
| 8 | +1963D3F / 5B / +2000 | `call +1902120`; 이후 `test al,al` | RCX=RSI=`[RBX]`, RDX=R12, R8=R13, R9D=1, mode5=0 | 개별 객체 후보 루프에서 7과 같은 연결 판정. 실패 시 +1963D51에서 다음 node |
| 9 | +1961BD5 / 5B / +2200 | `call +1902120`; 이후 `test al,al` | RCX=`[iterator]`, RDX=RDI, R8=`[r15+58]`, R9D=0, mode5=1 | native 성공 AND RDI 도시에서 비자기 연결 이웃 존재. H1의 mode!=0 경로에서 도시가 적격 객체를 하나라도 가지는지 확인 |
| 10 | +19628D5 / 5B / +2200 | `call +1902120`; 이후 `test al,al` | RCX=`[iterator]`, RDX=RSI, R8=`[r14+58]`, R9D=0, mode5=1 | 9와 같은 조건. H2가 속한 함수의 다른 분기에서 선택 도시의 적격 여부를 UI에 반영 |

H1의 리스트 삽입은 +1961C4C/+1961C56, 다음 node는 +1961C6F다. H2 주변에는 UI 객체 +490에 대한 vtable+240 및 +108 호출이 있다. 따라서 1=도시 목록, 2=선택 도시 설명/상태라는 분류는 근거가 강하다. 이 정보만으로 7=AI, 8=player 또는 9/10=반대 방향이라고 이름을 붙일 수는 없다.

원본 명령 전체와 바이트는 `ct4_disassembly.txt` 마지막 hook 1~10 구역에 있다. 덮어쓰기 범위는 1:20, 2:18, 3:17, 4:25, 5:25, 6:14, 7~10:5바이트다.

## D. CT와 현재 C++의 차이

### D1. BFS 및 owner/root

정상적이고 안정된 객체 메모리를 전제로 알고리즘은 같다:

- 두 객체 유효성, source root!=0, destination root 동일 확인.
- source를 큐에 넣고 FIFO로 처리. current==destination일 때 true.
- +20/+28/+30/+38/+40/+48의 6포인터. 유효하고 root가 같은 이웃만 추가.
- 중복은 포인터 비교. 큐 256개가 찬 상태에서 새로운 노드를 추가해야 하면 false.

C++은 ReadValue/SEH 실패를 false로 처리한다. 원본의 직접 역참조와 예외 행동까지 같지는 않다. owner ID가 아닌 root **포인터** 동일성을 비교하는 점은 같다. source/destination를 뒤집은 흔적은 확인되지 않았다.

**BFS 자체는 저장된 outgoing pointer만 따라간다.** 역방향 링크를 만들거나 대칭성을 검증하지 않는다. 실제 도시 데이터가 항상 양방향이라는 사실은 CT 코드만으로 증명되지 않는다. 동일 스냅샷에서 인접 링크의 상호성 및 객체 선택을 확인해야 한다. 양방향 호출을 해결책으로 추가할 근거는 없다.

### D2. FilterConnectedList

원본 +7B1과 현재 함수의 정상 경로는 같다.

```text
indexPtr = [listRef+08]
head = [ [EXE+34C8630] + 8 * DWORD[indexPtr] ]
전제: [EXE+34C8628]!=0, table!=0, index < DWORD[EXE+34C8660]
node+00 = object
node+08 = next
node+10 = prev (native guard에서 확인; 필터는 별도의 previous 변수 사용)
keep = Valid(object) && BFS([object+20], target)
erase = EXE+1C5A0(listRef, &localObject)
```

erase의 두 번째 인자는 node도 iterator도 아니라 **object 포인터를 담은 지역 변수의 주소**다. native erase는 해당 object와 일치하는 node들을 찾아 제거한다. 삭제 후 retained previous가 있으면 `[previous+8]`, 없으면 head 재조회. 유지하면 previous=node 후 next로 이동. 끝에서 head를 다시 조회해 리스트 비어 있음 여부를 반환한다.

CT의 guard 초기값 `0x20001` + pre-decrement와 C++ `0x20000` + post-decrement는 처리 허용 횟수 131,072회로 같다.

필터의 직접적인 논리 누락은 찾지 못했다. 그러나 원본 끝은 `setne al; movzx eax,al`, C++은 bool이다. **스텁에서 결과를 소비하는 ABI가 다르다.** 특히 리스트가 비었는데 EAX 상위 비트가 남으면 native를 생략해야 하는 분기가 통과할 수 있다. 다만 native가 빈 리스트를 어떻게 처리하는지까지는 여기서 확정하지 않는다.

### D3. 4/5의 25바이트와 CALL-only 이식

4번 호출 전: RAX는 앞 코드의 중간 객체, R8=`[RAX+20]`. 5번 호출 전: RCX는 중간 객체, R8=`[RCX+20]`. 두 경로 모두 마지막에 RCX를 `[R12+C0]`로 덮어쓴다. 따라서 **CALL 시점의 RCX는 중간 객체가 아니라 native의 첫째 인자**다.

`rsp+48`/`rsp+38`은 이 호출자가 이미 보유한 stack-local listRef의 주소다. CT 필터는 native보다 먼저 이 리스트를 수정한다. 이 두 줄만으로 “각각 반대 방향 도시 후보 리스트 생성”이라 말할 수 없다. `R12+C0`는 native에 넘기는 context 포인터까지 확정이며, 구체 클래스명/게임 내 이름은 미확정이다.

현재 C++은 앞의 20바이트를 그대로 실행한 뒤 +144948B/+144ABE6에서 같은 인자를 받는다. CALL 때문에 stack에 8바이트가 추가되지만, RDX는 CALL 전에 계산한 절대 포인터이므로 listRef가 8바이트 어긋나지 않는다. 스텁의 `sub rsp,88`은 이 진입 정렬에 맞다. 원본 CT는 inline JMP 진입이라 `sub rsp,80`이다.

필터 성공 시 C++은 +193DB10으로 tail-jump, 실패 시 RET한다. native가 보는 인자·스택 위치 및 게임 복귀 지점은 유지된다. **관찰 가능한 차이**는 native가 보는 return address: CT는 cave+37F/+460, C++은 EXE+1449490/+144ABEB. native가 caller를 검사하는지는 전체 함수가 없어 미확정이다. 현재 이를 실패의 강한 원인으로 올릴 증거는 없다.

### D4. MovementCheck78 / MovementCheck910

4개의 원래 CALL target은 모두 +1902120으로 확정이다.

```text
CT +2000:
    native(args, fifthDWORD) 먼저 호출
    native.AL==0 / fifthDWORD!=0 / destination==0 -> native 결과 유지
    그 외 BFS([arg1+20], destination)

CT +2200:
    native(args, fifthDWORD) 먼저 호출
    native.AL==0 -> 실패
    city==0 -> 실패
    city+20..48 중 non-null, non-self 이웃 n에 대해 BFS(city,n)
    하나라도 성공 -> 1, 전부 실패 -> 0
```

현재 두 C++ helper는 이 **Boolean 판정 의미**를 재현한다. 단, 910의 성공 반환은 CT의 `1` 대신 원래 native result를 반환한다. native가 0/1만 반환하면 동일하고 임의 nonzero byte를 반환하면 byte 값은 다르지만 호출자는 AL의 0/nonzero만 검사한다. native 본문 없이 0/1 정규화를 가정하지 않는다.

78의 ReadValue 실패 시 native result 유지, 910의 ReadValue 실패 시 해당 이웃 건너뛰기 등 C++의 추가 메모리 오류 정책도 원본과 구분한다. CT 78은 native에 전달한 outgoing stack slot의 fifth DWORD를 호출 후 재검사한다. C++은 자신의 arg5를 검사하므로 native가 그 stack slot 자체를 수정한다면 세부 동작은 달라질 수 있으나 그런 쓰기의 증거는 없다.

실제 임시 옵션은 78을 SymmetricMovementCheck로 다시 바꾸므로, 최종 활성 동작은 이 헤더 단독과 다르다. 이 실험은 CT 의미와 다르며 원본 비교 시 반드시 별도로 표시해야 한다.

### D5. MovementContextAllowed / hook6

원본 +874와 현재 C++의 정상 입력에 대한 조건 순서는 같다:

```text
Valid(context), Valid(destination), owner!=0 확인
[context+18] == owner
source = [context+20], Valid(source)
OwnerRoot(source) == owner
OwnerRoot(destination) == OwnerRoot(source)
모두 만족하면 BFS(source,destination)
하나라도 불충족하면 true (원본 실행 허용)
```

따라서 contextOwner 불일치 등을 무조건 false로 바꾸면 원본이 허용하던 다른 상황을 막게 된다. 3개 caller에서만 이 helper를 호출하는 범위 제한 역시 원본과 같다. 다만 **최종 bool 소비는 EAX/AL 불일치**, version caller 보정은 **미구현**이다.

R9B는 원본 함수의 네 번째 byte 인자이며 entry에서 `[rsp+20]`에 보관한다. 이 위치는 entry 기준 네 번째 인자의 home 영역이지 다섯 번째 인자가 아니다. caller가 CALL 전에 쓰는 `[rsp+20]`은 다섯 번째 인자다. 두 기준을 혼동하면 안 된다. R9B의 구체 게임 의미는 확보한 코드만으로 확정할 수 없고 CT의 문맥 helper도 이것을 연결 조건으로 사용하지 않는다.

| hook6에서 검사하는 return RVA | 확보한 caller 인자 | 확인 한계 |
|---|---|---|
| +144A326 | call +144A321. RCX=RBX, RDX=`[R15+20]`, R8=`[R15+18]`, R9D=0 | 반복 처리 후 RSI next로 진행. 정확한 명령명 미확정 |
| +144A8FA | call +144A8F5. RCX=RBP, RDX=RSI, R9D=0. stack 5~9번은 0,0,1,0,0 | R8 설정은 guard 앞부분 밖. 명령명 미확정 |
| +1450972 | call +145096D. RCX=RBX, RDX=RDI, R9D=0. stack 5~9번은 0,0,1,0,0 | R8 설정은 guard 앞부분 밖. 명령명 미확정 |

이 주소들을 근거 없이 각각 “이동/수송/배정”으로 배정하지 않는다. 정확한 이름은 A/B 로그 또는 더 넓은 게임 EXE caller 코드가 필요하다.

### D6. 활성화·복원·guard 차이

| 항목 | CT | C++ 현재 |
|---|---|---|
| game guard | 주변 함수/호출자/erase까지 18개 | 원본 훅 10개 구간. 4/5는 25B 모두 검사 |
| 실제 쓰기 범위 | 4/5 각 25B | 4/5 마지막 CALL 각 5B; 나머지는 CT 길이와 동일 |
| DLL 없는 기본 적용 | Lua가 두 DLL을 요구해 시작 불가 | DLL 조회 없이 원본 entry에 적용 가능 |
| 기존 version 훅 | 소유자와 trampoline 검사 후 slot bridge | hook6 original mismatch로 전체 적용 거절 |
| 쓰기 중 실행 경쟁 | thread freeze, RIP 검사, 쓰기 직전 재검증 | 해당 절차 없음 |
| 쓰기 후 검증 | read-back, 보호 복원 및 cache flush 결과 확인 | memcpy 성공 위주; 보호 복원/flush 반환값 미검사 |
| disable | current==our after 검사 후 before 복구 | 현재 소유자 검사 없이 before 덮어쓰기 |
| 부분 적용 실패 | rollback 실패 기록으로 후속 작업 제한 | RestoreAllPatches 결과를 무시하고 “복구 완료” 로그 |
| 재시도 | recovery 상태 유지 | rollback 실패 상태에서 gApplied=false면 disable이 일찍 반환하고, 다음 enable이 기록을 지울 가능성 |

후자의 복구 문제는 별도 수명주기 결함이며 방향성 실패의 증거는 아니다. “원본 10개 PASS”는 설치 전 구간 일치에 대한 좋은 근거지만 설치 후 10개 경로가 실제 실행되었다는 뜻은 아니다.

## E. 증상을 설명하는 강한 원인 후보

1. **최우선 강한 후보: bool/EAX 분기 결함 + 경로 차이.** 결함과 오판 가능성은 실제 함수로 재현했다. 7/8은 일반 C++ 호출 결과를 AL로 소비하고, 9/10은 native/이웃 검사로 별도 차단할 수 있는 반면, 1~6은 bool을 EAX로 검사한다. 한 방향만 해당 취약 경로에 의존하면 현재 증상을 설명할 수 있다. B에서 `rawAL=0 && rawEAX!=0 && branch=allow` 한 건이 관측되면 그 차단 누락은 확정할 수 있다.
2. **강한 후보: B가 9/10의 도시 적격 검사 또는 hook6의 허용 우회 조건만 통과하고, 실질 두 도시 비교를 거치지 않는다.** 9/10은 본토 도시 내부에 같은 세력 이웃이 있으면 통과한다. B에서 전달된 city가 본토이고 목적지 검사가 다른 곳에서 누락되면 방향성을 설명한다. 그러나 이것은 원본도 가진 분업이므로 9/10 자체가 잘못됐다는 뜻은 아니다. RDX와 실제 A/B 도시 대응을 기록해야 한다.
3. **후순위 후보: hook6 caller 미일치 또는 context/source/owner 불일치로 의도된 예외 허용 분기가 실행된다.** whitelist 및 비교 의미는 원본과 맞으므로, CT와 달라진 실제 인자/경로의 증거가 있어야 이식 원인으로 판정할 수 있다. version 없는 환경에서 재현한다면 version caller 보정을 이 원인으로 삼지 않는다.

4/5 방향 전담설, 새 caller RVA, 새로운 OR/BTS 패치, BFS 양방향화는 현재 증거로 제안하지 않는다.

## F. version.dll이 없는 정상 경로

요구하는 최종 설계와 현재 C++ 기본 설치 경로는 다음과 같다:

```text
EXE exact guard 확인
  -> EXE 기반 helper/stub 준비
  -> 10개 지정 구간 적용
  -> hook6은 [entry_rsp]에서 실제 EXE caller 판독
  -> 같은 세력 문맥 조건 및 연결 판정
```

version module, version hash, +B5C70/+B5D23은 이 경로의 필수 입력이 아니다. 현재 C++은 이 점을 지킨다. 다만 ABI 결함 때문에 “구조상 version 비필수”와 “모든 이동 제한 정상”은 분리해서 평가한다.

원본 Lua의 두 DLL 필수 assert는 최종 요구와 충돌하므로 그대로 포팅하면 안 된다. version.dll이 로드되어 있어도 +17B0BA0이 원본이면 동일 기본 경로다. 시스템 version.dll의 이름만 보고 bridge 경로를 선택해서도 안 된다.

## G. version.dll이 있는 optional bridge

로컬 version.dll MD5는 CT의 `05861023e39184a5252dd41221c418c5`와 같고 4개 signature도 모두 PASS다. 이는 **로컬 파일 분석**이며 실제 게임의 로드 모듈과 slot 상태까지 검사했다는 뜻은 아니다.

원본 bridge는 다음을 모두 검사한다:

1. EXE+17B0BA0: rel32 JMP, 뒤 4 NOP, 기존 `44 88 4C 24 20` 유지.
2. JMP 도착 stub: `FF 25 00 00 00 00` + qword(version+B5C70).
3. `[version+151E98] == stub`.
4. trampoline=stub+40: `test rcx,rcx; je EXE+17B1E93`, 이어 absolute JMP→EXE+17B0BA9.
5. `[version+14FE38] == trampoline` 또는 활성화 중에는 자기 CT cave entry.

인식된 경우의 흐름:

```text
EXE+17B0BA0 -> 기존 stub -> version+B5C70
  -> call [version+14FE38]
     (원래 trampoline 대신 CT hook6)
  -> CT 차단이면 RET -> version+B5D23
  -> CT 허용이면 displaced 명령 재현 -> EXE+17B0BAE
     -> 게임 함수 반환 -> version+B5D23
  -> version wrapper 후처리 -> 원래 EXE caller
```

EXE의 기존 JMP와 stub을 덮어쓰지 않고 slot만 바꾸는 것이 공존의 핵심이다. 허용 경로에서 CT는 trampoline의 효과를 자체 재현하므로 trampoline까지 다시 호출해 중복 실행하지 않는다.

wrapper는 push 4개(20h)+sub 58h 후 call 8바이트를 추가한다. 따라서 CT hook6 진입에서 `[entry_rsp]`는 version+B5D23, 실제 게임 caller는 `[entry_rsp+80h]`다. CT 저장 프레임 C8h와 helper CALL 8바이트까지 반영하면 보정 helper가 읽는 주소는 현재 RSP+D0h와 RSP+150h가 된다. +B5D23일 때만 후자를 선택한다. Lua의 `code[12]=11`은 RIP-relative 비교가 RET 바로 뒤의 qword를 가리키도록 하는 displacement 수정이다.

최종 C++ 설계는 entry가 원본인 Case A/B를 먼저 처리하고, 원본이 아니면서 정확히 인식된 version 소유 훅인 Case C에만 이 계층을 사용해야 한다. 알 수 없는 훅은 덮어쓰지 않는다. **version 부재/무관한 hash 차이 때문에 기본 CT4를 비활성화할 이유는 없다.** 실제 hook6 소유자가 인식 불가인 경우는 module 부재와 다른 충돌 상황이다.

현재 C++에는 Case C 구현이 없다. 이식 완성도 문제지만, 순수 게임에서의 B 실패 원인으로 설명하면 안 된다.

## H. 한 세션 A/B 테스트를 위한 최소 진단 설계

### H1. 목표와 제한

현재 `RunIsolatedTerritoryMovementDiagnostic()`는 한 번 원본 10개 바이트를 읽는 함수다. 호출 횟수·동적 인자·caller·helper 결과를 기록하지 않으므로 이번 원인을 분리할 수 없다.

추가 진단은 **기존 10개 훅/스텁 내부의 관측만** 사용한다. 새 게임 RVA 훅, PAGE_GUARD, OR/BTS 변경, 게임 객체·목록에 대한 추가 쓰기는 없다. 진단 저장소와 카운터만 DLL 소유 메모리에 둔다. 기존 feature가 수행하는 erase·실행은 그대로이고 진단을 이유로 다시 호출하지 않는다. 특히 `IsGameObjectValid`는 native vfunc를 호출하므로 “단순 메모리 읽기”로 간주해 진단에서 추가 호출하지 않는다. 연결성도 기존 실행 결과를 관측하며 검사 목적으로 BFS를 반복 호출하지 않는다.

진단 코드를 기존 스텁에 포함하려면 진단용 DLL 재빌드가 필요하다. “게임 상태를 바꾸지 않는 관측”과 “실행 코드까지 전혀 바꾸지 않는 관측”은 다르다. 이 보고서에서는 설계와 오프라인 ABI 재현까지만 제공하며 게임용 진단을 설치하지 않았다.

### H2. 공통 최소 필드

```cpp
// 설계용 레코드. 실제 게임 DLL에는 아직 추가하지 않았다.
struct TraceEvent {
    uint32_t phase;       // A, B, CONTROL
    uint32_t hookId;      // 1..10 (공유 helper여도 callsite로 구분)
    uint32_t threadId;
    uint32_t stage;       // ENTRY, HELPER_RESULT, NATIVE_RESULT, EXIT
    uint64_t sequence;    // thread 내 순서 + nested call 연결용 event/parent ID
    uintptr_t caller;    // 주소와 module+RVA를 따로 보관
    uintptr_t source;
    uintptr_t target;
    uintptr_t sourceRoot;
    uintptr_t targetRoot;
    uint32_t rawEax;      // AL만 기록하면 핵심 결함을 놓침
    uint32_t reason;      // caller_miss, owner_mismatch, disconnected 등
    uint8_t nativeAl;
    uint8_t finalAl;
    uint8_t readMask;     // root/field는 읽기 실패와 null을 구분
};
```

helper를 호출하지 않은 분기에서는 `connected=NOT_RUN`을 기록한다. 읽지 못한 root는 `UNKNOWN`이며 0이나 다른 owner로 취급하지 않는다. module 주소는 이름+RVA로 정규화하고 raw 주소도 남긴다.

고정 크기 buffer, hook별 total/hit/skip/block 카운터, overflow/dropped 카운터를 사용한다. 문자열·파일 I/O·AddLog·동적 할당은 hot path에서 하지 않고 A/B 종료 후 출력한다. 멀티스레드 생산을 허용하면 slot 예약과 완료(sequence publish)를 구분해 불완전한 record를 읽지 않게 한다. 상세 기록이 유실되면 “확정” 판정을 하지 않는다.

### H3. 훅별 추가 필드

| 훅 | 최소 추가 기록 |
|---|---|
| 1 | R15, `[R15+60]`, `[R15+38]`, RDI. role/null/연결 분기와 raw EAX. 필요할 때만 RBX node |
| 2 | R14, `[R14+38]`, RSI. raw EAX 및 연결/메시지 분기 |
| 3 | entry caller, RCX/RDX, fifth out-count 포인터. caller match, raw EAX, 조기 반환 여부. out-count에 진단이 쓰지 않음 |
| 4/5 | callsite ID, RCX(native context), RDX(listRef), R8(target), R9, R12. 필터 전후 head, 실제 방문/유지/삭제 수, **기존 판정으로** 처리된 object/source, raw EAX 및 native 호출 여부 |
| 6 | raw caller/보정 caller, RCX/RDX/R8, R9B. `[context+18]`, `[context+20]`, 비교 root. whitelist 미일치/validity 실패/owner mismatch/BFS false를 구분. raw EAX와 실제 RET/continue 분기 |
| 7/8 | callsite ID, RCX/RDX, fifth DWORD, `[RCX+20]`, native AL, 적용/생략 이유, forward 결과. 현 테스트 연결이면 이미 실행하는 reverse 결과도 관측 |
| 9/10 | callsite ID, RCX/RDX, fifth DWORD, native AL. 6개 이웃 중 기존 검사를 실제 수행한 이웃, 해당 BFS 결과, 최초 성공 이웃, 최종 AL |

카운터는 10개 모두, 상세 데이터는 실행된 경로만 남긴다. 4/5에서 모든 리스트 내용을 무조건 덤프할 필요는 없지만, A/B에 관련된 source/target 객체가 필터에서 실제로 평가됐는지는 식별 가능해야 한다.

### H4. return address를 잘못 읽지 않기

- 3/6은 함수 entry이므로 저장 전 `[RSP]`가 caller다. 현재 저장 후에는 `[RSP+C8]`.
- 4/5 CALL-only wrapper entry의 `[RSP]`는 각각 +1449490/+144ABEB.
- 7/8/9/10은 +1901F7C/+1963D44/+1961BDA/+19628DA로 구분 가능. near thunk가 JMP이므로 반환 주소는 유지된다.
- 1/2는 함수 중간이다. **그 지점의 `[RSP]`를 caller라고 기록하면 틀린다.** 확보된 prologue 기준 원래 caller는 hook 진입 RSP+E8 / RSP+D8. 이 오프셋을 사용할 때는 해당 prologue 전체 guard 일치를 요구하거나 unwind 정보를 사용한다.
- version bridge의 +80 보정은 정확한 wrapper 인식에 성공했을 때만 사용한다.

### H5. raw EAX를 반드시 스텁에서 먼저 잡기

C++ 로그 함수에 bool 인자로 넘긴 뒤 기록하면 컴파일러가 AL을 정규화해서 오류가 사라진다. helper 반환 직후, 다른 함수 호출 전에 **RAX/EAX 전체**를 포착해야 한다. 가장 작은 관측 지점은 기존 `CallRax()` 바로 뒤, `85 C0` 바로 앞이다.

스텁 진단 설계 예:

```text
저장 프레임을 기존보다 10h 확장 (80→90, 88→98; 16-byte 정렬 유지)
  ... 기존 helper 호출 ...
  mov [rsp+80h], rax      ; 새로 확보한 scratch; 저장 XMM 영역과 겹치지 않음
  ... 비할당 recorder에 원래 저장 레지스터/판정값 전달 ...
  mov rax, [rsp+80h]      ; 전체 결과 복원
  test eax,eax            ; 진단 단계에서는 기존 분기를 그대로 유지
  ... 원래 분기 ...
```

추가 프레임으로 저장된 레지스터와 entry caller 오프셋도 +10h 이동한다. 예: C8→D8. version helper까지 바꾸는 경우 보정 상수도 따로 재계산한다. 입력 인자는 **helper 호출 전 값**을 저장해 쓰고, helper 후의 volatile RCX/RDX를 입력이라고 착각하지 않는다. 기존 프레임의 `[rsp+20]`은 XMM0 저장 영역이므로 로그 scratch로 덮어쓰면 안 된다. recorder가 쓸 shadow space도 확보해야 한다. 플래그·비휘발성 레지스터·XMM 보존은 기존 스텁 계약을 그대로 지킨다.

필터 내부 이유 등 추가 C++ 계측은 함수의 최적화 결과를 바꿀 수 있다. 따라서 1차 결정적 증거는 helper 본문 변경 없이 스텁 경계에서 raw EAX와 실제 분기를 잡는 것이다. 더 세부 계측을 추가하면 진단 빌드 해시와 assembly도 기록한다.

### H6. 한 세션 절차와 판정 기준

1. 시작 시 빌드 ID/DLL hash, EXE base, 적용된 10개 patch 목적지, hook6 direct/bridge, trace overflow=0 확인. H78이 원본 helper인지 symmetric helper인지 기록.
2. 동일 세이브·같은 세력의 A/B 도시를 정한다. `BEGIN A` 후 **도시/무장 선택을 열기 전부터** 단절지→본토 시도, 차단 결과 확인, `END A`.
3. 가능하면 세이브를 다시 읽어 도시·무장 상태를 같게 맞춘다. `BEGIN B` 후 본토→단절지 시도, 화면 허용/실행 결과를 기록하고 `END B`. 이 동작은 사용자의 테스트이며 logger가 명령을 실행하지 않는다.
4. 카운터 및 상세 이벤트를 한 번 출력한다. 실제 실행을 시험했다면 저장하지 않고 원래 세이브로 돌아가 통제한다.

출력 형식 예시(실측값이 아니라 포맷):

```text
phase=B hook=6 caller=exe+144A8FA matched=1 src=<p> dst=<q>
owner_eq=1 helper_AL=0 raw_EAX=<nonzero> consumer=test_eax branch=CONTINUE
```

| 실측 패턴 | 판단 |
|---|---|
| B에서 helper AL=0, EAX!=0, 실제 continue | 해당 지점 ABI 오판으로 차단 누락 확정 |
| B에서 9/10만 호출, RDX=본토, 이웃검사 성공, pair 검사 경로 부재 | 경로/역할 후보를 지지. 해당 pair를 맡아야 하는 기존 층을 추적; 9/10 의미 변경 금지 |
| H6 진입하지만 caller_miss | caller 범위 이탈 확정. 새 RVA를 추가하려면 먼저 해당 caller 원본 의미 분석 |
| H6 caller match지만 owner/context 조건 불충족 | CT helper의 원래 예외 허용 분기. 실제 인자 선택과 객체 역할 확인 |
| 4/5 필터에서 관련 객체가 제거되고 native가 생략됨 | 그 경로는 차단 정상. 다른 실행 경로를 추적 |
| 10개 카운터 모두 0 | phase 시점/적용 상태/다른 경로 문제. 방향성에 대한 결론 불가 |
| 연결 검사 결과 자체가 예상과 다름 | source/destination와 root, 실제 인접 pointer 스냅샷 추가 조사 |

한 번의 A/B로 **항상** 원인이 확정된다고 보장하지 않는다. 위 결정적 패턴이 포착되면 확정 가능하고, 그렇지 않으면 어디까지 원인을 좁혔는지를 명시한다.

## I. 원인 확인 후 가장 작은 수정

확정된 ABI 결함의 최소 수정 후보는 아래 다섯 스텁 생성 위치(6개 훅)다:

```cpp
// C++ bool 반환 helper 직후에 한정
// 기존: { 0x85, 0xC0 }   // test eax,eax
// 수정: { 0x84, 0xC0 }   // test al,al
```

위치: `BuildHook1Stub` 447줄, `BuildHook2Stub` 466줄, `BuildHook3Stub` 507줄, `BuildHook45Stub` 530줄, `BuildHook6Stub` 551줄. 원본 blob의 int-return helper에 대한 test EAX를 일괄 변경하는 제안이 아니다. C++ 함수가 bool을 반환하는 경계에만 적용한다. 대안은 해당 helper를 명시적 `uint32_t` 0/1 반환 ABI로 바꾸는 것이지만 더 넓은 변경이다.

이 수정은 BFS, caller whitelist, native 순서, list erase, 910 의미, game patch 범위를 바꾸지 않는다. static/offline상 타당하지만 실제 B 제한 정상화는 A/B 재시험 전 확정하지 않는다. 현재 보고서에서는 게임 소스에 적용하지 않았다.

실게임 로그가 다른 원인을 보이면 해당 기존 CT 경로만 원문대로 복구한다. 근거 없이 새 hook/caller를 추가하지 않는다. 양방향 실험 연결을 제거해 CT 그대로의 78로 돌아가는 작업, optional bridge 구현, 복구 수명주기 보강, 최종 독립 UI 연결은 각각 별도 변경으로 검증한다. 여러 변경을 한꺼번에 섞으면 어떤 수정이 증상을 바꿨는지 알 수 없다.

정확한 복구를 만족하려면 patch record에 before/after를 함께 저장하고 자기 after 일치 확인 뒤 복구해야 한다. 부분 쓰기 및 rollback 실패는 성공으로 로그하지 않고 복구 기록과 실패 상태를 유지한다. 이미 실행 중일 수 있는 cave를 해제하지 않는다. 이는 방향성 최소 수정과 구분해 처리할 항목이다.

## J. 기존 정상 기능을 보호하기 위한 유지 항목

- **7/8: native 먼저, native 거절 유지, fifth DWORD==0 조건 유지.** 현재 symmetric 실험 여부를 명시하고 관측 단계에서는 바꾸지 않는다.
- **9/10: fifth DWORD=1인 실제 caller 인자와 이웃 존재 검사 유지.** pair 연결 검사로 바꾸지 않는다.
- **4/5: 원래 인자 구성과 stack-local 주소, 필터→native 순서, erase(listRef,&object), 삭제 후 iterator 진행 유지.** 리스트 필터를 호출 후로 이동하거나 node 직접 삭제로 바꾸지 않는다.
- **6: 세 caller whitelist, context/owner 불일치 시 원본 허용, R9B와 stack 인자, 원본 null 분기, native 복귀 경로 유지.** 모든 +17B0BA0 호출에 무조건 BFS를 걸지 않는다.
- **1/2/3: mode/null 분기, 원래 continuation RVA, 3의 caller 제한과 out-count 처리 유지.** 한 개의 일반 helper로 합치지 않는다.
- **BFS: 원본의 6개 offset·root 포인터 비교·큐 제한 유지.** 양방향 AND, OR/BTS 또는 광범위 패치를 추가하지 않는다.
- **version은 optional.** 기본 entry와 알려진 bridge 소유자를 구분하고 무관한 DLL 부재/hash로 core를 거절하지 않는다.

실게임 완료 기준: (1) 단절지→본토 제한, (2) 본토→단절지 제한, (3) 기존 수송 기대 동작 유지, (4) 기존 배정 기대 동작 유지, (5) 연결된 같은 세력 도시 간 정상 이동, (6) disable 후 원본 행동 및 재enable. 수송/배정의 정확한 기대 동작은 기존 정상 테스트와 같은 시나리오로 비교한다. 사용자 확인 전 완료·main 병합으로 간주하지 않는다.

## 오프라인 증거 재생성

`extract_ct4.py`는 XML에서 지정 ID만 추출하며 CT나 게임 파일을 실행하지 않는다. Capstone이 필요하다. 기본 Python 경로 또는 임시 `ct4-audit-deps`에서 로드한다. hook 원본 10개가 포함된 guard와 모두 일치하는지도 assert한다.

`compile_abi_probe.py`는 설치된 MSVC/Windows SDK로 현행 header의 helper를 별도 컴파일하고, `abi_repro.cpp`의 가짜 객체만 사용하는 실행 파일을 실행한다. 게임 프로세스를 열거나 DLL을 로드하지 않는다. `/GL`을 사용하는 실제 최종 게임 DLL의 기계어와 동일하다고 주장하지 않는다. 소스의 ABI 계약 위반 및 현행 helper에서 가능한 오판을 입증하는 재현이다.

```text
D:\AI\miniconda3\python.exe docs\ct4_audit\extract_ct4.py
D:\AI\miniconda3\python.exe docs\ct4_audit\compile_abi_probe.py
```

생성된 obj/exe/blob binary는 이 디렉터리의 .gitignore로 제외했다. 실제 게임 소스·빌드 산출 DLL은 변경하지 않았다.
