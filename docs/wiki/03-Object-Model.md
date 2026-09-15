# 3. 오브젝트 모델

이 엔진은 **오브젝트-컴포넌트 모델**이다. ECS 가 아니고, 앞으로도 ECS 로 바꾸지 않는다(D-1).
기존 엔진이 이미 이 모델이었고, 리플렉션과 직렬화가 컴포넌트의 다형성 위에 서 있기 때문이다.

## 수명 계층은 하나다

```
Canvas
 ├─ 오브젝트 풀            TObjectPool<GameObject>
 ├─ 타입별 컴포넌트 풀      TObjectPool<Transform2D>, TObjectPool<SpriteRenderer2D>, … (버킷)
 ├─ 스크립트 풀            DLL 이 알려 준 크기·정렬로 잡는 풀
 ├─ 레이어 목록            Layer (식별자·이름·표시 여부·합성 순서)
 └─ 시스템 스케줄러         SystemScheduler → GameSystem 들
```

`Canvas` 위에 `World` 도 없고 아래에 `Scene` 도 없다(D-2). 이름을 바꿔서도 중복 수명 계층을 만들지 않는다.
금지 대상은 특정 이름이 아니라 **수명 계층이 둘 이상 되는 것** 자체다.

`Canvas` 는 Tier E 다. 스크립트는 `Canvas` 의 선언조차 보지 못하고, 오브젝트 생성 같은 일은 서비스를 거쳐야 한다.

## GameObject

실체 객체다. `Entity` 같은 정수 ID 를 따로 두지 않는다.

| 멤버 | 설명 |
|---|---|
| `InstanceId` | 64비트 영속 식별자. 생성 시 발급되고 저장 파일에 적힌다 |
| `InstanceHandle` | 풀 슬롯과 세대. 이번 실행에서의 위치 |
| 부모 / 자식 | `SafePtr<GameObject>` 와 `Array<SafePtr<GameObject>>`. 자식 순서는 사람이 보는 순서라 `SetChildIndex` 로 옮길 수 있다 |
| 레이어 | 소속 `Layer` 와 그 인덱스. 조회는 O(1) |
| 활성 | `IsActiveSelf()` 와 `IsActiveInHierarchy()`. 후자는 캐시라 O(1) 이고 `SetActive`·`SetParent` 가 하위 트리로 전파한다(D-54) |
| 태그 | 정수 `NameId` 로 산다(D-51). 원문은 `NameTable` 이 갖고 있다 |
| 컴포넌트 | `Array<ComponentSlot>`. 슬롯에 타입 id 를 복사해 두어 타입으로 찾을 때 제어 블록을 따라가지 않는다 |

**Transform 은 멤버가 아니라 컴포넌트다**(D-3). `GameObject` 는 자기가 2D 인지 3D 인지 모른다.
어느 Framework 가 로드됐느냐에 따라 `Component::Transform2D` 나 `Component::Transform3D` 가 붙는다.
부모·자식·레이어는 차원과 무관하므로 멤버로 남는다.

## ComponentBase

모든 컴포넌트의 다형성 베이스다. 파생 타입은 반드시 `static constexpr const char* StaticTypeName()` 을 제공하고
`GetTypeId()` 를 그 이름으로 구현한다. 타입 id 는 `MakeStableTypeId(T::StaticTypeName())` 으로 **이름에서 유도**한다.
손으로 매기는 번호가 없으므로 DLL 경계와 저장 파일을 넘어도 값이 같다.

가상 함수 집합은 D-48 로 고정했다. 스크립트 DLL 이 이 클래스를 상속하므로 vtable 이 곧 ABI 다.

```cpp
virtual ~ComponentBase();
virtual ComponentTypeId GetTypeId() const = 0;
virtual void OnAttached();    // 소유 오브젝트와 식별자가 확정된 직후. 형제 컴포넌트 캐시를 잡는 자리
virtual void OnDetached();    // 풀에 반납되기 직전
virtual void OnEnabled();
virtual void OnDisabled();
```

메모리는 **타입별 풀**이 소유하고, 논리 소유는 **오브젝트**가 한다. 풀은 슬롯 주소가 바뀌지 않는다(compaction 금지).
`SafePtr` 와 캐시된 포인터가 그 성질에 기댄다.

같은 타입 컴포넌트가 한 오브젝트에 여러 개 있을 수 있다. `InstanceId` 로 구분한다.
`GetComponent<T>()` 는 첫 번째를, `GetComponents<T>()` 는 전부를 준다(D-31).

