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
| 2026-09-12 | 0 | 구조 검토, D-42~D-55 확정, `ProjectRule.md` 동기화 | `81a42e8` |
| 2026-09-12 | 0 | `Layer2D` 지연 파생 (D-46 일부 선이행) | `ec8dd65` |
| 2026-09-12 | 0 | Windows SDK 버전 고정, §11 규칙 | `f12c658` |
| 2026-09-12 | 0 | §12 Git 커밋 규칙, §13 C++ 코딩 규칙 | `6597769`, `4f02179` |
| 2026-09-12 | 1 | D-53 죽은 선언 제거 (Tier 분리 선행) | `1b4a972` |
| 2026-09-12 | 1 | D-42 표 보정, 단계 1 실행 명세 | `69866a7`, `030e79f` |
| 2026-09-12 | S1-1 | Tier S → Tier E 간선 절단 (destroy 이음매 복원) | `322a12f` |
| 2026-09-12 | S1-2 | `JBroCanvas` 신설 | `931a802` |
| 2026-09-12 | S1-3 | `JBroHost` 신설, `JBroRuntime` 의존을 Core 하나로 | `3d857ce` |
| 2026-09-12 | S1-4 | `JBroFramework2DSystem` 분리 | `d49d22f` |
| 2026-09-12 | 1 | D-43 공통 `SystemContext` 에서 2D 시스템 제거 | `74da2e1` |
| 2026-09-12 | S1-5 | Framework 별 `ScriptAPI.h`, 에셋 분할(D-50), `GetOwner()` 핸들화 | `e87d25c`, `246c5ab`, `d2ab81f` |
| 2026-09-12 | S1-6 | 스크립트 타깃 경계 음성 테스트 3종 | `00c8547` |

| 2026-09-12 | 2 | D-44 호스트 레지스트리를 스크립트 DLL 에 바인딩 | `99c4ec3` |
| 2026-09-12 | 2 | D-48 `ComponentBase` 수명 훅 확정 | `08f2cf5` |
| 2026-09-12 | 2 | D-45 순회 가드와 지연 파괴 (Canvas 측) | `906f748` |

| 2026-09-12 | 3 | 풀 ControlBlock 재활용 + 주소 이분 탐색 (D-54) | `8effb5c` |
| 2026-09-12 | 3 | 상속 활성 캐시 (D-54) | `12a2429` |
| 2026-09-12 | 3 | Transform 월드 캐시 접기 (D-47) | `564ef24` |
| 2026-09-12 | 3 | `LayerId`·순서 캐시·렌더 반영 (D-46) | `adf54a9` |

| 2026-09-12 | 4 | D-49 `RenderResult` 3값, F-7 닫음 | `cb33b7f` |
| 2026-09-12 | 4 | D-54 스프라이트 인스턴스 80B → 44B, D-53 `renderOrder` 제거 | `45f72ff` |
| 2026-09-12 | 4 | 정점 속성 오프셋을 `offsetof` 에 묶음 (m22 대응) | `4e21d97` |

| 2026-09-12 | 5 | §3.6 dt 검증 순서 버그 | `e6e3e72` |
| 2026-09-12 | 5 | P-5 정렬 키 분리 + 타이브레이크 테스트 | `a06ee39`, `cdf3d3b` |
| 2026-09-12 | 5 | D-52 프레임 할당기와 할당기 정책 인스턴스화 | `7db226e`, `80bc19d` |
| 2026-09-12 | 5 | §9.5 공개 헤더 자립성 76 TU | `74d14bb` |
| 2026-09-12 | 5 | §3.4 타입 조회 역참조 1400 → 350 | `d62417d` |
| 2026-09-12 | 5 | D-51 태그 정수화 + DLL 이름표 바인딩 | `87df5ab`, `5fcc0bc` |
| 2026-09-12 | 5 | §9.5 Framework3D 실행 계층 분리 | `b86d0fa`, `58522de` |
| 2026-09-12 | 5 | §10 (A) 호스트 ↔ 스크립트 DLL 배선 | `5756160` |
| 2026-09-12 | 5 | §10 (B) ScriptSystem 실행 순서 (미완 표시) | `dd1e96b` |
| 2026-09-12 | 5 | §9.5 `GameObject` 프렐류드 강제 | `e35a435` |

**단계 1 완료.** §9.4 조건을 모두 확인했다.
**단계 2는 아래 §10 의 두 항목에서 막혀 있어 §10 의 선택지 2로 단계 3에 들어갔다.**

## 10. 단계 2에서 멈춘 지점

§4 의 단계 2는 다섯 항목이다. 앞의 셋은 끝났고 뒤의 둘은 이 리포에 없는 것을 전제한다.

| 항목 | 상태 |
|---|---|
| P-3 레지스트리 바인딩 (D-44) | 완료 `99c4ec3` |
| P-7 컴포넌트 수명 훅 (D-48) | 완료 `08f2cf5` |
| D-45 실행 순서·지연 변이 | **절반.** 순회 가드와 지연 파괴는 `906f748`. 실행 순서 목록은 `ScriptSystem` 없이 검증할 대상이 없다 |
| `EngineInstance` ↔ `ScriptDLLLoader` 연결 | **막힘 (A)** |
| `ScriptSystem` 실구현 | **막힘 (B)** |

### (A) 호스트가 어느 DLL 을 로드할지 정할 근거가 없다

