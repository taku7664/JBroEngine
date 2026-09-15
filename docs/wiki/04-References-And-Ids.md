# 4. 참조와 식별자

무엇을 가리킬 때 어떤 타입을 쓰는지가 이 엔진에서 가장 자주 헷갈리는 부분이다. 표 하나로 먼저 정리한다.

| 누가 | 무엇을 | 무엇으로 | 크기 | 무효가 되면 |
|---|---|---|---|---|
| 엔진 내부 | 고유 소유 | `OwnerPtr<T>` | | 소멸자가 대상을 지운다 |
| 엔진 내부 | 비소유 참조 | `SafePtr<T>` | | `TryGet()` 이 `nullptr` |
| 스크립트 | `GameObject` | `GameObjectHandle` | 16B | 안전 멤버는 로그만 남기고 아무 일도 하지 않는다 |
| 스크립트 | 컴포넌트 · 스크립트 | `Ref<T>` | 24B | `Get()` 이 `nullptr` |
| 누구나 | 에셋 | `AssetId`(저장) + `AssetHandle`(런타임) | 8B + 8B | 해석 실패 |

스크립트에 노출하는 참조는 **딱 두 종류**다(D-5). 타입마다 핸들을 새로 만들지 않고, 두 크기는 영구 고정이다(D-44).

## 식별자 둘: 영속과 위치

```cpp
using InstanceId = std::uint64_t;        // 영속. 저장 파일에 적힌다

struct InstanceHandle                     // 8B. 이번 실행에서의 위치
{
    std::uint32_t Slot;                   // 풀 슬롯 인덱스
    std::uint32_t Gen;                    // 세대. 파괴될 때 증가한다
};
```

둘은 짝이다. 로드할 때 `InstanceId` 로 `InstanceHandle` 을 채우고(패치업), 그 뒤 프레임 루프에서는 핸들만 쓴다.
핸들이 살아 있으면 식별자 조회는 0회다.

### InstanceId 의 비트 배치

```
[ 42비트 타임스탬프(ms) ][ 10비트 세션 난수 ][ 12비트 시퀀스 ]
        139년 분량              머신·세션 구분      ms당 4096개
```

- 시간은 프레임당 한 번만 읽어 캐시하므로 생성 비용은 사실상 `++counter` 다.
- 값이 시간순으로 정렬되므로 "생성 순서" 필드가 따로 필요 없다.
- 세션 난수가 없으면 서로 다른 브랜치에서 같은 밀리초에 만든 오브젝트끼리 충돌한다. 다만 10비트라 1024회 실행에 한 바퀴 돌고,
  **실행 간 절대 비반복은 보장하지 않는다**(D-59). 저장 파일이 세션을 넘어 ID 를 신뢰해야 할 때 다시 연다.

## Ref\<T\>

컴포넌트와 스크립트를 가리키는 **영속 참조**다. 저장·로드·프리팹·핫 리로드를 넘는다.

```cpp
struct InstanceRef                        // 24B · POD · DLL 경계를 넘는다
{
    InstanceId     ObjectId;              // 영속. 저장되는 값
    InstanceId     ComponentId;           // 컴포넌트·스크립트 카테고리만 쓴다
    mutable InstanceHandle Cached;        // 런타임 캐시. 저장하지 않는다
};

template<typename T>
class Ref : public InstanceRef
{
public:
    T*   Get() const;                     // 무효면 nullptr
    T*   operator->() const;              // Get() 과 같다. Debug 에서 assert
    T&   operator*() const;
    bool IsValid() const;                 // Get() != nullptr
    void Clear();
    explicit operator bool() const;       // "설정됨" 만 본다. 해석하지 않는다
    bool operator==(const Ref&) const;    // ObjectId 와 ComponentId 비교
};
```

