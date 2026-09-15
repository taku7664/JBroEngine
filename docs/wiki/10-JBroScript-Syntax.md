# 10. JBroScript 문법

> **전부 [계획] 이다.** 언어도 컴파일러 `jbroc` 도 아직 구현하지 않았다. 원문은 `tasks/jbroscript-syntax.md` 이고 2026-09-15 까지의 논의를 담고 있다.
> 항목마다 상태를 붙였다. **[확정]** 은 정한 것, **[제안]** 은 제안했고 답을 기다리는 것, **[열림]** 은 결정이 필요한 것이다.

## 왜 만드나

> 성능을 챙기기 위해 사용자가 필요한 것만 제공한다. C++ 특유의 많은 지식을 강요하지 않는다. 다만 편하게 만들려고 확장성을 제한하지는 않는다. [확정]

- 게임 로직을 쓰는 언어다. C++ 로 바뀐 뒤 컴파일되므로 **실행 속도는 C++ 와 같다.**
- C++ 스크립트를 **대체하지 않는다.** 추가 프론트엔드다. 언어가 막혀도 엔진은 멀쩡하다(D-56).
- 기존 엔진의 `JPROP` 스크립트는 옮기지 않는다. 변환기도 만들지 않는다.

## 한눈에 보기

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

script Enemy : IDamageable
{
    [range(1, 100), category("Stats")]
    Int MaxHp = 10

    [prop]
    Float MoveSpeed = 2.0

    [category("Links")]
    ref Transform2D target

    Int hp = 0
    EnemyState state = EnemyState.Idle
    Array<ref Enemy> allies
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
                if (target is not null)
                {
                    state = EnemyState.Chasing
                }
            }
            case EnemyState.Chasing
            {
                target.position.x += MoveSpeed * dt      // target 이 null 이면 이 문장은 걸러지고 에러 로그가 남는다
            }
            case EnemyState.Dead
            {
                return
            }
        }
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
}
```

## 파일과 전체 구조 [확정]

| 항목 | 내용 |
|---|---|
| 확장자 | `.jscript` |
| 파일 | 선언과 정의를 한 파일에 둔다. 헤더와 소스로 나누지 않는다 |
| 블록 | 중괄호 |
| 문장 끝 | 줄바꿈. 세미콜론이 없다 |
| 주석 | `//` |
| 다른 파일의 타입 | `import` 없이 프로젝트 전체의 타입이 보인다. C++ 로 바뀔 때는 컴파일러가 실제로 쓰는 타입만 `#include` 한다 |
| `script` 의 부모 | 적지 않으면 엔진의 스크립트 베이스(2D 프로젝트면 `GameScript2D`)를 상속한다 |

### 이름과 예약어

이름으로 쓸 수 없는 것은 **JBroScript 자체의 키워드뿐**이다 [확정]. `new`·`delete`·`template` 같은 C++ 키워드는 이름으로 쓸 수 있다. 사용자는 C++ 를 쓰는 것이 아니기 때문이다.
C++ 로 바꿀 때 문제가 되는 이름은 컴파일러가 알아서 바꿔 내린다([jbroc](11-Jbroc-Compiler.md) 의 이름 바꾸기). 대가는 그런 이름이 **디버거에서만** `jbro_gen_template` 처럼 보인다는 것이다. 인스펙터·저장 파일·에러 메시지에는 원래 이름이 나온다.

예약어 목록 [제안]:

| 분류 | 키워드 |
|---|---|
| 선언 | `script` `class` `struct` `interface` `enum` `fn` |
| 멤버 | `public` `protected` `private` `static` `const` `ref` |
| 문장 | `if` `else` `for` `while` `switch` `case` `default` `break` `continue` `return` |
| 식 | `and` `or` `not` `is` `null` `true` `false` |

`callback`·`override`·`require` 는 함수 선언 끝에서만, `in` 은 `for` 괄호 안에서만 뜻을 갖는다. 그 밖에서는 이름으로 쓸 수 있다.

