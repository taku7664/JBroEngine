# 구조 리팩터링 계획 — "다시 갈아엎지 않는" 기준선

> 상태: **확정(2026-09-12)**. P-1~P-12와 §3의 성능 계약은 `tasks/todo.md` Decisions **D-42~D-55**로,
> 규칙은 `docs/ProjectRule.md`에 반영했다. 이 문서는 근거와 단계별 진행 기록으로 유지한다.
> 매핑: P-1→D-42, P-2→D-43, P-3→D-44, P-4→D-45, P-5→D-46, P-6→D-47, P-7→D-48, P-8→D-49,
> P-9→D-50, P-10→D-51, P-11→D-52, P-12→D-53, §3 성능→D-54, `m_owner` 유지→D-55.
> 작성 기준: 2026-09-12 `main`(`986faa9`) 전 소스·문서·테스트와 구 엔진 대응 부분을 직접 대조.
> 실행으로 검증하지 않은 판단은 `[코드 정독]`으로 표시했다.

## 0. 목적과 판단 기준

목적은 **이후에 공개 계약을 깨는 재작업이 생기지 않는 구조**다. 지금 일이 커지는 것은 받아들인다.

무엇을 "지금" 정해야 하는지는 **나중에 바꿀 때의 비용**으로 가른다.

| 바꾸는 비용 | 대상 | 원칙 |
|---|---|---|
| 매우 크다 — 모든 사용자 프로젝트가 깨진다 | 스크립트 DLL이 보는 include 집합, POD Context 레이아웃, `ComponentBase` 가상 함수 집합, 핸들/Ref 크기, 컴포넌트 공개 필드, 렌더 패킷 필드 | **지금 확정.** 미정이면 양쪽이 모두 가능한 쪽으로 |
| 크다 — 엔진 전 모듈을 건드린다 | 모듈 경계·의존 방향, 컨테이너 할당기 정책, 오브젝트 풀 슬롯 구조, Layer 의미 | **지금 확정** |
| 작다 — 한 모듈 안에서 끝난다 | 시스템 내부 알고리즘, 스텁 채우기, 에셋 로더 | 나중에 |

기존 `ProjectRule.md` §1의 우선순위(정확성 → 단순성 → 영향 최소화 → 검증 가능성 → 유지보수성 → 속도)는 그대로다.
"영향 최소화"는 **지금 변경의 크기**가 아니라 **앞으로의 변경 필요를 최소화**하는 것으로 읽는다.

## 1. 현재 구조에서 확인한 결함 요약

자세한 근거는 §5(문서·코드 불일치 표)에 있다. 여기서는 방향에 영향을 주는 것만 적는다.

| # | 결함 | 근거 | 왜 지금인가 |
|---|---|---|---|
| F-1 | `JBroRuntime`이 오브젝트 모델과 호스트 합성 루트를 겸함. 스크립트 DLL이 호스트 코드를 링크하고 `EngineInstance.h` 등이 스크립트 include 경로에 보임 | `JBroRuntime.vcxproj`가 Graphics·RHI·Platform·Asset 참조. `JBro.Script.props`가 `JBroRuntime\Include` 제공 | 스크립트가 보는 include 집합은 가장 바꾸기 비싼 계약 |
| F-2 | 공통 `SystemContext`에 `System::IPhysics2DSystem*`(Framework2D 타입) | `Runtime/SystemContext.h:9-17` | POD Context 레이아웃. 3D 바이너리의 공통 Context에 2D 슬롯이 남는 ABI 오염 |
| F-3 | `InstanceRegistry`가 함수 정적 싱글턴이라 **호스트와 스크립트 DLL에 각각 존재**. DLL 안의 `Ref<T>::Get()`은 빈 레지스트리를 조회 | `Ref.cpp:13`, `ScriptModule.cpp:55-65`(System/Service만 바인딩) | ScriptSystem이 스텁이라 아직 드러나지 않음. 드러나면 참조 모델 전체를 다시 봐야 함 |
| F-4 | 스크립트 파이프라인이 호스트에 연결되지 않음. `ScriptDLLLoader`·`MakeFramework2DServiceContextBlock`은 테스트에서만 호출, `ScriptSystem.cpp`는 빈 파일 | grep | 실행 순서·지연 파괴 계약(OD3)이 정해지기 전에 스크립트 API가 굳으면 재작업 |
| F-5 | `Layer`가 렌더에 영향 없음. `IsVisible()`·`GetLayerIndex()`를 읽는 시스템 0건. `LayerIndex`는 식별자인데 이름은 순서. 구 엔진의 O(1) 합성 순서 캐시가 사라짐 | grep, `Canvas.cpp:163` `m_nextLayer++`, 구 엔진 `CGameLayer::m_index` | 렌더 정렬 키와 직렬화 형식에 들어가는 값의 의미는 나중에 못 바꿈 |
| F-6 | `WorldTransform2D`가 사용자가 직접 붙여야 하는 별도 컴포넌트. 시스템이 오브젝트마다 `GetComponent` 선형 탐색을 매 프레임 2~3회 | `Transform2DSystem.cpp:46,62,100`, 모든 테스트가 쌍으로 Attach | 컴포넌트 공개 표면 = 사용자 프로젝트 계약 |
| F-7 | `IFramework::Render()`가 "제출 없음"과 "실패"를 구분 못 함. `Framework3D::Render()`가 `false`를 돌려 3D 호스트가 첫 프레임에 종료 `[코드 정독]` | `Framework3D.cpp:78`, `EngineInstance.cpp` TickFrame | 인터페이스 시그니처 |
| F-8 | `TObjectPool::Create`가 객체마다 `new ControlBlock`, `Destroy`가 전 슬롯 선형 탐색 O(N). 구 엔진의 ControlBlock 재활용이 이식에서 빠짐 | `ObjectPool.h:210,339`, 구 엔진 `ObjectPool.h:169-182 m_freeBlocks` | 풀 슬롯 구조는 주소 안정성 계약과 묶여 있어 나중에 손대기 어려움 |
| F-9 | 컨테이너 할당기 정책이 무상태(`HeapAllocator::Allocate` static). `JMemoryContext.frame/scratch`가 존재하지만 어떤 컨테이너도 쓸 수 없음 | `Array.h:21`, `Table.h:24`, `Core.h JMemoryContext` | `Array<T, Allocator>` 정책 형태는 전 코드가 의존 |
| F-10 | `EngineContext`·`RuntimeModule`·`RefCategory::Canvas/Asset`·스켈레톤 스모크 3개·`Slot::generation`이 죽은 코드. `AssetManager`·`PrefabSpawner`·`AssetRegistry`는 `{}` 반환 골격 | grep | 죽은 계약이 문서에 살아 있으면 구현자가 그 위에 쌓음 |

