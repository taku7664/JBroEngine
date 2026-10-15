# ImGuiColorTextEdit

엔진 내장 코드 편집기의 위젯이다. upstream은 `goossens/ImGuiColorTextEdit`, 기준 commit은 `b12d713e77e8edddff1e69973f67085ac9cfaf74`다.
원본의 [MIT LICENSE](./LICENSE)를 보존한다. `ImGuiColorTextEdit.vcxproj`는 엔진의 ImGui 설정을 사용하는 별도 정적 라이브러리다.

## 로컬 수정

1. 전체 삭제 뒤 커서·스크롤: 최종 커서 갱신 뒤 가시성을 예약하고, 옛 배치를 새 문서의 삭제된 줄로 해석하지 않으며 목적 줄을 유효 범위에 제한한다.
2. 최종 LF 보존: `GetText`와 문서 왕복이 파일 끝에 없던 개행을 임의로 붙이지 않도록 한다. 기존 독립 스파이크가 맞춰 비교한 후행 개행 정규화와 구분한다.
3. 의미 색상·외부 Undo API: glyph별 `SetTextColor`/`ClearTextColors`로 JBroScript 의미 토큰 색을 덧입히고, `SetExternalUndoRedo`와 cpp 단축키 guard로 엔진 행동 표만 Undo/Redo를 처리하게 한다. 헤더 API만 추가해 단축키가 이중 실행됐던 결함은 후속 회귀로 잡아 보완했다.
4. 정확한 편집 통지: `ReplaceSectionsText`로 떨어진 여러 범위를 한 번에 되돌리며, 기존 transaction callback에 변경 종류를 조회하는 API를 더했다. Undo callback은 실제 역실행 순서대로 통지한다. 엔진은 이 구간을 사용해 동일한 줄 삽입과 큰 파일의 중단점 위치를 추측 없이 갱신한다.
5. 줄 번호 조작: 왼쪽 여백까지 줄 선택 영역으로 사용하고, 마우스가 가리키는 줄 전체를 `lineNumberHover` 팔레트 색으로 강조한다. 클릭·양방향 드래그는 줄 단위로 선택하며 중단점 영역과 겹치지 않는다. 확대·스크롤·접힌 줄·자동 줄바꿈의 실제 배치를 사용한다. 줄바꿈 뒤의 중단점 장식 위치도 화면 행을 기준으로 맞춘다.
6. 참조 배경: `SetRangeHighlights`/`ClearRangeHighlights`는 진단 밑줄과 의미 색상을 건드리지 않는 별도 범위 배경이다. 엔진 래퍼가 소스 변경 시 제거하며, 언어 서버 결과의 버전 검증도 엔진이 맡는다. 우클릭 메뉴·툴팁·UTF-16 선택은 기존 vendor API와 `Widget::CodeEditor`에서 연결한다.
7. 팝업 간격: 글자 배치를 위한 `ItemSpacing(0, 0)`을 메뉴와 찾기·바꾸기 UI에 전파하지 않는다. 팝업 구간에는 호출자가 사용하던 간격을 복원하므로 엔진 공통 메뉴의 행 높이와 이름·단축키 사이 간격을 따른다. 코드 편집기의 메뉴 항목은 공통 `Widget::MenuItem`을 사용하며, 코드용 16px 글꼴 대신 호출자의 UI 글꼴과 크기를 복원한다. 크기는 확대 배율을 중복 적용하지 않도록 `FontSizeBase`로 보관한다.
8. 클립보드 행동 소유권: `SetExternalClipboardActions`를 켜면 Ctrl+X/C/V/A·Ctrl+Delete·Ctrl+Insert·Shift+Insert를 vendor에서 실행하거나 소비하지 않는다. 엔진 패널은 행동 표와 사용자 키 설정 한 경로에서 처리하고 우클릭 메뉴도 같은 행동을 표시한다. 외부 행동 관리자가 없는 위젯은 native 편집 기능만 제공하고 고정 단축키를 메뉴에 표시하지 않는다. native 잘라내기는 읽기 전용에서 차단하며, 중복돼 있던 붙여넣기 Ctrl+Insert 분기는 Shift+Insert로 바로잡았다. 심볼·중단점·요약 툴팁은 엔진 래퍼의 공통 `HoveredTooltip` 지연 정책을 따른다.
9. 팝업 수명 위임: `SetTextContextMenuRenderer`를 설정하면 vendor는 우클릭 요청과 소스 위치만 넘기고, 매 프레임 호출되는 소유자가 팝업의 열기·시작·종료를 맡는다. 엔진 래퍼는 공통 `Widget::OpenContextMenu`·`BeginOpenedContextMenu`·`EndContextMenu`로 전체 수명을 처리한다. renderer를 설정하지 않은 독립 사용자는 기존 upstream callback과 팝업 동작을 유지한다. ThirdParty에 엔진 헤더 의존성은 추가하지 않는다.

