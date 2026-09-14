# 신규 리포를 기존 엔진 구조에 맞추기 — TODO

폐기된 World/ECS 단계 기록은 [canvas-world-foundation.md](./canvas-world-foundation.md)에 남아 있다.
이 문서는 현재 설계나 작업 지시가 아니다.
현재 계약은 [docs/ProjectRule.md](../docs/ProjectRule.md), 변경 근거는 아래 Decisions다.

**현재 작업은 `main` 단일 브랜치·단일 워크트리에서 순차 진행한다.** 아래 문서는 과거 워크트리
분할 당시의 담당 범위와 설계 근거를 보존한 기록이며, 현재 브랜치·병합 지시로 사용하지 않는다:

- [worktree-plan.md](./worktree-plan.md) — 5개 워크트리의 분기·병합 규칙
- [worktree-build.md](./worktree-build.md) — **W-build** · 빌드 구성과 2D/3D 배타 (Stage E)
- [worktree-platform.md](./worktree-platform.md) — **W-platform** · Platform/RHI/Graphics/Asset (D2·D3·C6·H4 API)
- [worktree-framework.md](./worktree-framework.md) — **W-framework** · Framework2D/3D + 시스템 (B5·B11·C4·C5-system·D1·D5)
- [worktree-ref.md](./worktree-ref.md) — **W-ref** · SafePtr 이식 + `Ref<T>` + `GameObjectHandle` + Canvas 구현 (F·G)
- [worktree-host.md](./worktree-host.md) — **W-host** · 컨텍스트 + 스크립트 로더 + 프렐류드 (D4·H)

이 파일의 Decisions는 결정 이력이고, 체크리스트와 진행 상태는 현재 코드·테스트와 대조해야 한다.
과거 작업 문서의 본문과 Decisions가 충돌하면 Decisions를 따르되, `ProjectRule.md`도 같은 변경에서
동기화한다.

**현재 진행 중인 작업 계획은 [structural-refactor-plan.md](./structural-refactor-plan.md)다.**
2026-09-12 전체 구조 검토에서 확정한 방향(D-42~D-55)의 근거·성능 계약·단계 순서·완료 조건이 거기 있다.

## Goal

`source/JBroEngine` 신규 트리를 **기존 엔진(`source/repos/JBroEngine/Engine`)의
오브젝트-컴포넌트 모델**에 맞추고, 스크립트 노출 표면만 핸들로 바꾼다.

## Current Audit Snapshot — 2026-09-12

이 절은 현재 코드와 테스트를 직접 대조한 작업 기준이다. 아래의 Stage·Review 체크리스트는 당시
스냅샷이므로 현재 상태를 증명하지 않는다.

- 현재 브랜치와 워크트리는 `main` 하나다.
- Renderer의 프레임 패킷 API, Canvas·참조·풀의 기본 구현과 버전형 스크립트 DLL 진입점은 코드와
  테스트가 존재한다.
- D-38에 따라 `JBro::Color`는 Core의 단일 정의를 사용한다. Framework2D의 기존 기본 흰색은
  소비자별 명시 초기화로 보존했고, `ScriptAPI.h`와 Framework2D 공개 헤더 결합 테스트가 통과한다.
- `String::Split`은 STL 컨테이너를 공개하지 않고 `Array<String>`을 반환한다. 기본으로 빈 필드를
  보존하고 `skipEmpty=true`일 때만 제외하는 계약을 공개 헤더 결합 테스트로 고정한다.
- `SafePtr.h`는 구 엔진 원본 477줄과 줄 단위로 대조했다. 이식본의 차이는 UTF-8 BOM과
  `namespace JBro` 여닫는 두 줄뿐이며 포인터·수명·캐스트 로직 차이는 0건이다.
- `TObjectPool<T>`의 32슬롯 청크는 생성자에서 받은 `JAllocator`로 할당·반환한다. 200개 생성 시
  7개 청크 할당, 풀 파괴 시 7개 반환, 첫 객체 주소 불변을 Debug/Release 테스트로 확인했다.
- 구 엔진의 `Utillity/Math`는 아직 이식되지 않았다. 현재 `Vec2`/`Rect`/`Matrix3x2`는 Framework2D,
  `Vec3`는 Framework3D, `Matrix4x4`는 Graphics에 분산돼 있다. 공통 수학 타입의 이름과 Core 입주
  범위를 확정하기 전에는 임시 타입을 정식 계약으로 간주하지 않는다.
- Framework3D의 기존 5개 타입은 `ComponentBase` 파생 컴포넌트가 되었고 Runtime Canvas를 사용한다.
  3D 전용 시스템과 렌더 추출은 아직 골격이므로 Framework3D 전체를 완료로 표시하지 않는다.
- D-40에 따라 차원 독립 `Canvas`와 공통 `Layer`를 JBroRuntime으로 옮겼다. D-41에 따라 블렌드·
  불투명도·공간·패럴랙스·별도 합성 텍스처 상태는 Framework2D의 `Layer2D`로 분리했다. Debug_Game3D
  링크에는 JBroRuntime과 JBroFramework3D만 포함되며 JBroFramework2D는 포함되지 않는다.
- 레이어 검색 실패를 `Array::Size()`와 비교하던 기존 오류를 `Array::InvalidIndex` 비교로 바로잡았다.
  파괴된 레이어의 조회·이동·재파괴·오브젝트 배정이 범위 밖 접근 없이 실패하는 테스트를 추가했다.
- D-5에 따라 `Ref<GameObject>`는 템플릿 정적 단언으로 컴파일을 거부한다. 일반 `Ref<T>`의 24B
  레이아웃 검증은 GameObject가 아닌 표본 타입으로 유지하며 음성 컴파일 프로브를 통과한다.
- Framework2D의 `ScriptSystem`은 아직 실행 구현이 없는 스텁이다.
- InstanceId 테스트는 4096 시퀀스를 검사하지만 서로 다른 생성기 세션의 난수 비반복은 검사하지 않는다.
- SDK/Dist 미러와 프로젝트 Templates는 현재 트리에 없다. 존재하기 전에는 관련 완료 조건을 충족한
  것으로 표시하지 않는다.
- draw.io XML 9페이지는 파싱과 연결 무결성을 확인했다. 설치된 draw.io 뷰어가 없어 렌더링 화면
  검수는 아직 증명되지 않았다.

## Open Decisions — 구현 전 빡대리 확인 필요

아래 항목은 구현자가 임의로 좁은 임시 구조를 선택하면 최종 방향을 바꾸므로, 빡대리가 계약을
확정한 뒤 Decisions와 `ProjectRule.md`에 함께 반영한다.

2026-09-12 구조 검토로 3·4·6·7·8이, 2026-09-14 에 1·2·5가 확정되어 Decisions로 옮겼다
(각 항목 끝의 `→ 확정` 참조). **열린 항목은 없다.**

1. **InstanceId 세션 10비트의 범위**
   현재 문구는 프로세스 시작 시 한 번 생성하는 난수를 뜻하지만 구현은 생성기 객체마다 다시 뽑는다.
   프로세스당 값 하나를 공유할지, 생성기마다 0~1023 안에서 비반복 값을 보장할지 결정해야 한다.
   프로세스 재실행 간 절대 비반복은 10비트 난수만으로 보장할 수 없어 비트 구성 또는 영속 상태 변경이
   필요하다.
   **→ 확정: D-59.** 프로세스당 값 하나를 공유하고, 한 실행 안에서의 비충돌만 보장한다.
2. **공통 수학 타입의 이름과 이식 순서**
   차원 독립 공개 타입을 JBroCore에 한 번만 정의한다는 소유권은 D-38로 이미 확정됐다. 여기서 정할
   것은 Core로 옮길지 여부가 아니라 정식 이름·필드 계약과 한 번에 옮길 범위다.
   구 엔진의 `Utillity/Math`에는 `Vector2T`/`Vector2`, `RectT`/`Rect`, `Size`, `Matrix3x2`,
   `Layout2D`가 있다. 신규 트리의 `Vec2`, `Vec3`, `Rect`, `Matrix3x2`, `Matrix4x4`를 한 번에 정식
   타입으로 통합할지, 구 엔진의 2D 타입부터 순차 이식할지 결정해야 한다. 순차 이식을 선택해도 남은
   차원 독립 임시 타입의 Core 통합 의무는 없어지지 않는다. `Layout2D`는 차원 의미가 있으므로
   Framework2D에 남기는 안이 기본 제안이다.
   **→ 확정: D-57.** 수학 타입은 Core 로 올리지 않고 2D·3D 모듈에 나눠 둔다.
