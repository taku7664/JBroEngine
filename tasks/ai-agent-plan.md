# 내장 AI 에이전트

사용자 승인: 2026-10-16. Codex app-server를 첫 연결로 사용하고, 요청한 작업은 직접 수행하며 사용법·위치 질문에는 현재 상태에 맞는 가이드를 생성한다. 드래그 첨부 UI와 이미지 입력은 후속이다. 결정 D-338.

## 구현 계약

- `Agent::IProvider`와 `CodexProvider`는 엔진 도구 구현에서 분리한다. `IProcessChannel`의 비동기 stdin/stdout을 사용한다. 줄 단위 JSON-RPC는 AI 통신 내부 경계에서만 쓰고 공개 인터페이스에 JSON 라이브러리 타입을 노출하지 않는다.
- 초기화 → 엔진 배포 스킬 루트 등록 → 계정 조회/로그인 → 프로젝트별 대화 시작/복원 순서다. 실행 파일은 탐색하거나 사용자가 지정하며 자동 설치하지 않는다. 인증은 Codex에 맡기고 인증 파일을 읽지 않는다. API 키는 프로젝트/대화/엔진 설정에 저장하지 않는다.
- Codex의 기본 셸·일반 실행·자식 에이전트를 끄고 read-only sandbox와 엔진 전용 동적 도구를 사용한다. 일반 MCP 설정은 빈 설정으로 덮어쓴다. 사용자 전역 설정 파일은 수정하지 않는다. 동적 도구는 실험 API이므로 호환하지 않는 CLI는 실패 상태와 업데이트 안내로 처리한다.
- 엔진 도구는 현재 프로젝트·캔버스 세대(scope), InstanceId, 반영 전 필드 값, 소스 읽기 토큰을 확인한다. 씬 변경은 기존 커맨드를 사용한다. 소스는 기존 코드 문서에서 실행 취소·디스크 충돌 검사를 유지한다. 미저장 사용자 변경은 덮어쓰지 않는다.
- 가이드 YAML은 기존 파서·행동 표를 사용한다. 해석 실패는 기존 가이드를 바꾸지 않는다. 가이드의 질문 버튼으로 대화 패널을 열면 진행과 입력 제한을 일시정지하고, 계속하기/패널 닫기로 재개한다.
- 엔진에 `jbro-guide`, `jbro-create` 스킬을 함께 배포한다. 실행 중 카탈로그와 엔진 API를 읽고 행동을 조합한다. 지원하지 않는 드래그 안내·둘째 동종 컴포넌트 가이드·깊은 필드 등을 만들어내지 않는다.
- 프로젝트별 설정은 기존 사용자 환경설정 옆 별도 YAML에 프로젝트 경로·CLI 경로·대화 ID·선택 모델만 저장한다. 대화 복원 시 이전 작업을 재실행하지 않고 대상은 다시 조회한다.
- 대화 중단·연결 끊김·프로젝트/캔버스 전환 뒤 변경 호출은 재전송하지 않는다. 같은 callId는 한 번만 실행한다. 인증 없는 루프백 제어 포트에는 편집 기능을 추가하지 않는다.

## 검증 기록

### 연결과 자동 검증

- 설치된 `codex-cli 0.153.4`의 app-server 스키마와 실제 RPC를 대조했다. initialize → bundled skill extraRoots → account/read → model/list → thread/start + dynamicTools 교환이 성공했다. 모델과 도구 목록은 하드코딩하지 않는다.
- CLI 전역 기본 모델 `gpt-6.1-sol`은 현재 ChatGPT 인증에서 실제 추론이 거절됐다. 모델 카탈로그의 기본값을 명시적으로 선택하도록 수정한 뒤 `gpt-6-astra`로 실제 모델 턴이 성공했다. 카탈로그 조회만으로 추론 성공을 주장하지 않는다.
- 정식 `Agent` suite는 분할 UTF-8, 중복 호출, 중단 뒤 호출, 다른 프로젝트의 호출, 인증 실패의 키 비노출, 잘못된 JSON과 시작 타임아웃을 검사한다. 실제 에디터에서는 오브젝트 생성/실행 취소/다시 실행, 재생성 전 InstanceId의 무효화, 반영 전 값 검사, 컴포넌트 추가, 소스 저장/실행 취소/미저장 충돌, 경로 탈출 거절, 잘못된 가이드 거절, 공용 위젯의 패널 그리기를 검사한다.
- 번들 스킬 두 개는 실제 Codex `skills/list`에서 활성화되어 발견됐다. skill-creator의 Python 검사기는 `yaml` 모듈이 없어 실행하지 못했다. 이를 검사 통과로 세지 않으며 패키지를 추가 설치하지 않았다.
- 최종 Debug 호스트와 시험 실행 파일 빌드가 성공했다(`F:/AI/Temp/jbro-agent-final-debug-host2.log`, `jbro-agent-final-tests-build2.log`). 회귀 중 새 번역 키 선언 누락을 발견해 공용 `LocalizationKeys.h`의 상수와 두 로케일을 맞춘 뒤 다시 빌드했다.
- `F:/AI/Temp/jbro-agent-final-regressions2.log`: Agent, CodeEditor, EditorLocalization, EditorWidget, EditorGuideFocus, EditorGuide, EditorControlPort, EditorShortcut, ScriptProject, ScriptBuild **10 suites가 통과했다**. 러너의 11개 표시는 기본으로 건너뛴 AgentLive 1개를 포함한다. 실제 모델 검사는 아래의 별도 명시 실행이다.
- ScriptBuild 회귀는 의도한 미선언 식별자의 C2065 진단, 수정 뒤 빌드 성공과 프로젝트 종료 시 빌드 중단을 확인했다. 이것은 AI가 새 게임 기능을 작성·실행한 검증과 구분한다. 소스 충돌과 실행 취소는 Agent/CodeEditor 회귀의 범위다.
- 이전 Release 호스트 빌드 성공은 최종 대화 표시 보완 전 결과이며 최종 Release 검증으로 세지 않는다. 최종 구성 검증은 Debug에 한정한다. 새 공개 헤더의 독립 컴파일은 Debug 시험 빌드에 포함됐으며 모듈 의존 관계는 바꾸지 않았다.

