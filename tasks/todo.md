# 신규 리포를 기존 엔진 구조에 맞추기 — TODO

이전 단계 기록은 [canvas-world-foundation.md](./canvas-world-foundation.md) 로 옮겼다.
확정 규칙은 [docs/ProjectRule.md](../docs/ProjectRule.md) 다. 아래 Decisions 가 그 근거다.
워크트리 분기 계획은 [worktree-plan.md](./worktree-plan.md) 에 있다.

## Goal

`source/JBroEngine` 신규 트리를 **기존 엔진(`source/repos/JBroEngine/Engine`)의
오브젝트-컴포넌트 모델**에 맞추고, 스크립트 노출 표면만 핸들로 바꾼다.

신규 트리는 ECS 로 지어져 있었다. 기존 엔진은 ECS 가 아니다.
이 간극을 메우는 것이 이 작업의 전부다.

## Decisions

문서 초안과 다르게 확정한 사항이다. 초안보다 이 절을 우선한다.

### 모델

- **D-1. ECS 를 쓰지 않는다. 오브젝트-컴포넌트 모델이 확정이다.**
  기존 엔진이 이미 이 구조이고 사용자가 그것을 설계 철학으로 정했다.
  `Entity` 정수 ID 없음, 컴포넌트는 다형성, 다중 타입 `Query` 없음.
  시스템은 `ForEach<T>` 로 타입별 컴포넌트 풀을 순회한다.
  **시스템은 `Ref<T>` 를 거치지 않는다** — 순회가 실체 참조를 그대로 주므로 해석 비용이 0 이다.
  순회 중 생성·파괴는 금지한다(live 배열이 흔들린다).
- **D-2. `World` 를 두지 않는다.**
  `Canvas` 가 오브젝트 풀과 타입별 컴포넌트 풀, Layer, 시스템을 직접 소유한다.
  수명 계층은 `Canvas` → `GameObject` 하나뿐이다. `Scene` / `SceneManager` 도 두지 않는다.
- **D-3. Transform 과 부모·자식 계층은 `GameObject` 의 멤버다.** 컴포넌트로 만들지 않는다.
- **D-4. 엔진 내부 참조는 `SafePtr` 를 그대로 쓴다.**
  핸들로 바꾸지 않는다. `Utillity/SafePtr` 는 불가침이다.

### 참조와 식별자

- **D-5. 스크립트에 노출하는 참조는 `Ref<T>` 하나뿐이다.**
  값 타입 핸들 클래스를 따로 만들지 않는다. 오브젝트 · 컴포넌트 · 스크립트 · 에셋 · 캔버스가
  같은 타입을 쓰고 카테고리는 `T` 로부터 컴파일타임에 결정된다.
  `GetComponent<T>()` 도 원시 포인터가 아니라 `Ref<T>` 를 반환한다 —
  원시 포인터는 저장할 수 없어 매 프레임 선형 탐색을 다시 하게 된다.
- **D-6. 스크립트별 핸들 타입을 코드 생성으로 만들지 않는다.**
  생성 전까지 사용자 코드가 컴파일되지 않아 스크립트를 막 작성한 시점에 편집기가 깨진다.
- **D-7. `Ref<T>` 의 접근자는 하나다.**

  ```cpp
  if (T* p = ref.Get()) p->Foo();   // 확인하고 쓴다
  ref->Foo();                        // 확인 안 하고 쓴다 — 무효면 크래시
  ```

  경로가 둘인 게 아니라 **접근자 하나에 사용법이 둘**이다.
  `operator->` 는 내부적으로 `Get()` 을 부르며 Debug assert 만 차이다.
  별도의 스코프 객체 타입은 두지 않는다 — 복사·이동을 삭제해도 C++17 의 보장된 복사 생략
  때문에 멤버 저장이 막히지 않아 강제 효과가 없다.
  `Ref<GameObject>` 는 부분 특수화로 `Destroy()` / `SetActive()` 같은 안전 멤버를 제공하며,
  그 안에서는 `->` 를 쓰지 않는다 — `->` 는 중간에 중단할 자리가 없기 때문이다.
