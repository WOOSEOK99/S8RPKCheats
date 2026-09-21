# SAN8RPK_Viewer.exe 정적 분석 노트

이 폴더는 `SAN8RPK_Viewer.exe`를 한 번에 분석하다가 작업이 끊기는 것을 방지하기 위한 **단계별 조사 기록**입니다.

## 기준

- 분석 대상: `SAN8RPK_Viewer.exe`
- 파일 크기: 9,423,704 bytes
- SHA-256: `34abf52f9651d4313076b16042476e5646967aa55af65613f542d69883084c3d`
- 분석 방식: **실행하지 않는 정적 분석**
- 비교 대상 S8RPKCheats 기준: `main` `96dfc11508bb9bfdc429f656f15530b13cb56045`
- 문서 작성 브랜치: `research/san8rpk-viewer-analysis`

## 기록 규칙

이 조사에서는 사실을 세 단계로 구분합니다.

- **확정**: EXE 내부 데이터/바이트 또는 현재 GitHub 소스에서 직접 확인.
- **유력**: 함수/상수/인접 문자열과 제어 흐름상 의미가 매우 강하지만 게임 런타임 검증 전.
- **미확정**: 주소나 코드 존재만 확인했고 의미는 아직 분석하지 않음.

게시글 설명은 분석 방향을 잡는 참고 자료로 사용하되, EXE 내부에서 직접 확인되지 않은 의미를 확정 사실로 승격하지 않습니다.

## Step 계획

| Step | 문서 | 상태 |
|---|---|---|
| 00 | [EXE 구조 및 추출 인벤토리](00_EXE_INVENTORY.md) | 1차 완료 |
| 01 | [S8RPKCheats AI 전투 개선과 Viewer 비교](01_AI_WAR_COMPARE.md) | 1차 완료 |
| 02 | [기능별 RVA/모듈 인덱스](02_FEATURE_INDEX.md) | 1차 완료 |
| 03 | [AI 공격 후보 확장 / 목표세력 제한 해제 상세 해부](03_AI_ATTACK_TARGET_EXPANSION.md) | payload 수준 완료 · helper 내부는 후속 확인 |
| 04 | [플레이어 우선공격 제거 상세 해부](04_AI_PLAYER_PRIORITY_REMOVAL.md) | static patch 수준 완료 · 호전/극호전 분기 매핑은 후속 확인 |
| 05 | [AI 포로 처형 조건 상세 해부](05_AI_PRISONER_EXECUTION.md) | 최종 payload/임계값 공식 완료 · native helper 의미는 후속 확인 |
| 06 | [AI 처형 로그 상세화](06_AI_EXECUTION_MESSAGE_DETAIL.md) | 4개 display-only patch 완료 · 등용/석방 두 분기의 정확한 매핑은 후속 확인 |
| 07 | [결전 발생빈도 조정](07_TOTALWAR_ONE_YEAR_RETRY.md) | 두 hook/1년 override 구조 완료 · +0x35/+0x38 공식 필드명은 후속 확인 |
| 08 | [항복권고 실패 후 공격 우선도 / 고의리 군주 항복 억제](08_SURRENDER_REFUSAL_AND_HONOR.md) | payload 수준 완료 · native helper 의미/overlap 버그 후보는 후속 검증 |
| 09 | [S8RPKCheats 이식 후보 정리 및 충돌 지도](09_PORTING_AND_CONFLICT_MAP.md) | 완료 · 실제 이식은 미실시 |
| 10 | 게임 런타임 검증 체크리스트 | 예정 |

## 중요 원칙

- 이 브랜치에서는 우선 **조사 문서만 추가**합니다.
- 게임 코드에 이식하기 전 반드시 각 기능을 별도로 분리합니다.
- 기존 S8RPKCheats 패치와 같은 RVA를 건드리는 경우 중복 적용하지 않습니다.
- 의미가 확정되지 않은 구조/오프셋은 write 대상으로 사용하지 않습니다.
- 실제 게임에서 검증된 결과만 최종 구현 문서에 확정값으로 기록합니다.
