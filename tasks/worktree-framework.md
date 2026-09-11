# W-framework · 프레임워크와 시스템 계층

> **역사 기록 — 현재 브랜치·병합 지시가 아니다.** 현재는 `main` 단일 워크트리에서 순차 작업한다.
> 이 문서는 당시 담당 범위와 설계 근거만 보존한다. 체크 상태와 본문이 충돌하면 현재 코드·테스트 및
> `todo.md` Decisions를 기준으로 판단한다.

**브랜치**: `work/framework` (경로: `../JBro-framework`)
**병합 순서**: 3번 (독립. W-build/W-platform 뒤)

## 목표

Framework2D / Framework3D 의 컴포넌트 데이터 정의와 시스템 로직을 완성한다.
그리고 Runtime 의 `GameSystem` / `SystemScheduler` 가 계층 위쪽 (RenderWorld2D 등) 을 참조하지
않도록 정리한다.

## 진행 순서 — Phase 1 · Phase 2

이 워크트리는 두 단계로 나뉜다.

### Phase 1 (W-ref 병합 전에 진행 가능 · 독립)

W-ref 의 실 구현에 의존하지 않는 작업. 파일 시그니처만 확정된 상태로 이 워크트리를 시작해도 된다.

- **C4** — 차원 마커 위치 검색 (`grep`).
- **D1** — `GameSystem` / `SystemScheduler` 가 `RenderWorld2D` 를 참조 안 하는지 확인.
- **D5** — `GameScript` 차원 종속 분리 (`GameScriptBase` 를 Runtime 으로).

### Phase 2 (W-ref 병합 · rebase 후)

W-ref 의 `Canvas` · `ComponentBase` · `TObjectPool<T>` · `Ref<T>` · `GameObjectHandle` 실 구현이
있어야 컴파일 가능.

- **B5** — 6개 컴포넌트 `ComponentBase` 파생. `Component::Transform2D` 포함
  (D-3 완화됨 — Transform 은 컴포넌트로 유지).
- **B11** — 같은 타입 컴포넌트 다중 지원 (Canvas 저장구조에서).
- **C5-system** — `System::Physics2DSystem` 시뮬레이션 몸통.
- **5개 시스템 몸통** — Transform2D/SpriteRender2D/Camera2D/Physics2D/Script.
- **Renderer 소비** — SpriteRender2DSystem / Camera2DSystem 이 W-platform 의 Renderer 저수준
  API 를 반복 호출.

## 소유 파일

- `source/JBroEngine/Modules/JBroFramework2D/**`
- `source/JBroEngine/Modules/JBroFramework3D/**`
- `source/JBroEngine/Modules/JBroRuntime/Include/JBro/Runtime/GameSystem.h`
- `source/JBroEngine/Modules/JBroRuntime/Include/JBro/Runtime/SystemScheduler.h`
- `source/JBroEngine/Modules/JBroRuntime/Source/GameSystem.cpp`
- `source/JBroEngine/Modules/JBroRuntime/Source/SystemScheduler.cpp`

**소유하지 않는 것**:
- Runtime 의 `GameObject.*`, `Component.*`, `Ref.h` — W-ref 소관
- Runtime 의 `EngineInstance.*`, `Context.*`, `IFramework.h` — W-host 소관
- Framework2D 안의 `Service::Physics2DService` — W-host 소관 (자세한 것은 아래 "다른 워크트리")

## 배경

- 프레임워크 5개 시스템 (Transform2DSystem / SpriteRender2DSystem / Camera2DSystem /
  Physics2DSystem / ScriptSystem) 이 헤더만 있고 몸통은 빈 스텁이다.
- 컴포넌트 6개 (Transform2D / WorldTransform2D / Camera2D / SpriteRenderer2D / Rigidbody2D /
  Collider2D) 는 지금 POD `struct`. **다형성 `ComponentBase` 파생으로 바꿔야 한다** (B5).