- **D-8. 안전 경로의 무효 접근은 로그 후 무시한다.**
  크래시도 예외도 절반 실행도 없다. 값을 돌려주는 접근만 실패가 드러나게 한다(`TryGetPosition`).
  Safety 초안 §7 의 "런타임 에러" 를 대체한다.
  무검사 경로(`->`)는 이 보장을 하지 않는다. 선택은 사용자가 한다.
- **D-9. 영속 식별자는 `InstanceId`(`uint64` 하나)다.**
  `[42비트 ms][10비트 세션난수][12비트 시퀀스]`. 시간은 프레임당 1회 캐시하므로 생성 비용은
  사실상 `++counter`. 시간순 정렬이 되어 `m_creationOrder` 를 흡수한다.
- **D-10. `Ref<T>` 는 영속 참조 역할을 유지하되 저장부를 정수로 바꾼다.**
  `char Guid[64]×2 = 128B` → `InstanceId ×2 + InstanceHandle = 24B`.
  로드 직후 일괄 패치업해서 프레임 루프의 식별자 조회를 0 회로 만든다.

### 이름과 경계

- **D-11. 네임스페이스로 구분한다. 타입 접두사를 쓰지 않는다.**
  `JBro::Component` / `Asset` / `System` / `Service` / `Internal`, 나머지는 `JBro` 직속.
  `JBro::Game` 은 두지 않는다 — 사용자 코드의 `namespace Game` 과 충돌한다.
  예외 둘: 인터페이스 `I` 접두, private 멤버 `m_`.
  네임스페이스와 같은 이름의 타입은 만들 수 없으므로 컴포넌트 베이스는 `ComponentBase` 다.
- **D-12. `Manager` 명칭을 폐기하고 `System` / `Service` 로 나눈다.**
  System 은 엔진 레이어 로우레벨(업데이트·순회), Service 는 스크립트 레이어 공개 API.
  서비스는 프로세스에 하나만 존재하며 `ServiceContext` 에 **값으로** 담긴다.
- **D-13. 컨텍스트는 셋이다.**
  `EngineContext`(호스트 전부) → `SystemContext`(DLL 은 받되 사용자엔 비공개) /
  `ServiceContext`(사용자 공개). 서비스 헤더는 시스템을 전방 선언만 하고
  실제 호출은 비인라인 `.cpp` 에 둔다.
- **D-14. DLL 경계는 게임 스크립트 하나뿐이다.**
  초안 §13 의 근거(프로세스 간 코드 페이지 공유)는 성립하지 않는다 — Windows 는 동일 EXE
  이미지도 같은 방식으로 공유한다. DLL 의 실질 가치는 핫 리로드이고 그것만 취한다.
  RHI 는 정적으로 시작하고 두 번째 Windows 백엔드가 생기면 승격한다.
- **D-15. 2D/3D 배타성은 엔진 빌드가 아니라 사용자 스크립트 프로젝트와 게임 익스포트에만 적용한다.**
  엔진·에디터는 둘 다 포함한다(엔진 1회 설치). 2D 게임 실행 파일에 3D 코드는 안 들어간다.
- **D-16. 디바이스 로스트는 치명적 오류로 처리하고 종료한다.**
  대신 RHI 디바이스 포인터를 그래픽스 계층 밖으로 내보내지 않아 나중 복구 작업을 국소화한다.
- **D-17. `GraphicsSystem` 을 폐기하고 `Renderer` 가 디바이스 부착을 흡수한다.**
  순회도 사용자 접근도 없어 System 도 Service 도 아니다.

## Assumptions

- 대상은 `Documents/GitHub/JBroEngine` 신규 리포다. 기존 엔진은 **읽기 전용 기준**으로만 쓴다.
- 기초가 서면 기존 엔진을 이 구조로 마이그레이션한다. 그래서 기존 구조를 기준으로 삼는다.
- Windows / D3D12 를 먼저 세운다. Vulkan / WebGPU / Android 는 모듈 규칙만 유지한 채 뒤로 미룬다.

