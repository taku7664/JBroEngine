# JBroScript 문법

> 2026-09-15 까지 빡대리와 논의한 **사용자가 쓰는 문법**을 모은 문서다. **언어도 `jbroc` 도 아직 구현하지 않았다.**
> **2026-09-29 에 참조와 null 을 다시 정했다(D-262).** `ref` 는 빌림만 뜻하고, null 이 될 수 있는 것은 `?` 가 붙은 타입뿐이며,
> 자동 null 검사(문장 건너뛰기)는 없앴다(§8·§9·§10.4). **같은 날 `jbroc` 은 렉서·파서에서 멈췄다(D-263)** - 파서는 아직 옛 문법을 읽으므로
> §13 은 그 파서가 읽는 옛 예시로 남기고, 새 문법의 예시는 §14 에 둔다.
> 컴파일러가 이 문법을 어떻게 검사하고 C++ 로 바꾸는지는 [jbroc-rules.md](./jbroc-rules.md) 에 있다.
> 논의 과정·근거·버린 안은 [jbroscript-plan.md](./jbroscript-plan.md) §12·§20·§21 에 있고, 둘이 다르면 이 문서가 더 최근이다.
> 확정 계약이 아니라 계획이므로 `docs/ProjectRule.md` 가 아니라 여기에 둔다.

| 표시 | 뜻 |
|---|---|
| **[확정]** | 빡대리가 정했다 |
| **[제안]** | 제안했고 아직 답을 받지 않았다. 바뀔 수 있다 |
| **[열림]** | 결정이 필요하다. §12 에 모았다 |

---

## 1. 취지 [확정]

> 성능을 챙기기 위해 **사용자가 필요한 것만** 제공한다. **C++ 특유의 많은 지식을 강요하지 않는다.**
> 다만 편하게 만들려고 **확장성을 제한하지는 않는다.**

- 게임 로직을 쓰는 스크립트 언어다. 실행은 C++ 와 같은 속도다(C++ 로 바뀐 뒤 컴파일된다).
- C++ 스크립트를 대체하지 않는다. C++ 로 스크립트를 쓰는 길도 남는다.
- 기존 엔진의 `JPROP` 스크립트는 옮기지 않는다.

## 2. 파일과 전체 구조 [확정]

| 항목 | 내용 |
|---|---|
| 확장자 | `.jscript` |
| 파일 | 선언과 정의를 한 파일에 둔다. 헤더와 소스로 나누지 않는다 |
| 블록 | 중괄호 |
| 문장 끝 | 줄바꿈. 세미콜론이 없다. **`( )` 와 `[ ]` 안의 줄바꿈은 문장을 끝내지 않는다**(2026-09-17 확정, D-104) - 긴 호출을 여러 줄로 나눠 쓸 수 있다 |
| 주석 | `//` |
| 다른 파일의 타입 | `import` 없이 프로젝트 전체의 타입이 보인다. C++ 로 바뀔 때는 컴파일러가 **실제로 쓰는 타입만** `#include` 한다 |
| `script` 의 부모 | 적지 않아도 엔진의 스크립트 베이스(2D 프로젝트면 `GameScript2D`)를 상속한다 |

### 2.1 이름과 예약어 [확정: 방향] · 목록 [제안]

**[확정]** 이름으로 쓸 수 없는 것은 **JBroScript 자체의 키워드뿐**이다. `new`·`delete`·`template` 같은 **C++ 키워드도 이름으로 쓸 수 있다.**
사용자는 C++ 를 쓰는 것이 아니기 때문이다. C++ 로 바꿀 때 문제가 되는 이름은 컴파일러가 알아서 처리한다
([jbroc-rules.md](./jbroc-rules.md) §5.5).

```
struct Item
{
    String template                  // 된다
    Int delete = 0                   // 된다
}
```

대가는 하나다. 이런 극히 일부 이름은 **디버거에서만** 다른 이름(예: `jbro_gen_template`)으로 보인다.
인스펙터·저장 파일·에러 메시지에는 원래 이름(`template`)이 나온다.

**[제안]** 예약어는 아래가 전부다.

