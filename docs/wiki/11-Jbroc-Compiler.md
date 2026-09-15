# 11. jbroc 컴파일러

> **전부 [계획] 이다.** 원문은 `tasks/jbroc-rules.md` 다. 상태 표시는 [문법 문서](10-JBroScript-Syntax.md)와 같고, **[대기]** 는 결정을 미룬 것이다.

## 무엇을 하는가 [확정]

`jbroc` 은 `.jscript` 를 **C++ 소스로 바꾸는 트랜스파일러**다. 그 뒤는 기존 C++ 스크립트와 같은 길이다.

```
Enemy.jscript  →  jbroc  →  Enemy.generated.h / Enemy.generated.cpp  →  MSVC  →  게임 DLL  →  호스트
                  ↑ 새로 만드는 것                                      ↑ 여기부터는 손대지 않는다
```

`ScriptDLLLoader`·`ScriptRegistry`·스크립트 풀·핫 리로드·Tier 분리·POD ABI 가 전부 그대로 쓰인다. 이들 입장에서는 C++ 스크립트가 하나 더 생긴 것이다.

### 왜 VM 이 아니라 트랜스파일인가

| | VM 을 만들면 | 트랜스파일하면 |
|---|---|---|
| GC | 직접 | 없음. 엔진 소유 모델 |
| 바이트코드·인터프리터 | 직접 | 없음 |
| 최적화기 | 직접 | MSVC |
| 디버거 엔진 | 직접 | VS 디버거. `#line` 이 원본을 가리킨다(실측) |
| 표준 라이브러리 | 직접 | `ScriptAPI.h` 가 이미 그것 |
| 성능 | 10~100배 느림 | C++ 와 같다 |

만들 것은 넷이다. **렉서 → 파서 → 타입체커 → C++ 이미터.**

대가는 **즉각성**이다. VM 은 저장하면 바로 돌지만 트랜스파일은 C++ 컴파일과 링크가 낀다. 핫 리로드 자체는 기존 DLL 재로드로 되고, 아래 빌드 규칙으로 완화한다.

선례: Haxe, Nim, Vala, 초기 C++(Cfront), Unity IL2CPP. "스크립트" 는 인터프리터로 도는 것을 뜻한 적이 없다. 엔진 코드가 아닌 게임 로직이 사는 층을 뜻한다.

## 구현 [확정]

| 항목 | 내용 |
|---|---|
| 언어 | **C++20**(MSVC), 엔진 리포 안의 실행 파일. 엔진만 쓰는 Tier E 도구다 |
| 단계 | 렉서 → 파서 → 타입체커 → C++ 이미터 |
| 파싱 | **두 번 훑는다.** 먼저 모든 파일의 타입 이름을 모으고, 그다음 몸통을 읽는다. `var` 가 없어서 `Float step = dt` 가 선언인지 알려면 `Float` 이 타입인 것을 먼저 알아야 하고, `import` 없이 다른 파일의 타입이 보여야 하기 때문이다 |
| 엔진 타입 정보 | 엔진의 리플렉션(`PropertyRegistry`)을 **링크해서** 빌트인 컴포넌트의 필드와 타입을 읽는다. 엔진 타입을 두 번째 파일에 다시 적지 않는다 |
| 편집기 | 같은 파서와 타입 정보를 `jbroc --lsp` 로 편집기에 준다. 편집기 쪽은 이 실행 파일을 띄우기만 한다 |

C++20 을 고른 이유는 엔진 리플렉션을 링크할 수 있고 툴체인이 늘지 않기 때문이다. TypeScript(엔진 타입 정보를 다시 적어야 하고 Node 가 딸린다), Rust·C#(툴체인·런타임이 는다)은 버렸다.

## 타입체커

### 목표 [확정]

> **`jbroc` 이 에러 없이 통과시킨 `.jscript` 는, 생성 C++ 가 `/W4` 에서 경고도 에러도 없이 컴파일된다.**

