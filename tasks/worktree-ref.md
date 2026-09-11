# W-ref · 식별자 · 참조 · 실 객체

> **역사 기록 — 현재 브랜치·병합 지시가 아니다.** 현재는 `main` 단일 워크트리에서 순차 작업한다.
> 이 문서는 당시 담당 범위와 설계 근거만 보존한다. 체크 상태와 본문이 충돌하면 현재 코드·테스트 및
> `todo.md` Decisions를 기준으로 판단한다.

**브랜치**: `work/ref` (경로: `../JBro-ref`)
**병합 순서**: 4번 (가장 큼. W-host 가 의존하므로 W-host 이전 필수)

## 목표

이 워크트리가 이번 리팩터링의 몸통 대부분을 담는다.

1. **`SafePtr<T>` 이식** — 기존 엔진에서. GameObject 내부 참조가 raw pointer 인 현재 상태를 바꾼다.
2. **`InstanceIdGenerator` 구현** — 프레임당 1회 시간 캐시 + 세션 난수 + 시퀀스.
3. **`Ref<T>` 구현** — 24B POD. 캐시 히트 시 InstanceId 안 읽는 해석 경로.
4. **`GameObjectHandle`** — GameObject 전용 16B 핸들. IF 없이 안전 멤버 (`.Destroy()` 등).
5. **`TObjectPool<T>`** 구현 — 청크 32슬롯 · 주소 불변 · generation.
6. **`GameObject` / `ComponentBase`** 실 구현.
7. **로드 후 InstanceId → {Slot, Gen} 패치업**.

## 소유 파일

### 신규 (이식 · 신설)

- `source/JBroEngine/Modules/JBroCore/Include/JBro/Types/SafePtr.h` (신규 이식 · 헤더-온리)
- `source/JBroEngine/Modules/JBroCore/Source/Core/InstanceIdGenerator.cpp` (신규)
- `source/JBroEngine/Modules/JBroCore/Source/Core/ObjectPool.cpp` (필요 시. 대부분 헤더-온리)
- `source/JBroEngine/Modules/JBroRuntime/Source/Ref.cpp` (신규 · Ref<T> 비-템플릿 부분)
- `source/JBroEngine/Modules/JBroRuntime/Include/JBro/Runtime/GameObjectHandle.h` (신규)
- `source/JBroEngine/Modules/JBroRuntime/Source/GameObjectHandle.cpp` (신규)

### 기존 리트로핏

- `source/JBroEngine/Modules/JBroCore/Include/JBro/Core/InstanceIdGenerator.h`
- `source/JBroEngine/Modules/JBroCore/Include/JBro/Core/ObjectPool.h`
- `source/JBroEngine/Modules/JBroRuntime/Include/JBro/Runtime/Ref.h`
- `source/JBroEngine/Modules/JBroRuntime/Include/JBro/Runtime/GameObject.h`
- `source/JBroEngine/Modules/JBroRuntime/Source/GameObject.cpp`
- `source/JBroEngine/Modules/JBroRuntime/Include/JBro/Runtime/Component.h`
- `source/JBroEngine/Modules/JBroRuntime/Source/Component.cpp`
- `source/JBroEngine/Modules/JBroFramework2D/Include/JBro/Framework2D/Canvas/Canvas.h`
- `source/JBroEngine/Modules/JBroFramework2D/Source/Canvas/Canvas.cpp`

**Canvas 는 W-framework 랑 공유하지 않냐?**
Canvas 의 **컴포넌트 풀 저장 구조 · InstanceId 발급 · 오브젝트 풀 인스턴스화** 는 W-ref. Canvas 의
공개 API 시그니처는 이미 확정 (B0/Stage B). W-framework 는 컴포넌트 파생과 시스템 로직만 얹는다.
Canvas 파일 자체를 W-ref 가 실제 구현할 수밖에 없어 소유는 W-ref.

## 배경

- 다이어그램 SCRIPT LAYER 세 박스가 참조 3분화를 명시:
  - **GameObject 참조**: `GameObjectHandle` (16B, `{Slot, Gen, InstanceId}`). IF 없이 안전 호출.
  - **저장되는 프로퍼티**: `Ref<T>` (24B, `{ObjectId, ComponentId, {Slot, Gen}}`).
  - **짧은 스코프 컴포넌트 접근**: 원시 `T*` — **최근 결정에서 `Ref<T>` 반환으로 통일**
    (`GetComponent<T>()` → `Ref<T>`). 원시 포인터는 안 씀.