## 2. 방향 결정 제안 (P-1 ~ P-12)

각 항목은 **결정 · 근거 · 대안과 기각 이유 · 영향 · 검증**으로 적는다. 확정되면 D-번호를 받는다.

### P-1. 모듈을 "스크립트가 보는 층"과 "엔진만 보는 층"으로 물리 분리한다

**결정.** 사용자 비공개는 include 경로가 강제한다(§10.1)는 규칙을 실제로 성립시키기 위해,
한 모듈 안에 두 층이 섞여 있는 곳을 모듈 단위로 나눈다.

```
스크립트 타깃이 받는 include (Tier S)          엔진·호스트만 받는 include (Tier E)
─────────────────────────────────────────      ──────────────────────────────────────────
JBroCore          Types·Core·StableTypeId      JBroCanvas        Canvas·GameObject·Layer
JBroRuntime       Component·Ref·GameObjectHandle                  GameSystem·SystemScheduler
                  GameScriptBase·ServiceContext                   Internal/InstanceRegistry
                  SystemContext·ScriptModule    JBroHost          EngineInstance·IFramework
JBroFramework2D   Component/*·Service/*                           ScriptDLLLoader·EngineContext
                  GameScript2D·ServiceContext   JBroFramework2DSystem
                  Internal/ScriptModuleContext                    System/*·Rendering/*·Framework2D
                  Layer2D(값 타입만)             JBroGraphics / JBroRHI / JBroPlatform
                  ScriptAPI.h (프렐류드)         JBroAsset(시스템 부분)
JBroAssetTypes    AssetId·AssetHandle
```

- `ScriptAPI.h`는 **각 Framework의 `Include/JBro/ScriptAPI.h`**에 둔다. 경로가 같으므로 D-18의
  "한 줄 include"는 유지되고, 프로젝트 차원 선택이 include 경로로 어느 프렐류드를 쓸지 결정한다.
  Core에서 Runtime을 역참조하던 현재 배치(§3 위반, OD4)가 해소된다.
- `ComponentBase::GetOwner()`·`GameScriptBase::GetGameObject()`는 Tier S에서 **`GameObjectHandle`을 반환**한다.
  `GameObject*`를 돌려주는 접근은 Tier E(`JBroCanvas`)의 내부 접근 클래스(구 엔진 `CCanvasRuntimeAccess` 패턴)로 옮긴다.
  이것으로 OD8("`Canvas` 선언이 스크립트 헤더에 보인다")도 닫힌다 — 스크립트는 `Canvas`·`GameObject` 선언 자체를 받지 않는다.
- 의존 방향: `Tier E → Tier S`만 허용. Tier S 모듈은 Tier E를 include하면 C1083으로 실패해야 한다.

