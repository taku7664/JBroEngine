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
