# 6. 스크립트 API

C++ 로 게임 스크립트를 쓸 때 쓸 수 있는 것을 전부 모았다. 이 문서에 없는 이름은 스크립트에서 쓸 수 없다고 보면 된다.
2D 프로젝트 기준이다. 3D 는 마지막 절에 차이만 적었다.

## 가장 짧은 스크립트

```cpp
#include <JBro/ScriptAPI.h>

JBRO_SCRIPT(Player) : public GameScript2D
{
public:
    static constexpr const char* StaticTypeName() { return "Player"; }
    ComponentTypeId GetTypeId() const override { return MakeStableTypeId(StaticTypeName()); }

    JBRO_REFLECT_BODY(Player)
    JBRO_FIELD(float, Speed, Range(0, 20)) = 5.0f;

    void OnStart() override
    {
        m_transform = GetGameObject().GetComponent<Component::Transform2D>();
    }

    void OnUpdate(float deltaTime) override
    {
        if (auto* t = m_transform.Get())
        {
            t->position.x += Speed * deltaTime;
        }
    }

private:
    Ref<Component::Transform2D> m_transform;
};
```

그리고 DLL 의 `Load` 안에서 등록한다.

```cpp
RegisterScriptType<Player>();
```

`JBRO_SCRIPT(Player)` 는 `class Player` 로 펼쳐지는 표식 매크로다. 런타임 동작은 없고, 에디터의 코드 생성기가 스크립트 목록을 만들 때 이 표식을 본다(D-19).
그냥 `class` 로 적어도 컴파일은 되지만 목록에 뜨지 않는다.

## DLL 뼈대

스크립트 DLL 하나에 아래 코드가 한 번 들어간다. 테스트용 스크립트 DLL(`Tests/ScriptModuleProbe/Source/Probe.cpp`)이 실제로 이 모양이다.
이 부분은 프렐류드 밖의 헤더(`Internal/ScriptModuleContext.h`, `ScriptRegistry.h`)를 쓰므로 스크립트 본문과 다른 파일에 둔다.

```cpp
#include <JBro/Framework2D/Internal/ScriptModuleContext.h>
#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Internal/InstanceRegistry.h>
#include <JBro/Runtime/ScriptRegistry.h>
#include <JBro/Types/NameTable.h>

namespace
{
    bool LoadModule(const JBro::ScriptModuleLoadContext* context) noexcept
    {
        if (context == nullptr || false == JBro::ValidateScriptModuleLoadContext(*context))
        {
            return false;
        }
        const auto* services = JBro::FindFramework2DServiceContext(*context);
        const auto* systems  = JBro::FindFramework2DSystemContext(*context);
        if (services == nullptr || systems == nullptr)
        {
            return false;
        }
        if (false == JBro::BindScriptModuleContexts(*context))     // 레지스트리·이름표·스크립트 표·공통 Context
        {
            return false;
        }
        JBro::BindFramework2DServiceContext(*services);
        JBro::BindFramework2DSystemContext(*systems);

        return JBro::RegisterScriptType<Player>()                   // 스크립트 타입마다 한 줄
            && JBro::RegisterScriptType<Enemy>();
    }

    void UnloadModule() noexcept
    {
        JBro::BindFramework2DServiceContext({});
        JBro::BindFramework2DSystemContext({});
        JBro::Internal::InstanceRegistry::Bind(nullptr);
        JBro::ScriptRegistry::Bind(nullptr);
        JBro::NameTable::Bind(nullptr);
        JBro::BindSystemContext({});
        JBro::BindServiceContext({});
    }

    constexpr JBro::ScriptContextRequirement RequiredContexts[] =
    {
        JBro::Framework2DServiceContextRequirement,
        JBro::Framework2DSystemContextRequirement
    };

    constexpr JBro::ScriptModuleApi ModuleApi =
    {
        JBro::ScriptModuleAbiVersion, sizeof(JBro::ScriptModuleApi),
        RequiredContexts, 2, 0,
        &LoadModule, &UnloadModule
    };
}

extern "C" __declspec(dllexport) const JBro::ScriptModuleApi* JBroScriptModule_GetApi(
    std::uint32_t hostAbiVersion, std::uint32_t hostApiSize) noexcept
{
    if (hostAbiVersion != JBro::ScriptModuleAbiVersion || hostApiSize != sizeof(JBro::ScriptModuleApi))
    {
        return nullptr;
    }
    return &ModuleApi;
}
```

JBroScript 가 생기면 이 뼈대는 `jbroc` 이 만들어 준다. 그때까지는 손으로 한 번 적는다.

