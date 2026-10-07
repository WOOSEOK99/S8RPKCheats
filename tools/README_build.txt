S8RPK 기재 이름/설명 편집기 - Windows EXE 배포용

구성 파일
- S8RPK_Trait_INI_Editor.py: Python 3.9+ 원본 소스 (Tkinter, 표준 라이브러리)
- S8RPK_Trait_INI_Editor.pyw: Windows에서 더블클릭 실행용 (같은 소스)
- build_exe.bat: PyInstaller 단일 EXE 빌드 스크립트

중요: 치트 DLL과 편집기 EXE, S8RPK_traits_default.json은 같은 폴더에 두고 사용합니다.

사용법
1. 기본 기재 (72개)를 선택하면 trait_texts.ini를 불러옵니다.
   파일이 없더라도 내장된 원본 72개 목록이 표시됩니다.
   값을 바꾸고 [저장]을 누르면 기존 치트가 읽는 HEX 형식의 INI를 새로 생성하거나 갱신합니다.
2. 추가 기재 (JSON)를 선택하면 DLL과 같은 폴더의 S8RPK_traits_default.json을 직접 읽습니다.
   표시 목록은 외부 JSON의 customNames에서 생성됩니다. 기존 고정 182개 데이터를 사용하지 않습니다.
   바꾼 이름/설명은 원본 JSON의 해당 문자열 값에 직접 저장됩니다.
   효과·ID·등급·기타 JSON 설정 및 그 형식은 그대로 유지합니다.
   JSON이 없으면 오류를 표시하며 파일을 임의 생성하지 않습니다.
3. 저장 전에는 변경된 항목을 선택해서 수정하고 [변경내용 반영]을 누르거나,
   [저장]을 바로 누르면 현재 입력 중인 항목까지 반영됩니다.
4. 저장한 원본 파일은 확장자 뒤에 .bak을 붙여 백업됩니다.
   (새 trait_texts.ini를 처음 생성할 때는 기존 파일이 없으므로 백업하지 않습니다.)
5. [파일 열기]는 다른 위치의 기본 INI 또는 외부 JSON을 수동으로 열 때 사용합니다.
   [저장 폴더 선택]에서 DLL 폴더를 지정할 수도 있습니다.
6. 게임과 치트를 종료한 상태에서 편집하고, 변경 후 재실행하여 확인합니다.
7. [메뉴 설명] 버튼에서 동일한 도움말을 볼 수 있습니다.

주의
- 추가 기재는 trait_texts_embedded.ini가 아니라 S8RPK_traits_default.json을 수정합니다.
- 이전에 사용했던 trait_texts_embedded.ini가 DLL 폴더에 남아 있으면 치트가
  그 파일의 기재명/설명 수정값을 재적용하여 JSON 내용을 덮어쓸 수 있습니다.
  사용하지 않는 예전 INI는 직접 백업한 뒤 DLL 폴더 바깥으로 옮기세요.
  프로그램은 예전 INI 파일을 자동으로 삭제하거나 옮기지 않습니다.
- 외부 version.dll이 존재하는 구성에서는 치트 내장 기재 런타임이 적용되지 않을 수 있습니다.
- 기본 INI는 이름 5자/설명 512자(UTF-16 기준) 이내이고 %d 등의 서식 토큰을 검증합니다.
  추가 JSON의 새 이름/설명도 동일한 입력 제한을 적용하지만, JSON의 기존 긴 이름은
  이름을 수정하지 않고 설명만 바꾸는 경우 그대로 보존합니다.
- JSON 저장은 customNames의 이름(name), 설명(desc) 문자열 값만 교체합니다.
  특성의 효과(effects), grade, bgTrait 등의 필드는 수정하지 않습니다.
- 이미 실행 중인 게임이 JSON 파일 수정 내용을 바로 반영한다고 보장하지 않습니다.

Windows EXE 생성 방법
1. Windows에 Python 3.9 이상 설치
2. 명령 프롬프트: py -m pip install pyinstaller
3. build_exe.bat 더블클릭
4. dist\S8RPK_Trait_INI_Editor.exe가 생성되었는지 확인
5. EXE만 사용자에게 배포하고, 치트 DLL과 JSON이 들어 있는 폴더에 놓으세요.

별도 Python 패키지 없이 소스를 실행할 수 있습니다(Tkinter가 있는 Python 설치 필요).
개발 환경에서 JSON 편집, 백업, 파일 형식 보존, INI 저장 및 가상 GUI 테스트를 진행했습니다.
실제 Windows EXE 빌드와 SAN8RPK 게임 실행 적용은 검증하지 않았습니다.