MSVC 가 무엇이든 말하면 그것은 `jbroc` 의 결함이다. 사용자는 C++ 에러 메시지를 보지 않아야 한다. `#line` 덕에 파일·줄은 맞지만 메시지는 C++ 말이기 때문이다.

### 잡는 것

| 검사 | 상태 | 예 |
|---|---|---|
| 이름 해석(없는 필드·함수·타입), 중복 선언 | 확정 | `target.Destory()` |
| 타입 불일치, 인자 개수·타입 | 확정 | `Int x = "a"` |
| 스크립트에 없는 타입 이름 | 확정 | `int`, `float`, `bool`, `Int32` |
| 손실 가능한 숫자 변환을 명시 없이 씀 | 확정 | `Int cells = width / size`, `Float x = someInt` |
| 반환 타입과 `return` 불일치, 반환값이 필요한데 끝에 닿는 함수 | 확정 | `fn F() -> Int { }` |
| 필드 타입 생략 | 확정 | `public MyInt = 1` |
| `ref` 매개변수에 넘기면서 호출하는 쪽에 `ref` 를 빠뜨림 | 확정 | `Raycast(from, down, 1.0, hit)` |
| 조건 괄호 누락 | 확정 | `if hp <= 0` |
| `if let`, `do`–`while`, `goto`, 이름 붙인 `break` | 확정 | 문법에 없다 |
| 다이아몬드 상속(`interface` 는 예외) | 확정 | |
| `script` 가 `script` 를 둘 이상 상속 | 확정 | |
| JBroScript 키워드를 이름으로 사용 | 확정 | 필드 이름 `fn`, `ref`. C++ 키워드는 거절하지 않는다 |
| `callback` 함수의 이름·인자가 엔진 훅 목록에 없음 | 확정 | `fn OnTick() callback` |
| `override` 인데 부모에 그런 가상 함수가 없음 | 확정 | |
| `require` 가 남은 채 인스턴스로 쓰는 타입 | 확정 | |
| 선언 종류 규칙 위반 | 확정 | `script` 를 값으로 선언, `struct` 에 생성자 |
| `static`·`const` 가 붙을 수 없는 자리 | 확정 | 전역 함수, const 멤버 함수 |
| 필드 어트리뷰트: 모르는 이름, 인자 개수·타입 | 확정 | `[rnage(0, 1)]` |
| 스크립트에서 읽기만 허용한 엔진 필드에 쓰기 | 확정 | `Transform2D` 의 월드 캐시 |
| 지역 변수를 초기화 없이 선언 | 제안 | 컨테이너와 값 타입의 기본값은 예외 |
| enum `switch` 에서 빠진 값(`default` 없음) | 제안 | |
| 컨테이너를 순회하는 도중 원소 추가·삭제 | 제안 | |

잡지 않는 것: 쓰지 않는 변수, 도달하지 않는 코드, 0 나누기, 무한 루프.

### 진단 메시지 [확정]

메시지는 문장이 아니라 **키**로 들고 있다가 언어를 골라 낸다. 편집기가 LSP 초기화 때 넘기는 `locale` 을 쓰고, 그 언어가 없으면 영어다. 명령줄도 같은 표를 쓴다.

## ref 를 무엇으로 바꾸는가 [열림]

자동 null 검사가 댕글링을 막으려면 대상이 사라질 때 `ref` 가 스스로 null 이 되는 C++ 타입이어야 한다. 원시 포인터는 그렇지 않다. 대상에 따라 고른다 [제안].

