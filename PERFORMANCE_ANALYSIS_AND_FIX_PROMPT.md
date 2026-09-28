# 삼국지8 리메이크 PK 치트 성능 분석 및 수정 작업 프롬프트

작성일: 2026-09-27  
분석 기준 커밋: `74febb5db3712becab7aa5f5f211886de17a38de`  
대상: `SAM8RPK_Ingame_Cheat.sln`, C++ 내부 DLL 치트 / ImGui / Direct3D 오버레이

## 1. 문서의 사용 방법과 분석 한계

이 문서는 **다른 AI 작업자가 실제 코드를 수정하기 위한 작업 지시서**다. 2~4절은 소스 분석 결과이고, **5절부터 마지막 절까지를 수정 AI에게 작업 프롬프트로 전달**하면 된다. 파일 전체를 전달하는 것이 가장 좋다.

현재 작업에서는 프로그램 소스, 설정 및 게임 파일을 수정하지 않았다. 실제 게임을 실행하거나 CPU/GPU/메모리 프로파일을 수집하지 않았으므로, 사용자가 경험한 자원 폭증의 단일 원인이 확정된 것은 아니다. 아래의 ‘확인’은 해당 코드 구조가 존재한다는 뜻이며, 실제 증상의 기여도는 별도로 측정해야 한다.

- **확인**: 호출 경로, 반복 범위, 메모리 보관, 동기식 대기 등을 현재 소스에서 확인했다.
- **조건부**: 해당 기능·설정·렌더링 API가 활성화되어야 비용이 발생한다.
- **측정 필요**: 게임 내부 객체 소유권, 드라이버의 동작, 실제 병목 비중은 소스만으로 확정할 수 없다.
- 우선순위 **P0**는 먼저 분리 측정하고 수정할 항목, **P1**은 다음으로 다룰 항목, **P2**는 측정 결과에 따라 개선할 항목이다.

아래 파일 경로는 별도 표기가 없으면 `Internal DX11 Base/` 기준이다. 파일명만 적은 경우 해당 디렉터리의 하위 폴더에서 찾는다. 예를 들어 `SelectOfficercapture.cpp`는 `Cheats/Officer/SelectOfficercapture.cpp`다. 줄 번호는 위 커밋 기준 탐색용이며, 수정하면서 바뀌므로 **함수명도 함께 검색**해야 한다. 외부 라이브러리 전체에 대한 감사가 아니라, 기능별 진입점과 주요 실행·할당·저장 경로에 대한 정적 분석이다.

## 2. 먼저 확인해야 할 핵심 원인 후보

| 우선순위 | 항목 | 확인한 구조 / 발생 조건 | 예상 영향 | 작업 |
|---|---|---|---|---|
| P0 | 배속과 치트 타이머 혼용 | 배속이 QPC/GetTickCount 계열을 후킹하고 치트의 대부분 타이머가 같은 API를 사용한다. 여러 atomic 변수를 조합한 시계 갱신도 하나의 원자적 연산이 아니다. | 배속 시 CPU 반복 작업 증가, 시간 점프에 따른 재실행; 게임 자체 작업량·렌더링량 증가 가능 | T01 |
| P0 | 알림 대기열 무제한 증가와 동시 접근 | `g_notifications`는 상한 없이 추가되고 렌더링 중에만 제거된다. 여러 스레드가 추가하고 UI가 순회·삭제한다. | RAM 누적, 매 프레임 순회 증가, 데이터 경쟁에 따른 멈춤/충돌 | T02 |
| P0 | 다중 무장 편집의 파일 저장 반복 | 무장별 `SetTargetSkillCount()` 호출마다 전체 JSON을 저장한다. UI에서 선택 무장 수만큼 반복한다. | 버튼 클릭 시 큰 프레임 지연, 디스크 I/O·CPU 증가, mutex 대기 | T03 |
| P1 | 렌더링 경로의 게임 데이터 검색 | 자녀 5,102슬롯 검색이 약 1초마다 Present 경로에서 실행되고, 도시 반란 유지도 매 렌더 프레임 수행된다. | 주기적 프레임 지연, FPS에 비례하는 불필요한 작업 | T04 |
| P1 | 프로세스 전체 메모리 검색 | 기재 선택의 캐시 실패 경로는 UI에서 전체 readable 메모리를 동기 검색한다. 무작위 부여는 프레임마다 8MiB 검색 예산을 사용한다. 배우자·디버그 검색은 worker에서 넓은 메모리를 바이트 단위로 훑는다. | 클릭 시 화면 정지, CPU·메모리 대역폭 사용, 페이지 접근으로 working set 증가 가능 | T05 |
| P1 | 기재 문구 메모리 누적 | JSON 문구를 다시 게시할 때 문자열별 VirtualAlloc; 이전 포인터를 보관하지 않은 채 배열 교체. 별도 문구 후크는 이전 실행 메모리를 retired 목록에 계속 보관한다. | 설정 재적용 횟수에 비례한 committed memory 증가 | T06 |
| P1 | 기능 해제 시 UI 스레드 대기 | 군주 보너스 OFF에서 최대 6초 `WaitForSingleObject`; 도로·시작 설정·기술 초기화도 동기 대기한다. | 기능을 끌 때 게임 화면 정지 | T07 |
| P1 | 전투 작업의 상태·세대 관리 | 넓은 battleActive 판정과 별도 캐시/재적용 경로가 공존한다. 무신은 500ms마다 해당 부대의 전법 최대 45개를 재검사한다. | 전투 중 CPU 비용, 로드·전투 전환 시 오래된 캐시/재작업 | T08 |
| P1 | 5번 책략의 추가 처리 | 시작 시 기능을 강제 요청하며, 전투 중 부대 감시와 UI 상태 갱신을 수행한다. 자체 생성 버튼의 실패·파괴 경로는 추가 확인이 필요하다. | 기능을 의식하지 않아도 비용 발생 가능; UI 객체 수명 문제는 검증 필요 | T09 |
| P1 | 동기 파일 로그 | 로그 한 줄마다 파일 상태 조회, open/write/close를 전역 로그 mutex 안에서 수행한다. | 로그 옵션 ON일 때 게임 후크·UI·worker 지연 | T10 |
| P1/조건부 | DX12 렌더링 수명 및 동기화 | allocator Reset 전 GPU 완료 확인이 없고 초기화 재시도의 자원 정리가 불완전하다. | DX12 경로에서 GPU 오류·stall·자원 누적 가능 | T11 |
| P2 | 화면별 불필요한 재구성 | 명품 수여 대상 목록을 매 프레임 복사·정렬하며 일부 목록에는 clipper가 없다. | 해당 창을 열 때 CPU·할당 증가 | T12 |

**GPU 사용률 상승을 CPU 스캔만으로 직접 설명하면 안 된다.** 배속으로 실제 FPS 또는 게임 작업량이 증가한 경우, 오버레이가 추가된 경우, DX12 자원 문제가 있는 경우를 나누어 조사해야 한다. GPU 사용률, GPU frame time, 전용/공유 GPU 메모리는 서로 다른 지표다. RAM 역시 Private Bytes 증가와 Working Set 증가를 구분해야 한다.

## 3. 현재 실행 구조

```text
Source.cpp: MainThread_Initialize
  ├─ 5번 책략 UI bridge 준비 + SetStratagemFiveFeature(true)
  ├─ InitCheats + 최대 60회의 별도 재시도 worker
  ├─ Direct3D / 입력 API 후킹
  ├─ ClientBGThread: Menu::Loops 실행 후 sleep_for(30ms)
  │    ├─ 행동력, 연회/중개, 보주 등 값 유지
  │    ├─ 기재 런타임: 내부 500ms 검사 / 설정 파일 상태 2초 검사
  │    ├─ 전투 감시: 100ms 조건
  │    ├─ AI 성장 / 평정 / 적극성: 500ms 조건
  │    ├─ 저장 설정 적용: 내부 1초 조건
  │    ├─ 자동 수송·배치: 내부 500ms 조건
  │    ├─ 상성 성장: 내부 250ms 조건 + 분기 전환 시 실제 작업
  │    ├─ 재야·등용·사망 감시: 상태 전환 시 실제 전체 검색
  │    └─ 주점 청부: 500ms 조건
  └─ 별도 주기 worker: 군주 보너스 / 도로 / 시작 설정 / 기술 초기화 등

게임 Present
  └─ IsAnyUIOpen()일 때 Overlay → Menu::Render → DrawMenu
       ├─ 화면과 위젯 그리기
       ├─ 자녀 검색 / 임신 정보 갱신
       ├─ 도시 반란 0 유지
       ├─ 무장·도시·명품 등 창 갱신
       └─ 알림 순회·이동·삭제
```

근거: `Source.cpp:226`, `Source.cpp:275`, `Menu.cpp:349`, `Menu.cpp:365`, `Menu.cpp:1077`, `Engine.cpp:202`, `Engine.cpp:555`.

주의할 점:

1. `sleep_for(30ms)`는 작업 시간을 포함한 정확한 30ms 주기 보장이 아니다. 배속이 Sleep 자체를 후킹한다는 근거는 없다. 현재 배속은 시간 조회 API를 후킹한다.
2. 위 ‘100ms/500ms/2초’는 코드가 읽는 시계 기준이다. 배속 ON 시 실제 경과 시간과 달라질 수 있고, 바깥 루프 주기 때문에 정확히 배속 배율만큼 호출 횟수가 늘어나는 것도 아니다.
3. 메뉴를 접어도 `bShowMenu`는 유지될 수 있다. 따라서 접힘은 오버레이 전체 중지와 같지 않다. 반대로 모든 UI가 닫히면 Present의 Overlay 호출은 이미 생략된다.
4. 무장 목록에는 이미 20개→384개 단위의 분할 구축, 필터 캐시, `ImGuiListClipper`가 있다. ‘모든 무장 목록을 매 프레임 5,102명씩 다시 생성한다’는 진단은 현재 코드에 맞지 않는다.