## 선언 종류 넷 [확정]

| 접두사 | 컴포넌트 목록에 뜨나 | 기본 접근자 | 값 타입으로 쓸 수 있나 | 필드가 기본으로 프로퍼티인가 | 인자 받는 생성자 |
|---|---|---|---|---|---|
| `script` | 뜬다 | private | 없다 | 아니다 | 없다 |
| `class` | 안 뜬다 | private | 있다 | 아니다 | **있다** |
| `struct` | 안 뜬다 | public | 있다 | **그렇다**(private 필드도) | 없다 |
| `interface` | 안 뜬다 | public | 못 쓴다 | 해당 없음 | 없다 |

```
script Player        { Int score = 0 }                   // private, 프로퍼티 아님
class Inventory      { public Int Gold = 0   Int capacity = 20 }
struct DropEntry     { Int Weight = 1   private Int rollCount = 0 }   // 둘 다 프로퍼티
interface IDamageable { fn TakeDamage(Int amount) }       // 순수 가상
```

## 멤버

### 접근자 [확정]

멤버마다 앞에 `public` / `protected` / `private` 를 붙인다. 생략하면 선언 종류의 기본값이다.

### 필드 [확정]

- **타입을 반드시 쓴다.** 타입이 이름 앞이다. `var` 는 없다.
- 초깃값은 `=` 뒤에. 쓰지 않으면 타입의 기본값이다.

```
public Int MaxHp = 10
Float speed = 2.5
Array<Int> lines
```

타입을 초깃값에서 추론하지 않는 이유: `Speed = 1` 이 `Int` 가 되어 인스펙터에서 `1.5` 를 넣을 수 없게 된다.

### 프로퍼티(인스펙터 노출·저장) [확정]

`script`·`class` 는 **대괄호 어트리뷰트가 붙은 필드만** 노출된다. 접근자 `public` 은 노출과 무관하다. `struct` 는 모든 필드가 프로퍼티다.

| 어트리뷰트 | 인스펙터 | 저장 |
|---|---|---|
| `[prop]` | 나온다 | 된다 |
| `[range(...)]`, `[name(...)]`, `[category(...)]` 등 | 나온다 | 된다 |
| `[noserialize]` | 나온다 | 안 된다 |
| 없음 | 안 나온다 | 안 된다 |

어트리뷰트는 선언 윗줄에 쓰거나 같은 줄에 쓴다. `[hidden]` 은 예약해 두었다.

```
[range(4, 40), category("Field")]
Int FieldRows = 20

[name("낙하 간격")]
Float DropInterval = 0.5

[prop] Bool ShowDebug = false
```

### static · const [확정]

- `static` 은 **필드와 함수**에만 붙는다. 선언 밖의 전역 함수·전역 변수는 없다.
- `const` 는 **값·필드·매개변수**에만 붙는다. C++ 의 const 멤버 함수 같은 것은 없다.

```
static Int aliveCount = 0
const Float Gravity = 9.8
static fn Clamp01(Float value) -> Float { ... }
```

## 함수

### 모양 [확정]

```
fn 이름(매개변수) -> 반환타입 접미사
```

반환이 없으면 `->` 를 생략한다.

```
fn OnStart() callback                // 반환 없음, 엔진 훅
fn HpRatio() -> Float
fn MakeHit(Int damage) -> HitInfo
fn FindNearest() -> ref Enemy
```

### 접미사 [확정]

| 접미사 | 뜻 |
|---|---|
| `callback` | **엔진이 부르는 훅**이다(`OnStart`, `OnUpdate` 등). 이름·인자가 엔진 훅 목록에 없으면 컴파일 에러 |
| `override` | **사용자 타입끼리** 부모의 가상 함수를 재정의한다. `interface` 함수를 구현할 때도 쓴다 |
| `require` | 순수 가상 함수다. `interface` 안에서는 생략할 수 있다 |

