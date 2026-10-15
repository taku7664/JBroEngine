# ImGui 내장 코드 편집기 도입 전 독립 검증 (2026-10-15)

JBroScript·C++ 파일을 전용 도크와 파일 탭에서 편집하고 기존 언어 서버를 재사용하려는 요청의 선행 검증이다.
이번에는 임시 Win32/DX11 프로그램에서 세 항목을 시험했다. 실제 엔진 패널·`JBroRHI` 통합이나 제품 도입은 수행하지 않았다.

**검증 판단: 세 항목의 기능은 확인했지만 후보 원본 그대로의 도입은 보류한다. 전체 선택 후 삭제 충돌을 먼저 수정하고 재검증해야 한다.**
기존 언어·에디터 계약과 Decisions를 변경하는 기록은 아니다.

## 시험 구성

- 작업 브랜치: `codex/imgui-code-editor-spike`, 기준 `main`의 `9a8351c9`.
- 임시 프로브·증거: `F:/AI/Temp/imgui-code-editor-spike`.
- 후보: `goossens/ImGuiColorTextEdit`의 `b12d713e77e8edddff1e69973f67085ac9cfaf74` 원본. vendor 소스는 수정하지 않았다.
- ImGui: 엔진 작업트리의 `1.92.9b` cpp/h 및 `imconfig`(`IMGUI_USE_WCHAR32`)를 직접 컴파일했다.
  공식 Win32/DX11 backend는 `v1.92.9-docking`의 `9b4eb24cee2071e61dc1f9ef3e5228097cdde720`이다.
- MSVC v145, Debug x64 빌드 통과. 독립 실행에 필요한 엔진 `ItemLocator` 시험 훅은 no-op으로 연결했다.
  근거: [고정 버전](F:/AI/Temp/imgui-code-editor-spike/pins.json)·[빌드 로그](F:/AI/Temp/imgui-code-editor-spike/build.log).
- 소스·하네스는 D-244의 임시 시험 코드이므로 커밋 대상이 아니다. 이번 저장소 변경은 이 기록과 TODO 링크뿐이다.

## 세 항목 결과

| 항목 | 결과와 확인 범위 | 증거 |
|---|---|---|
| 실제 한국어 IME | **통과.** Microsoft Korean IME에서 VK 한/영 전환 후 ASCII `gksrmf`와 space를 입력했다. 완성 Unicode 문자 주입은 사용하지 않았다. 조합 중 `한`이 조합창에 보이고 버퍼에는 아직 확정되지 않은 상태를 확인했다. 최종 버퍼는 `한글 `과 개행이며 `WM_IME_STARTCOMPOSITION` 1회·`COMPOSITION` 8회·`RESULT` 2회를 관측했다. | [조합 화면](F:/AI/Temp/imgui-code-editor-spike/ime-composing.png)·[조합 상태](F:/AI/Temp/imgui-code-editor-spike/ime-composing.json)·[확정 화면](F:/AI/Temp/imgui-code-editor-spike/ime-committed.png)·[확정 상태](F:/AI/Temp/imgui-code-editor-spike/ime-committed.json) |
| 도킹·문서별 상태 | **통과.** native 도킹의 `.jscript`·`.cpp` 두 문서에서 내용·Undo/Redo·커서·선택·스크롤·dirty가 탭 왕복 뒤 동일했다. B의 Undo가 A를 바꾸지 않고 B의 Redo가 복구했으며 B의 110줄 스크롤이 유지됐다. 좌우 분할 화면도 확인했다. | [결과](F:/AI/Temp/imgui-code-editor-spike/result-tabs.json)·[A 이전](F:/AI/Temp/imgui-code-editor-spike/tab-a-before.json)·[A 복귀](F:/AI/Temp/imgui-code-editor-spike/tab-a-return.json)·[B 이전](F:/AI/Temp/imgui-code-editor-spike/tab-b-before.json)·[B 복귀](F:/AI/Temp/imgui-code-editor-spike/tab-b-return.json)·[B Undo](F:/AI/Temp/imgui-code-editor-spike/tab-b-undo.json)·[B Redo](F:/AI/Temp/imgui-code-editor-spike/tab-b-redo.json)·[분할 화면](F:/AI/Temp/imgui-code-editor-spike/tabs-split.png) |
| 기존 JBroScript 진단의 문자 위치 | **통과.** 기존 main의 `Build/x64/Debug/JBroc.exe --lsp`가 `String text = "한글😀" + missingValue`의 `JBC3004`를 발행했다. 0부터 세는 LSP UTF-16 범위 `line 4, character 31..43`을 위젯의 Unicode code point 인덱스 `30..42`로 변환했고, 실제 밑줄이 `missingValue`의 12글자와 일치했다. | [원문](F:/AI/Temp/imgui-code-editor-spike/source.txt)·[수신 진단](F:/AI/Temp/imgui-code-editor-spike/diagnostics.json)·[선택 진단](F:/AI/Temp/imgui-code-editor-spike/selected-diagnostic.json)·[표시 화면](F:/AI/Temp/imgui-code-editor-spike/diagnostic-display.png)·[표시 상태](F:/AI/Temp/imgui-code-editor-spike/diagnostic-display.json)·[결과](F:/AI/Temp/imgui-code-editor-spike/result-diagnostic.json) |

