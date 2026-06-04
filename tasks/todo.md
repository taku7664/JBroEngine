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