## 4. 기능별 분석표

| 기능 | 현재 동작 및 비용 판단 | 근거 파일 / 함수 | 지시 |
|---|---|---|---|
| 게임 배속 | 프로세스 내 시간 API 4종 후킹. 치트 타이머까지 영향을 받으며 시계 상태의 다중 스레드 일관성이 부족하다. | `Cheats/System/SpeedHack.cpp`, `hkQueryPerformanceCounter`, `hkGetTickCount64`, `SpeedHack_Update` | T01 |
| 상단 알림 / 기록 | 기록은 50개 제한이지만 실제 표시 대기열은 무제한. 한계돌파는 능력치별 알림을 추가한다. | `NotificationManager.cpp:19`, `StatMonitor.cpp:108` | T02 |
| 전법 횟수 다중 편집 | 클릭 한 번에 선택 무장마다 전체 설정을 다시 저장한다. 지속 부하보다 조작 시 정지 후보다. | `OfficerDetail.cpp:289`, `SkillCountManager.cpp:99` | T03 |
| 군악대·무신·함선 병기화 등 다중 지정 | 전법 횟수와 동일한 저장 경로를 사용한다. | `OfficerDetail.cpp:1442`, `OfficerDetail.cpp:1470` | T03 |
| 전체 무장 전법·특기 편집 | 확인 버튼에서 5,102슬롯을 동기 순회한다. 유효 무장별 특기 24개·전법 35개에서 변경되는 byte마다 VirtualProtect 설정/복구를 반복한다. | `Cheats/Officer/BatchOfficerEditor.cpp:130`, `Cheats/Officer/StatMonitor.cpp:249-338` | T03/T12 |
| 자녀 관리·조기 임관 | 관리창을 닫아도 DrawMenu 경로에서 1초마다 전체 5,102슬롯 부모 포인터 검색. 매번 임시 map 생성. | `ChildEarlyAppearance.cpp:228`, `:319`, `Menu.cpp:1077` | T04 |
| 임신 관리 | 자녀 갱신에 연결. 창 열기 시 직접 경로 우선이며 실패했다고 자동 전체 프로세스 검색을 하지는 않는다. 별도 제한 영역 검색 경로는 구분한다. | `PregnancyManager.cpp:385`, `:898`, `:902` | T04/T05 |
| 배우자 검색 창 | 별도 detached worker지만 전체 writable committed 영역을 검색한다. 주석과 달리 내부 검색은 `i++`다. 취소·시간 예산이 없다. | `SelectOfficercapture.cpp:2939` | T05 |
| 디버그 메모리 검색 | 전체 readable 영역을 검색. 결과는 약 100,001개에서 중단되므로 무제한 결과 저장은 아니다. 다음 검색은 결과 복사 및 후보별 임시 buffer 할당. | `debug.cpp:105`, `:223` | T05 |
| 기재 JSON 런타임 | 500ms 감시, 2초 파일 변경 검사, 실제 효과 쓰기는 pending/sentinel 변화 시에만 수행. 문자열 재게시 시 이전 할당 누적은 별개 문제다. | `TraitConfigRuntime.cpp:391`, `:1814`, `:2184` | T06/T13 |
| 커스텀 기재 월간 효과 | 월간 게임 후크가 원본 처리 후 1,801명 포인터 및 보유 기재 3슬롯을 검색하여 효과 166을 합산한다. 500ms 감시와 다른 실행 경로다. | `TraitConfigRuntime.cpp:1297`, `CustomMonthlyTraitUpdate` | T13 |
| 기재 이름·일반 설명·특수 설명 편집 | 적용/해제 반복 시 실행 메모리를 retired 목록에 보관. 문자열 비교 코드를 수정 항목별로 생성한다. | `TraitTextNameHook.cpp:326`, `TraitTextDescHook.cpp:168`, `TraitTextSpecialDescHook.cpp` | T06 |
| 원본 게임 기재 뷰어 | `TraitViewerRoster/BattleMap/OfficerInfo/BattleInfo/BattlePrep/NativeEditors/Widths`는 주로 후크·패치 설치/복구. 화면별 후크 호출량은 계측 필요. | `Cheats/Officer/TraitViewer*.cpp`, `TraitViewerFeature.cpp` | T06/T14 |
| 모든 무장 목록 | 이미 분할 구축/clipper 적용. 상세 패널의 기재 ID vector 재생성, 큰 선택 작업, 세션 전환 캐시를 조사한다. | `SelectOfficercapture.cpp:1393`, `:2285`, `:2566`, `:2763` | T12 |
| 무장 상세 / 주인공 편집 | 화면 표시 비용 외에 전법별 mutex 조회와 다중 편집 저장 비용이 있다. 단순 표시와 클릭 처리 비용을 분리한다. | `OfficerDetail.cpp`, `SelectOfficercapture.cpp:1854` | T03/T12 |
| 기재 직접 선택 | 캐시·무장 슬롯에 없는 기재를 선택하면 `SetTraitID()`에서 전체 readable 프로세스 메모리를 동기 검색할 수 있다. 없는 객체일수록 끝까지 탐색한다. | `SelectOfficercapture.cpp:1495`, `OfficerDetail.cpp:440-548` | T05 |
| 기재 무작위 일괄 부여 | 무장 수집 worker가 있지만 기재 객체 검색은 렌더링 경로에서 프레임당 8MiB 예산으로 실행한다. byte 제한만으로 프레임 시간을 보장할 수 없다. 이후 적용도 32명씩 프레임 분할한다. | `SelectOfficercapture.cpp:1106`, `:1140`, `:1181`, `OfficerDetail.cpp:672` | T05/T07/T12 |
| 무장 관계·상생숙명 표시 | 선택 무장별 결과 캐시와 관계 일괄 검색이 이미 있다. 상생 테이블 주소 해석 실패 시 모듈 시작부터 8MiB 검색하는 fallback의 반복·지연은 별도 확인한다. | `SelectOfficercapture.cpp:1725`, `OfficerData.cpp:217`, `:896` | T05/T12 |
| 재야 장수 / 등용·사망 알림 | 30ms 루프에서 호출되지만 전체 5,102슬롯 검색은 초기화 또는 내정↔평정 전환에 묶여 있다. 매 틱 전체 검색으로 오인하지 않는다. | `RoninMonitor.cpp:517` | T13 |
| 능력치 99→100 | 옵션 ON이며 평정 월 전환에서 전체 슬롯 검색. 영역 검증 캐시가 이미 있다. 다량 알림이 T02를 악화시킬 수 있다. | `StatMonitor.cpp:58`, `BattleMonitor.cpp:614` | T02/T13 |
| 전 무장 적극성 | 옵션/주인공 변경 후 안정화 대기 및 1회 적용. 실패 시 재시도 비용은 확인한다. | `StatMonitor.cpp:160` | T13 |
| AI 무장 성장 | 연간 이벤트에서 최대 ID 1,800 범위를 처리한다. 평상시 지속 전체 검색이 아니다. | `AIOfficerGrowth.cpp:496`, `:600` | T13 |
| 특수능력 자동 판정·연말 부여 | worker에 10ms 양보가 이미 존재. 결과 반영·저장·알림은 메인 관리 루프에서 처리한다. | `SpecialAbilityAutoAssign.cpp:280`, `:468`, `:585` | T07/T13 |
| 자동 상성 성장 | 250ms 상태 감시, 분기 전환 시 실제 작업. 스캔 및 후크 상태 수명을 따로 확인한다. | `OfficerData.cpp:1213` | T13 |
| 도시 반란 0 유지 | DrawMenu가 실행될 때마다 전체 도시 순회. SEH 읽기이므로 도시마다 VirtualQuery를 한다고 단정할 수 없다. | `CityInfoWindow.cpp:60`, `:7800` | T04 |
| 도시 자원·내정·일괄 수정 | 대부분 버튼 시점 처리. 도시 리스트 snapshot은 dirty일 때 갱신한다. 전체 목록 제출·큰 일괄 쓰기는 별도 측정한다. | `CityInfoWindow.cpp:348`, `:373`, `:7859` 이후 | T12 |
| 도시 수송·무장 배치·군단 자동 배치 | dirty/조작 시 5,102슬롯 roster 재구축. 자동화는 500ms 상태 감시. UI와 background에서 같은 상태를 만지는 소유권 점검 필요. | `CityInfoWindow.cpp:2118`, `:7114`, `:7706` | T12/T13 |
| 명품 목록·수여·자동 배분 | 명품 ID 목록을 매 프레임 구성·정렬하며, 수여 대상 창도 이름 목록 복사·정렬·전 행 순회를 한다. 자동 배분은 평정 종료 이벤트. | `SpecialtyInfoWindow.cpp:699`, `:1008`, `:1078` | T12 |
| 주점 청부 무한 | 옵션 ON, 내정 상태에서 500ms마다 전체 도시 4슬롯 유지 검사. 동일 메모리 영역 검증 캐시와 세대 보호가 이미 있다. | `TavernMonitor.cpp:336`, `:424`, `:495` | T13 |
| 행동력 / 보주 소모 유지 | 30ms 루프의 작은 값 쓰기. 이것만으로 RAM 폭증을 설명할 근거가 없다. 동일값 쓰기 생략과 활성 상태 gate 정도를 검토한다. | `Menu.cpp:424`, `:445` | T14 |
| 악명 0 유지 | DrawMenu에서 옵션 ON·값이 양수일 때 작은 값 쓰기. 렌더링 의존성을 제거할 대상이지만 단독 자원 폭증 원인으로 볼 근거는 없다. | `Menu.cpp:623` | T04/T14 |
| 보주 개방·보조 보주 | 별도 tick에서 로드 후 bitmap 재적용. 지속 전체 메모리 검색으로 볼 근거는 없다. | `Cheats/Civilian/JewelSettings.cpp`, `TickJewelSettings` | T14 |
| 연회·중개 무제한 | 해당 bit가 이미 0이면 쓰기를 생략한다. 호출 조건과 실제 시간 기반 gate 유지. | `Menu.cpp:430`, `Cheats/Social/Infinitetalk.cpp` | T14 |
| 무한 대화·선물·충성·친밀·호감·공명 | 대부분 토글 시 코드/후크 설치. AOB 검색과 설치 worker가 겹치는 순간, 실패 재시도 및 OFF 중 설치 완료 경쟁을 조사한다. | `Cheats/Social/Infinitegift.cpp`, `Infinitetalk.cpp`, `Loyaltycave.cpp`, `Fastrelationship.cpp`, `InstantLoveCave.cpp`, `Resonancecave.cpp` | T07/T14 |
| 도시 유형 변경 | 대도시·농경·상업·방목은 토글 worker의 단계적 쓰기. 상시 전체 검색은 아니다. 0x80000 단위 보호 변경과 요청 유실 점검. | `Bigcityconvert.cpp`, `NonggyeongCity.cpp`, `SangeopCity.cpp`, `BangmokCity.cpp:41` | T07/T14 |
| 내정 배수·기술점수 / 전법 조건 | 주로 토글 시 AOB/코드 패치. 시작 또는 일괄 활성화의 순간 부하 후보. | `DomesticsMult.cpp`, `Techpointcave.cpp`, `Cheats/System/SkillCondition.cpp` | T14 |
| 기술 초기화 | 200ms worker로 시나리오 조건 감시 후 1회 전체 세력 기술 초기화. OFF 시 최대 1초 대기. | `Techzero.cpp:56`, `:78`, `:128` | T07 |
| 시작 설정·미발견→재야 | 200ms worker. 실제 미발견 보정은 시작 이벤트 gate 뒤에 실행되며 단순 200ms 전체 재작성은 아니다. | `StartSetting.cpp:278`, `:343` | T07/T13 |
| 군주 보너스 | 5초 worker, 테이블 재작성 및 세력 순회. OFF 시 최대 6초 대기. | `FactionLordBonus.cpp:127`, `:235`, `:248` | T07 |
| 도로 차단 1·2 | 100ms worker에서 최대 4개 qword를 값 비교 없이 보호 변경→0 쓰기→복구. | `Roadblock.cpp:304`, `:322` | T07 |
| 월·전기·전투부대 캡처 | 게임 후크가 값/주소를 잡고 관리 루프에서 확인한다. 초기 AOB 및 후크 수명 비용을 구분한다. | `MonthCapture.cpp`, `SystemMonth.cpp`, `TengiCave.cpp`, `Battleunitcapture.cpp` | T07/T14 |
| 자가치료·동토·지형무시·방어건물·투석·천계 | 대체로 전투 진입/토글 때 데이터 패치. 전투 감시가 OFF→ON 일괄 재적용한다. 상시 GPU 작업을 직접 수행하지 않는다. | `BattleMonitor.cpp:403`, `Selfheal.cpp`, `Dongto.cpp`, `Terrainignore.cpp`, `Defbuildingboost.cpp`, `Catapult.cpp`, `Celestia.cpp` | T08/T14 |
| 특수부대 능력·무신·등갑군 | 활성 부대 감시는 틱마다, 공통 버프 확인은 3초, 무신 전법 검사는 500ms. 이미 일부 중복 쓰기 생략이 있다. | `SpecialAbility.cpp:187`, `:427`, `:464` | T08 |
| 전법 횟수 전투 주입 | 전투 캐시 생성/1일차 등에 최대 부대×45 전법 처리. 개별 설정 조회는 반복 mutex 진입. | `BattleMonitor.cpp:148`, `:458` | T08/T03 |
| 날씨·날짜·지형 보정·함선 병기화 | 내부 200/300ms gate와 변경값 쓰기 생략이 있다. 공통 포인터·세대 처리와 강제 호출 중복을 개선한다. | `BattleEnvironment.cpp:142`, `:220`, `:395` | T08 |
| 공성전 1·2 | 600개 terrain table 검색은 적용 단계에 존재; 실패 시 1초 재시도 제한이 이미 있다. | `SiegeWarfare.cpp`, `ApplyShallowTerrainOnce`, `UpdateSiegeWarfare2` | T08 |
| 5번 책략·추가 회복·게이지 최대 | 시작 강제 활성화 요청, 부대 morale 감시, UI/model 후크. Diagnostics라는 함수명에 실제 병력 회복 로직이 섞여 있다. | `Source.cpp:283`, `Spell5HealProbe.cpp:443`, `StratagemSlotProbe.cpp:5564`, `StratagemGaugeMax.cpp` | T09 |
| 전장 무작위·전투 간격·총력전 주기·병력 공방 | 전환 시 데이터 변경 또는 고정 코드 패치 중심. 변경된 규칙이 게임 자체 AI 작업량을 늘리는지는 별도 비교해야 한다. | `BattleMapShuffle.cpp`, `ShortBattleCooldown.cpp`, `TotalWarCycleShortening.cpp`, `TroopCountCombatScaling.cpp` | T14 |
| AI 전쟁 거절·항복·처형 / 포로·부장 / 증원 관련 | `AIRefusalWarFix`, `AIHighHonorSurrenderFix`, `AIExecutionConditionChange`, `DeputyCaptureFix`, `IsolatedCityCaptureFix`, `PrisonerCaptureManagement`, `GovernorPrisonerDisposal`, `ReinforcementArrivalAction`, `ReinforcementDefenderPlacement`는 설치/콜백 중심. 콜백 횟수와 설치 반복 여부를 측정한다. | `Cheats/War/`의 동명 파일 | T14 |
| 오버레이·글꼴·전법 범위 이미지 | 글꼴/11개 이미지는 정상 초기화 시 1회 로드. 이미지 Release 부재는 수명 문제지만 정상 매 프레임 재생성으로 오인하면 안 된다. DX12는 별도 확인. | `Engine.cpp:400`, `:487`, `:615`, `TextureLoader.cpp`, `TacticsEditWindow.cpp` | T11 |
| 파일 로그·메모리 로그 | 일반 로그는 옵션 OFF면 바로 return. `SaveMemoryLog`는 현재 소스에서 호출자를 찾지 못했으므로 상시 I/O 원인으로 분류하지 않는다. | `showlog.cpp:121`, `:233` | T10 |

