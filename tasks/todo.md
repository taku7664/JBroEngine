# TODO — BuildGame Pack Writer Contract Parity

## Goal
`BuildGame.ps1`의 C# asset pack writer를 엔진 C++ pack writer의 runtime index v2 계약과 맞춘다.

## Assumptions
- 웹/스크립트 빌드도 같은 `.jbpack` runtime index 계약을 써야 한다.
- `Read-JBroMeta`의 `Importer`는 asset type 판정에는 계속 필요하지만 pack index에는 쓰지 않는다.
- PowerShell 세션에 이전 C# type이 로드된 반례를 피하기 위해 writer/entry type 이름을 v2로 분리한다.

## Success Criteria
- `BuildGame.ps1` pack writer가 index version 2를 쓴다.
- script-side pack index에 `Importer`, `SourceExtension`이 기록되지 않는다.
- script-side pack writer type name이 stale loaded type과 충돌하지 않는다.

## Plan
- [x] `BuildGame.ps1` embedded C# pack writer 계약 수정
- [x] C++/PowerShell pack index field 순서 재대조
- [x] 문서/todo 갱신
- [ ] diff/build or script-level 검증 및 커밋

## Verification
- [x] `rg`로 script pack writer의 removed fields 확인
- [x] `git diff --check`
- [x] embedded C# pack writer `Add-Type` compile
- [ ] `Debug_Game|x64` build: not rerun for script-only parity change; previous asset pack contract build passed.

## Review
- 코드를 읽었고: `BuildGame.ps1`의 embedded `JBroPackWriter`가 아직 index version 1과 `Importer`/`SourceExtension`을 쓰는 것을 확인했다.
- 생각했고: C++ writer만 v2로 바꾸면 Windows editor package와 script/web package의 `.jbpack` 계약이 갈라진다.
- 반례를 찾았고: PowerShell 장기 세션에 이전 C# type이 로드되어 있으면 같은 type 이름 재정의가 실패하거나 stale type을 계속 쓸 수 있다.
- 고쳤다: script writer/entry type을 `JBroPackWriterV2`/`JBroPackEntryV2`로 바꾸고, index version 2와 C++ writer와 같은 field 순서로 맞췄다.
- 검증했다: removed field 잔존 검색과 embedded C# `Add-Type` compile을 통과했다.

---

# TODO — Asset Pack Release Contract Hardening

## Goal
남은 감사 목록 1번의 첫 단계로 release/runtime pack 계약에서 디스크 materialization과 source/debug metadata 노출을 제거한다.

## Assumptions
- 이번 단계는 package index 계약을 먼저 hardened runtime record로 줄인다.
- cooked payload 전환은 importer별 payload 포맷 계약이 필요하므로 다음 커밋 단위로 진행한다.
- `ImportOptionsYaml`은 현재 Sprite PPU/frame과 Audio mode 생성에 쓰이는 runtime-required metadata라 cooked payload 전환 전까지 유지한다.
- 에디터 개발 호환성보다 release/runtime 보호 계약을 우선한다.

## Success Criteria
- `CAssetPackReader::MaterializePayload()` 기본 경로가 사라진다.
- packed runtime load는 memory payload를 지원하는 asset loader만 허용한다.
- pack index에 `Importer`, `SourceExtension` 같은 debug/source metadata를 저장하지 않는다.
- 사용되지 않는 debug-name package flag가 제거된다.
- `ImportOptionsYaml`은 cooked payload 전환 전까지 runtime-required metadata로만 남긴다.
- SDK public mirror가 같은 asset pack record 계약을 가진다.

## Plan
- [x] pack record serialization/deserialization 계약 확인
- [x] source/debug metadata 필드 제거 및 SDK mirror 동기화
- [x] `.packcache` materialization 제거
- [x] packed runtime memory-loader 실패 처리를 명시화
- [x] audit/follow-up 문서 갱신
- [ ] diff/build 검증 및 커밋

## Verification
- [x] `rg`로 `MaterializePayload`, `.packcache`, source metadata field 잔존 확인
- [x] `git diff --check`
- [x] `Debug_Game|x64` build
- [ ] `Debug_Editor|x64` build 가능 여부 확인: skipped because `Build/Debug_Editor/Application.exe` is held by running PID 24848.

## Review
- 코드를 읽었고: `AssetPackage` index writer/reader, `CAssetManager::LoadPackedAssetManifest()`, `LoadAssetInternal()`, Sprite/File/Audio/AudioEffect loader memory path를 확인했다.
- 생각했고: default `.packcache` materialization은 resource protection 목표와 충돌하므로 release/runtime 기본 경로에서 제거해야 한다고 판단했다.
- 반례를 찾았고: Audio streaming은 memory payload만으로 현재 load할 수 없지만, 이를 파일로 풀어 해결하면 pack 보호 계약이 깨진다.
- 고쳤다: `MaterializePayload()`와 cache root를 제거하고, packed payload가 있는데 loader가 memory load를 못 하면 명시적 실패 로그를 내게 했다.
- 추가로 읽었고: runtime index의 `Importer`, `SourceExtension`, `DebugNamePresent`는 현재 loader 동작에 필요 없고 debug/source 정보에 가깝다.
- 고쳤다: 해당 필드/flag를 제거하고 index version을 2로 올렸으며, v1 pack은 제거 필드를 읽어서 버리는 호환 경로를 남겼다.
- 남겼다: `ImportOptionsYaml`은 Sprite PPU/frame과 Audio mode에 필요하므로 cooked payload 전환 전까지 runtime-required metadata로 유지했다.

---

# TODO — Engine Audit Dead-Code Cleanup

## Goal
엔진 전체 감사에서 확인된 데드/스테일 코드 중 현재 호출 계약상 제거 가능한 항목을 먼저 정리하고, 남은 작업을 실행 순서 기준으로 다시 정리한다.

## Assumptions
- 이번 1차 수정은 확정된 dead/stale 코드 제거에 한정한다.
- 런타임 호환성, pack materialization, raw-source cook 전환처럼 정책 판단이 필요한 항목은 남은 작업으로 재분류한다.
- `Build/` 산출물 변경은 이번 커밋 범위에 포함하지 않는다.

## Success Criteria
- `BuildAssetPackage()`의 stale JSON manifest writer가 제거된다.
- 사용되지 않는 `AssetPackageBuildDesc::OutputManifestPath` 계약이 제거된다.
- SDK public mirror도 같은 계약으로 동기화된다.
- 남은 audit finding은 우선순위/분류가 명확한 follow-up 문서로 정리된다.

## Plan
- [x] `BuildAssetPackage()` 호출자와 desc 계약 재확인
- [x] stale JSON manifest writer 및 죽은 manifest path 필드 제거
- [x] Engine/SDK mirror 동기화
- [x] 남은 audit 작업 재분류 문서화
- [x] diff/build 검증 및 커밋

