# ImGui 내장 코드 편집기 도입 전 독립 검증 (2026-10-15)

JBroScript·C++ 파일을 전용 도크와 파일 탭에서 편집하고 기존 언어 서버를 재사용하려는 요청의 선행 검증이다.
이번에는 임시 Win32/DX11 프로그램에서 세 항목을 시험했다. 실제 엔진 패널·`JBroRHI` 통합이나 제품 도입은 수행하지 않았다.

**현재 판정: 삭제 충돌 수정 패치를 적용한 후보에서 삭제 회귀 10개와 실제 키보드 삭제·Undo/Redo, 핵심 3항목 재검증이 통과했다. 실제 엔진 통합과 실시간 LSP 연결은 남아 있다.**
기존 언어·에디터 계약과 Decisions를 변경하는 기록은 아니다.

## 시험 구성

- 작업 브랜치: `codex/imgui-code-editor-spike`, 기준 `main`의 `9a8351c9`.
- 임시 프로브·증거: `F:/AI/Temp/imgui-code-editor-spike`.
- 최초 후보: `goossens/ImGuiColorTextEdit`의 `b12d713e77e8edddff1e69973f67085ac9cfaf74` 원본. 최초 시험에서는 vendor 소스를 수정하지 않았다. 후속 수정·재검증은 아래에 구분한다.
- ImGui: 엔진 작업트리의 `1.92.9b` cpp/h 및 `imconfig`(`IMGUI_USE_WCHAR32`)를 직접 컴파일했다.
  공식 Win32/DX11 backend는 `v1.92.9-docking`의 `9b4eb24cee2071e61dc1f9ef3e5228097cdde720`이다.
- MSVC v145, Debug x64 빌드 통과. 독립 실행에 필요한 엔진 `ItemLocator` 시험 훅은 no-op으로 연결했다.
  근거: [고정 버전](F:/AI/Temp/imgui-code-editor-spike/pins.json)·[빌드 로그](F:/AI/Temp/imgui-code-editor-spike/build.log).
- 임시 프로브·회귀시험 하네스는 D-244에 따라 커밋 대상이 아니다. 최초 기록은 이 문서와 TODO 링크였으며, 후속 후보 수정은 재적용 가능한 패치로 별도 보존한다.

## 최초 세 항목 결과

| 항목 | 결과와 확인 범위 | 증거 |
|---|---|---|
| 실제 한국어 IME | **통과.** Microsoft Korean IME에서 VK 한/영 전환 후 ASCII `gksrmf`와 space를 입력했다. 완성 Unicode 문자 주입은 사용하지 않았다. 조합 중 `한`이 조합창에 보이고 버퍼에는 아직 확정되지 않은 상태를 확인했다. 최종 버퍼는 `한글 `과 개행이며 `WM_IME_STARTCOMPOSITION` 1회·`COMPOSITION` 8회·`RESULT` 2회를 관측했다. | [조합 화면](F:/AI/Temp/imgui-code-editor-spike/ime-composing.png)·[조합 상태](F:/AI/Temp/imgui-code-editor-spike/ime-composing.json)·[확정 화면](F:/AI/Temp/imgui-code-editor-spike/ime-committed.png)·[확정 상태](F:/AI/Temp/imgui-code-editor-spike/ime-committed.json) |
| 도킹·문서별 상태 | **통과.** native 도킹의 `.jscript`·`.cpp` 두 문서에서 내용·Undo/Redo·커서·선택·스크롤·dirty가 탭 왕복 뒤 동일했다. B의 Undo가 A를 바꾸지 않고 B의 Redo가 복구했으며 B의 110줄 스크롤이 유지됐다. 좌우 분할 화면도 확인했다. | [결과](F:/AI/Temp/imgui-code-editor-spike/result-tabs.json)·[A 이전](F:/AI/Temp/imgui-code-editor-spike/tab-a-before.json)·[A 복귀](F:/AI/Temp/imgui-code-editor-spike/tab-a-return.json)·[B 이전](F:/AI/Temp/imgui-code-editor-spike/tab-b-before.json)·[B 복귀](F:/AI/Temp/imgui-code-editor-spike/tab-b-return.json)·[B Undo](F:/AI/Temp/imgui-code-editor-spike/tab-b-undo.json)·[B Redo](F:/AI/Temp/imgui-code-editor-spike/tab-b-redo.json)·[분할 화면](F:/AI/Temp/imgui-code-editor-spike/tabs-split.png) |
| 기존 JBroScript 진단의 문자 위치 | **통과.** 기존 main의 `Build/x64/Debug/JBroc.exe --lsp`가 `String text = "한글😀" + missingValue`의 `JBC3004`를 발행했다. 0부터 세는 LSP UTF-16 범위 `line 4, character 31..43`을 위젯의 Unicode code point 인덱스 `30..42`로 변환했고, 실제 밑줄이 `missingValue`의 12글자와 일치했다. | [원문](F:/AI/Temp/imgui-code-editor-spike/source.txt)·[수신 진단](F:/AI/Temp/imgui-code-editor-spike/diagnostics.json)·[선택 진단](F:/AI/Temp/imgui-code-editor-spike/selected-diagnostic.json)·[표시 화면](F:/AI/Temp/imgui-code-editor-spike/diagnostic-display.png)·[표시 상태](F:/AI/Temp/imgui-code-editor-spike/diagnostic-display.json)·[결과](F:/AI/Temp/imgui-code-editor-spike/result-diagnostic.json) |