`ScriptDLLLoader` 는 완성돼 있고 실제 DLL 로 교체·재로드까지 검증돼 있다(D-39). 다만 호스트가
그것을 부르려면 **스크립트 DLL 경로**를 알아야 하고, 그 경로는 프로젝트 설정에서 온다.
현재 트리에 프로젝트 파일 형식도 Templates 도 없다(`todo.md` Audit). 임의로 `EngineConfig` 에
경로 문자열을 하나 얹으면 프로젝트 파일 설계를 앞질러 정하게 된다.

### (B) 리플렉션 없이 만든 `ScriptSystem` 은 완료로 칠 수 없다

`todo.md` Open Decision 3 이 못 박고 있다 — "H5의 DLL 리플렉션 생성·파괴 함수 없이 정적 부착
스크립트만 지원하는 구현은 완료로 간주하지 않는다." 리플렉션은 `todo.md` 보류 절에 있다.

지금 만들 수 있는 것은 `Canvas::AttachComponent<T>` 로 정적으로 붙인 스크립트에 대해
레이어 → 계층 → 부착 순서로 목록을 세우고 `OnCreate`/`OnStart`/`OnUpdate`/`OnFixedUpdate`/`OnDestroy`
를 돌리는 것까지다. 사용자 스크립트는 DLL 안에서 이름으로 생성되어야 하므로 그 경로는 열리지 않는다.

### 선택지

1. **(B)만 부분 구현하고 미완으로 표시한다.** 실행 순서와 수명 호출은 지금 서고, 리플렉션이 붙을 때
   생성 경로만 잇는다. OD3 의 문구는 "완료로 표시하지 않는다"는 것이지 "만들지 말라"는 것이 아니다.
2. **단계 2를 여기서 멈추고 단계 3(오브젝트 모델 성능)으로 넘어간다.** ← **선택됨**
3. **리플렉션(H5)을 앞당겨 단계 2 안으로 끌어온다.** 범위가 가장 크고, 보류 결정을 뒤집는 일이다.

## 11. 단계 3 진행 상황

§4 의 단계 3은 일곱 항목이다. 다섯이 끝났고 둘이 남았다.

| 항목 | 상태 |
|---|---|
| 3.1 풀 — ControlBlock 재활용, `Destroy` 주소 탐색 | 완료 `8effb5c` |
| 3.3 활성 캐시 | 완료 `12a2429` |
| P-6 Transform 접기 (D-47) | 완료 `564ef24` — 단, 부모 먼저 순서 배열은 §11.1 |
| P-5 Layer (D-46) | 완료 `adf54a9` |
| 3.4 형제 캐시 | **절반** `01089a4` — 타입 비교의 가상 호출은 제거. 형제 참조 캐시는 §11.2 |
| P-11 프레임 할당기 (D-52) | **남음** §11.3 |
| P-10 태그 정수화 (D-51) | **남음** §11.4 |

### 11.1 Transform 의 부모 먼저 순서 배열

D-47 은 `Canvas::GetHierarchyVersion()` 이 바뀔 때만 `Transform2D*` 배열을 재구축하고 매 프레임은
그것을 한 번 훑는 방식을 적는다. 그러려면 `GameObject::SetParent` 가 Canvas 에 알려야 하는데,
`GameObject` 는 Tier S 이고 `Canvas` 정의를 보지 않는다(§9.1.1). destroy 와 같은 함수 포인터 이음매를
하나 더 놓는 것이 방법이지만, 이음매를 늘리는 판단이라 확인이 필요하다.
현재는 루트에서 내려가는 재귀이며 노드당 조회는 접기 전의 절반이다.

### 11.2 형제 참조 캐시

`Camera2D`·`SpriteRenderer2D`·`Rigidbody2D`·`Collider2D` 가 같은 오브젝트의 `Transform2D` 를
직접 들면 조회가 0 이 된다. 다만 네 컴포넌트의 레이아웃에 참조가 하나씩 붙고, 정확성이
부착 순서에 걸린다 — 스프라이트가 먼저 붙고 Transform 이 나중에 붙는 경우를 지연 채움으로
처리해야 한다. `01089a4` 로 스캔 자체가 정수 비교가 됐으므로 급하지 않다.

### 11.3 프레임 할당기 (D-52)

`Array`/`Table` 의 할당기 정책을 인스턴스 보유형으로 바꾸고 `JAllocatorRef` 를 추가한 뒤,
`JMemoryContext.frame` 을 `Canvas::BeginFrame` 에서 리셋되는 선형 할당기로 구현해야 한다.
정책 타입을 바꾸는 일이라 `Array`·`Table` 의 모든 인스턴스화에 영향을 준다.
**지금 프레임 임시 배열을 쓰는 곳이 없다** — 쓰기 직전에 넣자는 것이 D-52 의 시점 결정이었으므로,
그 소비자가 생길 때(렌더 정렬 키 배열, 스크립트 실행 목록) 함께 넣는 것이 맞다.

### 11.5 변이 검증 기록

§11 의 변경들이 실제로 지탱하는지, 테스트가 실제로 물어뜯는지를 코드를 일부러 깨뜨려 확인했다.
**통과는 증거가 아니다** — 통과하는 테스트가 아무것도 검사하지 않을 수 있고, 이 표의 두 항목이
처음에 정확히 그랬다.