3. **ScriptSystem 실행 순서와 변이 경계**
   구 엔진은 레이어 → 하이라키 → 오브젝트의 컴포넌트 부착 순서로 실행 목록을 만들고, 순회 중
   스크립트 생성·파괴가 목록을 무효화하지 않도록 재빌드와 파괴를 안전 지점까지 미룬다. 신규 Canvas는
   이 목록과 지연 변이 경계가 없다. 이 순서를 그대로 유지할지와 FixedUpdate/Update 뒤 flush 시점을
   확정해야 한다. H5의 DLL 리플렉션 생성·파괴 함수 없이 정적 부착 스크립트만 지원하는 구현은 완료로
   간주하지 않는다.
   **→ 확정: D-45.** 구 엔진 계약을 그대로 이식한다.
4. **ScriptAPI 한 줄 include의 실제 소유 위치**
   D-18은 `<JBro/ScriptAPI.h>` 하나만 허용하지만 현재 파일은 Core 경로에서 Runtime을 역참조하고
   Framework2D 공개 타입은 포함하지 않는다. 프로젝트 차원 선택이 같은 경로의 완성된 프렐류드를
   제공할지, 별도 ScriptAPI 빌드 단위를 둘지 결정해야 한다. 사용자가 Framework 헤더를 추가로
   include하게 하는 방식은 D-18을 수정하지 않는 한 채택할 수 없다.
   **→ 확정: D-42.** 각 Framework 모듈의 `Include/JBro/ScriptAPI.h`(같은 경로)로 이동한다.
5. **PixelPerfect 투영 계약**
   기준 해상도, pixels-per-unit, 정수 배율, 남는 영역의 letterbox/crop 정책을 정해야 한다. 현재
   `CameraProjection2D::PixelPerfect`는 존재하지만 시스템이 명시적으로 실패시키며 구현 완료가 아니다.
   **→ 확정: D-58.** 동작해야 한다. 세부 계약은 기존 엔진의 것을 읽고 따른다.
6. **프로젝트 수명의 에셋 소유자 이름과 역할**
   D-12는 `Manager` 명칭을 폐기했지만 현재 코드에는 `AssetManager`, `GetAssetManager` 및 관련 Context
   필드가 남아 있다. 메타데이터를 보관하는 `AssetRegistry`는 별도로 존재하고, 문서에는 사용자 호출
   표면인 `Service::AssetService`도 예고되어 있다. 에셋 로드·캐시를 소유하는 프로젝트 수명 객체를
   `AssetSystem`으로 개명하고 값형 `AssetService`를 앞에 두는 안을 기본으로 제안한다. `AssetRegistry`가
   로드 소유까지 합칠지는 수명과 공개 API를 바꾸므로 빡대리가 확정한다.
   **→ 확정: D-50.** `AssetSystem`(로드·캐시)과 `AssetRegistry`(메타데이터)를 분리 유지, 값형 `AssetService`.
7. **스크립트 리플렉션의 컨테이너 메모리 경계**
   `Allocator.h`는 호스트 할당기를 DLL에 바인딩한다고 설명하지만, 현재 `ScriptModuleLoadContext`에는 할당
   함수가 없고 `BindHeapAllocator` 호출도 0건이다. 또한 `String`은 `HeapAllocator`를 쓰지 않으므로 할당기
   함수만 ABI에 추가해도 스크립트 필드 전체가 안전해지지 않는다. D-37을 유지하려면 호스트는 DLL
   메모리의 C++ 컨테이너를 직접 조작하지 않고, DLL이 제공하는 필드 복사·편집·직렬화 연산을 통하는
   안을 기본으로 제안한다. 반대로 호스트가 직접 편집해야 한다면 `String`까지 포함한 공유 할당기 ABI를
   새로 설계해야 하므로 빡대리가 확정한다.
   **→ 확정: D-51.** 기본 제안 채택 — 호스트는 DLL 컨테이너를 직접 조작하지 않고 DLL이 제공하는 연산을 통한다.
8. **ScriptAPI의 실체 타입 노출 범위**
   `ProjectRule.md` §5는 `Canvas` 같은 구현 타입이 스크립트 헤더에 나타나지 않아야 한다고 규정하지만,
   §6은 `Ref<Canvas>`를 스크립트 참조 종류로 열어 두고 있다. 현재 `ScriptAPI.h`는 실체
   `GameObject.h`를 include하며 그 공개 표면에서 `Canvas* GetCanvas()`까지 보인다. 스크립트가
   `GameObjectHandle`과 서비스만 보게 할지, 실체 타입 선언은 보되 획득 경로만 막을지 확정해야 한다.
   이 결정 전에는 프렐류드 공개 표면을 완료로 표시하지 않는다.
   **→ 확정: D-42.** 스크립트는 `Canvas`·`GameObject` 선언 자체를 받지 않는다. 모듈 계층 분리로 강제한다.

## Decisions

문서 초안과 다르게 확정한 사항이다. 초안보다 이 절을 우선한다.

### 모델

- **D-1. ECS 를 쓰지 않는다. 오브젝트-컴포넌트 모델이 확정이다.**
  `Entity` 정수 ID 없음, 컴포넌트는 다형성, 다중 타입 `Query` 없음.
  시스템은 `ForEach<T>` 로 타입별 컴포넌트 풀을 순회한다.
  **시스템은 `Ref<T>` 를 거치지 않는다** — 순회가 실체 참조를 그대로 주므로 해석 비용이 0 이다.
  순회 중 생성·파괴는 금지한다.
- **D-2. `World` 를 두지 않는다.**
  `Canvas` 가 오브젝트 풀 · 타입별 컴포넌트 풀 · Layer · 시스템을 직접 소유한다.
  수명 계층은 `Canvas` → `GameObject` 하나뿐. `Scene` / `SceneManager` 도 두지 않는다.
- **D-3. 부모·자식·레이어는 `GameObject` 의 멤버. Transform 은 컴포넌트로 유지.**
  기존 엔진 (2D-only) 은 Transform 을 멤버로 뒀지만, 신규 리포는 2D+3D 지원이라 다르다.
  `Component::Transform2D` (Framework2D), `Component::Transform3D` (Framework3D) 각각.
  GameObject 는 dimension 을 모른다. 어느 프레임워크가 로드됐냐에 따라 어느 Transform 을
  attach 할지 결정된다. 부모/자식/레이어는 dimension 무관이라 그대로 멤버로 둔다.
- **D-4. 엔진 내부 참조는 `SafePtr` 를 그대로 쓴다.**
  핸들로 바꾸지 않는다. `Utillity/SafePtr` 는 불가침이다.
  → **W-ref 가 SafePtr 를 이식하고 GameObject 리트로핏.**

### 참조와 식별자

- **D-5. 스크립트에 노출하는 참조는 두 가지뿐이다.**
  1. **`GameObjectHandle`** (16B) — GameObject 만 예외. `IF` 없이 안전 멤버 호출
     (`.Destroy()` / `.SetActive()` / `.GetComponent<T>()` 등). `operator->` 없음.
  2. **`Ref<T>`** (24B) — 그 외 모든 참조 (컴포넌트/스크립트/에셋/캔버스). 저장 · 직렬화용.
- **D-6. 스크립트별 핸들 타입을 코드 생성으로 만들지 않는다.**
  생성 전까지 사용자 코드가 컴파일되지 않아 스크립트를 막 작성한 시점에 편집기가 깨진다.
- **D-7. `Ref<T>` 의 접근자는 하나다.**

  ```cpp
  if (T* p = ref.Get()) p->Foo();   // 확인하고 쓴다
  ref->Foo();                        // 확인 안 하고 쓴다 — 무효면 크래시
  ```

  `operator->` 는 내부적으로 `Get()` 을 부르며 Debug assert 만 차이다.
- **D-8. `GameObjectHandle` 의 무효 접근은 로그 후 무시한다.**
  크래시도 예외도 절반 실행도 없다. 값을 돌려주는 접근만 실패가 드러나게 한다(`TryGetPosition`).
  무검사 경로(`Ref<T>::operator->`)는 이 보장을 하지 않는다. 선택은 사용자가 한다.
- **D-9. 영속 식별자는 `InstanceId`(`uint64` 하나)다.**
  `[42비트 ms][10비트 세션난수][12비트 시퀀스]`. 시간은 프레임당 1회 캐시.