기존 `crash_analysis.md`는 현재 코드의 일부 수정 전 상태를 설명한다. 현재는 전투 최초 재적용에 날짜/부대 수 gate가 있고, 전투 날짜 검증 및 공성 지형 쓰기의 보호 변경, 일부 특수능력 주소 검증도 추가되어 있다. `InitializeBattleCache()`도 설정 map을 한 번 복사한 뒤 lock을 해제한다. 기존 문서의 ‘중첩 mutex deadlock’ 등을 재현 근거 없이 확정 원인으로 반복하지 말 것.

---

## 5. 수정 AI에게 전달할 공통 프롬프트

당신은 이 저장소의 C++/Windows 게임 내부 DLL 성능 문제를 수정하는 작업자다. 사용자는 삼국지8 리메이크 PK에서 치트를 사용하면 CPU·GPU·RAM이 증가하고 심한 렉이 발생한다고 보고했다. 위 분석을 출발점으로 실제 호출 경로를 재확인하고, 아래 작업을 작은 변경 단위로 수행하라.

### 지켜야 할 조건

1. 게임 규칙, 기존 기능, 단축키, 한글/IME, 사용자 설정 호환성을 유지한다. 기능을 전부 꺼서 성능이 좋아졌다고 완료하지 않는다.
2. 수정 전 기준 측정 → 한 원인군 수정 → 동일 조건 재측정 순서로 진행한다. 실제 측정 없이 ‘CPU 80% 감소’ 등 숫자를 만들지 않는다.
3. 렌더링/게임 후크 안에서 전체 프로세스 스캔, 대규모 JSON 저장, 디스크 로그, worker 종료 대기를 실행하지 않는다.
4. 기능을 worker로 이동하는 것만으로 해결됐다고 판단하지 않는다. 총 작업량, 호출 주기, 메모리 상한, 취소, 스레드 소유권도 함께 해결한다.
5. 게임 소유 객체는 임의 background thread에서 생성·파괴하지 않는다. 필요한 경우 게임이 해당 객체를 다루는 검증된 이벤트/스레드에서 변경한다. UI는 snapshot을 읽고 요청만 전달하는 구조를 우선한다.
6. 성능을 위해 포인터 검증을 무조건 제거하지 않는다. 세션/전투/테이블 세대 변경 시 캐시를 폐기하고, 쓰기 직전 대상의 정체성을 확인한다. 주소가 같아도 객체가 재사용될 수 있다.
7. `VirtualProtect`를 없애거나 넓은 메모리를 영구 RWX로 만들지 않는다. 이미 writable인 검증된 데이터 페이지는 중복 보호 변경을 생략할 수 있지만, 코드 패치와 읽기 전용 데이터는 기존 권한 복구를 보장한다. 서로 보호 속성이 다른 영역을 하나의 oldProtect로 복원하지 않는다.
8. 참조 중인 문자열/실행 메모리를 RAM 절감을 위해 즉시 해제하지 않는다. 게임 보유 문자열 포인터와 실행 중인 code cave를 구분하여 안전한 수명 정책을 만든다.
9. 데이터 경쟁을 해결할 때 전역 mutex로 모든 후크와 렌더링을 직렬화하지 않는다. 짧은 publish, 불변 snapshot, 단일 소유자, bounded queue를 우선 검토한다.
10. 실제 게임을 실행할 수 없다면 정적 수정·빌드·모의 입력 검증까지 수행하고 게임 검증은 ‘미실행’으로 명시한다. 사용자에게 맡길 재현 절차와 남은 불확실성을 남긴다.