| 깨뜨린 것 | 결과 |
|---|---|
| `DestroySlot` 이 참조 남은 블록도 재활용 | 스위트가 **행**. 프로세스 강제 종료 |
| `FlushPendingDestroy` 의 만료 검사 제거 | **ACCESS_VIOLATION**. 단, 테스트를 두 번 고친 뒤에야 이 경로를 밟았다 |
| `RefreshActiveInHierarchy` 의 자식 재귀 제거 | `disabling an ancestor must reach every descendant` |
| `SetParent` 의 캐시 갱신 제거 | `reattaching under an inactive parent must recompute it again` |
| `MoveLayer` 의 `ReindexLayers` 제거 | `moving a layer must reindex every layer's order` |
| `DestroyLayer` 의 `ReindexLayers` 제거 | `destroying a layer must close the gap it left in the order` |
| `CacheTypeId` 를 no-op 으로 | `single component lookup must return the first matching component` |
| 정렬 키에서 레이어 순서 제거 | `layer order must outrank renderOrder when sorting` |
| 풀의 ControlBlock 재활용 비활성화 | `spawning inside the reserved capacity must not allocate at all` |
| `DestroyComponent` 가 순회 중에도 즉시 파괴 | `components may not leave their owner before the safe point` |
| `FlushPendingDestroy` 가 컴포넌트 큐를 안 비움 | `flushing must detach every queued component` |
| `Transform2DSystem` 이 `worldValid` 를 안 세움 | `default systems must collect all sprites` |
| 부모 월드를 곱하지 않음 (계층 무시) | `parent rotation must affect child world x` |
| 스크립트 DLL 에 레지스트리 바인딩 제거 | `a loaded script DLL must resolve references through the host registry` |

| `NothingToSubmit` 을 버리지 않고 `EndFrame` 까지 흘림 | `nothing to submit must report a skipped frame, not a failure` |
| `Framework3D::Render()` 가 다시 `Failed` | `a 3D project must keep ticking even though its renderer submits nothing` |
| 카메라 없는 월드를 `Submitted` 로 보고 | `empty reopened project must report nothing to submit, not success` |
| 제출 실패를 `Submitted` 로 삼킴 | `collection overflow must reach the host` |
| 아핀의 2x2 부분을 전치 | `uploaded affine must apply flip, size, pivot and rotation...` |
| 이동 성분 x·y 교환 | 위와 같은 단언 |
| 인스턴스 속성 3개 → 2개 (셰이더가 선언한 틴트를 빼먹음) | `D3D12 renderer must initialize` (PSO 생성 실패) |
| `SpriteTransform2D` 에 패딩 float 추가 | **빌드 실패** (`static_assert`) |
| 틴트 속성 오프셋 28 → 24 | **잡히지 않음** — §12.4 참조 |

| dt 검증을 BeginFrame 뒤로 되돌림 | `a non-finite delta time must not blank the collected frame` |
| 정렬 키에서 부호 옮김 제거 | `render-world sprites must sort by render order` |
| 정렬 키에서 레이어 비트 제거 | `layer order must outrank renderOrder when sorting` |
| 동률 시 `sourceId` 비교 제거 | `sprites with an identical key must order by sourceId` |
| `GetSprite` 가 순열을 건너뜀 | `collection must be sorted before submission` |
| 재해싱이 할당기 정책을 잃음 | `the table must have drawn from the arena` |
| 호스트가 프레임 아레나를 되감지 않음 | `every frame must rewind the arena before the framework runs` |
| 되감기가 넘친 블록을 회수하지 않음 | `rewinding must hand the overflow block back to the heap` |
| 대입이 원본 할당기를 가져옴 | `assigning from an arena-backed container must keep the destination allocator` |
| 아레나 넘침 검사 제거 | `the overflow must be counted` |
| 슬롯의 타입 id 를 0 으로 | `single component lookup must return the first matching component` |
| 조회가 다시 후보마다 역참조 | `a type lookup must dereference the component it wants, not the ones before it` |
| `NameTable` 이 원문을 보관하지 않음 | `the table must give the text back` |
| `SetTag` 가 인턴하지 않음 | `an object must give back the name it was created with` |
| DLL 에 이름표 바인딩 제거 | `a loaded script DLL must resolve names through the host name table` |
| 공개 헤더에서 자기 include 제거 | **빌드 실패** (자립성 TU) |
| 스크립트 정렬에서 계층 깊이 제거 | `scripts must run parents before children inside one layer` |
| 스크립트 정렬에서 레이어 제거 | 위와 같은 단언 |
| 시작 목록에 기록하지 않음 | `an already started script must not be started again` |
| 시작 목록을 솎지 않음 | `a destroyed script must not keep a slot in the started list` |
| 스크립트 풀 판정을 항상 거짓으로 | `every active script must run once per update` |

**44개 중 43개가 겨냥한 단언에서 잡혔다.** 엉뚱한 곳에서 터진 것은 없다.
잡히지 않은 하나는 §12.4 에 적었고, 그 실수를 쓸 수 없게 코드를 바꿨다.

**테스트가 스스로 틀렸던 두 번.** 부모·자식 지연 파괴 테스트는 "부모가 먼저 파괴되어 자식 항목이
만료된 경로"를 덮는다고 주석에 썼지만 덮지 않았다. 큐는 LIFO 이고 순회 순서를 정하는 것은
오브젝트 생성 순서가 아니라 **컴포넌트 부착 순서**여서, 언제나 자식이 먼저 나갔다.
생성 순서를 뒤집어 고치려 한 첫 시도도 같은 이유로 실패했다. 부착 순서를 뒤집고 나서야
변이가 크래시로 잡혔다.

### 11.4 태그 정수화 (D-51)