- `Canvas::AttachComponent<T>()` / `GetComponent<T>()` / `ForEach<T>()` 는 시그니처만 있고
  구현이 비었다 (W-ref 의 `TObjectPool` 구현이 있어야 채울 수 있음).

### 다이어그램 요약 (`docs/JBroEngine.drawio.xml` CANVAS 레인)

- **Canvas** — 오브젝트 풀 · 타입별 컴포넌트 풀 · Layer · 시스템을 직접 소유. World/Scene/
  SceneManager 없음.
- **컴포넌트는 다형성** — POD 가 아니라 `Component::ComponentBase` 파생.
- **`TypeId = MakeStableTypeId(T::StaticTypeName())`** — 매직넘버 없음.
- **`IsActiveComponent()` 단일 게이트** — 모든 시스템이 반드시 이걸 씀.
- **같은 타입 컴포넌트 여러 개 붙이기 허용** — `InstanceId` 로 구분.
- **`ForEach<T>(fn)` 순회 중 생성·파괴 금지** — 지연 큐로.

## 작업 항목

### B5. 컴포넌트를 POD → `ComponentBase` 파생으로

**Why**: 다이어그램/§8 계약. 리플렉션 · 직렬화 · 인스펙터가 다형성에 의존한다. 컴포넌트가 POD
struct 면 vtable 이 없어 타입 판별을 매번 밖에서 해야 한다.

**How** (컴포넌트 6개 각각. `Transform2D`/`WorldTransform2D` 도 대상 — D-3 완화로 컴포넌트로 유지):

1. `struct Component::Transform2D` → `class Transform2D : public ComponentBase`.
2. 필수 오버라이드:
   ```cpp
   static constexpr const char* StaticTypeName() { return "Component::Transform2D"; }
   ComponentTypeId GetTypeId() const override { return MakeStableTypeId(StaticTypeName()); }
   ```
3. 데이터 멤버는 `public` 유지 (지금까지 POD 로 쓰던 방식 호환).
4. 같은 요령으로 `Camera2D`, `SpriteRenderer2D`, `Rigidbody2D`, `Collider2D`, `WorldTransform2D`.

**주의**: `ComponentBase` 는 W-ref 가 정의한다. 이 워크트리는 `#include <JBro/Runtime/Component.h>`
로 소비만.

### B11. 같은 타입 컴포넌트 여러 개 지원

**Why**: 다이어그램 명시. 예: 오브젝트 하나에 Collider2D 두 개 (몸통 + 트리거).

**How** (`Canvas.h/cpp` 편집):

1. `Canvas::AttachComponent<T>(GameObject* owner)` 는 항상 새 인스턴스를 생성. 기존이 있어도
   덮지 않고 추가.
2. `Canvas::GetComponent<T>(owner)` 는 **첫 번째만** 반환. 여러 개 필요하면
   `Canvas::GetComponents<T>(owner, out)` — 두 번째 API 추가.
3. `Canvas::ForEach<T>(fn)` 는 타입별 풀 전체를 훑는다 (오브젝트당 몇 개인지는 무관).

### C4. 차원 마커 위치 확인 (`Transform2DSystem` 등)

**Why**: ProjectRule §10.2 규칙 — 차원 마커는 도메인 명사 바로 뒤. `TransformSystem2D` 가
아니라 `Transform2DSystem`.

**How**:

1. `grep -rn "[A-Z][a-z]*System2D\b" source/JBroEngine/Modules/` — 검색 결과가 0 이어야.
   있으면 개명.
2. 대칭으로 Framework3D 도 확인.
3. 이미 Stage C 에서 대부분 처리됐다. 이 항목은 잔여 검색.

### C5. `System::Physics2DSystem` 완성 (시뮬레이션 부분)

**Why**: Physics 는 System (시뮬레이션) + Service (스크립트 노출) 로 나뉜다. Service 는 W-host
소관. System 구현은 여기서.