### 권장 순서

`T00 → T01 → T02 → T03 → T07 → T04/T05 → T06 → T08/T09 → T10/T11/T12/T13/T14`

DX12가 실제 사용되고 자원 오류가 관측되면 T11을 앞당긴다. 특정 기능으로 증상이 즉시 재현되면 해당 작업을 먼저 하되 공통 계측과 기준 비교는 생략하지 않는다.

## T00. 재현 조건과 저비용 계측부터 확보

**목표:** 배속으로 늘어난 정상 게임 작업량, 치트 자체 병목, 누적 메모리를 구분한다.

수행할 작업:

- 실제 로드된 치트 DLL 종류(`dinput8`/`dxgi`/`hid`), 동일 치트 중복 로드 여부, 외부 `version.dll`, 빌드 구성, 활성 기능을 기록한다. 세 프록시가 빌드된다는 사실만으로 중복 로드됐다고 추정하지 않는다.
- `Menu::Loops`, `Overlay`, 전투/자녀/청부/성장/자동 배치/검색/저장 작업의 횟수·시간을 원본 QPC로 측정한다. 프레임마다 파일에 쓰지 말고 메모리에 집계한 후 주기적으로 출력한다.
- `IsValidPtr`, `VirtualProtect` 래퍼, 패턴 검색, 전체 roster 검색, JSON 저장, 로그, 알림 생성, hook/cave 할당에 호출 수·누적 시간·실제 변경 수를 붙인다. 계측 자체의 overhead도 OFF/ON 비교한다.
- 프레임 시간 p50/p95/p99/max, CPU 사용률, Private Bytes, Working Set, GPU frame time/사용률/전용·공유 메모리, thread/handle 수를 별도로 기록한다. CPU 백분율이 전체 코어 기준인지도 기록한다.
- 알림 queue 길이, retired cave 개수/bytes, 문자열 pool 개수/bytes, 스캔 읽은 bytes, 캐시 재구축 횟수를 추가한다. 측정용 저장소에도 상한을 둔다.
- Visual Studio profiler 또는 ETW/WPR/WPA 등 사용 가능한 도구로 실제 스택을 확인한다. 사용 도구가 없으면 내부 계측 결과와 제한을 명시한다.

완료 기준:

- 동일 세이브·장면·해상도·그래픽 설정·프레임 제한에서 치트 미사용, 로드만 한 상태, UI 접힘/펼침, 개별 기능 ON의 기준표가 있다.
- 배속 ON에서도 실제 10초를 계측했을 때 계측 시계는 실제 10초를 가리킨다.
- ‘모든 토글 OFF’에도 초기화에서 5번 책략 또는 내장 기재 런타임이 실행되는지 기록하여 기준 상태를 명확히 한다.

## T01. 배속 시간과 치트 관리 시간을 분리 [P0]

근거: `Cheats/System/SpeedHack.cpp:92` 이후의 네 시간 후크, `:207`의 설치, `:278`의 `SpeedHack_GetRealDeltaTime`; `Menu.cpp:376`, `:489`; `BattleMonitor.cpp:284`.

현재 문제:

- 치트의 `GetTickCount/GetTickCount64` 기반 throttle·안정화·재시도·세션 timeout도 배속된 값을 읽는다.
- `prevReal`, `fake`를 서로 다른 atomic에서 읽고 다시 쓰는 연산은 복합 상태로서 원자적이지 않다. 여러 스레드가 동시에 들어오면 증가량 손실·시간 역행 가능성이 있다.
- 특히 `hkGetTickCount64`의 `real - prevReal`은 unsigned여서 호출 순서가 엇갈릴 때 underflow 가능성이 있다. 이후 큰 delta를 더하는 경로를 점검한다.
- ON/OFF 시 fake 시간과 원본 시간을 직접 전환하고 clock state를 리셋한다. unsigned 시간차를 사용하는 관리 코드의 즉시 실행·연속 재시도를 유발할 수 있다.
- 실제 QPC를 우회 호출하는 방법이 이미 `SpeedHack_GetRealDeltaTime`에 있으나 알림 animation에 한정되어 있다. 이 함수 자체는 호출자 공유 previous 값이 있으므로 범용 타이머로 그대로 재사용하지 않는다.

수정 요구:

1. 훅 설치 전후 모두 안전한 `RealClock::Now()` 및 단위 변환 API를 만든다. 설치 후에는 해당 API의 원본 trampoline을 사용한다.
2. 치트 관리 주기·실패 backoff·프로파일·알림 만료·캐시 TTL은 RealClock으로 통일한다. 게임 연도/월/턴 같은 의미상 이벤트는 게임 데이터를 그대로 사용한다.
3. 가짜 시계는 호출 간 delta 누적 대신 일관된 `{realAnchor, virtualAnchor, multiplier}` snapshot에 기반한 계산을 우선 검토한다. 배율 변경 때 snapshot을 안전하게 교체하며 후크가 매번 mutex나 heap allocation을 요구하지 않게 한다.
4. ON/OFF·배율 변경 시 반환 시간 연속성과 단조성을 설계한다. 이미 가상 시간이 앞선 상태에서 OFF 때 원본을 즉시 반환하는 정책과 단조성은 충돌할 수 있으므로, 원본+연속 offset 등 정책을 명시하고 게임 동작으로 검증한다.
5. 일반 bool인 활성 플래그·설치 상태와 UI/background 동시 변경을 안전하게 전달한다. MinHook 반환값 확인 및 해당 기능 후크만의 enable/rollback을 구현한다. `MH_ALL_HOOKS`로 다른 기능 상태를 바꾸지 않는다.
6. 배속 1/2/5배에서 게임 진행만 의도대로 달라지고, 치트 내부의 500ms 작업 호출 횟수는 실제 시간 기준으로 일정하게 유지한다.

검증: 동시 호출, 배율 반복 변경, OFF→ON→OFF, 긴 pause, 32비트 tick wrap, 불일치 상태 입력을 테스트한다. 무거운 게임 작업을 5배 실행한 결과의 CPU 상승까지 제거하겠다고 약속하지 않는다.

## T02. 알림 대기열 상한과 스레드 안전성 확보 [P0]

근거: `NotificationManager.cpp:19-31`, `:80-137`, `MenuState.cpp:228`, `Engine.cpp:202`, `StatMonitor.cpp:108-144`.

현재 문제:

- history만 50개이고 `g_notifications`는 무제한이다.
- 화면을 그리지 않으면 animation/삭제도 진행되지 않는다. 표시 속도보다 생성 속도가 빠르면 표시 중에도 backlog가 커진다.
- 생성자는 background/별도 worker/UI에 걸쳐 있고 렌더러는 같은 vector를 순회·erase한다. 공통 동기화가 없다.
- 표시 목록 전체를 매 프레임 순회하고 vector 중간 삭제로 뒤 원소들을 이동시킨다.

수정 요구:

1. 생산자용 bounded queue와 UI 소유 표시 목록을 분리한다. 알림 ID, 종류, 생성 실제 시간, 집계 횟수를 가진다.
2. 대기·표시·history에 각각 상한을 둔다. 초기 제안은 대기 128건, 동시 표시 8건, history 50건이며 실제 UX에 맞게 조정한다. 초과 시 같은 유형을 합치거나 생략 건수를 표시한다.
3. 대규모 능력치/배치 변경은 ‘N명 갱신’으로 집계하고 상세는 한정된 기록에서 확인하게 한다. 기존 작업 완료/오류 정보가 사라지지 않게 한다.
4. 화면이 닫혀 있어도 실제 시간 TTL로 만료시킨다. 표시하지 않는 backlog 때문에 ImGui를 계속 강제로 실행시키지 않는다.
5. UI는 짧게 queue를 drain한 뒤 lock 없이 그린다. ImGui 함수는 producer에서 호출하지 않는다.

검증: UI 표시/미표시 각각에서 여러 producer가 수만 건을 추가해도 개수·메모리가 상한 내에 있고, UI 접근 위반과 반복 전체 재할당이 없어야 한다.

## T03. 다중 편집을 한 번의 설정 변경·저장으로 처리 [P0]