세 번째 항목은 실제 서버에서 받은 **진단 스냅샷을 읽는 시험 어댑터**다. 실시간 LSP 클라이언트는 구현하지 않았다.
`.cpp` 문서는 편집·탭 상태만 시험했으며 C++ 언어 서버는 연결하지 않았다.

## 도입을 막는 재현 결함

후보 원본에서 전체 선택 후 Backspace를 누르면 MSVC의 `vector subscript out of range` assertion이 발생했다.
`SelectAll()` API 뒤 실제 Backspace와 실제 Ctrl+A 뒤 Backspace의 두 경로에서 각각 재현했다.
스택은 `TextEditor::TypeSetter::docPos2VisPos → TextEditor::docPos2VisPos → handlePossibleScrolling → render`다.
오래된 `ensureVisiblePos`가 남는 것이 의심되지만 정확한 원인과 수정은 검증하지 않았다.
vendor 소스는 그대로 두었고 반복 재현에 사용한 시험 프로세스만 종료했다.

근거: [키보드 경로 스택](F:/AI/Temp/imgui-code-editor-spike/assert-stack.txt)·[API 선택 경로 스택](F:/AI/Temp/imgui-code-editor-spike/delete-all-api-stack.txt)·[삭제 전 상태](F:/AI/Temp/imgui-code-editor-spike/delete-all-before.json).

## 통합 전 남은 범위와 시험 중 교정

현재 엔진 `EditorUI`·`WindowsPlatform`에서 viewport의 `PlatformHandleRaw`에 HWND를 연결하거나 IME 창 위치를 지정하는 연결은 찾지 못했다.
기본 ImGui IME callback에는 Raw handle이 필요하고 독립 공식 backend는 이를 설정했다.
따라서 이번 한국어 통과를 실제 엔진의 IME 통합 완료로 해석하지 않는다. 엔진 도크·파일 열기/저장·실시간 LSP 연결 역시 이번 시험 범위 밖이다.

초기 프로브에서는 전역 `TextEditor`가 `defaultPalette`보다 먼저 초기화되어 글자가 투명했다.
`ProbeInit`에서 `GetDarkPalette`를 명시적으로 적용해 하네스 초기화 문제를 교정했다.
한국어 TSF context를 Python의 다른 프로세스에서 조회해 mode를 잘못 판정한 부분도 프로브 UI 스레드 안의 조회로 바꾼 뒤 IME 시험을 통과했다.
이는 시험 하네스 교정이며 후보 원본의 전체 삭제 결함을 수정한 것은 아니다.

다음 도입 판단에 필요한 최소 작업은 전체 삭제 충돌의 수정·재현 시험, 실제 엔진의 HWND/IME 위치 연결 확인, 실시간 JBroScript LSP 클라이언트 검증이다.
이번 결과로 엔진 통합·C++ 진단·후보의 전체 편집 동작을 검증했다고 보지 않는다.