## Verification
- [x] `rg`로 `OutputManifestPath` 잔존 확인
- [x] `git diff --check`
- [ ] `Debug_Editor|x64` build: source compile completed, but final link failed because `Build/Debug_Editor/Application.exe` is held by running PID 24848.
- [x] `Debug_Game|x64` build

## Review
- 코드를 읽었고: `BuildAssetPackage()`와 현재 유일 호출자인 `CGameBuildManager::StagePackage()`를 확인했다.
- 생각했고: JSON sidecar writer만 제거하면 `OutputManifestPath`라는 죽은 계약이 남아 다시 분기/오해를 만들 수 있다고 판단했다.
- 반례를 찾았고: 별도 manifest path를 쓰는 호출자가 있는지 `rg`로 전체 소스/SDK/스크립트 범위를 확인했지만 현재 소스 호출자는 없었다.
- 고쳤다: stale JSON writer와 `AssetPackageBuildDesc::OutputManifestPath`를 제거하고, Engine/SDK mirror와 호출부를 `OutputBlobPath` 단일 계약으로 맞췄다.
- 남은 작업을 정리했다: `tasks/EngineAuditFollowup.md`에 해결 항목과 후속 작업 우선순위를 분리했다.

---

# TODO — Engine-wide Code Audit

## Goal
엔진 전체 소스에서 데드코드, 이상한 구조, 버그 가능 코드, 최적화 후보를 찾아 근거와 함께 md 문서로 정리한다.

## Assumptions
- 검토 대상은 `Application/`, `Engine/`, `SDK/Include/`, `BuildScripts/`, `SampleProject/`, `Samples/`이다.
- `Build/`, `.git/`, `Engine/ThirdParty/`는 산출물/외부 소스라 제외한다.
- 이번 작업은 수정이 아니라 발견/분류/권장 작업 문서화가 목표다.

## Success Criteria
- 각 finding은 파일/라인 근거를 가진다.
- severity, category, impact, recommendation을 적는다.
- 불확실한 항목은 추정으로 명시하고 과장하지 않는다.
- 문서는 처음 보는 사람이 다음 작업 우선순위를 잡을 수 있게 작성한다.

## Plan
- [x] 전체 파일/모듈 구조 스캔
- [x] 정적 패턴 검색으로 위험 후보 수집
- [x] 후보별 주변 코드 읽고 반례 확인
- [x] `tasks/EngineCodeAudit.md` 작성
- [x] 문서 검토 및 커밋

## Verification
- [x] `rg` 기반 정적 검색 수행
- [x] 주요 후보 파일 직접 열람
- [x] `git diff --check`

## Review
- 코드를 읽었고: `Application/`, `Engine/`, `SDK/Include/`, `BuildScripts/`, `SampleProject/`, `Samples/`에서 build/package/runtime/render/asset/editor 후보를 `rg`로 먼저 모았다.
- 생각했고: 이번 감사의 핵심은 단순 dead code보다 빌드 산출 계약, pack 보호 계약, RHI별 병렬성, editor/runtime 분리의 drift 여부라고 판단했다.
- 반례를 찾았고: `RenderScene::Sort()` 반복 호출처럼 겉보기엔 의심스럽지만 `m_needsSort` guard가 있는 항목은 낮은 우선순위로 내렸다.
- 문서화했다: `tasks/EngineCodeAudit.md`에 severity/category/impact/recommendation과 확인한 반례를 분리해 기록했다.

---

# TODO — SceneView Translate Arrow Line Trim

## Goal
Translate Guizmo의 축 선이 화살표 삼각형 안으로 튀어나와 보이지 않게 정리한다.

## Assumptions
- 문제는 선을 화살표 tip까지 그려서 삼각형 내부에 선이 겹쳐 보이는 것이다.
- hit-test 범위는 기존 축 전체 길이를 유지한다.
- draw만 화살표 밑변까지 줄인다.

## Success Criteria
- Translate X/Y 축 선이 화살표 밑변에서 끝난다.
- 화살표 tip은 삼각형만 차지한다.
- Local/World 축 방향과 기존 hit-test 동작은 유지된다.

## Plan
- [x] Translate draw line end를 arrow base로 조정
- [x] shadow line도 같은 기준으로 조정
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: Translate draw는 축 선을 `pivotScreen -> xEnd/yEnd`까지 그린 뒤, 같은 `xEnd/yEnd`를 화살표 tip으로 삼아 삼각형을 덮어 그리고 있었다.
- 생각했고: 이 구조에서는 선이 화살표 삼각형 내부까지 들어가므로, 이미지처럼 화살표 중앙에서 선이 삐져나온 것처럼 보일 수 있다.
- 반례를 찾았고: hit-test 선분까지 줄이면 사용자가 화살표 근처 축을 잡는 감도가 줄어든다.
- 고쳤다: hit-test는 기존 축 전체 길이를 유지하고, draw line과 shadow line만 `tip - axisDir * ARROW_SIZE` 위치에서 끝나도록 잘랐다.

---

# TODO — SceneView Guizmo Drag Follow / Scale Line Hit-Test

## Goal
Translate Guizmo를 드래그하는 동안 Guizmo가 preview된 object position을 따라가고, Scale Guizmo는 사각형 handle뿐 아니라 축/대각선 선분도 상호작용한다.

## Assumptions
- "Position Guizmo"는 현재 `Translate` mode를 의미한다.
- Translate drag 중 실제 transform preview는 기존처럼 start transform + world delta로 계산한다.
- Guizmo 표시 위치만 start pivot 고정에서 current pivot preview로 바꾼다.
- Scale line hit-test는 기존 X/Y/Uniform handle 의미를 그대로 사용한다.

## Success Criteria
- Translate X/Y/XY drag 중 Guizmo pivot과 축이 오브젝트 이동을 따라간다.
- Translate undo/redo transaction 구조는 기존과 동일하다.
- Scale mode에서 X/Y/Uniform 사각형뿐 아니라 연결 선을 눌러도 해당 scale drag가 시작된다.
- Scale 사각형 handle 우선순위는 기존처럼 유지된다.