| `ref` 의 대상 | C++ | 스스로 null 이 되나 | 근거 |
|---|---|---|---|
| `script`, 빌트인 컴포넌트 | `Ref<T>`(24B) | 된다 | 영속 참조. 저장·핫 리로드를 넘는다 |
| 게임 오브젝트 | `GameObjectHandle`(16B) | 된다 | 같다 |
| `OwnerPtr` 로 소유된 `class` | `SafePtr<T>` | 된다 | 엔진의 비소유 참조 규칙 |
| 다른 객체 안에 값으로 들어 있는 `class` | 없다 | **안 된다** | `SafePtr` 제어 블록은 `OwnerPtr`·풀에서만 생긴다 |
| 값, 컨테이너 원소 | `T*` | **안 된다** | 원시 포인터밖에 없다 |
| `ref` 매개변수(값 대상) | `T&` 또는 `T*` | 해당 없음 | 한 콜백 안에서만 산다 |

멤버로 "안 된다" 줄을 들고 있으면 `ProjectRule.md` §6 의 MUST 와 부딪힌다. 허용하려면 그 규칙을 함께 고쳐야 한다.

## 이미터

### 디버깅을 위한 제약 셋 [확정]

실측으로 굳은 것이다(jbroscript-plan §18). 셋을 지키면 Visual Studio 에서 지역 변수·콜스택·스텝이 스크립트처럼 보인다. 안 지키면 지역 변수 창에 `jbro_tmp_0..8` 이 뜨고 중단점이 옆 줄에 선다.

1. **문장마다 `#line` 을 다시 찍는다.** 블록 앞에 한 번은 모자라다.
2. **이름을 1:1 로 유지한다.** `speed` 는 `speed`, `fn OnUpdate` 는 `OnUpdate`. 예외는 C++ 에서 그대로 쓸 수 없는 이름뿐이다.
3. **임시변수를 만들지 않는다.** 식을 그대로 옮긴다. 공통부분식 제거 같은 최적화는 MSVC 에 맡긴다.

### 문법 → C++

| 문법 | C++ | 상태 |
|---|---|---|
| `Int`, `Float`, `Bool`, `String` | 엔진 타입 그대로(`JBro::Int` 는 `Int64` 의 별칭, `JBro::Float` 은 `float` 을 감싼 클래스) | 확정 |
| `Vector2` | 엔진 타입 그대로. 이름 변경은 엔진 쪽 일 | 확정 |
| `0.5` | `0.5f` | 확정 |
| `and` / `or` / `not` | `&&` / `\|\|` / `!` | 확정 |
| `Int(x)`, `Float(x)` | 엔진 타입의 명시 생성(안쪽은 `static_cast`) | 확정 |
| `ref hit`(호출하는 쪽) | 매개변수의 C++ 모양에 맞춰 넘긴다 | 확정 |
| `if (...)`, `while (...)`, `switch (...)`, `else` | 같은 C++ 문 | 확정 |
| `fn Name(...) -> T` | `T Name(...)` | 확정 |
| `callback` | 엔진 훅의 `override` | 확정 |
| `override` / `require` | `override` / `= 0` | 확정 |
| 접근자 | 멤버마다 C++ 접근 구역으로 | 확정 |
| 스크립트 `enum` | `enum class` + 리플렉션 enum 이름 설명자 | 확정 |
| 프로퍼티 필드 | `PropertyInfo` 를 직접 생성해 스크립트 보관함(DLL 수명)에 등록 | 확정 |
| `is null` / `is not null` | `== nullptr` / `!= nullptr`(또는 핸들의 무효 검사) | 제안 |
| `for (i in 0..n)` | `for (Int i = 0; i < n; ++i)` | 제안 |
| `for (ref e in arr)` | 범위 기반 `for` 와 참조 | 제안 |
| `switch` | 떨어짐 없는 `switch`, `case` 마다 `break` | 제안 |

### 상속 [확정]