엔진 패널은 `Widget::CodeEditor`를 통해 이 위젯을 사용한다. LSP 프로세스·파일 저장·도크 수명은 vendor 코드가 맡지 않는다.

## pin 재현과 유지보수

원본 commit의 `TextEditor.h`·`TextEditor.cpp`·`LICENSE`를 별도 폴더에 준비하고, 이번 로컬 수정 전체를 담은 [jbro.patch](./jbro.patch)를 그 폴더에서 다음 순서로 적용한다.
아래 checkout 경로는 자신의 절대 경로로 바꾼다. 최초 독립 스파이크의 삭제 전용 `0001-scroll-after-final-cursor.patch`만으로는 위 수정 전체가 재현되지 않는다.

```powershell
$editorPatch = 'C:/path/to/JBroEngine/source/JBroEngine/ThirdParty/ImGuiColorTextEdit/jbro.patch'
git apply --check $editorPatch
git apply $editorPatch
```

재적용한 h/cpp를 현재 vendor 파일과 LF 정규화 기준으로 비교하고 엔진 빌드·정식 CodeEditor 시험을 다시 실행한다.
upstream pin을 바꿀 때도 로컬 수정의 필요 여부와 패치 문맥을 다시 검토한다. LICENSE와 원본 고지는 유지한다.
줄 번호 조작과 참조 배경을 포함한 `jbro.patch`는 해시가 일치하는 pin 원본에 검사·재적용했고, LF 정규화 후 현재 vendor 두 파일과 완전히 일치했다([검증 해시](F:/AI/Temp/imgui-code-editor-spike/gutter-reapply-20261015/vendor-hashes.json)).

| 파일 | upstream | 현재 로컬 수정, LF 기준 |
|---|---|---|
| `TextEditor.h` | `fb7e3a27c9b1f6a2898f16f69ca415e74267a21e9e3eab0e97599ca689b8e9e3` | `ffa82326ccdd18f41ec5818e3c19d2afe58bfc0a6dd30da88437822d9b4b3363` |
| `TextEditor.cpp` | `9591def9022bbd5e693b1927955252af95fa980e81741411f1a3951b096733ee` | `97db6f3007b84827bc811bb3a800be9ca5c39b48c4086adbd1af4e4e580a1d8f` |

최초 통합에서는 Debug/Release 전체 빌드·CodeEditor suite, 관련 회귀 10개·긴 회귀 5개와 실제 IME·진단 해제·탭·저장/재불러오기·단일 Undo/Redo·전체 삭제/복구·LSP 재연결을 확인했다. 정확한 편집 통지 확장은 Debug 위젯 회귀에서 동일 줄 삽입, 1,500줄 파일의 떨어진 편집, 중간 이력 복원과 역순 Undo 통지를 검증했다. 정확한 범위와 후속 기능은 [통합 기록](../../../../tasks/imgui-code-editor-integration.md)에 있다.
줄 번호·참조 배경 확장은 정식 Debug `CodeEditor` 회귀에서 전체 줄 호버, 한글·탭·빈 줄·마지막 줄 선택, 양방향 드래그, 22px 확대와 가로·세로 스크롤, 중단점 프리뷰와 선택의 분리, UTF-16 범위 선택, 툴팁과 참조 배경, 우클릭 메뉴의 단일 Undo/Redo 및 읽기 전용 동작을 검증했다. 실제 호스트의 화면 검증은 엔진 통합 기록에서 별도로 보고한다.
팝업 간격 회귀는 실제 우클릭 메뉴에서 호출자 간격 `(8, 6)`과 `(13, 9)`를 각각 검사한다. 메뉴의 행 간격·단축키 칸 사이 간격과 `Render` 뒤의 스타일·스타일 스택 복원을 검증하며, 수정 전에는 호출자 간격 검사가 실패했고 수정 뒤 정식 Debug `CodeEditor` suite를 통과했다.
글꼴 회귀는 실제 엔진 코드 글꼴이 켜진 위젯에서 UI 13px·별도 글꼴 19px·크기 변경 21px·1.5배 확대 조건의 메뉴 높이와 이름 폭·단축키 간격, 호출 후 글꼴과 스택 복원을 검사했다. 코드 글꼴이 메뉴에 전파되던 기존 구현에서 행 높이 검사가 실패했고, 래퍼에서 UI 글꼴을 복원한 뒤 정식 Debug `CodeEditor` suite를 통과했다.
임시 독립 프로브·회귀시험 하네스는 D-244에 따라 커밋하지 않는다.