`GameObject::m_tag` 가 `String` 이다. 규칙대로면 `NameId` 정수만 남기고 원문은 에디터·직렬화
계층이 보관한다. 그런데 **이 트리에는 그 계층이 없다.** 지금 바꾸면 `CreateObject("이름")` 의 이름이
어디에도 남지 않아, 디버깅과 테스트에서 오브젝트를 가리킬 수단이 사라진다.
태그는 현재 매 프레임 경로에서 읽히지 않으므로 §9 위반도 아니다.
에셋·직렬화 계층이 생길 때 함께 옮기는 것이 순서다.

## 9. 단계 1 실행 명세

D-42를 구현하기 위한 작업 단위다. 로직 변경은 없고 파일 이동·include 재작성·빌드 단위 추가가 전부다.
각 커밋은 Debug/Release x64 리빌드와 `JBroTests` 통과, 그리고 `Debug_Game2D`·`Debug_Game3D` 링크로 닫는다.

### 9.1 착수 전 확정해야 하는 것

구현 착수 시점에 D-42의 모듈 표가 두 군데 틀렸음이 드러났다. 하나는 확정했고 하나는 남아 있다.

**(a) `Internal/InstanceRegistry` → Tier S. 확정됨.**
`Ref<T>::Get()`과 `GameObjectHandle::Resolve()`가 레지스트리를 호출하고 이 둘은 스크립트 DLL이 링크한다(D-44).
레지스트리가 Tier E면 DLL이 링크되지 않는다. `Canvas`는 등록·해제하는 쪽이고 Tier E → Tier S 방향이라 문제없다.
D-42와 `ProjectRule.md` §3 표를 이에 맞게 고쳤다.

**(b) `GameObject`는 Tier S다. 남은 것은 그 대가를 받아들일지 확인하는 것뿐이다.**

Tier S로 남는 세 파일이 `GameObject`의 **정의**를 필요로 한다.

| Tier S 파일 | 필요한 것 |
|---|---|
| `Source/Component.cpp` | `owner->IsActiveInHierarchy()`, `owner->SafeFromThis()` |
| `Source/GameObjectHandle.cpp` | `object->RequestDestroy()`, `SetActive()`, `IsActiveInHierarchy()`, `FindComponentReference()` |
| `Source/GameScriptBase.cpp` | `GameObject*` 반환 |

셋 다 스크립트 DLL이 링크해야 하므로 **`GameObject`는 Tier S다.** D-42 표는 Tier E로 적고 있다.
Tier E에 남는 것은 `Canvas`·`Layer`·`GameSystem`·`SystemScheduler`다.

대안은 실질적으로 없다. `GameObject`를 Tier E에 두려면 `ComponentBase`·`GameObjectHandle`·`GameScriptBase`가
모두 함수 포인터 간접 계층을 거쳐야 하는데, 그것은 오브젝트 모델 전체를 인디렉션으로 덮는 일이라
D-1의 실체 객체 모델과 §9의 성능 계약에 함께 어긋난다.

**받아들여야 하는 대가는 강제 수단이 한 단계 약해진다는 것이다.**
D-42의 목표("스크립트는 `GameObject` 선언 자체를 받지 않는다")는 여전히 성립한다 —
`ScriptAPI.h`가 `GameObject.h`를 include하지 않고 `GetOwner()`가 `GameObjectHandle`을 반환하면
선언이 사용자 TU에 들어오지 않는다. 다만 `GameObject`에 한해 그 보장은 §10.1의 "include 경로가 강제한다"가
아니라 **프렐류드 구성**이 한다. 사용자가 `<JBro/Runtime/GameObject.h>`를 직접 적으면 컴파일된다.
다른 Tier E 타입은 경로 자체가 없어 `C1083`으로 막힌다.

### 9.1.1 destroy 이음매

`GameObject`가 Tier S면 남는 Tier S → Tier E 간선은 하나다.

```
GameObject::RequestDestroy()  →  m_canvas->DestroyObject(this)
```

선언을 Tier S 헤더에 두고 정의를 Tier E `.cpp`에 두면 스크립트 DLL에 미해결 심볼이 남는다.
따라서 함수 포인터 이음매가 필요하다 — `1b4a972`에서 "죽은 간접 계층"으로 판단해 제거한
`m_destroyContext` / `m_destroyCallback`이 정확히 그것이다. D-53 기준으로는 옳은 제거였지만
D-42 기준으로는 일렀다. 되살리되 죽은 코드가 아니라 **Tier 경계 이음매**로 이름과 주석을 붙인다.

### 9.2 커밋 분할

| # | 커밋 | 내용 | 파일 이동 |
|---|---|---|---|
| S1-1 | `refactor:` | **Tier S → Tier E 간선 절단.** `GameObject`에 destroy 이음매 복원(9.1.1), `GameObject::GetCanvas()`를 Tier E 내부 접근 클래스로 이동. 이후 `GameObject.cpp`는 `Canvas.h`를 include하지 않는다 | 없음 |
| S1-2 | `refactor:` | `JBroCanvas` 신설. `Canvas`·`Layer`·`GameSystem`·`SystemScheduler` 이동(9.3) | 헤더 4 · 소스 4 |
| S1-3 | `refactor:` | `JBroHost` 신설. `EngineInstance`·`IFramework`·`ScriptDLLLoader` 이동 | 헤더 3 · 소스 2 |
| S1-4 | `refactor:` | `JBroFramework2DSystem` 분리. `System/*`·`Rendering/*`·`ScriptSystem`·`Framework2D` 클래스 이동 | 공개 헤더 7 · 소스 트리 11 |
| S1-5 | `refactor:` | `Asset.h`를 `JBroAssetTypes`(값)와 `JBroAsset`(`AssetSystem`)로 분할, `ScriptAPI.h`를 각 Framework 소유로 이동, `GetOwner()`가 `GameObjectHandle` 반환 | — |
| S1-6 | `test:` | Tier E include 음성 테스트 3종, 헤더 자립성 테스트(기존 스켈레톤 스모크 3개를 대체) | — |