## Findings — 신규 리포 vs 기존 엔진

기존 엔진 구조(읽어서 확인한 것):

```
CGameCanvas
 ├─ TObjectPool<CGameObject> m_objectPool        청크 32슬롯 · 주소 불변 · ForEachObject
 ├─ m_componentPools  정렬 [TypeKey → TObjectPool<T>]   ForEach<T>
 ├─ m_layers          CGameLayer
 └─ 시스템 소유       AddSystem<TSystem>

CGameObject : GameInstance, EnableSafeFromThis
 ├─ Transform2D Local / WorldTransform2D World          ← 멤버
 ├─ SafePtr<CGameLayer> m_layer                          ← GetLayerIndex() O(1)
 ├─ SafePtr<CGameObject> m_parent / m_children           ← 계층도 멤버
 └─ vector<SafePtr<CComponent>> m_components             ← 논리 소유

GameInstance : File::Guid m_instanceGuid + Guid128 m_instanceGuid128
```

| # | 항목 | 기존 엔진 | 신규 리포 | 대응 |
|---|---|---|---|---|
| F1 | 중간 계층 | 없음 (Canvas 직접) | `CWorld` | B1 |
| F2 | 오브젝트 식별 | 객체 자체 / InstanceGuid | `Entity` uint32 | B2 |
| F3 | 컴포넌트 소유 | 오브젝트가 `vector<SafePtr>` | 저장소 분리 | B3 |
| F4 | Transform | **오브젝트 멤버** | `Transform2DComponent` | B4 |
| F5 | 컴포넌트 성격 | **다형성** + `GetTypeName()` 가상 | POD `struct` | B5 |
| F6 | 계층 | 오브젝트 멤버 | `HierarchyComponent` | B4 |
| F7 | 레이어 소속 | 오브젝트가 `SafePtr<CGameLayer>`, O(1) | Canvas 의 `unordered_map<Entity,LayerId>` | B6 |
| F8 | 순회 | `ForEach<T>` / `ForEachObject` | `Query<A,B>` | B7 |
| F9 | ComponentTypeId | `MakeStableTypeId(T::StaticTypeName())` | 매직넘버 `0x2001` | B8 |
| F10 | 활성 게이트 | `IsActiveComponent()` **단일 게이트** | 없음 | B9 |
| F11 | 오브젝트 속성 | `Tag` / `Flags`(EditorHidden) / `m_creationOrder` | 없음 | B10 |
| F12 | 멀티 컴포넌트 | 같은 타입 여러 개, guid 로 구분 | 타입당 1개 | B11 |
| F13 | 안정 식별자 | `File::Guid` + `Guid128` | `AssetId{uint64}` 뿐 | F 단계 |
| F14 | 참조 시스템 | `Ref<T>` 5카테고리 + `SafePtr` | 없음 | F·G 단계 |

**F9 는 신규가 명백히 열등하다.** 기존은 이름에서 컴파일타임에 안정 ID 를 뽑는데
신규는 손으로 배정한다. 기존 방식을 가져오면 충돌 검증 자체가 필요 없어진다.

**F10 은 놓치면 안 된다.** 기존 코드 주석에 *"시스템별로 `owner->IsActive` 를 제각각
판단하던 불일치를 없애기 위함"* 이라고 적혀 있다. 이미 겪은 버그다.

### 신규 리포 자체 결함 (기존 엔진과 무관)

- F15. `JBroRuntime` ↔ `JBroFramework` 상호 의존. → Stage A 에서 모듈 흡수로 해소 완료.
- F16. 공통 모듈이 차원에 오염됐다. `GameSystem::ExtractRender(CWorld&, RenderWorld2D&)`.
- F17. `JBroPlatform` 이 `GraphicsApi`/`SurfaceHandle` 때문에 `JBroRHI` 를 include 한다.
- F18. `IPlatform::CreateWindow` / `DestroyWindow` 가 Win32 매크로와 충돌한다.
  `WindowsPlatform.cpp` 가 `Windows.h` 를 include 하는 순간 깨진다.
