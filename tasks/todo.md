# TODO — ECS → 다형성 Scene-Object-Component 전환

## Goal

sparse-set ECS(EntityId+ComponentPool)를 다형성 Scene→Object→Component 구조로 전환.
- GameObject/Component는 타입별 청크 풀(10/청크, 순회 지원)에 거주, 외부 공유는 SafePtr.
- Transform/계층은 GameObject 멤버(컴포넌트 아님).
- 직렬화는 리플렉션 구동 유지(SceneSnapshot 제거).
- Utillity 불가침. refactor 브랜치.

## Success Criteria

- Debug(에디터)/Debug_Game(런타임) x64 컴파일 0에러.
- 오브젝트/컴포넌트 CRUD, 계층, 씬 저장↔재로드 동일성, 시뮬 Play/Stop, 스크립트 핫리로드 필드/Ref 유지.
- 파괴 후 잔여 SafePtr 안전(null). 다청크(>10) 순회 정상.

## Plan

- [x] 0. refactor 브랜치 생성 (main 파생)
- [x] A. `TObjectPool<T>` 신규 — `Engine/GameFramework/Object/ObjectPool.h` (청크 10, 점유 순회, ControlBlock 바인딩/no-op deleter/수동 해제). Utillity 무수정
- [x] B. 코어 타입:
  - `Component/Component.h` (CComponent 베이스, IsEnabled/Owner, GetTypeName 가상, JBRO_COMPONENT 매크로)
  - `Object/GameObject.h`,`.cpp` (CGameObject 실체: Name/Active/Layer/Guid + Transform2D/WorldTransform2D 멤버 + Parent/Children SafePtr + Components SafePtr, 계층/파괴)
  - `Scene/Scene.h`,`.cpp` (풀 소유, AddComponent/RemoveComponent/GetComponent(object), ForEach<T>=타입풀, ForEachObject, FindByInstanceGuid, DestroyComponent, Update 패스). EntityId 미사용
- [x] C. 컴포넌트 전환 완료: 10종 `class: public CComponent`(IsEnabled 베이스), 옛 GameObject struct/TransformHierarchy2D 스텁화, BuiltinComponentRegistry(GameObject/Transform/Hierarchy 등록·IsEnabled prop 제거), ReflectionRegistry(EntityId→CGameObject&, AddNew/RemoveSpecific/GetAllAddresses 제거)
- [x] 리뷰픽스: F1(지연파괴 큐, SafePtr 기반)·F3(Allocate static_assert) 적용. F4(블록 new 정상) 정정 수용
- [x] D. 시스템 5종 완료: Transform/Script/SpriteRender/Audio/**Physics2DSystem**. SceneTransformUtils→GetWorldTransform(const CGameObject&)=World 캐시 읽기. GameCamera도 전환. (⚠ Physics는 ENOSPC로 파일 0바이트化→git 복원 후 편집 전량 재적용함)
- [ ] E. Ref 해석(guid→SafePtr<CGameObject>→GetComponent) / SceneManager / GameScript(Bind: entity→object) 브리지
- [ ] F. 직렬화 제네릭화(SceneSnapshot 삭제, Transform 객체노드 직접기록) + 에디터(Inspector/Hierarchy/SceneView/Commands/AssetHandler/GuiHelpers)
- [ ] G. 프로젝트 파일(vcxproj/filters)에 신규 파일 등록 + 빌드·런타임 검증 (에디터→게임→웹)

## Verification

- [ ] msbuild Debug, Debug_Game (x64) 0에러
- [ ] 에디터 런타임 시나리오 통과
- [ ] Debug_Game 저장 씬 구동
- [ ] diff 리뷰

## Review

### Changed

### Verified

### Not Verified

### Risks