S1-1은 파일을 옮기지 않는다. 그 간선 하나가 남아 있으면 S1-2가 순수 파일 이동이 되지 못하므로 먼저 떼어낸다.

S1-4에서 `System/IPhysics2DSystem.h`는 **옮기지 않는다.** Tier S의 `Physics2DService.cpp`가 이 인터페이스로
호출하고, D-43의 `Framework2DSystemContext`가 이 포인터를 담으므로 Tier S에 남는다. 구현
`Physics2DSystem`만 Tier E로 간다. 소스 트리 11개는 `.cpp` 9개와 내부 헤더 2개
(`RenderBridge2D.h`, `Physics2DGeometry.h`)다.

### 9.3 S1-2 파일 이동표

`vcxproj`가 `Include\**\*.h` · `Source\**\*.cpp` 와일드카드를 쓰므로 파일 목록 편집은 필요 없다.
새 `vcxproj` 하나와 `.slnx` 등록, 소비자의 `ProjectReference`·`AdditionalIncludeDirectories`만 추가한다.

| 이동 전 | 이동 후 | 외부 참조 |
|---|---|---|
| `JBroRuntime/Include/JBro/Runtime/Canvas.h` | `JBroCanvas/Include/JBro/Canvas/Canvas.h` | 12곳 |
| `JBroRuntime/Include/JBro/Runtime/Layer.h` | `JBroCanvas/Include/JBro/Canvas/Layer.h` | 1곳 |
| `JBroRuntime/Include/JBro/Runtime/GameSystem.h` | `JBroCanvas/Include/JBro/Canvas/GameSystem.h` | 5곳 |
| `JBroRuntime/Include/JBro/Runtime/SystemScheduler.h` | `JBroCanvas/Include/JBro/Canvas/SystemScheduler.h` | 2곳 |
| `JBroRuntime/Source/{Canvas,Layer,GameSystem,SystemScheduler}.cpp` | `JBroCanvas/Source/` | — |

`JBroCanvas`의 의존은 `JBroCore`, `JBroRuntime`, `JBroAsset`이다. 소비자는
`JBroFramework2D`(분리 후 `JBroFramework2DSystem`), `JBroFramework3D`, `JBroEditor`, `JBroGameHost`, `JBroTests`다.

### 9.4 단계 1 완료 조건 — 전부 확인됨 (2026-09-12)

- [x] 스크립트 프로브가 `<JBro/Canvas/Canvas.h>`, `<JBro/Host/EngineInstance.h>`,
  `<JBro/Framework2DSystem/Framework2D.h>` 를 include하면 각각 `C1083`으로 실패한다.
  `/p:JBroTierProbe=Canvas|Host|FrameworkSystem` 으로 실행해 세 건 모두 확인했다.
- [x] 스크립트 프로브 DLL이 `JBroCore`·`JBroRuntime`·`JBroFramework2D`만 링크하고 빌드된다.
  `JBroAssetTypes` 는 헤더 전용 Utility 라 링크 대상이 없다.
- [x] Debug/Release x64 전체 빌드 경고 0, `JBroTests` 통과, `Debug_Game2D`·`Debug_Game3D` 링크 성공.
- [x] Tier S → Tier E include 0건. Tier S 는 `JBroCore`·`JBroRuntime`·`JBroFramework2D`·`JBroAssetTypes`
  서로만 참조한다.

### 9.5 단계 1에서 남긴 것

완료로 표시하지 않는다. 다음 단계에서 처리한다.

- **헤더 자립성 테스트.** D-53 은 스켈레톤 스모크 3개를 테스트 프로젝트로 옮기겠다고 했지만,
  한 번역 단위는 첫 include 하나의 자립성만 증명한다. 현재는 모듈 로컬 스모크 3개가 그대로 남아 있다.
  공개 헤더마다 번역 단위를 두는 방식으로 다시 설계해야 한다.
- **Framework3D 의 실행 계층 분리.** 3D 는 아직 시스템이 없어 한 모듈이며, 컴포넌트 헤더만 갈라 두었다.
  `Framework3D.h` 가 같은 모듈에 있으므로 3D 스크립트 타깃은 경로상 그것을 볼 수 있다.
  시스템이 생기는 시점에 2D 와 같은 방식으로 나눈다.
- **`GameObject` 의 비공개 강제.** 9.1(b) 에서 받아들인 대로 프렐류드 구성이 지키며, include 경로가
  막지는 못한다. 사용자가 `<JBro/Runtime/GameObject.h>` 를 직접 적으면 컴파일된다.

## 12. 단계 4 — 렌더 패킷 ABI (2026-09-12)

§4 의 단계 4다. 둘 다 ABI 라 지금 확정했다.

### 12.1 `IFramework::Render()` → `RenderResult` (D-49, F-7)

`bool` 은 "그릴 것이 없다"를 표현할 수 없었다. 그래서 두 구현이 반대로 거짓말했다.

- `Framework3D::Render()` 는 `false` 를 돌려줬고 호스트는 **첫 프레임에 종료**했다.
- 카메라 없는 2D 프로젝트는 `true` 를 돌려줬고 호스트는 **아무 뷰도 쓰지 않은 백버퍼를 제시**했다.

