# ImGuiColorTextEdit

엔진 내장 코드 편집기의 위젯이다. upstream은 `goossens/ImGuiColorTextEdit`, 기준 commit은 `b12d713e77e8edddff1e69973f67085ac9cfaf74`다.
원본의 [MIT LICENSE](./LICENSE)를 보존한다. `ImGuiColorTextEdit.vcxproj`는 엔진의 ImGui 설정을 사용하는 별도 정적 라이브러리다.

## 로컬 수정

1. 전체 삭제 뒤 커서·스크롤: 최종 커서 갱신 뒤 가시성을 예약하고, 옛 배치를 새 문서의 삭제된 줄로 해석하지 않으며 목적 줄을 유효 범위에 제한한다.
2. 최종 LF 보존: `GetText`와 문서 왕복이 파일 끝에 없던 개행을 임의로 붙이지 않도록 한다. 기존 독립 스파이크가 맞춰 비교한 후행 개행 정규화와 구분한다.
3. 의미 색상·외부 Undo API: glyph별 `SetTextColor`/`ClearTextColors`로 JBroScript 의미 토큰 색을 덧입히고, `SetExternalUndoRedo`와 cpp 단축키 guard로 엔진 행동 표만 Undo/Redo를 처리하게 한다. 헤더 API만 추가해 단축키가 이중 실행됐던 결함은 후속 회귀로 잡아 보완했다.

엔진 패널은 `Widget::CodeEditor`를 통해 이 위젯을 사용한다. LSP 프로세스·파일 저장·도크 수명은 vendor 코드가 맡지 않는다.

## pin 재현과 유지보수

원본 commit의 `TextEditor.h`·`TextEditor.cpp`·`LICENSE`를 별도 폴더에 준비하고, 이번 로컬 수정 전체를 담은 [jbro.patch](./jbro.patch)를 그 폴더에서 다음 순서로 적용한다.
아래 checkout 경로는 자신의 절대 경로로 바꾼다. 최초 독립 스파이크의 삭제 전용 `0001-scroll-after-final-cursor.patch`만으로는 위 세 수정 전체가 재현되지 않는다.

```powershell
$editorPatch = 'C:/path/to/JBroEngine/source/JBroEngine/ThirdParty/ImGuiColorTextEdit/jbro.patch'
git apply --check $editorPatch
git apply $editorPatch
```

재적용한 h/cpp를 현재 vendor 파일과 LF 정규화 기준으로 비교하고 엔진 빌드·정식 CodeEditor 시험을 다시 실행한다.
upstream pin을 바꿀 때도 세 수정의 필요 여부와 패치 문맥을 다시 검토한다. LICENSE와 원본 고지는 유지한다.
Undo cpp guard를 포함한 최종 `jbro.patch`를 `F:/AI/Temp/imgui-code-editor-spike/engine-pin-final`의 pin 원본 h/cpp에 검사·재적용했고, LF 정규화 후 현재 vendor 두 파일과 완전히 일치했다([검증 해시](F:/AI/Temp/imgui-code-editor-spike/engine-vendor-pins.json)).

| 파일 | upstream | 현재 로컬 수정, LF 기준 |
|---|---|---|
| `TextEditor.h` | `fb7e3a27c9b1f6a2898f16f69ca415e74267a21e9e3eab0e97599ca689b8e9e3` | `8ef45ac105207df7017e9393f4528bef18cbaf9d33b25428881c19c2ad16bfbe` |
| `TextEditor.cpp` | `9591def9022bbd5e693b1927955252af95fa980e81741411f1a3951b096733ee` | `c3d30e3b7aa328507dcfdb43c0c635bcd50059506735f302d65285370bbdb08a` |

최종 Debug/Release 전체 빌드·CodeEditor suite, 관련 회귀 10개·긴 회귀 5개와 실제 IME·진단 해제·탭·저장/재불러오기·단일 Undo/Redo·전체 삭제/복구·LSP 재연결을 확인했다. 정확한 범위와 후속 기능은 [통합 기록](../../../../tasks/imgui-code-editor-integration.md)에 있다.
임시 독립 프로브·회귀시험 하네스는 D-244에 따라 커밋하지 않는다.
