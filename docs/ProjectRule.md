# JBroEngine Project Rules

이 문서는 JBroEngine 작업에서 계속 지켜야 하는 프로젝트 규칙을 정리한다.
아키텍처 초안과 내용이 다를 경우 이 문서를 우선하며, 아직 합의되지 않은 설계는 강제 규칙으로 취급하지 않는다.

규칙의 강도는 다음과 같다.

- **MUST**: 반드시 지켜야 한다.
- **SHOULD**: 특별한 이유가 없다면 지킨다. 예외를 두면 이유를 기록한다.
- **MAY**: 상황에 따라 선택할 수 있다.

## 1. 작업 원칙

- 변경 판단은 **정확성 → 단순성 → 영향 범위 최소화 → 검증 가능성 → 유지보수성 → 작업 속도** 순으로 우선한다. (MUST)
- 당장 동작하는 임시 구조보다 실제 게임 엔진으로 확장하고 유지할 수 있는 구조를 선택한다. (MUST)
- 임시 불리언 플래그를 추가해 구조적 문제를 숨기지 않는다. (MUST)
- 큰 작업은 구현 전에 가정, 완료 조건, 검증 방법을 작업 계획에 적고 진행한다. (SHOULD)
- 큰 단계가 끝나면 현재 상태를 바탕으로 다음에 이어갈 엔진 작업을 제안한다. (SHOULD)

## 2. 플랫폼과 렌더링 경계

- Windows와 Web에서 모두 지원 완료로 선언한 기능은 같은 공개 계약과 기능 수준을 유지해야 한다. (MUST)
  현재 구현 순서는 Windows/D3D12 우선이며 Web 스텁은 지원 완료로 간주하지 않는다.
- 플랫폼별 그래픽스 API 의존성은 RHI 뒤에 격리해야 한다. (MUST)
- 현재 공개 Game Framework API는 2D 제작에 집중하되, Core, Renderer, RHI 내부 구조는 향후 3D 확장을 막지 않아야 한다. (MUST)
- Renderer의 `Submit*` API는 프레임 패킷을 수집해야 하며 호출 시점에 RHI 드로우를 실행하지 않아야 한다. (MUST)
  Framework는 자기 차원별 프레임 타입을 Graphics에 넘기지 않고, 명시적인 View 경계 안에서 POD 패킷을
  단건 또는 `ArrayView`로 제출한다. Renderer는 프레임 종료 시 컬링·정렬·배칭과 커맨드 기록을 수행한다.
- 사용자 커스텀 포스트프로세스는 Shader Graph → Shader/Material → PostProcessProfile 흐름으로 제공한다. (MUST)
  사용자용 Shader Graph와 엔진 내부 Render Graph를 분리하며, 게임 스크립트에 Renderer/RHI 또는 임의 GPU
  콜백을 노출하지 않는다. (MUST)
- 정상 렌더 프레임 경로는 일반 힙 할당, 문자열 생성·비교, `WaitIdle` 호출을 하지 않아야 한다. (MUST)
- 2D 스프라이트 정렬은 아이템이 아니라 `(키, 인덱스)` 를 옮긴다. 키는 레이어 순서가 최상위이고,
  그 아래에 부호를 옮긴 `renderOrder` 가 온다. 같은 키일 때만 아이템의 `sourceId` 로 안정화한다. (MUST)
  그리는 순서는 `GetSprite(drawIndex)` 로, 제출된 순서는 `GetSubmittedSprites()` 로 읽는다.
- 2D 스프라이트 패킷(`SpriteSubmit`)과 GPU 인스턴스의 변환은 `Matrix4x4`가 아니라 **아핀 6개 + 깊이 1개**를
  담는 `SpriteTransform2D { float linear[4]; float translation[2]; float depth; }`(28B)로 전달한다. (MUST)
  버텍스 셰이더는 `float4x4`를 조립하지 않고 두 내적으로 위치를 직접 만든다. `MeshSubmit`은 `Matrix4x4`를 유지한다.
  패킷 필드는 D-32 ABI이므로 이후 변경은 Decisions를 거친다. (D-54)
- GPU 인스턴스 레이아웃과 정점 속성 오프셋은 손으로 적지 않는다. 속성 오프셋은 `offsetof`로 구조체에서 끌어오고,
  크기·오프셋은 `static_assert`로 고정한다. (MUST)
  `.hlsl`을 고치면 `Modules/JBroGraphics/Shaders/Compile.ps1`로 생성 헤더를 다시 만들어 함께 커밋한다.
  빌드는 HLSL을 컴파일하지 않는다 — 커밋된 DXIL 덕분에 클론에 셰이더 컴파일러가 없어도 빌드된다. (MUST)
- GPU 읽기 경로(`IRHIDevice::ReadTexture`)는 진단과 테스트 전용이다. 매 프레임 경로에서 부르지 않는다. (MUST)
  프레임이 열려 있는 동안 호출하면 실패해야 하고, 구현하지 않은 백엔드는 `false`를 반환한다.
- 프로젝트 파일은 `.jproject`(YAML)이며 키 이름은 기존 엔진과 같다. (MUST)
  두 번째 형식을 만들지 않는다. 읽지 못하는 구조는 추측하지 않고 줄 번호와 함께 거절한다.
- 스크립트 DLL은 컴포넌트 저장소를 직접 만들지 않는다. (MUST)
  `ScriptRegistry`에 크기·정렬·제자리 생성·파괴만 등록하고 메모리는 호스트 풀이 잡는다.
  `Canvas`는 Tier E라 스크립트 타깃이 보지 못하므로, 기존 엔진처럼 캔버스를 넘겨받을 수 없다.
- `IFramework::Render()`는 `RenderResult { Submitted, NothingToSubmit, Failed }`를 반환하며 호스트는 `Failed`만
  치명 오류로 본다. 렌더 시스템이 없는 Framework는 `NothingToSubmit`을 반환한다. (MUST) (D-49)
- Web 환경 문제로 Windows 쪽 엔진 구조 안정화가 불필요하게 막히지 않도록 작업 순서를 조정할 수 있다. (MAY)

## 3. 모듈 경계와 링크

모듈은 개념 경계이고 DLL은 런타임 이음매다. 둘을 같은 것으로 다루지 않는다.

- 모듈은 각자 빌드 단위(vcxproj)를 가진다. 폴더 분리만으로 모듈을 나눴다고 하지 않는다. (MUST)
- 모듈 공개 헤더는 `Modules/<모듈>/Include/JBro/<이름>/` 아래에 두고,
  참조는 `#include <JBro/<이름>/...>` 형태로만 한다. (MUST)
- 모듈은 자신이 의존 선언한 모듈의 Include 경로만 받는다.
  의존하지 않는 모듈의 헤더를 include하면 컴파일이 실패해야 한다. (MUST)
- 모듈 간 역방향 include와 순환 의존을 만들지 않는다. (MUST)
- **외부 라이브러리는 `<JBro/...>` 규칙의 예외다.** (MUST) (D-60)
  `ThirdParty/` 아래에 소스째로 두고 그 라이브러리가 정한 이름으로 include 한다(`<imgui.h>`).
  래핑하지 않는다 — 래퍼는 라이브러리를 올릴 때마다 같이 고쳐야 하는 두 번째 표면이 된다.
  **어느 모듈이 그것을 보는지는 그대로 통제된다**: 그 모듈의 vcxproj 가 include 경로를
  선언해야 하고, 선언하지 않으면 C1083 이다. 자세한 것은 `source/JBroEngine/ThirdParty/README.md`.
- 공통 모듈에는 2D/3D 차원 개념과 무관한 기능만 둔다.
  공통 계층의 공개 시그니처에 특정 Framework 타입을 노출하지 않는다. (MUST)
  `SystemContext`도 공통 계층이다 — 차원별 시스템 인터페이스는 D-37 확장 블록(`Framework2DSystemContext`)으로 전달한다. (D-43)