**How**:

1. `Physics2DSystem::OnFixedUpdate(Canvas& canvas, float fixedDeltaTime)` 몸통:
   - `canvas.ForEach<Component::Rigidbody2D>([&](Rigidbody2D& body) { ... integrate ... })`
   - `canvas.ForEach<Component::Collider2D>([&](Collider2D& col) { ... broad phase ... })`
   - 충돌 감지 후 `WorldTransform2D` 에 write-back.
2. 스코프 제한: 원 · 박스 콜라이더만. 회전 없는 강체만. 실제 물리 엔진 (Box2D 등) 통합은 별도.
3. Service 가 나중에 부를 수 있게 `Physics2DSystem::Raycast(...)` / `OverlapBox(...)` public
   메서드 유지.

### D1. `GameSystem` / `SystemScheduler` 가 `RenderWorld2D` 를 참조하지 않는지 확인

**Why**: F16 지적. Runtime 이 Framework2D 의 렌더 타입에 컴파일 의존하면 3D 만 쓰는 게임에서도
2D 렌더 헤더가 딸려 온다.

**How**:

1. `grep -rn "RenderWorld2D" source/JBroEngine/Modules/JBroRuntime/` — 결과 0 이어야.
2. 현재 스켈레톤은 이미 없음. 확인만.
3. 렌더 데이터 추출은 시스템별 훅으로 (예: `SpriteRender2DSystem::ExtractRenderWorld(canvas)`),
   `GameSystem` 자체엔 `RenderWorld2D` 인자 없음.

### D5. `GameScript` 차원 종속 검토

**Why**: F21 지적. `GameScript::OnCollisionEnter(Collision2D&)` 처럼 2D 전용 콜백을 베이스에 두면
3D 스크립트도 이걸 상속해야 한다.

**How**:

1. `GameScript` 의 차원 무관 훅 (Create/Start/Update/FixedUpdate/Destroy) 은 Runtime 으로 옮긴다.
   → 새 파일 `JBroRuntime/Include/JBro/Runtime/GameScriptBase.h`
2. 충돌 콜백은 각 Framework 가 파생 클래스로 얹는다.
   ```cpp
   // JBroFramework2D
   class GameScript2D : public GameScriptBase {
   public:
       virtual void OnCollisionEnter(const Collision2D& c) { (void)c; }
       virtual void OnCollisionExit(const Collision2D& c)  { (void)c; }
   };
   ```
3. 사용자 스크립트는 `class Player : public GameScript2D` (2D 프로젝트).

### 5개 시스템 몸통 완성

각 시스템의 `On*` 훅을 실제 로직으로 채운다. 자세한 것:

- **Transform2DSystem**: `canvas.ForEach<Transform2D>` 로 local 을 world 로 곱한다.
  부모 계층은 `GameObject::GetParent()` 를 따라 재귀. 캐시 (`WorldTransform2D::dirty`) 로 optimization.
- **SpriteRender2DSystem**: `canvas.ForEach<SpriteRenderer2D>` 로 자기 안의 `RenderWorld2D`
  에 아이템 축적. 프레임 끝에서 `Renderer::SubmitSprite(...)` 를 반복 호출 (D-29). Renderer 는
  `RenderWorld2D` 자체를 모른다.
- **Camera2DSystem**: primary 카메라 찾아 view · projection 매트릭스 구성 →
  `Renderer::SetCamera(CameraParams{view, proj, clearColor})`.
- **ScriptSystem**: `canvas.ForEach<GameScript>` (또는 `GameScript2D`) 로 Start/Update/FixedUpdate
  호출. `IsActiveComponent()` 체크 필수.

**주의**: 각 훅에서 `IsActiveComponent(component)` 를 반드시 확인. 이걸 빠뜨리면 다이어그램 gate
계약 위반이다.

## 검증