**대안과 기각.**
(a) 모듈은 그대로 두고 프렐류드 음성 테스트만 늘린다 — 사용자가 `<JBro/Runtime/Canvas.h>`를 직접 include하는 것을 막을 수 없다.
막지 못한 API는 나중에 닫을 때 사용자 코드를 깨므로 기각.
(b) 한 모듈에 include 루트를 둘 둔다(`Include/`·`IncludeHost/`) — "공개 헤더는 `Include/JBro/<이름>/`" 규칙과 충돌하고,
vcxproj 하나가 두 개념 경계를 갖게 되어 §3 "모듈은 각자 빌드 단위" 취지에 어긋남. 기각.

**영향.** vcxproj 3개 추가(`JBroCanvas`·`JBroHost`·`JBroFramework2DSystem`), 파일 이동 약 25개, 로직 변경 없음.
`JBro.Script.props`의 include 목록이 Tier S로 축소. 슬루션 구성 매트릭스 갱신.

**검증.** 스크립트 프로브 프로젝트에서 `<JBro/Canvas/Canvas.h>`·`<JBro/Host/EngineInstance.h>`·
`<JBro/Framework2DSystem/...>` include 각각 C1083. 기존 `JBroNegativePreludeProbe` 메커니즘을 확장한다.

### P-2. 차원별 시스템은 확장 블록으로 전달하고 공통 `SystemContext`를 비운다

**결정.** `SystemContext`에서 `Physics2D`를 제거한다(`SystemContextAbiVersion` 3).
Framework2D는 `Framework2DSystemContext`(Tier S `Internal/`, 인터페이스 포인터 값)를 D-37 확장 블록으로 전달한다.
`Physics2DService.cpp`는 `GetFramework2DSystems().Physics2D`를 읽는다.
공통 `SystemContext`에는 차원 무관 시스템(Time·Input 인터페이스 등)만 들어간다.

**근거.** D-36/D-37이 서비스에 적용한 원칙을 시스템에도 동일하게 적용하는 것뿐이다. §3 MUST 위반 해소.

**검증.** `static_assert`로 `SystemContext` 안에 `Framework` 이름이 없는지 확인하는 컴파일 테스트는 불가능하므로,
`ContextBoundaryTests`에서 3D 프로브가 Framework2D 블록 없이 로드 성공하는 것과, 2D 프로브가 블록 누락 시 로드 거부되는 것을 검사.

### P-3. `InstanceRegistry`를 스크립트 DLL에 1회 바인딩한다. 레지스트리는 프로세스 전역·캔버스 무관으로 확정한다

**결정.**
- `ScriptModuleLoadContext`에 `Internal::InstanceRegistry* Registry`를 추가한다(ABI 2).
  Runtime의 `InstanceRegistry::Get()`은 "바인딩된 포인터 반환"으로 바뀐다. 호스트는 프로세스 시작 시 자기 인스턴스를 바인딩하고,
  DLL은 `Load`에서 호스트 것을 받는다. 매 프레임 함수 테이블 호출이 아니라 포인터 1회 바인딩이므로 §6.2와 충돌하지 않는다.
- 레지스트리는 **캔버스를 모른다.** 슬롯은 프로세스 전역이고 `InstanceId`는 프로세스 유일이므로, 캔버스 두 벌(에디터 편집본 + Play 사본)이
  동시에 살아도 해석에 모호함이 없다. D-26의 "다중 캔버스는 명시 파라미터 API로 예외 처리"와 `tasks/todo.md` 보류 절의
  "Canvas 두 벌이 필요해지면 핸들 타입 재검토"는 **재검토 없이 성립**하는 것으로 확정한다. 핸들 16B·Ref 24B는 영구 고정이다.
- "어느 캔버스에 생성하는가"는 해석 문제가 아니라 **서비스 문제**다. `Instantiate`류 서비스는 호출 스크립트의 소유 오브젝트에서 캔버스를 얻는다.

**근거.** 핸들 크기와 해석 경로는 스크립트 저장 필드·직렬화·DLL ABI에 모두 들어가는 가장 비싼 계약이다. 지금 닫는다.

**검증.** 프로브 DLL이 호스트 오브젝트의 `GameObjectHandle`을 받아 `IsValid()`·`GetComponent<T>()`를 성공시키는 테스트.
두 `Canvas`를 만들고 서로의 핸들을 교차 해석하는 테스트(`TestCanvasIdsAreUniqueAcrossCanvases` 확장).

### P-4. 스크립트 실행 순서와 변이 경계는 구 엔진 계약을 그대로 이식한다 (OD3 확정 제안)

