# Jbro C++ Script Object Safety Draft

> **역사 초안 — 현재 계약이나 구현 지시로 사용하지 않는다.** 이 문서에는 폐기된 단일 `Ref<T>`,
> `Ref<GameObject>`, World/ECS, Canvas 싱글턴 모델이 본문과 대체 요약에 함께 남아 있다. 부분적인
> `[대체됨]` 표시는 문서 전체의 현행성을 보장하지 않는다. 현재 계약은 `ProjectRule.md`, 변경 근거는
> `tasks/todo.md`의 Decisions를 사용한다. 아래 본문은 설계 변천을 보존하기 위한 자료다.

> **이 문서의 어느 절도 현재 구현 계약으로 사용하지 않는다.** 아래의 대체 요약·최종 구조·코드
> 예시는 모두 2026-09-02 당시 검토 기록이며, 이후 확정된 `GameObjectHandle` + `Ref<T>` 이원 모델과
> Canvas 직접 소유 모델을 반영하지 않는다. 현재 계약은 [ProjectRule.md](./ProjectRule.md), 변경 근거와
> 미결정 항목은 `tasks/todo.md`를 사용한다.

## 1. 목표

C++ 스크립트에서 `GameObject*`, `Component*` 같은 raw pointer를 장기간 보관하지 않도록 하고,
객체가 파괴된 뒤에도 댕글링 포인터가 발생하지 않는 구조를 만든다.

핵심 방향:

- 스크립트에 노출되는 `GameObject`는 실제 객체가 아니다.
- `GameObject`는 실제 객체를 찾기 위한 **Handle / Proxy 값 타입**이다.
- 실제 객체 상태는 `World / Scene / ECS Storage`가 소유한다.
- 객체 파괴 시 Handle의 generation을 변경해 기존 참조를 무효화한다.
- Ref Counting은 사용하지 않는다.

---

# 2. Object Handle

기본 Handle:

```cpp
struct ObjectHandle
{
    uint32_t index;
    uint32_t generation;
};
```

Slot 예:

```cpp
struct ObjectSlot
{
    uint32_t generation;
    bool alive;
};
```

객체 생성:

```text
index      = 42
generation = 7
```

스크립트의 `GameObject`는:

```text
{ index = 42, generation = 7 }
```

만 가진다.

---

# 3. 객체 파괴

객체 파괴 시:

```text
Before

Slot 42
generation = 7
alive      = true
```

```text
After

Slot 42
generation = 8
alive      = false
```

기존 `GameObject`:

```text
{ index = 42, generation = 7 }
```

는 더 이상 유효하지 않다.

같은 Slot이 새로운 객체에 재사용되어도 generation이 다르기 때문에
과거 Handle이 새로운 객체를 잘못 참조하지 않는다.

---

# 4. GameObject는 Proxy / Handle 객체

사용자 API:

```cpp
class GameObject
{
public:
    bool IsValid() const;

    void SetActive(bool active);
    void Destroy();

private:
    WorldHandle mWorld;
    ObjectHandle mObject;
};
```

실제 게임 상태는 `GameObject` 안에 존재하지 않는다.

```text
GameObject
    ├─ World Handle
    └─ Object Handle
```

실제 데이터:

```text
World / Scene
├─ Entity Storage
├─ Component Storage
├─ Transform Storage
├─ Object Registry
└─ Scene Data
```

---

# 5. 동작 방식

사용자:

```cpp
GameObject enemy = FindGameObject("Enemy");

enemy.SetActive(false);
```

내부적으로는 개념적으로:

```cpp
void GameObject::SetActive(bool active)
{
    World* world = ResolveWorld(mWorld);

    if (!world)
        RaiseInvalidObjectAccess();

    world->SetActive(mObject, active);
}
```

World 내부:

```cpp
void World::SetActive(
    ObjectHandle handle,
    bool active)
{
    ObjectSlot* slot = Resolve(handle);

    if (!slot)
        RaiseInvalidObjectAccess();

    // 실제 상태 변경
}
```

즉:

```text
GameObject
    ↓
Handle 전달
    ↓
World / Scene
    ↓
실제 Object / ECS 데이터 조작
```

---

# 6. Global SceneManager 직접 참조는 피한다

> **[대체됨]** `GameObject` 에 `WorldHandle` 을 넣지 않는다.
> Canvas는 프로세스당 하나이므로(아키텍처 초안 §12: 1 Process / 1 Engine Instance / 1 Framework)
> 어느 컨테이너를 볼지 식별할 필요가 없다. Handle은 `{ index, generation }` 만 보관하고,
> `WorldHandle` → `World*` 조회 테이블도 두지 않는다. 핸들 해석이 배열 인덱싱 한 번으로 끝난다.
>
> 이 절이 든 이유 중 "여러 Scene / World 지원" 은 채택하지 않는다.
> `Scene` 은 어떤 형태로도 두지 않으며(ProjectRule §7), Canvas도 하나다.
> "PIE / Editor Preview 확장" 은 Canvas 두 벌 대신 스냅샷·복원으로 대응한다.
>
> **재검토 지점**: 에디터 Play 모드를 "정지 시 원상복구" 로 만들 때 Canvas 두 벌이 필요해질 수 있다.
> 스냅샷·복원으로 해결되지 않으면 Handle 타입을 바꿔야 하고, 그때는 사용자 게임 코드까지 영향을 받는다.

