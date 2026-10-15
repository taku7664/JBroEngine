# ImGuiColorTextEdit 전체 삭제·스크롤 수정 패치

적용 대상은 `goossens/ImGuiColorTextEdit`의 `b12d713e77e8edddff1e69973f67085ac9cfaf74`에 있는 `TextEditor.cpp`다.
[0001-scroll-after-final-cursor.patch](./0001-scroll-after-final-cursor.patch)는 5개 hunk로 구성된다.
후보 원본의 [MIT LICENSE](./LICENSE)를 같은 폴더에 보존했다.

## 적용

해당 pin의 후보 소스를 준비한 뒤 `TextEditor.cpp`가 있는 폴더에서 실행한다. 아래 패치 경로는 자신의 JBroEngine checkout 경로로 바꾼다.

```powershell
$editorPatch = 'C:/path/to/JBroEngine/tasks/patches/imgui-code-editor/0001-scroll-after-final-cursor.patch'
git apply --check $editorPatch
git apply $editorPatch
```

## 수정 이유와 유지보수 범위

문서 변경 직후 옛 커서 좌표로 스크롤을 예약하던 호출을 transaction 끝의 커서 갱신 뒤로 옮겼다.
긴 문서 삭제 뒤 옛 배치를 새 문서의 줄로 해석하지 않도록 첫 줄 번호를 배치에서 직접 읽고, 스크롤·접기/펼치기 목적 줄을 마지막 유효 줄까지 제한했다.
원본 복사본에서 패치 검사·적용과 LF 정규화 후 수정 소스 일치를 확인했다.

`TextEditor.cpp`의 SHA-256:

| 대상 | SHA-256 |
|---|---|
| pinned 원본 | `9591def9022bbd5e693b1927955252af95fa980e81741411f1a3951b096733ee` |
| 패치 적용 후, LF 기준 | `5340dfa1205b2fc139577a02f6f7eba85bce46777164e76d843054b49ef0a2e3` |

후보를 새로 받거나 버전을 올리면 패치의 적용 가능성과 원인 경로를 다시 확인하고 삭제·스크롤·Undo/Redo 회귀를 재검증해야 한다.
이 패치는 임시 독립 위젯 검증용으로 보존한 소스 차이이며 엔진 통합·서드파티 의존성 추가·실시간 LSP 구현을 뜻하지 않는다.
임시 프로브·회귀시험 하네스는 D-244에 따라 커밋 대상에서 제외한다.
검증 범위와 증거는 [독립 시험 기록](../../imgui-code-editor-spike.md#삭제-충돌-수정과-독립-재검증-2026-10-15)에 있다.