- **D-10. `Ref<T>` 저장부는 24B POD.**
  `InstanceId ObjectId + InstanceId ComponentId + InstanceHandle Cached`.
  로드 직후 일괄 패치업해서 프레임 루프의 식별자 조회를 0 회로 만든다.

### 이름과 경계

- **D-11. 네임스페이스로 구분한다. 타입 접두사를 쓰지 않는다.**
  `JBro::Component` / `Asset` / `System` / `Service` / `Internal`, 나머지는 `JBro` 직속.
  `JBro::Game` 은 두지 않는다.
  예외 둘: 인터페이스 `I` 접두, private 멤버 `m_`.
- **D-12. `Manager` 명칭을 폐기하고 `System` / `Service` 로 나눈다.**
- **D-13. 컨텍스트는 셋이다.**
  `EngineContext`(호스트 전부) → `SystemContext`(DLL 은 받되 사용자엔 비공개) /
  `ServiceContext`(사용자 공개). 서비스 헤더는 시스템을 전방 선언만 하고
  실제 호출은 비인라인 `.cpp` 에 둔다.
- **D-14. DLL 경계는 게임 스크립트 하나뿐이다.**
  RHI 는 정적으로 시작. 두 번째 Windows 백엔드가 생기면 승격.
- **D-15. 2D/3D 배타성은 엔진 빌드가 아니라 사용자 스크립트 프로젝트와 게임 익스포트에만 적용한다.**
  엔진·에디터는 둘 다 포함. 2D 게임 실행 파일에 3D 코드는 안 들어간다.
- **D-16. 디바이스 로스트는 치명적 오류로 처리하고 종료한다.**
  대신 RHI 디바이스 포인터를 그래픽스 계층 밖으로 내보내지 않는다.
- **D-17. `GraphicsSystem` 을 폐기하고 `Renderer` 가 디바이스 부착을 흡수한다.**
  → **W-platform 담당 (JBroGraphics 안에서 신설).**

### 스크립트 표면

- **D-18. `ScriptAPI.h` 는 스크립트 DLL 이 include 하는 유일한 헤더다.**
  `using namespace JBro` 포함. 1-뎁스 네임스페이스 (`Component::Transform2D` 등) 는 유지.
  `SystemContext` 헤더는 include 하지 않는다 (사용자에게 시스템 노출 금지).
  → **W-host 담당.**
- **D-19. 게임 스크립트 클래스는 `JBRO_SCRIPT` 매크로로 선언한다.**
  `class` 로 적으면 컴파일은 되지만 에디터 코드 생성기가 grep 못 해서 목록에 안 뜬다.
  → **W-host 담당.**

### 이번 세션 종료 시점의 상태 재정의 (컨텍스트 압축 후 잊혔던 것들)

- **D-20. `F2. GameInstance 식별자 → InstanceId` 는 폐기.**
  신규 리포엔 `GameInstance` 클래스 자체가 없다. 대체: **GameObject 와 컴포넌트가 생성 시
  `InstanceIdGenerator::Generate()` 로 아이디 발급.**
- **D-21. `G7. using GameObject = CGameObject 별칭 제거` 는 폐기.**
  신규 리포엔 `CGameObject` 자체가 없다.
- **D-22. `D2. GraphicsApi/SurfaceHandle 을 Core 로 이동` 은 철회.**
  대체: `GraphicsApi` 는 RHI, `SurfaceHandle` 은 Platform. 의존은 `RHI → Platform` 방향.
- **D-23. `F5. 프레임 루프 식별자 조회 0회 확인` 은 스코프 축소.**
  프레임 루프 자체가 아직 없다. 대체: `Ref<T>::Get()` 캐시 히트 시 InstanceId 안 읽는지 유닛테스트.
- **D-24. `C5. Physics2D System/Service 분리` 는 크로스-워크트리.**
  `System::Physics2DSystem` (시뮬레이션) 은 W-framework, `Service::Physics2DService` (스크립트 노출)
  는 W-host.
- **D-25. `H4. LoadLibrary/HMODULE 은 코드에 없음` 은 크로스-워크트리.**
  `IPlatform::LoadDynamicLibrary` / `GetSymbol` / `UnloadDynamicLibrary` API 는 W-platform,
  스크립트 로더 소비는 W-host.

### 검토 3차에서 확정한 것

- **D-26. 활성 캔버스 가정. `GameObjectHandle` / `Ref<T>` 는 프로세스-전역 레지스트리로 해석한다.**
  Handle 은 16B 유지. `Canvas::CreateObject` 시 `InstanceId → GameObject*` 를 전역 레지스트리에 등록,
  `DestroyObject` 시 제거. 다중 캔버스 (에디터 미리보기) 는 명시적 캔버스 파라미터 API 로 예외 처리.
  → Unity 의 (자산 GUID = 영구) + (InstanceID = 활성 씬 런타임) 이원 구조와 동일.
- **D-27. `Context.h` 를 3개 파일로 분리한다.**
  - `Runtime/EngineContext.h` — 호스트 전용 (`EngineContext` 정의).
  - `Runtime/SystemContext.h` — DLL 은 include 하지만 `ScriptAPI.h` 프렐류드는 include 하지 않음.
  - `Runtime/ServiceContext.h` — `ScriptAPI.h` 프렐류드가 include. 사용자 노출.
  현재 B0 에서 한 파일에 셋을 모아뒀는데, `ScriptAPI.h` 가 include 하면 `SystemContext` 정의도 사용자
  TU 로 딸려온다. 파일을 나눠서 include 트리로 노출 범위를 강제.
- **D-28. `SystemContext` ABI 는 (c) 재빌드 강제 + (a) 버전 스탬프 안전망 조합.**
  게임 DLL 은 어차피 사용자 프로젝트마다 호스트와 함께 빌드되므로 재빌드가 자연스러운 규약.
  `SystemContext` 첫 필드는 `uint32 AbiVersion`. 호스트/DLL 이 다른 버전이면 로드 거부.
  런타임 함수 테이블(b) 은 매 프레임 호출되는 시스템에 함수 오버헤드가 곤란해서 채택 안 함.
- **D-29. Renderer 는 저수준 API 만 노출한다.**
  `BeginFrame` / `SetCamera` / `SubmitSprite` / `SubmitMesh` / `EndFrame`. `RenderWorld2D` /
  `RenderWorld3D` 같은 프레임 타입은 소비자 (Framework2D/3D 시스템) 가 자기 안에서 만들고
  Renderer 의 저수준 API 를 반복 호출한다. Renderer 가 `RenderWorld2D` 를 인자로 받으면 `JBroGraphics`
  가 `JBroFramework2D` 를 참조해야 하는 역방향 의존이 생겨서 안 됨.
- **D-30. `GetComponent<T>` 는 `GameObject::m_components` 선형 순회.**
  기존 엔진도 같은 구조 (`FindComponentRaw` 가 `m_components` 순회). n=5 기준 캐시-핫 배열 순회가
  해시맵 조회보다 빠르다. 시스템의 매 프레임 순회는 `Canvas::ForEach<T>` 를 쓰므로 GetComponent
  는 사용자 스크립트의 초기 캐싱 용도. 매 프레임 부담이 없다.
- **D-31. `GetComponent<T>` 는 첫 번째만 반환.** 여러 개 필요하면 `GetComponents<T>` (복수형).
  같은 타입 다중 컴포넌트 (B11) 를 지원하지만 첫 매칭을 조용히 반환하는 게 문서 명시된 계약.
- **D-32. D-29를 보정한다 — Renderer 저수준 API는 즉시 드로우가 아니라 프레임 패킷 수집이다.**
  `BeginFrame` 안에서 `BeginView / EndView` 로 카메라·출력 경계를 명시하고, Framework2D/3D 는
  자기 프레임 타입을 Graphics 에 노출하지 않은 채 `ArrayView` 로 스프라이트·메시 패킷을 일괄
  제출한다. Renderer 는 `EndFrame` 에서 컬링·정렬·에셋 해석·배칭·렌더 그래프 구성·커맨드 기록을
  수행한다. `SubmitSprite / SubmitMesh` 단건 API 는 같은 수집 경로의 편의 함수이며 RHI 를 즉시
  호출하지 않는다. 정상 프레임 경로에서 일반 힙 할당·문자열 생성/비교·`WaitIdle` 을 하지 않는다.