- **`SafePtr<T>`** 는 엔진 내부 전용 — GameObject 의 parent/children/components/layer 참조.
  스크립트 노출 표면 아님.
- **Handle 은 스크립트 노출 표면 전용**. 엔진 내부는 SafePtr.

### InstanceId 비트 구조

```
[ 42비트 ms 타임스탬프 ][ 10비트 세션 난수 ][ 12비트 시퀀스 ]
       139년 분량            머신·세션 구분      ms 당 4096개
```

- 시간은 프레임당 1회만 읽어 캐시 → 생성 비용은 `++counter`.
- 값이 시간순 정렬되어 `m_creationOrder` 를 흡수한다.
- 세션 난수가 없으면 다른 브랜치·다른 실행에서 같은 ms 에 만든 것끼리 충돌.

### `Ref<T>` 24B POD 저장부 (B0 에서 확정)

```cpp
struct InstanceHandle          // 8B
{
    uint32 Slot = 0;
    uint32 Gen  = 0;
};

struct InstanceRef             // 24B · POD
{
    InstanceId     ObjectId    = InvalidInstanceId;
    InstanceId     ComponentId = InvalidInstanceId;
    InstanceHandle Cached;
};

template<typename T>
class Ref : public InstanceRef { ... };
```

## 작업 항목

### 1. `SafePtr<T>` 이식

**Why**: 다이어그램 GameObject 박스: `SafePtr<GameObject> m_parent / m_children`,
`SafePtr<Layer> m_layer`, `vector<SafePtr<ComponentBase>> m_components`. 지금 raw pointer 로
짜여 있어 파괴 시 dangling.

**How**:

1. 원본 위치 확인: `C:\Users\박주형\source\repos\JBroEngine\Engine\Utillity\Pointer\SafePtr.h`
   (**헤더-온리 · 477줄**. `.cpp` 없음).
2. `SafePtr.h` 를 `JBroCore/Include/JBro/Types/SafePtr.h` 로 복사 이식.
   - 네임스페이스를 `namespace JBro { ... }` 로 감쌈.
   - include 경로 `"Utillity/…"` → `<JBro/…>` 조정.
3. `ControlBlock` (비원자 refcount) 은 `SafePtr` 내부 구현 세부. 스레드-세이프하지 않음이 계약.
   메모리에 있는 `[SafePtr 는 메인 스레드 전용]` 규칙을 헤더 상단 주석으로 명시.
4. `EnableSafeFromThis<T>` (기존 엔진에 함께 정의됨) 도 같이 이식. GameObject 가 상속한다.
5. `<JBro/Types/Types.h>` 에 `SafePtr.h` include 추가.

### 2. `InstanceIdGenerator` 구현

**Why**: F1. 헤더는 B0 에서 만들었고, 몸통이 없다.

**How** (`JBroCore/Source/Core/InstanceIdGenerator.cpp` 신규):

```cpp
#include <JBro/Core/InstanceIdGenerator.h>

#include <chrono>
#include <random>

namespace JBro
{
    namespace
    {
        std::uint32_t GenerateSessionRandom()
        {
            std::random_device rd;
            std::mt19937 rng(rd());
            std::uniform_int_distribution<std::uint32_t> dist(0, (1u << 10) - 1);
            return dist(rng);
        }
    }

    void InstanceIdGenerator::BeginFrame()
    {
        using namespace std::chrono;
        m_cachedMs = static_cast<std::uint64_t>(
            duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
        m_sequence = 0;
        if (m_session == 0) m_session = GenerateSessionRandom();
    }

    InstanceId InstanceIdGenerator::Generate()
    {
        const std::uint64_t timestamp = m_cachedMs & ((1ULL << 42) - 1);
        const std::uint64_t session   = m_session & ((1ULL << 10) - 1);
        const std::uint64_t seq       = (m_sequence++) & ((1ULL << 12) - 1);
        return (timestamp << 22) | (session << 12) | seq;
    }
}
```