## Plan
- [x] Translate drag current pivot 저장 추가
- [x] Translate drag draw 기준을 current pivot으로 변경
- [x] Scale line segment hit-test 추가
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: Translate drag 중 preview transform은 매 프레임 갱신되지만, Guizmo draw는 계속 `m_dragStartWorld`를 기준으로 했다.
- 생각했고: TransformSystem이 돌기 전에는 `GetWorldTransform()`이 stale일 수 있으므로, preview된 object를 다시 스캔해 pivot을 계산하는 것보다 drag start pivot + constrained delta가 더 직접적이고 안정적이다.
- 반례를 찾았고: Local X/Y 축 drag는 constrained delta만큼만 이동하므로 raw mouse delta를 쓰면 Guizmo 표시 위치가 실제 object preview와 어긋난다.
- 고쳤다: `m_dragCurrentWorld`를 추가하고, Translate drag에서 constrained delta 적용 후 `m_dragStartWorld + constrainedDelta`로 갱신해 그 위치에 Guizmo를 그리게 했다.
- 추가로 읽었고: Scale hit-test는 사각형 handle `SquareRect()`만 검사하고, 실제로 그려지는 X/Y/Uniform 선분은 검사하지 않았다.
- 생각했고: 시각적으로 선이 handle의 일부로 보이므로 선분도 같은 ScaleX/Y/XY drag를 시작해야 한다.
- 반례를 찾았고: 선분 검사를 먼저 하면 사각형 handle 근처에서 의도한 handle 우선순위가 흐려질 수 있다.
- 고쳤다: 기존 사각형 hit-test를 먼저 유지하고, miss일 때만 `DistanceToSegmentSq()`로 X/Y/Uniform 선분 hit-test를 수행했다.

---

# TODO — SceneView 2D Local Translate Space

## Goal
SceneView Guizmo에서 World/Local 공간을 전환할 수 있고, Translate X/Y handle이 Local 공간에서는 active object의 world 방향축을 따른다.

## Assumptions
- 우선 Local space는 Translate에만 적용한다.
- Local 축 기준은 active object의 world transform basis를 사용한다.
- active object가 없거나 basis 길이가 0에 가까우면 World 축으로 fallback한다.
- drag 중 object transform이 변해도 시작 시점의 축 기준을 유지한다.

## Success Criteria
- SceneView overlay에서 World/Local space를 전환할 수 있다.
- World space Translate는 기존처럼 화면상 world X/Y 축을 따른다.
- Local space Translate는 active object 회전 방향의 X/Y 축으로 handle을 그리고, drag delta도 해당 축으로 projection한다.
- toolbar hover/click은 기존 selection/collider input을 막는다.
- release 후 undo/redo 1회로 시작/끝 transform을 복원한다.

## Plan
- [x] 2D translate axis basis 계산 추가
- [x] Translate hit-test/draw/drag constraint에 basis 적용
- [x] SceneView toolbar에 World/Local space toggle 추가
- [x] 문서 review 갱신
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: `EGuizmoSpace`는 controller에 있었지만 SceneView에서 바꿀 UI가 없고, `CGuizmo2D::UpdateAndDraw()`도 space 인자를 무시하고 있었다.
- 생각했고: Local/World state만 노출하고 실제 handle 동작이 바뀌지 않으면 사용자가 신뢰할 수 없는 토글이 되므로, 우선 Translate X/Y handle의 draw/hit-test/drag constraint를 같은 basis로 묶어야 했다.
- 반례를 찾았고: idle draw/hit-test에서 drag 시작 좌표를 재사용하면 이전 drag 상태나 원점에 의존해 handle 방향이 틀어질 수 있다.
- 고쳤다: `pivotWorld`를 draw/hit-test에 명시적으로 넘겨 현재 프레임 pivot 기준으로 screen-space axis 방향을 계산했다.
- 추가로 읽었고: `Matrix3x2`의 x/y basis는 `(M11, M12)`와 `(M21, M22)`이고, scale이 섞이면 길이가 달라진다.
- 생각했고: handle 길이는 screen-space constant여야 하므로 basis는 방향만 정규화해서 쓰는 것이 맞다.
- 반례를 찾았고: active object가 없거나 scale 0에 가까운 basis면 Local 축이 정의되지 않는다.
- 고쳤다: Local space는 active object world basis를 정규화해 사용하고, basis가 유효하지 않으면 World 축으로 fallback했다.
- 추가 반례: drag 중 transform이 바뀌면 local axis도 계속 바뀌어 mouse projection이 흔들릴 수 있다.
- 고쳤다: Translate drag 시작 시 `m_dragAxisX/Y`를 저장하고, drag 중 preview와 draw는 저장된 축을 사용하도록 했다.

---

# TODO — SceneView 2D Rotate Snap

## Goal
SceneView Rotate Guizmo에서 Shift 드래그 시 15도 단위로 회전 delta를 스냅한다.

## Assumptions
- 별도 설정 UI 없이 우선 Shift modifier를 snap trigger로 사용한다.
- 스냅은 absolute rotation이 아니라 drag 시작점 대비 delta에만 적용한다.
- undo/redo transaction 구조는 기존 Rotate drag와 동일하게 유지한다.

## Success Criteria
- Shift 없이 Rotate drag는 기존처럼 연속 회전한다.
- Shift를 누른 상태의 Rotate drag는 15도 단위 delta만 적용한다.
- Shift를 drag 중에 누르거나 떼도 preview는 같은 drag transaction 안에서 안정적으로 갱신된다.
- release 후 undo/redo 1회로 시작/끝 transform을 복원한다.

## Plan
- [x] Rotate delta snap helper 추가
- [x] UpdateRotateDrag에 Shift snap 적용
- [x] 문서 review 갱신
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: Rotate drag는 mouse angle과 drag start angle의 차이를 그대로 `RotateObjectAroundPivot()`에 넘기고 있었다.
- 생각했고: snap은 absolute rotation에 적용하면 드래그 시작 시 object가 튈 수 있으므로, drag 시작점 대비 delta에만 적용해야 한다.
- 반례를 찾았고: snap을 별도 command나 설정 저장으로 만들면 Guizmo 기본 편집 단계에 비해 범위가 커지고, editor/runtime 경계와 저장 정책까지 불필요하게 건드린다.
- 고쳤다: `ImGui::GetIO().KeyShift`가 true인 frame에서만 rotate delta를 15도 단위로 반올림하고, 기존 preview/undo transaction 경로는 그대로 유지했다.

---

# TODO — SceneView 2D Scale Guizmo

## Goal
SceneView Guizmo에서 Scale mode를 선택할 수 있고, 선택 오브젝트를 pivot 기준으로 X/Y/Uniform 스케일 편집한다.

## Assumptions
- Scale은 2D Transform2D의 local Scale 값을 편집한다.
- X/Y handle은 화면/월드축 기준으로 mouse delta를 읽지만, 회전된 오브젝트에 대한 완전한 world-axis non-uniform scale은 Transform2D만으로 shear 없이 표현할 수 없으므로 local Scale 증감으로 제한한다.
- 부모가 있는 object는 pivot 기준으로 계산한 target world position을 parent local position으로 환산한다.
- 음수 scale sign flip은 허용하지 않고, 기존 sign을 보존하며 최소 절대 scale 값으로 clamp한다.