**저장부가 템플릿이 아닌 `InstanceRef` 인 이유**: 리플렉션·직렬화·인스펙터는 `Ref<Enemy>` 인지 `Ref<SpriteRenderer2D>` 인지 모른 채
`ObjectId` 를 읽고 써야 한다. 필드가 `Ref<T>` 안에만 있으면 그 코드가 `T` 를 알아야 하고, 타입마다 접근자를 등록하는 코드 생성으로 되돌아간다.

`Ref<T>` 는 `InstanceRef` 위에 데이터 멤버도 가상 함수도 얹지 않는다. 얹으면 24B POD 가 깨져 DLL 경계를 넘지 못한다.
`static_assert` 셋이 크기·standard layout·trivially copyable 을 붙잡고 있다.

`Ref<GameObject>` 는 컴파일되지 않는다. 오브젝트는 `GameObjectHandle` 이다.

### Get() 이 하는 일

1. `Cached` 가 설정돼 있으면 `ResolveInstanceByHandle` 로 슬롯을 본다. 살아 있고 세대가 같으면 그 포인터를 돌려준다.
2. 슬롯은 살아 있는데 세대만 다르면 **확정 사망**으로 단락한다. 해시 조회로 떨어지지 않는다(D-54).
3. 그 밖의 경우에만 `ResolveInstanceById` 로 `InstanceId` 를 조회하고, 찾으면 `Cached` 를 갱신한다.

로드 직후 모든 `Ref` 를 일괄 패치업하면 프레임 루프에서 3번은 일어나지 않는다. 런타임에 새로 만든 대상은 첫 접근에서 한 번만 3번을 탄다.

### 접근자는 하나, 쓰는 법은 둘

```cpp
if (T* p = ref.Get())          // 확인하고 쓴다
{
    p->Foo();
}
ref->Foo();                    // 확인 안 하고 쓴다. 무효면 크래시(Debug 에서는 assert)
```

경로가 둘인 것이 아니다. `operator->` 가 내부에서 `Get()` 을 부르고 Debug assert 만 다르다.
별도의 스코프 객체나 콜백 방식은 채택하지 않았다. C++17 의 보장된 복사 생략 때문에 복사·이동을 삭제한 타입으로도
멤버 저장을 막을 수 없어, 실제로 강제가 되지 않기 때문이다.

## GameObjectHandle

오브젝트 전용 16B 값이다. `operator->` 가 **없고**, 대신 자주 쓰는 동작을 안전 멤버로 준다.

```cpp
class GameObjectHandle final
{
public:
    bool IsValid() const;
    explicit operator bool() const;

    void Destroy();                       // 무효면 로그만 남기고 아무 일도 하지 않는다
    void SetActive(bool active);          // 같다
    bool IsActive() const;                // 무효면 false
    template<typename T> Ref<T> GetComponent() const;   // 무효면 빈 Ref
    InstanceId GetInstanceId() const;
};
```

왜 `operator->` 가 없는가. 안전 멤버 안에서 대상 해석에 실패하면 **그 자리에서 `return` 할 수 있어야** 하기 때문이다.
포인터를 내주면 그 뒤에 무엇을 하든 막을 수 없다.

무효 접근의 계약은 D-8 이다. **크래시도, 예외도, 절반 실행도 없다.** 사용자가 `if` 를 쓰지 않아도 안전해야 하고,
Release 빌드에서도 슬롯 범위와 세대 비교를 빼지 않는다. 죽은 대상용 가짜 인스턴스를 돌려주지도 않는다.
함수가 절반만 실행되는 것(죽은 적이 점수를 주는 식)이 무시보다 나쁘기 때문이다.

값을 돌려주는 접근은 실패가 드러나야 한다. `GetComponent<T>()` 가 `Ref<T>` 를 돌려주고 사용자가 `Get()` 을 확인하는 것이 그 방식이다.
없는 값을 기본값으로 대신 만들어 주지 않는다.

```cpp
GameObjectHandle target = ...;
target.Destroy();                                            // if 없이 안전
Ref<Component::Transform2D> t = target.GetComponent<Component::Transform2D>();
if (auto* value = t.Get())
{
    value->position.x += 1.0f;
}
```