```
class Weapon      { fn Damage() -> Int require }
class Sword : Weapon
{
    fn Damage() -> Int override { return 10 }
}
```

### 생성자 [확정: class 만] · 모양 [제안]

인자를 받는 생성자는 **`class` 에만** 있다. `script` 는 `OnCreate`·`OnStart` 콜백을, `struct` 는 필드 초깃값을 쓴다.
모양은 클래스 이름과 같은 이름의 `fn` 이고 반환 타입이 없다 [제안].

```
class Inventory
{
    public Int Gold = 0
    fn Inventory(Int startGold) { Gold = startGold }
}

Inventory bag = Inventory(100)
```

## 상속 [확정]

- **다중 상속**을 지원한다.
- **다이아몬드 상속은 금지**한다(컴파일 에러). 단 **`interface` 의 다이아몬드는 허용**한다.
- 모든 `script` 가 엔진 베이스를 조상으로 가지므로 **`script` 는 다른 `script` 를 하나까지만** 상속할 수 있다.
- `interface` 에서 구체 타입으로 내려가는 변환은 v1 에 없다.

```
interface ICombatant : IDamageable, IHealable { }
script Knight : ICombatant { fn TakeDamage(Int amount) override { ... }  fn Heal(Int amount) override { ... } }

script Priest : Mage, Healer { }     // 에러: Mage 와 Healer 가 둘 다 script 라 엔진 베이스가 두 경로로 닿는다
```

## 타입

### 엔진이 제공한 타입만 쓴다 [확정]

`Int`·`Float`·`String` 도 C++ 기본 타입이 아니라 엔진의 클래스다. `int`·`float`·`bool`·`double`·`int32` 같은 이름은 **없다.**

| 분류 | v1 에 있는 것 |
|---|---|
| 수 | `Int`(64비트), `Float`(32비트) |
| 논리 | `Bool` |
| 글자 | `String` |
| 엔진 값 타입 | `Vector2`, `Rect`, `Color` |
| 엔진 enum | 엔진이 이름을 알려 주는 것 |
| 사용자 타입 | `script`, `class`, `struct`, `interface`, `enum` |
| 참조 | `ref T` |
| 컨테이너 | `Array<T>`, `Table<K, V>`(K 는 `Int`·`String`) |

엔진의 2D 벡터 이름을 `Vec2` 에서 `Vector2` 로 바꾸기로 했다(2026-09-15 확정). 아직 바꾸지 않았다.
엔진에는 `Int32`·`UInt` 도 있지만 스크립트에는 `Int`·`Float` 만 둔다.

### enum 과 컨테이너 [확정]

- 스크립트 enum 은 멤버를 한 줄에 하나씩 쓴다. 저장 파일에는 숫자가 아니라 이름으로 남는다.
- 컨테이너에 `ref` 를 담는 표기는 `Array<ref Enemy>` 다.

```
enum EnemyState { Idle  Chasing  Dead }       // 실제로는 한 줄에 하나씩
EnemyState state = EnemyState.Idle
Array<ref Enemy> allies
Table<String, Int> scores
```

## ref

### 정한 것 [확정]

- 언어에 **포인터와 참조는 없다. `ref` 만** 쓴다.
- `ref` 는 단순히 포인터다. **매개변수·멤버·지역** 어디에나 쓸 수 있다.
- **null 이 될 수 있는 것은 `ref` 뿐이다.**
- `ref` 는 누군가가 소유하고 있는 것을 가리킨다. 여럿이 함께 쓰는 `class` 인스턴스도 누군가가 소유하고 나머지는 `ref` 로 가리킨다.
- **`ref` 매개변수에 넘길 때는 호출하는 쪽에도 `ref` 를 쓴다.**

```
ref Transform2D target                       // 멤버
ref const Array<DropEntry> table             // const 와 함께
fn Apply(ref HitInfo hit) { ref Enemy nearest = FindNearest() }   // 매개변수, 지역

Collision2D hit
Bool found = GetFramework2DServices().Physics2D.Raycast(from, down, 10.0, ref hit)   // 호출하는 쪽에도 ref
```