근거: `Cheats/System/SkillCountManager.cpp:14`, `:90`, `:99`; `OfficerDetail.cpp:289-299`, `:1442-1500`; `SelectOfficercapture.cpp:2268`; `Cheats/Officer/StatMonitor.cpp:249-338`.

현재 `SetTargetSkillCount()`는 매 호출마다 `SaveSkillCounts()`를 실행하며, 저장은 `g_skillCountMutex`를 잡은 상태로 전체 map을 직렬화하고 파일을 쓴다. 선택 무장 N명에게 적용하면 N회 이상의 전체 파일 저장이 발생한다. 파일 크기도 N에 비례해 커지는 경우 총 직렬화 작업량은 이차적으로 커질 수 있다.

수정 요구:

1. 단건/다건을 공통 batch API로 처리한다. 변경된 값만 map에 반영하고 작업 완료 후 한 번 저장한다. 현재 무장이 선택 목록에도 포함된 경우 중복 처리하지 않는다.
2. 잠금 안에서는 변경 반영 또는 불변 snapshot 확보까지만 수행한다. JSON 직렬화·파일 I/O는 잠금 밖의 단일 writer에서 처리한다.
3. 여러 저장 요청은 revision으로 합치고 오래된 snapshot이 최신 파일을 덮지 못하게 한다. 임시 파일→교체 방식, 실패 상태, 종료 시 마지막 dirty revision 처리를 마련한다.
4. 큰 메모리 편집도 UI에서 일괄 완료하지 말고 취소·진행률·세션 확인이 가능한 작업으로 나눈다. 특히 `ApplyBatchOfficerEdit()`의 5,102슬롯×최대 59개 항목은 바뀌는 byte마다 보호 설정/복구를 반복한다. 변경 목록을 먼저 만들고 같은 유효 객체/메모리 영역의 쓰기를 묶어 보호 변경 횟수를 줄인다. 페이지 경계와 원래 보호 속성을 보존하고, 보호 변경 실패 시 쓰지 않는다. 게임 메모리 쓰기 시점의 안전성은 유지한다.
5. 단순 조회인 `GetTargetSkillCount()`는 한 프레임/작업당 snapshot 또는 revision cache를 사용하여 수십·수백 번의 같은 mutex 획득을 줄인다.
6. 일반 `SaveConfig()`도 slider 변경마다 저장되는지 계측하고, 필요 시 입력 확정/짧은 debounce로 합친다. 저장 지연으로 변경사항이 유실되지 않게 한다.

검증: 다수 무장 전법 횟수/특수능력 변경 한 번에 JSON commit 1회, 최종 설정·해제 결과 동일, 빠른 연속 클릭 후 재실행에서도 최종 값 유지. 저장 중 다른 전투 조회가 파일 I/O 시간만큼 대기하지 않아야 한다.

## T04. 자녀 감시·도시 반란 유지와 렌더링 분리 [P1]

근거: `Menu.cpp:1077-1082`, `ChildEarlyAppearance.cpp:228-309`, `:319-344`, `CityInfoWindow.cpp:7800-7821`.

수정 요구:

1. 자녀 목록·예약 상태·임신 snapshot을 한 관리 주체가 갱신하고 UI에는 읽기 전용 snapshot을 게시한다. 렌더러와 worker가 `g_children`을 동시에 수정하는 이동은 금지한다.
2. 자녀 검색은 주인공/roster/세션 변경과 출생 관련 변화에 맞춰 갱신한다. 신뢰할 이벤트가 없으면 실제 시간 기반의 느린 fallback과 시간 예산이 있는 분할 검색을 사용한다.
3. 조기 임관 예약의 완료 판정은 게임 연도 변화로 처리한다. 목록 창을 열고 닫는 것이 기능 실행의 조건이 되지 않게 한다.
4. 도시 반란 0 유지도 FPS 대신 실제 시간 또는 게임 변화 기준으로 처리한다. 임시 목표 100~250ms 주기는 실제 효과 유지 여부를 확인한 뒤 정한다. 읽기/쓰기 건수와 변경 수를 기록한다.
5. 창 열기 즉시 갱신이 필요한 경우 요청을 보내고 기존 snapshot/로딩 상태를 표시한다. 임신 관리의 직접 경로 우선 정책을 유지한다.
6. `DrawMemoryNotepadWindow()`가 `Menu::Render`와 `DrawMenu` 양쪽에서 호출되는 중복도 제거한다. UI 전체 숨김 판정은 모든 독립 창·위젯·활성 알림을 정확히 고려한다.
7. `DrawMenu`의 악명 0 유지도 같은 관리 주체로 옮겨 메뉴 표시 여부와 기능 실행을 분리한다. 작은 값 유지 하나를 별도 고빈도 worker로 만들지는 않는다.

검증: 메뉴 접힘/완전 닫힘/다른 보조창만 표시한 상태에서도 예약·반란 유지 동작이 같고, 30/60/144 FPS에 따라 데이터 검색 횟수가 증가하지 않는다.

## T05. 전체 프로세스 스캔에 범위·예산·취소 추가 [P1]

근거: `SelectOfficercapture.cpp:2939-3053`, `:1106-1190`, `:1495`; `OfficerDetail.cpp:440-548`, `:672`; `debug.cpp:105-220`, `:223-338`, `:382-393`; `OfficerData.cpp:217`; `PregnancyManager.cpp`의 직접 경로 및 제한 영역 검색.

특히 `ImGui::Selectable → SetTraitID → FindTraitDataPointerByID → FindTraitObjectInProcessMemory`는 기재 객체 캐시가 실패했을 때 UI 스레드에서 전체 readable 메모리를 훑는 경로다. 기재 무작위 부여도 수집 단계만 worker이고, 객체 검색 단계는 프레임당 8MiB의 byte 예산으로 렌더링 경로에서 처리된다. 둘 다 사용자 조작 직후 렉을 재현할 우선 시나리오다.

수정 요구:

1. 배우자 검색은 검증된 관계 데이터/roster/직접 경로를 먼저 사용한다. 전체 메모리 탐색은 직접 경로가 실패한 경우의 명시적인 진단 fallback으로 분리한다.
2. 공통 scan job에 실제 시간 budget, 읽을 byte budget, 진행률, 취소 token, session generation, 결과 상한을 둔다. 한 번에 하나의 대규모 scanner만 실행하게 한다.
3. `s_isSpouseScanning` 확인→대입을 원자적인 작업 시작으로 바꾸고 detached thread 대신 수명이 관리되는 worker를 사용한다. 디버그 스캐너도 worker 시작 전 running을 확보하여 중복 시작을 막는다.
4. 64KiB 버퍼를 영역마다 새로 만들지 말고 재사용한다. 디버그 다음 검색의 `neighborhood`도 후보마다 재할당하지 않는다.
5. 결과를 지역 버퍼에 모아 묶어서 게시한다. hit마다 UI와 공유하는 mutex를 잡지 않는다. 기존 약 10만 결과 상한과 표시 1,000개 제한을 구분한다.
6. 배우자 포인터가 실제로 정렬된다는 증거가 있을 때만 검색 보폭을 8로 바꾼다. 주석만 믿고 바꾸지 않는다. 검색 chunk 경계에 걸친 패턴도 놓치지 않는다.
7. 취소·세이브 로드·주인공 변경·DLL 종료 시 기존 작업을 폐기하고, 과거 세대의 주소를 결과/편집 대상으로 게시하지 않는다.
8. 기재 단건 변경은 캐시 실패 시 검색 요청·대기 상태를 반환하고 UI의 전체 동기 검색을 제거한다. 게임이 제공하는 객체 카탈로그/생성 경로를 우선 조사하고, fallback은 관리되는 scan job으로 처리한다. 완료 직전에 대상 무장·슬롯·기재 객체의 현재 세대를 다시 검증한다. 선택을 바꾼 뒤 도착한 이전 결과가 덮어쓰지 않게 한다.
9. 기재 일괄 검색의 고정 8MiB/frame을 실제 시간·bytes/초 예산으로 바꾼다. 32명/frame 적용도 시간 예산을 함께 사용하고, 창 표시/FPS가 작업 처리율을 결정하지 않게 한다. 읽기·쓰기의 스레드 안전성을 확인한 뒤 관리 루프/worker로 이동한다.
10. 기재 객체 미발견 및 `TryResolveSynergeticTable()`의 8MiB fallback 실패를 세션·데이터 revision별로 잠시 기억해 반복 전체 검색을 막는다. 실제 객체가 나중에 생길 수 있으므로 영구 실패 캐시로 만들지 않는다. 기존 선택 무장별 관계 캐시와 한 번의 관계 테이블 순회를 보존한다.

검증: 충분히 큰 프로세스 메모리에서 검색 중 프레임 지연·읽은 bytes/초·page fault·Working Set을 비교한다. 사용 중인 기재/미사용 커스텀 기재/객체를 찾을 수 없는 기재를 각각 선택하고, 무작위 일괄 부여도 비교한다. 중지 후 짧은 정해진 시간 안에 종료하고 스레드 수가 원래 수준으로 돌아와야 한다.

## T06. 기재 문자열 및 실행 메모리 수명 개선 [P1]

근거: `TraitConfigRuntime.cpp:391-475`, `:1814-1834`, `:1944`; `TraitTextNameHook.cpp:20`, `:326-355`; `TraitTextDescHook.cpp:21`, `:353-370`; `TraitTextSpecialDescHook.cpp:20`, `:367`.

현재 문제:

- JSON의 파일 시간이 바뀌면 동일 내용이어도 다시 파싱/게시할 수 있다. 문구가 있는 최대 254개 항목에 대해 이름/설명/format 설명을 각각 VirtualAlloc한다.
- 이전 문자열을 즉시 해제하지 않는 이유는 게임 UI가 주소를 보유할 수 있기 때문이다. 하지만 기존 주소 배열을 덮어써 소유권을 잃고, 실패 도중의 부분 할당도 회수하지 않는다.
- 문구 후크는 이름 0x40000, 일반 설명 0x40000, 특수 설명 0x8000 크기의 cave를 만든다. 셋 모두 재적용되는 경우 한 세대당 **최대 544KiB**의 이전 cave가 추가 보관될 수 있다. 이는 매 프레임 누수가 아니라 적용/해제 반복에 따른 증가다.

수정 요구:

1. 내용 hash/revision으로 실질적인 변경 여부를 비교하고 동일 문구는 안정된 주소를 재사용한다. 이름/설명/format 문구를 intern하거나 안정된 arena로 관리하여 작은 문자열별 VirtualAlloc을 줄인다.
2. 모든 할당의 owner를 보관한다. 게시 전 실패는 rollback하고, 게시 후 참조될 수 있는 세대는 추적한다. 여러 포인터 배열을 동시에 읽는 후크가 일관된 snapshot을 보게 한다.
3. 게임이 문자열을 언제까지 보유하는지 입증되지 않으면 즉시 해제하지 않는다. 동일 텍스트 재사용과 총 보관 budget을 적용하고, budget 도달 시 무한 할당 대신 명시적인 적용 실패/재시작 필요 상태를 반환한다. 임의 길이로 사용자 문자열을 잘라 메모리를 줄이지 않는다.
4. 문구 후크는 불필요한 재설치를 건너뛴다. 가능하면 고정 trampoline + 변경 가능한 읽기 전용 데이터 snapshot 구조로 바꾸되 ABI/register/원본 분기/format 토큰을 보존한다.
5. retired cave 회수는 실행 중인 후크와 게임이 보유한 문자열 참조까지 고려한다. 단순 `VirtualFree` 루프를 추가하는 수정은 금지한다. 안전한 quiescence를 입증할 수 없다면 세대 상한을 두고 제한을 문서화한다.
6. 생성 코드의 모든 수정 문자열 순차 비교가 실제 병목인지 계측한다. 검증된 ID 또는 포인터 기반 lookup으로 대체할 수 있으면 전환한다.

검증: 같은 설정 100회 재적용 시 추가 할당이 거의 없어야 한다. 내용이 실제 바뀌는 반복 작업에서도 설명 가능한 상한 내에 있어야 한다. 게임 문구 화면이 열린 상태, 설명 tooltip, `%d`/`%%`, 한글, 로드 전후에서 사용 후 해제가 없어야 한다.

## T07. worker 수명과 기능 OFF의 동기 대기 제거 [P1]

근거: `MenuSections.cpp:1933`, `FactionLordBonus.cpp:235-277`, `Roadblock.cpp:304-358`, `Techzero.cpp:128-151`, `StartSetting.cpp:291-321`, `Source.cpp:226` 이후.

수정 요구:

1. 군주 보너스의 `Sleep(5000)`를 stop event로 즉시 깨울 수 있는 timed wait로 바꾸거나 공통 실제 시간 scheduler로 이동한다. UI에서 최대 6초 join하는 구조를 제거한다.
2. 도로 차단의 이미 0인 값 쓰기와 보호 변경을 생략한다. 군주 보너스 테이블도 값 변경/세대 변경 시에만 쓰고 느린 무결성 확인을 유지한다.
3. 기술 초기화·시작 설정·도시 변환·AOB 설치 worker에 requested state와 applied state를 구분한다. 설치 중 OFF 요청이 왔는데 완료 후 다시 ON되는 현상을 막는다.
4. 공유 bool/포인터/map의 동기화를 정리한다. `WaitForSingleObject` timeout 반환 후 thread handle을 닫았다고 스레드가 종료된 것으로 처리하지 않는다. 종료 전 map clear/복구 또는 다음 worker 시작이 겹치지 않게 한다.
5. 각 worker는 stop token과 session generation을 확인한다. 프로세스 종료와 일반 DLL unload의 정리 경로를 구분하고, loader lock 안에서 복잡한 join을 하지 않는다.
6. `g_Engine.release()`의 소유권 포기와 `WCMUpdate` join 생략 등 종료 경로도 정리한다. DLL 코드가 unload된 뒤 detached worker/게임 후크가 진입할 수 없게 한다.
7. 일회성 worker를 무조건 주기 worker로 바꾸지 않는다. 도시 유형 변경의 200ms 두 단계처럼 기능에 필요한 순서·간격은 유지한다.

검증: 해당 기능을 빠르게 20회 ON/OFF해도 thread/handle 수가 누적되지 않고 마지막 요청대로 동작한다. UI OFF 호출이 worker의 5초 sleep을 기다리지 않아야 한다. 종료·세이브 로드 중 쓰기 작업은 이전 세대에 접근하지 않아야 한다.

## T08. 전투 상태와 특수능력 작업을 세대·변화 기준으로 정리 [P1]

근거: `BattleMonitor.cpp:266-584`, `SpecialAbility.cpp:89-140`, `:187-254`, `:463-502`, `BattleEnvironment.cpp:220`, `SiegeWarfare.cpp`.

수정 요구:

1. 로딩/포진/실제 전투/전투 종료를 구분하는 명시적 상태와 generation을 둔다. 유효한 날짜·부대 수·게임 상태·캡처 신호의 의미를 검증한다. 숫자 하나를 임의로 ‘전투 상태’라고 정하지 않는다.
2. 현재 최초 전쟁 패치 재적용에는 날짜/부대 수 gate가 있지만 캐시 구축 및 다른 처리에는 동일 gate가 없다. 각 기능이 포진 단계에서 필요한지 구분해 올바른 상태에 배치한다.
3. 유효 부대 수의 상한(현재 정상 판정 60)을 캐시 구축·모든 소비자에 일관되게 적용한다. 현재 일부 경로는 `unitCountTotal > 0`만 검사한다.
4. gameBase가 0이 되거나 세대가 바뀔 때 모든 관련 캐시를 함께 폐기한다. `s_isWarModsApplied`가 false라는 이유로 다른 캐시 정리를 건너뛰지 않는다.
5. 캐시 구축 함수는 성공·부분 준비·실패를 구분한다. 외부에서 함수 호출만 끝났다고 `s_isCacheBuilt=true`로 설정하지 않는다. 증원·부대 교체·skill table 재생성도 반영한다.
6. `Set(false)→Set(true)` 일괄 재적용 대신 새 세대·새 설정에 필요한 차이만 적용한다. 패치 성공 후 applied 상태를 갱신한다.
7. 무신의 최대 `부대 수×45×2` 수준 레코드별 검증을 세대 내 skill table snapshot/영역별 검증으로 줄인다. 바뀐 제한만 쓰고, 게임이 덮어쓰는지 확인한 후 느린 fallback 검사 주기를 정한다.
8. 활성 부대에 즉시 필요한 효과, 등갑군의 턴 판정, 단순 테이블 유지 검사를 분리한다. 일괄적으로 모든 주기를 늘려 효과를 놓치지 않는다.
9. 전법 횟수 주입도 한 번의 설정 snapshot을 사용한다. `InitializeBattleCache`의 기존 snapshot 최적화를 유지한다.
10. 환경 force 호출, 전투 진입, 날짜 변경이 같은 틱에 중복되면 하나의 갱신 결과를 공유한다. 공성 terrain 적용과 retry는 실제 시간 backoff를 사용한다.

검증: 포진→전투 1일차→행동 부대 전환→증원→전투 종료→다음 전투→전투 중 저장 로드 순서에서 효과가 유지되고, 같은 세대의 캐시 구축/패치 횟수가 불필요하게 증가하지 않아야 한다.

## T09. 5번 책략의 기능 처리·진단·객체 소유권 분리 [P1]

근거: `Source.cpp:283-292`, `BattleMonitor.cpp:482-503`, `Spell5HealProbe.cpp:435-554`, `StratagemSlotProbe.cpp:3046`, `:3180`, `:5401`, `:5564`.

수정 요구:

1. 초기 `SetStratagemFiveFeature(true)`의 의도와 사용자 설정 로드 순서를 확인한다. early bridge 설치가 필요한 시점은 유지하되, 설치 준비와 실제 기능 활성화·진단 활성화를 별도 상태로 분리한다. 사용자 기능 의도를 임의로 바꾸지 않는다.
2. `UpdateSpell5TargetDiagnostics()`는 실제 병력 회복을 수행한다. 함수 전체를 ‘디버그 기능’이라며 제거하거나 로그 옵션 뒤에 숨기지 않는다.
3. 회복 감시와 진단 dump를 분리한다. 회복 설정이 0/비대상인 경우 부대 전체 감시는 건너뛸 수 있다. 다시 활성화할 때 기준값을 재설정하여 과거 morale 변화를 회복으로 오인하지 않게 한다.
4. 가능하면 실제 책략 사용 이벤트의 검증된 ID/대상으로 회복을 적용한다. 현재 morale 증가량 추정에 의존하는 방식은 성능뿐 아니라 다른 사기 상승 이벤트를 회복으로 오인할 수 있으므로 별도 회귀 검증한다.
5. ready 상태의 `RefreshStratagemFiveBattleRuntime`은 owner/model/registry 변화가 없으면 가벼운 확인으로 끝내고, 필요한 재결합은 관련 게임 UI 이벤트에서 수행한다. 호출 빈도를 줄여 초기화 타이밍을 깨뜨리지 않는다.
6. `UpdateStratagemFiveUiRuntimeProbe`는 이미 layout당 로그 중복 방지가 있으나, 로그 옵션 OFF일 때 진단을 위한 추가 메모리 탐색 자체도 생략한다. 기능상 필요한 검증은 남긴다.
7. `allocFn(..., 0x1D8, ...)`로 만든 버튼의 생성 실패, initialize 실패, registry 등록 성공 후 게임 소유권 이전, dialog 파괴 시 정리를 추적한다. 현재 생성 후 여러 early return 및 세대 리셋 시 포인터만 0으로 만드는 경로가 있다. 게임이 정리하는지 확인하기 전 누수 확정 또는 임의 free를 하지 않는다.