**결정.**
- 실행 목록: **레이어 합성 순서 → 오브젝트 계층(부모 먼저) → 컴포넌트 부착 순서.** 구 엔진과 동일.
- 목록은 더티 플래그로 지연 재구축(`MarkScriptExecutionOrderDirty`). 재구축 트리거: 스크립트 부착/분리, `SetParent`, 레이어 이동/생성/파괴.
- 순회 중 생성은 즉시 수행하되 목록에는 다음 프레임 반영, 순회 중 파괴는 `m_pendingDestroy*`에 넣고
  **`FixedUpdate` 묶음 뒤와 `Update` 뒤** 두 지점에서 flush한다. 순회 깊이 가드(`ScriptIterationGuard`)를 `Canvas`가 소유한다.
- 이 가드는 `Canvas::ForEach<T>`에도 적용한다(§8 "순회 중 생성·파괴 금지"의 실제 강제).

**근거.** 구 엔진에서 검증된 계약이고, 신규 트리에 상반되는 요구가 없다. 새로 발명하면 검증 비용만 든다.

### P-5. `Layer`는 식별자와 순서를 분리하고, 순서를 캐시하며, 렌더가 그것을 쓴다

**결정.**
- `LayerIndex` → **`LayerId`**(단조 증가, 재사용 없음, 직렬화되는 값).
- `Layer`에 `std::uint16_t m_order`(합성 순서 캐시) 추가. `Canvas`가 Create/Destroy/Move에서 `ReindexLayers()`. 구 엔진 `CGameLayer::m_index`와 동일한 역할.
- `GameObject`는 `SafePtr<Layer>`와 `LayerId`를 들고, 순서는 `GetLayer()->GetOrder()`로 O(1).
- 렌더 정렬 키는 `(layerOrder, renderOrder, sourceId)`를 하나의 `std::uint64_t`로 패킹한다. 비가시 레이어의 오브젝트는 추출 단계에서 건너뛴다.
- `Layer2D`는 **지연 생성**한다. `GetLayer2D(id)`는 살아 있는 런타임 레이어에 상태가 없으면 기본값으로 만들고, 죽은 레이어면 스테일 엔트리를 지우고 `nullptr`.
  Runtime 공개 계약 변경 없음(D-41 유지).
- 구 엔진 `CGameLayer`의 나머지 필드 귀속을 함께 확정한다: `ScaleMode`·`AnchorToSafeArea` → `Layer2D`,
  `SourceAssetGuid`·`KeepOnCanvasChange` → Runtime `Layer`(차원 중립, 캔버스 전환 승계).

**근거.** 렌더 정렬 키와 직렬화되는 식별자는 나중에 의미를 바꿀 수 없다. 이름(`Index`)과 의미(식별자)가 다른 상태로 굳으면 혼란이 영구화된다.

### P-6. 월드 변환 캐시를 `Transform2D` 안으로 접는다

**결정.** `Component::WorldTransform2D`를 없애고 `Transform2D`에 `world`(행렬)·`worldRotation`·`worldScale`·`worldValid`를 둔다.
사용자는 컴포넌트 하나만 붙인다. 시스템만 월드 필드를 쓴다(스크립트에는 읽기 접근만).

전파 알고리즘은 재귀·`GetComponent` 없이 다음으로 한다.
- `Canvas`가 `hierarchyVersion`(계층 변경마다 증가)을 유지한다.
- `Transform2DSystem`은 버전이 바뀌면 **부모 먼저 순서의 `Transform2D*` 배열**을 1회 재구축한다(주소 안정성 때문에 raw 포인터 보관 가능,
  재구축 시점에만 갱신). 매 프레임은 그 배열을 한 번 선형 순회한다. 조회 0회.

**근거.** D-3("Transform은 컴포넌트")은 유지된다. 구 엔진은 둘 다 오브젝트 멤버였고, 컴포넌트로 옮긴 뒤 굳이 둘로 쪼갤 이유가 없다.
쌍으로 붙이는 API는 사용자 프로젝트에 퍼지면 되돌릴 수 없다.

**대안과 기각.** `Transform2DSystem`이 `WorldTransform2D`를 자동 부착 — 사용자가 보는 컴포넌트 목록에 "내가 안 붙인 것"이 생기고,
인스펙터·직렬화·프리팹 diff에 잡음이 남는다. 기각.

### P-7. `ComponentBase`의 가상 함수 집합을 지금 확정한다

**결정.** 스크립트 DLL이 파생하는 타입의 vtable은 ABI다. 다음 집합으로 고정하고, 이후 추가는 D-28의 재빌드 규약 위에서만 허용한다.

```cpp
class ComponentBase
{
public:
    virtual ~ComponentBase();
    virtual ComponentTypeId GetTypeId() const = 0;
    virtual void OnAttached();      // 소유 오브젝트·캔버스가 확정된 직후. 형제 컴포넌트 캐시 지점
    virtual void OnDetached();      // 풀 반납 직전
    virtual void OnEnabled();
    virtual void OnDisabled();
};
```