`RenderResult { Submitted, NothingToSubmit, Failed }` 로 나눴다. `NothingToSubmit` 은 상태이지
오류가 아니다 — 호스트는 그 프레임을 버리고(`AbortFrame`) `FrameStatus::Skipped` 로 계속 돈다.
`Failed` 의 처리는 종전과 같다.

**실행으로 확인.** `Debug_Game3D` 호스트를 실제로 띄웠다.

| | 4초 뒤 | 창 닫기 후 |
|---|---|---|
| 고치기 전 (`Render()` 가 `Failed`) | 이미 스스로 종료 | `exit=4` |
| 고친 뒤 | 생존, CPU 375ms (틱이 실제로 돌고 있다) | `exit=0` |

`Debug_Game2D` 도 같다 (생존, CPU 453ms, `exit=0`).

### 12.2 스프라이트 인스턴스 패킷 80B → 44B (D-54, D-53)

2D 변환이 `Matrix4x4` 열여섯 개 중 여섯 개만 채우고 나머지 열 개는 고정된 z·w 행이었다.
버텍스 셰이더는 그 고정값과 매 정점을 성실히 내적하고 있었다.

`SpriteTransform2D { float linear[4]; float translation[2]; float depth; }` (28B) 로 접었다.

| | 전 | 후 |
|---|---|---|
| `GpuSpriteInstance` | 80B | **44B** (-45%) |
| 인스턴스 정점 속성 | 5개 (Float4 x5) | 3개 (Float4, Float3, Float4) |
| VS 내적 | 4회 | 2회 |
| `RenderBridge2D` | `Matrix3x2` → 패딩된 4x4 확장 | 여섯 값 직접 대입 |

`SpriteSubmit::renderOrder` 도 함께 없앴다. 렌더러는 그걸 한 번도 읽지 않았고,
레이어 합성과 정렬은 `RenderWorld2D` 가 제출 전에 끝낸다 (D-53).

`depth` 는 지금 항상 0 이다. 깊이 버퍼가 없으므로 그리는 순서가 여전히 정렬 결과다.
`depth` 를 살리는 것(깊이 버퍼 부착, 2D/3D 혼합)은 파이프라인 변경이라 별도 판단이 필요하다.
**단 그것은 ABI 를 다시 깨지 않는다** — 자리를 지금 비워 뒀다.

### 12.3 셰이더 재컴파일 경로 — 빌드에 연결돼 있지 않았다

착수 전 확인한 사실이다.

- `.vcxproj` 어디에도 HLSL 컴파일 단계가 없다. `BuiltinSprite{VS,PS}.generated.h` 가
  DXIL 바이트 배열로 커밋돼 있고 `Renderer.cpp` 가 그걸 `#include` 한다.
- 커밋된 blob 은 dxc 1.9 (private build) 산출물이었다. 이 기계에는 1.6(SDK 22621)과
  1.8(SDK 26100)만 있다.
- 재생성 스크립트도 없었다.

**그대로 두되 재현 경로를 문서화했다.** 빌드가 셰이더를 컴파일하지 않는 것은 장점이다 —
클론에 셰이더 컴파일러가 없어도 빌드된다. 대신 `Modules/JBroGraphics/Shaders/Compile.ps1` 을
추가해, `JBro.Common.props` 와 같은 SDK(10.0.22621.0)로 고정해 재생성한다.
`.hlsl` 을 고치면 이 스크립트를 손으로 돌리고 생성 헤더를 함께 커밋한다.

### 12.4 남은 검증 구멍 — 정직하게

**변이 m22 는 잡히지 않았다.** 틴트 정점 속성의 오프셋을 28 → 24 로 옮겨 GPU 가
`depth` 부터 색을 읽게 만들었는데, 전 스위트가 통과했다. 이유는 두 가지다.

- D3D12 는 스트라이드 안쪽이면 어떤 오프셋이든 받아준다. PSO 생성이 실패하지 않는다.
- 스위트에 픽셀 읽기가 없다. `RHI.h` 에 `MemoryType::Readback` 은 있지만 읽는 API 가 없다.

**대응: 틀린 상태를 표현할 수 없게 만들었다.** 오프셋 상수를 지우고 `offsetof` 로 묶었다.
매직 넘버가 구조체와 따로 놀 수 있는 틈이 사라졌고, 구조체 쪽 변경은 `static_assert` 가 잡는다
(m21 로 확인).

**그래도 완전히 닫히지는 않았다.** 셰이더가 건네받은 필드를 실제로 그 자리에서 읽는지는
GPU 읽기 경로 없이는 증명할 수 없다. 예컨대 HLSL 에서 `worldLinear.xy` 를 `.xz` 로
바꿔도 아무 테스트도 울지 않는다. RHI 에 읽기 경로를 붙이는 것은 새 기능이라
여기서 하지 않았다. **이것은 알려진 구멍으로 남긴다.**

## 13. 남은 항목 일괄 처리 (2026-09-12)

§10 · §11 · §9.5 에 남아 있던 것들을 순서대로 닫았다. 닫지 못한 것은 이유와 함께 §13.10 에 남긴다.

### 13.1 §3.6 dt 검증 순서 (버그)

`Framework2D::Update` 가 `m_renderWorld.BeginFrame()` 을 부른 **뒤에** dt 를 검사했다.
NaN 이나 음수 dt 한 번이면 카메라와 수집된 스프라이트를 모두 지운 채 돌아가고,
호스트는 무시했어야 할 타임스탬프 때문에 빈 화면을 제시했다. 검사를 앞으로 옮겼다.