아래 구조:

```cpp
SceneManager::Get().SetActive(handle, true);
```

보다는 `GameObject`가 자신이 속한 World를 식별할 수 있게 하는 것을 권장한다.

예:

```cpp
struct GameObject
{
    WorldHandle world;
    ObjectHandle object;
};
```

이유:

- 여러 Scene / World 지원 가능
- PIE / Editor Preview 확장 가능
- 전역 Singleton 의존 감소
- 테스트 용이
- 객체가 어느 World에 속하는지 명확

---

# 7. Null / Invalid 접근 정책

> **[대체됨]** 무효 접근을 "Script Runtime Error" 로 처리하지 않는다.
> **로그를 남기고 아무 일도 하지 않는다.** 사용자가 `if` 를 쓰지 않아도 안전해야 한다.
>
> 이유: 게임플레이 코드는 "이번 프레임에 대상이 죽었을 수도 있음" 이 상시 상황이라
> 호출마다 검사를 강요하면 코드가 소음으로 덮인다.
> 다만 이 보장은 **`Ref<GameObject>` 의 안전 멤버 경로에만** 해당한다.
> `->` 는 "포인터를 얻고 → 그 포인터로 부른다" 는 2단계라 중간에 중단할 자리가 없어서,
> 안전 멤버는 `->` 를 쓰지 않고 멤버 함수 안에서 그냥 `return` 한다.
> `ref->Foo()` 로 직접 쓰는 무검사 경로는 이 보장을 하지 않는다 — 선택은 사용자가 한다.
>
> 값을 돌려주는 접근만 실패가 드러나게 한다(`TryGetPosition(out)`).
> Release 에서도 검사(슬롯 범위 + 세대 비교)를 제거하지 않는다는 원칙은 그대로다.
> 자세한 내용은 ProjectRule §6.1 참고.

Handle 구조는 댕글링 포인터를 제거하지만,
삭제된 객체에 접근하는 논리 오류까지 자동으로 없애지는 않는다.

두 API를 구분한다.

## 안전한 선택적 접근

```cpp
if (enemy.IsValid())
{
    enemy.SetActive(false);
}
```

또는 내부적으로 `TryResolve()` 계열을 제공한다.

## 반드시 존재해야 하는 접근

```cpp
enemy.SetActive(false);
```

이때 객체가 이미 파괴됐다면:

```text
Script Runtime Error
```

를 발생시킨다.

Release에서도 체크를 제거하지 않는다.

단순 `assert()`만 사용하는 것은 피한다.

---

# 8. Null Object 패턴은 사용하지 않는다

> **[유지 · 근거 보강]** 가짜 인스턴스를 돌려주지 않는다는 결론은 그대로다.
> 다만 이유가 하나 더 있다 — **함수가 절반만 실행된다.**
>
> ```cpp
> void Enemy::Dead()
> {
>     Score::Add(10);              // 가짜 인스턴스여도 이건 실행된다
>     GetGameObject().Destroy();   // 이건 무효 핸들이라 무시
> }
> ```
>
> 죽은 적이 점수를 준다. 아무 일도 안 하는 것보다 나쁘다.
> §7 의 "로그 후 무시" 는 **호출 자체를 하지 않는 것**이라 이 문제가 없다.

Invalid Handle 접근 시 가짜 `EmptyGameObject`를 반환하지 않는다.

이유:

```cpp
enemy.TakeDamage(100);
```

에서 enemy가 이미 삭제되었는데 아무 일도 일어나지 않으면
크래시보다 찾기 어려운 논리 버그가 된다.

따라서:

```text
Invalid Handle
    ↓
명확한 Runtime Error
```

를 기본 정책으로 한다.

---

# 9. Component도 같은 구조 적용 가능

> **[대체됨]** 컴포넌트 전용 프록시 타입을 만들지 않는다. 컴포넌트도 `Ref<T>` 를 쓴다.
>
> ```cpp
> JPROP() Ref<Component::Rigidbody2D> Body;          // 저장 · 직렬화 · 인스펙터
> if (auto* rb = Body.Get()) rb->AddForce(f);        // 그 자리에서 쓰고 버린다
> ```
>
> `GetComponent<T>()` 는 `Ref<T>` 를 반환한다. 원시 포인터는 저장할 수 없어 매 프레임
> 다시 찾아야 하고 그 조회가 선형 탐색이기 때문이다. `Ref<T>` 로 받아두면 이후 상수 시간이다.
>
> 컴포넌트마다 핸들 타입을 코드 생성으로 뽑는 안은 버렸다 —
> 생성 전까지 사용자 코드가 컴파일되지 않아, 스크립트를 막 작성한 시점에 편집기가 깨진다.


사용자:

```cpp
Transform2D transform =
    gameObject.GetComponent<Transform2D>();
```