- **모듈은 스크립트가 보는 층(Tier S)과 엔진만 보는 층(Tier E)으로 나뉜다.** 한 모듈에 두 층을 섞지 않는다. (MUST) (D-42)
  사용자 비공개는 include 경로가 강제하므로(§10.1), 스크립트 타깃은 Tier S 모듈의 Include 경로만 받는다.
  의존은 Tier E → Tier S 방향만 허용한다. Tier S 모듈이 Tier E 헤더를 include하면 컴파일이 실패해야 한다.

  | 층 | 모듈 | 내용 |
  |---|---|---|
  | Tier S | `JBroCore` | 값 타입·컨테이너·`StableTypeId`·`InstanceIdGenerator` |
  | Tier S | `JBroRuntime` | `ComponentBase`·`GameObject`·`GameObjectHandle`·`Ref<T>`·`GameScriptBase`·`SystemContext`·`ServiceContext`·`ScriptModule`·`Internal/InstanceRegistry` |
  | Tier S | `JBroFramework2D` | 컴포넌트·서비스·`GameScript2D`·`Layer2D` 값 타입·`Internal/ScriptModuleContext`·`ScriptAPI.h` |
  | Tier S | `JBroAssetTypes` | `AssetId`·`AssetHandle`·`AssetMetadata`·`Asset::*` (헤더 전용) |
  | Tier E | `JBroCanvas` | `Canvas`·`Layer`·`GameSystem`·`SystemScheduler`·`Internal::CanvasAccess` |
  | Tier E | `JBroFramework2DSystem` | 2D 시스템·렌더 추출·`Framework2D`(IFramework 구현) |
  | Tier E | `JBroHost` | `EngineInstance`·`IFramework`·`ScriptDLLLoader` |
  | Tier E | `JBroAsset`·`JBroGraphics`·`JBroRHI`·`JBroPlatform`·`JBroD3D12RHI`·`JBroEditor`·`JBroGameHost` | 엔진·호스트 |

  > `GameObject` 는 Tier S다. `ComponentBase`·`GameObjectHandle`·`GameScriptBase` 가 그 정의를 필요로 하고
  > 셋 다 스크립트 DLL 이 링크하기 때문이다. 스크립트가 그 선언을 받지 않는 것은 프렐류드가
  > `GameObject.h` 를 include 하지 않아서이며, 다른 Tier E 타입처럼 include 경로가 막아 주지는 않는다.
  > 이 분리는 단계 1에서 완료했다. 기록은 `tasks/structural-refactor-plan.md` §8·§9다.
- **DLL 경계는 교체하거나 다시 로드해야 하는 곳에만 만든다.** (MUST)
  현재 DLL로 두는 것은 게임 스크립트 하나뿐이며 나머지 모듈은 정적 링크한다.
  DLL 경계는 POD 전달, 소유권 규칙, 수명 순서 같은 비용을 그 API에 영구히 부과하므로
  "나중에 필요할지도 모른다"는 이유로 미리 만들지 않는다.
- RHI는 정적 링크로 시작한다. 두 번째 Windows 백엔드가 실제로 생기면 DLL로 승격한다. (SHOULD)
- 게임 스크립트 핫 리로드를 지원한다. 스크립트 DLL 경계 설계는 이 요구를 전제로 한다. (MUST)

> 참고: 아키텍처 초안 §13은 전 모듈 DLL화의 근거로 프로세스 간 코드 페이지 공유를 든다.
> Windows는 동일 EXE 이미지도 같은 방식으로 코드 페이지를 공유하므로,
> 같은 exe를 여러 개 띄우는 초안 §12 시나리오에서는 이 근거가 DLL 분리를 정당화하지 못한다.
> DLL의 실질 가치는 핫 리로드와 런타임 백엔드 교체다.

## 4. 2D / 3D 경계

배타성은 사용자에게 보이는 API 표면에 적용하고, 엔진 빌드를 나누는 방식으로는 적용하지 않는다.

| 자리 | 배타 | 방법 |
|---|---|---|
| 엔진 설치 · 에디터 | 아니오 | 2D/3D를 모두 포함한다 |
| 사용자 스크립트 프로젝트 | 예 | include 경로에서 반대 Framework를 제거한다 |
| 게임 익스포트 | 예 | 선택된 Framework만 링크한다 |

- 엔진과 에디터 빌드는 2D와 3D를 함께 포함한다. 엔진을 2D판과 3D판으로 나누지 않는다. (MUST)
  제품 플로우가 "엔진 1회 설치 → 새 프로젝트 → 2D/3D 선택 → 생성"이기 때문이다.
- 프로젝트는 2D 또는 3D 중 하나를 선택하며, 선택하지 않은 Framework의 헤더는
  그 프로젝트의 스크립트 타깃 include 경로에 넣지 않는다. (MUST)
- 차원에 종속되는 타입은 각 Framework가 소유한다. `Transform2D`와 `Transform3D`를 하나로 합치지 않는다. (MUST)
- Framework2D와 Framework3D는 서로 직접 의존하지 않는다. (MUST)
- 차원 독립 `Canvas` 본체와 `Layer` 정체성은 공통 Tier E 모듈(`JBroCanvas`)에 한 번만 정의한다. (MUST)
  Framework별 Canvas 복제본을 만들지 않으며, 블렌드·불투명도·공간·패럴랙스·별도 합성 텍스처처럼
  2D 렌더 합성에만 필요한 상태는 `JBroFramework2D`가 소유한다.

> **구현 근거:** `Canvas`와 공통 `Layer`는 한 번만 정의하며, Framework2D는 별도
> `Layer2D`에 2D 합성 상태를 보관한다. Framework3D는 Framework2D를 링크하지 않고 공통 Canvas의
> 오브젝트·컴포넌트·시스템 실행 경계를 사용한다. 3D 렌더 시스템 자체는 아직 후속 구현 대상이다.
> D-42 이전 트리에서는 이 정의가 `JBroRuntime`에 있다.

## 5. 엔진과 게임 코드의 경계

- 네임스페이스 규칙은 §10.1 을 따른다. 스크립트 레이어는 프렐류드가 `using namespace JBro;` 를 한다. (MUST)
- 게임 DLL 이 받는 시스템 집합을 `SystemContext`, 사용자에게 공개되는 서비스 집합을 `ServiceContext` 라 한다.
  둘은 차원과 무관한 공통 Context이며, 대상의 수명을 소유하지 않는다. (MUST)
  호스트가 프로세스 자원을 조립하는 자리는 `EngineInstance` 자체이며 별도의 `EngineContext` 타입을 두지 않는다. (D-53)
  이전 이름 `EngineCore` / `ScriptCore` 와 호스트 네임스페이스 `Core::` 는 쓰지 않는다.
  모듈 이름 `JBroCore` 와 충돌하기 때문이다.
- 차원별 서비스와 시스템은 선택된 Framework가 별도 값 Context로 제공한다. (MUST)
  2D 프로젝트는 `Framework2DServiceContext`(`Physics2DService` 값)와 `Framework2DSystemContext`(`IPhysics2DSystem*`)를
  D-37 확장 블록으로 받는다. 이 타입들을 공통 `ServiceContext`·`SystemContext`에 넣어 공통 계층이 Framework2D를
  참조하게 만들지 않는다. (D-36, D-43)
- **Context에는 시스템과 서비스만 넣는다. 콘텐츠 단위 객체를 넣지 않는다.** (MUST)
  `Canvas`는 장면의 단위, `GameObject`는 액터의 단위이므로 Context에 들어갈 수 없다.
  스크립트가 오브젝트를 다뤄야 하면 `Canvas*` 가 아니라 `Service::GameObjectService` 를 쓴다.
  상태가 없는 것(Math 등)도 Context 슬롯을 차지하지 않는다. 헤더 전용으로 제공한다.
- 호스트는 프로세스 자원 전부를 알지만 **모듈에 통째로 넘기지 않는다.**
  각 모듈에는 필요한 부분집합만 전달한다. (MUST)
  통째로 넘기면 모듈별 include 경계가 런타임에 무의미해진다.

  ```
  EngineInstance          호스트. 프로세스 자원 전부를 소유·조립
      ├─ FrameworkContext            Framework에 주는 부분집합
      ├─ ScriptModuleLoadContext     게임 DLL 로드 시 1회 전달 (POD)
      │    ├─ SystemContext              공통 시스템 — DLL 은 받지만 사용자에겐 보이지 않는다
      │    ├─ ServiceContext             공통 서비스 — 사용자에게 공개
      │    ├─ Registry                   InstanceRegistry* — 호스트 것을 DLL 이 바인딩 (D-44)
      │    └─ Extensions[]               Framework2DSystemContext / Framework2DServiceContext 블록
      └─ (Tier E 내부)                Canvas·Renderer·Platform·RHI·AssetSystem
  ```