- **D-33. 사용자 커스텀 포스트프로세스는 Shader Graph 에셋으로 제공한다.**
  사용자용 Shader Graph 는 컴파일되어 Shader/Material 에셋과 바인딩 메타데이터가 되고,
  `PostProcessProfile` 이 실행 순서를 보관한다. Graphics 내부 Render Graph 는 패스 순서·렌더 타깃
  수명·배리어를 관리한다. 게임 스크립트에는 Renderer/RHI 또는 임의 GPU 콜백을 노출하지 않는다.
  단일 그래프는 기본적으로 입력 하나에서 출력 하나를 만들고, 여러 단계 효과는 Profile 이 연결한다.
  Bloom 처럼 내부 다중 패스가 필요한 그래프는 Shader Graph 컴파일 결과가 그 패스 구성을 가진다.
- **D-34. `ServiceContext`는 서비스 객체를 값으로, `SystemContext`는 좁은 시스템 인터페이스 포인터를 보관한다.**
  서비스 포인터는 사용자 코드에 null 검사와 수명 혼동을 퍼뜨리므로 채택하지 않는다. 구체 시스템 포인터는
  프렐류드 경계를 오염시키므로 채택하지 않는다. 두 Context는 첫 필드의 `AbiVersion`과 POD 경계를 유지한다.
- **D-35. GameHost의 `Skipped` 프레임은 플랫폼 이벤트 인지형 최대 16ms 대기로 제한한다.**
  정상 `Ready` 프레임은 기다리지 않는다. 무조건 `Sleep`하는 방식은 입력·창 종료 반응을 늦추고,
  아무 대기도 하지 않는 방식은 최소화·표면 일시 불가 상태에서 CPU를 소모하므로 채택하지 않는다.
- **D-36. 차원별 서비스는 선택된 Framework의 별도 값 Context로 제공한다.**
  Updates: D-13, D-27, D-34. Runtime의 세 Context는 공통 경계로 유지하고, `Physics2DService`는
  `Framework2DServiceContext`가 값으로 보유한다. 스크립트는 `GetFramework2DServices().Physics2D`로 접근한다.
  활성 프로젝트를 여는 호스트만 Framework의 스크립트 Context를 바인딩한다. 독립 미리보기 초기화는
  전역 바인딩을 바꾸지 않으며, 프로젝트 종료 시 시스템을 파괴하기 전에 바인딩을 해제한다.
  3D 전용 스크립트 타깃에서는 Framework2D include 경로가 없어 이 접근점 자체가 컴파일되지 않는다.
- **D-37. 스크립트 DLL은 단일 버전형 C 진입점과 확장 Context 블록으로 바인딩한다.**
  Updates: D-14, D-28, D-36과 H4의 기존 `JBroScriptModule_Register` 다중 심볼 설명.
  DLL은 `JBroScriptModule_GetApi` 하나를 내보내고, `ScriptModuleApi`의 ABI 버전·구조체 크기·필수
  Context 요구 목록을 호스트가 먼저 검사한다. 공통 `SystemContext`/`ServiceContext`는 고정 필드로,
  차원별 Context는 `TypeId + AbiVersion + Size + Data` POD 블록으로 로드 시 한 번 전달한다.
  Framework2D가 자기 TypeId·요구 버전·블록 생성과 해석을 내부 ABI 어댑터로 소유하므로 Runtime은
  Framework2D를 참조하지 않고 일반 Framework2D 서비스 공개 헤더도 Runtime 내부 계약을 노출하지 않는다.
  `Load`/`Unload` 함수 포인터는 DLL 수명 전환에만 쓰며 매 프레임 호출하지 않는다. `Load` 실패 시에도
  `Unload` 롤백 훅을 먼저 부른 뒤 DLL을 해제한다. 최초 성공 로드와 명시적 언로드, 이전 모듈을 내린
  모든 재로드 시도는 세대 변경으로 외부 캐시 무효화 신호를 만든다.
  DLL 경계로 C++ 가상 객체나 메모리 소유권을 넘기지 않는다. 구 엔진의 C++ 가상 모듈 객체는 컴파일러·
  CRT·할당자 ABI를 경계에 고정하므로 채택하지 않았고, Context별 내보내기 심볼 방식은 Framework가
  늘 때마다 Runtime 로더 수정과 부분 바인딩 실패를 만들므로 채택하지 않았다.
- **D-38. 차원 독립 공개 값 타입은 JBroCore에 한 번만 정의한다.**
  Framework는 같은 이름의 공개 타입을 다시 선언하지 않고 Core의 정식 타입을 include한다.
  임시 타입이 있던 상태에서 정식 타입을 이식할 때는 소비자 마이그레이션과 임시 정의 제거를 같은
  작업의 완료 조건으로 삼는다. `ScriptAPI.h`와 선택 Framework의 공개 헤더를 함께 컴파일하는 검증과
  공개 타입 중복 검사를 통과하기 전에는 이식을 완료로 표시하지 않는다. `Color`가 이 규칙의 첫 교정
  대상이며, 필드명과 기본값처럼 동명이지만 서로 다른 계약도 소비자별로 명시적으로 보존한다.
- **D-39. Windows의 스크립트 DLL은 원본과 같은 디렉터리의 일회성 섭도 복사본으로 로드한다.**
  Updates: D-25, D-37과 H6. Windows가 로드한 파일을 잠그는 동안에도 빌드 출력 경로를
  삭제·교체할 수 있게, `WindowsPlatform` 내부 핸들이 `원본경로.jbro.PID.순번.dll`을 소유한다.
  의존 DLL 탐색 위치를 바꾸지 않도록 원본과 같은 디렉터리를 쓰며, 로드 실패·재로드·언로드에서
  섭도 파일과 네이티브 핸들 래퍼를 함께 정리한다. 로드 중 원본 파일 삭제·재배치와 섭도 복사본
  개수를 실제 DLL 테스트로 검증한다. 별도로 빌드한 V1·V2 DLL을 교체해 내보낸 revision이
  1→2로 바뀌는 것까지 실측했다. `ScriptSystem` 실행과 연결한 `OnUpdate` 로그 변화는 H6에 남는다.
- **D-40. 차원 독립 `Canvas` 본체와 `Layer` 정체성은 JBroRuntime의 단일 정의로 둔다.**
  Updates: D-2, D-3, D-15와 D-38의 단일 정의 원칙. Framework별 Canvas 복제본을 만들지 않는다.
  오브젝트·컴포넌트 풀과 시스템 스케줄러는 공통 Canvas가 소유하고, Framework2D/3D는 선택된 차원의
  컴포넌트·시스템·렌더 추출만 연결한다. Runtime `Layer`에는 차원 독립 정체성과 소속 계약만 두며,
  블렌드·불투명도·패럴랙스·별도 합성 텍스처 같은 2D 상태는 Framework2D가 별도로 소유한다.
- **D-41. Framework2D의 레이어 합성 상태 타입은 `Layer2D`다.**
  Runtime `Layer`에는 식별자·이름·순서·표시 여부만 남긴다. `Layer2D`는 블렌드·불투명도·공간·
  패럴랙스·별도 합성 텍스처 상태를 보관하며 Framework2D가 소유한다. Runtime `Layer`와의 연결 저장
  방식은 Framework2D 내부 구현으로 두며 Runtime 공개 계약이나 직렬화 형식을 늘리지 않는다.
  공통 Layer에 2D 상태를 남기거나 Framework별 Canvas를 복제하는 방식은 채택하지 않는다.

### 2026-09-12 구조 검토에서 확정한 것

근거·대안·검증은 [structural-refactor-plan.md](./structural-refactor-plan.md) §2·§3에 있다.
판단 기준은 "나중에 바꾸는 비용이 큰 계약은 지금 확정한다"이다.