`Transform2D` 역시 실제 Component 포인터가 아니라
Handle / Proxy로 만들 수 있다.

예:

```text
Transform2D
    ├─ WorldHandle
    ├─ EntityHandle
    └─ ComponentType
```

호출:

```cpp
transform.SetPosition({ 10.0f, 20.0f });
```

내부:

```text
Transform2D Proxy
    ↓
World
    ↓
Transform2D Storage
    ↓
실제 Component 데이터 수정
```

---

# 10. Raw Pointer 정책

장기간 raw pointer 저장은 금지한다.

금지:

```cpp
class EnemyAI
{
    GameObjectImpl* mTarget;
};
```

권장:

```cpp
class EnemyAI
{
    GameObject mTarget;
};
```

단, 이미 검증한 객체를 짧은 Scope에서 임시로 사용하는 것은 허용할 수 있다.

```cpp
void Update()
{
    auto* enemy = world.TryResolve(mTarget);

    if (!enemy)
        return;

    enemy->UpdateA();
    enemy->UpdateB();
}
```

원칙:

> Raw pointer는 장기 소유 / 저장하지 않고,
> 검증 이후 현재 Scope에서만 임시 borrow 용도로 사용한다.

---

# 11. 성능 고려

매 API 호출마다:

```text
Handle
    ↓
World Lookup
    ↓
Index Lookup
    ↓
Generation Check
```

가 발생할 수 있다.

따라서 Hot Path에서는 한 번 Resolve 후 짧은 Scope에서 재사용할 수 있다.

```cpp
if (auto* object = world.TryResolve(handle))
{
    object->UpdateA();
    object->UpdateB();
    object->UpdateC();
}
```

하지만 해당 pointer를 Member로 저장하지 않는다.

---

# 12. 권장 명칭

> **[일부 대체됨]** 채택하는 이름은 `InstanceId`, `InstanceHandle`, `InstanceRef`, `Ref<T>` 다.
> `WorldHandle` / `EntityHandle` / `ComponentRef<T>` 는 도입하지 않는다(§6·§9 대체 참고).
> 클래스 이름에는 ProjectRule §10 에 따라 `C` 접두를 붙이되,
> 스크립트에 노출되는 값 타입(`GameObject`, `Ref<T>`)은 사용자 코드에 그대로 보이는
> API 표면이므로 접두 없이 둔다. 기반 타입만 `JBro` 접두를 붙여 전역 이름 충돌을 피한다.
>
> `SafePtr` 이라는 이름을 스크립트 레이어에 쓰지 않는다는 이 절의 취지는 유지한다.
> 다만 **엔진 내부에서는 `SafePtr` 를 계속 쓴다** — 이름을 바꾸지도, 없애지도 않는다.
>
> `SafePtr` 이라는 이름을 스크립트 레이어에 쓰지 않는다는 이 절의 취지는 유지한다.

`SafePtr`보다는 실제 의미가 드러나는 이름을 권장한다.

예:

```text
GameObject
ObjectHandle
ObjectRef<T>
EntityHandle
ComponentRef<T>
WorldHandle
```

`Ptr`이라는 이름은 실제 메모리 주소를 보관하는 타입으로 오해하기 쉽다.

---

# 13. 최종 구조

> **[대체됨]** 아래 그림에서 `WorldHandle` 줄을 제거하고, `World` 는 `Canvas` 가 소유하는
> ECS 저장소로 읽는다. `Scene Data` 항목은 두지 않는다. 갱신된 구조는 다음과 같다.
>
> ```text
> User C++ Script
>       |
>       v
>    GameObject          { index, generation }
>       |
>       v
>     Canvas
>       |
>       +-> World (ECS)
>             +-> Entity Slot (generation)
>             +-> Component Storage
>       +-> Layer (합성 전용, 수명 소유 안 함)
> ```

```text
User C++ Script
      |
      v
   GameObject
      |
      | ObjectHandle
      | WorldHandle
      v
     World
      |
      +-> Object Registry
      +-> ECS Storage
      +-> Component Storage
      +-> Scene Data
```

`GameObject` 자체는 작은 값 타입이며,
복사해도 Ref Count는 증가하지 않는다.

```cpp
GameObject a = player;
GameObject b = player;
```

두 객체는 같은 Handle을 복사해서 들고 있을 뿐이다.

실제 객체 Lifetime은 `World / Scene`이 관리한다.

---

# 14. 핵심 규칙 요약

1. 사용자 C++ Script에 장기 raw pointer를 노출하지 않는다.
2. `GameObject`는 실제 객체가 아니라 Handle / Proxy다.
3. Handle은 `index + generation`을 사용한다.
4. 실제 객체와 Component 데이터는 World / ECS가 소유한다.
5. 객체 파괴 시 generation을 변경한다.
6. 과거 Handle은 자동으로 invalid 상태가 된다.
7. Ref Counting은 사용하지 않는다.
8. Invalid 접근은 Null Object로 숨기지 않는다.
9. 반드시 존재해야 하는 접근은 Runtime Error로 처리한다.
10. Raw pointer는 검증 이후 짧은 Scope에서만 임시 사용한다.