## Success Criteria
- SceneView overlay에서 Scale mode를 선택할 수 있다.
- Scale mode는 X/Y/Uniform handle을 그리고 screen-space hit-test로 drag를 시작한다.
- Scale drag는 선택 top-level object들의 position과 scale을 pivot 기준으로 preview 갱신한다.
- release 후 undo/redo 1회로 시작/끝 transform을 복원한다.
- scale 값은 0 또는 sign flip으로 붕괴하지 않는다.

## Plan
- [x] SceneView Guizmo mode toolbar에 Scale 추가
- [x] Scale handle draw/hit-test 추가
- [x] Scale drag preview/commit 구현
- [x] 음수 scale sign 보존 및 minimum clamp 적용
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: Translate/Rotate는 SceneViewTool이 mode toolbar와 overlay block만 담당하고, 실제 hit-test/drag/undo는 `CGuizmo2D`가 담당하는 구조였다.
- 생각했고: Scale도 같은 host/controller 계약을 따라야 SceneView 선택, collider editing, undo 흐름과 충돌하지 않는다.
- 반례를 찾았고: Scale toolbar 클릭이 기존 SceneView picking이나 collider input으로 흘러가면 mode 변경과 선택/편집이 동시에 발생할 수 있다.
- 고쳤다: SceneView mode toolbar에 `S` 버튼만 추가하고 기존 `modeToolbarHovered -> overlayBlocked` 차단 경로를 그대로 타게 했다.
- 추가로 읽었고: Transform2D는 position/rotation/scale만 표현하므로 회전된 object에 대한 완전한 world-axis non-uniform scale은 shear 없이 표현할 수 없다.
- 생각했고: 여기서 억지로 world-axis scale을 가장하면 부모/회전 조합에서 결과가 불명확해지므로, handle은 월드/화면 기준 입력을 쓰되 실제 값은 local Scale 증감으로 제한하는 것이 현재 Transform2D 계약에 맞다.
- 반례를 찾았고: pivot을 지나 드래그하면 scale factor가 음수가 되어 sprite flip이나 0 scale 붕괴가 발생할 수 있다.
- 고쳤다: sign flip은 허용하지 않고 기존 scale sign을 보존하며 `0.001` minimum absolute scale로 clamp했다.
- 추가 반례: 부모가 있는 object의 position을 world target 그대로 local에 넣으면 부모 transform 아래에서 위치가 틀어진다.
- 고쳤다: Scale preview에서도 pivot 기준 target world position을 계산한 뒤 `WorldPositionToLocalPosition()`으로 parent local position에 환산했다.

---

# TODO — SceneView 2D Rotate Guizmo

## Goal
SceneView Guizmo에서 Translate와 Rotate mode를 선택할 수 있고, Rotate mode에서 선택 오브젝트를 pivot 기준으로 회전 편집한다.

## Assumptions
- Scale은 다음 단계로 미루고 이번에는 Translate/Rotate만 노출한다.
- Rotate는 기본 `SelectionCenter` pivot 기준으로 동작한다.
- 부모가 있는 object는 회전 후 world position을 parent local position으로 환산한다.
- 드래그 중 preview 후 release 때 old/new transform 스냅샷 커맨드 1개만 기록한다.

## Success Criteria
- SceneView overlay에서 Translate/Rotate mode를 전환할 수 있다.
- Rotate mode는 circular handle을 그리고 screen-space ring hit-test로 drag를 시작한다.
- 회전 drag는 선택 top-level object들의 position과 rotation을 pivot 기준으로 preview 갱신한다.
- release 후 undo/redo 1회로 시작/끝 transform을 복원한다.
- mode toolbar hover/click은 SceneView picking, box selection, collider editing을 막는다.

## Plan
- [x] SceneView Guizmo mode toolbar 추가
- [x] toolbar 영역 입력 차단을 SceneView/Collider/Guizmo context에 반영
- [x] Rotate handle draw/hit-test 추가
- [x] Rotate drag preview/commit 구현
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: Translate Guizmo는 구현되어 있었지만 controller mode를 바꿀 UI가 없어 Rotate를 추가해도 접근할 수 없었다.
- 생각했고: 기능을 숨겨 둔 채 구현만 하는 것은 사용 가능한 단계가 아니므로, SceneView overlay에 Translate/Rotate mode toolbar가 먼저 필요했다.
- 반례를 찾았고: toolbar 위 클릭이 기존 SceneView picking이나 collider vertex editing으로도 흘러가면 mode 변경과 선택/편집이 동시에 발생한다.
- 고쳤다: mode toolbar hover를 `overlayBlocked`에 포함하고, collider editing, Guizmo hit-test, SceneView click state machine이 같은 차단 값을 쓰도록 했다.
- 추가로 읽었고: Transform world cache는 `TransformSystem`이 갱신하는 값이라 drag preview 중 object world가 즉시 최신이라는 보장이 없다.
- 생각했고: Rotate drag는 preview 중 `object.GetWorld()`에 의존하지 말고, 드래그 시작 local transform과 parent world만으로 target local position을 계산해야 한다.
- 반례를 찾았고: 부모가 있는 object를 selection pivot 기준으로 회전할 때, world target position을 그대로 local position에 넣으면 부모 회전/스케일 아래에서 위치가 틀어진다.
- 고쳤다: `RotateObjectAroundPivot()`에서 initial local position을 parent world로 world position화하고, 회전 후 target world position을 parent inverse로 local position에 환산했다.
- 추가 반례: drag 중 controller mode가 바뀌어도 active drag는 시작한 handle 계약을 계속 따라야 한다.
- 고쳤다: `m_activeHandle`이 `Rotate`면 현재 mode와 무관하게 rotate drag update/draw를 계속 타도록 했다.

---

# TODO — SceneView 2D Translate Guizmo

## Goal
SceneView에 에디터 전용 2D Translate Guizmo를 추가한다.

## Assumptions
- Guizmo는 런타임 기능이 아니므로 `Application/Editor/Main/Guizmo/` 아래에 둔다.
- 1차 구현은 Translate만 실제 동작시키고, Rotate/Scale/3D는 구조만 열어 둔다.
- 드래그 중에는 preview로 transform을 직접 갱신하고, release 때 old/new transform 스냅샷 커맨드 1개만 기록한다.
- 부모+자식 동시 선택은 기존 `Editor::GetSelectedTopLevel()` 정책을 사용한다.

## Success Criteria
- 선택된 2D object에 X/Y/XY translate handle이 SceneView overlay로 표시된다.
- handle hover/click/drag가 기존 object picking/box selection을 소비한다.
- 드래그는 화면 공간 handle hit-test와 world delta 변환을 통해 transform을 preview 갱신한다.
- release 후 undo 1회로 드래그 시작 전 transform으로 돌아가고, redo 1회로 끝 transform이 복원된다.
- Game build에는 Guizmo 파일이 포함되지 않는다.