## 프렐류드가 주는 것

`<JBro/ScriptAPI.h>` 하나가 아래를 전부 끌어오고 `using namespace JBro;` 를 한다.

| 헤더 | 안에 있는 것 |
|---|---|
| `JBro/Types/Types.h` | `Int`, `Float`, `Bool`, `UInt`, `Color`, `String`, `Array<T>`, `Table<K,V>`, `ArrayView<T>`, `SafePtr` |
| `JBro/Runtime/GameObjectHandle.h` | `GameObjectHandle` |
| `JBro/Runtime/Ref.h` | `Ref<T>`, `InstanceRef`, `InstanceHandle` |
| `JBro/Runtime/ServiceContext.h` | 공통 `ServiceContext`(지금은 비어 있다) |
| `JBro/Script/Macros.h` | `JBRO_SCRIPT` |
| `JBro/Framework2D/Component/*.h` | `Transform2D`, `SpriteRenderer2D`, `Camera2D`, `Rigidbody2D`, `Collider2D`, `Collision2D` |
| `JBro/Framework2D/Layer2D.h` | `Layer2D` |
| `JBro/Framework2D/Math2D.h` | `Vec2`, `Rect`, `Matrix3x2` 와 행렬 함수 둘 |
| `JBro/Framework2D/Prefab/Prefab.h` | `PrefabAsset`, `PrefabSpawnParams`, `PrefabSpawner` |
| `JBro/Framework2D/Scripting/GameScript.h` | `GameScript2D`(→ `GameScriptBase` → `ComponentBase`) |
| `JBro/Framework2D/ServiceContext.h` | `Framework2DServiceContext`, `GetFramework2DServices()` |

네임스페이스는 강제하지 않지만 **1뎁스 네임스페이스를 붙여 쓰는 것을 권한다.** `Component::Transform2D` 처럼 쓰면 `JBro` 직속 이름과 달리
사용자 코드와 충돌하지 않는다. `JBro::Game` 은 없다. `using namespace JBro;` 뒤에 사용자의 `namespace Game` 과 부딪히기 때문이다.

## 스크립트 베이스

```cpp
class ComponentBase
{
public:
    virtual void OnAttached();          // 소유 오브젝트가 확정된 직후. 형제 컴포넌트 캐시를 잡는 자리
    virtual void OnDetached();          // 풀에 반납되기 직전
    virtual void OnEnabled();
    virtual void OnDisabled();

    InstanceId       GetInstanceId() const;
    InstanceHandle   GetHandle() const;
    ComponentTypeId  GetCachedTypeId() const;
    GameObjectHandle GetOwner() const;  // 소유 오브젝트. 핸들로 준다
    bool IsActiveComponent() const;     // 자기 enabled 와 오브젝트 활성을 합친 단일 게이트
    bool IsEnabled() const;
    void SetEnabled(bool enabled);
};

class GameScriptBase : public ComponentBase
{
public:
    GameObjectHandle GetGameObject() const;   // GetOwner() 와 같다
    virtual void OnCreate();
    virtual void OnStart();
    virtual void OnUpdate(float deltaTime);
    virtual void OnFixedUpdate(float fixedDeltaTime);
    virtual void OnDestroy();
};

class GameScript2D : public GameScriptBase
{
public:
    virtual void OnCollisionEnter(const Collision2D& hit);   // 아직 부르는 곳이 없다
    virtual void OnCollisionExit(const Collision2D& hit);
};
```

훅이 불리는 순서와 시점은 [오브젝트 모델](03-Object-Model.md)의 "스크립트도 컴포넌트다" 절에 있다.
요점만 적으면, `OnCreate` 와 `OnStart` 는 첫 `OnUpdate` 와 같은 프레임에 그 앞에서 불리고, `OnFixedUpdate` 는 고정 스텝(기본 1/60초)마다 불린다.

파생 타입은 반드시 `StaticTypeName()` 과 `GetTypeId()` 를 제공한다. 이름이 곧 타입 id 이고 저장 파일에 적히는 이름이다.
기본 생성자가 있어야 한다. 풀 슬롯에 제자리 생성되기 때문이다.

## GameObjectHandle

오브젝트를 들고 있을 때 쓰는 16바이트 값이다. 무효한 핸들에 안전 멤버를 불러도 크래시하지 않는다. 로그만 남기고 아무 일도 하지 않는다.