| 분류 | 키워드 |
|---|---|
| 선언 | `script` `class` `struct` `interface` `enum` `fn` |
| 멤버 | `public` `protected` `private` `static` `const` `ref` |
| 문장 | `if` `else` `for` `while` `switch` `case` `default` `break` `continue` `return` |
| 식 | `and` `or` `not` `is` `null` `true` `false` |

- `callback`·`override`·`require` 는 **함수 선언 끝에서만**, `in` 은 **`for` 괄호 안에서만** 뜻을 갖는다. 그 밖에서는 이름으로 쓸 수 있다.
- `case`·`default` 는 `switch` 제안(§11.2)에 따라 들어갔다.

## 3. 선언 종류 넷 [확정]

| 접두사 | 컴포넌트 목록에 뜨나 | 기본 접근자 | 값 타입으로 쓸 수 있나 | 필드가 기본으로 프로퍼티인가 | 인자 받는 생성자 |
|---|---|---|---|---|---|
| `script` | 뜬다 | private | 없다 | 아니다(§4.3) | 없다 |
| `class` | 안 뜬다 | private | 있다 | 아니다(§4.3) | **있다** |
| `struct` | 안 뜬다 | public | 있다 | **그렇다**(private 필드도) | 없다 |
| `interface` | 안 뜬다 | public | — | — | 없다 |

```
script Player
{
    Int score = 0                    // private
}

class Inventory
{
    public Int Gold = 0              // public
    Int capacity = 20                // private
}

struct DropEntry
{
    Int Weight = 1                   // public + 프로퍼티
    private Int rollCount = 0        // private + 프로퍼티
}

interface IDamageable
{
    fn TakeDamage(Int amount)        // public, 순수 가상
}
```

## 4. 멤버

### 4.1 접근자 [확정]

멤버마다 앞에 `public` / `protected` / `private` 를 붙인다. 생략하면 선언 종류의 기본값(§3)이다.

### 4.2 필드 [확정]

- **타입을 반드시 쓴다.** 타입은 이름 앞이다. `var` 는 없다.
- 초깃값은 `=` 뒤에 쓴다. 쓰지 않으면 타입의 기본값이다.

```
public Int MaxHp = 10
Float speed = 2.5
Array<Int> lines
```

타입을 초깃값에서 추론하지 않는 이유: `Speed = 1` 이 `Int` 가 되어 인스펙터에서 `1.5` 를 넣을 수 없게 된다.

### 4.3 프로퍼티(인스펙터 노출·저장) [확정]

`script`·`class` 는 **대괄호 어트리뷰트가 붙은 필드만** 노출된다. 접근자 `public` 은 노출과 무관하다.
`struct` 는 모든 필드가 기본으로 프로퍼티다.

| 어트리뷰트 | 인스펙터 | 저장 |
|---|---|---|
| `[prop]` | 나온다 | 된다 |
| `[range(...)]`, `[name(...)]`, `[category(...)]` 등 | 나온다 | 된다 |
| `[noserialize]` | 나온다 | 안 된다 |
| 없음 | 안 나온다 | 안 된다 |

`[hidden]` 은 예약해 두었다. 어트리뷰트는 선언 윗줄에 쓰고, 같은 줄에 써도 된다.

```
[range(4, 40), category("Field")]
Int FieldRows = 20

[name("낙하 간격")]
Float DropInterval = 0.5

[prop] Bool ShowDebug = false
```

### 4.4 `static` · `const` [확정]

- `static` 은 **필드와 함수**에만 붙는다. 선언 밖의 전역 함수·전역 변수는 없다.
- `const` 는 **값·필드·매개변수**에만 붙는다. C++ 의 const 멤버 함수 같은 것은 없다.

```
static Int aliveCount = 0
const Float Gravity = 9.8

static fn Clamp01(Float value) -> Float
{
    ...
}
```

## 5. 함수

### 5.1 선언 모양 [확정]

```
fn 이름(매개변수) -> 반환타입 접미사
```

반환이 없으면 `->` 를 생략한다.

