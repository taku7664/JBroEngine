# TODO — Forward2DRenderer constant buffer 병목 제거

## Goal
스프라이트 draw마다 constant buffer를 생성하는 렌더 병목을 제거하고, D3D11/WebGPU/Vulkan 병렬 RHI 기준으로 안전한 재사용 구조를 만든다.

## Assumptions
- 단일 constant buffer를 draw마다 덮어쓰는 방식은 Vulkan/WebGPU에서 같은 프레임 내 이전 draw가 마지막 상수값을 읽는 반례가 있다.
- 렌더러는 RHI frame 시작 이후, render pass들보다 먼저 frame-local 자원 cursor를 리셋할 수 있다.

## Success Criteria
- `Forward2DRenderer`의 per-sprite `CreateBuffer` 호출이 제거된다.
- `RenderImpl`, `RenderFiltered`, `FillViewportColor`가 frame-local constant buffer pool을 공유한다.
- WebGPU도 `UpdateBuffer`를 구현해 공통 renderer path를 지원한다.
- D3D11/WebGPU/Vulkan의 동작 차이를 문서/보고에 명시한다.

## Plan
- [x] 현재 per-draw buffer 생성 경로 확인
- [x] 렌더러 frame begin hook 추가
- [x] sprite constant buffer pool 적용
- [x] WebGPU `UpdateBuffer` 구현
- [x] 렌더 병목 후보 검토 정리
- [x] 빌드/정적 검증

## Verification
- [x] `rg "CreateBuffer\\(.*constant|CreateBuffer\\(.*cb" Engine/Core/Renderer/Forward2DRenderer.cpp`
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: `RenderImpl`, `RenderFiltered`, `FillViewportColor`가 draw마다 constant buffer를 `CreateBuffer`로 생성했다.
- 생각했고: 단일 reusable constant buffer는 D3D11에서는 가능성이 있지만 Vulkan/WebGPU에서는 같은 프레임 내 draw들이 마지막 업데이트 값을 읽을 수 있다.
- 반례를 찾았고: Vulkan/WebGPU command recording은 buffer binding이 데이터 snapshot이 아니므로 같은 buffer overwrite는 안전하지 않다.
- 고쳤다: `IRenderer::BeginFrame`에서 frame-local cursor를 리셋하고, draw마다 pool의 서로 다른 constant buffer를 업데이트/바인딩하도록 변경했다.
- WebGPU는 기존에 `UpdateBuffer`가 없어 공통 renderer path가 깨지므로 `wgpuQueueWriteBuffer` 기반 override를 추가했다.
- 남은 병목 후보: WebGPU per-draw bind group 생성, Vulkan per-draw descriptor set allocate/update, sprite batching/instancing 부재, 다중 pass에서 반복 sort/state bind.
- Web 빌드는 `C:\Users\박주형\Desktop\Project\Project.Jproject`가 현재 존재하지 않아 확인하지 못했다.

---

# TODO — RHI 병렬 개발 문서화 및 main 머지

## Goal
RHI/Renderer 공통 개발 원칙을 문서로 남기고 `vulkan` 브랜치를 `main`에 병합한다.

## Assumptions
- 현재 작업 브랜치는 `vulkan`이다.
- "main에 머지"는 `vulkan`의 현재 커밋을 `main` 브랜치로 병합한다는 뜻이다.

## Success Criteria
- D3D11/WebGPU/Vulkan 병렬 개발 기준이 md 문서에 기록된다.
- 문서 변경이 커밋된다.
- `main` 브랜치가 `vulkan` 변경을 포함한다.
- 병합 후 git 상태가 깨끗하다.

## Plan
- [x] RHI/Renderer 공통 개발 문서 작성
- [x] 문서 변경 커밋
- [x] `main` checkout 후 `vulkan` merge
- [x] 병합 상태 확인

## Verification
- [x] `git status --short --branch`
- [x] `git log --oneline --decorate -5`

## Review
- RHI/Renderer 병렬 개발 원칙은 `tasks/RhiRendererParallelDevelopment.md`에 별도 문서로 기록했다.
- `vulkan` 브랜치의 Vulkan 기반 작업과 공통 개발 문서가 `main`에 병합됐다.
- 병합은 충돌 없이 완료됐다.

---

# TODO — 정수 ID 전면 제거 (ObjectId → SafePtr/void*/Guid)

## Goal
ECS 잔재 정수 식별자(`ObjectId`/`GetId()`/`FindObjectById`) 전면 제거.
- 에디터 선택/픽킹/커맨드 → `SafePtr<CGameObject>` (undo 내구성 필요분 `InstanceGuid`)
- 렌더/디버그 외곽선 태그 → `const void*` (역참조 안 함, 프레임 한정 비교 키, Core 레이어)
- 핫리로드 스냅샷 → `InstanceGuid`

## Success Criteria
- `ObjectId`/`INVALID_OBJECT_ID`/`GetId()`/`FindObjectById` 0개(렌더 태그 alias 제외).
- Debug_Editor + Debug_Game + Sample GameScript.dll green.

## Plan
- [ ] S1 Core 태그: RendererTypes/DebugDraw2D `=const void*`, 생산/소비 포인터 키
- [ ] S2 GameFramework: SceneTypes ObjectId 제거, GameObject::GetId 제거, Scene::FindObjectById 제거(FindByInstanceGuid 유지)
- [ ] S3 ImEditor: 포커스셋 const void*
- [ ] S4 SceneDebugDrawSystem 태그 포인터화
- [ ] S5 LiveCompile snapshot.Entity → InstanceGuid
- [ ] S6 Editor.h 선택 SafePtr<CGameObject>
- [ ] S7 EditorSceneCommands 식별 InstanceGuid/SafePtr
- [ ] S8 EditorDragDrop 페이로드 CGameObject*
- [ ] S9 소비자 Hierarchy/Inspector/SceneViewTool/EditContext/Contour/GuiHelpers
- [ ] S10 빌드 green

## Verification
- [ ] grep 잔존 0 / 3빌드 green

## Review
(작성 예정)
