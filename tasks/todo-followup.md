# TODO — ECS→OOP 후속 정리 + 방향성 재정렬

> 선행 작업(ECS→다형성 전환 Phase A~G)은 `tasks/todo.md` 참조. 이 문서는 그 위의 후속.

## Goal

ECS 잔재 제거 + Entity→Object 용어 통일 + 멀티 컴포넌트/스크립트 지원 + System 엔진전용 단일화.
사용자가 원하는 엔진 방향성으로 코어를 재정렬한다.

## 확정된 설계 결정 (사용자 합의)

1. **ObjectId**: 도입 보류. 객체 참조는 SafePtr 로 통일(에디터 포함). ID 해싱맵 안 만듦.
2. **EntityId/ECS 잔재**: 전면 폐지.
3. **용어**: Entity → Object 통일.
4. **CComponent 베이스**: 유지. 라이프사이클 OnCreate/OnDestroy 2개만(OnUpdate/OnFixedUpdate/OnStart 제거). System 이 엔진 컴포넌트 구동.
5. **컴포넌트 SafePtr/ControlBlock**: 유지(외부 스크립트 안정성). lazy 할당 최적화 보류.
6. **멀티 컴포넌트/스크립트**: object 당 같은 타입 N개 허용. AddComponent 중복차단 제거 + GetComponents<T> 추가.
7. **컴포넌트 InstanceGuid**: 추가. Ref 가 특정 컴포넌트 1개 지목 가능해야 함. (object guid 는 이미 존재)
8. **Ref 저장 = (object guid + 컴포넌트 guid) 쌍**. 컴포넌트 전역맵 회피 — object 로 좁히고 그 안에서 컴포넌트 guid 선형탐색.
9. **System = 엔진 전용**. 개발자 확장은 스크립트로만. 범용 AddSystem/FindSystem/m_systems 폐지, Scene 이 모든 System 고정 멤버 소유 + 외부 의존성 주입.

### "가벼운 객체" 정의 (사용자)
class 동작/힙관리 없는 데이터홀더. guid·POD 필드 보유 OK. vtable(OnCreate/OnDestroy)도 OK — 순회 시 타입 명확하므로 가상디스패치 안 탐.

### System lazy 등록의 진짜 이유 (코드 검증됨)
리소스 폭발 회피 아님(그건 PreloadReferencedAssets 가 따로 함). Sprite/Audio System 이 EngineCore 소유 디바이스(RenderScene/RHIDevice/AudioDevice/AssetManager)에 의존하는데, Scene 생성 시점엔 없어서 외부가 사후 주입하느라 lazy. → 고정멤버 + 주입으로 더 깔끔.

## Success Criteria
- [ ] ECS 디렉터리/헤더 흔적 0 (Engine 본체 + SDK 스테이징 + targets)
- [ ] 코드 전역 Entity 용어 0 (Object/ObjectId 통일)
- [ ] object 에 같은 타입 컴포넌트 2개 → 둘 다 생존, Ref 가 각각 지목
- [ ] object 에 스크립트 2개 → 둘 다 OnUpdate
- [ ] System 등록 경로 단일(고정 멤버), FindSystem null 함정 제거
- [ ] Editor + Game DLL 양쪽 빌드 통과, 멀티플랫폼(Win/Web) 병렬 유지
- [ ] 씬 저장/로드 라운드트립: 멀티 컴포넌트 + 컴포넌트 guid 보존

## Plan

### Phase A — ECS 잔재 제거 (저위험, 선행) ✅ 완료 (Debug_Editor+Debug_Game x64 빌드 통과)
- [x] StageSDK.targets ECS 글로브 줄 삭제
- [x] SDK/Include/GameFramework/ECS/ 죽은 헤더 삭제 (EntityManager.h, ComponentPool.h) — EntityTypes.h 는 B 까지 보존
- [x] SDK/Include/GameFramework/Component/TransformHierarchy2D.h 삭제
- [x] EntityTypes.h 비트팩 함수 삭제 (별칭+INVALID 만 임시 유지, B 에서 폐기)
- [x] web_game_sources.txt 죽은 .cpp 줄 제거 (EntityManager.cpp, GameObjectComponent.cpp)
- [x] ScriptSystem.h 죽은 EntityTypes include 제거
- [ ] GameFrameworkAll.h ECS include 제거 — B 에서 EntityTypes 폐기와 함께 (지금 제거하면 EntityId 별칭 끊김)