| 멤버 | 무효일 때 |
|---|---|
| `bool IsValid() const` | `false` |
| `explicit operator bool() const` | `false` |
| `void Destroy()` | 로그, 무시 |
| `void SetActive(bool active)` | 로그, 무시 |
| `bool IsActive() const` | `false` |
| `template<T> Ref<T> GetComponent() const` | 빈 `Ref` |
| `InstanceId GetInstanceId() const` | 들고 있던 id 그대로 |

`operator->` 는 없다. 오브젝트의 이름·태그·부모·자식·레이어에 닿는 길은 아직 스크립트 표면에 없다. 필요해지면 서비스나 안전 멤버로 추가된다.

## Ref\<T\>

컴포넌트나 다른 스크립트를 멤버로 들고 있을 때 쓴다. 저장되고, 로드 뒤에도 같은 대상을 가리킨다.

```cpp
T*   Get() const;                 // 무효면 nullptr. 이것으로 확인하고 쓴다
T*   operator->() const;          // 확인 없이 쓴다. 무효면 Debug 에서 assert, Release 에서 크래시
T&   operator*() const;
bool IsValid() const;
void Clear();
explicit operator bool() const;   // "값이 설정돼 있는가" 만. 대상이 살아 있는지는 보지 않는다
bool operator==(const Ref&) const;
```

`GetComponent<T>()` 는 `T*` 가 아니라 `Ref<T>` 를 준다. 원시 포인터는 저장할 수 없어 매 프레임 다시 찾아야 하는데 그 조회가 선형 탐색이다.
`Ref<T>` 로 한 번 받아 두면 이후 접근이 캐시 비교 한 번이다. 받는 자리는 `OnAttached` 나 `OnStart` 가 좋다.

`Ref<GameObject>` 는 컴파일 에러다. 오브젝트는 `GameObjectHandle` 이다.

## 빌트인 컴포넌트

전부 `JBro::Component` 안에 있고, 필드는 `JBRO_FIELD` 로 선언돼 있어 인스펙터에 보이고 저장된다.
`ReadOnly` 표시가 있는 필드는 시스템이 채우는 캐시라 스크립트는 **읽기만** 한다. 써도 다음 갱신에 덮인다.

### Component::Transform2D

| 필드 | 타입 | 기본값 | 비고 |
|---|---|---|---|
| `position` | `Vec2` | (0, 0) | 로컬 |
| `rotation` | `float` | 0 | 라디안 |
| `scale` | `Vec2` | (1, 1) | |
| `world` | `Matrix3x2` | 단위 | 월드 캐시. 읽기 전용, 저장 안 함 |
| `worldPosition` | `Vec2` | | 월드 캐시 |
| `worldRotation` | `float` | | 월드 캐시 |
| `worldScale` | `Vec2` | | 월드 캐시 |
| `worldValid` | `bool` | false | false 인 동안 world* 값은 읽지 않는다 |

로컬과 월드를 한 컴포넌트에 둔 이유는 D-47 이다. 둘로 나누면 사용자가 항상 쌍으로 붙여야 하고, 하나를 빠뜨리면 렌더·카메라·물리에서 조용히 빠진다.

### Component::SpriteRenderer2D

| 필드 | 타입 | 기본값 | 비고 |
|---|---|---|---|
| `spriteId` | `AssetId` | 0 | 저장되는 쪽 |
| `sprite` | `AssetHandle` | | `spriteId` 에서 해석된 값. 읽기 전용, 저장 안 함. **아직 채우는 패스가 없다** |
| `materialId` | `AssetId` | 0 | |
| `material` | `AssetHandle` | | 같다 |
| `tint` | `Color` | (1,1,1,1) | |
| `pivot` | `Vec2` | (0.5, 0.5) | |
| `size` | `Vec2` | (1, 1) | |
| `flip` | `SpriteFlip` | `None` | `None` / `Horizontal` / `Vertical` / `Both` |
| `renderOrder` | `std::int32_t` | 0 | 같은 레이어 안의 순서. 부호 있는 값이다 |
| `visible` | `bool` | true | |

### Component::Camera2D

| 필드 | 타입 | 기본값 |
|---|---|---|
| `projection` | `CameraProjection2D` | `Orthographic` (`PixelPerfect` 는 D-58 로 구현 예정) |
| `orthographicSize` | `float` | 10 |
| `nearPlane` | `float` | -100 |
| `farPlane` | `float` | 100 |
| `clearColor` | `Color` | (0.08, 0.09, 0.11, 1) |
| `primary` | `bool` | false |

### Component::Rigidbody2D