유닛테스트: 같은 프레임에서 4096 개 다른 값 생성, 5000 번째부터는 시퀀스 오버플로 → 어떻게 처리?
(경고 로그 + 랩. 또는 assert. 결정 필요.)

### 3. `TObjectPool<T>` 구현

**Why**: Canvas 가 오브젝트 · 컴포넌트를 이걸로 소유. 주소 안정성 계약.

**How** (`JBroCore/Include/JBro/Core/ObjectPool.h` 확장):

1. 청크 배열: `Array<OwnerPtr<Chunk>> m_chunks`, `Chunk` = 32 슬롯의 aligned storage.
2. 슬롯마다 `{ generation: uint32, alive: bool }` 상태.
3. Free list: `Array<uint32> m_freeSlots`.
4. `Create` — free 있으면 재사용 (generation++), 없으면 새 청크. placement new. 슬롯 alive=true.
5. `Destroy(T*)` — 슬롯 찾아 destroy_at, generation++, free list push. **주소는 그대로**.
6. `ForEachLive(fn)` — 모든 청크 · 모든 슬롯 순회, alive 만 방문.

**주소 안정성 계약**: 한 번 발급한 `T*` 는 그 슬롯이 destroy 되기 전까지 유효. Create 로 재사용
되면 같은 주소지만 generation 이 다르다.

### 4. `Ref<T>` 구현 (해석 경로 · 접근자)

**Why**: G2 · G3. 헤더는 B0 에서 시그니처만 확정, 몸통 없음.

**How** (`JBroRuntime/Include/JBro/Runtime/Ref.h` 채우고 `Ref.cpp` 신규):

1. `Ref<T>::Get()` 해석 경로:
   ```cpp
   template<typename T>
   T* Ref<T>::Get() const
   {
       if (ObjectId == InvalidInstanceId) return nullptr;
       // 캐시 히트: Slot 범위 + Gen 비교. InstanceId 안 읽음.
       T* candidate = ResolveByHandle<T>(Cached);
       if (candidate != nullptr) return candidate;
       // 캐시 미스: InstanceId 로 재해석 후 Cached 갱신.
       Ref<T>* mutable_this = const_cast<Ref<T>*>(this);
       auto resolved = ResolveByInstanceId<T>(ObjectId, ComponentId);
       if (resolved.pointer != nullptr) {
           mutable_this->Cached = resolved.handle;
       }
       return resolved.pointer;
   }
   ```
2. `ResolveByHandle<T>` / `ResolveByInstanceId<T>` 는 **프로세스-전역 레지스트리**를 참조 (D-26).
   레지스트리는 `JBro::Internal::InstanceRegistry` 싱글턴이다.
   `Canvas::CreateObject` 시 `registry.Register(instanceId, obj)`,
   `Canvas::DestroyObject` 시 `registry.Unregister(instanceId)`.
   여러 캔버스가 로드돼도 InstanceId 가 프로세스-유일 (세션×ms×시퀀스) 이라 충돌 없음.
   Unity 의 (자산 GUID = 영구 / InstanceID = 활성 런타임) 이원 구조와 동일.
3. `operator->()` — Debug 에서 `assert(IsValid())`, Release 에서는 `Get()` 과 동일 (무효면 nullptr
   deref → 크래시).
4. `IsValid()` — `Get() != nullptr`.
5. `Clear()` — 모든 필드 초기화.
6. `explicit operator bool()` — 저장되어 있는지만 (`ObjectId != InvalidInstanceId`). 해석 안 함.

**주의**: G5 안전 멤버 (`Destroy()` 등) 는 `Ref<GameObject>` 특수화가 아니라 **`GameObjectHandle`**
로 나눴다 (다음 항목). `Ref<T>` 자체는 이 특수화를 두지 않는다.

### 5. `GameObjectHandle` 신규

**Why**: 다이어그램 SCRIPT LAYER ①. **GameObject 만 예외**로 IF 없이 안전 호출 가능한 별도
16B 핸들. 나머지 참조는 `Ref<T>` 로 통일.

**How** (`JBroRuntime/Include/JBro/Runtime/GameObjectHandle.h` 신규):