### 대상이 사라지면 [열림]

null 검사만으로는 댕글링을 막을 수 없다. 파괴된 대상을 가리키는 포인터는 null 이 아니기 때문이다. 자동 null 검사가 댕글링을 막으려면 **대상이 사라질 때 `ref` 가 스스로 null 이 되어야** 한다.

| `ref` 가 가리키는 것 | 대상이 사라지면 `ref` 가 null 이 되나 |
|---|---|
| `script`, 엔진 컴포넌트, 게임 오브젝트 | 된다 (`Ref<T>`, `GameObjectHandle`) |
| 소유자가 따로 있는 `class` 인스턴스 | 된다 (`SafePtr`) |
| 다른 객체 안에 **값으로** 들어 있는 `class` | **지금 엔진으로는 안 된다** |
| 값(`Int`, `Float`, `struct`), 컨테이너 원소 | **안 된다** |

매개변수·지역의 `ref` 는 한 콜백 안에서만 살고 엔진이 파괴를 콜백 뒤로 미루므로 대상이 먼저 사라지지 않는다.
단 **컨테이너 원소를 가리키는 `ref` 는 같은 함수 안에서도 끊길 수 있다**(원소를 더해 컨테이너가 커지면).
문제는 표의 아래 두 줄을 **멤버**로 들고 있는 경우다. 허용할지, 금지할지, 경고할지가 열려 있다(열린 것 1번).

## null 검사

### 문법 [확정]

```
if (target is null) { return }
if (target is not null) { ... }      // [제안]
```

`if let` 은 없다.

### 자동 null 검사 `autochecknullable` [확정]

- 컴파일러가 **`ref` 를 쓰는 문장**에 null 검사를 자동으로 넣는다.
- 켜져 있으면 그 문장은 `ref` 가 null 일 때 **걸러지고 에러 로그가 남는다.**
- 값이 필요한 자리도 같다. `Float hp = target.GetHp()` 에서 `target` 이 null 이면 대입이 걸러지고 `hp` 는 기본값(0)으로 남는다.
- 한 문장에 `ref` 가 둘 이상이면 모두를 한 조건으로 검사한다.
- 선언마다 어트리뷰트로 끄고, 기본은 켜짐이다 [제안]. 끄면 직접 `is null` 로 검사하거나 자신 있으면 그냥 쓴다.

```
ref Transform2D target
[autochecknullable(false)]
ref Transform2D home

fn OnUpdate(Float dt) callback
{
    target.position.x += 1.0                 // target 이 null 이면 걸러지고 에러 로그
    if (home is not null)                    // 자동 검사를 껐으므로 직접 검사한다
    {
        home.position.y = 0.0
    }
}
```

걸러진 뒤의 동작 중 정해야 할 것이 남았다(열린 것 4번). `return enemy.GetHp()` 는 무엇을 돌려주는가, `if (enemy.IsDead()) { A } else { B }` 는 둘 다 건너뛰는가, `while` 은 끝나는가.

## 식

### 연산자 [확정]

| 우선순위(높은 것부터) | 연산자 |
|---|---|
| 1 | `.` 멤버, `()` 호출, `[]` 인덱싱 |
| 2 | 단항 `-`, `not` |
| 3 | `*` `/` `%` |
| 4 | `+` `-` |
| 5 | `<` `<=` `>` `>=` |
| 6 | `==` `!=` `is null` `is not null` |
| 7 | `and` |
| 8 | `or` |

v1 에 **없는 것**: 비트 연산, `++`/`--`, 삼항 `?:`, 쉼표 연산자, **식으로서의 대입**. 대입(`=`, `+=`, `-=`, `*=`, `/=`)은 **문장**이라 `if (a = b)` 같은 실수를 쓸 수 없다.
우선순위를 C++ 와 같게 둔 것은 이미터가 괄호를 새로 넣거나 식을 쪼갤 일이 없게 하기 위해서다.