## Plan
- [x] Guizmo 타입/controller/2D/3D stub 파일 추가
- [x] old/new transform 스냅샷 editor command 추가
- [x] SceneViewTool에 Guizmo host 연결
- [x] Application project 파일 동기화
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Editor|x64` build
- [x] `Debug_Game|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: SceneViewTool은 이미 하나의 `InvisibleButton`으로 SceneView 입력을 소유하고, 선택/박스선택/우클릭 팬 상태 머신을 그 아이템 상태에 묶어 처리하고 있었다.
- 생각했고: Guizmo가 별도 Tick이나 별도 런타임 renderer path를 만들면 기존 SceneView 입력 계약과 editor/runtime 경계를 깨므로, SceneViewTool이 frame context만 넘기고 Guizmo가 소비 결과를 반환하는 구조가 맞다.
- 반례를 찾았고: handle 클릭이 기존 좌클릭 상태 머신에도 전달되면 object picking 또는 box selection이 동시에 실행될 수 있다.
- 고쳤다: `CEditorGuizmoController`/`CGuizmo2D`를 `Application/Editor/Main/Guizmo`에 추가하고, `GuizmoFrameResult::ConsumedMouse`가 true인 프레임은 SceneView 선택 인텐트를 초기화하도록 했다.
- 추가로 읽었고: Inspector transform command는 모든 대상에 같은 local delta를 적용하는 `CSetObjectTransformCommand`를 사용한다.
- 생각했고: Guizmo translate는 world-space delta가 기본이고, 서로 다른 부모를 가진 다중 선택은 대상별 local delta가 달라질 수 있다.
- 반례를 찾았고: 부모가 회전/스케일된 object에 같은 local delta를 적용하면 화면상 같은 world 이동이 되지 않는다.
- 고쳤다: Guizmo release 시 old/new local transform 스냅샷을 한 번만 커밋하는 `CSetObjectTransformsCommand`를 추가했다.
- 추가 반례: 문서 기본 pivot은 selection center인데 active object를 우선하면 다중 선택 pivot이 사용자의 기대와 달라진다.
- 고쳤다: controller 기본 pivot을 `SelectionCenter`로 두고, active object pivot은 명시 모드일 때만 사용하도록 했다.
- 검증 중 첫 `Debug_Editor` 링크는 실행 중인 `Build/Debug_Editor/Application.exe` 잠금으로 실패했다.
- 고쳤다: 해당 프로세스만 종료한 뒤 재빌드했고, `Debug_Editor|x64`와 `Debug_Game|x64`가 통과했다.

---

# TODO — 기본 Sprite instanced batching 구현

## Goal
기본 sprite pipeline을 쓰는 연속 RenderItem을 instance buffer로 묶어 draw call과 constant buffer update 횟수를 줄인다.

## Assumptions
- 커스텀 material/pipeline은 batch 호환성을 알 수 없으므로 기존 per-sprite draw 경로로 유지한다.
- 투명도 순서 보존을 위해 RenderItem을 재정렬하지 않고, 현재 정렬 결과에서 연속된 같은 mesh/texture/sampler 구간만 batch한다.
- Batch shader는 D3D11/HLSL, WebGPU/WGSL, Vulkan/SPIR-V 모두 제공한다.

## Success Criteria
- 기본 sprite pipeline을 사용하는 연속 item은 `DrawIndexedInstanced`를 탄다.
- batch는 view constant buffer 1개와 instance vertex buffer 1개를 사용한다.
- custom pipeline, invalid resource, filtered/excluded item 경계에서는 기존 단일 draw 경로 또는 batch flush가 동작한다.
- D3D11/WebGPU/Vulkan 공통 renderer path를 유지한다.

## Plan
- [x] batch shader/pipeline 추가
- [x] view constant buffer와 sprite instance buffer pool 추가
- [x] RenderImpl/RenderFiltered batching 적용
- [x] SDK mirror 동기화
- [x] 빌드/Web 검증 및 커밋

## Verification
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] Web Release build with `SampleProject/Project.Jproject`
- [x] `git diff --check`

## Review
- 코드를 읽었고: Forward2DRenderer의 기본 sprite draw는 item마다 constant buffer를 갱신하고 `DrawIndexed`를 호출하는 구조였다.
- 생각했고: 이전 단계에서 RHI instanced draw 계약을 만들었으므로, 기본 quad mesh와 같은 texture/sampler를 쓰는 연속 item은 instance buffer로 묶을 수 있다.
- 반례를 찾았고: 투명도 정렬을 바꾸면 blending 결과가 달라질 수 있으므로 재정렬 batch는 하지 않고 현재 정렬 결과의 연속 구간만 묶었다.
- 고쳤다: batch 전용 view constant buffer와 sprite instance buffer pool을 추가하고, D3D11/HLSL, WebGPU/WGSL, Vulkan/SPIR-V batch vertex shader를 추가했다.
- 추가로 읽었고: batch pipeline 생성은 최적화 경로인데 초기화 필수 조건에 넣으면 shader/backend 문제 하나로 기존 단일 sprite 렌더까지 실패할 수 있었다.
- 생각했고: batch는 실패해도 correctness를 깨면 안 되는 선택 경로여야 한다.
- 반례를 찾았고: `DrawSpriteBatch()` 실패 후 batchCount만큼 인덱스를 넘기면 해당 sprite들이 화면에서 사라진다.
- 고쳤다: batch pipeline은 best-effort로 생성하고, batch draw 실패 시 현재 item을 기존 `DrawSpriteItem()` 경로로 그리도록 fallback을 보장했다.
- 추가 반례: SDK public mirror와 engine header가 갈라지면 script/sample 쪽에서 다른 renderer layout을 보게 된다.
- 고쳤다: `SDK/Include/Core/Renderer/Forward2DRenderer.h`와 `SpriteVulkanShaders.h`를 engine source와 동기화했다.

---

# TODO — RenderItem draw resource pre-resolve

## Goal
Sprite render loop에서 material virtual getter 반복 조회를 줄이고, 추후 멀티스레드 렌더링에 맞게 RenderItem이 렌더에 필요한 RHI 리소스를 제출 시점에 들고 있게 한다.

## Assumptions
- 현재 `CRenderMaterial`은 생성 후 pipeline/texture/sampler setter가 없는 immutable resource이다.
- 외부 submitter가 새 필드를 채우지 않아도 기존 `Material` fallback으로 동작해야 한다.
- SDK mirror도 같은 `RenderItem` layout을 가져야 한다.

## Success Criteria
- `SpriteRenderSystem`은 `RenderItem` 제출 시 Pipeline/Texture/Sampler를 함께 채운다.
- `Forward2DRenderer`는 먼저 RenderItem의 pre-resolved resource를 쓰고, 없으면 기존 Material getter로 fallback한다.
- batch run 탐색은 같은 item의 texture/sampler/pipeline을 반복 조회하지 않는다.
- 기존 단일 draw fallback과 custom material fallback은 유지된다.