- `ServiceContext`에는 게임플레이가 정당하게 필요로 하는 **서비스만** 넣는다. (MUST)
  엔진의 실행 구조나 하드웨어를 조작할 수 있는 것은 넣지 않는다.
  제외 대상: 플랫폼, RHI, 그래픽스/렌더러, 에셋 레지스트리, 프레임워크, `Canvas`,
  그리고 **모든 시스템**(그건 `SystemContext` 몫이다).
- 호스트 전용 여부는 명명 규칙이 아니라 **include 경로와 모듈 경계**로 강제한다.
  스크립트 타깃의 include 경로에 엔진 내부 모듈 헤더를 넣지 않는다. (MUST)
- 호스트와 게임 DLL 경계를 넘는 데이터는 POD 형태여야 한다. `std::string`, `std::vector` 같은 소유권을 가진 STL 타입을 경계 너머로 직접 전달하지 않는다. (MUST)
- DLL에서 만든 객체는 원칙적으로 같은 DLL에서 파괴한다. (MUST)
- 스크립트가 엔진 실 객체에 도달하는 경로는 서비스를 통한다. (MUST)
  `Canvas` 같은 Tier E 구현 타입은 스크립트 헤더에 **선언조차 나타나지 않는다.**
  `GameObject`는 §3의 사유로 Tier S에 있으나 프렐류드가 그 헤더를 include하지 않아 같은 결과가 된다.
  Tier S의 `ComponentBase::GetOwner()`·`GameScriptBase::GetGameObject()`는 `GameObjectHandle`을 반환하며,
  `GameObject*`·`Canvas*`를 돌려주는 접근은 `JBroCanvas`의 `Internal::CanvasAccess`
  (구 엔진 `CCanvasRuntimeAccess` 패턴)에만 둔다. (D-42)
  `ScriptAPI.h`는 각 Framework 모듈의 `Include/JBro/ScriptAPI.h`에 두어 include 경로는 하나, 내용은 차원별이다. (D-18, D-42)
- 스크립트 DLL은 로드 시 호스트의 `InstanceRegistry` 포인터를 받아 자기 정적 링크 사본의 접근점에 바인딩한다. (MUST) (D-44)
  레지스트리는 프로세스 전역이며 캔버스를 모른다. 캔버스 여러 개가 공존해도 핸들·`Ref` 해석은 모호하지 않다.
- 서비스 헤더는 시스템을 **전방 선언만** 하고, 실제 호출은 비인라인 구현(`.cpp`)에 둔다. (MUST)
  인라인으로 두면 시스템 정의가 프렐류드를 타고 사용자에게 노출된다.
- 스크립트 DLL 은 로드 시 `SystemContext`, 공통 `ServiceContext`, 선택된 Framework의 서비스 Context를
  1회 바인딩한다. 핫 리로드 때 모두 다시 바인딩한다. (MUST)
- 활성 프로젝트를 연 호스트만 Framework 서비스 Context를 바인딩한다. 독립 미리보기는 현재 활성 프로젝트의
  바인딩을 바꾸지 않으며, 프로젝트 종료 시 시스템을 파괴하기 전에 바인딩을 해제한다. (MUST)

## 6. 소유권과 객체 안전성

- 엔진 레이어의 고유 소유권에는 `OwnerPtr<T>`를 사용한다. (MUST)
- **엔진 내부 참조는 `SafePtr<T>` 를 그대로 쓴다.** (MUST)
  `GameObject::m_parent` / `m_children` / `m_components`, `ComponentBase::m_owner`,
  풀의 ControlBlock 이 이 위에 서 있다. 핸들로 바꾸지 않는다.
- **핸들은 스크립트 노출 표면에만 적용한다.** (MUST)
  스크립트가 다른 오브젝트를 오래 들고 있는 자리가 실제 댕글링이 나는 곳이고,
  거기만 `GameObject` 핸들로 덮는다.
- **스크립트에 노출하는 참조는 두 종류뿐이다.** (MUST)
  `GameObject`는 16B `GameObjectHandle`로 다루며, `operator->` 없이 안전 멤버만 제공한다.
  컴포넌트 · 스크립트는 24B `Ref<T>`를 쓰고 카테고리는 `T`에서 컴파일타임에 결정한다.
  에셋은 `AssetHandle`, 캔버스는 스크립트에 노출하지 않으므로 `RefCategory::Asset`·`Canvas`는 두지 않는다. (D-53)
  이 둘 외에 타입별 핸들을 추가하지 않는다. 두 크기(16B·24B)는 영구 고정이다. (D-44)
- `GameObjectHandle`·`Ref<T>`는 `SafePtr`와 같이 **메인 스레드 전용**이다. 해석 캐시를 워커에서 갱신하지 않는다. (MUST) (D-54)
- **`GetComponent<T>()` 는 원시 포인터가 아니라 `Ref<T>` 를 반환한다.** (MUST)
  원시 포인터는 저장할 수 없어 매 프레임 다시 찾아야 하고, 그 조회가 선형 탐색이다.
  `Ref<T>` 로 한 번 받아두면 이후 접근이 상수 시간이 된다.
  시스템이 쓰는 `T*` 반환 조회는 `GetComponent`라는 이름을 쓰지 않고 Tier E 내부 접근 클래스의 `FindComponentRaw<T>`로 둔다. (D-42)
- `Ref<T>` 의 접근자는 하나다. 별도의 스코프 객체 타입을 두지 않는다. (MUST)

  ```cpp
  T*   Get() const;                // 무효면 nullptr
  T*   operator->() const;         // Get() 과 같음. Debug 에서 assert + 로그
  bool IsValid() const;
  explicit operator bool() const;  // "설정됨" 만 확인. 해석하지 않음
  ```

  ```cpp
  if (T* p = ref.Get())              // 확인하고 쓴다
  {
      p->Foo();
  }
  ref->Foo();                        // 확인 안 하고 쓴다 — 무효면 크래시
  ```

  **경로가 둘인 게 아니라 접근자 하나에 사용법이 둘이다.**
  `operator->` 는 내부적으로 `Get()` 을 부르며, Debug 빌드 assert 만 차이다.
- 스크립트가 대상의 수명 밖까지 보관하는 멤버에는 raw pointer를 저장하지 않는다. (MUST)
  `GameObject`를 저장하면 `GameObjectHandle`, 그 외 대상을 저장하면 `Ref<T>`를 멤버로 둔다.
  엔진 내부 참조는 이 규칙을 스크립트 핸들로 우회하지 않고 §6의 소유권과 `SafePtr<T>` 계약을 따른다.

  > 이 규칙을 타입으로 강제하려는 시도(복사·이동을 삭제한 스코프 객체)는 채택하지 않았다.
  > C++17 의 보장된 복사 생략 때문에 prvalue 로 멤버를 초기화하는 것이 막히지 않아
  > 실제로 강제가 되지 않는다. 강제되는 것은 콜백 방식뿐인데 문법 비용이 크다.
- **스크립트별 핸들 타입을 코드 생성으로 만들지 않는다.** (MUST)
  생성 전까지 사용자 코드가 컴파일되지 않아, 스크립트를 막 작성한 시점에 편집기가 깨진다.
- 서비스 참조는 **수명 스코프**를 기준으로 다룬다. (MUST)

  | 스코프 | 대상 | 죽는 시점 |
  |---|---|---|
  | Process | 플랫폼, RHI 모듈·디바이스 | 프로세스 종료 |
  | Project | 프레임워크, `Canvas`, 에셋 서비스 | 프로젝트 닫기 |
  | ScriptModule | 스크립트 인스턴스와 스크립트가 소유한 객체 | 핫 리로드 |