```cpp
#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Runtime/Ref.h>

#include <cstdint>

namespace JBro
{
    class GameObject;
    class ComponentBase;

    // 스크립트 노출 표면에서 GameObject 를 참조하는 유일한 방법.
    // 16B. IF 없이 안전 호출 가능. operator-> 없음.
    class GameObjectHandle
    {
    public:
        GameObjectHandle() = default;

        // 안전 멤버 — 무효하면 로그 후 무시(아무 일 없음).
        void            Destroy();
        void            SetActive(bool active);
        bool            IsActiveInHierarchy() const;

        // 컴포넌트 얻기 — 저장 가능한 Ref<T> 반환.
        template<typename T>
        Ref<T>          GetComponent() const;

        // 상태 조회.
        bool            IsValid()   const;
        InstanceId      GetInstanceId() const { return m_instanceId; }
        explicit operator bool()   const { return IsValid(); }

    private:
        InstanceHandle m_cached;                     // 8B (Slot + Gen)
        InstanceId     m_instanceId = InvalidInstanceId; // 8B

        // 내부 해석 — 캐시-히트-우선 경로.
        // 1) m_cached 유효 → InstanceRegistry 에서 slot 범위/gen 확인 후 반환.
        // 2) 캐시 미스 → InstanceRegistry.Find(m_instanceId) 로 재해석 + m_cached 갱신.
        GameObject* Resolve() const;
    };

    static_assert(sizeof(GameObjectHandle) == 16, "handle must be 16 bytes");
    static_assert(std::is_standard_layout_v<GameObjectHandle>);
    static_assert(std::is_trivially_copyable_v<GameObjectHandle>);
}
```

`GameObjectHandle::Destroy()` 몸통:
```cpp
void GameObjectHandle::Destroy()
{
    GameObject* obj = Resolve();
    if (obj == nullptr) {
        // 무효 접근 로그 (사용자가 이미 파괴한 오브젝트를 다시 파괴 시도). 크래시 없음.
        JBroLog::Warn("GameObjectHandle::Destroy on invalid handle (id={})", m_instanceId);
        return;
    }
    obj->GetCanvas()->DestroyObject(obj);
}
```

### 6. `GameObject` 리트로핏 (SafePtr 로)

**Why**: 다이어그램 계약. 지금은 raw `GameObject*`, `Array<GameObject*>`, `Array<ComponentBase*>`,
`uint32_t m_layerIndex`.

**How** (`GameObject.h/.cpp` 수정):

1. 상속 추가: `class GameObject : public EnableSafeFromThis<GameObject>` (EnableSafeFromThis
   함께 이식).
2. 필드 교체 (D-3 완화 반영 — **Transform 필드는 두지 않는다**):
   ```cpp
   SafePtr<GameObject>            m_parent;
   Array<SafePtr<GameObject>>     m_children;
   Array<SafePtr<ComponentBase>>  m_components;
   SafePtr<Layer>                 m_layer;      // Layer 참조.
   uint32_t                       m_layerIndex = 0;  // GetLayerIndex() O(1) 캐시.
   // Transform 은 컴포넌트로 존재 (Component::Transform2D 또는 Transform3D).
   // 프레임워크가 attach 하고, GameObject 는 dimension 을 모른다.
   ```
3. `GameObject::SetLayer(Layer*)` — SafePtr 갱신 + 인덱스 캐시 갱신. `SetLayerIndex(uint32)` 는
   삭제 (인덱스는 Layer 로부터 유도).
4. `SetParent` / `AttachComponent` / `DetachComponent` 는 SafePtr 로 갱신.
5. `InstanceId` 를 Canvas 로부터 발급받도록 생성자 수정 — Canvas 가 `Create` 시
   `object->m_instanceId = idgen.Generate()`.
6. **`GameObject::GetComponent<T>()` 추가** — `m_components` 선형 순회 + `GetTypeId()` 비교.
   기존 엔진의 `FindComponentRaw` 와 같은 구조. n=5 기준 캐시-핫이라 해시맵보다 빠르다 (D-30).
   `GetComponents<T>()` 는 복수형 반환. 첫 매칭만 원하면 `GetComponent<T>()` (D-31).

### 7. `ComponentBase` 확정

