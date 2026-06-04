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