- 디바이스 로스트(`DXGI_ERROR_DEVICE_REMOVED`, `VK_ERROR_DEVICE_LOST`, WebGPU `device.lost`)는
  현재 **치명적 오류로 처리하고 종료한다**. 런타임 복구는 구현하지 않는다. (MUST)
  대신 나중에 복구를 넣을 때 영향이 국소적으로 유지되도록
  RHI 디바이스 포인터를 그래픽스 계층 밖으로 노출하지 않는다. (MUST)
  게임플레이·프레임워크·스크립트는 디바이스를 직접 잡지 않는다.

- 자기보다 짧은 스코프의 포인터를 멤버로 캐시하지 않는다.
  캐시가 필요하면 그 스코프의 무효화 이벤트를 구독하고 재획득한다. (MUST)
- 짧은 스코프를 참조하는 코드는 무효화 시 **명시적 재생성**으로 대응한다.
  약참조가 조용히 null이 되어 동작을 건너뛰게 만들지 않는다. (MUST)
  디바이스 로스트의 올바른 처리는 null 검사가 아니라 GPU 리소스 재생성이다.
  약참조는 크래시를 조용한 버그로 바꿀 뿐 안전을 주지 않는다.

### 6.1 `GameObjectHandle`과 `Ref<T>`의 형태와 무효 접근

- `Ref<T>` 의 저장부는 `InstanceRef` 하나다. 별도의 핸들 베이스 타입을 두지 않는다. (MUST)

  ```cpp
  struct InstanceHandle                // 8B. 런타임 위치
  {
      uint32 Slot;                     // 풀 슬롯 인덱스
      uint32 Gen;                      // 세대. 파괴 시 증가한다
  };

  struct InstanceRef                       // 24B · POD · DLL 경계 통과
  {
      InstanceId     ObjectId;         // 영속. 직렬화되는 값
      InstanceId     ComponentId;      // 컴포넌트·스크립트 카테고리만
      InstanceHandle Cached;           // 런타임 캐시. 저장하지 않는다
  };
  ```

  `InstanceId` 와 `InstanceHandle` 은 짝이다 — 전자는 **영속 식별자**,
  후자는 **이번 실행에서의 위치**다. 로드 시 전자로 후자를 채운다(패치업).

  **저장부를 템플릿이 아닌 타입으로 빼는 이유**는 타입을 모르는 코드가 필드에 접근해야 하기
  때문이다. 리플렉션 · 직렬화 · 에디터 인스펙터는 `Ref<Enemy>` 인지 `Ref<SpriteAsset>` 인지
  모른 채 `ObjectId` 를 읽고 쓴다. 필드가 `Ref<T>` 안에만 있으면 그 코드가 `T` 를 알아야 하고,
  타입마다 접근자를 등록하는 코드 생성으로 되돌아간다.

  ```cpp
  InstanceRef* ref = static_cast<InstanceRef*>(propertyAddress);
  ref->ObjectId = loadedId;            // T 를 몰라도 된다
  ```

  기존 엔진도 같은 이유로 이 타입을 리플렉션 헤더(`Reflection/ReflectionTypes.h`)에 두고 있다.
  리플렉션이 모듈로 분리되면 `InstanceRef` 의 소속을 다시 본다.

- `Ref<T>` 는 `InstanceRef` 에 **데이터 멤버를 추가하지 않고 가상 함수를 갖지 않는다.** (MUST)
  둘 중 하나라도 어기면 24B POD 가 깨져 DLL 경계를 넘지 못한다. 테스트로 고정한다.

  ```cpp
  static_assert(sizeof(Ref<ComponentBase>) == sizeof(InstanceRef));
  static_assert(std::is_standard_layout_v<Ref<ComponentBase>>);
  static_assert(std::is_trivially_copyable_v<Ref<ComponentBase>>);
  ```

  활성 Canvas 가 사용하는 레지스트리는 프로세스 전역이며, InstanceId도 프로세스에서 유일하다.
  `WorldHandle`이나 World 레지스트리 같은 중간 조회 계층은 두지 않는다.
- `GameObjectHandle`은 16B `{InstanceHandle, InstanceId}` 값이며
  **자주 쓰는 안전 멤버**를 제공한다. (MUST)
  `Destroy()` / `SetActive()` / `GetComponent<T>()` 등은 무효여도 로그만 남기고
  아무 일도 하지 않는다. `operator->`는 제공하지 않는다.

  ```cpp
  GameObjectHandle target;
  target.Destroy();                            // 안전 멤버. if 불필요
  Ref<Component::Transform2D> transform = target.GetComponent<Component::Transform2D>();
  if (auto* value = transform.Get())            // Ref<T>는 실패를 직접 확인한다
  {
      …
  }
  ```

  GameObject만 안전 명령용 Handle을 쓰며, 컴포넌트·스크립트별 Handle은 만들지 않는다.
- **`GameObjectHandle`에는 `operator->`를 추가하지 않는다.** (MUST)
  안전 멤버 내부에서 대상 해석에 실패하면 즉시 `return`할 수 있어야 한다.
- **안전 경로의 무효 접근은 로그를 남기고 아무 일도 하지 않는다.** (MUST)
  크래시도, 예외도, 절반 실행도 없어야 한다. 사용자가 `if` 를 쓰지 않아도 안전해야 한다.
  Release 빌드에서도 검사(슬롯 범위 + 세대 비교)를 제거하지 않는다.
- 값을 돌려주는 접근은 실패가 드러나야 한다. (MUST)
  대상이 없을 때 기본값을 조용히 돌려주면 찾기 어려운 논리 버그가 된다.
  현재 `GetComponent<T>()`는 `Ref<T>`를 반환하고 사용자가 `Get()` 결과를 확인하는 방식으로
  실패를 드러낸다. 존재하지 않는 값을 대신 만들어 반환하지 않는다.

- 죽은 대상용 가짜 인스턴스를 돌려주지 않는다. (MUST)
  함수가 절반만 실행되어(죽은 적이 점수를 주는 식) 무시보다 나쁜 결과가 된다.
- 핸들을 해석해 얻은 raw pointer 는 장기 보관하지 않는다. 검증 이후 짧은 스코프에서만 쓴다. (MUST)

### 6.2 스크립트 DLL ABI와 핫 리로드 수명

- 게임 스크립트 DLL은 `JBroScriptModule_GetApi`라는 단일 C 심볼만 필수 진입점으로 공개한다. (MUST)
  호스트와 DLL 사이에 C++ 가상 객체를 생성하거나 넘기지 않는다. (MUST)
- `ScriptModuleApi`와 모든 Context 전달 구조체는 standard-layout·trivially-copyable POD여야 한다. (MUST)
  호스트는 모듈 코드를 활성화하기 전에 API ABI 버전, 구조체 크기, 함수 포인터와 필수 Context 요구를
  전부 검사해야 한다. (MUST)
- 공통 `SystemContext`와 `ServiceContext`는 고정 입력으로 전달한다. Framework별 Context는
  `TypeId`, `AbiVersion`, `Size`, `Data`로 구성된 확장 블록으로 전달한다. (MUST)
  각 Framework가 자기 블록의 안정 TypeId와 버전·크기 계약을 소유하며 Runtime은 그 구체 타입을
  참조하지 않는다. (MUST)
- Context 블록의 `Data`는 모듈 `Load` 호출이 끝날 때까지만 빌린다. DLL은 필요한 값을 자기 정적 링크
  사본에 복사해야 하며 포인터 자체를 저장하거나 그 메모리의 소유권을 가져가면 안 된다. (MUST)
- `Load`와 `Unload` 훅은 예외를 DLL 경계 밖으로 내보내면 안 된다. (MUST)
  `Load`가 실패하면 호스트는 같은 모듈의 `Unload`를 롤백 훅으로 호출한 뒤 라이브러리를 해제한다. (MUST)
  정상 언로드도 스크립트 인스턴스와 모듈 Context를 먼저 정리한 뒤 운영체제 DLL 핸들을 해제한다. (MUST)
- 모듈 API 함수 포인터는 로드·재로드·언로드 때만 호출한다. 매 프레임 시스템·서비스 호출을 이
  함수 테이블로 우회하지 않는다. (MUST)