- F19. 합성 루트가 둘이다. `EngineInstance` 와 `EditorApplication` 이 같은 서비스 집합을
  각자 멤버로 들고 있어 수명 소유자가 애매하다.
- F20. `Physics2DSystem` 이 System 과 Service 역할을 겸한다.
  `OnFixedUpdate`(순회) + `Raycast`/`OverlapBox`(스크립트 질의).
- F21. `GameScript` 가 차원에 묶여 있다(`OnCollisionEnter(const Collision2D&)`).
  이대로면 Framework3D 가 스크립트 수명 주기를 통째로 다시 만들어야 한다.
- F22. `TComponentPool` 은 청크가 이동하지 않아 주소가 안정적이고
  이미 `TestComponentAddressesStayStable` 로 검증된다.
  문제는 **그 계약이 어디에도 문서화돼 있지 않다**는 점이다.

## Success Criteria

- 신규 트리에 `Entity` 정수 ID, `CWorld`, `Query<A,B>`, POD 컴포넌트가 남아 있지 않다.
- `Canvas` 가 오브젝트 풀과 타입별 컴포넌트 풀을 직접 소유한다.
- 시스템이 `ForEach<T>` 로 컴포넌트 풀을 순회한다.
- 네임스페이스가 §10.1 표대로 적용되고 타입 접두사가 없다(`I` / `m_` 제외).
- 스크립트가 `Ref<T>` 만 보고 실객체(`CGameObject*`)나 원시 컴포넌트 포인터를 보지 않는다.
- `Ref<GameObject>` 안전 멤버를 `if` 없이 호출해도 크래시가 없고 로그가 남는다.
- `Ref<T>` 가 24B 이고 프레임 루프에서 식별자 조회가 0 회다.
- 게임 스크립트 DLL 을 재로드해도 호스트가 살아 있고 참조가 복구된다.
- 모듈 간 역방향 include 0 건, 2D 스크립트 타깃에서 Framework3D include 시 컴파일 실패.
- 테스트가 Debug / Release x64 양쪽에서 통과한다.

## Plan

우선순위는 **B0 → B → C → (D · E · F) → G → H** 다.
B0 는 선언만 만드는 덧붙이기라 먼저 하고, B·C 는 전 파일을 건드려 나눌 수 없다.
워크트리 분기는 **C 까지 끝난 뒤**에 가능하다([worktree-plan.md](./worktree-plan.md)).

### Stage A. 빌드 단위 분리 — **완료**

- [x] A1. 공통 property sheet `JBro.Common.props`
- [x] A2. 모듈 10 개 vcxproj 분리 (전부 StaticLibrary)
- [x] A3. 헤더를 `Include/JBro/<이름>/` 로 재배치, 참조를 `<JBro/<이름>/...>` 로 정리
- [x] A4. 모듈 의존을 include 경로 + 프로젝트 참조로 명시
- [x] A5. `JBroTests` exe 추가, 기존 `Tests/*.cpp` 편입
- [x] A6. slnx x86 구성 제거
- [x] A7. `JBroFramework` 를 `JBroRuntime` 에 흡수 (F15 해소)
- [x] A8. `Jbro` → `JBro` 케이싱 통일

### Stage B0. 골격 선언 — **최우선**

**구현이 아니라 선언만 만든다.** 각 워크트리가 나중에 채울 타입과 함수의 이름·시그니처를
여기서 못박으면, 분기 후 공유 헤더를 두 곳에서 건드릴 일이 사라진다.

기존 코드를 거의 건드리지 않는 **덧붙이기 작업**이라 B·C 보다 먼저 할 수 있고,
먼저 해야 B·C 가 맞춰갈 목표가 생긴다. 새 타입은 처음부터 최종 네임스페이스로 만든다 —
그러면 Stage C 는 기존 코드만 정리하면 된다.

