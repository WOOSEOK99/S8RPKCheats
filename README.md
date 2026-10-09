# 삼국지8 리메이크 PK 인게임 치트 (S8RPKCheats)

**삼국지8 리메이크 파워업키트(PK) 게임 버전 1.1.3용 인게임 치트** 프로젝트입니다.

게임 실행 중 치트 메뉴에서 무장, 도시 내정, 전투, 인간관계 및 여러 편의 기능을 설정할 수 있도록 만든 Windows용 C++ DLL 프로젝트입니다. DirectX 11 화면에 ImGui 메뉴를 표시하며, 일부 기능은 게임 메모리와 실행 코드를 다룹니다.

> **지원 기준: 삼국지8 리메이크 PK 1.1.3 / Windows x64**
>
> 게임 업데이트로 내부 주소나 코드가 달라지면 기능이 작동하지 않거나 게임이 종료될 수 있습니다. **다른 게임 버전과의 호환은 보장하지 않습니다.** 사용 전 저장 데이터를 백업하고, 개인 싱글플레이 환경에서 테스트하세요.

## 1. 어떤 프로젝트인가요?

주요 기능은 다음 범주로 구성되어 있습니다. 세부 항목은 개발 상태나 게임 상황에 따라 달라질 수 있습니다.

- **무장 관련:** 무장 정보 조회·편집, 능력 및 특기·기재 관련 기능
- **도시/내정:** 도시 정보, 내정 수치, 기술·시설 관련 기능
- **전투/전쟁:** 전투 환경, 부대, 전법 및 AI 동작 관련 기능
- **교류/인간관계:** 대화, 친밀도 및 기타 교류 관련 기능
- **시스템/편의:** 게임 속도, 시작 설정, 알림 및 설정 저장

DLL은 `hid.dll`, `dinput8.dll`, `dxgi.dll` 형태로 빌드할 수 있도록 소스가 구성되어 있습니다. **현재 프로젝트의 기본 빌드 설정은 `hid.dll`입니다.** 각 파일은 Windows DLL을 통해 게임 프로세스에 로드되는 프록시 방식이며, 게임 환경에 따라 사용할 수 있는 종류가 다를 수 있습니다.

### 사용 시 주의사항

- 공식 개발사에서 제공한 기능이 아닌 **비공식 팬 제작 도구**입니다.
- 게임이 실행 중일 때 DLL을 교체하거나 설정 파일을 무리하게 수정하지 마세요.
- DLL 종류를 무작정 여러 개 동시에 넣지 마세요. 다른 모드의 동일한 이름 DLL과 충돌할 수 있습니다.
- 저장 파일은 반드시 별도로 백업하세요.
- 게임 업데이트 후에는 작동을 가정하지 말고 지원 버전을 다시 확인하세요.

## 2. 컴파일에 필요한 프로그램

**컴파일**은 GitHub에 공개된 C++ 소스코드를 `.dll` 파일로 만드는 작업입니다. 처음이라면 아래 도구를 설치하세요.

1. **Windows PC** (x64 빌드용)
2. **Visual Studio** 및 `C++를 사용한 데스크톱 개발` 워크로드
3. **MSVC v145 플랫폼 도구 집합** 및 **Windows 10/11 SDK**
4. 소스코드 다운로드용 웹브라우저 또는 Git / GitHub Desktop

프로젝트 파일(`Internal DX11 Base/Internal DX11 Base.vcxproj`)은 현재 **MSVC `v145`**, **Windows SDK 10.0**, **Release x64에서 C++20**으로 설정되어 있습니다. Visual Studio에 해당 C++ 도구 집합이 없다면 Visual Studio Installer의 **수정(Modify)** → **개별 구성 요소**에서 설치하세요. 이전 버전의 Visual Studio만 설치되어 있다면 빌드 도구 버전 오류가 발생할 수 있습니다.

> ImGui 및 MinHook 관련 소스는 저장소에 포함되어 있습니다. 이 프로젝트의 기본 DLL 빌드를 위해 Python을 설치할 필요는 없습니다. Python은 `tools/`의 별도 기재 편집기 개발에 사용됩니다.