- **D-42. 모듈을 스크립트가 보는 층(Tier S)과 엔진만 보는 층(Tier E)으로 물리 분리한다.**
  Updates: D-18, D-27. Closes: Open Decision 4·8.
  Tier S = `JBroCore`, `JBroRuntime`(Component·Ref·GameObjectHandle·GameScriptBase·System/ServiceContext·ScriptModule·
  `Internal/InstanceRegistry`), `JBroFramework2D`(컴포넌트·서비스·`GameScript2D`·`Layer2D` 값 타입·
  `Internal/ScriptModuleContext`·`ScriptAPI.h`), `JBroAssetTypes`.
  Tier E = `JBroCanvas`(Canvas·GameObject·Layer·GameSystem·SystemScheduler),
  `JBroHost`(EngineInstance·IFramework·ScriptDLLLoader), `JBroFramework2DSystem`(시스템·렌더 추출·`Framework2D` 클래스),
  Graphics·RHI·Platform·Asset. 의존은 Tier E → Tier S 방향만 허용한다.
  `ScriptAPI.h`는 각 Framework 모듈의 `Include/JBro/ScriptAPI.h`에 두어 경로는 하나, 내용은 차원별이다.
  Tier S의 `ComponentBase::GetOwner()`·`GameScriptBase::GetGameObject()`는 `GameObjectHandle`을 반환하며,
  `GameObject*`를 돌려주는 접근은 `JBroCanvas`의 내부 접근 클래스(구 엔진 `CCanvasRuntimeAccess` 패턴)에만 둔다.
  `Canvas::GetComponent<T>(owner)`(`T*` 반환)는 같은 내부 접근 클래스로 옮기고 `FindComponentRaw`로 개명한다.
  기각: 프렐류드 음성 테스트만 늘리는 안(직접 include를 막지 못함), 한 모듈에 include 루트 둘(§3 빌드 단위 원칙과 충돌).
  `Internal/InstanceRegistry`는 Tier S다. `Ref<T>::Get()`과 `GameObjectHandle::Resolve()`가 레지스트리를 호출하고
  이 둘은 스크립트 DLL이 링크해야 하므로(D-44), 레지스트리가 Tier E에 있으면 DLL이 링크되지 않는다.
  `Canvas`는 레지스트리에 등록·해제하는 쪽이고 Tier E → Tier S 방향이라 문제가 없다.
- **D-43. 차원별 시스템 인터페이스는 확장 블록으로 전달하고 공통 `SystemContext`에는 차원 무관 시스템만 둔다.**
  Updates: D-34, D-36, D-37. `SystemContext::Physics2D`를 제거한다(`SystemContextAbiVersion` 3).
  Framework2D는 `Framework2DSystemContext`(Tier S `Internal/`)를 D-37 확장 블록으로 전달하고
  `Physics2DService.cpp`는 `GetFramework2DSystems().Physics2D`를 읽는다.
- **D-44. `InstanceRegistry`는 프로세스 전역·캔버스 무관이며, 스크립트 DLL에 로드 시 1회 바인딩한다.**
  Updates: D-26. `ScriptModuleLoadContext`에 `Internal::InstanceRegistry* Registry`를 추가한다(ABI 2).
  Runtime의 `InstanceRegistry::Get()`은 바인딩된 포인터를 반환한다. 호스트는 프로세스 시작 시 자기 인스턴스를,
  DLL은 `Load`에서 호스트 것을 바인딩한다. 포인터 1회 바인딩이므로 §6.2의 "매 프레임 함수 테이블 우회 금지"와 충돌하지 않는다.
  레지스트리는 캔버스를 모른다. 슬롯은 프로세스 전역이고 `InstanceId`는 프로세스 유일이므로 캔버스 두 벌(에디터 편집본+Play 사본)이
  공존해도 해석이 모호하지 않다. D-26의 "다중 캔버스 명시 파라미터 예외"와 보류 절의 "Canvas 두 벌이면 핸들 재검토"는
  **재검토 없이 성립**한다. `GameObjectHandle` 16B·`Ref<T>` 24B는 영구 고정이다.
  "어느 캔버스에 생성하는가"는 해석이 아니라 서비스 문제이며, 생성 서비스는 호출 스크립트의 소유 오브젝트에서 캔버스를 얻는다.
- **D-45. 스크립트 실행 순서와 변이 경계는 구 엔진 계약을 그대로 이식한다.**
  Closes: Open Decision 3. 실행 목록은 레이어 합성 순서 → 오브젝트 계층(부모 먼저) → 컴포넌트 부착 순서다.
  목록은 더티 플래그로 지연 재구축하며, 트리거는 스크립트 부착/분리·`SetParent`·레이어 생성/파괴/이동이다.
  순회 중 생성은 즉시 수행하되 목록에는 다음 프레임 반영, 순회 중 파괴는 지연 큐에 넣고 `FixedUpdate` 묶음 뒤와 `Update` 뒤
  두 지점에서 flush한다. 순회 깊이 가드(`ScriptIterationGuard`)는 `Canvas`가 소유하고 `Canvas::ForEach<T>`에도 적용한다.
- **D-46. `Layer`는 식별자(`LayerId`)와 합성 순서 캐시(`m_order`)를 분리해 갖고, 렌더가 순서와 가시성을 사용한다.**
  Updates: D-41(유지·보강). `LayerIndex`를 `LayerId`로 개명한다(단조 증가·재사용 없음·직렬화 값).
  `Canvas`는 Create/Destroy/Move에서 `ReindexLayers()`로 `m_order`를 갱신한다(구 엔진 `CGameLayer::m_index`와 같은 역할).
  렌더 정렬 키는 `(layerOrder, renderOrder, sourceId)`를 `std::uint64_t` 하나로 패킹하고, 비가시 레이어는 추출 단계에서 건너뛴다.
  `Layer2D`는 지연 생성한다 — `GetLayer2D(id)`는 살아 있는 런타임 레이어에 상태가 없으면 기본값으로 만들고,
  죽은 레이어면 스테일 엔트리를 지우고 `nullptr`을 반환한다. Runtime 공개 계약 변경 없음.
  구 엔진 `CGameLayer`의 잔여 필드 귀속: `ScaleMode`·`AnchorToSafeArea` → `Layer2D`,
  `SourceAssetGuid`·`KeepOnCanvasChange` → Runtime `Layer`.
  기각: Runtime `Canvas`에 수명 콜백 추가(D-41 위반, 등록 누락 시 재발), 가변 `Canvas` 접근 차단(D-42로 이미 해소).
- **D-47. 월드 변환 캐시는 `Component::Transform2D` 안에 둔다. `WorldTransform2D`는 폐기한다.**
  Updates: D-3(Transform은 컴포넌트 — 유지). `Transform2D`에 `world`·`worldRotation`·`worldScale`·`worldValid`를 두고
  시스템만 쓴다. `Transform2DSystem`은 `Canvas::GetHierarchyVersion()`이 바뀔 때만 부모 먼저 순서의 `Transform2D*` 배열을
  재구축하고, 매 프레임은 그 배열을 한 번 선형 순회한다(조회 0회, 재귀 없음).
  기각: 시스템이 `WorldTransform2D`를 자동 부착(사용자가 붙이지 않은 컴포넌트가 인스펙터·프리팹 diff에 나타남).
- **D-48. `ComponentBase`의 가상 함수 집합을 확정한다.**
  `~ComponentBase()`, `GetTypeId()`, `OnAttached()`, `OnDetached()`, `OnEnabled()`, `OnDisabled()`.
  스크립트 DLL이 파생하는 타입의 vtable은 ABI이므로 이후 추가는 D-28 재빌드 규약 위에서만 허용한다.
  `GameScriptBase`의 `OnCreate`는 `OnAttached` 뒤, `OnDestroy`는 `OnDetached` 앞에 온다.
  형제 컴포넌트 캐시는 `OnAttached`에서 잡고 `InstanceHandle`과 함께 저장해 프레임 시작에 세대 비교 1회로 검증한다.
- **D-49. `IFramework::Render()`는 `RenderResult { Submitted, NothingToSubmit, Failed }`를 반환한다.**
  호스트는 `Failed`만 치명으로 본다. `Framework3D`는 렌더 시스템이 생길 때까지 `NothingToSubmit`을 반환한다.
- **D-50. 에셋은 값 타입 모듈과 시스템 모듈로 나누고 `AssetManager`는 `AssetSystem`으로 바꾼다.**
  Closes: Open Decision 6. `JBroAssetTypes`(Tier S: `AssetId`·`AssetHandle`·`AssetMetadata`·`Asset::*`)와
  `JBroAsset`(Tier E: `AssetSystem` 로드·캐시 소유, 프로젝트 수명; `AssetRegistry` 메타데이터). 스크립트 표면은 값형 `Service::AssetService`.
  `AssetRegistry`는 로드 소유를 합치지 않는다.
