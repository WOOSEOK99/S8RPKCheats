# PERFORMANCE_VALIDATION

## 상태

- 작업 단계: T00 재현 조건 / 저비용 계측
- 기준 main: `74febb5db3712becab7aa5f5f211886de17a38de`
- 작업 브랜치: `perf/T00-baseline-instrumentation`
- 실게임 실행: 미실행
- Release x64 빌드: 이 작업 환경에서는 미실행
- CPU/GPU/RAM 실측: 미실행

빌드 성공이나 정적 검토만으로 성능 개선을 주장하지 않는다. 이 파일에는 실제 측정값이 확인된 경우에만 숫자를 기록한다.

## T00 계측 활성화

성능 계측은 기본 OFF이며 기존 기능 동작을 바꾸지 않는다.

1. 게임 실행 전에 환경 변수 `S8RPK_PERF_DIAGNOSTICS=1`을 설정한다.
2. 기존 파일 로그 또는 디버그 로그를 활성화해야 `[Perf:T00]` 요약을 확인할 수 있다.
3. 계측은 `QueryUnbiasedInterruptTime`을 우선 사용하므로 현재 SpeedHack의 QPC/GetTickCount/GetTickCount64/timeGetTime 후킹과 분리된다.
4. 출력은 약 10초 단위 요약이며 프레임마다 파일에 기록하지 않는다.

현재 연결된 계측 항목:

- `SpeedHack_Update` 호출 수 / 총 시간 / 평균 / 최대
- `SaveSkillCounts` 호출 수 / 총 시간 / 평균 / 최대
- 현재/최대 알림 queue 및 history 길이를 기록할 수 있는 공통 상태
- 프록시 DLL 이름 후보(`dinput8.dll`, `dxgi.dll`, `hid.dll`)와 외부 `version.dll` 모듈 존재 개수 기록 기반

후속 T00 연결 예정 항목:

- `Menu::Loops`
- Overlay/Present
- `IsValidPtr`
- `FindPattern`
- 알림 생성/렌더링
- 전체 roster/scan 작업의 읽은 bytes와 재구축 횟수

## 동일 조건 기준표

아래 표는 반드시 같은 세이브, 장면, 카메라, 해상도, 그래픽 설정, 프레임 제한에서 측정한다.

| 조건 | 실제 측정 시간 | CPU | Private Bytes | Working Set | GPU 사용률 | GPU frame time | 전용 GPU 메모리 | 공유 GPU 메모리 | thread | handle | 비고 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 치트 미로드 | 미측정 | | | | | | | | | | |
| 치트 로드 / 일반 토글 OFF | 미측정 | | | | | | | | | | |
| UI 완전 숨김 | 미측정 | | | | | | | | | | |
| UI 접힘 | 미측정 | | | | | | | | | | |
| UI 펼침 | 미측정 | | | | | | | | | | |
| 배속 1x | 미측정 | | | | | | | | | | |
| 배속 2x | 미측정 | | | | | | | | | | |
| 배속 5x | 미측정 | | | | | | | | | | |

## T00 확인 절차

### A. 배속과 계측 시계 분리

- 배속 OFF 상태에서 10초 동안 `[Perf:T00]` 보고 간격을 확인한다.
- 배속 2x 및 5x에서도 실제 벽시계 약 10초마다 보고되는지 확인한다.
- 배속 변화와 무관하게 `SpeedHack_Update` 호출 수가 어떤 비율로 변하는지 기록한다.
- 이 단계에서는 호출 수가 일정하다고 가정하지 않는다. 실제 결과를 기록한다.

### B. 다중 전법 횟수 편집 기준

- 1명, 수십 명, 가능한 최대 선택으로 각각 한 번씩 적용한다.
- `[Perf:T00] SkillCountSave`의 호출 수와 총 시간을 기록한다.
- T03 전에는 선택 인원 수만큼 저장이 반복되는지 실측으로 확인한다.

### C. 메모리/렌더러 기준

Windows 작업 관리자만으로 원인을 단정하지 않는다. 가능하면 Visual Studio Profiler 또는 WPR/WPA를 사용한다.

필수 분리 지표:

- CPU: 전체 코어 기준인지 명시
- RAM: Private Bytes / Working Set 분리
- GPU: 사용률 / frame time / dedicated / shared memory 분리
- thread / handle 수

### D. 로드 모듈 확인

`[Perf:T00] diagnostics=ON ... modules(...)` 출력에서 다음을 기록한다.

- 실제 로드된 프록시 DLL 종류
- 동일 이름 모듈의 비정상 중복 여부
- `version.dll` 존재 여부

세 프록시가 프로젝트에서 빌드된다는 사실만으로 중복 로드라고 판정하지 않는다.

## 미실행 / 확인 불가 항목

현재 작업 환경에서는 다음을 확인할 수 없다.

- 실제 게임 renderer가 DX11인지 DX12인지
- 실제 게임에서 CPU/GPU/RAM 증가폭
- ETW/WPR/WPA 스택
- 게임 객체 수명과 page fault / working set 변화
- 책략창 반복, 전투 전환, 저장/로드에 따른 장시간 RAM 기울기

위 항목은 사용자 실게임 검증 전까지 확정 원인이나 개선 수치로 표현하지 않는다.
