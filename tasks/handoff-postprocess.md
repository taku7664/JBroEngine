# 인계: 후처리와 노드 그래프 (2026-10-14)

`postprocess` 브랜치를 이어받는 사람이 무엇이 섰고 다음에 무엇을 할지 바로 알게 하는 문서다.
결정의 원문은 `tasks/todo.md` 의 D-321~D-324, 계획의 원문은 [postprocess-plan.md](./postprocess-plan.md)·[node-graph-plan.md](./node-graph-plan.md) 다.
이 문서와 원문이 어긋나면 원문이 맞다.

## 어디서 받나

`main` 과 `postprocess` 가 같은 커밋이다(`818343da`). 둘 다 `origin` 에 올라가 있다.

```bash
git fetch origin
git switch postprocess
```

## 섰다

| 무엇 | 결정 | 어디 |
|---|---|---|
| 에셋 핸들 타입 비트 4 → 8 (`TypeShift` 28 → 24) | D-322 (2) | `JBroAsset/Include/JBro/Asset/Asset.h` |
| 노드 그래프 모델·도메인·YAML·샘플 도메인 `Math` | D-323 | `Modules/JBroNodeGraph` |
| 노드 그래프 창·노드 인스펙터·창마다의 실행 취소·저장 묻기 | D-324 | `JBroEditor/Source/Panel/NodeGraph*` |
| 휠 줌이 따라가는 내내 마우스 아래 점을 붙잡음 | D-321 (1) | `JBroEditor/Source/ViewCamera.cpp` |
| 오브젝트·레이어 복사가 시스템 클립보드의 YAML 글자 | D-321 (2)~(4) | `JBroCanvas/Source/CanvasFile.cpp` 의 `WriteObjectsText`·`ReadObjectsText` |
| 노드 찾기 메뉴·주석·경유점·미니맵·노드 안 값 칸 | D-321 (5) | `NodeGraphView.cpp`·`Widget/NodeGraphCanvas.cpp`·`NodeGraphPanel.cpp` |

## 다음에 시작할 일

후처리 계획 [postprocess-plan.md](./postprocess-plan.md) §3 의 **1 단계**다. 아직 손대지 않았다.

1. RHI 바인딩 레이아웃: 파이프라인 설명에 슬롯 표(상수 버퍼·읽기 텍스처·UAV·샘플러마다 개수와 보이는 단계). 상한은 `DeviceCaps` 로 묻는다.
2. 상수 버퍼 묶기 `SetConstantBuffer(slot, buffer, offset, size)` 와 프레임 슬롯마다의 업로드 링. 프레임 경로에 할당이 없어야 한다.
3. 비인덱스 `Draw`(전체 화면 삼각형).
4. 포맷: `R11G11B10F`·`R16F`·`R32F`·`RG16F`·`RGBA32F`.

세 백엔드(D3D12·D3D11·Vulkan)를 같이 고치고, 픽셀 시험은 디버그 레이어를 켜고 잰다(계획 §3 머리). 백엔드마다 다른 점은 계획 §2.1 에 있다
(Vulkan 은 비어 있는 binding 0~7 에 상수 버퍼, D3D11 은 11.1 의 `*SetConstantBuffers1`).

노드 그래프는 사용자와 다듬는 단계(node-graph-plan §3 의 3)다. 사용자 지적이 오면 그것부터 한다.

## 남은 것

| 무엇 | 상태 |
|---|---|
| 전체 시험 | 합치기 전 실행이 사용자 결정으로 끝나기 전에 합쳤다. 62 스위트까지 실패 없음, 그 뒤는 결과를 못 봤다. 다음 작업 전에 한 번 돌린다 |
| 뮤테이션 | D-321 (2) 클립보드는 하나 돌렸다(바깥 참조 `@` 를 빼면 죽음). D-321 (5) 다듬기는 안 돌렸다 |
| 노드 안 값 칸 | 화면 왼쪽 끝에 걸친 노드는 칸이 캔버스 밖이라 서지 않는다 |
| 노드 묶음(하위 그래프) | 없다. 미정 |
| 셰이더 그래프 도메인 | 후처리 11 단계 뒤다(D-322) |
| 컴포넌트 복사 | D-167 대로 에디터 안 스냅샷이다. YAML 로 옮기지 않았다 |

## 이 브랜치에서 걸린 것

- **결정 번호가 main 과 겹친다.** 이번에 D-307·D-308·D-309·D-313 이 main 의 다른 결정과 겹쳐 D-322·D-323·D-324·D-321 로 옮겼다.
  번호를 새로 쓰기 전과 합치기 직전마다 `git show main:tasks/todo.md | grep -oE "^- \*\*D-[0-9]+\."` 로 확인한다. 코드 주석의 번호도 같이 옮긴다.
- **워크트리는 `tasks/*.md` 를 CRLF 로 받는다.** `ScriptCompilerParser` 스위트가 "section 13 has a code block" 로 죽고 전체 시험이 거기서 멈춘다.
  `sed -i 's/\r$//' tasks/jbroscript-syntax.md` 로 LF 로 바꾸고 돌린 뒤 `git checkout -- tasks/jbroscript-syntax.md` 로 되돌린다.
- **시험 빌드 출력의 `Localization` 은 저절로 갱신되지 않는다.** 새 키가 영어로 보이면 `cp Localization/*.yaml Build/x64/Debug/Localization/`.
- **숨은 창의 에디터는 UI 안의 클립보드를 쓴다**(`EditorUI::UsePrivateClipboard`). 시험이 사용자 클립보드를 덮지 않는다.
- **노드 그래프 통합 시험에서 같은 노드를 이어 누르면 두 번 누르기가 된다.** 노드 인스펙터가 포커스를 가져가 단축키가 그래프 창에 안 간다. 두 번 누르기 시간(30 프레임)을 넘기고 누른다.
- **오른쪽 아래는 미니맵이다.** 시험에서 빈 곳을 누를 때 피한다.

## 시험 돌리는 법

```bash
cd source/JBroEngine
MSBuild Tests/JBroTests.vcxproj -p:Configuration=Debug -p:Platform=x64 -m:1 -p:CL_MPCount=4
JBRO_TESTS=NodeGraph,NodeGraphView,CanvasFile ./Build/x64/Debug/JBroTests.exe
```

에디터 통합 시험 하나만 돌릴 때는 `RunEditorApplicationTests` 머리에 임시 훅을 걸고, 커밋 전에 뗀다(CLAUDE.md 의 임시 시험 코드 규칙).
`JBRO_EDITOR_SHOT=<폴더>` 를 주면 `editor_node_graph.bmp`·`editor_node_graph_polish.bmp` 를 찍는다.

이 문서를 읽고 나면 브랜치를 받아 후처리 1 단계(RHI 바인딩 레이아웃·상수 버퍼·업로드 링·비인덱스 그리기·포맷)를 시작할 수 있어야 한다.