## 3. 소스코드 받기 — 초보자용

### 방법 A: ZIP 다운로드 (컴파일만 해볼 때)

1. 이 페이지 상단의 **Code** 버튼을 누릅니다.
2. **Download ZIP**을 눌러 내려받습니다.
3. ZIP 파일을 원하는 폴더에 압축 해제합니다.
4. 폴더 안에 `SAM8RPK_Ingame_Cheat.sln` 파일이 있는지 확인합니다.

ZIP으로 받아도 빌드는 할 수 있지만, 다른 개발자와 변경 사항을 주고받으려면 아래의 Git 방식이 더 편리합니다.

### 방법 B: Git으로 복제 (공동 개발 권장)

[Git](https://git-scm.com/downloads) 또는 [GitHub Desktop](https://desktop.github.com/)을 설치합니다. 터미널(명령 프롬프트, PowerShell 등)을 쓸 수 있다면 다음을 실행하세요.

```powershell
git clone https://github.com/WOOSEOK99/S8RPKCheats.git
cd S8RPKCheats
git status
```

`git clone`은 저장소를 PC로 복사하고, `cd`는 해당 폴더로 이동하는 명령입니다. `git status`는 현재 변경된 파일이 있는지 보여줍니다.

## 4. Visual Studio로 DLL 컴파일하기 (가장 쉬운 방법)

1. Visual Studio를 실행합니다.
2. **프로젝트 또는 솔루션 열기**에서 `SAM8RPK_Ingame_Cheat.sln`을 엽니다.
3. 화면 상단의 **솔루션 구성**을 `Release`로 선택합니다.
4. 바로 옆의 **솔루션 플랫폼**을 반드시 `x64`로 선택합니다. `x86`은 선택하지 마세요.
5. 상단 메뉴 **빌드 → 솔루션 빌드**(`Ctrl + Shift + B`)를 실행합니다.
6. 하단 **출력(Output)** 창에서 빌드 오류 여부를 확인합니다.

현재 기본 설정대로 빌드하면 `hid.dll` 대상입니다. 제공된 `build_hid.bat` 기준 출력 경로는 다음과 같습니다.

```text
S8RPKCheats/
└─ x64/
   └─ Release/
      └─ hid.dll
```

실제 출력 위치는 빌드 설정에 따라 다를 수 있으므로, 결과 파일이 없다면 Visual Studio의 출력 창에서 생성 경로를 확인하세요.

### 다른 이름의 DLL을 만들고 싶다면

프로젝트에는 세 가지 진입점이 있습니다.

| 빌드 종류 | 생성 DLL | 관련 소스 |
| --- | --- | --- |
| 기본 | `hid.dll` | `dllmain_hid.cpp` |
| 대안 | `dinput8.dll` | `dllmain.cpp` |
| 대안 | `dxgi.dll` | `dllmain_dxgi.cpp` |

**파일 이름만 바꿔서 사용하지 마세요.** DLL 종류에 따라 빌드에 포함되는 진입점과 내보내기 설정이 다릅니다. 다른 종류가 필요하면 아래 스크립트 또는 명령줄 빌드의 `ProxyType`과 `TargetName`을 사용하세요.

## 5. 명령줄과 빌드 스크립트

저장소에는 `build.ps1`, `build.bat`, `build_hid.bat` 파일이 있습니다.

### PowerShell 스크립트

저장소의 최상위 폴더에서 PowerShell을 열고 필요한 종류를 선택해 실행합니다.

```powershell
.\build.ps1 -Type hid
# 또는
.\build.ps1 -Type dinput8
.\build.ps1 -Type dxgi
.\build.ps1 -Type all
```

`all`은 세 종류를 순서대로 빌드합니다. `build_hid.bat`는 `hid.dll` 빌드용 배치 파일입니다. `build.bat`는 현재 솔루션 설정으로 Release 빌드를 호출합니다.

**주의:** 현재 제공된 스크립트는 `/p:Platform=x64`를 명시하지 않습니다. 따라서 실행 환경에 따라 선택되는 솔루션 플랫폼을 확인해야 합니다. 처음 컴파일한다면 위의 **Visual Studio에서 Release / x64를 직접 선택하는 방법**을 권장합니다.

### 명령줄에서 x64를 확실하게 지정하기

Visual Studio의 **Developer PowerShell**에서 저장소 폴더로 이동한 뒤 실행합니다.

```powershell
msbuild "SAM8RPK_Ingame_Cheat.sln" /m /p:Configuration=Release /p:Platform=x64 /p:ProxyType=hid /p:TargetName=hid
```

다른 종류를 빌드할 때는 `ProxyType`과 `TargetName`을 **둘 다** `dinput8` 또는 `dxgi`로 변경합니다. `msbuild` 명령을 찾을 수 없다면 일반 PowerShell 대신 Visual Studio용 Developer PowerShell을 사용하거나 Visual Studio Installer에서 MSBuild 구성 요소를 확인하세요.

## 6. 빌드한 DLL과 설정 파일

이 프로젝트는 프록시 DLL 방식을 사용합니다. 사용할 DLL 종류가 게임 환경에서 로드되는지 확인한 뒤, **게임 실행 파일이 있는 폴더**에 해당 DLL을 배치하는 방식입니다. 다른 모드가 이미 같은 이름을 사용 중이라면 덮어쓰기 전에 백업하고 충돌 여부를 확인하세요. 게임 설치·로딩 구성에 따라 동작 여부는 달라질 수 있습니다.

실행 중에 생성되는 `S8RPK_cheat_config.json`은 **DLL이 있는 폴더**를 기준으로 저장/읽기 됩니다. 별도 기능에 따라 다음의 데이터 파일도 사용됩니다.

| 파일 | 용도 |
| --- | --- |
| `S8RPK_traits_default.json` | 추가 기재 관련 외부 데이터 |
| `S8RPK_cheat_char.json` | 무장/문자 관련 데이터 |
| `effect_definitions.json` | 효과 정의 데이터 |
| `speciality_definitions.json` | 특기 정의 데이터 |
| `trait_texts.ini` | 기본 기재 텍스트 편집 시 사용하는 INI |
| `S8RPK_cheat_config.json` | 실행 시 사용하는 치트 설정 (DLL 위치 기준) |

위 파일 중 JSON 원본은 저장소 최상위 폴더에도 있으며, 일부 기본 리소스는 소스에 포함되어 있습니다. 각 기능의 외부 파일 사용 여부는 해당 구현을 확인하세요. 특히 **추가 기재 이름/설명을 수정하려면** `S8RPK_traits_default.json`을 DLL과 같은 폴더에 두고 수정합니다.

기재 이름/설명 편집기를 별도로 사용하려면 [`tools/README_build.txt`](tools/README_build.txt)를 확인하세요.

## 7. 소스코드 구조 이해하기

처음에는 아래 파일과 폴더만 알아도 수정할 위치를 찾는 데 도움이 됩니다.

```text
S8RPKCheats/
├─ README.md                       # 지금 읽고 있는 안내문
├─ AGENTS.md                       # 개발/수정 시 지켜야 할 규칙
├─ SAM8RPK_Ingame_Cheat.sln        # Visual Studio 솔루션
├─ build.ps1 / build_hid.bat       # 빌드 스크립트
├─ S8RPK_traits_default.json       # 추가 기재 데이터
├─ Internal DX11 Base/
│  ├─ Internal DX11 Base.vcxproj   # C++ 프로젝트/빌드 설정
│  ├─ dllmain*.cpp                 # DLL 종류별 진입점
│  ├─ Source.cpp / Source_impl.inc # 시작 및 초기화 관련 코드
│  ├─ Menu.cpp / Menu_impl.inc     # 치트 메뉴 기본 처리
│  ├─ MenuSections*.cpp            # 메뉴 항목 구성
│  ├─ Config.cpp / ConfigBase.inc  # 설정 저장 및 불러오기
│  ├─ Cheats/
│  │  ├─ Civilian/                # 도시/내정
│  │  ├─ Officer/                 # 무장
│  │  ├─ Social/                  # 교류/관계
│  │  ├─ System/                  # 시스템
│  │  └─ War/                     # 전쟁/전투
│  ├─ Framework/                  # ImGui 및 렌더링 관련
│  └─ Hooking/                    # 후킹 관련 코드
└─ tools/                          # 별도 편집 도구
```

예를 들어 **도시 정보 관련 버그**를 고치려면 `Internal DX11 Base/Cheats/Civilian/CityInfoWindow.cpp`를 먼저 살펴보세요. 메뉴 문구를 수정하려면 관련 `MenuSections*.cpp` 파일을 찾는 편이 좋습니다. 함수 사용처를 검색할 때는 Visual Studio의 **전체 검색(`Ctrl + Shift + F`)**을 활용하세요.

## 8. 함께 개발에 참여하는 방법 (Pull Request)

GitHub의 **Issue**는 버그나 개선 아이디어를 논의하는 게시글이고, **Pull Request(PR)**는 직접 수정한 코드를 프로젝트에 반영해 달라고 요청하는 기능입니다.

### 처음 참여하는 사람의 순서

1. [Issues](https://github.com/WOOSEOK99/S8RPKCheats/issues)에서 같은 문제나 제안이 이미 있는지 확인합니다.
2. 저장소 오른쪽 위의 **Fork**로 자신의 GitHub 계정에 복사본을 만듭니다.
3. 자신의 Fork를 GitHub Desktop의 **Clone repository**로 받거나 아래 Git 명령으로 복제합니다.
4. **새 작업 브랜치**를 만들어서 변경 내용을 다른 작업과 분리합니다.
5. 문제와 관련된 파일 **최소한만 수정**합니다. 수정 전후 내용을 비교하고, 가능하다면 자신이 수정한 기능을 직접 확인합니다.
6. 변경 파일만 커밋하고 자신의 Fork에 올립니다.
7. GitHub의 **Contribute → Open pull request**에서 원본 저장소의 `main` 브랜치를 대상으로 PR을 작성합니다.

### Git 명령 예시

아래 `내아이디`는 본인의 GitHub 아이디로 바꿔 입력하세요.

```powershell
git clone https://github.com/내아이디/S8RPKCheats.git
cd S8RPKCheats
git switch -c fix/example

# Visual Studio에서 필요한 파일 수정 후
git status
git diff
git diff --check

# 실제 수정한 파일만 선택해서 추가
git add "Internal DX11 Base/Cheats/Civilian/CityInfoWindow.cpp"
git commit -m "fix: 도시 정보 기능 수정"
git push -u origin fix/example
```

`git diff`는 수정 전후 차이를, `git diff --check`는 공백 관련 문제를 확인합니다. 예시 파일은 실제 작업 대상에 맞게 바꿔 주세요. Git 명령이 어렵다면 GitHub Desktop에서 **Changes → Commit → Push origin**을 사용해도 됩니다.

### PR에 꼭 적어 주세요

- **수정한 이유:** 어떤 문제를 해결하거나 어떤 기능을 추가했나요?
- **게임 환경:** 삼국지8 리메이크 PK 1.1.3에서 확인했나요?
- **수정한 파일:** 관련 파일과 핵심 변경 내용
- **검증 결과:** 컴파일 여부, 실제 게임에서 테스트한 상황 및 결과
- **주의사항:** 아직 확인하지 못한 부분이나 다른 기능에 미칠 수 있는 영향

**컴파일 성공과 실제 게임 동작 성공은 서로 다릅니다.** 실행을 확인하지 않았다면 테스트하지 않았다고 명시해 주세요.

## 9. 공동 개발 시 지켜야 할 규칙

수정 전에 루트의 [`AGENTS.md`](AGENTS.md)를 반드시 읽어 주세요. 핵심 원칙은 다음과 같습니다.

- 필요한 부분만 조사하고 **작은 범위의 변경**으로 문제를 해결합니다.
- 관계없는 코드 정리, 파일명 변경, 대규모 리팩터링을 한 PR에 섞지 않습니다.
- 기존 훅, 스레드, 콜백, 정리(종료) 순서를 이해하기 전에 새 종료 경로를 추가하지 않습니다.
- 메모리 주소나 구조체 오프셋을 추정만으로 바꾸지 않습니다. **게임 1.1.3에서 확인한 근거**를 남깁니다.
- 헤더 파일을 바꿀 때는 다른 소스에 미치는 영향까지 확인합니다.
- 빌드 옵션/툴셋을 마음대로 변경하거나, 요청 없이 정리·전체 재빌드 스크립트를 실행하지 않습니다.
- 제출 전에 `git diff`로 실제 변경 파일을 확인하고 불필요한 수정이 없는지 검토합니다.

특히 이 프로젝트는 게임 메모리와 함수 훅을 사용하므로 일반적인 UI 프로그램보다 **충돌, 잘못된 주소 접근, 저장 데이터 손상** 가능성에 주의해야 합니다. 기능을 하나씩 수정하고 테스트 가능한 범위를 분명하게 남겨 주세요.

## 10. 문제를 발견했을 때

[새 Issue 작성하기](https://github.com/WOOSEOK99/S8RPKCheats/issues/new)에서 다음 정보를 알려 주시면 재현과 수정에 도움이 됩니다.

- **게임 버전**(예: PK 1.1.3)과 치트 버전/사용 DLL 이름(`hid.dll` 등)
- 문제가 발생하는 기능, 눌렀던 메뉴, 재현 순서
- 기대했던 동작과 실제 발생한 현상
- 오류 메시지 또는 관련 로그 일부(있다면)
- 새 게임/저장 게임 중 어느 상황인지, 다른 모드와 충돌하는지

로그나 스크린샷을 공유하기 전에 사용자 이름, 개인 경로 등 **개인정보**가 들어 있지 않은지 확인하세요. 게임 저장 파일·덤프 파일의 공개 업로드도 신중히 결정하세요.

### 자주 발생하는 컴파일 문제

| 증상 | 먼저 확인할 사항 |
| --- | --- |
| `MSB8020` 등 도구 집합을 찾지 못함 | Visual Studio Installer에 **MSVC v145**가 설치되어 있는지 |
| Windows SDK 관련 오류 | **Windows 10/11 SDK** 설치 여부 |
| `msbuild` 명령을 찾지 못함 | **Developer PowerShell**을 사용했는지 |
| DLL이 보이지 않음 | `Release / x64` 선택, 출력 창의 실제 경로, 빌드 성공 여부 |
| DLL을 넣었지만 메뉴가 표시되지 않음 | 게임 버전, DLL 종류/로드 여부, 다른 모드와의 충돌 여부 |
| 빌드는 되지만 게임이 종료됨 | 게임 1.1.3 호환성, 최근 변경 코드, 잘못된 메모리 접근 여부 |

컴파일에 실패했다면 오류 메시지의 **첫 번째 실제 오류**와 변경된 파일부터 살펴보세요. 근거 없이 빌드 설정이나 게임 주소를 바꾸는 방식은 피하는 것이 좋습니다.

## 11. 참고 및 출처

- [GitHub 저장소](https://github.com/WOOSEOK99/S8RPKCheats)
- [버그 신고 및 개선 제안](https://github.com/WOOSEOK99/S8RPKCheats/issues)
- [Pull Request 목록](https://github.com/WOOSEOK99/S8RPKCheats/pulls)
- [원본 DX11 ImGui Internal Hook 기반 프로젝트 (NightFyre)](https://github.com/NightFyre/DX11-ImGui-Internal-Hook/tree/b44d74cbb20918c1b616190325ee49589b80cf36)

이 저장소에는 원본 DX11 ImGui 기반 코드와 함께 ImGui, MinHook 등 외부 프로젝트에서 유래한 구성 요소가 포함되어 있습니다. 해당 코드의 저작권 및 라이선스 조건을 존중해 주세요.

> **공개(Public) 저장소와 자유로운 재배포 허가는 같은 의미가 아닙니다.** 현재 저장소 루트에 별도의 `LICENSE` 파일이 확인되지 않으므로, 코드 재사용·재배포 조건은 권리자와 확인해야 합니다.