### 실제 모델과 에디터 입력

`AgentLive`는 기본 실행에서 건너뛰며 `JBRO_AGENT_LIVE=1`로 명시적으로 실행해야 현재 Codex 계정으로 추론한다. `JBRO_AGENT_EVIDENCE`에 대화와 생성 YAML을 남긴다. 실행은 테스트용 프로젝트에서 하며 사용자 작업 프로젝트를 수정하지 않는다.

사용자가 지정한 세 문장을 그대로 보냈다.

| 질문 | 생성된 가이드와 확인 |
|---|---|
| 오브젝트 포커스 모드를 어떻게 진입해? | `object.focus`를 생성했다. 강조된 대상의 실제 창 입력을 따라 `GetCanvasViewFocus`가 목표 오브젝트가 됐다. 카메라 맞추기만 한 결과와 구분한다. |
| 스프라이트 Z오더 안만지고 순서 조절 어떻게 해? | 기존 SpriteRenderer2D 순서 메뉴의 `object.bring_forward`를 생성했다. 인스펙터 머리 우클릭 → 앞으로 가져오기를 따라 완료됐으며 `renderOrder`는 원래 값이었다. |
| 스크립트 어떻게 만들어? | `script.create`를 생성했다. 에셋 브라우저 스크립트 뿌리 → 새 스크립트 → C++ 생성 창 → 만들기를 따라 실제 소스 파일이 생겼다. 이 시나리오는 생성 가이드 검사이며 생성 스크립트의 빌드·게임 실행은 확인하지 않는다. |

세 질문 모두 AI가 학습 동작을 직접 편집하는 도구를 호출한 횟수는 0이었다. 가이드 파싱/시작뿐 아니라 실제 Windows/D3D12 창의 입력을 따라 최종 확인까지 통과했다. 첫 완주 91.4초의 대화/YAML은 `F:/AI/Temp/jbro-agent-acceptance`에 있다. Python 캡처를 동반한 후속 완주는 93.7초이며 `F:/AI/Temp/jbro-agent-window-captures/run.log`에 있다.

최종 재촬영 실행도 세 질문을 끝까지 통과했다(87.9초, `F:/AI/Temp/jbro-agent-final-captures/run.log`).

### 화면 확인과 캡처

- 다른 세션 **스크립트 언어 (2)**에서 사용한 `F:/AI/Temp/script-summary-ui/capture_engine.py`를 확인했다. Pillow와 Win32 PrintWindow를 사용하며, 이번 테스트 프로세스의 `JBroEngineWindow`만 찾아 원본 PNG를 저장했다. 전체 화면이나 다른 앱을 캡처하지 않았다.
- 최초 캡처는 대화 스크롤이 첫 답변에 머물렀고 일시정지/모달 전환 중의 가이드가 흐리게 보였다. 이를 최신 답변·완료 흐름이 읽히는 화면으로 보지 않는다. 대화의 끝 따라가기를 보완한 뒤 페이드가 끝난 시점과 실제 목표 메뉴에서 다시 캡처했다.
- 캡처 도구의 동기화와 대기 표식은 검증 중에만 사용하고 정식 소스에 남기지 않는다. 캡처 때문에 임시로 추가했던 GPU 읽기 공개 API도 제거했다. 정식 AgentLive 입력 검사는 유지한다.
- Computer Use의 창 캡처는 두 번 타임아웃이 났으며 재시도하지 않았다. Python 도구 사용과 모델/가이드 검증 성공은 이 실패와 별개다.

- 최종 원본 PNG를 직접 열어 확인했다. 최신 순서/스크립트 답변이 대화 끝에 보이며, 스크립트는 모달 뒤의 흐린 가이드 대신 실제 새 스크립트 메뉴와 선명한 설명을 함께 찍었다. 사진은 [포커스](F:/AI/Temp/jbro-agent-final-captures/scenario-1-guide.png), [순서 메뉴](F:/AI/Temp/jbro-agent-final-captures/scenario-2-guide.png), [스크립트 메뉴](F:/AI/Temp/jbro-agent-final-captures/scenario-3-guide.png), [최신 스크립트 답변](F:/AI/Temp/jbro-agent-final-captures/scenario-3-chat.png)이다. 캡처 시점 이후 번역 키 상수 연결만 바로잡았으며 그 변경 뒤 Debug 빌드/회귀를 다시 통과했다.
- 사용자 지시로 검증에서 만든 Codex 세션 7개를 공식 `codex delete --force <UUID>`로 영구 삭제했다. 앱 목록을 다시 조회해 검증 프로젝트의 세션이 0개임을 확인했다. 이 후 실제 모델 검사는 추가 실행하지 않았다. 외부 증거 파일은 별개로 남아 있다.
- 소스·문서 수정과 로컬 기능 커밋의 범위이며 원격 푸시·메인 병합·사용 중 배포본 교체·최종 Release 검증은 수행하지 않았다. 사용자가 정한 세 질문의 가이드 부분은 위 실제 모델·입력·화면 확인 범위에서 마쳤다.

## 후속

- 드래그 수신 UI, 이미지 첨부, 다른 AI 제공자. 현재 Target 자료형은 오브젝트/에셋/컴포넌트 구분을 보존한다.
- 지원되지 않는 가이드 행동은 본 작업에서 임의로 확장하지 않는다.