| 필드 | 타입 | 기본값 | 비고 |
|---|---|---|---|
| `bodyType` | `BodyType2D` | `Dynamic` | `Static` / `Kinematic` / `Dynamic` |
| `linearVelocity` | `Vec2` | (0, 0) | 저장 안 함. 시뮬레이션이 매 프레임 다시 쓴다 |
| `angularVelocity` | `float` | 0 | 저장 안 함 |
| `mass` | `float` | 1 | 0~1000. 0 이하인 Dynamic 은 움직이지 않는다 |
| `gravityScale` | `float` | 1 | |
| `linearDamping` | `float` | 0 | |
| `fixedRotation` | `bool` | false | |

### Component::Collider2D

| 필드 | 타입 | 기본값 |
|---|---|---|
| `shape` | `ColliderShape2D` | `Box` (`Box` / `Circle` / `Capsule` / `Polygon`) |
| `offset` | `Vec2` | (0, 0) |
| `size` | `Vec2` | (1, 1) |
| `radius` | `float` | 0.5 |
| `isTrigger` | `bool` | false |

### Collision2D

충돌 훅과 `Raycast` 가 돌려주는 값이다.

```cpp
struct Collision2D
{
    GameObjectHandle      other;
    Component::BodyType2D bodyType;
    Vec2                  point;
    Vec2                  normal;
};
```

## 서비스

서비스는 `GetFramework2DServices()` 로 얻는 값 구조체 안에 **값**으로 들어 있다. 포인터가 아니므로 null 검사가 필요 없다.

```cpp
struct Framework2DServiceContext
{
    std::uint32_t AbiVersion;
    Service::Physics2DService Physics2D;
};

const Framework2DServiceContext& GetFramework2DServices();
```

### Service::Physics2DService

```cpp
// hit 을 덮어쓴다. 빗나가거나 시스템이 없으면 이전 결과를 지우고 false 다.
bool Raycast(Vec2 origin, Vec2 direction, float distance, Collision2D& hit) const;

// results 를 덮어쓴다. 오브젝트마다 한 번씩 들어간다. 반복 호출 전에 Reserve 해 두는 것이 좋다.
void OverlapBox(const Rect& area, Array<GameObjectHandle>& results) const;
```

```cpp
Collision2D hit;
if (GetFramework2DServices().Physics2D.Raycast(origin, Vec2{0.0f, -1.0f}, 1.0f, hit))
{
    hit.other.Destroy();
}
```

메인 스레드에서만 부른다. 시스템의 수명은 소유하지 않는다.

지금 있는 서비스는 이것 하나다. `Time`·`Input`·오브젝트 생성 서비스는 아직 스크립트 표면에 없다.

## 프리팹

선언은 있으나 몸통은 골격이다(D-53 에 "선언만 있는 골격" 으로 명시).

```cpp
struct PrefabSpawnParams
{
    GameObjectHandle parent;                      // 비어 있으면 최상위
    bool             preserveSourceIdentity = false;
};

class PrefabSpawner
{
public:
    GameObjectHandle Spawn(AssetId prefabAsset, const PrefabSpawnParams& params);
    bool             ApplyOverrides(GameObjectHandle instance, AssetId prefabAsset);
    void             DestroyInstance(GameObjectHandle instance);
};
```

## 수학과 값 타입

### 2D 수학 (`Math2D.h`)

```cpp
struct Vec2      { float x, y; };
struct Rect      { Vec2 min, max; };
struct Matrix3x2 { float m11, m12, m21, m22, m31, m32; };   // 행 벡터 기준 아핀 변환

Matrix3x2 MakeTransformMatrix2D(const Vec2& position, float rotation, const Vec2& scale);
Matrix3x2 MultiplyMatrix3x2(const Matrix3x2& left, const Matrix3x2& right);
```

`Vec2` 에는 연산자가 없다. 성분을 직접 다룬다. 이름은 `Vector2` 로 바꾸기로 했으나 아직 바꾸지 않았다(jbroscript-syntax §7.1).

### Color

```cpp
struct Color { float R, G, B, A = 1.0f; };    // float[4] 와 메모리 배치가 같다
float* Data();  float& operator[](std::size_t);
```

집합체라 `Color{1.0f, 0.0f, 0.0f, 1.0f}` 로 초기화한다.

### 숫자 강타입

| 이름 | 실체 |
|---|---|
| `Int` | `Int64` 의 별칭. 폭을 밝히지 않은 정수는 64비트다 |
| `Int32`, `UInt`, `UInt32` | 각 폭의 강타입 |
| `Float` | 32비트 `float` 을 감싼 클래스. `float` 과 암묵 변환된다 |
| `Bool` | 강타입 |