- [ ] Debug / Release x64 통과
- [ ] `JBroTests` 통과
- [ ] `grep -rn "RenderWorld2D" JBroRuntime/` 결과 0
- [ ] 컴포넌트 6개가 `ComponentBase` 파생인지 static_assert 로 확인
- [ ] Physics2DSystem 시뮬레이션 유닛테스트 (body 하나가 중력으로 떨어짐)
- [ ] Transform 계층 유닛테스트 (부모 회전 자식 반영)

## 다른 워크트리와의 인터페이스

- **W-ref 의존**: `ComponentBase`, `Ref<T>`, `GameObject`, `SafePtr<T>`, `Canvas` 실 구현.
  W-ref 병합 뒤에 컴포넌트 파생 + 시스템 몸통 작업 가능.
- **W-platform 의존**: `Renderer::Render(RenderWorld2D)` 시그니처. W-platform 이 이걸 확정하면
  SpriteRender2DSystem 이 프레임 데이터를 넘긴다.
- **W-host 협조**: `Service::Physics2DService` 는 W-host 가 만들고 이 워크트리의
  `Physics2DSystem::Raycast/OverlapBox` 를 위임 호출한다. 그러므로 System 의 public 인터페이스
  (`Raycast`, `OverlapBox`) 는 유지·안정화.

## 병합

- W-ref 병합 후에 이 워크트리가 rebase → 컴포넌트/시스템 몸통 마무리.
- 자체 검증 통과 후 main 병합.

## 2026-09-07 현재 구현 상태

아래는 현재 코드와 이번 검증 결과를 기록한 것이며, 위의 기존 계획은 그대로 보존한다.

- `Camera2DSystem`과 `SpriteRender2DSystem`의 렌더 데이터 추출을 구현했다. `ForEach`와 `IsActiveComponent()`를 사용하며, 활성 상태이고 갱신이 끝난(`dirty == false`) `WorldTransform2D`만 읽는다. 카메라는 역행렬을 만들 수 없는 특이 행렬·비유한 값을 건너뛰고 처음 유효한 primary를 선택하며, 투영 모드·크기·near/far·배경색을 보존한다. 스프라이트는 에셋 핸들·pivot·tint·순서·sourceId를 보존하고 크기의 부호로 반전을 표현한다.
- `RenderWorld2D`는 미리 확보한 용량 안에서 수집한다. 용량 초과 시 배열을 늘리지 않고 `false`를 반환하며 누락 개수를 기록한다. 추출·활성 조건·카메라 예외·용량 제한 검증은 [Framework2DSystemTests.cpp](../source/JBroEngine/Tests/Framework2DSystemTests.cpp)에 있다.
- 전체 솔루션 Debug/Release x64 Rebuild가 모두 경고 0개·오류 0개로 통과했고, 두 구성의 `JBroTests`도 `D3D12Smoke`를 포함해 모두 통과했다. [구조 다이어그램](../docs/JBroEngine.drawio.xml)의 5페이지 `2D render extraction`을 추가하고 draw.io에서 열어 확인했다.
- 아직 `Framework2D` 기본 시스템 등록과 프레임 순서 조립, `FrameworkContext`의 `Renderer` 직접 연결은 구현되지 않았다. 기존 `GraphicsSystem` 스텁 연결이 남아 있으며, 새 Framework 경로로 화면까지 렌더링한 증거와 구 엔진 대비 성능 측정은 없다.
- Transform 및 물리 적분·조회 기반은 기존 커밋에 반영되어 있다. 충돌 반응과 스크립트 생명주기 실행·베이스 분리는 아직 완료되지 않았다.

## 2026-09-08 현재 구현 상태

전날 기록 이후의 변경 사항이다. 위 기록은 당시 상태로 보존한다.