- **다이아몬드 검사는 `jbroc` 이 한다.** 모든 타입을 먼저 훑었으므로 상속 그래프 전체를 안다. C++ 컴파일러에 맡기면 안 된다. 비가상 다이아몬드는 선언만으로는 C++ 에러가 아니고, 모호한 멤버에 접근할 때 비로소 사용자가 쓰지 않은 코드의 말로 에러가 난다.
- **`interface` 상속은 C++ 가상 상속(`virtual public`)으로 내린다.** `interface` 다이아몬드를 허용하므로 비가상으로 내리면 모호성 에러가 난다. `interface` 는 상태가 없어 대가가 작다.
- 그래서 `interface` → 구체 타입 내림 변환은 v1 에 없다. 가상 기반에서는 `static_cast` 가 안 되고 `dynamic_cast` 는 매 프레임 경로에서 금지다.
- **엔진 베이스를 C++ 부모 목록의 맨 앞에 둔다.** 풀이 주소로 슬롯을 찾는 길이 부모 순서에 기대는지 확인하지 않았으므로 앞에 두면 그 질문이 없다.

### 자동 null 검사

`ref` 를 쓰는 문장을 `if` 로 감싸고 null 이면 걸러서 에러 로그를 남긴다 [확정].

```
// Enemy.jscript                             // 생성 C++ (모양만)
target.position.x += 1.0                     if (target != nullptr) { target->position.x += 1.0f; } else { /* 에러 로그 */ }
target.position.y = enemy.GetHeight()        if (target != nullptr && enemy != nullptr) { ... } else { /* 에러 로그 */ }
Float hp = enemy.GetHp()                     Float hp; if (enemy != nullptr) { hp = enemy->GetHp(); } else { /* 에러 로그 */ }
```

- **선언은 `if` 밖에 남긴다.** 안에 두면 변수의 범위가 `if` 블록으로 줄어 다음 줄에서 쓸 수 없다. 그래서 걸러진 경우 변수는 기본값으로 남는다.
- 에러 로그는 null 인 `ref` 마다 남기고, 호출 위치마다 한 번만 남긴다 [제안]. 매 프레임 경로에서 문자열을 만들면 안 되므로 고정 문자열과 위치 번호만 넘긴다.
- 대상은 두 번 해석한다(조건에서 한 번, 본문에서 한 번) [제안]. `Ref<T>` 해석은 캐시 비교라 싸다. `if (T* p = target.Get())` 로 한 번만 하면 지역 변수 `p` 가 디버거에 떠서 제약 3번과 부딪힌다.
- 자동 검사를 끈 `ref` 는 `target->Func()` 로 바로 내린다.
- `return`·`if` 조건·`while` 조건처럼 문장을 통째로 거르면 뜻이 달라지는 자리의 규칙은 [열림] 이다.

### C++ 에서 쓸 수 없는 이름은 바꿔 쓴다 [확정]

스크립트가 예약하는 것은 JBroScript 키워드뿐이다. `template`·`new`·`delete` 처럼 C++ 에서 쓸 수 없는 이름은 사용자에게 금지하지 않고 **이미터가 그 이름만 바꿔 내린다.**

```
// jscript                          // 생성 C++
String template                     String jbro_gen_template;
Int delete = 0                      Int jbro_gen_delete = 0;
```

원래 이름이 쓰이는 곳: 리플렉션(`PropertyInfo` 의 이름), 저장 파일, 인스펙터, 진단 메시지, LSP. 바뀐 이름이 보이는 곳: **디버거뿐**이다.

세부 규칙 [제안]:

- 바꾸는 대상은 C++ 키워드, C++ 대체 토큰(`xor`, `bitand` …), 구현용 예약 모양(`__` 포함, `_대문자` 시작), 스크립트가 보지 못하는 엔진 베이스 멤버와 겹치는 이름(`GetTypeId` 등) 전부다.
- 사용자 이름이 `jbro_gen_` 으로 시작하면 그것도 바꾼다(`jbro_gen_x` → `jbro_gen_jbro_gen_x`). 그러면 규칙이 일대일이 되어 접두사를 예약하지 않아도 겹칠 수 없다. 이미터가 스스로 만드는 이름도 전부 `jbro_gen_` 으로 시작한다.
- **스크립트 필드 등록에 `JBRO_REFLECT_BODY`·`JBRO_FIELD` 매크로를 쓰지 않는다.** `JBRO_FIELD` 는 이름을 멤버 이름에서 뽑아 `jbro_gen_template` 이 등록되고, 필드를 `public` 으로 만드는데 스크립트에는 `private` 프로퍼티가 있다. 그래서 `jbroc` 은 `PropertyInfo` 를 원래 이름으로 직접 생성한다.
- 스크립트 공개 헤더에 `windows.h` 의 `DELETE`·`near` 같은 매크로가 섞이면 그 이름도 바꿔야 한다. 지금 Tier S 공개 헤더에는 `windows.h` 가 없고, 테스트로 붙잡는다.

