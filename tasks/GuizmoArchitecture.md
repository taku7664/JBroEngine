# Guizmo Architecture Handoff

## Goal

에디터 SceneView에서 직접 구현한 Guizmo를 제공한다.

1차 목표는 2D transform guizmo를 실사용 수준으로 만드는 것이다.
3D guizmo는 지금 완성하지 않고, 2D와 같은 편집 계약을 공유할 수 있도록 구조만 열어 둔다.

## Core Decision

Guizmo는 Engine runtime 기능이 아니라 Editor SceneView 편집 계층이다.

따라서 위치는 다음이 맞다.

```txt
Application/Editor/Main/Guizmo/
```

Guizmo가 직접 들어가면 안 되는 위치:

- `Engine/Core/Renderer`
  - 이유: Guizmo는 에디터 입력, 선택, undo, SceneView camera에 의존한다.
- `Engine/GameFramework`
  - 이유: 런타임 gameplay 시스템이 아니다.
- `SceneViewTool.cpp` 대형 inline 구현
  - 이유: 현재 SceneView는 이미 선택, 피킹, collider 편집, camera, render target 표시가 섞여 있다. Guizmo까지 직접 넣으면 유지보수가 어렵다.

SceneViewTool은 Guizmo의 owner/view host 역할만 한다.
Guizmo 상태, hit-test, drawing, interaction은 별도 클래스로 분리한다.

## Layer Placement

```txt
Editor UI Layer
└─ SceneViewTool
   ├─ SceneView camera / viewport rect / render target display
   ├─ selection sync
   ├─ object picking / box selection host
   └─ CEditorGuizmoController 호출

Editor Guizmo Layer
└─ Application/Editor/Main/Guizmo
   ├─ CEditorGuizmoController
   ├─ CGuizmo2D
   ├─ CGuizmo3D placeholder
   ├─ GuizmoDraw
   ├─ GuizmoHitTest
   ├─ GuizmoInteraction
   └─ GuizmoTypes

Editor Command Layer
└─ EditorCommandManager / transform edit commands

Runtime Layer
└─ CGameObject / Transform2D / future Transform3D
```

## Ownership

### SceneViewTool

Responsibilities:

- SceneView viewport rectangle 전달
- screen ↔ world 변환에 필요한 camera 값 전달
- 현재 선택 목록 전달
- mouse/input 상태 전달
- Guizmo가 input을 소비했는지 받아서 picking/box selection을 막음
- draw list 또는 overlay drawing target 제공

SceneViewTool이 하지 말아야 할 것:

- handle별 hit-test 직접 구현
- drag 상태 직접 보유
- transform 계산 직접 수행
- undo command 직접 중복 생성

### CEditorGuizmoController

Responsibilities:

- 현재 guizmo mode 관리
  - Translate
  - Rotate
  - Scale
  - future Universal
- 현재 coordinate space 관리
  - World
  - Local
- 현재 pivot mode 관리
  - SelectionCenter
  - ActiveObject
  - Individual, future
- 2D/3D guizmo 선택
- frame update 순서 조정
- input consume 결과 반환

초기에는 2D만 실제 구현한다.
3D는 interface와 stub만 둔다.

### CGuizmo2D

Responsibilities:

- 선택된 2D object의 pivot 계산
- 2D handle geometry 생성
- handle hit-test
- drag begin/update/end
- move/rotate/scale delta 계산
- snap 적용
- undo transaction 생성

2D Guizmo는 `Transform2D`와 `SceneTransformUtils` 기준으로 동작한다.
부모가 있는 object는 world delta를 local transform delta로 변환해야 한다.

### CGuizmo3D

Initial status:

- 구현하지 않는다.
- 다음 interface만 유지한다.

Responsibilities planned:

- 3D camera ray 생성
- axis/plane hit-test
- world/local 3D transform delta 계산
- depth-aware overlay rendering
- future Vulkan/D3D/WebGPU compatible debug draw path

3D는 지금 2D 구현에 끌려오면 안 된다.
단, 2D와 같은 mode/space/pivot/undo 계약을 공유하도록 interface를 맞춘다.

## Suggested File Layout

