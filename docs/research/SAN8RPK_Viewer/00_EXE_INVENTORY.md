# Step 00 - EXE 구조 및 추출 인벤토리

## 1. PE 기본 정보

정적 확인 결과:

- 형식: `PE32+`
- 아키텍처: x86-64
- GUI executable
- PE section 수: 7
- 파일 크기: 9,423,704 bytes
- SHA-256: `34abf52f9651d4313076b16042476e5646967aa55af65613f542d69883084c3d`

## 2. PyInstaller 패키징 확인

EXE 끝부분에서 PyInstaller CArchive cookie를 직접 확인했습니다.

- PyInstaller cookie offset: `9,423,616`
- package start: `369,664`
- package length: `9,054,040`
- Python version tag: `310`
- embedded Python DLL: `python310.dll`
- CArchive entry 수: `967`
- PYZ 내부 Python module 수: `115`

따라서 이 프로그램은 네이티브 C++ 단독 EXE가 아니라 **Python 3.10 애플리케이션을 PyInstaller로 묶은 프로그램**입니다.

## 3. 핵심 Python 모듈

PYZ에서 다음 모듈 존재를 직접 확인했습니다.

| 모듈 | 압축 해제 후 크기 | SHA-256 |
|---|---:|---|
| `ai_patch` | 14,412 | `3c051ba5297e827bbc4f23e269bc3198bf24aec67b8792d202704e0c9bc33ae5` |
| `ai_profile` | 9,866 | `7644cedde10d78e3a726afedbc3b75c72c4ef397dcf58bce87b675446de51703` |
| `message_patch` | 526 | `20641845a80a261c77aa9906b65d3f9ce8e14f1ba9a7651cebb2b4b8062a941e` |
| `message_profile` | 1,356 | `01c7c0ccd667cdc13dd617e50717f8383e7aa4a2a348862842a78ae6d295b893` |
| `totalwar_patch` | 454 | `7f437bebbd35e1da8a6c06ebe7a734aac9e7f32cb76062da3ad0161960d64848` |
| `totalwar_profile` | 1,160 | `44297589e2ea88df80d9da40f92d80c20ff846f3b47b8e03f8226c356841dc37` |

압축을 푼 본문은 Python 3.10 marshal code object입니다. 현재 분석 환경의 Python 버전과 code-object 형식이 달라 바로 `marshal.loads()`로 역직렬화되지는 않았지만, 문자열 상수와 정수 상수/RVA는 별도로 추출 가능합니다.

## 4. 프로그램의 패치 방식

`ai_patch` 내부 설명 문자열과 API 이름에서 다음을 확인했습니다.

- 게임 파일 자체를 디스크에서 수정하는 방식이 아님.
- 실행 중 프로세스에 붙는 runtime patch 방식.
- `VirtualAllocEx`
- `WriteProcessMemory`
- `VirtualProtectEx`
- `FlushInstructionCache`
- thread suspend/resume/context 검사 관련 API 사용.
- code cave를 할당하여 프로필별 코드를 설치하는 구조.
- 패치 해제 후에도 실행 중인 native call이 cave로 복귀할 가능성을 고려해 게임 종료 전까지 cave를 해제하지 않는 설계 문구가 존재.

즉 S8RPKCheats의 단순 바이트 패치보다 일부 기능은 훨씬 큰 **원격 code cave + trampoline** 형태입니다.

## 5. 내부 프로필 설명 문자열

직접 추출된 대표 설명:

- `ai_profile`: `Exact-build AI hook profile. No assembler or third-party packages required.`
- `message_profile`: `Exact-build, display-only branches. No code cave or decision changes.`
- `totalwar_profile`: `TotalWarPoint only: one-year retry gate, native remaining conditions preserved.`

특히 메시지 기능은 의사결정 로직을 바꾸지 않는 표시 전용 분기임이 내부 설명에 명시되어 있습니다.

## 6. 다음 작업

다음 Step부터 각 profile의 상수와 payload 템플릿을 기능 단위로 분리합니다.

우선순위:

1. `ai_profile` 공격 후보 로직
2. `ai_profile` 포로/처형 로직
3. `message_profile`
4. `totalwar_profile`
5. `ai_profile` 항복권고/항복 확률 부분