## 빌드 [확정]

- **`.jscript` 하나당 생성 `.cpp` 하나.** 한 파일에 몰면 한 줄 고칠 때마다 전체를 다시 컴파일한다.
- **내용이 바뀌지 않았으면 생성 파일을 덮어쓰지 않는다.** 그래야 MSVC 가 재컴파일을 건너뛴다.
- **실제로 쓰는 타입만 `#include` 한다.**
- **생성 헤더에는 전방 선언만 둔다.** 다른 스크립트의 헤더 `#include` 는 생성 `.cpp` 에 둔다. 값으로 담는 타입처럼 크기를 알아야 하는 경우만 헤더에 넣는다. 한 스크립트를 고쳤을 때 재컴파일이 번지지 않게 하기 위해서다.
- 편집기의 "빌드" 는 `jbroc` → MSBuild 를 부르는 일이다. MSVC 에러는 `#line` 덕분에 `.jscript` 줄을 가리킨다.

## 엔진 API 투영

| 무엇 | 어떻게 | 상태 |
|---|---|---|
| 이름 | `ScriptAPI.h` 가 C++ 스크립트에 주는 이름을 그대로 쓴다 | 확정 |
| 필드 | `PropertyRegistry` 를 링크해서 읽는다. 빌트인 컴포넌트에 필드가 늘면 저절로 안다 | 확정 |
| 서비스 | C++ 스크립트와 같은 길(`GetFramework2DServices().Physics2D`) | 확정 |
| 함수(`Destroy`, `GetComponent<T>`, `Raycast`, 엔진 훅) | 리플렉션에 함수가 없으므로 엔진 쪽에 **"스크립트가 부를 수 있는 함수" 선언 표**를 두고, 표의 모든 항목을 부르는 C++ 를 생성해 컴파일하는 테스트로 실제 코드와 맞춘다 | **대기** |

버린 안: C++ 헤더 파싱(기존 엔진이 정규식으로 실패한 길), 별도 문서만 두기(C++ 와 어긋나도 잡을 수 없다), 엔진 함수 호출은 MSVC 에 맡기기(타입체커 목표를 어긴다).

### 시그니처를 자동으로 뽑을 수 있는가 (2026-09-15 프로브)

**된다.** 실제 `<JBro/ScriptAPI.h>` 를 include 하고 `decltype(&C::F)` 를 템플릿으로 풀어 매개변수·반환 타입을 꺼낸 뒤 스크립트 이름으로 바꿨다. 경고 0개로 컴파일되고 돌았다.

| 엔진 C++ | 자동 변환 결과 |
|---|---|
| `bool Physics2DService::Raycast(Vec2, Vec2, float, Collision2D&) const` | `fn Raycast(Vector2, Vector2, Float, ref Collision2D) -> Bool` |
| `void Physics2DService::OverlapBox(const Rect&, Array<GameObjectHandle>&) const` | `fn OverlapBox(Rect, ref Array<ref GameObject>)` |
| `void GameObjectHandle::SetActive(bool)` | `fn SetActive(Bool)` |
| `SizeType Array::Size() const noexcept` | `fn Size() -> Int` |

변환 규칙: `bool`/`float` → `Bool`/`Float`, `int32`/`int64`/`size_t` → `Int`, `const T&` → `T`, `T&` → `ref T`, `Ref<T>`/`GameObjectHandle` → `ref T`/`ref GameObject`, `void` 반환 → `->` 없음.