```
fn OnStart() callback                // 반환 없음
fn HpRatio() -> Float                // Float 반환
fn MakeHit(Int damage) -> HitInfo    // struct 반환
fn FindNearest() -> Enemy?           // 핸들 반환(D-262)
```

### 5.2 접미사 [확정]

| 접미사 | 뜻 |
|---|---|
| `callback` | **엔진이 부르는 훅**이다(`OnStart`, `OnUpdate` 등) |
| `override` | **사용자 타입끼리** 부모의 가상 함수를 재정의한다. `interface` 함수를 구현할 때도 쓴다 |
| `require` | 순수 가상 함수다. **`interface` 안에서는 생략할 수 있다** |

```
class Weapon
{
    fn Damage() -> Int require
}

class Sword : Weapon
{
    fn Damage() -> Int override
    {
        return 10
    }
}
```

### 5.3 생성자 [확정: `class` 만] · 모양 [제안]

인자를 받는 생성자는 **`class` 에만** 있다. `script` 는 `OnCreate`·`OnStart` 콜백을, `struct` 는 필드 초깃값을 쓴다.

**[제안]** 모양은 클래스 이름과 같은 이름의 `fn` 이고 반환 타입이 없다.

```
class Inventory
{
    public Int Gold = 0

    fn Inventory(Int startGold)
    {
        Gold = startGold
    }
}

Inventory bag = Inventory(100)
```

## 6. 상속 [확정]

- **다중 상속**을 지원한다.
- **다이아몬드 상속은 금지**한다(컴파일 에러). 단 **`interface` 의 다이아몬드는 허용**한다.
- 모든 `script` 가 엔진 베이스를 조상으로 가지므로, **`script` 는 다른 `script` 를 하나까지만** 상속할 수 있다.
- `interface` 에서 구체 타입으로 내려가는 변환은 v1 에 없다.

```
interface IDamageable
{
    fn TakeDamage(Int amount)
}

interface IHealable
{
    fn Heal(Int amount)
}

interface ICombatant : IDamageable, IHealable
{
}

script Knight : ICombatant
{
    fn TakeDamage(Int amount) override
    {
        ...
    }

    fn Heal(Int amount) override
    {
        ...
    }
}

// 에러: Mage 와 Healer 가 둘 다 script 라 엔진 베이스가 두 경로로 닿는다
script Priest : Mage, Healer
{
}
```

## 7. 타입

### 7.1 엔진이 제공한 타입만 쓴다 [확정]

스크립트의 **모든 타입은 엔진이 제공한 타입**이다. `Int`·`Float`·`String` 도 C++ 기본 타입이 아니라 엔진의 클래스다.
`int`·`float`·`bool`·`double`·`int32` 같은 이름은 **없다.**

| 분류 | v1 에 있는 것 |
|---|---|
| 수 | **`Int`(64비트)**, **`Float`** |
| 논리 | `Bool` |
| 글자 | `String` |
| 엔진 값 타입 | **`Vector2`**, `Rect`, `Color` |
| 엔진 enum | 엔진이 이름을 알려 주는 것 |
| 사용자 타입 | `script`, `class`, `struct`, `interface`, `enum` |
| 참조 | 핸들 `T?`, 빌림 `ref T`(§8, D-262) |
| 컨테이너 | `Array<T>`, `Table<K, V>`(K 는 `Int`·`String`) |

엔진 코드와 맞춰야 하는 것:

- **엔진의 벡터 타입 이름을 `Vector2`·`Vector3`·`Vector4` 로 바꾼다(2026-09-15 확정, D-250 에서 3·4 차원까지 넓힘).**
  아직 바꾸지 않았다. 새 엔진 코드는 지금 `Vector2`·`Vector3`(`JBro/Types/Math2D.h`·`Math3D.h`, D-241)이고
  D-57·D-241 도 `Vector2` 로 적혀 있어서, 바꿀 때 함께 고친다. `Vector4` 는 지금 없어서 새로 만든다.
  줄이지 않는 쪽으로 맞추는 까닭은 `Matrix3x2`·`Matrix4x4` 가 이미 안 줄인 이름이고 저장 파일에도 그렇게 적히기 때문이다(D-250).