`GameScriptBase`의 `OnCreate/OnStart/OnUpdate/OnFixedUpdate/OnDestroy`는 그 위에 얹는다. `OnCreate`는 `OnAttached` 뒤에, `OnDestroy`는 `OnDetached` 앞에 온다.

**근거.** 시스템이 형제 컴포넌트를 매 프레임 찾지 않으려면 부착 시점 훅이 필요하다(§3-P 성능). 훅을 나중에 추가하면 vtable이 바뀐다.

### P-8. `IFramework::Render()`는 세 값을 돌려준다

**결정.** `enum class RenderResult : std::uint8_t { Submitted, NothingToSubmit, Failed };`
호스트는 `Failed`만 치명으로 본다. `Framework3D`는 렌더 시스템이 생길 때까지 `NothingToSubmit`을 돌려 3D 호스트가 정상 실행된다.

### P-9. 에셋 헤더를 값 타입과 시스템으로 나누고 `Manager`를 없앤다 (OD6 확정 제안)

**결정.** `JBroAssetTypes`(Tier S: `AssetId`·`AssetHandle`·`AssetMetadata`·`Asset::*` 태그 타입)와
`JBroAsset`(Tier E: `AssetSystem`(로드·캐시 소유, 프로젝트 수명)·`AssetRegistry`(메타데이터)). 스크립트 표면은 값형 `Service::AssetService`.
`EngineInstance::GetAssetManager` → `GetAssetSystem`.

### P-10. `String`은 `std::string` 래퍼로 영구 확정하고, 경계와 핫 데이터에서는 금지한다 (OD7 일부)

**결정.** `String : public std::string`을 정식으로 인정한다. 잘 검증된 SSO 구현을 다시 만들 이유가 없다.
대신 다음을 규칙으로 둔다.
- POD Context·패킷·`Ref`·핸들에 `String`을 두지 않는다(이미 `is_trivially_copyable` 정적 단언이 잡는다).
- 컴포넌트 공개 필드에 `String`을 두지 않는다. 이름·태그는 인턴된 정수(`NameId = StableTypeId(text)`)로 두고 문자열은 에디터/직렬화 계층이 보관한다.
  `GameObject::m_tag`가 첫 교정 대상이다(§9 "매 프레임 문자열 비교 금지"와도 맞는다).
- 스크립트 리플렉션 필드의 `Array`/`Table`/`String` 편집은 DLL이 제공하는 연산을 통한다(OD7의 기본 제안 채택).

### P-11. 컨테이너 할당기 정책을 상태 보유 가능으로 바꾸고, 프레임 할당기를 실제로 쓴다

**결정.** `Array<T, Allocator>`·`Table<..., Allocator>`의 `Allocator`를 **인스턴스를 가질 수 있는 정책**으로 바꾼다.
기본 `HeapAllocator`는 지금처럼 빈 타입(`[[no_unique_address]]`로 크기 증가 0), 새로 `JAllocatorRef`(`JAllocator` 값 하나를 들고 그것으로 할당) 정책을 추가한다.
`JMemoryContext.frame`은 `Canvas::BeginFrame`에서 리셋되는 선형 할당기로 구현하고, 프레임 임시 배열은 `Array<T, JAllocatorRef>`로 `frame`을 쓴다.

**근거.** `Array`의 템플릿 형태는 전 코드가 의존한다. 기본 인자가 있으므로 기존 코드는 바뀌지 않고, 정책 하나가 추가되는 것이라 지금 넣어도 비용이 작다.
반대로 나중에 넣으려면 그때까지 쌓인 "임시 배열은 힙" 코드를 전부 다시 봐야 한다.

### P-12. 죽은 계약을 지우고 골격은 "미완"으로 명시한다

- 삭제: `EngineContext`(조립 코드 0건 — `EngineInstance`가 사실상 그 역할), `RuntimeModule`/`Runtime.h`, `RefCategory::Canvas`·`Asset`
  (P-3와 P-9에 따라 에셋은 `AssetHandle`, 캔버스는 스크립트에 노출하지 않음), 스켈레톤 스모크 3개(헤더 자립성은 테스트 프로젝트에서),
  `TObjectPool::Slot::generation`(세대는 레지스트리가 관리), `GameObject::m_destroyContext/m_destroyCallback`(`Canvas`가 friend이므로 직접 호출).
- 문서 Audit Snapshot에 명시: `PrefabSpawner`·`AssetRegistry`·`AssetSystem::Load`·`ScriptSystem`은 선언만 있는 골격.

## 3. 성능 계약과 피드백