## Plan
- [x] `RenderItem`에 pre-resolved Pipeline/Texture/Sampler 추가
- [x] `SpriteRenderSystem` 제출 시 draw resource 채우기
- [x] `Forward2DRenderer` resource resolve helper 적용
- [x] SDK mirror 동기화
- [x] 빌드/Web 검증 및 커밋

## Verification
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] Web Release build with `SampleProject/Project.Jproject`
- [x] `git diff --check`

## Review
- 코드를 읽었고: `SpriteRenderSystem`은 `RenderItem` 제출 시 material queue만 해석하고, renderer loop에서 pipeline/texture/sampler를 다시 material virtual getter로 반복 조회했다.
- 생각했고: 렌더 제출 시점에 draw resource를 같이 넣으면 Forward2DRenderer의 batch 탐색과 fallback draw가 같은 item resource를 재조회하지 않아도 된다.
- 반례를 찾았고: 외부 submitter가 새 필드를 채우지 않으면 기존 렌더가 깨질 수 있다.
- 고쳤다: `RenderItem`에 Pipeline/Texture/Sampler를 추가하되, `Forward2DRenderer::ResolveSpriteDrawResources()`가 pre-resolved 값이 비어 있으면 기존 `Material` getter로 fallback하도록 했다.
- 추가로 읽었고: `CRenderMaterial`은 생성자로 받은 pipeline/texture/sampler를 setter 없이 반환하는 immutable resource 형태였다.
- 생각했고: 같은 프레임의 render item에 resource snapshot을 저장하는 것은 현재 material 구조와 맞고, 멀티스레드 render command 준비에도 유리하다.
- 반례를 찾았고: custom material pipeline은 batch 대상이 아니어야 하며, default sprite pipeline만 instancing 대상이어야 한다.
- 고쳤다: batch 가능 판정은 resolved pipeline이 기본 sprite pipeline과 같은 경우로 제한하고, custom pipeline은 기존 단일 draw 경로로 유지했다.
- 검증 중 `Debug_Editor` 첫 빌드는 stale MSBuild node/PDB/PCH 중간 산출물 문제로 실패했다.
- 고쳤다: MSBuild node reuse 프로세스를 종료하고 `Build/Intermediate/Engine/Debug_Editor/x64` 중간 산출물만 정리한 뒤 `/nr:false` 단일 빌드로 통과시켰다.

---

# TODO — Sprite instancing RHI 계약 추가

## Goal
Sprite batching/instancing을 구현하기 위한 공통 RHI 입력 레이아웃과 instanced draw 명령을 추가한다.

## Assumptions
- 실제 sprite batch renderer는 다음 단계에서 구현한다.
- 이번 단계는 D3D11/WebGPU/Vulkan 모두 같은 RHI 계약을 지원하도록 만드는 기반 작업이다.
- 기존 vertex element는 기본값 `InputSlot=0`, `InputRate=PerVertex`로 유지해 기존 파이프라인 동작을 바꾸지 않는다.

## Success Criteria
- `RHIVertexElementDesc`가 input slot과 per-vertex/per-instance rate를 표현한다.
- D3D11/WebGPU/Vulkan pipeline 생성이 slot별 vertex buffer layout을 만든다.
- D3D11/WebGPU/Vulkan command context가 `DrawIndexedInstanced`를 지원한다.
- 기존 sprite pipeline은 기존과 동일하게 컴파일된다.

## Plan
- [x] RHI vertex input contract 확장
- [x] D3D11/WebGPU/Vulkan pipeline input layout 적용
- [x] D3D11/WebGPU/Vulkan instanced indexed draw 추가
- [x] SDK mirror 확인
- [x] 빌드 검증 및 커밋

## Verification
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] Web Release build with `SampleProject/Project.Jproject`
- [x] `git diff --check`

## Review
- 코드를 읽었고: 기존 RHI vertex input은 slot 0/per-vertex로 고정되어 있고, indexed instanced draw API가 없었다.
- 생각했고: sprite batching/instancing을 renderer에 직접 넣기 전에 D3D11/WebGPU/Vulkan 공통 계약이 먼저 필요하다.
- 반례를 찾았고: 한 backend만 임시 구현하면 멀티 플랫폼 렌더러 병렬 개발 규칙을 깨고, Vulkan/WebGPU에서 batch renderer가 막힌다.
- 고쳤다: `RHIVertexElementDesc`에 `InputSlot`/`InputRate`를 추가하고, `IRHICommandContext::DrawIndexedInstanced`를 공통 API로 추가했다.
- 추가로 읽었고: WebGPU는 vertex buffer stride가 `sizeof(float) * 4`로 고정되어 있었다.
- 생각했고: instancing뿐 아니라 다른 vertex layout에서도 잘못된 stride가 될 수 있으므로 slot별 stride 계산이 필요하다.
- 반례를 찾았고: WebGPU/Vulkan은 vertex buffer slot별 layout이 필요하며 slot 0/1을 안정적으로 써야 한다.
- 고쳤다: D3D11/WebGPU/Vulkan pipeline 생성이 element의 input slot/rate를 반영하고 slot별 stride를 계산하도록 변경했다.
- SDK에는 RHI header mirror가 없어 별도 동기화 대상은 없었다.

---

# TODO — RenderScene sort / SpriteRenderSystem 루프 비용 정리

## Goal
멀티 플랫폼 RHI 경계를 흔들지 않는 범위에서 렌더 제출 전 CPU 비용을 먼저 줄인다.

## Assumptions
- Sprite batching/instancing은 RHI vertex input, shader, Vulkan SPIR-V까지 같이 바뀌어야 하므로 별도 큰 단위로 진행한다.
- 현재 RenderScene은 매 프레임 Clear 후 Submit 순서가 이미 정렬되어 있을 수 있다.
- SpriteRenderSystem의 renderer 타입은 한 프레임 루프 안에서 변하지 않는다.

## Success Criteria
- RenderScene은 제출 순서가 이미 Queue/SortOrder 기준 정렬이면 `std::sort`를 호출하지 않는다.
- RenderFiltered/RenderImpl이 같은 scene을 여러 번 렌더해도 이미 정렬된 scene은 재정렬하지 않는다.
- SpriteRenderSystem은 sprite마다 `dynamic_cast<CForward2DRenderer*>`를 반복하지 않는다.
- 렌더 결과 순서 계약은 기존 Queue/SortOrder 기준을 유지한다.

## Plan
- [x] RenderScene incremental dirty sort 추가
- [x] SpriteRenderSystem renderer cast 루프 밖 이동
- [x] 빌드 검증
- [x] 커밋