- 엔진에는 `Int32`·`UInt`·`UInt32` 도 있지만 스크립트에는 `Int`·`Float` 만 둔다. `Float` 은 엔진에서 32비트 `float` 을 감싼다(`Float.h:12`).

### 7.2 enum 과 컨테이너 [확정]

- **스크립트 enum** 은 멤버를 한 줄에 하나씩 쓴다. 저장 파일에는 숫자가 아니라 이름으로 남는다.
- 컨테이너에 핸들을 담는 표기는 `Array<Enemy?>` 다(D-262). 옛 판의 `Array<ref Enemy>` 는 없어졌다 - 컨테이너에는 빌림을 담을 수 없다.

```
enum EnemyState
{
    Idle
    Chasing
    Dead
}

EnemyState state = EnemyState.Idle
Array<Enemy?> allies
Table<String, Int> scores
```

## 8. 참조와 null (D-262)

> **2026-09-29 에 갈아엎었다.** 옛 §8 은 `ref` 하나가 ① 엔진 객체를 오래 가리키기 ② 값을 빌려 제자리에서 고치기 ③ 출력 인자를
> 모두 맡았다. ①은 대상이 사라지면 스스로 무효가 되지만 ②는 댕글링이 된다 - 옛 §8.2 의 표와 §12 의 1번이 이 섞임에서 나왔다.
> 엔진도 이미 둘을 다르게 다룬다(①은 `GameObjectHandle`·`Ref<T>`, ②는 C++ 참조). 옛 판은 git 이력에 있다.

### 8.1 엔진 객체는 언제나 핸들이고, 핸들은 언제나 `?` 다 [확정]

- `script`·엔진 컴포넌트·게임 오브젝트는 **값으로 담을 수 없다.** 이 타입의 변수는 언제나 **핸들**이고, 대상이 사라지면 null 이 된다.
  C++ 로는 `Ref<T>` 와 `GameObjectHandle` 이다(ProjectRule §6.1).
- 핸들은 대상이 언제든 사라질 수 있으므로 **타입에 `?` 를 붙여야 한다.** `Transform2D target` 은 에러이고 `Transform2D? target` 으로 쓴다.
- 핸들은 멤버·지역·매개변수·반환·컨테이너 원소 어디에나 둘 수 있다.

```
Transform2D? target                  // 멤버
Array<Enemy?> allies                 // 컨테이너 원소
fn FindNearest() -> Enemy?           // 반환
```

### 8.2 `ref` 는 빌림이다 [확정]

- `ref` 는 **값을 제자리에서 읽고 고치려고 빌리는 것**이다. **매개변수와 `for` 변수에만** 쓴다.
- 멤버·지역 변수·컨테이너 원소·반환에는 쓸 수 없다(컴파일 에러). 그래서 빌린 것이 빌려준 것보다 오래 살 수 없다.
- 빌린 매개변수에 넘길 때는 **호출하는 쪽에도 `ref`** 를 쓴다(옛 판과 같다).
- **출력 인자는 없다.** 엔진의 `bool Raycast(..., RaycastHit2D& hit)` 같은 함수는 `RaycastHit2D?` 를 돌려주는 모양으로 투영한다(§8.4).

```
fn Heal(ref Stats stats, Int amount) // 빌린 매개변수
{
    stats.Hp += amount
}

Heal(ref playerStats, 10)            // 호출하는 쪽에도 ref

for (ref entry in drops)             // 빌린 for 변수
{
    entry.Weight += 1
}
```

### 8.3 `class` 인스턴스는 값으로 소유한다 [확정]

- `class` 는 **값으로만** 담는다. 다른 곳에 있는 `class` 인스턴스를 오래 가리키는 수단은 없다.
- 여럿이 함께 써야 하면 그 객체를 `script` 나 컴포넌트로 올려 핸들로 가리키거나, 번호로 가리킨다.
  옛 §8.2 표의 "소유자가 따로 있는 `class`"·"다른 객체 안에 값으로 들어 있는 `class`" 가 이렇게 사라진다.