검증: 책략창 100회 열기/닫기, 여러 전투 및 저장 로드에서 버튼 live 수와 heap 사용이 안정적이어야 한다. 4/5개 model, OFF 요청의 지연 적용, 세대 보호, 설정한 회복량을 보존한다.

## T10. 로그를 bounded 비동기 기록으로 변경 [P1]

근거: `showlog.cpp:121-167`, `:176-213`, 게임 후크와 worker의 `AddLog` 호출.

수정 요구:

1. 파일 로그 producer는 고정 크기 또는 상한 있는 queue에 넣고 반환한다. writer 한 개가 파일을 유지하며 batch로 기록한다.
2. UTF-8 변환·시간 문자열 생성·filesystem 조회·open/close는 가능한 한 writer에서 수행한다. 큐가 꽉 차면 반복 debug 로그를 묶거나 버리고 drop 수를 기록한다. 게임 후크를 기다리게 하지 않는다.
3. 파일 크기/보관 개수 제한을 추가한다. 메모리 디버그 로그는 현재 1,000개 상한을 유지하되 vector front erase 대신 ring buffer 등으로 관리한다.
4. UI는 로그 snapshot을 얻은 뒤 잠금 없이 filter/clipper로 그린다. `g_logMutex`를 잡은 채 ImGui 전체 로그를 그리지 않는다.
5. 현재처럼 파일/화면 로그 모두 OFF면 빠르게 반환하도록 유지한다. 인수가 비싼 진단 호출은 그 계산 전에도 gate한다.

검증: 로그 ON/OFF의 p99 차이, 초당 대량 로그 때 queue 상한, 디스크 지연·쓰기 실패 상황, 종료 시 제한된 flush를 확인한다.

## T11. 렌더링 자원·DX12 동기화 및 실제 표시 gate 정리 [P1, API 조건부]

근거: `Engine.cpp:307-340`, `:343-552`, `:555-644`, `:209-240`; `MenuState.cpp:228`; `Engine.cpp` 소멸자와 `g_RangeTextures` 사용처.

수정 요구:

1. 먼저 실제 게임의 renderer가 DX11인지 DX12인지 로그/측정으로 확인한다. DX12 코드가 있다는 이유만으로 DX11 증상의 원인으로 단정하지 않는다.
2. DX12는 frame context별 allocator와 fence 값을 관리하고, 해당 GPU 제출이 완료된 후에만 Reset한다. 이벤트 기반 대기를 사용하고 매 프레임 전체 GPU flush를 넣지 않는다. queue 선택도 실제 swapchain과의 관계를 검증한다.
3. `InitImGui`는 임시 RAII owner로 자원을 만든 뒤 전체 성공 시 commit한다. queue 대기/descriptor 생성 실패 등의 재시도에서 GetDevice 참조·ImGui context·heap이 누적되지 않게 한다.
4. 모든 HRESULT, backbuffer 개수/index, CommandQueue 수명, device removed를 처리한다. 배열 크기 8로 자르기만 하고 실제 index를 그대로 쓰는 경로를 검증한다.
5. 현재 ResizeBuffers hook은 구현되어 있지만 설치 코드에서 연결하지 않는다. resize/device recreation에 대한 안전한 release/recreate 순서를 구현하고 실제 설치 여부까지 확인한다.
6. DX11의 11개 range SRV를 명확한 owner에 넣고 정상 unload/device recreation 시 해제한다. 글꼴/이미지를 매 프레임 다시 로드하는 구조는 만들지 않는다.
7. 글꼴 atlas 실제 크기와 DPI별 메모리를 측정한다. 한글 전체 지원을 무조건 삭제하지 말고 필요할 때 atlas/oversampling을 조정한다.
8. 표시 창/위젯/알림이 전혀 없을 때 ImGui frame과 GPU 제출을 건너뛴다. 작은 접힌 메뉴나 위젯이 보일 때는 필요한 표시를 유지한다. 메뉴만 껐다는 이유로 알림·독립 창이 사라지지 않게 한다.
9. GPU 원인 분석을 위해 게임 자체 프레임 제한을 고정한 비교와 제한 없는 비교를 모두 수행한다. 사용자가 요청하지 않은 전역 FPS 제한으로 문제를 숨기지 않는다.

검증: 창 열기·닫기, Alt-Tab, 해상도 변경, window resize, 여러 전투/로드에서 device/heap/SRV 수가 누적되지 않아야 한다. DX12를 사용할 수 없다면 해당 경로의 실게임 검증은 미실행으로 남긴다.

## T12. 무장·도시·명품 UI의 목록 캐시와 큰 작업 개선 [P2]

근거: `SelectOfficercapture.cpp:1445-1465`, `:2561-2628`, `:2763`; `SpecialtyInfoWindow.cpp:1078-1085`; `CityInfoWindow.cpp:2118`, `:7114`; `BatchOfficerEditor.cpp:130`.

수정 요구:

1. 기존 모든 무장 목록의 clipper·분할 구축·검색 debounce를 보존한다. 고정 384개 처리도 시간이 초과되면 중단할 수 있는 실제 시간 budget으로 개선한다.
2. 명품 ID 목록과 수여 대상 이름 목록은 각각 관련 데이터 revision이 바뀔 때만 복사·정렬한다. 검색 결과 인덱스를 캐시하고 보이는 행만 제출한다.
3. 기재 목록/등급별 후보 목록은 기재 설정 revision으로 캐시한다. 단순 화면 redraw마다 전체 ID vector를 만들어 정렬하지 않는다.
4. 도시 roster는 dirty/세대/명령 완료에 맞춰 snapshot을 재구축한다. 모든 refresh를 단순히 삭제하지 말고 자동 배치 후 UI가 실제 결과를 표시하게 한다.
5. 도시 UI와 `RunYearlyRearSupport/RunCorpsDeploymentPhaseMonitor`가 공유하는 vector/map/선택 상태를 조사하고, 작업 상태의 owner를 통일한다. UI용 상태와 자동화용 상태를 같은 mutable 객체로 사용하지 않는다.
6. 목록에 가변 높이 행이 있다면 clipper의 전제에 맞춰 처리한다. 단순 clipper 삽입으로 상세 펼침·검색 위치 이동을 깨뜨리지 않는다.

검증: 큰 roster·명품 목록·수여 대상·무장 배치 창에서 정지 상태의 할당/정렬 횟수가 안정화되고, 이름 수정·필터·다중 선택·로드 후 stale data가 없어야 한다.

## T13. 평정·연간·분기 자동화를 이벤트와 예산으로 관리 [P2]

근거: `Menu.cpp:493-553`, `BattleMonitor.cpp:589`, `RoninMonitor.cpp:517`, `StatMonitor.cpp`, `AIOfficerGrowth.cpp`, `SpecialAbilityAutoAssign.cpp`, `OfficerData.cpp:1213`, `CityInfoWindow.cpp:7706`, `TavernMonitor.cpp:424`, `TraitConfigRuntime.cpp:1297`.

수정 요구:

1. GameSnapshot에 세션, 날짜, 내정/평정 상태, roster generation을 담아 같은 틱의 반복 포인터 체인 탐색을 줄인다.
2. 여러 자동화가 평정 진입 순간 동시에 수천 슬롯을 검색한다면 읽기 snapshot을 공유하거나 예산을 나눈다. **AI 성장 완료 후 특수능력 자동 판정**이라는 현재 순서를 유지한다.
3. 전체 이벤트를 `(session, year, month, phase, featureRevision)` 등으로 식별한다. 로드·활성화·상태 중간값에서 이벤트가 중복 적용되거나 필요한 적용이 빠지지 않게 한다.
4. 재야/등용/사망 감시는 이미 상태 전환 기반이므로 계속 주기 전체 스캔으로 바꾸지 않는다. 동일 루프에서 반복하는 작은 상태 해석만 공유한다.
5. 주점 청부는 현재 세대·live source 검증을 유지한다. 전체 도시 4슬롯 검사 비용이 실제로 크면 dirty 도시/슬롯 우선 처리와 느린 전체 점검을 도입한다. 이미 사라진 청부 포인터를 성능 때문에 재사용하지 않는다.
6. 기능 OFF는 잔여 작업 취소와 필요한 상태 정리 후 빠르게 return한다. 완료 알림은 T02의 묶음 알림을 사용한다.
7. `CustomMonthlyTraitUpdate()`의 원본 월간 처리→커스텀 효과 166 적용 순서를 유지한다. 1,801명×3슬롯에서 같은 기재 효과를 반복 해석하는 비용을 측정하고, 필요하면 실제 효과 데이터의 변경을 반영하는 revision별 합산 캐시를 사용한다. 부호 있는 값·상한·적용 횟수를 보존한다. 게임이 해당 후크 반환 시 완료된 효과를 필요로 한다면 임의로 다음 프레임까지 지연시키지 않는다.