## Verification
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: `CRenderScene::Sort()`는 렌더 호출마다 무조건 `std::sort`를 수행했고, 같은 scene을 `RenderImpl`/`RenderFiltered`에서 다시 렌더해도 같은 sort를 반복할 수 있었다.
- 생각했고: Clear 후 Submit 되는 순서가 이미 Queue/SortOrder 기준이면 정렬 결과는 동일하므로 `std::sort`를 피할 수 있다.
- 반례를 찾았고: 제출 중간에 낮은 Queue/SortOrder item이 뒤늦게 들어오면 정렬을 유지해야 한다.
- 고쳤다: Submit 시 직전 item과 sort key를 비교해 순서가 깨진 경우만 `m_needsSort`를 세우고, Sort 후에는 dirty를 내린다.
- 추가로 읽었고: `SpriteRenderSystem::OnUpdate()`는 sprite마다 `dynamic_cast<CForward2DRenderer*>`를 반복했다.
- 생각했고: renderer dependency는 한 update 루프 안에서 바뀌지 않으므로 루프 밖에서 한 번만 cast해도 동작이 같다.
- 반례를 찾았고: forward renderer가 없어도 기존 mesh/material이 살아있는 sprite는 submit 가능해야 한다.
- 고쳤다: `forwardRenderer`를 루프 밖에서 캡처하되, 기존 생성 분기 조건만 그대로 사용해 submit 흐름은 유지했다.
- SDK public mirror의 `RenderScene.h`도 같은 private layout으로 동기화했다.

---

# TODO — WebGPU bind group cache lookup 정리

## Goal
WebGPU bind group cache가 많은 sprite에서 선형 검색을 반복하지 않도록 안정적인 draw 순서 기반 cursor를 추가한다.

## Assumptions
- Forward2DRenderer의 draw item 순서는 대체로 프레임 간 안정적이다.
- cache key 비교는 계속 `SafePtr` 비교를 사용한다.
- 순서가 바뀌는 경우에도 correctness가 유지되어야 한다.

## Success Criteria
- 같은 draw 순서에서는 cache hit가 cursor 위치에서 O(1)로 처리된다.
- cache miss 또는 순서 변경 시에도 기존 entry를 SafePtr 비교로 찾거나 새 entry를 cursor 위치에 삽입한다.
- WebGPU cache가 첫 프레임부터 매 draw마다 전체 cache를 선형 검색하지 않는다.

## Plan
- [x] WebGPU bind group cache cursor 추가
- [x] cursor hit/move/insert 흐름 구현
- [x] 빌드 및 Web Release 검증
- [x] 커밋

## Verification
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] Web Release build with `SampleProject/Project.Jproject`
- [x] `git diff --check`

## Review
- 코드를 읽었고: 직전 WebGPU cache는 `std::vector`를 앞에서부터 순회해 같은 binding 조합을 찾는 구조였다.
- 생각했고: bind group 생성/해제는 줄었지만 stable sprite draw order에서도 N개 sprite가 매 프레임 N단계 선형 탐색을 반복할 수 있다.
- 반례를 찾았고: 첫 프레임 cache 생성 중에도 이미 추가된 entry를 계속 훑으면 O(N²) 초기 비용이 생기고, 이후 프레임도 순서가 같아도 앞에서부터 다시 찾는다.
- 고쳤다: 프레임별 cursor를 추가해 같은 draw 순서에서는 cursor 위치에서 즉시 hit하고, 순서가 바뀐 경우에만 SafePtr 비교로 기존 entry를 찾아 cursor 위치로 이동한다.
- 추가 반례: cursor 뒤쪽만 찾으면 같은 프레임에서 이전 resource 조합을 다시 쓸 때 중복 entry가 생길 수 있어, cursor mismatch일 때만 전체 cache를 검색하도록 보정했다.

---

# TODO — WebGPU bind group / Vulkan descriptor pool 병목 정리

## Goal
WebGPU의 draw별 bind group 생성/해제 병목을 줄이고, Vulkan에서 draw 수가 descriptor pool 고정 크기를 넘으면 이후 draw가 조용히 누락되는 위험을 제거한다.

## Assumptions
- Forward2DRenderer의 per-sprite constant buffer pool은 프레임을 넘어 재사용되므로 WebGPU bind group은 같은 pipeline/buffer/texture/sampler 조합에서 재사용할 수 있다.
- WebGPU bind group cache는 RHI 리소스의 `SafePtr` 유효성을 기준으로 정리해야 한다.
- Vulkan descriptor set은 현재 프레임 안에서만 필요하므로 descriptor pool은 프레임마다 reset하되, 필요한 경우 pool을 추가 생성해 draw 누락을 막는다.

## Success Criteria
- WebGPU Draw/DrawIndexed가 매번 `wgpuDeviceCreateBindGroup`/`wgpuBindGroupRelease`를 반복하지 않는다.
- WebGPU cache 비교는 resource raw pointer가 아니라 `SafePtr` 비교를 사용한다.
- Vulkan descriptor allocation이 첫 pool 한계에 닿아도 추가 pool로 재시도한다.
- 기존 D3D11 경로는 영향받지 않는다.

## Plan
- [x] WebGPU bind group cache 추가 및 수명 정리
- [x] Vulkan descriptor pool 추가 생성/프레임 reset 구조 추가
- [x] Debug_Game/Debug_Editor/Web Release 빌드 검증
- [x] diff 검토 및 커밋

## Verification
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] Web Release build with `SampleProject/Project.Jproject`
- [x] `git diff --check`

## Review
- 코드를 읽었고: WebGPU `Draw`/`DrawIndexed`가 매 draw마다 `CreateCurrentBindGroup()`로 bind group을 만들고 draw 직후 release하고 있었다.
- 생각했고: 이전 작업으로 constant buffer가 풀 기반 재사용으로 바뀌었으므로 같은 pipeline/buffer/texture/sampler 조합은 bind group도 재사용할 수 있다.
- 반례를 찾았고: 캐시가 resource raw pointer만 보면 `SafePtr` 정책과 어긋나고, resource가 파괴되면 stale bind group을 잡고 있을 수 있다.
- 고쳤다: WebGPU command context에 `SafePtr` 기반 bind group cache를 추가하고, 프레임 시작/소멸/device 변경 시 invalid entry와 API 객체를 정리한다.
- 추가로 읽었고: Vulkan은 draw마다 descriptor set을 새로 allocate/update하며 pool이 1024 set 고정이었다.
- 생각했고: 지금 단계에서 descriptor set 캐시는 constant buffer가 draw마다 달라 효과가 제한적이고, 먼저 1024 draw 이후 조용히 draw가 빠지는 버그 후보가 더 명확하다.
- 반례를 찾았고: `vkAllocateDescriptorSets` 실패 시 `BindPendingDescriptors()`가 그냥 return하므로 sprite가 많은 장면에서 일부 draw가 descriptor 없이 진행될 수 있다.
- 고쳤다: Vulkan descriptor pool을 프레임 reset 가능한 pool 목록으로 바꾸고, 현재 pool 할당 실패 시 추가 pool을 생성해 재시도한다.
- 검증 중 사용자 프로젝트 경로 `C:\Users\박주형\Desktop\Project\Project.Jproject`는 현재 존재하지 않아 Web 검증은 `SampleProject/Project.Jproject`로 대체했다.