### 8.4 값에도 `?` 를 붙일 수 있다 [확정: 방향] · 엔진 타입 [열림]

- `Int?`·`RaycastHit2D?` 처럼 값 타입에 `?` 를 붙이면 "없을 수도 있는 값" 이다. 실패를 알리는 길이 null 하나로 모인다(§10.4).
- **[열림]** C++ 로 내릴 값 타입이 엔진에 없다(`JBroCore` 의 `Types/` 에 `Optional` 이 없다). JBroCore 에 새 공개 타입을 두는 일이라 확인이 필요하다.

## 9. null 검사 (D-262)

### 9.1 검사하지 않고 쓰면 컴파일 에러다 [확정]

- `?` 타입의 멤버에 닿거나 함수를 부르려면 **먼저 null 이 아님을 보여야 한다.** 보이지 않고 `target.position` 을 쓰면 에러다.
- 문법은 옛 판 그대로 `is null` / `is not null` 이다. `if let` 은 없다.

### 9.2 흐름 좁히기 [확정] · 멤버를 좁힌 상태가 언제 풀리나 [열림]

- `if (x is null) { return }` 다음부터, 그리고 `if (x is not null) { ... }` 의 본문 안에서 `x` 는 null 이 아닌 것으로 본다.
- 한 콜백 안에서는 이 판단이 뒤집히지 않는다. 엔진이 파괴를 콜백 뒤로 미루기 때문이다(`RequestDestroy`).
- **[열림]** 멤버를 좁힌 상태가 "그 멤버에 대입할 때까지" 인지 "자기 함수를 부를 때까지" 인지는 타입체커를 만들 때 정한다.

### 9.3 `?.` 과 `??` [확정]

- `x?.F()` 는 `x` 가 null 이면 부르지 않는다. 값이 있는 식이면 결과가 `?` 타입이 된다.
- `a ?? b` 는 `a` 가 null 이면 `b` 다. 건너뛴 뒤의 값을 **쓰는 사람이 직접** 준다.
- 그래서 옛 §12 의 4번(걸러진 뒤 선언·`return`·`if`·`while` 은 무엇이 되나)이 사라진다 - 식마다 값이 정해져 있다.

### 9.4 `!` 단언 [확정: 동작] · 구현 [열림]

- `x!` 는 "여기서 `x` 는 null 이 아니다" 라는 단언이다. 검사 없이 쓰고 싶은 자리에 쓴다.
- **단언이 틀리면 에러 로그를 남기고 그 콜백의 나머지를 건너뛴다.** 게임은 계속 돈다.
  엔진의 "무효 접근은 로그를 남기고 아무 일도 하지 않는다"(ProjectRule §6.1)를 콜백 단위로 옮긴 것이다.
- **[열림]** 중단을 호출 사슬 위로 올리는 방식(함수마다 상태를 돌려주는가, 콜백 경계에서 잡는가)은 이미터를 만들 때 정한다.

### 9.5 옛 자동 null 검사는 없앴다 [확정]

옛 §9.2 의 `autochecknullable` 은 `ref` 를 쓰는 문장을 조용히 건너뛰었다. 없앤 까닭은 셋이다.

1. **건너뛴 뒤의 상태가 틀린다.** `Float hp = enemy.GetHp()` 가 걸러지면 `hp` 는 0 으로 남고, 다음 줄의 `if (hp <= 0) { Die() }` 가
   대상이 없다는 이유로 죽인다. ProjectRule §6.1 의 "대상이 없을 때 기본값을 조용히 돌려주면 찾기 어려운 논리 버그가 된다" 와 부딪힌다.
2. **값이 필요한 자리에서 뜻을 정할 수 없었다.** `return`·`if`·`while` 이 옛 §12 의 4번으로 열린 채였다.
3. **어느 줄이 실행됐는지 코드에 보이지 않는다.**

## 10. 식

### 10.1 연산자 [확정]