- **D-51. `String`은 `std::string` 래퍼로 영구 확정하고 경계·핫 데이터에서는 금지한다.**
  `GameObject::m_tag` 이식 완료(2026-09-12). 원문은 `NameTable`이 보관하고 `NameId`는
  `MakeNameId(text)`로 표 없이 구할 수 있다. `NameTable`도 `InstanceRegistry`와 같은
  `Local`/`Get`/`Bind`를 가지며 `ScriptModuleLoadContext.Names`로 DLL에 바인딩한다(ABI 3).
  Closes: Open Decision 7(기본 제안 채택). POD Context·패킷·`Ref`·핸들·컴포넌트 공개 필드에 `String`을 두지 않는다.
  이름·태그는 인턴된 정수(`NameId = MakeStableTypeId(text)`)로 두고 문자열은 에디터·직렬화 계층이 보관한다.
  `GameObject::m_tag`가 첫 교정 대상이다. 스크립트 리플렉션 필드의 컨테이너 편집은 DLL이 제공하는 연산을 통한다.
- **D-52. 컨테이너 할당기 정책은 인스턴스를 가질 수 있어야 하며, 프레임 임시 배열은 `JMemoryContext.frame`을 쓴다.**
  구현됨(2026-09-12). 되감기 주체는 `Canvas::BeginFrame`이 아니라 `EngineInstance::Tick`이다 —
  프레임을 여는 쪽이 프레임 메모리를 소유하며, Canvas는 이 메모리를 소유하지 않는다.
  복사는 원본 정책을 물려받지 않고 대입은 받는 쪽 정책을 지킨다. 재해싱은 정책을 유지한다.
  아레나를 넘긴 요청은 기본 힙에서 받아 오고 그 블록도 되감기가 회수한다.
  `Array<T, Allocator>`·`Table<..., Allocator>`의 정책 타입에 `[[no_unique_address]]` 멤버로 상태를 허용한다.
  기본 `HeapAllocator`는 빈 타입으로 유지(크기 증가 0), `JAllocatorRef` 정책을 추가한다.
  `frame`은 `Canvas::BeginFrame`에서 리셋되는 선형 할당기다. 도입 시점은 단계 3의 첫 항목이다 — 프레임 임시 배열을 처음 쓰기 직전.
- **H5 리플렉션 — 생성·파괴 경로만 구현됨(2026-09-13).**
  `ScriptRegistry`(Runtime, `Local`/`Get`/`Bind`)에 DLL이 `{name, typeId, size, alignment, Construct, Destruct}`를
  등록하고 `Canvas::AttachScript(owner, name)`이 `ScriptPool`로 만든다. 로드 컨텍스트 ABI 4(`Scripts`).
  기존 엔진의 `CreateScriptFunc`가 캔버스를 받던 모양은 쓸 수 없다 — D-42의 Tier 분리 때문이다.
  **프로퍼티·인스펙터 메타데이터·직렬화는 아직 없다.** Open Decision 3은 생성 경로를 지목했고 그것은 섰다.
- **프로젝트 파일은 `.jproject`(YAML), 기존 엔진과 같은 키를 쓴다(2026-09-13).**
  `LoadProjectFile` / `EngineInstance::OpenProjectFile`. 읽는 범위는 기존 파일이 실제로 쓰는 부분집합이고,
  모르는 구조는 줄 번호와 함께 거절한다. 아직 읽지 않는 키 아래 블록은 들여쓰기로 건너뛴다.
- **D-53. 죽은 계약을 삭제하고 골격은 "미완"으로 명시한다.**
  삭제: `EngineContext`(`EngineInstance`가 그 역할), `RuntimeModule`/`Runtime.h`, `RefCategory::Canvas`·`Asset`,
  스켈레톤 스모크 3개, `TObjectPool::Slot::generation`, `GameObject::m_destroyContext`/`m_destroyCallback`.
  Audit Snapshot에 명시: `PrefabSpawner`·`AssetRegistry`·`AssetSystem::Load`·`ScriptSystem`은 선언만 있는 골격.
- **D-54. 성능 계약을 측정 가능한 형태로 고정한다.**
  Updates: D-32(패킷 필드 보강). 정상 프레임(스폰 포함)에서 힙 할당 0회, `InstanceRegistry` 영속 조회 0회를 카운팅 할당기·카운터로 단언한다.
  `TObjectPool`: ControlBlock을 구 엔진처럼 `m_freeBlocks`로 재활용하고 `Reserve`에서 미리 확보, `Destroy`는 청크 베이스
  정렬 배열 이진 탐색으로 슬롯을 찾는다. `GameObject`는 `m_activeInHierarchy`를 캐시하고 `SetActive`·`SetParent`가 전파한다.
  `Ref<T>::Get()`은 캐시 슬롯이 살아 있고 세대만 다르면 확정 사망으로 단락한다. `Table<InstanceId, …>`는 항등 해시를 쓴다.
  `SpriteSubmit`·`GpuSpriteInstance`의 `world`는 `Matrix4x4`가 아니라 아핀 6 + 깊이 1이다.
  구현형은 `SpriteTransform2D { float linear[4]; float translation[2]; float depth; }`(28B)이고 인스턴스 스트라이드는 44B다.
  셰이더는 `float4x4`를 조립하지 않고 두 내적으로 위치를 만든다. `MeshSubmit`은 `Matrix4x4`를 유지한다.
  `SpriteSubmit::renderOrder`는 렌더러가 읽지 않아 제거했다(D-53) — 정렬은 `RenderWorld2D`가 제출 전에 끝낸다.
  `depth`는 깊이 버퍼가 붙기 전까지 항상 0이며, 자리를 비워 둔 것은 그때 ABI를 다시 깨지 않기 위해서다.
  `RenderWorld2D`는 `(uint64 key, uint32 index)`를 정렬하고
  아이템은 제자리에 둔다. `Ref<T>`·`GameObjectHandle`은 `SafePtr`와 같이 메인 스레드 전용이다.
- **D-55. `ComponentBase::m_owner`는 `SafePtr<GameObject>`로 유지한다.**
  raw 포인터로 줄이면 8B와 역참조 하나를 아끼지만 §6의 명시 규칙을 바꾸는 일이다. private 멤버라 나중에 바꿔도 공개 계약이
  깨지지 않으므로(D-28 재빌드 규약) 지금 열지 않는다.

- **D-56. 게임 스크립트 언어를 자작하고 C++ 로 트랜스파일한다. 이름 JBroScript, 확장자 `.jscript`.**
  **아직 아무것도 구현하지 않았다.** 방향·근거·실측은 [tasks/jbroscript-plan.md](./jbroscript-plan.md) 에 있다.
  요지: 백엔드는 VM 이 아니라 C++ 소스이며 `ScriptDLLLoader`·`ScriptRegistry`·`ScriptPool`·Tier 분리·POD ABI 가 전부 그대로 쓰인다.
  리플렉션은 **형식 하나(`PropertyInfo`), 생산자 둘** 로 푼다 — 빌트인 컴포넌트는 C++ 매크로, 사용자 스크립트는 트랜스파일러.
  언어는 대체가 아니라 **추가 프론트엔드**다. C++ 스크립트 경로를 죽이지 않으므로 언어가 막혀도 엔진은 멀쩡하다.
  `PropertyInfo` 모양은 2026-09-14 에 확정했다(계획서 §8) — 타입의 사실은 `TypeDescriptor` 에,
  필드의 사실은 `PropertyInfo` 에 두고, 접근은 오프셋이 아니라 접근자 하나로 한다.
  **쪽지 보관함은 둘이다** — 빌트인 컴포넌트는 엔진 수명, 스크립트는 DLL 수명이라 한 그릇에 섞지 않는다.
  Open Decision 3 / H5 의 남은 절반(프로퍼티·인스펙터 메타데이터·직렬화)이 이 결정의 대상이다.
  **빌트인 쪽 생산자는 2026-09-14 에 붙였다**(`JBRO_FIELD`, `7be6602`) — 계획서 §14.
  **보관함 둘과 2D 빌트인 부착도 2026-09-14 에 끝냈다**(`c64b8c6`, `ff94c69`, `b7354f2`) — 계획서 §15.
  `TypeDescriptor` 에 `fields` 가 생겨 구조체가 자기 필드로 말한다(88 → 96 바이트).
  **3D 빌트인 다섯도 붙였다**(`926491b`, 계획서 §16). 남은 것은 `ArrayOps`/`TableOps` 구현과
  직렬화(`.jcanvas`)다. 등록 함수의 반환값은 소비자가 없어 실패 경로가 변이로 잡히지 않는다(§16.3).
  에셋 참조는 `AssetId`(저장) + `AssetHandle`(해석된 런타임 값, 비저장)로 나눴다(`ff6b41f`, 계획서 §15.4).
  `AssetSystem` 이 스텁이라 **해석 패스는 아직 없다** — 로드가 실제로 생길 때 붙인다.
  **직렬화의 아래층(YAML 부분집합 읽기·쓰기)은 붙였다**(`e5cf75f`, `JBro/Host/Yaml.h`).
  기존 엔진의 `.jcanvas` 다섯 개를 실제로 읽고, 쓰는 모양이 그 파일들과 같다는 것을 테스트가 고정한다.
  **캔버스 저장도 붙였다**(`87d73d6`, 계획서 §17). 구조체는 이름 없이 나열하고
  (`TypeDescriptor::writeFieldsAsSequence`), 기존 엔진의 `.jcanvas` 는 읽지 않는다 —
  거기 있는 컴포넌트가 이 엔진에 하나도 없어서 읽어 봐야 거의 다 버려진다.
  **읽는 쪽도 붙였다**(`05ac54b`, 계획서 §17.5). `ComponentRegistry` 가 이름으로 컴포넌트를
  붙이고, 저장→로드→저장이 같은 글자를 낸다. 읽기는 모르는 필드·타입을 조용히 넘기지 않고
  어디서 멈췄는지 말한다.