자동으로 안 되는 것: **매개변수 이름**(함수 타입에 없다), **같은 이름의 오버로드**(`Float::Clamp` 가 C3556), **기본 인자**, `noexcept`·`const` 조합(한 번씩 적으면 끝), 이름이 없는 타입(`ref <unmapped>` 로 드러나므로 컴파일 에러로 만들면 된다).

그래서 선언 표를 만든다면 이런 모양이 된다 [제안]. 타입은 컴파일러가 뽑고 사람은 매개변수 이름만 적는다. 이름 개수가 다르면 컴파일 에러다.

```cpp
JBRO_SCRIPT_API(Service::Physics2DService, Raycast, "origin", "direction", "distance", "hit");
```

## 테스트 [확정]

- `.jscript` 표본 모음을 두고 생성 C++ 를 `/W4` 로 컴파일해 경고·에러 0 인지 본다.
- **거절 표본**도 둔다. 타입 에러가 있는 표본은 `jbroc` 이 거절해야 통과한다. 거절 표본이 없으면 무엇이든 받아들이는 `jbroc` 이 통과한다.
- 디버그 매핑은 PDB 에 직접 물어서 잰다(`SymGetLineFromAddr64`). 표본의 문장 줄과 PDB 의 줄이 같아야 한다.
- 진단 메시지는 `ko` 와 `en` 으로 각각 받아 서로 다른지 본다.
- 이름 바꾸기 표본: C++ 키워드·대체 토큰·예약 모양·`jbro_gen_` 시작 이름을 필드·함수·enum 멤버·지역 변수로 쓴 것이 컴파일되고, 저장→로드에서 원래 이름으로 남고, 다른 스크립트가 그 멤버에 닿는지 본다.
- 스크립트 공개 헤더를 include 한 뒤 알려진 매크로 이름이 정의되어 있지 않은지 컴파일 타임에 확인한다(`#ifdef DELETE` 류).

## 엔진 쪽에 필요한 것

| 무엇 | 왜 | 상태 |
|---|---|---|
| 인터페이스로 컴포넌트 찾기(`GetComponent<IDamageable>()`) | 엔진은 지금 정확한 타입 id 로만 찾는다 | 확정: 필요 |
| `Field.h` 가 clang 의 함수 서명 모양도 읽기 | 편집기에서 디버깅하려고 스크립트를 clang 으로 빌드할 때만 컴파일이 멈춘다 | 편집기 디버깅 전에 |
| 값으로 들어 있는 `class` 를 스스로 null 이 되게 가리키기 | `SafePtr` 제어 블록이 `OwnerPtr`·풀에서만 생긴다 | 열린 것 1번에 따라 |
| `Vec2` → `Vector2` | 스크립트는 엔진 타입만 쓰고 이름을 1:1 로 내린다. 84곳, D-57 함께 수정 | 확정: 바꾼다 |
| 엔진 함수 선언 표 | 위 API 투영 | 대기 |

## 열린 것

1. 스스로 null 이 될 수 없는 대상을 멤버 `ref` 로 들고 있는 경우.
2. 자동 검사로 걸러진 뒤의 동작(`return`, `if`/`else`, `while`).
3. 엔진 함수 선언 표. 결정을 미뤘다.
4. 이름 바꾸기의 세부. 방향은 확정이다.

## 편집기 [계획]

스크립트 편집기는 Code-OSS 포크 "JBro Script Editor" 로 만든다(D-87, `tasks/ide-plan.md`). `.jscript` 전용이고 기능은 내장 확장으로, 언어 지식은 `jbroc --lsp` 에 둔다.
문법 강조 확장만 별도 리포(`F:\Project\JBroScriptEditor`)에 섰고 포크는 아직 없다. 씬 에디터(네이티브 ImGui)와는 파일로만 대화한다.