| 우선순위(높은 것부터) | 연산자 |
|---|---|
| 1 | `.` 멤버, `?.` null 이면 건너뛰는 멤버, `()` 호출, `[]` 인덱싱, 뒤붙이 `!` 단언(D-262) |
| 2 | 단항 `-`, `not` |
| 3 | `*` `/` `%` |
| 4 | `+` `-` |
| 4.5 | `??`(D-262). 비교보다 먼저 묶인다 - `a ?? b > c` 는 `(a ?? b) > c` 다 |
| 5 | `<` `<=` `>` `>=` |
| 6 | `==` `!=` `is null` `is not null` |
| 7 | `and` |
| 8 | `or` |

- v1 에 **없는 것**: 비트 연산, `++`/`--`, 삼항 `?:`, 쉼표 연산자, **식으로서의 대입**.
- 대입(`=`, `+=`, `-=`, `*=`, `/=`)은 **문장**이다. `if (a = b)` 같은 실수를 쓸 수 없다.

```
if (hp <= 0 and not isDead)
{
    isDead = true
}
```

### 10.2 리터럴 [확정]

`20` 은 `Int`, `0.5` 는 `Float`, `"..."` 는 `String`, `true`/`false` 는 `Bool`, 그리고 `null`.

### 10.3 형변환 [확정]

> **값이 손실될 수 있는 숫자 변환은 명시적으로 한다.**

사용자가 알아야 할 규칙은 이것 하나다. C++ 의 변환 규칙은 몰라도 된다.

| 변환 | 저절로 되나 | 이유 |
|---|---|---|
| `Int` → `Float` | **안 된다.** `Float(x)` 로 쓴다 | `Int` 는 64비트, `Float` 은 32비트라 큰 값의 정밀도가 조용히 사라진다 |
| `Float` → `Int` | **안 된다.** `Int(x)` 로 쓴다 | 소수점 아래가 사라진다 |
| 엔진 API 경계의 **손실 없는 넓힘**(예: 엔진이 돌려주는 `Int32` → `Int`) | 된다 | 값이 바뀌지 않는다. 사용자는 `Int` 만 본다 |

- `Int / Int` 는 `Int` 다.
- **[제안]** `Float` 자리에 온 **정수 리터럴**은 정확히 표현되면 그대로 받는다. `Raycast(from, down, 10)` 의 `10` 처럼.
- **[제안]** 엔진 컨테이너의 크기(`Array.Size()`)는 C++ 에서 부호 없는 64비트(`std::size_t`)라 `Int` 로 바꾸는 것이 엄밀히는
  손실 가능한 변환이다. 원소가 2^63 개가 될 수 없으므로 **API 투영이 `Int` 를 돌려주는 것으로 정한다**(사용자 쪽 변환이 아니다).

```
Float ratio = Float(hp) / Float(maxHp)
Int cells = Int(width / cellSize)
Float bad = hp                       // 에러: Int → Float 은 명시해야 한다
```

### 10.4 오류 처리 [확정] (D-262)

예외도 `Result` 도 없다. **실패는 `?` 로 드러난다** - 대상이 없으면 null 핸들, 값이 없으면 `?` 값이다(§8·§9).
검사를 건너뛰고 싶으면 `!` 로 단언하고, 틀리면 그 콜백만 멈춘다(§9.4). 게임 오브젝트 안전 멤버의 로그는 그대로다.

## 11. 제어문

### 11.1 정한 것 [확정]

- **`if`, `else if`, `for`, `while`, `switch` 는 조건을 괄호로 감싼다.** `else` 는 괄호 없이 그대로 쓴다.
- **`do`–`while`, `goto`, 이름 붙인 `break` 는 넣지 않는다.**

### 11.2 나머지 [제안]

- **본문은 언제나 중괄호**다. 한 줄 본문은 없다.
- `switch` 는 **떨어짐(fallthrough)이 없다.** `case` 마다 자기 블록이 있고 `break` 가 필요 없다.
- enum 에 대한 `switch` 는 **모든 값을 다루거나 `default` 가 있어야** 한다.
- 컨테이너를 순회하는 도중에 원소를 더하거나 빼면 에러다.