#### B0-1. `JBroCore` — 식별자

- [ ] `JBro/Core/Core.h` 에 추가

  ```cpp
  namespace JBro
  {
      using InstanceId = std::uint64_t;
      inline constexpr InstanceId InvalidInstanceId = 0;
  }
  ```

- [ ] `JBro/Core/InstanceIdGenerator.h` 신규 — 선언만

  ```cpp
  namespace JBro
  {
      // [ 42비트 ms ][ 10비트 세션 난수 ][ 12비트 시퀀스 ]
      class InstanceIdGenerator
      {
      public:
          void       BeginFrame();          // 타임스탬프를 프레임당 1회만 읽는다
          InstanceId Generate();            // 실질 비용은 ++m_sequence
      private:
          std::uint64_t m_cachedMs  = 0;
          std::uint32_t m_session   = 0;    // 프로세스 시작 시 1회 난수
          std::uint32_t m_sequence  = 0;
      };
  }
  ```

  `Core.h` 에 넣지 않고 별도 헤더로 둔다 — 상태를 가진 클래스이고
  `Core.h` 는 상태 없는 기반 타입만 담는다(§3 Core 입주 조건).

#### B0-2. `JBroRuntime` — 참조

- [ ] `JBro/Runtime/Ref.h` 신규 — 선언만

  ```cpp
  namespace JBro
  {
      struct InstanceHandle                    // 8B. 이번 실행에서의 위치
      {
          std::uint32_t Slot = 0;
          std::uint32_t Gen  = 0;
          bool IsSet() const;
      };

      struct InstanceRef                       // 24B · POD · DLL 경계 통과
      {
          InstanceId     ObjectId    = InvalidInstanceId;
          InstanceId     ComponentId = InvalidInstanceId;
          InstanceHandle Cached;
      };

      enum class RefCategory : std::uint8_t { Object, Component, Script, Asset, Canvas };

      template<typename T>
      class Ref : public InstanceRef
      {
      public:
          static constexpr RefCategory Category = /* T 로부터 결정 */;

          T*   Get() const;                    // 무효면 nullptr
          T*   operator->() const;             // Get() 과 같음 + Debug assert
          T&   operator*()  const;
          bool IsValid() const;
          void Clear();
          explicit operator bool() const;      // "설정됨" 만. 해석하지 않음
          bool operator==(const Ref& rhs) const;
      };
  }
  ```

- [ ] `static_assert` 3 종을 같은 헤더에 둔다

  ```cpp
  static_assert(sizeof(Ref<GameObject>) == sizeof(InstanceRef));
  static_assert(std::is_standard_layout_v<Ref<GameObject>>);
  static_assert(std::is_trivially_copyable_v<Ref<GameObject>>);
  ```

`Ref<GameObject>` 부분 특수화(안전 멤버)는 **여기서 하지 않는다** — `GameObject` 실객체가
Stage B 에서 생긴 뒤라야 의미가 있다. G5 가 담당한다.

#### B0-3. `JBroRuntime` — 컨텍스트

- [ ] `JBro/Runtime/Context.h` 신규 — 빈 구조체 + 바인딩 선언

  ```cpp
  namespace JBro
  {
      struct EngineContext  {};                // H1 이 채운다
      struct SystemContext  {};                // H1
      struct ServiceContext {};                // H1

      void BindSystemContext(const SystemContext& context);    // H3
      void BindServiceContext(const ServiceContext& context);  // H3
  }
  ```

#### B0-4. 검증

- [ ] 새 헤더가 어느 모듈에서도 컴파일되는지 확인 (빈 `.cpp` 로 include 스모크)
- [ ] Debug / Release x64 빌드 통과
- [ ] `JBroTests` 통과 (기존 테스트가 깨지지 않았는지)
- [ ] 커밋 — 여기까지가 워크트리 분기의 전제다

#### 워크트리 매핑