- 스크립트 리플렉션의 필드 연산 계약을 확정하기 전에는, 호스트가 DLL이 소유한 스크립트 객체의
  `Array`/`Table`/`String` 메모리를 C++ 컨테이너 연산으로 직접 재할당·해제하지 않는다. (MUST)
  `BindHeapAllocator`는 현재 `ScriptModuleLoadContext`에 연결되지 않았으며, `String`은 이 할당기를 쓰지 않아
  이 함수만 연결하는 것으로는 경계 안전이 완성되지 않는다.
- Windows는 컴파일러가 출력 DLL을 로드 중에도 교체할 수 있게, 원본과 같은 디렉터리에
  유일한 임시 DLL을 만들어 그 복사본을 운영체제에 로드한다. (MUST)
  의존 DLL 검색 기준을 유지하기 위해 다른 임시 디렉터리로 옮기지 않는다. 로드 실패나 정상
  언로드 후에는 임시 DLL과 네이티브 핸들 래퍼를 모두 정리한다. (MUST)
- 모듈 세대는 활성 모듈의 정체가 바뀌거나 사라질 때 증가한다. 재로드가 실패해도 이전 DLL이 이미
  내려갔다면 세대를 증가시켜 이전 스크립트 슬롯 캐시가 다시 사용되지 않게 한다. (MUST)

## 7. 엔진 서비스

- 선택된 Framework의 최상위 실행 단위는 Runtime `Canvas`이며, `Canvas`가 오브젝트 풀과 타입별
  컴포넌트 풀, 순서를 가진 공통 `Layer` 정체성들을 직접 소유한다. (MUST)
  `World` 같은 중간 계층을 두지 않는다. 수명 계층은 `Canvas` → `GameObject` 하나뿐이다.
- 공통 `Layer`는 식별자(`LayerId` — 단조 증가·재사용 없음·직렬화 값)·이름·표시 여부·에셋 출처(`SourceAssetGuid`)·
  캔버스 전환 승계(`KeepOnCanvasChange`)와 **합성 순서 캐시(`GetOrder()`)**를 갖는다. GameObject의 실행 수명은
  소유하지 않는다. (MUST) 순서 캐시는 `Canvas`가 레이어 생성·파괴·이동 시 재색인하며 그 외에는 쓰지 않는다. (D-46)
  렌더 추출은 레이어 순서를 정렬 키의 최상위로 쓰고 비가시 레이어를 건너뛴다. (MUST)
  불투명도·블렌드 방식·공간·패럴랙스·별도 합성 텍스처·`ScaleMode`·`AnchorToSafeArea`는 2D 렌더 합성
  상태이므로 Framework2D의 `Layer2D`가 소유한다. (MUST) `Layer2D`는 살아 있는 공통 `Layer`에 대해 **지연 생성**되고,
  죽은 `Layer`의 상태는 접근 시 정리된다 — 공통 `Canvas`에 수명 콜백을 추가하지 않는다. (D-41, D-46)
- 별도의 `Scene` 또는 `SceneManager` 실행 계층은 두지 않는다. 이 이름으로 `Canvas`와 중복되는 수명 계층을 다시 만들지 않는다. (MUST)
  금지 대상은 특정 이름이 아니라 **중복 수명 계층 자체**다. 이름만 바꾼 같은 계층도 금지한다.
- Time, Input 같은 핵심 서비스의 수명은 엔진이 소유한다. (MUST)
- 서비스 접근을 위해 매 호출마다 delta time이나 서비스 참조를 전달하는 구조를 기본 방식으로 삼지 않는다. (MUST)
- `Time`, `Input`처럼 소유권과 분리된 전역 접근 지점을 제공할 수 있다. 이 접근 지점이 서비스 수명을 소유해서는 안 된다. (MAY)

## 8. 오브젝트-컴포넌트 모델

**이 모델은 확정이다. ECS 로 바꾸지 않는다.**
기존 엔진(`Engine/GameFramework`)이 이미 이 구조이며, 신규 코드는 여기에 맞춘다.
아키텍처 초안 §6("ECS 자체는 공통 시스템")은 이 절로 대체되었다.

- `GameObject` 는 실체 객체다. **`Entity` 정수 ID 를 도입하지 않는다.** (MUST)
  식별은 객체 자체와 `InstanceId` 로 한다.
- 컴포넌트는 **다형성** `JBro::ComponentBase` 파생이며 타입별 풀(`TObjectPool<T>`)에 거주한다. (MUST)
  메모리 소유는 풀, **논리 소유는 오브젝트**(`Array<SafePtr<ComponentBase>>`)다.
  컴포넌트를 POD 구조체로 만들지 않는다 — 리플렉션과 직렬화가 다형성에 의존한다.
- 부모·자식 계층과 레이어 소속은 `GameObject`의 멤버다. (MUST)
  Transform은 차원별 컴포넌트로 유지한다. 2D는 `Component::Transform2D`,
  3D는 `Component::Transform3D`를 쓰며 `GameObject`는 차원을 알지 않는다. (MUST)
  **월드 변환 캐시는 Transform 컴포넌트 안에 있다.** 로컬과 월드를 별도 컴포넌트로 나누지 않으며 사용자는
  Transform 하나만 붙인다. 월드 필드는 시스템만 쓰고 스크립트에는 읽기만 허용한다. (MUST) (D-47)
- `ComponentBase`의 가상 함수 집합은 `~ComponentBase`·`GetTypeId`·`OnAttached`·`OnDetached`·`OnEnabled`·`OnDisabled`다. (MUST)
  스크립트 DLL이 파생하는 타입의 vtable은 ABI이므로 추가는 Decisions와 D-28 재빌드 규약을 거친다.
  형제 컴포넌트 캐시는 `OnAttached`에서 잡고 `InstanceHandle`과 함께 저장해 프레임 시작에 세대 비교로 검증한다. (D-48)
- `GameObject`는 `m_activeInHierarchy`를 캐시하고 `SetActive`·`SetParent`가 하위 트리에 전파한다.
  `IsActiveInHierarchy()`는 O(1)이다. (MUST) (D-54)
- 시스템은 `ForEach<T>` 로 **타입별 컴포넌트 풀을 순회하며** 갱신한다. (MUST)
  다중 타입 `Query<A,B>` 를 도입하지 않는다.
- **시스템은 `Ref<T>` 를 거치지 않는다.** 순회가 풀의 실체 참조(`T&`)를 그대로 준다. (MUST)
  풀이 살아있는 것만 방문하고 주소가 불변이므로 해석 비용이 0 이다.
  `Ref<T>` 는 "이 오브젝트가 저 오브젝트를 가리킨다" 는 **관계를 저장할 때만** 쓴다.
- **순회 중에 컴포넌트나 오브젝트를 생성·파괴하지 않는다.** (MUST)
  live 배열이 흔들려 바깥 순회가 무효화된다.
  `Canvas`가 순회 깊이 가드(`ScriptIterationGuard`)를 소유하고 `ForEach<T>`와 스크립트 실행 목록 순회에 적용한다.
  순회 중 생성은 즉시 수행하되 실행 목록에는 다음 프레임 반영, 순회 중 파괴는 지연 큐에 넣고
  `FixedUpdate` 묶음 뒤와 `Update` 뒤 두 지점에서 flush한다. (MUST) (D-45)
- 스크립트 실행 순서는 **레이어 합성 순서 → 오브젝트 계층(부모 먼저) → 컴포넌트 부착 순서**다. (MUST)
  목록은 더티 플래그로 지연 재구축하며 트리거는 스크립트 부착/분리·`SetParent`·레이어 생성/파괴/이동이다. (D-45)
- 컴포넌트 타입 ID 는 `MakeStableTypeId(T::StaticTypeName())` 으로 **이름에서 유도한다.** (MUST)
  손으로 배정한 매직넘버를 쓰지 않는다. 이름 기반이라 DLL 경계와 직렬화를 넘어 안정적이다.