**Why**: 컴포넌트 파생 계약. W-framework 가 여기 파생할 컴포넌트들을 만든다.

**How** (`Component.h` 확장):

```cpp
class ComponentBase
{
public:
    virtual ~ComponentBase() = default;

    virtual ComponentTypeId GetTypeId()      const = 0;
    // Optional 하지만 관례상 파생 클래스가 제공한다:
    // static constexpr const char* StaticTypeName();

    GameObject* GetOwner() const                       { return m_owner; }
    void        SetOwner(GameObject* owner)            { m_owner = owner; }

    bool        IsEnabled() const                      { return m_enabled; }
    void        SetEnabled(bool enabled)               { m_enabled = enabled; }
    bool        IsActiveComponent() const;             // 오너 계층 활성 && m_enabled

    InstanceId  GetInstanceId() const                  { return m_instanceId; }
    void        SetInstanceId(InstanceId id)           { m_instanceId = id; }

protected:
    GameObject* m_owner      = nullptr;
    InstanceId  m_instanceId = InvalidInstanceId;
    bool        m_enabled    = true;
};
```

### 8. Canvas 실 구현 (오브젝트 풀 · 컴포넌트 풀)

**Why**: Canvas 는 지금 껍데기다. W-ref 가 오브젝트 풀 · 컴포넌트 풀을 실제로 인스턴스화.

**How**:

1. `Canvas::CreateObject(name)` 구현:
   ```cpp
   GameObject* obj = m_objects->Create();
   obj->SetInstanceId(m_idGen.Generate());
   obj->SetCanvas(this);
   if (name != nullptr) obj->SetTag(name);
   return obj;
   ```
2. `Canvas::DestroyObject(obj)` 구현:
   - 자식들 재귀 파괴.
   - 컴포넌트들 각자 풀에서 해제.
   - 오브젝트 풀에서 slot 반환 (generation++).
3. `AttachComponent<T>()`:
   - 타입별 버킷 조회 (`TObjectPool<T>*`). 없으면 새로 만들어 등록.
   - `T* comp = pool->Create()`.
   - `comp->SetInstanceId(m_idGen.Generate())`.
   - `owner->AttachComponent(comp)`.
   - return comp.
4. `ForEach<T>(fn)` 는 해당 타입 풀의 `ForEachLive(fn)` 위임.

### 9. 로드 후 InstanceId → {Slot, Gen} 패치업 (F4)

**Why**: 저장된 씬 파일을 로드할 때 모든 오브젝트가 이전 InstanceId 를 가진다. 로드 후 새 슬롯에
배치되므로 `Ref<T>` 캐시 (`Slot, Gen`) 를 그때 채워 준다.

**How**:

1. `Canvas::LoadFromFile(path)` (또는 `LoadFromSnapshot(...)`) 후:
   - InstanceId → (실 오브젝트/컴포넌트 포인터) 맵 하나 만든다.
   - 씬 안의 모든 `Ref<T>` 필드를 순회 (리플렉션으로) — 이 부분은 리플렉션 인프라 필요.
     지금은 리플렉션이 없으므로 **계약과 함수 시그니처만 정의**, 실제 순회는 리플렉션 붙을 때.
2. 패치업 후 첫 접근에서 캐시가 채워졌으므로 이후 조회는 O(1).

**주의**: 리플렉션이 별도 워크트리에 없어서 F4 는 이번엔 계약만.

### 10. 잔여 항목

- **F5 축소본**: `Ref<T>::Get()` 캐시 히트 시 InstanceId 안 참조하는 유닛테스트.
- **F6**: `TObjectPool` 주소 안정성 계약 테스트 (200 개 성장해도 첫 원소 주소 안 바뀜).
- **G8**: 파괴 후 재사용 슬롯에서 예전 `Ref` 가 `nullptr` 돌려주는 테스트,
  `GameObjectHandle::Destroy` 를 무효 핸들에서 호출 → 크래시 없음.

## 검증

