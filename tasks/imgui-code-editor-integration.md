# 엔진 내장 코드 편집기 통합 (2026-10-15, D-335)

## 승인 범위와 현재 상태

사용자가 승인한 범위는 엔진의 네이티브 코드 편집기 전용 도크, 파일별 탭, JBroScript·C++ 파일 편집과 기존 JBroScript LSP의 진단·문법/타입 강조 연결이다.
스크립트 파일과 빌드 진단은 내부 도크로 여는 것이 기본이고, 외부 편집기 열기는 명시적 옵션으로 남긴다.
이는 D-299의 기본 파일 열기 경로를 바꾸는 후속 계약이다. D-284의 패널·행동 등록과 소속 도크 규칙은 유지한다.
확정 계약은 [ProjectRule §21](../docs/ProjectRule.md#21-엔진-내장-코드-편집기-2026-10-15-d-335)에 둔다.

작업 브랜치는 `codex/imgui-code-editor-spike`이며 이번 통합 전에 main을 병합했다.
**요청 범위의 구현·검증을 완료했다. 최종 framing·채널 방어·재생 중 행동을 포함한 Debug/Release 전체 빌드·CodeEditor suite, 관련 회귀 10개·긴 회귀 5개와 아래 실제 엔진 UI를 확인했다. 최종 vendor 패치 재적용도 일치했다.**
[독립 위젯 시험](./imgui-code-editor-spike.md)의 삭제 회귀·IME·탭·진단 위치 통과를 엔진 통합 통과로 세지 않는다.

## 소스에서 확인한 구현

다음 표는 현재 소스의 연결 상태이며 정식 시험·실제 화면의 통과 기록이 아니다.

| 구성 | 현재 구현과 경계 |
|---|---|
| 파일별 패널·도크 | `CodeEditorPanel`을 비고유 `InstancePanel`로 등록하고 `CodeEditor` 전용 도크에 둔다. 파일 경로가 같으면 기존 탭을 앞으로 가져와 요청 줄·열에 초점을 둔다. 파일마다 버퍼·Undo/Redo·커서·선택·스크롤 상태를 소유한다. |
| 내부 기본·외부 옵션 | UI가 켜진 `OpenInScriptEditor`는 `OpenCodeFile`로 연결된다. 코드 패널의 `파일 → 외부 편집기에서 열기`는 기존 외부 편집기 설정을 사용한다. UI 없는 기존 호출 경로는 외부 열기를 유지한다. 빌드 실행·결과 수집 계약은 바꾸지 않는다. |
| 파일 입출력 | UTF-8 텍스트를 읽고 8 MiB 초과·NUL·잘못된 UTF-8은 거절한다. BOM과 CRLF/LF 구분을 기억하고 최종 개행을 임의로 추가하지 않도록 위젯을 수정했다. 저장 전에 디스크 바이트를 다시 비교해 외부 변경이 있으면 덮어쓰지 않고 재불러오기를 안내한다. 같은 폴더의 임시 파일을 쓴 뒤 교체한다. 혼합 줄바꿈을 바이트 그대로 보존한다는 주장은 아니다. |
| 변경 보존·편집 행동 | 저장 전 변경은 탭의 `*`로 표시한다. 닫기·재불러오기는 변경을 버릴지 묻는 경로를 갖는다. 저장·Undo·Redo는 패널 범위의 행동 표에 등록하고 Ctrl+S·Ctrl+Z·Ctrl+Y/Ctrl+Shift+Z를 연결하며 재생 중에도 허용한다. 스크립트 루트 밖 파일은 현재 경로 비교에 따라 읽기 전용이다. |
| 위젯·구문 색상 | 패널은 `Widget::CodeEditor`를 거쳐 후보 위젯을 그린다. JBroScript/C++ 어휘 색상을 제공하고 JBroScript의 서버 의미 토큰으로 타입·멤버 등의 색을 덧입힌다. 진단 범위는 밑줄·메시지로 연결한다. C++ 언어 서버·C++ 의미 진단은 이번 구현에 없다. |
| JBroScript 클라이언트 | 프로젝트가 사용하는 기존 `JBroc.exe --lsp`를 자식 프로세스로 시작한다. `initialize`의 프로젝트·locale·UTF-16 설정, `didOpen/didChange/didSave/didClose`, 진단과 `semanticTokens/full`을 연결했다. 편집한 버퍼를 보내며 분석을 위해 파일을 저장하지 않는다. |
| 오래된 결과·끊김 | 편집 시 이전 진단·의미 색을 비우고 문서 버전과 요청 버전에 맞는 결과만 받는다. 변경 전송은 150 ms 간격으로 모은다. 실패 시 표시를 비우고 상태·재연결을 제공하며 미저장 텍스트는 클라이언트에 남긴다. 프로젝트 닫기는 패널과 서버를 해제하는 경로가 있다. |
| 플랫폼 연결 | `IProcessChannel`로 stdin/stdout/stderr를 분리하고 Windows 구현의 작업 스레드·제한 큐·Job으로 자식 트리의 수명을 소유한다. LSP 본문은 8 MiB, 채널 큐는 16 MiB 상한을 둔다. 엔진 viewport에 Raw HWND를 연결하고 플랫폼을 통해 IME 조합·후보 위치를 지정한다. 실제 엔진의 한국어 조합·확정은 아래 후속 UI 검증에서 확인했다. |

소스: [언어 클라이언트](../source/JBroEngine/Modules/JBroEditor/Source/Code/LanguageClient.cpp)·[위젯](../source/JBroEngine/Modules/JBroEditor/Source/Widget/CodeEditor.cpp)·[패널](../source/JBroEngine/Modules/JBroEditor/Source/Panel/CodeEditorPanel.cpp)·[Windows 채널/IME](../source/JBroEngine/Modules/JBroPlatform/Source/WindowsProcessChannel.cpp).

## 서드파티와 재현 범위

후보를 [ThirdParty/ImGuiColorTextEdit](../source/JBroEngine/ThirdParty/ImGuiColorTextEdit/README.md)의 별도 정적 라이브러리로 추가했다.
upstream pin은 `b12d713e77e8edddff1e69973f67085ac9cfaf74`이고 MIT LICENSE를 보존한다.
로컬 수정은 전체 삭제 뒤 커서/스크롤 유효성, 최종 LF 유무 보존, 의미 색상 오버레이·외부 Undo/Redo API의 세 범위다.
최종 LF 수정은 최초 독립 시험에서 관측한 `GetText`의 후행 개행 정규화를 제품 파일 편집에서 유지하지 않도록 한 변경이다.
[최종 jbro.patch](../source/JBroEngine/ThirdParty/ImGuiColorTextEdit/jbro.patch)는 Undo 단일 소유 조건까지 포함한다. `engine-pin-final`의 pin 원본 h/cpp에 검사·재적용하고 LF 정규화 후 현재 vendor 두 파일과 완전히 일치함을 확인했다([해시 기록](F:/AI/Temp/imgui-code-editor-spike/engine-vendor-pins.json)). 최종 cpp 해시는 `c3d30e3b7aa328507dcfdb43c0c635bcd50059506735f302d65285370bbdb08a`다. 최초 스파이크의 삭제 전용 패치는 이번 전체 수정의 재현물이 아니다.
임시 하네스는 D-244에 따라 커밋하지 않고, 제품의 정식 `CodeEditorTests.cpp`는 구분한다.

## 빌드·정식 시험·패치 검증

최종 framing 엄격 검사(`2garbage` 거절)·채널 실패 방어·재생 중 행동 설정을 포함한 결과다. Debug/Release 최종 빌드·CodeEditor 시험은 모두 종료 코드 0이다.

| 대상 | 확인 범위·증거 |
|---|---|
| Debug | 최종 전체 솔루션 빌드와 `CodeEditor` suite 1.0초 통과([최종 빌드](F:/AI/Temp/imgui-code-editor-spike/solution-debug-final.log)·[최종 시험](F:/AI/Temp/imgui-code-editor-spike/code-editor-debug-final.log)). |
| Release | 최종 전체 솔루션 빌드와 `CodeEditor` suite 0.8초 통과([최종 빌드](F:/AI/Temp/imgui-code-editor-spike/solution-release-final.log)·[최종 시험](F:/AI/Temp/imgui-code-editor-spike/code-editor-release-final.log)). 기존 C4723·생성 probe 경고는 빌드 실패와 구분한다. |
| 관련 회귀 10개 | `ScriptLanguageServer/CodeEditor/EditorUI/EditorLocalization/EditorGuideFocus/EditorGuide/EditorShortcut/ScriptProject/ScriptBuild/ScriptBreak` 통과([로그](F:/AI/Temp/imgui-code-editor-spike/integration-regressions.log)). |
| 긴 회귀 5개 | `EditorApplication/PlatformContract/CodeEditor/ContextBoundary/PublicHeaderComposition` 모두 통과했다. `EditorApplication`은 531.3초였다([로그](F:/AI/Temp/imgui-code-editor-spike/editor-lifecycle-regressions.log)). |
| vendor 재현 | `F:/AI/Temp/imgui-code-editor-spike/engine-pin-final`에서 최종 `jbro.patch`의 검사·재적용과 h/cpp LF 일치를 확인했다. 정확한 해시·명령은 [vendor 안내](../source/JBroEngine/ThirdParty/ImGuiColorTextEdit/README.md#pin-재현과-유지보수)에 있다. |

[CodeEditorTests.cpp](../source/JBroEngine/Tests/CodeEditorTests.cpp)는 프로토콜·오래된 버전 거절/재연결, 내용·이력/최종 LF·전체 삭제, 색상/진단/UTF-16, Undo 단일 소유, 실제 JBroc의 미저장 분석, 엔진 문서·저장 충돌·프로젝트 종료를 검사한다.
엄격 framing과 채널 실패 방어도 최종 suite에 포함한다.

## 실제 엔진 UI 확인

| 확인한 동작 | 증거 |
|---|---|
| 두 파일 탭·한국어 `JBC3004`·밑줄·문법/타입/주석 색상 | [탭](F:/AI/Temp/imgui-code-editor-spike/engine-ui/integrated-tabs.png)·[진단](F:/AI/Temp/imgui-code-editor-spike/engine-ui/integrated-diagnostics.png). |
| 미저장 `missingName → speed` 수정 뒤 진단 1개→0개 | [수정 후](F:/AI/Temp/imgui-code-editor-spike/engine-ui/unsaved-fixed.png). 탭의 `*`와 진단 0개를 함께 확인했다. |
| 실제 OS 한국어 IME의 `gks`/`rmf`로 `한` 조합→`한글` 확정 | [조합](F:/AI/Temp/imgui-code-editor-spike/engine-ui/ime-composing-engine.png)·[확정](F:/AI/Temp/imgui-code-editor-spike/engine-ui/ime-committed-engine.png). 완성 Unicode 문자 주입과 구분한다. |
| Ctrl+S 저장·미저장 재불러오기 | [저장](F:/AI/Temp/imgui-code-editor-spike/engine-ui/saved-ime.png) 뒤 실제 파일 바이트에 `// 한글 `을 확인했다. [버림 확인창](F:/AI/Temp/imgui-code-editor-spike/engine-ui/reload-unsaved-dialog.png)·[재불러온 상태](F:/AI/Temp/imgui-code-editor-spike/engine-ui/reload-confirmed.png)를 확인했다. |
| 실제 Ctrl+Z 한 번·Ctrl+Y | `AB → A`로 한 번만 되돌리고 `AB`로 복원했다([Undo](F:/AI/Temp/imgui-code-editor-spike/engine-ui/keyboard-one-undo.png)·[Redo](F:/AI/Temp/imgui-code-editor-spike/engine-ui/keyboard-one-redo.png)). |
| 전체 삭제·Ctrl+Z 전체 복구 | [빈 파일](F:/AI/Temp/imgui-code-editor-spike/engine-ui/delete-all-engine.png)·[복구](F:/AI/Temp/imgui-code-editor-spike/engine-ui/delete-all-engine-undo.png). |
| 실제 JBroc 자식 종료·재연결 | 자식을 강제 종료하면 연결 끊김·재연결 버튼이 표시되고 의미 색/진단이 비워졌다([끊김](F:/AI/Temp/imgui-code-editor-spike/engine-ui/language-server-disconnected.png)). 버튼을 누르면 같은 문서에서 정상 분석 연결을 회복했다([재연결](F:/AI/Temp/imgui-code-editor-spike/engine-ui/language-server-reconnected.png)). |

## 발견한 결함과 수정 근거

- 진단 문구의 `String`을 가변 인자로 잘못 넘겨 깨지던 표시를 `c_str()`로 고치고 상태 fallback·번역표 검사를 추가했다. 이전 `patrol.png`의 깨진 문구 대신 위 한국어 진단 캡처를 현재 표시 증거로 사용한다.
- Ctrl+Z 실기에서 Undo가 두 번 실행됐다. `externalUndoRedo` 헤더 API만 추가되고 cpp 단축키 조건이 빠져 있었기 때문이다. `TestUndoHasOneOwner`의 [수정 전 실패](F:/AI/Temp/imgui-code-editor-spike/undo-red.log)를 확인하고 cpp guard·Undo/Redo 직후 버퍼 revision 갱신으로 고쳤다. [수정 후 시험](F:/AI/Temp/imgui-code-editor-spike/undo-green.log)과 위 한 번 Undo/Redo 화면을 함께 확인했다.
- 최초 독립 시험의 전체 삭제·옛 커서/배치 인덱싱 결함은 [스파이크 기록](./imgui-code-editor-spike.md)에 남겼다. 이번 엔진 전체 삭제/복구와 정식 시험은 그 후속 검증이다.

## 후속 범위와 검증 한계

C++ 언어 서버·자동완성·정의 이동·디버거 통합은 후속 범위다. C++는 현재 편집·어휘 강조를 제공한다.
모든 문자/입력/프로젝트 수명 조합이나 성능 벤치마크를 검증한 것은 아니다.

구현 커밋은 로컬 `fc592bd6` (`feat(editor): edit scripts in file tabs with live JBroScript diagnostics`)다. 원격 push와 main 병합은 수행하지 않았다. 이번 요청의 main 병합 승인은 없으며 main의 별도 디버거 작업은 그대로 유지했다.
최종 샘플 Host(PID 74560)에서 두 파일 탭·분석 연결 정상·진단 0개를 다시 확인했다([최종 화면](F:/AI/Temp/imgui-code-editor-spike/engine-ui/final-tabs-check.png)).
병렬 main의 기존 D-330과 충돌하지 않도록 이번 결정과 참조만 D-335로 옮겼다. 기존 main 결정의 의미는 바꾸지 않았다.