- 같은 타입 컴포넌트가 한 오브젝트에 여러 개 있을 수 있다. `InstanceId`로 구분한다. (MUST)
- 컴포넌트 활성 판정은 `IsActiveComponent()` **단일 게이트**를 쓴다. (MUST)
  시스템이 각자 `owner->IsActive` 를 판단하면 시스템 간 불일치가 생긴다(이미 겪은 문제다).
- 객체 풀은 슬롯 주소가 불변이어야 한다(compaction 금지). (MUST)
  캐시된 raw 포인터와 `SafePtr` 가 이 성질에 의존한다.
- `TObjectPool<T>`의 청크 저장소는 생성자에서 받은 `JAllocator`로 할당하고 같은 allocator로
  반환해야 한다. (MUST) 청크 포인터 목록과 free-list 같은 Core 컨테이너의 내부 저장소는
  `Array`의 모듈 로컬 할당 계약을 따르되, 객체 슬롯을 담는 청크 자체는 전달받은 allocator가 소유한다.
- `TObjectPool<T>`의 `SafePtr` ControlBlock은 구 엔진처럼 재활용 목록으로 돌려 쓰고 `Reserve`에서 미리 확보한다.
  정상 스폰 경로에서 ControlBlock `new`는 0회다. `Destroy`의 슬롯 탐색은 청크 베이스 주소 정렬 배열의
  이진 탐색이며 전 슬롯 선형 탐색을 하지 않는다. 세대는 `InstanceRegistry`가 단독으로 관리한다. (MUST) (D-54)

### 8.1 참조와 식별자

- 영속 식별자는 **`InstanceId`(`uint64` 하나)** 다. (MUST)
  비트 배치는 다음과 같다.

  ```
  [ 42비트 타임스탬프(ms) ][ 10비트 세션 난수 ][ 12비트 시퀀스 ]
        139년 분량              머신·세션 구분      ms당 4096개
  ```

  - 시간은 프레임당 1회만 읽어 캐시한다. 생성 비용은 사실상 `++counter` 다.
  - 값이 시간순으로 정렬되므로 `GameObject::m_creationOrder` 를 흡수한다.
  - 세션 난수를 빼면 다른 브랜치에서 같은 ms 에 만든 오브젝트끼리 충돌한다. 반드시 넣는다.
  - 순수 난수보다 해시맵 지역성이 좋고 디버깅에서 생성 시각이 읽힌다.
- `Ref<T>` 는 저장·로드·프리팹·핫 리로드를 넘는 **영속 참조**다. 이 역할은 유지한다. (MUST)
  다만 저장부를 문자열에서 정수로 바꾸고 런타임 캐시를 붙인다.

  ```cpp
  // 현재: char Guid[64] + char ComponentGuid[64] = 128B, strcmp, 매 호출 재해석
  struct InstanceRef
  {
      InstanceId     ObjectId;          // 영속. 직렬화되는 값
      InstanceId     ComponentId;       // 컴포넌트 Ref 만 사용
      InstanceHandle Cached;            // 런타임 캐시. 저장하지 않는다
  };                                    // 24B
  ```

- **로드 직후 `Ref` 를 일괄 패치업한다.** (MUST)
  `InstanceId → { Slot, Gen }` 맵을 1회 만들고 모든 `Ref` 의 캐시를 채운다.
  그래야 프레임 루프에서 식별자 조회가 0 회가 된다.
  런타임에 생성된 대상은 첫 접근에서만 1회 해석하고 이후 캐시를 쓴다.
- 핫 리로드로 스크립트 인스턴스 슬롯이 바뀐 뒤에는 이전 슬롯 캐시를 사용하지 않아야 한다. (MUST)
  현재 코드에는 이 무효화 경로가 아직 없으며 `ScriptAllocationGeneration`이라는 구체 타입명도
  확정하지 않았다. H5의 리플렉션·할당 수명 설계에서 메커니즘을 확정하고 구현하기 전까지는
  핫 리로드 참조 안전성을 완료로 표시하지 않는다.

## 9. 성능

- 매 프레임 도는 경로에 `dynamic_cast`, 힙 할당, 문자열 생성·비교를 두지 않는다. (MUST)
  타입 분기는 정적 디스패치나 타입별 저장소로, 조회는 초기화 시점 캐시로 해결한다.
  **스폰·파괴를 포함한 정상 프레임**이 기준이다. 오브젝트 생성이 힙을 건드리면 위반이다. (D-54)
- 이 계약은 측정으로 고정한다. (MUST) 카운팅 할당기를 `Canvas`·`Renderer`에 주입한 정상 프레임에서 할당 0회,
  `InstanceRegistry` 영속 조회 0회 증가, `TObjectPool::Destroy`의 비교 횟수 상한을 테스트가 단언한다. (D-54)
- 프레임 임시 배열은 `JMemoryContext.frame`(`Canvas::BeginFrame`에서 리셋되는 선형 할당기)을 쓴다.
  `Array`·`Table`의 할당기 정책은 인스턴스를 가질 수 있어야 하며 기본 `HeapAllocator`는 빈 타입으로 유지한다. (MUST) (D-52)
- `Ref<T>::Get()`은 캐시 슬롯이 살아 있고 세대만 다르면 확정 사망으로 단락하며 해시 조회로 떨어지지 않는다.
  `Table<InstanceId, …>`는 항등 해시를 쓴다. (MUST) (D-54)
- 렌더 정렬은 `(uint64 key, uint32 index)` 배열을 정렬하고 아이템은 제자리에 둔다. 키는 `(layerOrder, renderOrder, sourceId)` 패킹이다. (MUST) (D-46, D-54)
- 컴포넌트 저장소의 요소 주소 안정성처럼 상위 코드가 기대는 성질은 계약으로 문서화하고 테스트로 고정한다. (MUST)
- `GameHost`는 정상 렌더 프레임(`Ready`) 뒤에 인위적인 대기를 넣지 않는다. (MUST)
- `GameHost`는 렌더링을 생략한 프레임(`Skipped`) 뒤에 플랫폼 이벤트를 기다리되,
  OS 메시지가 들어오면 즉시 깨고 메시지가 없으면 최대 약 16ms 뒤에 다음 Tick을 수행한다. (MUST)
  고정 `Sleep`은 창 이벤트 응답을 늦추므로 사용하지 않는다.

## 10. 이름과 직렬화

**타입 이름에 접두사를 쓰지 않는다.** 구분은 네임스페이스가 한다. (MUST)
`C` / `A` / `M` / `E` 접두사 체계는 채택하지 않는다 — 읽기 나쁘고 네임스페이스와 역할이 겹친다.

**제품명 `JBro` 를 타입 이름에 붙이지 않는다.** (MUST)
네임스페이스가 이미 그 역할을 하므로 `JBro::JBroHandle` 처럼 겹쳐 읽힌다.
`JBro::InstanceHandle` 로 쓴다.

예외 둘은 유지한다.

- **인터페이스에는 `I` 접두사를 붙인다.** (MUST)
  구현과 이름으로 구분되어야 하고, 기존 엔진 전반이 이미 이 규칙을 쓴다
  (`IAsset`, `IAudioDevice`, `IRHIDevice`, `ISaveStorage`, `IPrefabSpawner`, `IInputHandler`).
- **private 멤버는 `m_` 로 시작한다.** (MUST) 타입 접두사와는 별개 규칙이다.

### 10.1 네임스페이스

모든 JBro 코드는 최상위 `JBro` 네임스페이스 아래에 둔다. (MUST)

| 대상 | 네임스페이스 | 예 |
|---|---|---|
| 컴포넌트 | `JBro::Component` | `Component::Transform2D` |
| 에셋 | `JBro::Asset` | `Asset::TextureAsset` |
| 시스템 (엔진 레이어 로우레벨) | `JBro::System` | `System::TimeSystem` |
| 서비스 (스크립트 레이어 공개) | `JBro::Service` | `Service::TimeService` |
| 그 외 사용자 비공개 | `JBro::Internal` | `Internal::…` |
| 게임프레임워크 · 공통 타입 | `JBro` 직속 | `JBro::GameObject`, `JBro::Canvas`, `JBro::Color` |