- [ ] Debug / Release x64 빌드 통과 (경고 0, 오류 0)
- [ ] `JBroTests` 통과
- [ ] SafePtr 유닛테스트: 파괴 후 IsValid() = false
- [ ] TObjectPool 주소 안정성 테스트: 첫 원소 주소 불변
- [ ] Ref<T> 캐시 히트 유닛테스트: 두 번째 Get() 이 InstanceId 안 참조
- [ ] GameObjectHandle 안전 호출: 파괴된 핸들 `.Destroy()` 크래시 없음
- [ ] InstanceIdGenerator 유닛테스트: 4096 시퀀스, 세션 난수 반복 없음
- [ ] Canvas CreateObject / DestroyObject 왕복, 오브젝트 count 정합

## 다른 워크트리와의 인터페이스

- **W-framework 의존**: 이 워크트리가 완성돼야 컴포넌트 파생 (B5) · 시스템 몸통 (ForEach) 가능.
- **W-host 의존**: `GameObjectHandle`, `Ref<T>` 를 `ScriptAPI.h` 프렐류드에 노출.
- **W-platform 무관**: 이 워크트리는 하드웨어 계층을 안 만짐.

## 병합

- 이 워크트리가 가장 크다. 서브 브랜치로 나눠 (SafePtr → InstanceIdGen → TObjectPool → Ref →
  GameObjectHandle → Canvas 통합) 점진 커밋 권장.
- 자체 검증 통과 후 main 병합.
- 병합 후 W-framework 와 W-host 는 rebase.

## 2026-09-11 후속: SafePtr 이식 로직 대조

Updates: 위 1번 SafePtr 이식의 현재 검증 근거. 이 절은 과거 워크트리 완료 표시를 복원하는 것이 아니라
현재 파일과 읽기 전용 구 엔진 원본의 대조 결과다.

- 원본 `Engine/Utillity/Pointer/SafePtr.h`는 477줄, 신규 `JBro/Types/SafePtr.h`는 480줄이다.
- `git diff --no-index`로 두 파일을 직접 비교한 차이는 UTF-8 BOM 1개와 `namespace JBro` 여닫는 두 줄이다.
- 템플릿 저장부, ControlBlock 카운트, 소유권 비교, 정적·동적 캐스트를 포함한 실행 로직 차이는 0건이다.
- 구 엔진 원본은 수정하지 않았다.

## 2026-09-11 후속: TObjectPool allocator 계약

Updates: 위 3번의 `Array<OwnerPtr<Chunk>>` 예시는 당시 주소 안정성 설계 기록이다. 현재 구현은
청크 메모리를 생성자에서 받은 `JAllocator`로 할당·반환하기 위해 이동 전용 청크 소유 래퍼를 사용한다.

- 32슬롯 청크 자체는 전달받은 allocator가 소유한다. 청크 포인터 목록과 free-list는 JBro `Array`의
  모듈 로컬 저장소를 사용한다.
- 청크 소유 래퍼는 `noexcept` 이동 뒤 원본을 비워 `Array` 성장 중 이중 해제를 막는다. 청크 생성이나
  목록 추가가 실패하면 아직 게시되지 않은 청크를 같은 allocator로 즉시 반환한다.
- 200개 객체가 7개 청크를 할당하고 풀 파괴 시 7개를 모두 반환하는지 검사한다. 기존 주소 안정성,
  free-list 재사용, `SafePtr` 무효화 검증도 함께 통과했다.

## 2026-09-12 후속: Canvas 소유 경로 정정

Updates: 위 소유 파일 목록의 `Framework2D/Canvas/**` 경로. 당시 작업 분할 기록은 보존하지만 현재
구현 위치로 사용하지 않는다.

- D-40에 따라 공통 `Canvas`와 `Layer`의 선언·구현은 `JBroRuntime`으로 이동했다.
- D-41에 따라 블렌드·불투명도·공간·패럴랙스·별도 합성 텍스처는 Framework2D의 `Layer2D`가 소유한다.
- Framework별 Canvas 복제본은 만들지 않는다. Framework2D와 Framework3D가 같은 Runtime Canvas를
  각자의 실행 인스턴스로 소유한다.
- 기존 레이어 미존재 경로가 `Array::IndexOfBy()`의 실패 값 대신 배열 크기를 비교하던 오류도 함께
  수정했다. 파괴된 레이어의 조회·이동·재파괴·오브젝트 배정은 범위 밖 접근 없이 실패한다.