| 선언 | 나중에 채우는 곳 |
|---|---|
| `InstanceIdGenerator` | W-ref (F1) |
| `InstanceHandle` / `InstanceRef` / `Ref<T>` | W-ref (G1~G4) |
| `Ref<GameObject>` 특수화 | W-ref (G5) |
| `EngineContext` / `SystemContext` / `ServiceContext` | W-host (H1~H3) |

### Stage B. ECS 걷어내고 오브젝트-컴포넌트 모델로 (F1~F12)

**이 단계가 이번 작업의 본체다.** 되돌리기 어려우므로 단독 커밋으로 끊고 빌드를 통과시킨다.

- [ ] B1. `CWorld` 제거. `Canvas` 가 오브젝트 풀과 컴포넌트 풀을 직접 소유한다.
- [ ] B2. `Entity` 정수 ID 제거. `GameObject` 실객체를 도입한다.
- [ ] B3. 컴포넌트 논리 소유를 오브젝트로 옮긴다(`vector<SafePtr<ComponentBase>>`).
- [ ] B4. Transform 과 부모·자식 계층을 `GameObject` 멤버로 옮긴다.
      `HierarchyComponent` 를 제거한다.
- [ ] B5. 컴포넌트를 POD `struct` 에서 다형성 `ComponentBase` 파생으로 바꾼다.
- [ ] B6. 레이어 소속을 오브젝트 멤버로 옮기고 `GetLayerIndex()` 를 O(1) 로 만든다.
- [ ] B7. `Query<A,B>` 를 제거하고 `ForEach<T>` / `ForEachObject` 로 바꾼다.
- [ ] B8. `ComponentTypeId` 매직넘버를 `MakeStableTypeId(T::StaticTypeName())` 으로 대체한다.
- [ ] B9. `IsActiveComponent()` 단일 활성 게이트를 도입하고 모든 시스템이 쓰게 한다.
- [ ] B10. `Tag` / `Flags` 를 `GameObject` 에 추가한다. (`m_creationOrder` 는 `InstanceId` 가 흡수)
- [ ] B11. 같은 타입 컴포넌트를 여러 개 붙일 수 있게 한다.
- [ ] B12. 기존 ECS 테스트를 새 모델 기준으로 다시 쓴다.

### Stage C. 네임스페이스와 이름 (D-11, D-12)

- [ ] C1. `JBro::Engine` 을 §10.1 체계로 교체한다.
      `Component` / `Asset` / `System` / `Service` / `Internal` + `JBro` 직속.
- [ ] C2. 타입 접두사를 제거한다. 인터페이스 `I` 와 private `m_` 만 남긴다.
- [ ] C3. 시스템·서비스 이름에 `System` / `Service` 접미를 붙인다.
- [ ] C4. 차원 마커 위치를 `<도메인><차원><역할>` 로 통일한다
      (`TransformSystem2D` → `Transform2DSystem`).
- [ ] C5. F20 을 분리한다. `System::Physics2DSystem` + `Service::Physics2DService`.
- [ ] C6. `GraphicsSystem` 을 폐기하고 `Renderer` 가 디바이스 부착을 흡수한다 (D-17).

### Stage D. 의존 방향 교정 (F16~F19)

- [ ] D1. `GameSystem` / `SystemScheduler` 의 `RenderWorld2D` 의존을 제거한다 (F16).
- [ ] D2. `GraphicsApi` / `SurfaceHandle` 을 Core 로 옮겨 Platform→RHI 의존을 끊는다 (F17).
- [ ] D3. `IPlatform::CreateWindow` / `DestroyWindow` 를 개명한다 (F18).
- [ ] D4. 합성 루트를 하나로 정리한다 (F19). `EngineContext` 가 서비스 집합에 이름을 준다.
- [ ] D5. `GameScript` 의 차원 종속을 검토한다 (F21).
      차원 무관 수명 주기를 공통 모듈로 내리고 충돌 콜백만 Framework 가 얹는다.

### Stage E. 2D/3D 배타 정책 (D-15)