- **`JBro::Game` 은 두지 않는다.** (MUST)
  `using namespace JBro;` 이후 사용자 코드의 `namespace Game` 과 충돌한다.
  게임 개발자가 가장 흔히 쓰는 이름이라 위험이 크다. 게임프레임워크 타입은 `JBro` 직속에 둔다.
- **네임스페이스와 같은 이름의 타입을 만들 수 없다.** (MUST)
  `namespace JBro::Component` 와 `class JBro::Component` 는 공존이 불가능하다.
  따라서 컴포넌트 베이스 클래스는 **`ComponentBase`** 로 한다.
  `Asset` / `System` / `Service` / `Internal` 도 같은 이유로 타입 이름으로 쓸 수 없다.
- **시스템과 서비스는 네임스페이스에 이미 나타나더라도 타입 이름 끝에 `System` / `Service` 를 명시한다.** (MUST)
  `using namespace JBro;` 이후 `System::Time` 과 `Service::Time` 이 생기면 읽는 쪽이 헷갈린다.
  접미사가 있으면 `TimeSystem` / `TimeService` 로 항상 구분된다.
- **차원과 무관한 공개 값 타입은 JBroCore의 `JBro` 네임스페이스에 한 번만 정의한다.** (MUST)
  Framework는 동일한 이름의 공개 타입을 재정의하지 않고 Core의 정식 헤더를 include한다.
  정식 타입 이식은 소비자 마이그레이션, 임시 정의 제거, Core와 선택 Framework 공개 헤더의 결합
  컴파일까지 끝나야 완료다. 필드명과 기본값이 다른 임시 타입은 조용히 합치지 말고 각 소비자의
  의도를 확인해 명시적으로 보존한다.
  **벡터·행렬은 이 규칙의 대상이 아니다.** (D-57) `Vec2`·`Rect`·`Matrix3x2` 는 `JBroFramework2D`,
  `Vec3` 는 `JBroFramework3D`, `Matrix4x4` 는 `JBroGraphics` 가 소유한다. 차원이 곧 의미이므로
  차원 독립 타입이 아니며, 2D 프로젝트가 3D 수학을 링크하지 않는다. 이 규칙이 말하는 것은
  `Color` 처럼 차원 의미가 없는 값 타입이다.
- 스크립트 레이어는 네임스페이스를 강제하지 않는다. 프렐류드 헤더(`ScriptAPI.h`)가
  `using namespace JBro;` 를 수행한다. (MUST)
  단 **1 뎁스 네임스페이스 사용을 적극 권장한다** — `Component::Transform2D` 처럼 쓰면
  `JBro` 직속 이름들과 달리 사용자 코드와 충돌하지 않는다.
- 사용자 비공개는 네임스페이스가 아니라 **include 경로**가 강제한다. (MUST)
  스크립트 타깃 include 경로에 없으면 선언 자체가 없어 이름을 쓸 수 없다.
  다만 프렐류드가 전이적으로 끌고 오면 보이므로, 서비스 헤더는 시스템을 **전방 선언만** 하고
  실제 호출은 비인라인 구현(`.cpp`)에 둔다.

### 10.2 이름의 구성 순서

**타입 이름은 `<도메인><차원><역할>` 순서로 짓는다.** (MUST)

- 차원 마커(`2D` / `3D`)는 **도메인 명사 바로 뒤**에 붙인다. 이름 맨 뒤에 두지 않는다.

  | 맞음 | 틀림 |
  |---|---|
  | `Component::Transform2D` | `Component::TransformComponent2D` |
  | `System::Transform2DSystem` | `System::TransformSystem2D` |
  | `System::Camera2DSystem` | `System::CameraSystem2D` |
  | `Component::MeshRenderer3D` | `Component::MeshRenderer` (차원 마커 누락) |

- `Component` 네임스페이스의 컴포넌트 타입에는 `Component` 접미사를 반복하지 않는다. (MUST)
- 역할 접미(`System` / `Service`)는 항상 맨 뒤에 온다. (MUST)
- **같은 도메인의 구조체와 시스템은 앞부분이 정확히 일치해야 한다.** (MUST)
  구조체-시스템 짝이 이름으로 바로 보여야 한다.

  ```
  Component::Transform2D       ↔  System::Transform2DSystem
  Component::Camera2D          ↔  System::Camera2DSystem
  ```

- 차원 마커를 붙일 이유가 없는 타입은 **애초에 Framework 안에 있어야 하는지 검토한다.** (MUST)
  차원과 무관한 개념이면 공통 모듈에 두어야 반대 Framework 가 같은 것을 다시 만들지 않는다.
- 차원별 의미와 저장 계약이 다른 타입은 스크립트 표면에서도 차원 마커를 유지한다. (MUST)
  예: 2D 스크립트는 `Component::Transform2D`를 쓴다.

### 10.3 System 과 Service

**`Manager` 라는 이름은 쓰지 않는다. `System` 과 `Service` 로 나눈다.** (MUST)

| | `JBro::System` | `JBro::Service` |
|---|---|---|
| 제공 대상 | **엔진 레이어** | **스크립트 레이어** |
| 성격 | 로우레벨 API. 업데이트·순회를 수행한다 | 사용자에게 필요한 것만 추린 API |
| 사용자 접근 | **불가** | 가능 |
| 예 | `System::TimeSystem` (Update, 델타 계산·반환) | `Service::TimeService` (델타 반환만) |

- 스크립트 레이어에서 시스템 같은 로우레벨 객체에 직접 접근하는 것을 엄금한다. (MUST)
- 엔진 레이어는 시스템을 직접 써도 된다. (MAY)
- **서비스는 소유 스코프마다 하나의 논리 접근점을 제공한다.** 공통 서비스는 EngineInstance가,
  차원별 서비스는 활성 프로젝트의 Framework Context가 바인딩한다. 서비스 값 객체가 여러 Context에
  복사될 수 있으므로 물리적 C++ 객체가 프로세스에 정확히 하나라는 뜻은 아니다. (MUST)
  컴포넌트는 기능 단위로 나뉘고 여러 개 생성할 수 있다.
- **서비스는 포인터로 넘기지 않는다.** `ServiceContext` 에 값으로 둔다. (MUST)

  ```cpp
  struct ServiceContext { Service::TimeService Time; /* … */ };
  struct Framework2DServiceContext { Service::Physics2DService Physics2D; };
  ```

- 서비스는 결국 시스템 기능을 써야 한다. 시스템 접근은 `SystemContext`(공통) 또는 Framework의 시스템 Context 블록으로 넘긴다. (MUST)

  ```
  ScriptModuleLoadContext          호스트가 조립. DLL 로드 시 1회
      ├─ SystemContext                 공통 시스템 — 게임 DLL 은 받지만 사용자에겐 보이지 않는다
      ├─ ServiceContext                공통 서비스 — 사용자에게 보인다
      ├─ Framework2DSystemContext      2D 시스템 인터페이스 — 확장 블록, 사용자에겐 보이지 않는다
      └─ Framework2DServiceContext     2D 서비스 — 확장 블록, 2D 프로젝트에만 보인다
  ```

  `SystemContext.h` 는 프렐류드가 include 하지 않는다. 서비스 `.cpp` 만 include 한다.
  `SystemContext` 도 DLL 경계를 넘으므로 POD 여야 하고, 그 안의 시스템은 인터페이스 포인터다.
  바인딩은 `ServiceContext` 와 같은 시점에 하고 핫 리로드 때 함께 재바인딩한다.
  Framework별 DLL Context 변환기는 해당 Framework의 `Internal` 경계에 두며 일반 서비스 공개 헤더가
  `SystemContext` 또는 DLL 로더 계약을 끌어오지 않게 한다. (MUST)
- **하나의 타입이 두 역할을 겸하면 분리한다.** (MUST)
  예: 물리는 매 프레임 강체를 순회하는 `System::Physics2DSystem` 과
  스크립트가 부르는 `Service::Physics2DService`(Raycast, Overlap)로 나눈다.
- 두 역할 어디에도 해당하지 않으면 `System` 이나 `Service` 를 장식으로 붙이지 않고
  역할을 그대로 이름에 쓴다. (MUST)
  예: `Renderer`, `SystemScheduler`, `EngineInstance`, `AssetRegistry`.