활성 판정은 `IsActiveComponent()` **하나**다. 시스템마다 `owner->IsActive` 를 따로 보면 시스템끼리 어긋난다.
기존 엔진에서 실제로 겪은 문제라 게이트를 하나로 못박았다.

## 스크립트도 컴포넌트다

`GameScriptBase` 는 `ComponentBase` 의 파생이고, 2D 프로젝트에서는 `GameScript2D` 가 그 위에 충돌 훅을 얹는다.

```
ComponentBase
 └─ GameScriptBase        OnCreate · OnStart · OnUpdate(dt) · OnFixedUpdate(fdt) · OnDestroy
     └─ GameScript2D      + OnCollisionEnter(hit) · OnCollisionExit(hit)   ← 아직 부르는 곳이 없다
```

훅 순서는 `OnAttached` → `OnCreate` → `OnStart` → (매 프레임) `OnFixedUpdate`… `OnUpdate` → `OnDestroy` → `OnDetached` 다.
`OnCreate` 와 `OnStart` 는 **같은 프레임 안에서** 처음 `OnUpdate` 앞에 온다. 시작 훅이 한 프레임 늦게 보이면 안 된다.

스크립트 실행 순서는 **레이어 합성 순서 → 오브젝트 계층(부모 먼저) → 컴포넌트 부착 순서**다(D-45).

> 지금 코드는 이 목록을 매 프레임 다시 세운다. 계약은 "더티 플래그로 지연 재구축" 인데 아직 그렇게 돼 있지 않다.
> `tasks/todo.md` 의 Divergence Findings A1 에 적혀 있는 알려진 어긋남이다.

## 레이어

`Layer` 는 오브젝트의 **소속과 합성 순서**만 말한다. 오브젝트의 수명은 소유하지 않는다.

| 필드 | 설명 |
|---|---|
| `LayerId` | 단조 증가, 재사용 없음, 저장되는 값 |
| 이름 | 64자 |
| `GetOrder()` | 합성 순서 캐시. `Canvas` 가 레이어를 만들고·지우고·옮길 때만 다시 매긴다(D-46) |
| 표시 여부 | 렌더 추출이 비가시 레이어를 건너뛴다 |

블렌드·불투명도·공간(World/Screen)·패럴랙스·별도 합성 텍스처는 **2D 렌더 합성 상태**라 공통 `Layer` 에 없다.
`JBroFramework2D` 의 `Layer2D` 가 갖고, 살아 있는 `Layer` 에 대해 **지연 생성**된다(D-41, D-46).
공통 `Canvas` 에 수명 콜백을 달지 않기 위해서다. 죽은 레이어의 `Layer2D` 는 다음 접근 때 정리된다.

## 시스템과 서비스

역할이 다른 둘을 이름으로 가른다. `Manager` 라는 이름은 쓰지 않는다(D-12).

| | `JBro::System` | `JBro::Service` |
|---|---|---|
| 누가 쓰나 | 엔진 계층 | 스크립트 |
| 무엇을 하나 | 매 프레임 컴포넌트 풀을 순회하며 갱신한다 | 사용자에게 필요한 것만 추린 얇은 API |
| 어떻게 전달되나 | `SystemContext` 에 인터페이스 포인터로. 스크립트에는 안 보인다 | `ServiceContext` 에 **값**으로. 프렐류드가 include 한다 |
| 예 | `System::Physics2DSystem` (적분, 쿼리 구현) | `Service::Physics2DService` (`Raycast`, `OverlapBox`) |

한 타입이 두 역할을 겸하면 분리한다. 물리가 그렇게 둘로 나뉘어 있다.

시스템은 `GameSystem` 을 상속하고 `Canvas::ForEach<T>` 로 **타입별 풀을 순회**한다. 순회가 실체 참조(`T&`)를 그대로 주므로
`Ref<T>` 해석 비용이 없다. 다중 타입 `Query<A,B>` 는 도입하지 않는다.

```cpp
class GameSystem
{
public:
    virtual int GetExecutionOrder() const;   // 작은 것이 먼저
protected:
    virtual void OnInitialize (Canvas& canvas);
    virtual void OnFixedUpdate(Canvas& canvas, float fixedDeltaTime);
    virtual void OnUpdate     (Canvas& canvas, float deltaTime);
    virtual void OnShutdown   (Canvas& canvas);
};
```