§9 "매 프레임 경로에 `dynamic_cast`·힙 할당·문자열 생성/비교 없음"을 **측정 가능한 계약**으로 바꾼다.
각 항목은 현재 상태 → 문제 → 확정 방향이다.

### 3.1 오브젝트 풀

| | 현재 | 방향 |
|---|---|---|
| 생성 | 객체마다 `new SafePtrDetail::ControlBlock` | 구 엔진처럼 `m_freeBlocks` 재활용 + `Reserve`에서 블록도 미리 확보. 정상 스폰 경로에서 `new` 0회 |
| 파괴 | `FindSlot` 전 슬롯 선형 탐색 O(N) | 청크 베이스 주소를 정렬 배열로 유지 → 이진 탐색 O(log 청크 수) 후 포인터 차로 슬롯 인덱스. 청크는 파괴되지 않으므로 배열은 `Reserve` 때만 바뀐다 |
| 순회 | 슬롯마다 `alive` 검사 | 유지. 청크 단위 `liveMask`(32비트)로 빈 청크 건너뛰기는 선택 |
| 세대 | `Slot::generation`과 `InstanceRegistry::Entry::Generation` 이중 | 레지스트리 하나로. 풀 세대 삭제 |

### 3.2 컴포넌트 헤더 크기

`ComponentBase` = vptr 8 + `EnableSafeFromThis` 8 + `SafePtr<GameObject>` 16 + `InstanceId` 8 + `InstanceHandle` 8 + `bool` → 약 56B.
`Transform2D` 페이로드(P-6 접은 뒤 약 60B)와 비슷한 크기다. D-1(다형성 컴포넌트)·D-4(SafePtr 불가침)를 유지하는 한 이 헤더는 받아들인다.
줄일 여지가 있는 곳은 `m_owner`(`SafePtr` 16B → 소유 오브젝트는 컴포넌트보다 항상 오래 살므로 논리적으로 raw 8B로 충분)지만,
§6이 `ComponentBase::m_owner`를 SafePtr로 명시하므로 **규칙을 바꾸지 않는 한 유지**한다. 규칙 변경을 원하면 별도 결정.

### 3.3 활성 판정

`IsActiveComponent()` = `m_owner.TryGet()`(컨트롤 블록 역참조) + `IsActiveInHierarchy()`(부모 체인 순회). 컴포넌트마다 매 프레임 O(깊이).

**방향.** `GameObject`에 `m_activeInHierarchy` 캐시. `SetActive`·`SetParent`가 하위 트리에 전파(변경 시에만 O(부분 트리)).
`IsActiveInHierarchy()`는 O(1). 컴포넌트는 `OnEnabled/OnDisabled`(P-7)로 상태 변화를 받는다.

### 3.4 형제 컴포넌트 조회

`SpriteRender2DSystem`·`Camera2DSystem`·`Physics2DSystem`이 오브젝트마다 `canvas.GetComponent<...>(owner)`를 매 프레임 호출한다(선형 탐색).

**방향.** P-7의 `OnAttached()`에서 형제 `Transform2D*`를 캐시(주소 안정). 형제가 분리되면 `OnDetached` 시점에 소유 오브젝트의
다른 컴포넌트에 알리는 대신, **캐시는 `Transform2D`의 `InstanceHandle`과 함께 저장하고 프레임 시작에 세대 비교 1회**로 유효성을 확인한다.
해시 조회가 아니라 배열 인덱스 + 정수 비교다.

### 3.5 참조 해석

- `Ref<T>::Get()` 캐시 히트 = 슬롯 범위 검사 + 세대 비교 + 카테고리 비교. 좋다.
- 캐시 미스(대상 사망)마다 `Table` 해시 조회로 떨어진다. 스크립트가 죽은 적을 매 프레임 `if (ref.Get())`로 확인하면 매 프레임 해시 조회.
  **방향.** 캐시된 슬롯이 살아 있고 세대만 다르면 "확정 사망"으로 단락하고 `Cached`를 비운다. 해시 조회는 캐시가 비어 있을 때(로드 직후·첫 접근)만.
- `Table<InstanceId,...>`의 해시는 `std::hash<uint64>`(MSVC는 바이트 FNV). `InstanceId`는 이미 시간+시퀀스로 분산되어 있으므로 항등 해시로 충분하다.
- `Ref<T>::Get() const`가 `mutable Cached`를 쓴다 — 스레드 안전하지 않다. §6의 "SafePtr는 메인 스레드 전용"과 같은 문장을 `Ref`·`GameObjectHandle`에도 명시한다.

### 3.6 렌더 추출과 패킷