- **D-57. 수학 타입은 차원별 모듈에 둔다. Core 로 올리지 않는다.**
  Closes: Open Decision 2. Narrows: D-38.
  `Vec2`·`Rect`·`Matrix3x2` 는 `JBroFramework2D/Math2D.h`, `Vec3` 는 `JBroFramework3D/Math3D.h`,
  `Matrix4x4` 는 `JBroGraphics/Renderer.h` 에 두는 현재 배치를 유지한다.
  D-38 의 "차원 독립 공개 값 타입"은 `Color` 처럼 **차원 의미가 없는 것**에만 적용된다.
  벡터·행렬은 차원이 곧 의미이므로 그 대상이 아니며, 2D 프로젝트가 3D 수학을 링크하지 않는다.
  대가: 2D 와 3D 를 모두 보는 코드(에디터·Graphics)는 양쪽 타입을 각각 받는다.
  변환이 필요하면 그 지점에 명시적으로 둔다.

- **D-58. `CameraProjection2D::PixelPerfect` 는 실제로 동작해야 한다.**
  Closes: Open Decision 5. 지금 `RenderBridge2D` 가 명시적으로 실패시키는 자리를 구현으로 바꾼다.
  세부 계약(기준 해상도, pixels-per-unit, 정수 배율, 남는 영역 처리)은 착수 시 기존 엔진의 것을
  먼저 읽고 따른다 — `.jproject` 때와 같은 이유로 두 번째 계약을 새로 만들지 않는다.

- **D-59. `InstanceId` 의 세션 비트는 한 실행 안에서의 비충돌만 보장한다.**
  Closes: Open Decision 1. 프로세스당 값 하나를 만들어 모든 생성기가 공유하고, 그 안에서 인덱스가 겹치지 않게 한다.
  실행 간 절대 비반복은 보장하지 않는다 — 10비트는 1024회 실행에 한 바퀴 돌며, 그것을 없애려면
  비트 폭을 늘리거나 영속 상태를 두어야 한다. 저장 파일이 세션을 넘어 ID 를 신뢰해야 할 때 다시 연다.

## Assumptions

- 대상은 `Documents/GitHub/JBroEngine` 신규 리포다. 기존 엔진은 **읽기 전용 기준**으로만 쓴다.
- 기초가 서면 기존 엔진을 이 구조로 마이그레이션한다.
- Windows / D3D12 를 먼저 세운다. Vulkan / WebGPU / Android 는 모듈 규칙만 유지한 채 뒤로 미룬다.

## Historical Findings — 신규 리포 vs 기존 엔진

> Stage B/C 직후 작성한 비교 스냅샷이다. 아래 `잔여`와 `담당` 열은 현재 상태가 아니며,
> 구현 여부는 문서 상단의 Current Audit Snapshot과 현재 코드·테스트로 확인한다.