- `Canvas`가 `SystemScheduler`를 소유한다. 실행 순서에 따른 초기화·갱신과 역순 종료, 초기화 전 시스템 등록, RTTI 없는 타입 토큰 조회를 구현했고 콜백 중 스케줄 변경·재진입을 막는다. [SystemSchedulerTests.cpp](../source/JBroEngine/Tests/SystemSchedulerTests.cpp)에서 순서와 수명을 검증한다.
- `Framework2D`가 Transform·Physics·Camera·Sprite 시스템 네 개를 등록하고 프레임 수집의 Begin/End를 조립한다. 고정 시간 간격과 프레임당 최대 추적 단계를 설정받으며, 지연 시 초과 정수 단계는 버리고 소수 잔여 시간을 보존한다. 입력 유효성 검사와 종료·재초기화 상태 초기화도 구현했다. `ScriptSystem`은 아직 등록하지 않는다.
- `FrameworkContext`는 `Renderer*`를 직접 받으며 `IFramework::Render()`가 뷰와 스프라이트를 제출한다. Renderer 프레임 시작·종료는 호스트 소유다. 내부 `RenderBridge2D`는 행벡터 affine을 열벡터 4×4 행렬로 변환하고, size·pivot·flip을 world 변환 전에 적용한다. 직교 투영은 화면 종횡비와 세로 반높이를 사용하며 64개씩 스택 버퍼에서 일괄 제출한다. `Renderer`에는 `AbortFrame`, 소멸자 정리, 초기화 상태·화면 크기·제출 한도 조회가 추가됐다.
- Debug/Release x64 전체 Rebuild는 모두 경고 0개·오류 0개이며, 두 구성의 모든 `JBroTests`가 통과했다. [RendererContractTests.cpp](../source/JBroEngine/Tests/RendererContractTests.cpp)의 Debug CRT 힙 할당 훅은 스프라이트 70개를 사용하는 한 프레임의 Framework→가짜 RHI 경로에서 할당 0회를 확인했다. 이는 해당 테스트 조건의 결과이며 전체 실행 환경에 대한 무할당 증명은 아니다.
- [D3D12SmokeTests.cpp](../source/JBroEngine/Tests/D3D12SmokeTests.cpp)는 숨겨진 창에서 실제 D3D12로 Framework 제출·Present를 6프레임 통과했다. 픽셀 읽기나 화면 육안 검증은 하지 않았다.
- `PixelPerfect`의 기준 해상도·배율 계약은 빡대리께 질문한 상태로 답변을 기다리고 있으며, 현재 해당 모드는 `false`로 명시적으로 거부한다. `EngineInstance`는 아직 스텁이고 `GraphicsSystem`은 그 기존 호스트 골격에 남아 있다. 셰이더의 텍스처·머티리얼 처리, 스크립트·서비스·물리 전체 완성과 구 엔진 대비 성능 측정도 남아 있다.

### 2026-09-08 D5 스크립트 베이스 분리