### Phase B — Entity→Object 용어 통일 (광범위 mechanical)
> 범위: EntityId/INVALID_ENTITY_ID 토큰 ~210곳, 23파일(거의 전부 에디터/커맨드/직렬화).
> ⚠ 레이어 위반 발견: Core/Renderer/Forward2DRenderer.h 가 GameFramework/ECS/EntityTypes.h include(Core→GameFramework 역전).
>   RenderFiltered(unordered_set<EntityId>) 는 "렌더 객체 id 필터" 만 필요 → Core 자체 타입으로 독립시킬 것(GameFramework 의존 끊기).
> 별칭 위치 결정: ObjectId(=불투명 uint64) → SceneTypes.h(GameFramework 공개). Core 렌더러는 별도 RenderObjectId(uint64) 자체 보유.
> EntityTypes.h(Engine+SDK 사본) 는 이 단계에서 완전 폐기. vcxproj/filters 의 ECS 필터+EntityTypes.h 등록도 제거.
- [x] EntityId 별칭 → ObjectId, SceneTypes.h 로 이주(불투명 uint64, GetId 기반) + INVALID_ENTITY_ID→INVALID_OBJECT_ID
- [x] Core 레이어 역전 해소: Forward2DRenderer(RenderObjectId) + Core/Debug 6파일(DebugObjectId) GameFramework 의존 끊음
- [x] EntityTypes.h 완전 폐기(Engine 본체 + SDK 사본) + ECS 폴더 삭제 + vcxproj/filters ECS 필터·등록 제거
- [x] PrefabTypes: TargetEntity→TargetObject
- [x] ReflectionTypes: EReflectPropertyType::EntityId 죽은 enum값 제거(Ref 로 대체됨) + InspectorTool 죽은 case 제거. CanAddToEntity→CanAddToObject 는 미처리(빌드 무관, 잔여 명명 후속)
- [x] Forward2DRenderer::RenderFiltered<EntityId>→RenderObjectId
- [x] 죽은 stale SDK 사본 정리(SceneSnapshot.h)
- [~] 잔여 변수/필드 명명(m_sceneViewSelectedEntities, RenderItem.Entity, snapshot.Entity, CanAddToEntity 등) = 빌드 무관 지역명, 후속 미세작업으로 분리

### Phase C — 멀티 컴포넌트/스크립트 + 컴포넌트 guid
- [ ] CComponent 에 File::Guid InstanceGuid 추가 + 생성 시 GenerateGuid
- [ ] CComponent 가상함수: OnUpdate/OnFixedUpdate/OnStart 제거, OnCreate/OnDestroy 유지
- [ ] AddComponent 중복차단 제거(Scene.h) — 항상 새 인스턴스
- [ ] GameObject: GetComponents<T>(전부) 추가, GetComponent<T>(첫개) 유지
- [ ] OnCreate/OnDestroy 배선: AddComponent→OnCreate, DestroyComponent→OnDestroy
- [ ] RefBase 에 컴포넌트 guid 슬롯 추가(object+component 쌍) — POD 유지
- [ ] Ref.cpp ResolveComponent: object 좁힌 뒤 컴포넌트 guid 매치
- [ ] Ref.cpp ResolveScript: "object 당 단일" 가정 제거, 스크립트 guid 로 특정

### Phase D — System 엔진전용 단일화
- [ ] Scene 에 m_spriteSystem/m_audioSystem 고정 멤버 추가
- [ ] 범용 AddSystem/FindSystem/m_systems 폐지
- [ ] GetSpriteSystem()/GetAudioSystem() 접근자 추가
- [ ] Application.cpp/RootDockWindow.cpp/AssetHandler.cpp 의 FindSystem→AddSystem→SetDeps 보일러플레이트(3곳) → GetSystem→SetDeps 축소
- [ ] UpdateSystems 순서 명시 고정(transform→physics→sprite→audio→script)

### Phase E — 직렬화 대응
- [ ] SceneSerializer: 컴포넌트 InstanceGuid 저장/복원
- [ ] Ref 직렬화: object+component guid 쌍 저장/복원
- [ ] 멀티 컴포넌트 라운드트립 검증

## Verification
- [ ] Editor 빌드 + Game DLL 빌드 통과
- [ ] 멀티 컴포넌트 추가/삭제/Ref지목 수동 시나리오
- [ ] 씬 저장→로드 라운드트립(멀티 컴포넌트 포함)
- [ ] Web 빌드 소스목록(web_game_sources.txt) ECS 참조 제거 확인
- [ ] grep: Entity 잔존 0, ECS 잔존 0

## Risks
- Phase B = 광범위, SDK 스테이징 동기 + 줄끝 일관성 주의.
- Phase C(Ref guid 쌍) = 직렬화 포맷 변경 → 기존 씬 파일 호환. 마이그레이션/버전 처리 필요할 수 있음.
- Phase C OnCreate/OnDestroy 배선 = RigidBody 등이 이 훅에 의존 시 물리월드 등록/해제 타이밍 영향.
- DLL 경계: RefBase 확장(component guid) 시 POD 레이아웃 유지 필수(memory-pod-dll-boundary 룰).
- 멀티플랫폼: Win 구현마다 Web 병렬 확인.

## Review
### Changed
### Verified
### Not Verified
### Risks