`Float` 에는 `Floor`·`Ceil`·`Round`·`Trunc`·`Clamp`·`Abs`·`IsFinite`·`NearlyEquals` 멤버와 같은 이름의 static 함수, 그리고 `Lerp(a, b, t)` 가 있다.

### Array\<T\>

`std::vector` 대신 쓴다. 할당기 정책을 타입 인자로 받는다(기본 `HeapAllocator`).

| 분류 | 멤버 |
|---|---|
| 크기 | `Size()`, `Capacity()`, `IsEmpty()`, `Reserve(n)`, `Resize(n)`, `Shrink()`, `Clear()`, `Reset(n)`, `Release()` |
| 넣기 | `Add(value)`, `Emplace(args...)`, `Insert(index, value)`, `EmplaceAt(index, args...)`, `Append(ptr, count)` |
| 빼기 | `RemoveAt(i)`, `RemoveAtSwap(i)`, `RemoveAll(pred)`, `RemoveAllSwap(pred)`, `Pop()` |
| 찾기 | `Contains(v)`, `IndexOf(v)`, `IndexOfBy(pred)`, `FindBy(pred)` |
| 접근 | `operator[]`, `First()`, `Last()`, `Data()`, `View()`, 범위 기반 for |

`operator[]` 의 범위 검사는 Debug `assert` 뿐이다. 외부 입력에서 온 인덱스는 따로 검사한다.

### Table\<K, V\>

`std::unordered_map` 대신 쓴다. open addressing 해시 표다.

| 분류 | 멤버 |
|---|---|
| 크기 | `Size()`, `Capacity()`, `IsEmpty()`, `Reserve(n)`, `Clear()`, `Reset(n)` |
| 넣기 | `operator[](key)`, `FindOrAdd(key)`, `InsertOrAssign(key, value)`, `TryAdd(key, value)` |
| 찾기 | `Find(key)` → `Value*`, `At(key)`, `Contains(key)` |
| 빼기 | `Remove(key)` |
| 순회 | 범위 기반 for. 원소는 `Entry` 이고 `KeyValue`·`MappedValue` 멤버로 읽는다 |

`Table<InstanceId, …>` 는 항등 해시를 쓴다. 순회 도중에 넣거나 빼면 반복자가 무효다.

### String

`std::string` 의 래퍼로 확정됐다(D-51). `Append`, `Contains`, `StartsWith`, `EndsWith`, `Split`, `Trim`, `ToLower`, `ToUpper`, `Substr`, `ReplaceAll`, `View()`, `Std()` 가 있다.
**컴포넌트 공개 필드·POD Context·핸들에는 두지 않는다.** 이름과 태그는 `NameId` 다.

## 3D 프로젝트의 차이

3D 프렐류드는 같은 경로 `<JBro/ScriptAPI.h>` 이고 `JBroFramework3D` 가 제공한다. 스크립트 베이스는 `GameScriptBase` 를 직접 쓴다(3D 충돌 훅이 아직 없다).
서비스 Context 는 없다. 시스템도 서비스도 아직 없기 때문이다.

| 컴포넌트 | 필드 |
|---|---|
| `Component::Transform3D` | `position: Vec3`, `rotation: Quaternion`, `scale: Vec3 = (1,1,1)` |
| `Component::MeshRenderer3D` | `meshId`, `mesh`(읽기 전용), `materialId`, `material`(읽기 전용) |
| `Component::Camera3D` | `verticalFieldOfView: float = 60` (1~179) |
| `Component::Rigidbody3D` | `velocity: Vec3`(저장 안 함), `mass: float = 1` |
| `Component::Collider3D` | `size: Vec3 = (1,1,1)` |

쿼터니언은 저장 파일에 성분 넷으로 적힌다. `Vec3` 는 `JBroFramework3D/Math3D.h` 에 있고, 2D 프로젝트는 이것을 링크하지 않는다(D-57).

## 스크립트에서 하면 안 되는 것

- `new`/`delete`, `std::vector`, `std::unordered_map`, `std::unique_ptr`. JBro 타입이 있다.
- 다른 오브젝트의 컴포넌트 포인터(`T*`)를 멤버로 저장하는 것. `Ref<T>` 나 `GameObjectHandle` 로 든다.
- `OnUpdate` 안에서 문자열을 만들거나 비교하는 것. 매 프레임 경로의 규칙은 스크립트에도 적용된다.
- 워커 스레드에서 `Ref`·핸들·`SafePtr` 를 만지는 것. 전부 메인 스레드 전용이다.
- 순회 중인 컨테이너에 넣거나 빼는 것.