## InstanceRegistry

핸들과 `Ref` 를 실제 포인터로 바꿔 주는 표다. **프로세스 전역이고 캔버스를 모른다**(D-44).
슬롯은 프로세스 전역이고 `InstanceId` 도 프로세스에서 유일하므로, 에디터 편집본과 Play 사본처럼 캔버스가 두 벌 있어도 해석이 모호하지 않다.

호스트와 스크립트 DLL 은 각각 Runtime 을 정적 링크하므로 레지스트리 사본도 둘이다. 그래서 DLL 은 로드 때 호스트의 포인터를 받아
자기 사본의 접근점에 **1회 바인딩**한다. 포인터를 한 번 묶는 것이라 "매 프레임 함수 테이블 우회 금지" 와 충돌하지 않는다.

`Ref<T>::Get()` 과 `GameObjectHandle::Resolve()` 가 이 레지스트리를 부르고 그 둘은 DLL 이 링크하므로,
레지스트리는 Tier S(`JBroRuntime/Internal/InstanceRegistry.h`)에 있어야 한다. `Canvas` 는 등록·해제하는 쪽이라 Tier E 에서 Tier S 를 보는 방향이고 문제가 없다.

세대는 이 레지스트리가 **단독으로** 관리한다. 풀은 세대를 따로 들지 않는다.

## SafePtr 와 OwnerPtr

엔진 내부의 참조는 핸들로 바꾸지 않고 `SafePtr` 를 그대로 쓴다(D-4). `GameObject::m_parent` / `m_children` / `m_components`,
`ComponentBase::m_owner`, 풀의 제어 블록이 전부 이 위에 서 있다.

- `OwnerPtr<T>` : 고유 소유. `MakeOwnerPtr<T>(...)` 로 만든다. `std::unique_ptr` 와 `new` 는 쓰지 않는다.
- `SafePtr<T>` : 비소유. 대상이 죽으면 `TryGet()` 이 `nullptr` 다. `OwnerPtr` 와 풀에서만 제어 블록이 생긴다.
- 둘 다 **메인 스레드 전용**이다. 워커 태스크에 캡처하거나 워커에서 참조 카운트를 바꾸지 않는다. `GameObjectHandle`·`Ref<T>` 도 같다(D-54).

핸들은 **스크립트 노출 표면에만** 쓴다. 스크립트가 다른 오브젝트를 오래 들고 있는 자리가 실제로 댕글링이 나는 곳이고, 거기만 덮는다.

## 스코프 규칙

서비스와 포인터를 얼마나 오래 들고 있어도 되는지는 **수명 스코프**로 판단한다.

| 스코프 | 대상 | 죽는 시점 |
|---|---|---|
| Process | 플랫폼, RHI 모듈·디바이스 | 프로세스 종료 |
| Project | 프레임워크, `Canvas`, 에셋 서비스 | 프로젝트 닫기 |
| ScriptModule | 스크립트 인스턴스와 스크립트가 소유한 객체 | 핫 리로드 |

자기보다 짧은 스코프의 포인터를 멤버로 캐시하지 않는다. 필요하면 그 스코프의 무효화 이벤트를 받고 다시 얻는다.
그리고 무효화에는 **명시적 재생성**으로 대응한다. 약참조가 조용히 `null` 이 되어 동작을 건너뛰게 두지 않는다.
디바이스 로스트의 올바른 처리는 null 검사가 아니라 GPU 리소스 재생성이다. 지금은 디바이스 로스트를 치명 오류로 보고 종료한다(D-16).

## 아직 열린 것

핫 리로드로 스크립트 인스턴스 슬롯이 바뀐 뒤 **이전 슬롯 캐시를 쓰지 않게 하는 경로**가 아직 없다.
모듈 세대는 올라가지만 그것을 `Ref` 캐시 무효화에 연결하는 메커니즘이 확정되지 않았다.
그래서 핫 리로드 참조 안전성은 완료로 표시하지 않는다(ProjectRule §8.1).