검증: 동일 달을 반복 관측해도 실제 전체 작업은 한 번만 수행되고, 새 세션/날짜 변화에서는 정상 재개된다. 원래 한 번만 하던 기능을 매 틱 실행하는 회귀가 없어야 한다.

## T14. 공통 패턴 검색·메모리 접근·토글 패치 정리 [P2]

근거: `Cheats.cpp:20-87`, `:162-178`, `MemoryUtils.cpp`, `ConfigBase.inc:736-827`, 각 `Set*` 함수.

수정 요구:

1. `FindPattern`의 빈 패턴, `end <= start`, 검색 범위보다 긴 패턴, 마지막 시작 위치를 처리한다. 현재 길이 뺄셈의 underflow와 `< searchLen` 때문에 마지막 후보를 놓치는 경계를 수정한다.
2. 코드 서명은 검증된 PE section/읽기 가능한 범위를 대상으로 검색한다. 실제 호출자가 데이터 서명을 찾는 경우는 적절한 section을 지정한다. `exe+0x3000000` 같은 고정 범위를 무조건 읽지 않는다.
3. 모듈/빌드/서명별 검색 결과와 실패를 캐시한다. 코드 패치 후 원본 바이트가 바뀌는 점을 고려하여 캐시의 expected state를 검증한다. 실패를 매 프레임/매 틱 전체 재검색하지 않는다.
4. `InitCheats`의 기반 주소 탐색과 `LoadSkillCounts`를 분리한다. 시작 재시도 worker가 주소를 못 찾을 때 같은 JSON을 반복 로드하지 않게 한다.
5. `IsValidPtr`는 시작과 끝 영역만 검사한다. 큰 범위의 중간 영역 보호 상태도 필요한 경우 순회 확인한다. 읽기 가능과 쓰기 가능을 구분하고, 이 검사로 객체 생존/동시 변경까지 보장된다고 가정하지 않는다.
6. region cache는 검색 한 번 또는 명확한 snapshot 수명 안에서만 사용한다. 프로세스 전체의 페이지 유효성을 영구 캐시하지 않는다. SEH 실패 시 stale 세대를 폐기하고 backoff하여 예외를 정상 polling 방식으로 쓰지 않는다.
7. 행동력·보주 같은 작은 값 유지, 대부분의 토글 패치에는 필요한 만큼만 변경값 쓰기/성공 상태 관리를 적용한다. 성능 측정 없이 모두 복잡한 worker로 바꾸지 않는다.
8. `ApplyStoredConfigs`는 현재 apply 함수 호출 전에 appliedState를 바꾼다. 성공/대기/실패가 반영되도록 결과 계약을 바꾸고 제한된 재시도로 관리한다.
9. 각 war/social hook의 할당을 설치마다 중복 생성하지 않고, 빠른 ON/OFF 후 오래된 작업이 패치를 다시 넣지 않게 한다. 원본 명령 복원·instruction cache flush·활성 실행 코드 수명을 함께 검증한다.

검증: scanner 경계 입력과 보호 영역 경계, 지원하지 않는 게임 빌드, 서명 실패, 토글 중 로드에 대한 테스트를 마련한다. 최적화 후 잘못된 주소에 쓰는 경우가 증가하면 실패다.

## 6. 실게임 검증 시나리오

각 시나리오는 가능한 한 동일 세이브·카메라·해상도·그래픽·프레임 제한에서 비교한다. 워밍업 뒤 최소 수 분을 기록하고, 각 사례를 반복해 편차를 확인한다. 시간은 RealClock 또는 외부 계측 기준으로 잰다.

| 시나리오 | 비교 조건 | 핵심 확인 |
|---|---|---|
| 유휴 전략 화면 | 치트 미로드 / 로드만 / 모든 일반 토글 OFF | 상시 runtime·초기화 비용, worker 수, 기본 메모리 |
| 메뉴 상태 | 완전 숨김 / 접힘·위젯 / 펼침 / 보조창만 열림 | Present 경로, 검색 횟수, UI 기능 유지 |
| 배속 | OFF / 1배 / 2배 / 5배, 반복 변경 | 치트 작업 실제 호출률, 시계 단조성, 게임 자체 비용 |
| 다중 편집 | 1명 / 수십 명 / 가능한 최대 선택 | 저장 commit 수, UI p99/max, 결과 설정 |
| 자녀·임신 | 창 닫힘·열림, 출생·연도·주인공 변경 | snapshot 갱신, 예약 적용, 전체 검색 빈도 |
| 배우자·디버그 검색 | 각각 단독 검색 / 취소 / 로드 중 검색 | bytes/초, CPU, page fault, 취소 지연, 결과 세대 |
| 기재 선택·무작위 부여 | 캐시 있음 / 미사용 커스텀 / 미발견 객체 / 큰 일괄 작업 | Present 점유, 검색 예산, 결과 정확성, FPS 의존성 |
| 전투 | 포진 / 1일차 / 부대 전환 / 증원 / 종료 | 캐시 재구축·전법 검사·보호 변경 횟수 |
| 전투 옵션 분리 | 무신 / 환경 / 공성 / 5번 책략 각각 단독 | 기능별 CPU 시간과 정확한 효과 |
| 책략창 반복 | 100회 열기·닫기, 전투·세이브 전환 | live 버튼·할당 수, RAM 기울기, 기능 유지 |
| 평정 자동화 | 연말·연초·분기 전환, 여러 자동화 ON | 이벤트 중복, 성장→특수능력 순서, 일시 부하 |
| 커스텀 월간 효과 | 효과 166 기재 보유자 수 변화 / 반복 로드 | 월간 후크 시간, 원본 후처리 순서, 지급량·부호·상한 |
| 알림 폭주 | 다량 능력치 상승, UI 표시/미표시 | queue 상한·생략 집계·RAM·동시 접근 |
| 문구 반복 적용 | 동일 내용 100회 / 실제 변경 반복 | 문자열/cave 할당량, 상한, 문구 참조 안전성 |
| worker 토글 | 군주·도로·시작·기술 ON/OFF 반복 | UI 종료 대기, thread/handle 증가 여부 |
| 로그 | OFF / 파일 ON / 화면 ON | writer queue, I/O와 프레임 지연 |
| 렌더러 수명 | 창 변경·Alt-Tab·해상도 변경 | D3D 자원 수명, resize, device 오류 |
| 장시간 | 30~60분 플레이 + 로드·전투 반복 | Private Bytes·GPU memory가 무한 증가하지 않는지 |

측정 결과는 `PERFORMANCE_VALIDATION.md` 등에 환경·기준값·수정값·재현 절차·미실행 항목을 함께 남긴다. 서로 다른 배속·FPS·게임 장면의 CPU 숫자를 직접 비교하지 않는다.

## 7. 완료 기준과 제출물

다음은 보장된 성능 수치가 아니라 작업 완료를 판단하기 위한 기준이다.

1. CPU·GPU·RAM 증가에 대한 원인별 증거가 있고, 확인하지 못한 부분은 명시되어 있다.
2. 배속과 무관하게 치트 관리 작업이 실제 시간 기준 주기를 유지한다.
3. 알림/로그/검색 결과/retired 자원에 상한 또는 입증된 회수 정책이 있다. 동일 작업 반복으로 Private Bytes가 계속 선형 증가하지 않는다.
4. 다중 설정 편집 한 작업의 JSON commit은 1회로 합쳐진다. 저장 파일의 최종 상태가 실제 설정과 일치한다.
5. UI·Present·게임 후크에서 대규모 스캔, 동기 저장, 느린 로그, sleep 종료 대기를 하지 않는다.
6. 변경한 기능의 OFF/ON, 세이브 로드, 전투 전환에서 동작 회귀와 오래된 주소 접근이 없다.
7. 기존 성공 기능을 제거하거나 주기를 무작정 늦춰 통과시키지 않는다. 실제 기능 반응 시간과 p95/p99 프레임 지연을 함께 보고한다.
8. Release x64로 빌드하고 지원하는 프록시별로 확인한다. 저장소 `build.ps1 -Type all`의 호출 구성을 참고하되 도구 설치 상태와 각 빌드 종료 코드를 직접 확인한다. 현재 프로젝트는 `PlatformToolset=v145`를 지정하므로 임의로 툴셋을 낮추지 않는다.
9. 변경한 공통 시계·bounded queue·batch persistence·scan 경계처럼 회귀 위험이 큰 로직에 필요한 테스트를 추가한다. 단순 UI 문구 등의 불필요한 테스트는 만들지 않는다.
10. 실제 게임 실행이 불가능하면 빌드 성공을 성능 개선 실증으로 표현하지 않는다.

제출물:

- 수정된 코드와 원인군별 변경 요약.
- 변경 전후 측정표 및 기능별 회귀 검증 결과.
- 지원/미확인 renderer, 게임 빌드 및 외부 DLL 조합.
- 남은 위험, 메모리 보관 상한, 검증하지 못한 게임 객체 소유권.
- 다음 작업자가 바로 재현할 수 있는 간단한 실행 절차.

이 문서의 분석 단계에서는 Markdown 파일만 추가했다. 코드 수정·빌드·게임 실행·실측 결과는 포함되어 있지 않다.