```txt
Application/Editor/Main/Guizmo/
  GuizmoTypes.h
  EditorGuizmoController.h
  EditorGuizmoController.cpp
  Guizmo2D.h
  Guizmo2D.cpp
  Guizmo3D.h
  Guizmo3D.cpp
  GuizmoDraw.h
  GuizmoDraw.cpp
  GuizmoHitTest.h
  GuizmoHitTest.cpp
  GuizmoInteraction.h
  GuizmoInteraction.cpp
```

Do not create all files blindly if the first implementation is smaller.
However, avoid putting all logic into `SceneViewTool.cpp`.

Recommended initial minimum:

```txt
GuizmoTypes.h
EditorGuizmoController.h/.cpp
Guizmo2D.h/.cpp
Guizmo3D.h/.cpp
```

`GuizmoDraw` and `GuizmoHitTest` can be split out once Move/Rotate/Scale logic starts to grow.

## Data Model

```cpp
enum class EGuizmoDimension
{
	TwoD,
	ThreeD
};

enum class EGuizmoMode
{
	Translate,
	Rotate,
	Scale
};

enum class EGuizmoSpace
{
	World,
	Local
};

enum class EGuizmoPivot
{
	SelectionCenter,
	ActiveObject
};

enum class EGuizmoHandle2D
{
	None,
	MoveX,
	MoveY,
	MoveXY,
	Rotate,
	ScaleX,
	ScaleY,
	ScaleXY
};
```

Frame context should be value-like and transient.
Do not store raw SceneView frame state globally.

```cpp
struct GuizmoFrameContext
{
	ImRect ViewportRect;
	ImDrawList* DrawList = nullptr;
	Vector2 CameraPosition;
	float CameraSize = 1.0f;
	float PixelsPerUnit = 100.0f;
	std::vector<CGameObject*> Selection;
	CGameObject* ActiveObject = nullptr;
	bool IsSceneViewHovered = false;
};
```

## Render Layer

2D Guizmo rendering should initially use ImGui overlay draw list.

Reason:

- Guizmo is an editor overlay, not a runtime render object.
- It must appear over the SceneView render target.
- It should not enter `IRenderScene` or affect game render order.
- Hit-test is screen-space friendly.

Initial rendering path:

```txt
SceneViewTool::OnRenderStay
  Draw scene render target image
  Draw selection overlays
  CEditorGuizmoController::UpdateAndDraw(context)
```

Future 3D rendering may need engine/RHI overlay rendering, but do not start there.
For 3D structure, keep a renderer interface that can later choose ImGui overlay or RHI line draw.

## Input Layer

Guizmo input belongs inside SceneView interaction order.

Recommended order:

1. SceneView viewport visible/hovered check
2. Guizmo hover/hit-test
3. Guizmo active drag update
4. If Guizmo consumed input, stop object picking/box selection/camera drag
5. If not consumed, continue existing SceneView interactions

Guizmo should return:

```cpp
struct GuizmoFrameResult
{
	bool ConsumedMouse = false;
	bool IsActive = false;
	bool ChangedTransform = false;
};
```

Important rule:

- Do not add a global Tick hook just for Guizmo.
- Guizmo updates only when SceneView is rendering/updating.
- Per-frame work is acceptable only while SceneView exists; expensive object scans should be bounded by current selection.

## Coordinate Conversion

SceneView already has screen/world conversion logic.
Guizmo should centralize this into a reusable helper instead of duplicating lambdas inside `SceneViewTool.cpp`.

Needed helpers:

```txt
WorldToScreen2D(world, viewport, camera)
ScreenToWorld2D(screen, viewport, camera)
ScreenDeltaToWorldDelta2D(delta, viewport, camera)
```

These helpers should live with SceneView/Guizmo editor code, not Engine runtime, unless later reused broadly.

## Interaction Model

Guizmo must be stateful only during active drag.

```txt
Idle
  └─ hover handle
Dragging
  ├─ capture selected object initial transforms
  ├─ calculate delta from mouse movement
  ├─ preview apply every frame
  └─ on release, commit one undo command/transaction
Cancelled
  └─ restore captured transforms
```

Undo rule:

- Drag must not create one command per frame.
- Begin drag captures initial transform values.
- During drag, preview modifies objects.
- End drag commits one transaction.
- Escape/right-click cancel should restore initial transforms.

This matches the existing editor transform edit direction: continuous edits must coalesce into one undo entry.

## Selection And Multi-Select

For multi-select:

- Pivot defaults to selection center.
- Translation applies the same world delta to all selected objects.
- Rotation/scale should be around pivot.
- Parent/child selection needs filtering or careful delta application.

Important counterexample:

- If both parent and child are selected and both receive the same world delta naively, the child may effectively move twice through parent transform propagation.

Recommended policy:

- Reuse or create a shared selected-parent filtering helper.
- Apply transform edits only to top-level selected objects unless a mode explicitly says otherwise.

## 2D Feature Scope

### Phase 1: Translate 2D

Must include:

- X axis handle
- Y axis handle
- center/free move handle
- screen-space hit-test
- drag preview
- one undo transaction
- multi-select support
- parent-child selected filtering

This is the first shippable unit.

### Phase 2: Rotate 2D

Must include:

- circular handle
- angle delta from pivot
- snap angle option
- local/world space behavior check
- one undo transaction

### Phase 3: Scale 2D

Must include:

- X scale handle
- Y scale handle
- uniform scale handle
- negative scale policy
- minimum scale clamp policy
- parent transform counterexamples

## 3D Structure Scope

Do now:

- define `IGuizmo`-like interface or controller dispatch shape
- add `CGuizmo3D` stub
- keep mode/space/pivot enum compatible

Do not do now:

- 3D ray picking implementation
- 3D transform component contract
- RHI depth-aware draw
- full 3D mesh handles

3D will require separate design once the 3D camera, Transform3D, and renderer contracts are stable.

## Integration Points

Expected integration files:

```txt
Application/Editor/Main/SceneView/SceneViewTool.h
Application/Editor/Main/SceneView/SceneViewTool.cpp
Application/Editor/Main/SceneView/SceneViewEditContext.*
Application/Editor/Command/*
Application/Editor/Main/Guizmo/*
```

Potential project files:

```txt
Application/Application.vcxproj
Application/Application.vcxproj.filters
```

If new files are added, project files must be updated.

## Styling And UX

2D visual defaults:

- X axis: red
- Y axis: green
- center/free move: yellow or white
- hover: brighter color
- active: high contrast color
- handle size: screen-space constant

Handles must keep stable pixel size regardless of zoom.
World geometry should adapt to camera zoom only where it represents actual world transform.

## Risks

### Risk: SceneViewTool grows further

Mitigation:

- SceneViewTool only hosts Guizmo.
- Guizmo calculations move to `Application/Editor/Main/Guizmo`.

### Risk: Undo spam

Mitigation:

- one drag = one transaction.
- preview changes during drag must be coalesced.

### Risk: Parent-child selected object double transform

Mitigation:

- top-level selected object filter.
- document and test parent-child selection behavior.

### Risk: 2D and 3D abstractions over-generalized too early

Mitigation:

- keep common enums/context/result.
- keep math implementation separate: `CGuizmo2D` and `CGuizmo3D`.
- do not force 3D math into 2D implementation.

### Risk: Runtime/editor boundary leak

Mitigation:

- Guizmo remains under `Application/Editor`.
- Runtime renderer does not know Guizmo.
- No Game build dependency on Guizmo files.

## Verification Plan

Build:

```txt
Debug_Editor|x64
Debug_Game|x64
```

Behavior:

- SceneView object selection still works when Guizmo is idle.
- Hovering a handle blocks object picking only when appropriate.
- Dragging move handle updates transform visually.
- Releasing drag creates one undo entry.
- Undo restores all selected objects.
- Redo reapplies all selected objects.
- Parent+child selected case does not double-apply transform.
- Game build does not include editor Guizmo files.

Web/mobile:

- Guizmo itself is editor-only.
- Do not add platform-specific runtime dependency.
- Keep renderer/runtime APIs unchanged unless a later 3D overlay renderer explicitly needs a shared debug draw contract.

## Recommended First Implementation

Start with 2D Translate only.

Implementation order:

1. Add `GuizmoTypes.h`.
2. Add `CEditorGuizmoController`.
3. Add `CGuizmo2D` with Translate handles.
4. Integrate SceneViewTool as host.
5. Add one drag transaction using existing editor command flow.
6. Verify multi-select and parent-child selection.
7. Add Rotate and Scale after Translate is reliable.

Do not start with 3D beyond stub structure.
Do not start with a generic all-mode abstraction before 2D Translate works.