- Runtime의 `GameScriptBase`에 공통 Create·Start·Update·FixedUpdate·Destroy 훅과 `GetGameObject()`를 옮겼다. Framework2D의 `GameScript2D`에는 2D 충돌 훅만 두며, 기존 `GameScript` 이름의 별칭은 제공하지 않는다.
- [GameScriptTests.cpp](../source/JBroEngine/Tests/GameScriptTests.cpp)를 테스트 실행기에 연결했다. 상속·추상 타입·Runtime의 충돌 훅 부재를 컴파일 시 검사하고, 직접 훅 호출의 가상 디스패치·소유자·활성 상태·파괴 시 `SafePtr` 무효화를 검사한다. 이번 D5 변경의 Debug/Release 전체 솔루션 Rebuild는 모두 경고 0개·오류 0개로 통과했고, 두 구성의 모든 `JBroTests`도 Game script base tests를 포함해 통과했다.
- `ScriptSystem`의 실행 순서 관리와 Create·Start·Destroy 자동 호출은 아직 구현하지 않았다. `Canvas::ForEach<GameScript2D>`는 정확히 해당 타입의 풀만 순회하므로 사용자 파생 스크립트 풀까지 수집하는 연결 방식도 해결되지 않았다.
- H8의 `JBRO_SCRIPT` 선언 매크로는 독립 컴파일 검증을 포함해 별도 커밋 `bbf465a`로 반영·푸시됐다. 플랫폼 창 닫기·크기 변경과 `PixelPerfect` 계약은 여전히 빡대리의 선택을 기다리며, 이번 분리로 새 설계를 확정하지 않았다.
- 후속 변경에서 `GameScriptBase`와 파생 타입의 `RefCategoryOf`를 `Script`로 분리하고 일반 컴포넌트 분류에서 제외했다. `GameScriptTests`에 등록 카테고리 구분, GameObject의 단일·복수 참조와 GameObjectHandle 참조, ID를 통한 캐시 복구·재조회 생략, 파괴 후 참조 무효화 검증을 추가했다. 실행 순서 관리나 핫 리로드를 구현한 것은 아니다. 이 후속 변경의 Debug/Release 전체 Rebuild는 모두 경고·오류 0개이며, 두 구성의 모든 `JBroTests`도 Game script base tests를 포함해 통과했다.
- B11의 `Canvas::GetComponents(owner, Array<T*>&)`는 별도 커밋 `fd94c8b`에 반영했다. 호출자 버퍼를 재사용하고 부착 순서와 비활성 컴포넌트를 보존하며, null·다른 Canvas 소유자·검색 결과 없음에서는 출력 내용을 비운다.
- B11 추가 검증에서 Collider 세 개 사이에 Transform을 부착한 뒤 첫 Collider를 제거하면 `RemoveAllSwap`이 남은 순서와 단일 조회 결과를 바꾸는 결함을 재현했다. `GameObject::DetachComponent`를 순서를 보존하는 `Array::RemoveAll`로 수정하고, [CanvasFoundationTests.cpp](../source/JBroEngine/Tests/CanvasFoundationTests.cpp)에 남은 두 Collider의 순서와 Canvas·GameObject의 첫 항목 조회 검증을 추가했다. 프레임 할당이나 RTTI는 추가하지 않았다. Debug/Release 전체 Rebuild는 모두 경고·오류 0개이며 두 구성의 모든 `JBroTests`가 통과했다.

## 2026-09-11 후속: Canvas 공통 소유권 복구

이 문서의 W-ref 의존 설명은 Canvas를 차원 독립 실행 단위로 사용하지만, 당시 소유 파일 표는
`Framework2D/Canvas/**`를 구현 위치로 지정했다. 이 절을 작성한 당시 코드도 그 위치를 따랐으므로
3D 게임 구성에서 Framework2D를 제외하면 Canvas 구현과 오브젝트·컴포넌트 수명 경로가 함께 사라졌다.

- `tasks/worktree-host.md`는 차원 독립 Canvas 서비스를 `JBroRuntime`에 두도록 기록했다.
- 당시 `Layer`는 범용 정체성·순서와 2D 블렌드·불투명도·패럴랙스 상태가 한 타입에 섞여 있었다.
- Framework3D 컴포넌트의 `ComponentBase` 전환만 먼저 하면 3D 실행 기반이 없는 상태를 가리게 된다.
- D-40에 따라 Canvas 본체와 범용 Layer 정체성은 Runtime 단일 정의로 옮기고 프레임워크별 복제는
  하지 않는다. D-41에 따라 2D 합성 상태는 Framework2D의 `Layer2D`로 분리한다.
- 2026-09-12 구현에서 위 이동과 분리를 완료했다. Framework3D의 5개 타입은 `ComponentBase`를
  상속하고 Runtime Canvas에서 생성·부착되며, Debug_Game3D 링크는 Framework2D를 포함하지 않는다.
  3D 전용 시스템과 렌더 추출은 여전히 후속 구현 대상이다.