2D 프로젝트에 등록되는 시스템과 순서는 다음과 같다.

| 순서 값 | 시스템 | 하는 일 |
|---|---|---|
| 100 | `Transform2DSystem` | 계층 버전이 바뀌었을 때만 부모 먼저 배열을 다시 만들고, 매 프레임 그 배열을 한 번 훑어 월드 캐시를 채운다(D-47) |
| 200 | `ScriptSystem` | 스크립트 훅을 부른다. 변환이 선 뒤, 렌더 추출 전이다 |
| 200 | `Physics2DSystem` | 고정 스텝에서 중력·감쇠·속도를 적분해 `Transform2D` 를 움직인다. 레이캐스트·오버랩 쿼리를 구현한다 |
| 300 | `Camera2DSystem` | 주 카메라를 골라 뷰를 만든다 |
| 400 | `SpriteRender2DSystem` | 스프라이트를 렌더 월드로 추출한다 |

같은 순서 값이면 등록 순서다. 스크립트가 물리보다 먼저 등록된다.

## 프레임 하나

`Framework2D::Update(dt)` 가 한 프레임이다.

```
dt 검증 (음수·NaN 이면 프레임 전체를 건너뛴다)
RenderWorld2D::BeginFrame        지난 프레임의 추출 결과를 비운다
Canvas::BeginFrame               프레임 메모리 등 프레임 단위 상태 리셋
RunFixedSteps(dt)
   └ 누적 시간이 fixedDeltaTime 을 넘는 동안, 최대 maxFixedStepsPerFrame 번:
        SystemScheduler::FixedUpdate    → 각 시스템의 OnFixedUpdate (스크립트 OnFixedUpdate 포함)
        Canvas::FlushPendingDestroy     ← 안전 지점 1
SystemScheduler::Update             → 각 시스템의 OnUpdate (스크립트 OnCreate/OnStart/OnUpdate 포함)
Canvas::FlushPendingDestroy         ← 안전 지점 2
RenderWorld2D::EndFrame             정렬 키 (layerOrder, renderOrder, sourceId) 로 정렬
```

그 뒤 호스트가 `Framework2D::Render()` 를 부르면 렌더 월드의 뷰와 패킷을 `Renderer` 에 넘긴다.
반환값은 `Submitted` / `NothingToSubmit` / `Failed` 셋이고 호스트는 `Failed` 만 치명으로 본다(D-49).

## 순회 중에는 만들거나 지우지 않는다

`ForEach<T>` 와 스크립트 훅 루프가 도는 동안 풀의 live 배열이 흔들리면 바깥 순회가 무효가 된다. 그래서 규칙이 있다(D-45).

- **순회 중 생성**은 즉시 하되, 실행 목록에는 다음 프레임에 반영한다.
- **순회 중 파괴**는 지연 큐에 넣고, 위 그림의 안전 지점 두 곳에서 `FlushPendingDestroy` 가 처리한다.
- `Canvas` 가 순회 깊이 가드(`IterationGuard`)를 갖고 `IsIterating()` 으로 지금 순회 중인지 알려 준다.

> 스크립트 훅을 부르는 루프 자체에는 아직 가드가 걸려 있지 않다(Divergence Findings A2).
> 스크립트가 `OnUpdate` 안에서 자기나 형제를 지우면 즉시 파괴 경로로 들어갈 수 있다. 고칠 항목으로 적혀 있다.

## 성능 계약

"매 프레임 도는 경로에 `dynamic_cast`·힙 할당·문자열 생성/비교를 두지 않는다" 가 규칙이고, **스폰과 파괴를 포함한 정상 프레임**이 기준이다(D-54).
말로만 두지 않고 테스트가 잰다.

- 카운팅 할당기를 `Canvas` 와 `Renderer` 에 넣은 정상 프레임에서 할당 0회
- `InstanceRegistry` 의 영속 조회(`InstanceId` 해시 조회) 0회 증가
- `TObjectPool::Destroy` 의 슬롯 탐색은 청크 베이스 주소 배열의 이진 탐색이고 비교 횟수에 상한이 있다
- `SafePtr` 제어 블록은 재활용 목록으로 돌려 쓰고 `Reserve` 에서 미리 확보한다. 정상 스폰에서 제어 블록 `new` 는 0회다

프레임 임시 배열은 `JMemoryContext.frame`(선형 할당기)을 쓴다. 되감는 주체는 프레임을 여는 `EngineInstance::Tick` 이다(D-52).