```
if (hp <= 0)
{
    Die()
}
else if (hp < 10)
{
    Flee()
}
else
{
    Fight()
}

while (timer > 0.0)
{
    timer -= dt
}

for (i in 0..count)                  // 0 이상 count 미만
{
    total += i
}

for (enemy in enemies)               // 원소를 복사해서 받는다
{
    if (enemy is null)
    {
        continue
    }
    enemy.Alert()
}

for (ref entry in drops)             // 원소를 제자리에서 고친다
{
    entry.Weight += 1
}

for (key, value in scores)           // Table 순회
{
    total += value
}

switch (state)
{
    case EnemyState.Idle
    {
        Patrol()
    }
    case EnemyState.Chasing, EnemyState.Dead
    {
        return
    }
}
```

## 12. 열린 것

1. ~~스스로 null 이 될 수 없는 대상을 멤버 `ref` 로 들고 있는 경우~~ **사라졌다(D-262).** 멤버에는 핸들만 둘 수 있고 `ref` 는 빌림이다(§8.2·§8.3).
2. ~~`Vector2` 와 엔진의 `Vector2`~~ **엔진 타입 이름을 `Vector2` 로 바꾼다(2026-09-15 확정).** 이름 변경 작업은 남았다(§7.1).
3. ~~엔진의 `Int32`·`UInt` 반환을 어떻게 받는가~~ **손실 없는 넓힘은 API 경계에서 저절로, 손실 가능한 변환은 명시(2026-09-15, §10.3).**
   컨테이너 크기를 API 투영이 `Int` 로 돌려주는 것만 [제안]으로 남았다.
4. ~~자동 검사로 걸러진 뒤의 동작~~ **사라졌다(D-262).** 자동 검사가 없고 `?.`·`??` 가 값을 정한다(§9.3).
5. ~~`Int` → `Float` 암묵 변환~~ **명시 변환만 허용한다(2026-09-15, §10.3).**
6. **값 `?` 를 내릴 엔진 타입**(§8.4). JBroCore 에 새 공개 타입이 필요하다.
7. **멤버를 좁힌 상태가 풀리는 때**(§9.2).
8. **`!` 가 콜백을 멈추는 구현**(§9.4).
9. **서비스 이름.** 스크립트는 `GetFramework2DServices().Physics2D` 같은 C++ 배관 대신 `Physics`·`Time`·`Input` 같은 이름으로 쓴다(§14).
   [확정: 방향] 이름과 게터의 대응표는 jbroc-rules §7 의 엔진 함수 선언 표와 함께 정한다.

## 13. 전체 예시 - 옛 문법 (지금 파서가 읽는 판)

**D-262 이전의 문법이다.** `jbroc` 파서가 이 예시를 읽는 시험이 있어서(`ScriptCompilerParserTests.cpp` 의 `TestTheSyntaxDocumentExampleParses`)
파서가 새 문법을 읽을 때까지 그대로 둔다. 새 문법의 예시는 §14 다. 편집기 문법 스냅숏(`source/JBroScriptEditor/.../enemy.jscript`)도 이 판이다.
[제안]·[열림] 항목이 들어간 줄에는 주석으로 표시했다.