세 번째 항목은 실제 서버에서 받은 **진단 스냅샷을 읽는 시험 어댑터**다. 실시간 LSP 클라이언트는 구현하지 않았다.
`.cpp` 문서는 편집·탭 상태만 시험했으며 C++ 언어 서버는 연결하지 않았다.

## 최초 원본에서 재현한 결함 (수정 전 이력)

후보 원본에서 전체 선택 후 Backspace를 누르면 MSVC의 `vector subscript out of range` assertion이 발생했다.
`SelectAll()` API 뒤 실제 Backspace와 실제 Ctrl+A 뒤 Backspace의 두 경로에서 각각 재현했다.
스택은 `TextEditor::TypeSetter::docPos2VisPos → TextEditor::docPos2VisPos → handlePossibleScrolling → render`다.
당시에는 오래된 `ensureVisiblePos`가 남는 것을 의심했으나 정확한 원인과 수정은 검증하지 못했다.
이 최초 시험에서는 vendor 소스를 그대로 두었고 반복 재현에 사용한 시험 프로세스만 종료했다.

근거: [키보드 경로 스택](F:/AI/Temp/imgui-code-editor-spike/assert-stack.txt)·[API 선택 경로 스택](F:/AI/Temp/imgui-code-editor-spike/delete-all-api-stack.txt)·[삭제 전 상태](F:/AI/Temp/imgui-code-editor-spike/delete-all-before.json).

## 통합 전 남은 범위와 시험 중 교정

현재 엔진 `EditorUI`·`WindowsPlatform`에서 viewport의 `PlatformHandleRaw`에 HWND를 연결하거나 IME 창 위치를 지정하는 연결은 찾지 못했다.
기본 ImGui IME callback에는 Raw handle이 필요하고 독립 공식 backend는 이를 설정했다.
따라서 이번 한국어 통과를 실제 엔진의 IME 통합 완료로 해석하지 않는다. 엔진 도크·파일 열기/저장·실시간 LSP 연결 역시 이번 시험 범위 밖이다.

초기 프로브에서는 전역 `TextEditor`가 `defaultPalette`보다 먼저 초기화되어 글자가 투명했다.
`ProbeInit`에서 `GetDarkPalette`를 명시적으로 적용해 하네스 초기화 문제를 교정했다.
한국어 TSF context를 Python의 다른 프로세스에서 조회해 mode를 잘못 판정한 부분도 프로브 UI 스레드 안의 조회로 바꾼 뒤 IME 시험을 통과했다.
이는 시험 하네스 교정이며 후보 원본의 전체 삭제 결함을 수정한 것은 아니다.

최초 검증 뒤 전체 삭제 충돌의 수정·재현 시험을 선행 작업으로 남겼다. 그 후속 결과는 다음 절에 기록한다.
실제 엔진의 HWND/IME 위치 연결과 실시간 JBroScript LSP 클라이언트는 여전히 미검증이다. 엔진 통합·C++ 진단·후보의 전체 편집 동작을 검증했다는 뜻이 아니다.

## 삭제 충돌 수정과 독립 재검증 (2026-10-15)

사용자가 “전체 삭제 충돌부터 고치고 다시 검증해”라고 승인한 뒤, 임시 후보 `F:/AI/Temp/imgui-code-editor-spike/TextEditor.cpp`에 수정했다.
원본 pin은 그대로이며 제품 엔진·서드파티 의존성은 추가하지 않았다. 저장소에는 [5개 hunk 패치](./patches/imgui-code-editor/0001-scroll-after-final-cursor.patch)와 [적용 안내](./patches/imgui-code-editor/README.md)를 남긴다.