기존 엔진 구조 (읽어서 확인한 것):

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
```

| # | 항목 | 기존 엔진 | Stage B/C 결과 | 잔여 | 담당 |
|---|---|---|---|---|---|
| F1 | 중간 계층 | Canvas 직접 | Canvas 직접 (World 제거됨) | — | ✓ |
| F2 | 오브젝트 식별 | InstanceGuid | `InstanceId` (24B `Ref`) | 생성 시 발급 (D-20) | W-ref |
| F3 | 컴포넌트 소유 | 오브젝트 `vector<SafePtr>` | 오브젝트 `Array<raw*>` | SafePtr 리트로핏 | W-ref |
| F4 | Transform | 오브젝트 멤버 (기존은 2D-only) | 컴포넌트 (POD) | `ComponentBase` 파생 유지 | W-framework (B5). D-3 완화됨 |
| F5 | 컴포넌트 성격 | 다형성 | 여전히 POD (일부) | `ComponentBase` 파생 | W-framework (B5) |
| F6 | 계층 | 오브젝트 멤버 | 오브젝트 멤버 (raw*) | SafePtr 리트로핏 | W-ref |
| F7 | 레이어 소속 | `SafePtr<Layer>` + O(1) 인덱스 | `uint32 m_layerIndex` 만 | `SafePtr<Layer>` + 인덱스 캐시 | W-ref |
| F8 | 순회 | `ForEach<T>` | `Canvas::ForEach<T>` 시그니처만 | 구현 | W-ref |
| F9 | ComponentTypeId | `MakeStableTypeId` | `MakeStableTypeId` 있음 | 사용 강제 | W-framework (B5) |
| F10 | 활성 게이트 | `IsActiveComponent()` 단일 | `ComponentBase::IsActiveComponent` 있음 | 모든 시스템 사용 | W-framework |
| F11 | 오브젝트 속성 | Tag/Flags/creationOrder | Tag/Flags 있음, order 는 InstanceId 흡수 | — | ✓ |
| F12 | 멀티 컴포넌트 | 같은 타입 여러 개 | 저장 구조 미지원 | B11 지원 | W-framework |
| F13 | 안정 식별자 | `File::Guid` + `Guid128` | `InstanceId uint64` | — | ✓ |
| F14 | 참조 시스템 | `Ref<T>` 5카테고리 + SafePtr | `Ref<T>` 24B 골격 | 구현 · GameObjectHandle | W-ref |

### 신규 리포 자체 결함

| # | 결함 | 담당 |
|---|---|---|
| F16 | 공통 모듈이 차원에 오염 (`GameSystem` 에 `RenderWorld2D`) | W-framework (D1) — 이미 스켈레톤에서 제거됨. 확인만 |
| F17 | Platform 이 GraphicsApi/SurfaceHandle 때문에 RHI 를 include | W-platform (D2 재정의: RHI→Platform 방향) |
| F18 | `IPlatform::CreateWindow` / `DestroyWindow` 가 Win32 매크로와 충돌 | W-platform (D3) |
| F19 | 합성 루트가 둘 (EngineInstance vs EditorApplication) | W-host (D4) |
| F20 | `Physics2DSystem` 이 System + Service 겸함 | W-framework (System) + W-host (Service) |
| F21 | `GameScript` 차원 종속 (`OnCollisionEnter(Collision2D&)`) | W-framework (D5) |
| F22 | `TObjectPool` 주소 안정성 계약이 문서화 안 됨 | W-ref (F6) |

## Success Criteria

- 신규 트리에 `Entity` 정수 ID, `CWorld`, `Query<A,B>`, POD 컴포넌트가 남아 있지 않다.
- `Canvas` 가 오브젝트 풀과 타입별 컴포넌트 풀을 직접 소유한다.
- `Canvas`와 공통 `Layer`는 JBroRuntime에 한 번만 정의되며 Framework별 복제본이 없다.
- Runtime `Layer`에는 2D 합성 상태가 없고, 해당 상태는 Framework2D의 `Layer2D`가 소유한다.
- 3D 게임 구성은 Framework2D 없이 Runtime Canvas의 오브젝트·컴포넌트·시스템 실행 경계를 사용한다.
- 시스템이 `ForEach<T>` 로 컴포넌트 풀을 순회한다.
- 네임스페이스가 §10.1 표대로 적용되고 타입 접두사가 없다(`I` / `m_` 제외).
- 차원 독립 공개 값 타입은 JBroCore에 단 하나만 정의되며 Framework 임시 중복 정의가 없다.
- **스크립트가 `GameObjectHandle` 로 GameObject 를, `Ref<T>` 로 나머지를 본다** (실 객체 참조 없음).
- **`GameObjectHandle` 을 `if` 없이 호출해도 크래시가 없고 로그가 남는다.**
- `Ref<T>` 가 24B 이고 프레임 루프에서 식별자 조회가 0 회다.
- 게임 스크립트 DLL 을 재로드해도 호스트가 살아 있고 참조가 복구된다.
- 모듈 간 역방향 include 0 건, 2D 스크립트 타깃에서 Framework3D include 시 컴파일 실패.
- 테스트가 Debug / Release x64 양쪽에서 통과한다.

## Historical Verification Snapshot — 워크트리 분할 시점

> 아래 체크박스는 당시 검증 기록이며 현재 완료표가 아니다. 특히 `~` 표시는 증거가 보존되지 않은
> 항목이다. 현재 통합 검증 상태는 상단 Current Audit Snapshot을 갱신해 기록한다.

- [x] Debug x64 / Release x64 전체 빌드 (Stage A~C+Types 이식 시점)
- [x] `JBroTests` Debug / Release 통과 (~)
- [x] 모듈 간 역방향 include 0 건 (~)
- [x] 경계 음성 테스트 2 건 (~)
- [ ] 5개 워크트리 병합 후 전체 재빌드 + 테스트
- [ ] 잔여 검색: `Entity`, `CWorld`, `Query<`, `dynamic_cast`, `Manager`, `C` 접두, 매직 TypeId
- [ ] 사용자 스크립트 타깃 음성 테스트 (W-build 의 E2 최종 형태)
- [ ] 스크립트 DLL 재로드 후 호스트 생존 및 `Ref` 복구 (W-host H6)
- [ ] `GameObjectHandle` 무효 접근이 `if` 없이 안전 (W-ref)

## Historical Progress — 워크트리 분할 이전

### Stage A · 빌드 단위 분리 (완료)

Stage A 결과 상세는 아래 [Historical Review Snapshots](#historical-review-snapshots) 참조.

### Stage B0 · 골격 선언 (완료 · `ce2ce97`)

각 워크트리가 나중에 채울 타입·함수의 이름·시그니처만 확정. `InstanceId`, `InstanceIdGenerator`,
`InstanceHandle`, `InstanceRef`, `Ref<T>` (24B POD, static_assert 3종), `EngineContext` /
`SystemContext` / `ServiceContext` + `BindSystemContext` / `BindServiceContext`.

### Stage B · ECS 제거 (완료 · `76fad39`)

- ECS 헤더/소스 삭제 (`Core/ECS/*`, `Runtime/World.*`, `ComponentPoolTests.cpp`)
- `TObjectPool<T>` · `StableTypeId` · `MakeStableTypeId` 도입
- `GameObject` / `ComponentBase` 실 클래스 (raw pointer 판)
- `Canvas` 가 오브젝트/컴포넌트 풀 직접 소유

### Stage C · 네임스페이스와 이름 (완료 · `76fad39`)

- `namespace JBro::Engine` → `namespace JBro` 전 파일
- `CCanvas` / `CLayer` → `Canvas` / `Layer`
- Framework2D 컴포넌트 → `JBro::Component::X2D`
- Framework2D 시스템 → `JBro::System::X2DSystem`
- Framework3D 대칭 정리

### Stage Types · JBro 값타입 이식 (완료 · `9fa0cb1`)

- 기존 엔진의 `Utillity/Types/*` 17개 헤더를 `JBroCore/Include/JBro/Types/` 로 이식
- 모두 `namespace JBro` 로 감쌈 (`StrongTypeOps.h` 매크로 파일 제외)
- 스켈레톤 리트로핏: `std::vector` → `Array`, `std::unordered_map` → `Table`, `std::make_unique` → `MakeOwnerPtr`
- Debug/Release · 테스트 통과

### Stage 워크트리 분기 (이후 철회됨)

당시 `main` 브랜치에서 5개 워크트리를 생성했다:
- `work/build` · `work/platform` · `work/framework` · `work/ref` · `work/host`

각 워크트리의 상세 작업은 [Success Criteria](#success-criteria) 위 링크된 5개 문서에.

## Historical Risks — 워크트리 분할 시점

> W-ref/W-host 병합 순서 등 아래 내용은 현재 작업 지시가 아니다. 아직 유효한 리플렉션·핫 리로드
> 위험은 Current Audit Snapshot과 Success Criteria에서 별도로 추적한다.

- **W-ref 가 가장 크다.** SafePtr 이식 + GameObject 리트로핏 + Canvas 실 구현 + Ref<T> 몸통
  + GameObjectHandle 신설. 서브 브랜치로 나눠 진행 권장.
- **W-host 는 W-ref 와 W-platform 병합 후에만 병합 가능**. 순서 강제.
- W-framework 는 컴포넌트 몸통 완성이 W-ref 의 Canvas 구현에 의존. rebase 필수.
- **리플렉션이 없다.** H5 (핫 리로드 시 Ref 캐시 무효화) 와 F4 (로드 시 InstanceId 패치업) 는
  리플렉션 없이는 계약만 정할 수 있다. 실동작은 리플렉션 붙을 때.
- **기존 엔진 마이그레이션 시 `ScriptAPI.h` 가 공개 표면 정의 역할**을 한다는 당시 계획이었다.
  현재 SDK/Dist 미러는 존재하지 않으며 ScriptAPI의 차원별 공개 표면은 별도 확정이 필요하다.

## 보류

필요해질 때 승격한다.

- Framework / Core / Runtime 등의 DLL 화 (D-14). 별도 exe 를 여럿 띄우게 되면 재검토.
- RHI DLL 화와 런타임 RHI 교체. 두 번째 Windows 백엔드가 생기면.
- `JBroVulkanRHI` / `JBroWebGPURHI` 프로젝트 승격, Android 플랫폼.
- 디바이스 로스트 런타임 복구 (D-16).
- 에디터 Play 모드 "정지 시 원상복구". Canvas 두 벌이 필요해지면 핸들 타입을 재검토해야 한다.
- 리플렉션 시스템 (F4 · H5 실동작 전제).

## Historical Review Snapshots

### Stage A (완료)

#### Changed

- 단일 `JBroEngine.vcxproj` 를 모듈 10 개 + `JBroTests` exe 로 분해.
- 헤더를 `Modules/<Mod>/Include/JBro/<Name>/` 로 재배치, 참조를 `<JBro/…>` 로 전부 교체.
- `JBro.Common.props` 로 toolset · C++20 · 경고 · 출력 경로를 한 곳에서 상속.
- `JBroFramework` 를 `JBroRuntime` 에 흡수해 상호 의존 해소.

#### Verified

- Debug / Release x64 클린 빌드 성공, 경고 0 건.
- `JBroTests.exe` 양쪽 통과.
- 모듈 간 역방향 · 미허용 include 0 건.
- 경계 음성 테스트 2 건 모두 `C1083` 으로 실패 확인 후 프로브 제거, 재빌드 정상.

### Stage B0 / B / C / Types (완료)

#### Changed

- `Stage B0`: `InstanceId` · `InstanceHandle` · `Ref<T>` · Context 3종 시그니처 확정.
- `Stage B`: ECS 걷어내고 `TObjectPool<T>` · `StableTypeId` · `GameObject` · `ComponentBase` 도입.
- `Stage C`: `JBro::Engine` → `JBro` 전 파일 · `CCanvas`/`CLayer` → `Canvas`/`Layer` ·
  컴포넌트/시스템 서브네임스페이스 정리.
- `Stage Types`: 기존 엔진의 `Array` · `Table` · `String` · `Float` · `Int` 등 17개 타입 헤더 이식,
  스켈레톤을 JBro 컨테이너로 리트로핏.

#### Verified

- Debug / Release x64 클린 빌드, 경고 0 건, 오류 0 건.
- `JBroTests` Debug / Release 통과.

#### Not Verified

- 오브젝트 풀 실 구현 (`TObjectPool<T>::Create/Destroy/ForEachLive` 몸통) — W-ref 몫.
- 컴포넌트 풀 실 구현 (`Canvas::AttachComponent<T>` 등) — W-ref 몫.
- 5개 F2D 시스템의 `OnUpdate` 몸통 — W-framework 몫.
- Renderer 실 구현 — W-platform 몫.
- 스크립트 DLL 로드/재로드 — W-host 몫.

### Stage 워크트리 (당시 진행 중 · 현재 아님)

당시에는 각 워크트리 문서에서 독립적으로 관리했다. 현재는 `main` 단일 워크트리만 사용한다.