```
// Enemy.jscript

enum EnemyState
{
    Idle
    Chasing
    Dead
}

interface IDamageable
{
    fn TakeDamage(Int amount)
    fn IsDead() -> Bool
}

struct DropEntry
{
    Int Weight = 1
    Int ItemId = 0
}

class LootTable
{
    Array<DropEntry> entries

    fn LootTable(Int capacity)                       // [제안] 생성자 모양
    {
        ...
    }

    public fn TotalWeight() -> Int
    {
        Int total = 0
        for (ref entry in entries)
        {
            total += entry.Weight
        }
        return total
    }
}

script Enemy : IDamageable
{
    [range(1, 100), category("Stats")]
    Int MaxHp = 10

    [prop]
    Float MoveSpeed = 2.0

    [category("Links")]
    ref Transform2D target

    [autochecknullable(false)]
    ref Transform2D home

    Int hp = 0
    EnemyState state = EnemyState.Idle
    Array<ref Enemy> allies
    LootTable loot = LootTable(8)                    // class 를 값으로 소유한다
    Vector2 down

    static Int aliveCount = 0
    const Float ChaseRange = 6.0

    fn OnStart() callback
    {
        hp = MaxHp
        down.y = -1.0
        aliveCount += 1
    }

    fn OnUpdate(Float dt) callback
    {
        switch (state)
        {
            case EnemyState.Idle
            {
                if (target is not null)              // [제안] is not null
                {
                    state = EnemyState.Chasing
                }
            }
            case EnemyState.Chasing
            {
                target.position.x += MoveSpeed * dt  // 자동 null 검사: target 이 null 이면 걸러지고 에러 로그
            }
            case EnemyState.Dead
            {
                return
            }
        }

        if (home is null)
        {
            return
        }
        home.position.y = 0.0                        // 자동 검사를 껐고, 위에서 직접 검사했다
    }

    fn TakeDamage(Int amount) override
    {
        hp -= amount
        if (hp <= 0 and state != EnemyState.Dead)
        {
            state = EnemyState.Dead
            aliveCount -= 1
        }
    }

    fn IsDead() -> Bool override
    {
        return state == EnemyState.Dead
    }

    fn HpRatio() -> Float
    {
        return Float(hp) / Float(MaxHp)
    }

    fn IsGrounded() -> Bool
    {
        Collision2D hit
        return GetFramework2DServices().Physics2D.Raycast(target.position, down, 1.0, ref hit)
        // [열림] §12 의 4번: target 이 null 이면 이 return 은 무엇을 돌려주는가
    }

    fn CountLivingAllies() -> Int
    {
        Int count = 0
        for (ally in allies)
        {
            if (ally is null)
            {
                continue
            }
            if (not ally.IsDead())
            {
                count += 1
            }
        }
        return count
    }

    static fn SumWeights(ref const Array<DropEntry> table) -> Int
    {
        Int total = 0
        for (i in 0..table.Size())
        {
            total += table[i].Weight
        }
        return total
    }
}
```

## 14. 전체 예시 - 새 문법 (D-262)

§13 과 같은 적을 새 문법으로 쓴다. 훅은 델타를 인자로 받지 않는다(D-242). 서비스 이름(`Time`·`Physics`)은 §12 의 9번이다.

```
// Enemy.jscript

script Enemy : IDamageable
{
    [range(1, 100), category("Stats")]
    Int MaxHp = 10

    [prop]
    Float MoveSpeed = 2.0

    [category("Links")]
    Transform2D? target                              // 핸들은 언제나 ?

    Transform2D? home
    Int hp = 0
    EnemyState state = EnemyState.Idle
    Array<Enemy?> allies
    LootTable loot = LootTable(8)                    // class 는 값으로 소유한다
    Vector2 down

    fn OnStart() callback
    {
        hp = MaxHp
        down.y = -1.0
    }

    fn OnUpdate() callback
    {
        switch (state)
        {
            case EnemyState.Idle
            {
                if (target is not null)
                {
                    state = EnemyState.Chasing
                }
            }
            case EnemyState.Chasing
            {
                if (target is null)
                {
                    state = EnemyState.Idle
                    return
                }
                target.position.x += MoveSpeed * Time.Delta   // 위에서 좁혔다
            }
            case EnemyState.Dead
            {
                return
            }
        }

        home!.position.y = 0.0                       // 단언: 틀리면 로그를 남기고 이 콜백을 멈춘다
    }

    fn IsGrounded() -> Bool
    {
        if (target is null)
        {
            return false
        }
        RaycastHit2D? hit = Physics.Raycast(target.position, down, 1.0)
        return hit is not null
    }

    fn TargetHeight() -> Float
    {
        return target?.position.y ?? 0.0             // 없으면 0 - 쓰는 사람이 정한다
    }

    fn CountLivingAllies() -> Int
    {
        Int count = 0
        for (ally in allies)
        {
            if (ally is not null and not ally.IsDead())
            {
                count += 1
            }
        }
        return count
    }

    static fn SumWeights(ref const Array<DropEntry> table) -> Int   // 빌린 매개변수
    {
        Int total = 0
        for (entry in table)
        {
            total += entry.Weight
        }
        return total
    }
}
```