### 13.2 P-5 정렬 키 (§3.6)

`RenderWorld2D::Sort` 가 100B 넘는 `SpriteRenderItem` 을 직접 정렬했다. 65536 상한이면
정렬 하나에 수 MB 를 옮긴다. 이제 16B `SpriteSortKey` 만 움직이고 아이템은 제자리에 있다.
키는 `[레이어 16][부호 옮긴 renderOrder 32][예약 16]` 이라 같은 키가 아닌 한 아이템을 만지지 않는다.

`GetSprites()` 는 `GetSprite(drawIndex)` 와 `GetSubmittedSprites()` 로 갈랐다.
호출부가 "그리는 순서"와 "제출된 순서" 중 무엇을 원하는지 고르게 해서,
테스트가 정렬이 아이템을 옮기지 않았다는 것을 직접 단언할 수 있다.

**기수 정렬은 하지 않았다.** §3.6 은 O(N) 을 적었지만 그러려면 `renderOrder` 범위를
24비트로 좁혀야 하고, 그것은 공개 계약 축소다. 지금 얻은 것은 이동량이며
그것이 §3.6 이 지적한 실제 비용이다.

### 13.3 P-11 프레임 할당기 (D-52, §11.3)

`JMemoryContext::frame` 은 계약이 쓰인 이래로 **아무도 채우지 않았다.**
선언만 있는 필드였고, 그것을 집는 프레임워크는 null 함수 포인터를 만났다.

`LinearAllocator` 를 만들고 `EngineInstance` 가 소유해 `memory.frame` 에 넣는다.
되감기는 틱 시작이다 — D-52 는 `Canvas::BeginFrame` 을 적었지만 Canvas 는 이 메모리를
소유하지 않는다. **프레임을 여는 쪽이 프레임 메모리도 가진다.**

`Array` 와 `Table` 의 할당기 정책이 정적 호출에서 멤버로 바뀌었다. 상태 있는 정책이
가능해진 것이 핵심이고, 상태 없는 `HeapAllocator` 는 MSVC 가 실제로 존중하는 철자의
`no_unique_address` 로 크기를 0 으로 유지한다(테스트가 두 크기를 고정한다).
덤으로 `Table` 의 `Hasher` 와 `KeyEqual` 이 표준 철자를 쓰고 있어 각각 1바이트씩
쓰고 있던 것도 함께 고쳤다.

전파 규칙 두 가지. **재해싱은 정책을 유지한다** — 첫 판은 잃어버려서 조용히 기본 힙으로
되돌아갔고 테스트가 잡았다. **복사는 원본의 정책을 물려받지 않고**, 대입은 받는 쪽 것을
지킨다. 프레임 아레나에 묶인 것을 복사해 더 오래 살리면 되감긴 메모리를 들게 되기 때문이다.

### 13.4 §3.4 타입 조회 — 측정하고 고쳤다

규칙대로 **측정이 먼저였고, 첫 측정은 내 예상을 뒤집었다.**

| 장면 | 조회 | 역참조 |
|---|---|---|
| 평평한 스프라이트 70개 | 71 | 71 |
| 4컴포넌트, Transform 마지막, 200노드 계층 | 350 | **1400** |
| 같은 계층, 고친 뒤 | 350 | **350** |

첫 줄만 봤다면 고칠 것이 없었다. 오브젝트마다 `Transform2D` 가 첫 컴포넌트였기 때문이다.
실제 비용은 **컴포넌트가 여럿일 때** 드러난다 — 후보마다 `SafePtr` 제어 블록을 따라가고
그 블록들은 서로 다른 곳에 있다. 노드당 프레임당 7회 추격이었다.

`GameObject` 가 `ComponentSlot`(참조 + 타입 id 사본)을 들도록 바꿨다. 비교가 슬롯 안에서
끝나고 맞는 것 하나만 따라간다. 값은 컴포넌트 수명 내내 불변이라 어긋날 수 없다.
비용은 컴포넌트당 8바이트다. 두 측정 모두 출력되고 단언으로 묶여 있다.

### 13.5 P-10 태그 정수화 (D-51, §11.4)

§11.4 는 "원문을 보관할 계층이 이 트리에 없다"는 이유로 미뤘다. `NameTable` 이 그 계층이다.
id 는 텍스트에서 바로 나오므로(`MakeNameId`) 표를 거치지 않고 비교할 수 있고,
그래서 태그 비교가 정수 비교가 된다. `GetTag` 와 `SetTag` 는 그대로 `const char*` 다.

레지스트리와 같은 `Local`/`Get`/`Bind` 를 붙이고 `ScriptModuleLoadContext` 에 `Names` 를
더했다(ABI 3, 크기 단언이 증가를 잡았다). 이것이 없으면 DLL 이 자기 표에 넣고
호스트가 지은 이름은 하나도 되찾지 못한다.

### 13.6 §10 (A) 호스트와 스크립트 DLL — 형식은 정하지 않고 배선만

§10 (A) 는 "프로젝트 파일 형식이 없어 호스트가 경로를 알 수 없다"였다.
**형식은 여전히 정해지지 않았고 여기서 정하지 않는다.** `OpenProject` 가 경로를 인자로
받을 뿐이다 — 그 문자열을 어느 파일이 주게 되든 이 배선은 바뀌지 않는다.

