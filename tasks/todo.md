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
- [ ] `main` checkout 후 `vulkan` merge
- [ ] 병합 상태 확인

## Verification
- [ ] `git status --short --branch`
- [ ] `git log --oneline --decorate -5`

## Review
(작성 예정)

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