- `RenderWorld2D::Sort`가 약 120B `SpriteRenderItem`을 직접 `std::sort`한다. 65536개 상한이면 매 프레임 수 MB 이동.
  **방향.** `(uint64 key, uint32 index)` 배열을 정렬하고 아이템은 제자리. 키는 P-5의 패킹 값. 8비트 레이어 순서 + 24비트 renderOrder 범위면 기수 정렬로 O(N).
- `SpriteSubmit::world`와 `GpuSpriteInstance::world`가 `Matrix4x4`(64B). 2D 스프라이트에는 아핀 6개 + 깊이 1개(28B)로 충분하다.
  `SpriteSubmit` 약 104B → 약 60B, GPU 인스턴스 80B → 44B. 인스턴스 버퍼 업로드 대역폭이 거의 절반.
  **이것은 패킷 ABI(D-32)라 지금 정해야 한다.** `SpriteSubmit { float affine[6]; float depth; AssetHandle sprite, material; float tint[4]; int32 renderOrder; }`.
  셰이더는 `float3x2 + depth`를 받아 `float4x4`를 조립한다. `MeshSubmit`은 `Matrix4x4` 유지.
- `RenderBridge2D`가 64개 스택 배치로 `Matrix3x2 → Matrix4x4` 변환 후 제출한다. 위 변경으로 변환 자체가 사라진다.
- `Framework2D::Update`가 `deltaTime` 검증 **전에** `m_renderWorld.BeginFrame()`을 호출해 무효 dt에서 빈 프레임이 나간다. 검증을 앞으로.

### 3.7 측정으로 고정할 것

각 계약은 테스트 프로젝트에서 카운터로 고정한다.
- 카운팅 할당기(`JAllocator` 래퍼)를 `Canvas`와 `Renderer`에 주입하고, 1000 오브젝트·1000 스프라이트 정상 프레임에서 **할당 0회**를 단언.
- `InstanceRegistry::GetPersistentLookupCount()`는 이미 있다. 정상 프레임에서 **0 증가**를 단언(D-23의 유닛 테스트를 프레임 단위로 확장).
- `TObjectPool::Destroy` 1000회의 비교 횟수 상한(디버그 카운터).

## 4. 실행 순서

뒤 단계가 앞 단계의 결과 위에서만 안정하도록 배치했다. 각 단계는 Debug/Release 전체 빌드·테스트 통과와 음성 테스트로 닫는다.

| 단계 | 내용 | 닫는 결정 | 완료 조건 |
|---|---|---|---|
| **0** | 이 문서의 P-1~P-12 확정. Decisions(D-42~)와 `ProjectRule.md` 동기화. OD1~8 정리 | 전부 | 두 문서에 충돌 0건 |
| **1 경계** | P-1 모듈 분리 → P-2 SystemContext 정리 → 프렐류드 이동 → P-9 에셋 분리 → P-12 삭제 | P-1, P-2, P-9, P-12 | 스크립트 프로브에서 Tier E include 3종 C1083. 프렐류드 음성 테스트 유지 |
| **2 참조·DLL** | P-3 레지스트리 바인딩 → `EngineInstance`↔`ScriptDLLLoader` 연결 → P-4 실행 순서·지연 파괴 → `ScriptSystem` 실구현 → P-7 훅 | P-3, P-4, P-7 | DLL 안의 `Ref`·핸들 해석 성공. 핫 리로드 후 참조 복구(Success Criteria 기존 항목). 순회 중 파괴가 flush 지점까지 지연됨을 테스트 |
| **3 오브젝트 모델 성능** | 3.1 풀 → P-6 Transform 접기 → P-5 Layer → 3.3 활성 캐시 → 3.4 형제 캐시 → P-11 프레임 할당기 → P-10 태그 정수화 | P-5, P-6, P-10, P-11 | 3.7의 카운터 테스트 전부 통과 |
| **4 렌더 패킷** | 3.6 `SpriteSubmit` 압축·키 정렬 → P-8 Render 결과 → 3D 호스트 1프레임 생존 테스트 | P-8 | `RendererContractTests` 갱신, Debug_Game3D 호스트가 N프레임 실행 후 정상 종료 |
| **5 골격 채우기** | Asset 시스템 → Time/Input 서비스(§7) → PixelPerfect(OD5) → Framework3D 렌더 | OD5 | 각자 별도 계획 |

단계 1은 로직 변경이 없어 가장 안전하고, 이후 모든 단계의 include 경로를 결정하므로 반드시 먼저다.
단계 2와 3은 서로 독립이지만, 3의 형제 캐시(3.4)가 P-7 훅에 의존하므로 2 → 3 순서로 둔다.

## 5. 문서 ↔ 코드 불일치 목록 (이 계획이 해소하는 것)