### 리터럴 [확정]

`20` 은 `Int`, `0.5` 는 `Float`, `"..."` 는 `String`, `true`/`false` 는 `Bool`, 그리고 `null`.

### 형변환 [확정]

> **값이 손실될 수 있는 숫자 변환은 명시적으로 한다.**

사용자가 알아야 할 규칙은 이것 하나다.

| 변환 | 저절로 되나 | 이유 |
|---|---|---|
| `Int` → `Float` | 안 된다. `Float(x)` | `Int` 는 64비트, `Float` 은 32비트라 큰 값의 정밀도가 조용히 사라진다 |
| `Float` → `Int` | 안 된다. `Int(x)` | 소수점 아래가 사라진다 |
| 엔진 API 경계의 손실 없는 넓힘(`Int32` → `Int`) | 된다 | 값이 바뀌지 않는다. 사용자는 `Int` 만 본다 |

- `Int / Int` 는 `Int` 다.
- `Float` 자리에 온 정수 리터럴은 정확히 표현되면 그대로 받는다(`Raycast(from, down, 10, ref hit)`) [제안].
- `Array.Size()` 는 C++ 에서 부호 없는 64비트지만 API 투영이 `Int` 를 돌려주는 것으로 정한다 [제안].

```
Float ratio = Float(hp) / Float(maxHp)
Int cells = Int(width / cellSize)
Float bad = hp                       // 에러: Int → Float 은 명시해야 한다
```

### 오류 처리 [제안]

예외도 `Result` 도 없다. 실패는 `ref` 의 null(자동 검사의 에러 로그), `Bool` 반환, 게임 오브젝트 안전 멤버의 로그로 드러난다.

## 제어문

### 정한 것 [확정]

- `if`, `else if`, `for`, `while`, `switch` 는 조건을 괄호로 감싼다. `else` 는 괄호 없이 그대로 쓴다.
- `do`–`while`, `goto`, 이름 붙인 `break` 는 넣지 않는다.

### 나머지 [제안]

- 본문은 언제나 중괄호다. 한 줄 본문은 없다.
- `switch` 는 **떨어짐(fallthrough)이 없다.** `case` 마다 자기 블록이 있고 `break` 가 필요 없다. 한 `case` 에 값을 여럿 적을 수 있다.
- enum 에 대한 `switch` 는 모든 값을 다루거나 `default` 가 있어야 한다.
- 컨테이너를 순회하는 도중에 원소를 더하거나 빼면 에러다.

```
for (i in 0..count)          { total += i }          // 0 이상 count 미만
for (enemy in enemies)       { enemy.Alert() }       // 원소를 복사해서 받는다
for (ref entry in drops)     { entry.Weight += 1 }   // 원소를 제자리에서 고친다
for (key, value in scores)   { total += value }      // Table 순회

switch (state)
{
    case EnemyState.Idle                    { Patrol() }
    case EnemyState.Chasing, EnemyState.Dead { return }
}
```

## 열린 것

1. **스스로 null 이 될 수 없는 대상을 멤버 `ref` 로 들고 있는 경우.** 허용하는가, 금지하는가, 경고하는가. 허용하면 댕글링을 자동 null 검사로 막을 수 없고 `ProjectRule.md` §6 의 raw pointer 금지 규칙과 부딪힌다.
2. ~~`Vector2` 이름~~ 엔진 타입을 `Vector2` 로 바꾼다(확정). 작업만 남았다.
3. ~~`Int32`·`UInt` 반환~~ 손실 없는 넓힘은 API 경계에서 저절로(확정). 컨테이너 크기만 [제안].
4. **자동 검사로 걸러진 뒤의 동작.** 선언의 기본값, `return` 이 돌려줄 값, `if`/`else` 를 둘 다 건너뛰는가, `while` 을 끝내는가.
5. ~~`Int` → `Float` 암묵 변환~~ 명시만 허용(확정).