- [ ] E1. 프로젝트 템플릿이 생성하는 스크립트 타깃의 include 경로를 2D/3D 별로 정의한다.
- [ ] E2. 2D 스크립트 타깃에서 Framework3D 헤더 include 시 실패하는 음성 테스트를 추가한다.
- [ ] E3. 게임 익스포트에서 선택된 Framework 만 링크하도록 설정을 분리한다.
- [ ] E4. 엔진·에디터 빌드가 2D/3D 를 모두 포함함을 빌드로 확인한다.

### Stage F. 식별자와 참조 (D-9, D-10)

- [ ] F1. `InstanceId` 생성기를 만든다. `[42비트 ms][10비트 세션난수][12비트 시퀀스]`,
      시간은 프레임당 1회 캐시.
- [ ] F2. `GameInstance` 의 식별자를 `InstanceId` 로 바꾼다.
- [ ] F3. `Ref<T>` 저장부를 문자열에서 정수로 바꾼다(128B → 24B).
- [ ] F4. 로드 직후 `InstanceId → {Slot, Gen}` 맵을 만들고 모든 `Ref` 를 일괄 패치업한다.
- [ ] F5. 프레임 루프에서 식별자 조회가 0 회임을 확인한다.
- [ ] F6. `TObjectPool` 의 주소 안정성을 계약으로 문서화하고 테스트로 고정한다 (F22).

### Stage G. `Ref<T>` 단일 참조 (D-5, D-7, D-8)

값 타입 핸들 클래스를 따로 만들지 않는다. 모든 참조가 `Ref<T>` 하나다.

- [ ] G1. 오브젝트·컴포넌트 슬롯에 generation 을 도입하고 파괴 시 증가시킨다.
      *(`InstanceHandle` / `InstanceRef` / `Ref<T>` 선언은 Stage B0 에서 이미 만들었다.
      여기서는 구현을 채운다.)*
- [ ] G2. `Ref<T>` 에 해석 경로를 만든다.
      `Cached.Slot` 범위 검사 + `Cached.Gen` 비교, 실패 시 `InstanceId` 로 재해석.
- [ ] G3. `Ref<T>` 에 접근자를 단다.
      `Get()` → `T*`(무효면 nullptr), `operator->`(`Get()` 과 같고 Debug assert 만 추가),
      `IsValid()`, `explicit operator bool()`.
- [ ] G5. `Ref<GameObject>` 부분 특수화로 안전 멤버를 제공한다.
      `Destroy()` / `SetActive()` / `GetComponent<T>()` / `TryGetPosition()`.
      안전 멤버 내부에서는 `operator->` 를 쓰지 않는다.
- [ ] G6. `GetComponent<T>()` 반환형을 `T*` 에서 `Ref<T>` 로 바꾼다.
- [ ] G7. `using GameObject = CGameObject` 별칭을 제거해 스크립트가 실객체를 못 보게 한다.
- [ ] G8. 테스트: 파괴된 슬롯이 재사용돼도 과거 `Ref` 가 `nullptr` 을 돌려주는지,
      `Ref<GameObject>` 안전 멤버를 `if` 없이 호출해도 크래시가 없는지.

### Stage H. 컨텍스트와 핫 리로드 (D-13, D-14)

- [ ] H1. `EngineContext` / `SystemContext` / `ServiceContext` 를 정의한다.
- [ ] H2. 서비스를 값으로 담고, 시스템은 전방 선언 + 비인라인 구현으로 은닉한다.
- [ ] H3. `BindSystemContext` / `BindServiceContext` 를 만든다.
- [ ] H4. 스크립트 DLL 로드 / 언로드 / 재로드 경로를 구현한다.
      구현 코드에 `LoadLibrary` / `HMODULE` 이 없어야 한다.
- [ ] H5. 핫 리로드 시 슬롯 캐시를 버리고 `InstanceId` 로 재해석한다.
- [ ] H6. 최소 동작 스크립트 하나로 재로드를 실측한다.

### 보류

필요해질 때 승격한다.