순서가 이 변경의 알맹이다. 모듈은 `BindScriptContexts` **뒤에** 실린다(로더가 방금 붙인
컨텍스트를 읽는다). `UnbindScriptContexts` **앞에** 내려간다(DLL 이 그 컨텍스트를 가리킨다).
모듈 로드에 실패한 프로젝트는 반쯤 열린 채로 남지 않고 아예 열리지 않는다.

`IFramework::GetScriptContextBlocks` 가 생겼다. 프레임워크가 자기 DLL 이 요구하는
확장 블록을 호스트에 넘기는 통로다.

### 13.7 §10 (B) ScriptSystem — 돌지만 완료는 아니다

헤더와 빈 오버라이드 넷, 그리고 "스케줄링은 의도적으로 생략"이라는 주석이 전부였다.
스크립트 훅을 부르는 곳이 아무 데도 없었으므로 D-45 의 실행 순서는 참일 대상이 없었다.

`Canvas::CollectScripts` 가 타입을 가리지 않고 살아 있는 스크립트를 모은다.
어느 풀이 스크립트인지는 `AttachComponent<T>` 인스턴스화 시점에 정해지므로
매 프레임 `dynamic_cast` 가 없다. `ScriptSystem` 이 **레이어 → 계층 깊이 → InstanceId**
로 세우고, 같은 프레임의 `OnUpdate` 앞에 `OnCreate` 와 `OnStart` 를 한 번씩 돌린다.
고정 스텝은 업데이트가 세운 순서를 그대로 쓴다.

시작 목록은 매 재구축마다 살아 있는 스크립트로 솎는다. 첫 판은 추가만 해서
긴 세션이면 파괴된 스크립트의 id 가 끝없이 쌓였다. 비활성일 뿐인 것은 남긴다 —
다시 켤 때 두 번 시작해서는 안 되기 때문이다.

**여전히 미완이고 그렇게 표시한다.** `Canvas::AttachComponent<T>` 로 정적으로 붙인 것만
스케줄에 오른다. DLL 안에서 이름으로 스크립트를 만드는 경로는 리플렉션(H5) 이 필요하고,
Open Decision 3 이 그것을 완료 조건으로 못 박았다.

### 13.8 §9.5 세 항목

- **헤더 자립성.** 공개 헤더마다 번역 단위 하나, 총 76개. `Generate.ps1` 이 만들고
  생성물을 커밋한다(빌드에 코드 생성 단계를 넣지 않기 위해서다). 각 파일은 헤더를 두 번
  넣어 재포함 방지도 함께 본다. 76개 모두 이미 통과했으므로 이것은 고침이 아니라 고정이다.
- **Framework3D 실행 계층.** `JBroFramework3DSystem` 신설. `JBroFramework3D` 는 컴포넌트와
  수학만 남고 Core, Runtime, AssetTypes 만 본다. 3D 스크립트 경로에서 실행 계층 헤더가
  C1083 이 되는 것을 직접 컴파일해 확인했다(양성 대조로 컴포넌트 헤더는 여전히 컴파일된다).
  이 분리로 `JBroFramework3D` 가 소스 없는 모듈이 되어 라이브러리가 사라졌다 —
  `Utility` 로 바꿔 링커 입력에서 뺐다. **분리 커밋은 이 사실을 놓쳤다**: 뒤이은 확인 스윕이
  첫 실패에서 멈추지 않아 Release 링크 실패를 지나쳤고, 다음 커밋에서 고쳤다.
- **`GameObject` 비공개.** include 경로로는 막을 수 없다 — 스크립트 DLL 이 `JBroRuntime` 을
  링크하므로 그 경로가 열려 있다. 대신 표식으로 막는다: 스크립트 타깃이 `JBRO_SCRIPT_TARGET`
  을, 프렐류드가 `JBRO_SCRIPT_PRELUDE` 를 정의하고, 앞의 것만 있으면 `#error` 다.
  Tier 프로브가 네 번째 음성 사례를 얻었다.

### 13.9 §11.1 Transform 부모 먼저 배열 — 측정하고 하지 않기로 했다

§13.4 이후 이 경로에 남은 것은 노드당 조회 1.75회, 역참조 1회다.
부모 먼저 배열이 없앨 수 있는 것은 루트 판정 조회뿐이고, 그러려면 계층 변경과
컴포넌트 부착·파괴 양쪽을 보는 무효화 계약이 필요하다.
**얻는 것보다 조용히 틀릴 위험이 크다.** 측정값을 단언으로 묶어 두었으므로
비율이 나빠지면 테스트가 먼저 운다. 계층이 훨씬 깊어지거나 조회가 더 늘면 그때 다시 본다.

### 13.10 아직 열려 있는 것 — 전부 사용자 확인이 필요하다

- **프로젝트 파일 형식.** §13.6 이 배선만 세웠다. 형식(YAML 인지 바이너리인지)과 그 안의
  항목은 에디터, 익스포터, 에셋 파이프라인을 함께 정하는 판단이다.
- **리플렉션(H5).** §13.7 의 나머지 절반이자 Open Decision 3 의 완료 조건이다.
- **GPU 읽기 경로.** §12.4 의 구멍은 여전히 열려 있다. `RHI.h` 에 `MemoryType::Readback` 은
  있지만 읽는 API 가 없어, 셰이더가 건네받은 필드를 그 자리에서 읽는지 증명할 수 없다.
  읽기 경로를 붙이는 것은 RHI 에 새 능력을 더하는 일(동기화 모델, 포맷 변환, 호출 가능 시점)이다.