확정 원인은 두 경로였다. `insertText`·`deleteText`가 문서를 바꾼 직후 호출자의 커서 조정 전에 `makeCursorVisible()`로 옛 좌표를 예약했다.
두 하위 함수의 호출을 제거하고 `endTransaction`에서 `cursor.update(document)` 뒤 가시성 예약을 수행하도록 옮겼다.
첫 수정 뒤 긴 문서 아래에서 전체 삭제하는 시험이 다시 실패했다. `updateState`가 옛 `typeSetter` 배치를 새 문서에 대입하여 이미 삭제된 줄을 인덱싱했다.
이전 첫 줄 번호를 옛 배치의 `row.line`에서 직접 읽고, `scrollToLine`을 `normalizeLine`으로 마지막 유효 줄에 제한했으며 접기/펼치기도 제한한 줄을 사용하도록 보완했다.
추가 실패 스택은 [scrolled-stack.txt](F:/AI/Temp/imgui-code-editor-spike/scrolled-stack.txt)에 있다.

| 재검증 | 결과·증거 |
|---|---|
| 원본 실패 → 수정 후 headless 회귀 | 새 독립 시험의 원본 `backspace`는 vector assertion과 종료 코드 70으로 실패했다([수정 전](F:/AI/Temp/imgui-code-editor-spike/scroll-regression-before.txt)). 최종 `backspace/delete/replace/cut/partial/single/empty/scrolled/wrapped/folded` 10개는 모두 종료 코드 0이다([수정 후](F:/AI/Temp/imgui-code-editor-spike/scroll-regression-after.json)). 내용과 유효 커서, 불러온 문서의 정확한 Undo 원복·Redo 재편집을 확인했으며 빈 문서에는 Undo가 없다. |
| 실제 Win32 키보드 | 실제 Ctrl+A 뒤 Backspace·Delete, B 문서 110줄 스크롤 뒤 전체 삭제, 실제 Ctrl+Z/Ctrl+Y가 통과했다. 다른 문서가 바뀌지 않는 조건도 확인했다([결과](F:/AI/Temp/imgui-code-editor-spike/result-delete-all.json)·[Undo 복원 화면](F:/AI/Temp/imgui-code-editor-spike/fixed-delete-undo-restored.png)). 각 경로의 전/삭제/Undo/Redo 상태는 증거 폴더의 `fixed-{backspace,delete,scrolled}-{before,removed,undo,redo}.json`에 보존했다. |
| 수정 후 핵심 3항목 | 실제 한국어 IME `gksrmf → 한글 `과 개행(조합 8회·결과 2회), 문서별 탭 상태·좌우 분할, 실제 JBroScript 진단 스냅샷 `missingValue`의 UTF-16 `31..43 → glyph 30..42` 표시를 다시 통과했다([IME](F:/AI/Temp/imgui-code-editor-spike/result-ime.json)·[탭](F:/AI/Temp/imgui-code-editor-spike/result-tabs.json)·[진단](F:/AI/Temp/imgui-code-editor-spike/result-diagnostic.json)). 위의 IME·분할·진단 화면 파일도 재검증 결과로 갱신했다. |

시험 기대조건도 바로잡았다. 11글자 대체 입력은 11개 transaction이므로 Undo를 모두 되돌린 뒤 원문을 비교했고, 개행 없는 한 줄 입력은 `GetText`가 붙이는 후행 개행을 baseline에 반영했다.
이는 시험 기대값 교정이며 새 언어·저장 정책의 결정이 아니다.

pinned 원본 복사본에 `git apply --check`와 `git apply`가 성공했다. Windows Git의 `autocrlf`로 CRLF만 달랐으므로 LF 정규화 후 수정 소스와 동일함을 확인했다.
`TextEditor.cpp` SHA-256은 원본 `9591def9022bbd5e693b1927955252af95fa980e81741411f1a3951b096733ee`, 수정 후 LF 기준 `5340dfa1205b2fc139577a02f6f7eba85bce46777164e76d843054b49ef0a2e3`이다([fixed-pins.json](F:/AI/Temp/imgui-code-editor-spike/fixed-pins.json)).
최종 [통합 검증 결과](F:/AI/Temp/imgui-code-editor-spike/verification-after-fix.json)는 headless 10개·실제 키보드 3개·핵심 3항목과 pinned 원본의 패치 재적용 통과를 함께 기록한다.

삭제 결함의 수정·독립 재검증은 완료했다. 실제 엔진의 Raw HWND/IME 위치 연결, 엔진 패널·파일 작업과 실시간 LSP 클라이언트는 여전히 남아 있다.
`.cpp`는 문서 편집만 확인했고 C++ 언어 서버는 연결하지 않았다. 임시 프로브·회귀시험 코드는 커밋하지 않는다.
