# ImGuiColorTextEdit

엔진 내장 코드 편집기의 위젯이다. upstream은 `goossens/ImGuiColorTextEdit`, 기준 commit은 `b12d713e77e8edddff1e69973f67085ac9cfaf74`다.
원본의 [MIT LICENSE](./LICENSE)를 보존한다. `ImGuiColorTextEdit.vcxproj`는 엔진의 ImGui 설정을 사용하는 별도 정적 라이브러리다.

## 로컬 수정

1. 전체 삭제 뒤 커서·스크롤: 최종 커서 갱신 뒤 가시성을 예약하고, 옛 배치를 새 문서의 삭제된 줄로 해석하지 않으며 목적 줄을 유효 범위에 제한한다.
2. 최종 LF 보존: `GetText`와 문서 왕복이 파일 끝에 없던 개행을 임의로 붙이지 않도록 한다. 기존 독립 스파이크가 맞춰 비교한 후행 개행 정규화와 구분한다.
3. 의미 색상·외부 Undo API: glyph별 `SetTextColor`/`ClearTextColors`로 JBroScript 의미 토큰 색을 덧입히고, `SetExternalUndoRedo`와 cpp 단축키 guard로 엔진 행동 표만 Undo/Redo를 처리하게 한다. 헤더 API만 추가해 단축키가 이중 실행됐던 결함은 후속 회귀로 잡아 보완했다.
4. 정확한 편집 통지: `ReplaceSectionsText`로 떨어진 여러 범위를 한 번에 되돌리며, 기존 transaction callback에 변경 종류를 조회하는 API를 더했다. Undo callback은 실제 역실행 순서대로 통지한다. 엔진은 이 구간을 사용해 동일한 줄 삽입과 큰 파일의 중단점 위치를 추측 없이 갱신한다.

엔진 패널은 `Widget::CodeEditor`를 통해 이 위젯을 사용한다. LSP 프로세스·파일 저장·도크 수명은 vendor 코드가 맡지 않는다.

## pin 재현과 유지보수

원본 commit의 `TextEditor.h`·`TextEditor.cpp`·`LICENSE`를 별도 폴더에 준비하고, 이번 로컬 수정 전체를 담은 [jbro.patch](./jbro.patch)를 그 폴더에서 다음 순서로 적용한다.
아래 checkout 경로는 자신의 절대 경로로 바꾼다. 최초 독립 스파이크의 삭제 전용 `0001-scroll-after-final-cursor.patch`만으로는 위 네 수정 전체가 재현되지 않는다.

```powershell
$editorPatch = 'C:/path/to/JBroEngine/source/JBroEngine/ThirdParty/ImGuiColorTextEdit/jbro.patch'
git apply --check $editorPatch
git apply $editorPatch
```

재적용한 h/cpp를 현재 vendor 파일과 LF 정규화 기준으로 비교하고 엔진 빌드·정식 CodeEditor 시험을 다시 실행한다.
upstream pin을 바꿀 때도 네 수정의 필요 여부와 패치 문맥을 다시 검토한다. LICENSE와 원본 고지는 유지한다.
정확한 편집 통지를 포함한 `jbro.patch`는 해시가 일치하는 pin 원본에 검사·재적용했고, LF 정규화 후 현재 vendor 두 파일과 완전히 일치했다([검증 해시](F:/AI/Temp/imgui-code-editor-spike/anchor-reapply-e357uwwa/vendor-hashes.json)).

| 파일 | upstream | 현재 로컬 수정, LF 기준 |
|---|---|---|
| `TextEditor.h` | `fb7e3a27c9b1f6a2898f16f69ca415e74267a21e9e3eab0e97599ca689b8e9e3` | `8bc61bf302e9d8d6ec4372c6342279b62df4582dab5654a7047145f725bb6c92` |
| `TextEditor.cpp` | `9591def9022bbd5e693b1927955252af95fa980e81741411f1a3951b096733ee` | `1d08fdb54ce32646f6eebf896109a6cd97ec386852547cbbfb8c3d321ff3f57b` |

최초 통합에서는 Debug/Release 전체 빌드·CodeEditor suite, 관련 회귀 10개·긴 회귀 5개와 실제 IME·진단 해제·탭·저장/재불러오기·단일 Undo/Redo·전체 삭제/복구·LSP 재연결을 확인했다. 정확한 편집 통지 확장은 Debug 위젯 회귀에서 동일 줄 삽입, 1,500줄 파일의 떨어진 편집, 중간 이력 복원과 역순 Undo 통지를 검증했다. 정확한 범위와 후속 기능은 [통합 기록](../../../../tasks/imgui-code-editor-integration.md)에 있다.
임시 독립 프로브·회귀시험 하네스는 D-244에 따라 커밋하지 않는다.