| 문서 | 코드 | 해소 |
|---|---|---|
| §3 역방향 include 금지 | `JBroCore/Include/JBro/ScriptAPI.h`가 Runtime 헤더 include | P-1 |
| §3 공통 계층 시그니처에 Framework 타입 금지 | `SystemContext.Physics2D` | P-2 |
| §5 스크립트 include 경로에 내부 모듈 헤더 금지 | `JBroRuntime\Include`에 `EngineInstance.h` 등, `JBroAsset\Include`에 `AssetManager` | P-1, P-9 |
| §5 `Canvas`는 스크립트 헤더에 나타나지 않음 | `GameObject.h`가 프렐류드에 포함, `GetCanvas()` 노출 | P-1 |
| §6 `GetComponent<T>()`는 `Ref<T>` 반환 | `Canvas::GetComponent<T>(owner)`는 `T*` | P-1(Tier E 내부 접근 클래스로 이동·개명 `FindComponentRaw`) |
| §6.1 `Ref<Canvas>`·에셋 Ref | 해당 카테고리 등록 코드 0건 | P-12 |
| §7 Layer는 순서를 표현 | 렌더가 순서·가시성을 읽지 않음, `LayerIndex`는 식별자 | P-5 |
| §8 순회 중 생성·파괴 금지·재진입 가드 | 가드 없음 | P-4 |
| §9 매 프레임 힙 할당 금지 | `TObjectPool::Create`의 `new`, `GetComponents<T>()`의 `Array` 반환 | 3.1, P-12(`GetComponents`는 out-param 판만 남김) |
| §10.2 이름 순서 | `MeshRenderer`(3D 마커 없음), `Vec3`·`Quaternion`이 `Framework3D.h` 안 | 단계 1에서 `MeshRenderer3D`, OD2와 함께 Core 수학 타입 확정 |
| §10.3 `Manager` 폐기 | `AssetManager`·`GetAssetManager` | P-9 |
| CLAUDE.md 한 줄 제어문 금지 | `GameSystem.cpp:5-8` | 단계 1 정리 |
| todo Success Criteria "3D 구성은 Runtime Canvas 실행 경계 사용" | 3D 호스트가 첫 프레임 종료 `[코드 정독]` | P-8 |
| todo Audit "Debug_Game3D에 Framework2D 미포함" | 링크는 맞으나 공통 Context에 2D 슬롯 | P-2 |

## 6. 이 계획이 바꾸지 않는 것

- D-1 오브젝트-컴포넌트 모델, D-2 `Canvas` 단일 수명 계층, D-4 `SafePtr` 불가침, D-5 핸들 두 종류, D-9/D-10 `InstanceId`·24B `Ref`,
  D-14 DLL은 스크립트 하나, D-15 2D/3D 배타 위치, D-32 패킷 수집 렌더러, D-37 단일 진입점·확장 블록, D-40/D-41 Canvas·Layer 단일 정의.
- `SafePtr.h` 원본. `Table`(SwissTable)·`Array`의 공개 API.

## 7. 확정 내역 (2026-09-12, 빡대리 "추천 방향으로 설정")

| # | 질문 | 결정 | 이유 |
|---|---|---|---|
| 1 | P-1 모듈 분리 범위 | `JBroCanvas`·`JBroHost`·`JBroFramework2DSystem` 3개 추가. `JBroRuntime` 이름은 스크립트 표면에 남긴다 | 스크립트 작성자가 "Runtime"이라 부르는 것이 바로 그 표면이다. Tier E 모듈은 담는 것의 이름을 따른다 |
| 2 | P-6 Transform 접기 | 접는다 | 쌍으로 붙이는 API는 사용자 프로젝트에 퍼지면 되돌릴 수 없다. D-3(컴포넌트) 유지 |
| 3 | `ComponentBase::m_owner` | `SafePtr` 유지 (D-55) | private 멤버라 나중에 바꿔도 공개 계약이 깨지지 않는다. 지금 §6을 열 이유가 없다 |
| 4 | `SpriteSubmit` 압축 | 지금 한다 (D-54) | 패킷 필드는 ABI. 셰이더·브리지·GPU 버퍼가 같이 바뀌므로 렌더러가 커지기 전이 가장 싸다 |
| 5 | `String` 확정 | `std::string` 래퍼 영구 (D-51) | 검증된 구현을 다시 만들 이유가 없다. 경계·핫 데이터 금지 규칙이 실제 위험을 막는다 |
| 6 | 할당기 정책 시점 | 단계 3 첫 항목 (D-52) | 프레임 임시 배열을 처음 쓰는 직전. 그 전에는 쓸 곳이 없고, 그 뒤로 미루면 힙 임시 배열이 쌓인다 |

## 8. 진행 기록

| 날짜 | 단계 | 내용 | 커밋 |
|---|---|---|---|
| 2026-09-12 | 0 | 구조 검토, D-42~D-55 확정, `ProjectRule.md` 동기화 | — |