- Framework / Core / Runtime 등의 DLL 화 (D-14). 별도 exe 를 여럿 띄우게 되면 재검토.
- RHI DLL 화와 런타임 RHI 교체. 두 번째 Windows 백엔드가 생기면.
- `JBroVulkanRHI` / `JBroWebGPURHI` 프로젝트 승격, Android 플랫폼.
- 디바이스 로스트 런타임 복구 (D-16).
- 에디터 Play 모드 "정지 시 원상복구". Canvas 두 벌이 필요해지면 핸들 타입을 재검토해야 한다.

## Verification

- [x] Debug x64 / Release x64 전체 빌드 (Stage A 시점)
- [x] `JBroTests` Debug / Release 통과 (Stage A 시점)
- [x] 모듈 간 역방향 include 0 건
- [x] 경계 음성 테스트 2 건 (하위→상위 모듈, 2D 소비자→Framework3D)
- [ ] Stage B 이후 전체 재빌드 + 테스트
- [ ] 잔여 검색: `Entity`, `CWorld`, `Query<`, `dynamic_cast`, `Manager`, `C` 접두, 매직 TypeId
- [ ] 사용자 스크립트 타깃 음성 테스트 (Stage E 의 최종 형태)
- [ ] 스크립트 DLL 재로드 후 호스트 생존 및 `Ref` 복구
- [ ] `GameObject` 핸들 무효 접근이 `if` 없이 안전

## Risks

- **Stage B 가 가장 크다.** 신규 트리의 실동작 코드 대부분(ECS, `CWorld`, `Canvas`)을 바꾼다.
  단독 커밋으로 끊고 빌드·테스트를 통과시킨 뒤 다음으로 간다.
- Stage C 의 일괄 개명은 범위가 넓다. 화이트리스트 치환 + 단계별 빌드로 확인한다.
- Stage H 의 핫 리로드는 스크립트 실체가 거의 없는 상태에서 설계된다.
  최소 동작 스크립트를 먼저 세워 실측하지 않으면 규약이 탁상공론이 된다.
- 기존 엔진 마이그레이션 시 `ScriptAPI.h` 가 공개 표면 정의 역할을 한다.
  현재 그 파일은 정상이고 SDK 미러와 바이트 단위로 일치한다(`Dist/` 사본만 낡음).

## Review

### Stage A (완료)

#### Changed

- 단일 `JBroEngine.vcxproj` 를 모듈 10 개 + `JBroTests` exe 로 분해.
- 헤더를 `Modules/<Mod>/Include/JBro/<Name>/` 로 재배치, 참조를 `<JBro/…>` 로 전부 교체.
- `JBro.Common.props` 로 toolset · C++20 · 경고 · 출력 경로를 한 곳에서 상속.
- `JBroFramework` 를 `JBroRuntime` 에 흡수해 상호 의존 해소.
- pch 제거(모든 모듈이 이미 `NotUsing` 이었고 pch 하나로 10 개 프로젝트를 못 덮는다).
  `WIN32_LEAN_AND_MEAN` 은 공통 props 로 이동.
- 테스트 파일 둘이 각각 `main()` 을 가져 `Tests/TestMain.cpp` 러너를 추가.
- `Jbro` → `JBro` 케이싱 통일(폴더 · vcxproj · props · include 경로).

#### Verified

- Debug / Release x64 클린 빌드 성공, 경고 0 건.
- `JBroTests.exe` 양쪽 통과.
- 모듈 간 역방향 · 미허용 include 0 건.
- 경계 음성 테스트 2 건 모두 `C1083` 으로 실패 확인 후 프로브 제거, 재빌드 정상.

#### Not Verified

- 사용자 스크립트 타깃의 최종 형태는 Stage E 에서 만든다.
  Stage A 의 음성 테스트는 `JBroTests` 를 2D 소비자 대역으로 삼은 것이다.
- 런타임 동작은 기존 ECS/Canvas 테스트 범위 밖으로는 검증하지 않았다.
  Stage A 는 배치 변경이며 로직은 건드리지 않았다.