---

# TODO — 렌더 반복 계산/상태 바인딩 병목 정리

## Goal
Forward2DRenderer에서 draw마다 반복되는 카메라 계산과 중복 RHI state bind를 줄인다.

## Assumptions
- RHI command context는 마지막으로 바인딩된 pipeline/mesh/texture/sampler 상태를 유지한다.
- Constant buffer는 draw마다 달라지므로 계속 바인딩해야 한다.
- SafePtr 비교 연산을 추가하고, state cache는 raw pointer가 아니라 SafePtr 비교를 사용한다.

## Success Criteria
- RenderImpl의 view parameter 계산이 item 루프 밖으로 이동한다.
- 같은 pipeline, vertex buffer, index buffer, texture, sampler는 같은 render 호출 내에서 중복 Set 호출을 피한다.
- RenderImpl, RenderFiltered, FillViewportColor가 같은 draw helper를 사용한다.
- D3D11/WebGPU/Vulkan 공통 renderer path를 유지한다.

## Plan
- [x] View parameter helper 추가
- [x] Render state cache/helper 추가
- [x] RenderImpl/RenderFiltered/FillViewportColor 적용
- [x] 정적 검색 및 빌드 검증

## Verification
- [x] `Debug_Game|x64` build
- [x] `Debug_Editor|x64` build
- [x] `git diff --check`

## Review
- 코드를 읽었고: `RenderImpl`은 item마다 view half extent/camera row 재계산을 했고, pipeline/mesh/texture/sampler state도 매 draw마다 무조건 다시 바인딩했다.
- 생각했고: constant buffer는 draw마다 바뀌므로 유지해야 하지만, view parameter 계산과 동일 resource state bind는 render 호출 범위에서 줄일 수 있다.
- 반례를 찾았고: state cache를 raw pointer로 두면 `SafePtr` 의미와 어긋나므로 `SafePtr` 비교 연산을 추가하고 cache도 `SafePtr`로 보관해야 한다.
- 고쳤다: `SafePtr`에 `==`/`!=` 비교를 추가했고, `Forward2DRenderer`는 `ViewParameters`와 `RenderStateCache`를 통해 반복 계산/중복 state bind를 줄인다.
- 추가 반례: mesh는 유효하지만 내부 vertex/index buffer가 죽은 경우 SetBuffer가 무시된 뒤 DrawIndexed가 호출될 수 있어 draw helper에서 선검증한다.
- SDK public mirror의 `SafePtr.h`와 `Forward2DRenderer.h`도 같이 동기화했다.
- 남은 큰 병목 후보는 WebGPU per-draw bind group 생성, Vulkan per-draw descriptor set allocate/update, sprite batching/instancing 부재다.

---

# TODO — 하이라키 오브젝트 순서 안정화

## Goal
오브젝트 추가 시 하이라키 "맨 아래"에 쌓이고, 시뮬레이션 정지 후에도 순서가 유지되도록 안정적인 생성순서 키를 도입한다.

## 근본 원인
하이라키 표시 순서 = `CScene::ForEachObject`(= 풀 슬롯 순회) 순서. 풀 슬롯 순서는 생성순서와 무관하다.
- 신규: `AddChunk` 가 free list 를 slot9→slot0 로 쌓아 할당은 slot9 부터, 순회는 slot0 부터 → 역순. 새 오브젝트가 위로 쌓임.
- 시뮬정지: 스폰/파괴로 슬롯이 free list 에 LIFO 재배치 → 재사용 순서가 원래 위치와 어긋나 순회 순서가 섞임.

## Assumptions
- 자식 순서도 풀 순회 기반(`childrenByParent`)이라 동일 증상 → 같은 키로 해결.
- CreationOrder 는 런타임/직렬화 양쪽에서 순서 기준이 되며, 저장 목록도 같은 키로 정렬해야 reload 후에도 일관.

## Success Criteria
- 새 오브젝트가 하이라키 맨 아래에 추가된다(형제 그룹 내 마지막).
- 시뮬레이션 정지 후 순서 불변.
- 저장→로드 후 순서 보존.

## Plan
- [ ] CGameObject: `std::uint64_t CreationOrder = 0;` 추가(비직렬화 — 로드 시 파일 순서로 재할당)
- [ ] CScene: `m_nextCreationOrder` 카운터, `CreateGameObject` 에서 할당
- [ ] HierarchyTool: root/형제 그룹을 CreationOrder 로 정렬
- [ ] SceneSerializer: 저장 목록을 CreationOrder 로 정렬(reload 일관성)
- [ ] 빌드 Debug_Editor + Debug_Game

## Verification
- [ ] 빌드 양쪽 EXIT 0
- [ ] 동작: 오브젝트 추가 → 맨 아래 / 시뮬 정지 → 순서 유지 (사용자 확인)

## Review

### 하이라키 순서 (CreationOrder)
- 원인: 표시 순서가 풀 슬롯 순회 순서였고, 슬롯 순서는 생성순서와 무관(할당 역순·재사용 섞임).
- 수정: CGameObject.CreationOrder(단조), CreateGameObject 부여, Hierarchy root/형제 정렬, SceneSerializer 저장 정렬.
- 검증: Game 컴파일+링크 클린(exe 생성), Editor 컴파일 클린(링크는 exe 실행중 잠금=코드무관).

### 게임 빌드 실패 2건 (이 세션 추가 발견)
1. Web 링크: `web_game_sources.txt` 에 삭제된 SceneSnapshot.cpp 잔존 + 신규 AudioEffectAsset.cpp 누락
   → SceneSnapshot 제거, AudioEffectAsset 추가. (SceneDebugDrawSystem 은 에디터 전용이라 제외 유지)
2. 에셋팩 실패(Windows+Web 공통): `.jfx`(AudioEffect) 메타가 Type=Unknown 으로 저장돼 패키징 첫 분기 거부.
   근본: AssetMetaFile ToString/ParseType 에 AudioEffect 분기 누락(과거 Audio 와 동일 버그 재발).
   → 두 곳에 AudioEffect 추가. 기존 자가복구(Importer→ParseType)가 디스크의 Type:Unknown 메타도 로드시 회복.
- 검증: Engine.lib Debug_Editor EXIT 0.

### Not Verified
- 실제 게임 빌드(Windows/Web) 재실행 후 통과 여부 — 사용자 확인 필요.
- 하이라키 동작(추가→맨아래 / 시뮬정지→순서유지) 런타임 확인 — 사용자 확인 필요.