- 에셋 로드·캐시를 소유하는 프로젝트 수명 객체는 `AssetSystem`, 메타데이터는 `AssetRegistry`, 스크립트 표면은 값형
  `Service::AssetService`다. `AssetManager`는 쓰지 않는다. (MUST) (D-50)

### 10.4 문자열과 이름

- `String`은 `std::string`의 래퍼로 확정한다. 다시 구현하지 않는다. (MUST) (D-51)
- `String`은 POD Context·패킷·`Ref`·핸들·**컴포넌트 공개 필드**에 두지 않는다. (MUST)
  이름·태그는 인턴된 정수(`NameId = MakeStableTypeId(text)`)로 두고 원문은 에디터·직렬화 계층이 보관한다.
- 스크립트 리플렉션 필드의 `Array`/`Table`/`String`은 호스트가 직접 재할당·해제하지 않고 DLL이 제공하는 연산을 통한다. (MUST) (D-51)

- 지속적으로 저장할 데이터는 YAML 또는 바이너리 형식을 우선한다. (SHOULD)

## 11. 검증

- 빌드 성공만으로 기능이 검증됐다고 판단하지 않는다. (MUST)
- 툴체인 버전(`PlatformToolset`, `WindowsTargetPlatformVersion`)은 `JBro.Common.props`에 **명시적으로 고정한다.** (MUST)
  "최신 설치본"으로 두면 머신마다 다른 컴파일러·SDK 로 빌드되고 그 사실이 기록되지 않아, 한쪽에서만 나는 오류를
  재현할 수 없다. 버전을 올릴 때는 Debug/Release 전체 빌드와 테스트로 확인하고 같은 변경에서 고정값을 바꾼다.
- 패키징 검증에서는 실행 파일이 켜진 상태로 유지되는지만 확인하지 않는다. 빌드 매니페스트를 포함한 런타임 설정 전달과 패키지 안의 핵심 동작을 함께 확인한다. (MUST)
- 전체 빌드 파이프라인은 별도 격리가 필요한 경우가 아니라면 임시 smoke project보다 사용자가 지정한 실제 프로젝트로 검증한다. (SHOULD)
- 경계 규칙은 "지켜졌다"를 확인하는 것으로 끝내지 않고, 어길 때 실제로 컴파일이 실패하는지 음성 테스트로 확인한다. (MUST)
- `emcc was not found`는 먼저 Emscripten SDK 환경이 현재 셸에 초기화됐는지 확인한다. 이 메시지만으로 C++ 컴파일 실패라고 결론 내리지 않는다. (MUST)

## 12. Git 커밋 규칙

- 작업 단위마다 커밋한다. 여러 주제를 한 커밋에 섞지 않는다. (MUST)
- 커밋 메시지는 한 줄 요약으로 쓰고 마침표를 붙이지 않는다. (SHOULD)
  요약만으로 근거가 전달되지 않는 변경은 본문에 이유와 검증 결과를 덧붙인다.
- 커밋 메시지에 비밀값·토큰·내부 URL을 적지 않는다. (MUST)
- 커밋 메시지는 영어로 쓴다. 사용자 보고는 한국어로 한다. (MUST)

### 커밋 메시지를 쓸 때

- 타입 접두어를 붙인다. `feat`, `fix`, `refactor`, `test`, `chore`, `docs` 중 하나. (MUST)
  테스트만 추가·수정하는 커밋은 `test`다. 기능 변경에 딸려 오는 테스트는 따로 떼지 않고
  그 변경의 접두어에 포함한다 — §11에 따라 검증은 변경과 같은 커밋에 있어야 한다.
- 무엇을 왜 바꿨는지 적는다. 단순히 "수정", "변경"이라고만 쓰지 않는다. (MUST)

> 이 규칙 이전의 커밋 85개에는 타입 접두어가 없다. 히스토리를 소급해 고치지 않으며,
> 이 규칙 이후의 커밋부터 적용한다.

### 커밋 전

- 빌드와 테스트가 통과하는지 확인한다. (MUST) §11의 검증 결과를 커밋 메시지에 남긴다.
- 의도하지 않은 파일이 스테이징되지 않았는지 diff를 확인한다. (SHOULD)

### 히스토리를 다시 쓰는 명령을 쓸 때 · 강제 푸시가 필요할 때

- 실행 전에 사용자에게 확인을 받는다. (MUST)

### 충돌이 났을 때

- 양쪽 변경 의도를 설명한 뒤 해결안을 제시한다. (SHOULD)

## 13. C++ 코딩 규칙

이 절은 일반 C++ 작성 규칙이다. 소유권·참조 모델의 세부는 §6, 성능 계약은 §9,
이름은 §10이 가지며 충돌하면 그쪽이 우선한다.

- C++17 이상 표준 기능을 우선 쓴다. (SHOULD) 현재 빌드 표준은 C++20이다.
- 컴파일러 경고를 오류로 취급하고 새 경고를 만들지 않는다. (MUST)

### 동적 메모리를 다룰 때

- `new`와 `delete`를 직접 쓰지 않고 `OwnerPtr`, `SafePtr`, JBro 컨테이너를 쓴다. (MUST)
  고유 소유는 `MakeOwnerPtr`, 비소유 참조는 `SafePtr`다. `std::unique_ptr`·`std::shared_ptr`와
  `std::vector`·`std::unordered_map`·`std::string`은 §6·§8의 소유권·수명 계약을 만족하지 않으므로 쓰지 않는다.
- 소유권이 어디에 있는지 주석 없이도 타입으로 드러나게 한다. (SHOULD)

### 헤더 파일을 수정할 때

- 헤더에는 선언만 두고 구현은 소스 파일로 옮긴다. (SHOULD)
  템플릿과 인라인이 불가피한 경우는 예외이며, 서비스 헤더는 §5에 따라 예외를 두지 않는다.
- 불필요한 include를 넣지 않고 전방 선언을 우선한다. (SHOULD)

### 포인터 연산을 할 때 · 배열 인덱스를 직접 다룰 때

- 범위 검사를 넣거나 `ArrayView`처럼 크기를 함께 넘기는 검사 가능한 접근을 쓴다. (MUST)
  `Array::operator[]`의 `assert`는 Debug 전용이므로 외부 입력에서 온 인덱스의 검사로 삼지 않는다.

### 예외 대신 오류 코드를 반환하는 코드베이스일 때

- 기존 방식을 따르고 예외를 새로 도입하지 않는다. (MUST)
  현재 공개 API는 실패를 반환값으로 알리고, 예외는 할당 실패와 스케줄러 오용 같은
  프로그래밍 오류에만 쓴다. 이 경계를 넓히지 않는다.

### 성능을 이유로 최적화할 때

- 측정 결과를 먼저 보여준 뒤 바꾼다. (SHOULD) §9의 카운터 계약이 그 측정 수단이다.

## 14. 관련 문서

- [tasks/structural-refactor-plan.md](../tasks/structural-refactor-plan.md) — 2026-09-12 구조 검토와 D-42~D-55의 근거·단계 계획.
  이 문서의 규칙 중 `(D-42)`~`(D-55)`가 붙은 것은 그 계획의 단계가 끝나기 전까지 현재 코드와 다를 수 있다.
- [tasks/jbroscript-plan.md](../tasks/jbroscript-plan.md) — JBroScript(`.jscript`)와 리플렉션 계획(D-56).
  계획일 뿐 확정 계약이 아니다. 현재 코드에는 프로퍼티 리플렉션이 없다.
- [Jbro Engine Architecture Draft](./Jbro_Engine_Architecture_Draft_v2.md)
- [Jbro C++ Script Object Safety Draft](./Jbro_CPP_Script_Object_Safety.md)

두 문서는 설계 근거와 세부 방향을 설명하는 초안이다. 초안의 모든 내용을 확정된 프로젝트 규칙으로 간주하지 않는다.

두 초안에는 이 문서의 규칙으로 대체된 절이 있다. 해당 절에는 대체 표시를 달아 두었으며,
대체 결정의 근거는 작업 계획 `tasks/todo.md` 의 Decisions 절에 기록한다.
