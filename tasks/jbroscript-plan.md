# JBroScript — 스크립트 언어와 리플렉션 계획

> 2026-09-14 토론 기록. **아직 아무것도 구현하지 않았다.** 방향과 근거, 그리고 실측 결과를 남긴다.
> 확정 계약이 아니라 계획이므로 `docs/ProjectRule.md` 가 아니라 여기에 둔다.

---

## 0. 결론 먼저

- 게임 스크립트 언어를 자작한다. 이름 **JBroScript**, 확장자 **`.jscript`**.
- 백엔드는 VM 이 아니라 **C++ 트랜스파일**이다. `.jscript` → 생성 `.h/.cpp` → 기존 파이프라인.
- 리플렉션은 이 결정으로 **사용자 스크립트 쪽이 해결된다**. 빌트인 컴포넌트는 별도 생산자가 필요하다.
- **형식은 하나(`PropertyInfo`), 생산자는 둘**(C++ 매크로 / 트랜스파일러).
- 언어는 **대체가 아니라 추가 프론트엔드**다. C++ 스크립트 경로를 죽이지 않는다.

**`PropertyInfo` 모양은 §8 에서 확정했다(2026-09-14).** 쪽지 보관함도 둘로 나누기로 했다(§8.4).
남은 것은 `ValueCodec`·`ArrayOps`·`TableOps` 의 구체 모양이며, 대부분 기존 엔진에서 가져올 수 있다.

---

## 1. 왜 이 얘기가 나왔나

Open Decision 3 / H5 의 남은 절반이 프로퍼티 리플렉션이다. `a1e54eb` 로 **이름으로 만드는 경로**는
섰지만(§14.3), 프로퍼티·인스펙터 메타데이터·직렬화는 없다.

그 절반을 C++ 로 어떻게 만들지 논의하다가, 빡대리가 **IDE 를 Code-OSS 로 자체 제작할 계획**이며
스크립트 언어 자체를 만드는 쪽을 고민 중이라고 밝혔다. 그러면 리플렉션 문제의 전제가 바뀐다.

---

## 2. 기존 엔진의 JPROP 이 실제로 무엇이 문제였나 (실측)

새 방향을 정하기 전에 기존 엔진(`C:\Users\박주형\source\repos\JBroEngine`)의 리플렉션을 읽었다.
코드 약 3,800줄 + 프로젝트 생성기 1,324줄.

### 2.1 파서가 정규식이다

`Engine/Editor/Project/GameScriptProjectGenerator.cpp:561`:

```
\bJPROP\s*\(((?:[^()]|\([^()]*\))*)\)\s*([A-Za-z_][A-Za-z0-9_:]*(?:\s*<[^>]*>)?)\s+([A-Za-z_][A-Za-z0-9_]*)...
```

타입 자리가 `<[^>]*>` 라 **꺾쇠 안에 꺾쇠가 못 들어간다.** 어트리뷰트는 괄호 중첩 1단계까지다.
그리고 `header.Text` 는 원본 파일 텍스트 그대로라 **주석도 전처리기도 모른다.**

같은 정규식에 실제 선언을 먹여 본 결과:

| 입력 | 결과 |
|---|---|
| `JPROP() float Speed = 5.0f;` | ✅ |
| `JPROP() Ref<CSpriteAsset> Icon;` | ✅ |
| `JPROP(Range(0,100)) float Hp = 1.0f;` | ✅ |
| `JPROP() Array<Ref<CSpriteAsset>> Items;` | ❌ **누락** |
| `JPROP() Table<int, Array<float>> Curves;` | ❌ **누락** |
| `JPROP() const float Gravity = 9.8f;` | ❌ **누락** |
| `JPROP() float* Buffer = nullptr;` | ❌ **누락** |
| `JPROP(Name(Concat("a"))) float Y;` | ❌ **누락** |
| `// JPROP() float Dead = 1.0f;` | ⚠️ **주석인데 매치** |
| `/* JPROP() int Removed = 3; */` | ⚠️ **주석인데 매치** |

누락은 `CSystemLog::Warning` 으로만 남는다 — **경고지 에러가 아니라** 빌드는 성공하고 그 필드만
인스펙터·저장 파일에서 사라진다. 가장 아픈 조합: 에러 메시지가 지원 타입으로 `Array<T>, Table<K,V>`
를 나열하는데 `Array<Ref<CSpriteAsset>>` 은 파싱이 안 된다. **지원 목록과 파서가 서로 다른 말을 한다.**

반대 방향도 있다. 주석 처리한 프로퍼티가 등록되고 생성기가 `offsetof(PlayerScript, Dead)` 를 뱉어
**생성된 파일에서 컴파일 에러**가 난다. 시끄러운 건 낫지만, 내가 쓰지 않은 파일에서 나고
고치는 방법은 "에디터로 돌아가 재생성"이다.

### 2.2 같은 문제를 두 곳에서 다르게 풀었다

생성기는 `offsetof(ClassName, PropName)` 을 뱉는다. 스크립트는 `CGameScript` 파생이라 가상 함수가
있고, **non-standard-layout 에 `offsetof` 는 조건부 지원**이다. 생성된 파일에서는 클래스가
완성돼 있어 통과한다.

그런데 `REFLECT_FIELD` 는 **클래스 본문 안**이라 같은 `offsetof` 가 C2079 로 죽었고, 그래서
`GetFieldPtr` 람다로 우회했다. 헤더 주석에 그 이유가 적혀 있다.

**하나의 문제, 두 개의 해법, 두 개의 등록 경로.** 이것이 "REFLECT_FIELD 는 레거시 호환" 이 된 이유다.
(참고: `REFLECT_FIELD` 는 **사용자 코드에 0건**이다. 엔진/SDK 헤더에만 남아 있다.)

### 2.3 진실이 도구 안에 있다

필드의 진실은 소스가 아니라 `GeneratedScriptRegistry.cpp` 이고, 그건 **에디터를 돌려야** 갱신된다.
VS 에서 `JPROP` 한 줄 추가하고 빌드하면 컴파일은 되는데 그 프로퍼티는 존재하지 않는다.

### 2.4 공정하게

기존 설계가 허술한 게 아니다. 지정 초기화로 필드 재정렬에 안 깨지게 했고, 마커 수와 매치 수를
비교해 누락을 감지하고, 어트리뷰트 오타까지 경고한다. `Kind` 축이 `Type` 에서 유도되는 이중
진실이라 지운 것도 정확한 판단이었다.

**문제는 그 꼼꼼함이 정규식 위에 서 있었다는 것뿐이다.**

### 2.5 실사용 규모

`JPROP` 선언 59건. 실제 게임 스크립트는 `TestProject/Test/Contents/Scripts/`(Tetris, Movement 등)와
`Samples/`. 쓰는 형태는 스칼라 · `Ref<GameObject>` · `Table<String, Vector2>` 수준이라
**위 실패는 아직 물고 있지 않다.** 다만 "스폰할 프리팹 목록"을 `Array<Ref<Prefab>>` 로 쓰는 순간 문다.

---

## 3. C++26 리플렉션은 답이 아니다 (실측)

P2996 "Reflection for C++26" 이 2025-02 Hagenberg 에서 C++26 작업 초안에 들어갔다.
`^^T` 리플렉션 연산자, `[: :]` 스플라이스, `std::meta::*` consteval 함수, `template for`(P1306),
어트리뷰트(P3394).

**이 툴체인 실측 (MSVC 14.51 / VS 18):**

| | `/std:c++20` | `/std:c++latest` |
|---|---|---|
| `_MSVC_LANG` | 202002 | 202400 |
| `__cpp_reflection` | 없음 | **없음** |
| `__cpp_expansion_statements` | 없음 | **없음** |
| `<meta>` | 없음 | **없음** |

그리고 개념적으로도 착각하면 안 된다 — **C++26 리플렉션은 전부 `consteval` 이다.** 런타임 타입 DB 를
주는 게 아니라 컴파일 타임에 멤버를 훑는 방법을 준다. 그 결과로 런타임 디스크립터 표를 *생성*하게 된다.
즉 **`PropertyInfo` 설계는 C++26 이 와도 그대로다.**

---

## 4. C++ 로 계속 갈 경우의 등록 기법 (실측 완료, 지금 쓸 수 있음)

트랜스파일로 가더라도 **빌트인 컴포넌트는 C++ 이므로 이 기법이 그대로 필요하다.**

### 4.1 이름을 멤버 포인터에서 유도한다

```cpp
template <auto MemberPointer>
constexpr std::string_view MemberName();   // __FUNCSIG__ / __PRETTY_FUNCTION__ 파싱
```

MSVC 14.51 에서 `static_assert(MemberName<&PlayerScript::Health>() == "Health")` 가
**컴파일 타임에 통과**했다. 문자열 리터럴을 손으로 쓰지 않는다 → 이름 드리프트 경로가 사라진다.

⚠ 컴파일러 서명 문자열을 파싱하는 것이라 툴체인 업그레이드에 깨질 수 있다.
채택하면 위 `static_assert` 를 테스트에 박아 둔다.

### 4.2 선언과 등록을 한 토큰에 묶는다

`&Self::Name` 을 **함수 본문 안**에 두면 클래스 완성 후 해석되므로 합법이다 —
기존 엔진이 `GetFieldPtr` 람다로 우회한 그 C2079 문제가 애초에 없고, `offsetof` 도 쓰지 않는다.

가상 함수를 가진 파생 클래스로 실측:

```
polymorphic script, 3 fields (vtable present: sizeof=32)
  [0] Speed    size=4  addr-offset=16
  [1] Health   size=4  addr-offset=20
  [2] Weight   size=8  addr-offset=24
write through accessor: Speed = 42.0 (ok)
static_assert on derived name: ok
```

### 4.3 END 매크로는 필요 없다

`requires { T::JBroFieldAt(Index<N>{}) }` 로 세면 개수를 스스로 알아낸다. 실측 통과.

`__COUNTER__` 구멍(클래스 본문에서 누가 `__COUNTER__` 를 한 번 더 쓰는 경우)은 조용히 잘리지 않고
**시끄럽게 실패**하도록 센 뒤 8칸을 더 확인한다. 음성 프로브로 확인:

```
error C2338: static assertion failed:
  'a gap in the field index means something else consumed __COUNTER__ inside the class body'
```

### 4.4 BEGIN 은 새 부담이 아니다

지금도 스크립트마다 `StaticTypeName()` + `GetTypeId()` 를 손으로 쓴다. 그 두 줄이 한 줄이 된다.

### 4.5 최종 모양 (실측 통과)

```cpp
class TetrisGameManager final : public GameScript2D
{
    JBRO_SCRIPT_BODY(TetrisGameManager, "Game::TetrisGameManager")

    JBRO_FIELD(int,   FieldRows,           Range(4, 40) | Category("Field")) = 20;
    JBRO_FIELD(float, DropIntervalSeconds, Name("낙하 간격"))                = 0.5f;
    JBRO_FIELD(float, Elapsed,             NoSerialize())                    = 0.0f;

    void OnUpdate(float deltaTime) override { ... }
};
```

- 어트리뷰트는 `constexpr` 값이고 `operator|` 로 겹친다 → **오타는 컴파일 에러**("그런 함수 없음").
  기존 엔진은 로그 경고였다.
- **기본값이 매크로 밖에 있다.** 매크로가 등록을 먼저 뱉고 선언을 열어 둔 채 끝내기 때문이다.
- 비교: 기존 `JPROP(Range(4, 40), Category("Field")) int FieldRows = 20;`

---

## 5. 왜 트랜스파일인가

### 5.1 무엇인가

**컴파일러를 만들되 백엔드가 기계어가 아니라 C++ 소스다.**

```
지금:        Player.h (C++)   →  MSVC  →  Game.dll  →  호스트
트랜스파일:  Player.jscript  →  jbroc  →  Player.generated.h/.cpp  →  MSVC  →  Game.dll  →  호스트
                             ↑ 새로 만드는 것          ↑ 여기부터는 손 안 댐
```

`ScriptDLLLoader`, `ScriptRegistry`, `ScriptPool`, Tier 분리, POD ABI — **이번 주에 만든 게 전부 산다.**
그것들 입장에서는 C++ 스크립트가 하나 더 있는 것이다.

### 5.2 `#line` 이 원본을 가리킨다 (실측)

생성 `.cpp` 에 `#line 7 "Player.jscript"` 를 심고 일부러 타입 에러를 넣었다.

```
Player.jscript(7): error C2111: '+': 포인터 더하기에는 정수 계열 피연산자가 있어야 합니다.

PDB 안의 소스 파일 목록:
  ...\Player.jscript      ← 디버거가 이것을 띄운다
  ...\Player.ok.cpp
```

**컴파일 에러는 원본을 가리킨다** — 이건 직접 봤다. **디버그 정보는 PDB 에 원본 경로와 줄이
기록되는 것까지만 확인했다** — 디버거가 실제로 그 줄에서 멈추는 것은 보지 못했다(이 기계에
명령줄 디버거가 없다). §13.3 에 확인 범위를 적어 두었고, **일찍 확인해야 하는 항목**이다 —
여기가 기대대로 안 돌면 트랜스파일 방식 전체의 전제가 흔들린다.

### 5.3 안 만들어도 되는 것

| | VM 만들면 | 트랜스파일하면 |
|---|---|---|
| GC | 직접 | 없음 — 엔진 소유 모델 |
| 바이트코드·인터프리터 | 직접 | 없음 |
| 최적화기 | 직접 | MSVC |
| 디버거 | 직접 | VS 디버거 (실측) |
| 표준 라이브러리 | 직접 | `ScriptAPI.h` 가 이미 그것 |
| 성능 | 10~100배 느림 | **C++ 과 동일** |

**만들 것은 넷:** 렉서 → 파서 → 타입체커 → C++ 이미터.

### 5.4 진짜 숙제는 타입체커다

타입 검사를 안 하면 틀린 코드가 그대로 C++ 로 나가고 **MSVC 에러가 사용자에게 간다.**
`#line` 덕에 파일·줄은 맞지만 메시지는 C++ 말이다(위 C2111 처럼).

**MSVC 까지 도달한 에러는 전부 "내 타입체커의 구멍"이다.** 다행히 점진적으로 채울 수 있다.

### 5.5 선례

Haxe(→C++/JS), Nim(→C), Vala(→C), 초기 C++ 자체(Cfront→C), Construct/GDevelop(→JS).
그리고 **Unity IL2CPP 가 C# 을 C++ 로 트랜스파일**한다 — 아무도 Unity 에 스크립팅이 없다고 하지 않는다.

### 5.6 "스크립트 언어가 아닌가?"

게임 엔진에서 "스크립트"는 한 번도 "인터프리터로 도는 것"을 뜻한 적이 없다.
**엔진 코드가 아닌, 게임 로직이 사는 층**을 뜻한다. UnrealScript(바이트코드), Blueprint(컴파일),
Unity C#(컴파일, IL2CPP 는 C++ 경유) 전부 그렇게 부른다.

구현 전략은 "스크립트냐"와 직교한다. 판단 기준은 하나 — *사용자가 게임 로직을 여기에 쓰는가*.

### 5.7 대가

**즉각성.** VM 은 저장하면 바로 돌지만 트랜스파일은 C++ 컴파일 + 링크가 낀다.
핫 리로드 자체는 기존 DLL 재로드로 되고, 툴체인은 빌드 파이프라인이 감춘다.

설계로 완화한다:
- **`.jscript` 하나당 생성 `.cpp` 하나.** 한 파일에 몰면 한 줄 고칠 때마다 전체 재컴파일이다.
- **내용이 안 바뀌었으면 파일을 덮어쓰지 않는다.** 그래야 MSVC 가 재컴파일을 건너뛴다.
  (기존 엔진 `WriteGeneratedFile` 이 이미 이걸 한다.)
- 생성 헤더는 얇게. 스크립트끼리 서로의 생성 헤더를 include 하면 재컴파일이 번진다.

---

## 6. 확장자와 이름

기존 엔진이 이미 두 계열로 쓴다:

| 계열 | 쓰임 | 예 |
|---|---|---|
| **`.j` + 이름** | 사용자·에셋 파일 | `.jproject` `.jcanvas` `.jprefab` `.jlayer` `.jmat` `.jfx` `.jmeta` |
| **`.jb` + 이름** | 빌드 산출물 | `.jbmanifest` `.jbpack` |

처음 `.jbs` 를 고려했으나 `jb` 계열은 빌드 산출물로 읽힌다. 스크립트 소스는 그 반대다.
→ **`.jscript`** 로 확정.

---

## 7. 빌트인 컴포넌트는 별도 생산자가 필요하다

트랜스파일은 `.jscript` 만 지나간다. **엔진의 빌트인 컴포넌트는 그 길을 안 거친다.**

### 7.1 기존 엔진이 한 방법

`Engine/GameFramework/Component/BuiltinComponentRegistry.cpp` — **210줄에 프로퍼티 127개**,
전부 손으로 쓴 유창 등록 표다.

```cpp
registry.RegisterComponent<SpriteRenderer2D>({ "SpriteRenderer2D", "Sprite Renderer 2D", "Rendering", true })
    .AddAssetProperty("SpriteGuid", offsetof(SpriteRenderer2D, m_spriteGuid), EAssetType::Sprite)
    .AddProperty("Size", EReflectPropertyType::Vector2Float, offsetof(SpriteRenderer2D, m_size), sizeof(Vector2))
    .AddProperty("FlipX", EReflectPropertyType::Bool, offsetof(SpriteRenderer2D, m_flipX), sizeof(bool))
```

멤버 하나당 **세 번** 적는다 — 멤버 선언, 이름 문자열, `offsetof` + `sizeof`. 그것도 다른 파일에서.

없는 멤버를 적으면 컴파일이 깨지므로 조용한 누락은 없다. 대신:

- **이름이 멤버와 따로 논다.** `m_spriteGuid` → `"SpriteGuid"` 로 이미 갈라져 있다
- **타입을 두 번 적는다.** `EReflectPropertyType::Vector2Float` 와 `sizeof(Vector2)` 가 어긋날 수 있다
- **멤버를 추가해도 아무것도 알려주지 않는다**
- **"일부러 안 뺀 것"과 "빼먹은 것"을 구분할 수 없다.**
  `CachedSpriteGuid`, `CachedPixelsPerUnit` 은 런타임 캐시라 등록 안 하는 게 맞는데,
  그게 의도인지 실수인지 코드만 봐선 모른다

### 7.2 그래서 빌트인은 §4 의 매크로로

```cpp
class SpriteRenderer2D final : public ComponentBase
{
    JBRO_COMPONENT_BODY(SpriteRenderer2D, "SpriteRenderer2D")

    JBRO_FIELD(Vec2,  size,  Name("크기"))          = {1.0f, 1.0f};
    JBRO_FIELD(bool,  flipX)                        = false;
    JBRO_FIELD(Color, tint,  Category("Rendering")) = {1, 1, 1, 1};

    // 등록 안 함 — 캐시다. 매크로가 없다는 것 자체가 의도 표시가 된다.
    AssetGuid cachedSpriteGuid;
};
```

손 표 대비: 이름을 안 적고, 타입을 안 적고, `offsetof` 를 안 쓰고, 선언 옆에 있고,
**private 멤버도 자연스럽다**(매크로가 클래스 *안*에 있으므로 접근 권한 문제가 없다 —
손 표는 바깥에서 private 을 `offsetof` 로 찔러야 했다).

### 7.3 핵심 — 형식 하나, 생산자 둘

| | 언어 | 생산자 | 산출물 |
|---|---|---|---|
| 빌트인 컴포넌트 | C++ | `JBRO_FIELD` 매크로 | `PropertyInfo[]` |
| 사용자 스크립트 | `.jscript` | 트랜스파일러 | `PropertyInfo[]` |

인스펙터·직렬화·undo 는 둘을 구분하지 못해야 한다. 그게 목표다.

기존 엔진의 진짜 실수는 "빌트인과 스크립트가 경로가 다르다"가 아니라
**스크립트 하나를 놓고 형식이 둘**이었던 것(`JPROP` 코드젠 vs `REFLECT_FIELD`)이다.
언어가 다르면 생산자가 둘인 건 당연하고 건강하다.

---

## 8. `PropertyInfo` — 모양 확정 (2026-09-14)

### 8.1 이게 뭐 하는 물건인가

에디터에서 스프라이트를 클릭하면 오른쪽 패널에 이렇게 뜬다:

```
SpriteRenderer2D
  크기      [1.0] [1.0]
  뒤집기X   ☐
```

패널이 저걸 그리려면 누가 알려 줘야 한다 — "SpriteRenderer2D 에는 `size` 라는 필드가 있고,
Vec2 이고, 메모리 어디에 있고, 화면엔 '크기' 라고 써라." 그 쪽지가 `PropertyInfo` 다.
**필드 하나당 쪽지 하나.** 저장 파일을 쓸 때도 같은 쪽지를 본다.

### 8.2 기존 엔진 쪽지의 문제 넷

`ReflectPropertyInfo` 17필드를 읽고 정리한 것이며, 근거는 대부분 **그들 자신의 주석**이다.

1. **주소 찾는 법이 둘인데 어느 게 진짜인지 안 적혀 있다.**
   `Offset`(offsetof)과 `GetFieldPtr`(함수 포인터)가 나란히 있고 주석이
   *"둘 중 하나만 사용"* 이라고만 말한다. 판별자 없는 union 이라 소비자마다 알아야 했다.
2. **같은 사실이 두 군데 있다.** `ReflectTypeDesc` 주석이 직접 말한다 —
   *"Type 은 ReflectPropertyInfo::Type / ScriptPropertyDesc::Type 과 같은 값·같은 의미다."*
   `Size` 도 마찬가지다. `Kind` 축을 "이중 진실" 이라고 지웠으면서 다른 축에 그대로 남겼다.
3. **타입 집합이 18값 enum 으로 닫혀 있다.** `StringEdit` 필드 주석이 대가를 기록한다 —
   *"새 EReflectPropertyType 을 만들지 않는 이유: 직렬화·undo·라이브 컴파일이 전부
   `Type == String` 으로 판정하므로 열거값을 늘리면 그 경로가 전부 갈라진다."*
   즉 **새 타입을 못 넣어 곁가지 필드를 달았다.** `ComponentSerializer.cpp` 1,596줄의 원인이다.
4. **타입의 성질과 필드의 성질이 섞여 있다.** `RefCategory`·`RefTypeName`·`ExpectedAssetType`·`Enum`
   은 전부 *타입* 의 성질인데 프로퍼티에 붙어 있다. 17칸 중 5칸이 대부분 빈칸이다.

덤으로 `ScriptPropertyDesc` 가 `ReflectPropertyInfo` 를 13필드쯤 복제한다(등록용/저장용 분리).

### 8.3 그래서 쪽지를 두 장으로 나눈다

**타입 설명서** — 타입마다 한 장, 모두가 공유한다.

```cpp
// 크기·정렬·복사 방식·컨테이너 조작이 전부 여기 있다.
// PropertyInfo 는 가리키기만 한다 — 같은 사실을 두 군데 적지 않는다.
struct TypeDescriptor
{
    NameId        typeName  = InvalidNameId;   // "float", "JBro.Vec2", "Ref<Sprite>"
    std::uint32_t size      = 0;
    std::uint32_t alignment = 0;
    bool          triviallyCopyable = false;

    // 구조는 ops 의 존재로 드러난다. 별도 Kind 축을 두지 않는다(§8.2 ②).
    const ArrayOps* arrayOps = nullptr;   // != nullptr 이면 element 유효
    const TableOps* tableOps = nullptr;   // != nullptr 이면 key/value 유효
    const TypeDescriptor* element = nullptr;
    const TypeDescriptor* key     = nullptr;
    const TypeDescriptor* value   = nullptr;

    // 타입에 붙는 사실. 프로퍼티가 아니라 타입의 성질이다(§8.2 ④).
    const EnumNames* enumNames = nullptr;   // enum 일 때만
    const RefTarget* refTarget = nullptr;   // Ref<T> 일 때만 (대상 분류 + 기대 에셋 타입)

    // 값 ↔ 텍스트/바이트. 직렬화가 타입별 switch 를 갖지 않게 하는 부분(§8.2 ③).
    const ValueCodec* codec = nullptr;
};
```

**닫힌 enum 이 없다.** "배열인가?" 는 `arrayOps != nullptr` 이 답하고, "무슨 타입인가?" 는
`typeName` 이 답한다. 새 타입을 더해도 코어를 건드리지 않는다.

**필드 쪽지** — 필드마다 한 장.

```cpp
struct PropertyInfo
{
    NameId name = InvalidNameId;
    const TypeDescriptor* type = nullptr;

    // 접근 방법은 하나뿐이다(§8.2 ①).
    // private 멤버도, 파생 클래스도, 생성 코드도 전부 같은 방식이다.
    void*       (*Address)(void* owner) noexcept = nullptr;
    const void* (*ConstAddress)(const void* owner) noexcept = nullptr;

    bool serialize = true;                    // false = 인스펙터엔 나오되 저장 안 함
    const PropertyEditInfo* edit = nullptr;   // 게임 빌드에선 nullptr 이어도 된다
};

// 인스펙터만 읽는다. 런타임(직렬화)은 이걸 안 본다.
struct PropertyEditInfo
{
    const char* displayName = nullptr;
    const char* tooltip     = nullptr;
    const char* category    = nullptr;
    bool  hasRange = false;
    float rangeMin = 0.0f;
    float rangeMax = 0.0f;
    bool  editable = true;
};
```

17필드 → **6필드 + 편집 메타 분리.** `edit` 을 `nullptr` 로 두면 게임 빌드에서 한글 툴팁이
통째로 빠진다.

### 8.4 보관함은 둘로 나눈다 (확정)

빌트인 컴포넌트와 사용자 스크립트의 쪽지를 **한 표에 섞지 않는다.**

기존 엔진은 `EReflectTypeKind { Component, Script }` 로 구분해 한 표에 넣었다. 그러면 인스펙터가
한 번만 물어보면 되지만, **수명이 다른 것이 한 그릇에 있게 된다** — 스크립트 쪽지는 DLL 이
내려갈 때 같이 죽어야 하고(`ScriptRegistry::Clear` 가 이미 그 이유로 존재한다), 빌트인 쪽지는
엔진 수명이다. 섞여 있으면 지울 때마다 골라내야 한다.

| 보관함 | 수명 | 채우는 쪽 |
|---|---|---|
| 빌트인 컴포넌트 표 | 엔진 | `JBRO_FIELD` 매크로 (C++) |
| 스크립트 표 | 로드된 DLL | 트랜스파일러가 생성한 코드 |

대가: 인스펙터가 두 군데를 찾아본다. 그 조회 순서와, 양쪽에 같은 이름이 있을 때의 규칙은
붙일 때 정한다(빌트인 우선이 자연스럽다 — 스크립트가 엔진 타입명을 덮지 못하게).

### 8.5 `ValueCodec` — 모양 확정 (2026-09-14)

값 하나를 **글자로 바꾸고 글자에서 되돌리는** 것만 한다.

**안 하는 것이 중요하다.**

- **컨테이너를 모른다.** 배열·표는 직렬화기가 `ArrayOps`/`TableOps` 로 걸어 내려가며
  원소마다 코덱을 부른다. 코덱은 **잎사귀 값**만 본다
- **YAML 을 모른다.** 코덱은 알맹이만 내놓고 따옴표·들여쓰기·줄바꿈은 직렬화기가 처리한다.
  기존 엔진에서 `.jproject` 파서가 **여러 줄 스칼라(`|+`)에서 두 번 죽었다**(§14.2) —
  YAML 을 아는 곳이 여러 군데면 그런 것이 여기저기서 터진다. 한 곳만 알게 둔다

```cpp
struct ValueCodec
{
    // 글자로 쓴다. 버퍼가 모자라면 false 를 주고 required 에 필요한 크기를 적는다.
    // 경계를 넘으므로 호출자가 버퍼를 소유한다 — 문자열을 돌려주지 않는다.
    bool (*ToText)(const void* value, char* buffer, std::size_t capacity,
                   std::size_t& required) noexcept = nullptr;

    // 글자에서 읽는다. 못 읽으면 value 를 건드리지 않고 false 다. 예외는 경계를 넘지 않는다.
    bool (*FromText)(void* value, const char* text, std::size_t length) noexcept = nullptr;

    // 같은 값인지 본다. 인스펙터의 "기본값으로 되돌리기" 와 undo 의 "진짜 바뀌었나" 가 쓴다.
    bool (*Equals)(const void* left, const void* right) noexcept = nullptr;

    // 복사한다. **memcpy 로 대신하지 않는다** — String 처럼 내용이 밖에 있는 타입은
    // 얕은 복사가 되어 먼저 죽는 쪽이 남은 쪽을 망가뜨린다.
    void (*Assign)(void* destination, const void* source) noexcept = nullptr;
};
```

기존 엔진은 이 자리를 18값 enum 의 `switch` 로 처리했고, **그 switch 가 직렬화기 한 파일에만 여섯 벌**
있었다. 타입 하나 늘리면 여섯 곳을 고쳐야 한다는 뜻이다.

`Assign` 이 따로 있는 이유: 기존 엔진 undo 가 문자열 아닌 값을 `memcpy` 로 되돌렸고 그들 스스로
위험으로 적어 뒀다. **항상 `Assign` 을 부르면** 그 함정이 없다 — 단순한 타입이면 내부에서 `memcpy`
하면 되고, 부르는 쪽은 고민할 게 없다.

### 8.6 저장 정책 — 기본값도 전부 쓴다 (확정)

기본값과 같은 프로퍼티를 저장 파일에서 **빼지 않는다.**

뺐다면 파일이 작고 diff 가 깔끔했을 것이다. 그러나 **나중에 기본값을 바꾸면 기존 씬들이 조용히
같이 바뀐다.** 추적하기 가장 어려운 종류의 버그이고, 2D 씬 파일 크기는 그 대가를 치를 만큼 크지 않다.

`ValueCodec::Equals` 는 그래도 남긴다 — 인스펙터의 "되돌리기" 버튼과 undo 의 변경 판정이 쓴다.

### 8.7 `ArrayOps` / `TableOps` — 기존 엔진 것을 가져온다

새로 설계하지 않는다. 기존 `ReflectArrayOps` / `ReflectTableOps` 가 이미 옳은 모양이고,
그 주석에 값비싸게 얻은 두 가지가 적혀 있다. **다시 깨닫지 말 것.**

1. **Table 은 슬롯 번호로 원소를 지목하면 안 된다.** open addressing 이라 슬롯이 조밀하지 않고,
   삽입 한 번에 리해시가 나면 기존 슬롯 번호가 전부 무효가 된다. 순회는 불투명한 커서로 하고
   수정은 **키**로 지목한다
2. **키·값을 `memcpy` 로 복사할 수 없다.** `String` 처럼 내용이 밖에 있는 타입은 얕은 복사가 되어
   먼저 소멸하는 쪽이 남은 쪽을 망가뜨린다

덧붙여 기존 엔진이 못 닫은 것 하나가 여기서는 닫힌다. 그들 규칙은 "엔진 컨테이너를 호스트와
게임 DLL 사이의 ABI-safe 타입으로 보지 않는다" 였고, 그래서 스크립트 필드에 owning `Array`/`Table`
을 허용하지 못했다. **새 구조에서는 허용된다** — 스크립트의 컨테이너에 가하는 모든 연산이
DLL 이 제공한 `ArrayOps`/`TableOps` 함수 포인터를 거치므로, 할당도 해제도 전부 DLL 안에서 일어난다.
호스트는 저장소 레이아웃을 직접 만지지 않는다.

## 9. 규율 — 언어가 엔진을 잡아먹지 않게

**언어를 대체가 아니라 추가 프론트엔드로 둔다.**

`.jscript` → C++ 생성 → 기존 파이프라인. **C++ 스크립트 경로는 그대로 살려 둔다.**
그러면 언어가 막히거나 재미없어져도 엔진은 멀쩡하다. 트랜스파일 방식이 이걸 자연스럽게 준다 —
출력이 애초에 C++ 이므로.

---

## 10. 순서

1. **`PropertyInfo` / `TypeDescriptor` 모양 확정** ← 여기부터. ABI 다
2. **`JBRO_FIELD` 매크로** (빌트인 + 당분간 C++ 스크립트). **완료 (2026-09-14, `7be6602`)** — §14 참조
3. **직렬화** — `.jcanvas` 형식이 필요. `.jproject` 처럼 기존 엔진 것을 따른다 ← **여기부터**
4. **인스펙터** — 어트리뷰트를 실제로 쓰는 유일한 소비자. 에디터가 생길 때
5. **`jbroc`** — 렉서 → 파서 → 타입체커 → C++ 이미터
6. **LSP** — 5번의 AST 를 재사용한다. Code-OSS IDE 와 파서 하나를 공유하는 것이 자작의 이점

1번만 해 두면 나머지는 언제 해도 재작업이 아니다.

---

## 11. 아직 안 정한 것

- **지원 타입 범위.** 트랜스파일로 가도 `Array<Ref<T>>` 가 자동으로 되는 게 아니다 —
  *선언이 파싱된다*는 것뿐이고 `ArrayOps`/`TableOps` 는 여전히 필요하다. 정규식과 무관한 별개 축이다
- **JBroScript 문법** 자체
- **타입체커 범위** — 어디까지 내가 잡고 어디부터 MSVC 에 넘길 것인가
- **기존 59건 마이그레이션** 여부와 방식

---

## 12. 이 문서를 만든 근거의 출처

전부 이 리포 또는 `C:\Users\박주형\source\repos\JBroEngine`(읽기 전용 기준)에서 읽거나,
MSVC 14.51 로 직접 컴파일해 얻었다. 추측으로 적은 항목은 없다.
실측 프로브는 세션 스크래치패드에 있었고 커밋하지 않았다 — 재현이 필요하면 §4·§5.2 의
설명만으로 다시 만들 수 있다.

---

## 12. JBroScript 문법 — 1차 확정 (2026-09-14)

### 12.1 모양

```
script TetrisGameManager : GameScript2D
{
    [range(4, 40), category("Field")]
    int FieldRows = 20

    [name("낙하 간격")]
    float DropInterval = 0.5

    [category("Canvas")]
    Ref<Transform2D> FieldBox

    [prop]
    bool ShowDebug = false

    float      elapsed = 0.0        // 대괄호가 없으면 노출되지 않는다
    Array<int> lines

    fn OnUpdate(float dt)
    {
        elapsed += dt
        if elapsed < DropInterval
        {
            return
        }
        elapsed = 0.0

        if let box = FieldBox
        {
            box.position.y -= 1.0
        }
    }
}
```

기존 것과 나란히:

```
JPROP(Range(4, 40), Category("Field")) int FieldRows = 20;    // 전
[range(4, 40), category("Field")]      int FieldRows = 20     // 후
```

거의 같은데 **정규식도, 코드 생성기도, 에디터를 돌려야 하는 것도 없다.**

### 12.2 정한 것

| 항목 | 결정 | 이유 |
|---|---|---|
| 타입 위치 | 이름 **앞** (`float speed`) | C++ 눈에 익다. `[...]` 나 `fn` 이 앞에 있어 파서 난이도는 같다 |
| 블록 | **중괄호** | 생성될 C++ 과 모양이 맞고 파서가 단순하다 |
| 문장 끝 | **줄바꿈** | 렉서 규칙 하나면 된다 |
| 노출 표시 | **대괄호 어트리뷰트** | `[prop] const float x` 가 `prop const float x` 보다 읽기 좋다 |
| 어트리뷰트 위치 | **선언 윗줄** (한 줄도 허용) | 선언 줄이 깨끗하고, 이름을 한 번만 쓴다 |
| `var` | **없음** | 대괄호 유무로 이미 갈린다. 멤버와 지역 변수가 같은 모양이 된다 |

### 12.3 노출 규칙

> **대괄호가 붙으면 그 필드는 노출된다.** `[prop]` 은 "설정 없이 노출" 이고,
> 나머지 어트리뷰트는 그 노출을 어떻게 그릴지다.

| | 인스펙터 | 저장 |
|---|---|---|
| `[prop]` | 나옴 | 됨 |
| `[range(...)]`, `[name(...)]`, `[category(...)]` 등 | 나옴 | 됨 |
| `[noserialize]` | 나옴 | **안 됨** (런타임 전용) |
| 없음 | 안 나옴 | 안 됨 |

**위험 하나.** 이 규칙은 "필드 어트리뷰트는 전부 인스펙터 얘기" 를 전제한다. 나중에 인스펙터와
무관한 것(예: `[deprecated]`)이 생기면 그걸 붙이는 순간 필드가 노출된다.
**대비로 `[hidden]` 을 예약해 둔다** — 그날 `[deprecated, hidden]` 로 빠져나간다.
이 실패는 인스펙터에 안 보이던 게 보이는 것이라 **바로 눈에 띈다.** 조용히 망가지는 종류가 아니다.

### 12.4 `var` 을 뺀 대가

`float step = dt * 2.0` 이 선언인지 알려면 파서가 `float` 이 타입임을 알아야 한다.
그래서 **두 번 훑는다** — 먼저 `script` 이름들을 걷고(엔진 타입은 이미 안다), 그 다음 함수 몸통을 읽는다.
흔한 방식이고 어렵지 않다.

`var` 은 나중에 **타입 추론용**으로 남겨 둔다(`var x = 5.0`). 지금은 쓰지 않는다.

### 12.5 `if let` — 이것 하나는 꼭 넣는다

```
if let box = FieldBox
{
    box.position.y -= 1.0
}
```

`Ref<T>` 는 가리키던 것이 죽었을 수 있다. C++ 이면 `FieldBox.Get()` 이 null 을 주고, 그걸 안 보고
쓰면 터진다 — 게임 스크립트 크래시의 1등이다. `if let` 은 **검사하지 않으면 쓸 수 없게** 만든다.

C++ 로는 그냥 이렇게 나간다:

```cpp
if (Transform2D* box = FieldBox.Get()) { ... }
```

만들기는 쉬운데 크래시 한 종류가 통째로 사라진다. 복잡도를 쓸 만한 몇 안 되는 자리다.

### 12.6 대괄호가 겹치지 않는 이유

**어트리뷰트는 선언 자리에만 온다.** 클래스 몸통, `script` 앞, `fn` 앞. 그 자리에서 `[` 로
시작할 수 있는 것은 어트리뷰트뿐이다.

```
lines[0] = 5              // 문장이 `lines` 로 시작한다
Array<int> a = [1, 2, 3]  // `[` 가 `=` 뒤 — 표현식 자리
Array<T>                  // 제네릭은 <> 라 무관
```

문장이 `[` 로 시작하는 경우는 "배열 리터럴 하나만 덩그러니 놓인 문장" 뿐이고, 그것은 아무 의미가
없으므로 문법에서 뺀다. 커스텀 어트리뷰트는 대괄호 **안**이 열려 있어 얼마든지 들어간다.

덤으로 `[range(0,100)] float speed` 는 **C++26 이 주려는 그 모양**이다(`[[=Range{0,100}]]`).
언어를 직접 만드니 기다릴 필요 없이 지금 쓴다.

### 12.7 v1 에 넣지 않는 것

스크립트끼리 상속, 사용자 제네릭, 람다·클로저, 연산자 오버로딩.
전부 나중에 더할 수 있고, 지금 넣으면 타입체커가 몇 배로 커진다.

### 12.8 아직 안 정한 것

- 표현식 문법 세부(연산자 우선순위, 형변환 표기)
- 엔진 API 표면을 언어에서 어떻게 부를지 — `ScriptAPI.h` 의 투영이 될 것이다
- `fn` 의 반환 타입 표기
- 오류 처리(`Result` 같은 것을 둘지, 아니면 없이 갈지)

---

## 13. IDE 와 IntelliSense (2026-09-14)

빡대리가 **Code-OSS 로 IDE 를 자체 제작**할 계획이므로, 두 갈래를 나눠 본다 —
C++ 로 남는 쪽(빌트인 컴포넌트)과 `.jscript` 쪽은 사정이 완전히 다르다.

### 13.1 C++ 매크로와 IntelliSense — 된다

`JBRO_FIELD` 매크로가 IntelliSense 를 망치는지 확인했다. 전처리해서 **IntelliSense 가 실제로
보게 되는 것**을 꺼내 보면:

```cpp
struct SpriteRenderer2D
{
    public: using Self = SpriteRenderer2D;
    static constexpr const char* StaticTypeName() { return "SpriteRenderer2D"; }
    static constexpr int JBroCounterBase = 0;

    static FieldInfo JBroFieldAt(Index<...>) { ... }  float speed = 5.0f;
    static FieldInfo JBroFieldAt(Index<...>) { ... }  bool  flipX = false;
};
```

매크로가 풀리고 나면 **평범한 멤버 선언**이다. IntelliSense 는 매크로를 펼쳐 파싱하므로
`speed` 를 `float` 멤버로 그대로 본다.

| | |
|---|---|
| 멤버 자동완성 (`sprite.sp…`) | 된다 |
| 마우스오버 타입 표시 | 된다 |
| 정의로 이동 · 이름 바꾸기 | 된다 (`&Self::speed` 가 진짜 토큰이다) |
| **매크로 인자 *안*에서 타이핑** | **약하다** |
| 에러 위치 | 매크로 줄 전체를 가리킨다 |

네 번째가 실제 불편이다. `JBRO_FIELD(Vec` 까지 쳤을 때 완성이 잘 안 나온다.

**다만 영향 범위가 작다.** `.jscript` 로 가면 이 매크로는 **엔진 빌트인 컴포넌트에만** 쓴다 —
사용자가 아니라 엔진 개발자가 쓰는 곳이고, 기존 엔진 기준 필드 127개, 한 번 쓰고 나면 거의
건드리지 않는다. 사용자 스크립트는 `.jscript` 로 가므로 이 불편을 겪지 않는다.

### 13.2 `.jscript` 의 IDE 지원 — 공짜는 없다

새 확장자에 Code-OSS 가 해 주는 것은 **아무것도 없다.** 네 조각이고 비용이 완전히 다르다.

| 조각 | 비용 | 무엇이 생기나 |
|---|---|---|
| ① 문법 강조 | 몇 시간 | 색이 입혀진다 |
| ② 에러 표시 | 하루 이틀 | 빨간 밑줄이 그어진다 |
| ③ 디버깅 | **거의 공짜** | `.jscript` 에 중단점을 찍는다 |
| ④ 자동완성 · 정의로 이동 | 진짜 일 | IDE 다워진다 |

**① 문법 강조** — TextMate 문법 파일(JSON + 정규식) 하나면 된다. VS Code 계열이 그대로 먹는다.
가장 싼 체감 개선이다.

**② 에러 표시** — LSP 중 **진단(diagnostics)이 제일 쉽다.** 저장할 때 `jbroc` 을 돌리고 나온 에러를
그대로 넘기면 된다. 자동완성 없이 이것만 있어도 체감이 크게 달라진다.
그리고 §5.4 에서 정한 대로 **MSVC 까지 도달하는 에러는 타입체커의 구멍**이므로, 이 진단이
곧 "내 컴파일러가 얼마나 여물었나" 의 척도가 된다.

**③ 디버깅** — `#line` 덕에 기존 C++ 디버거가 `.jscript` 를 띄운다. **디버그 어댑터를 만들지 않는다.**
확인 범위는 §13.3 을 볼 것.

**④ 자동완성 · 정의로 이동** — 여기가 프로젝트다. 다만 **트랜스파일러의 AST 를 그대로 쓴다** —
파서와 타입 정보를 이미 손에 쥐고 있으므로 남의 언어에 LSP 를 붙이는 것보다 훨씬 싸다.
**이것이 언어를 직접 만드는 실제 이점이다.** 파서 하나가 컴파일러와 LSP 를 둘 다 먹인다.

### 13.3 확인한 것과 확인하지 않은 것

계획서 전체에서 이 절이 가장 중요하다. 실측과 추정을 섞지 않는다.

**직접 확인함**

- 매크로 전처리 결과가 평범한 멤버 선언이다 (§13.1 의 출력)
- `#line` 이 붙은 생성 C++ 의 **컴파일 에러가 `.jscript` 파일·줄을 가리킨다**
- **PDB 에 `.jscript` 경로가 소스 파일로 기록된다**

**확인하지 않음**

- **디버거가 실제로 `.jscript` 줄에서 멈추는가.** PDB 기록까지만 봤다.
  이 기계에 명령줄 디버거(cdb)가 없어 스텝을 돌려보지 못했다
- IntelliSense 가 매크로 인자 안에서 얼마나 나쁜가 — VS 를 띄워 확인한 것이 아니라
  매크로 인자 안에서 완성이 약하다는 일반적 성질에 기댄 판단이다
- TextMate 문법과 LSP 는 **아무것도 만들지 않았다.** 비용 추정은 경험칙이다

**그래서 일찍 해야 할 일**

> **VS 로 생성 C++ 을 한 번 디버깅해 볼 것.** `.jscript` 에 중단점이 찍히고 변수가 보이는지.
>
> 여기가 기대대로 안 돌면 **트랜스파일 방식의 전제 하나가 무너진다**(§5.3 의 "디버거는 안 만들어도
> 된다"). 그 경우 선택지는 디버그 어댑터를 직접 만들거나, 생성 C++ 을 그냥 디버깅하며 사는 것이다.
> 둘 다 치명적이진 않지만 **알고 시작하는 것과 나중에 아는 것은 다르다.**

### 13.4 권하는 순서

```
① 문법 강조          몇 시간, 바로 체감
② 에러 표시          하루 이틀, 쓸 만해짐
③ 디버깅 확인        VS 로 한 번 붙여만 보기 — 되도록 일찍
④ 자동완성           나중에, 트랜스파일러가 돌기 시작한 뒤
```

①②만 있어도 `.jscript` 를 쓸 만하다. ④가 없어도 개발이 막히지 않는다.
③은 순서상 늦어도 되지만 **위험 때문에 앞당긴다.**

---

## 14. `JBRO_FIELD` — 붙였다 (2026-09-14)

§10 의 2번이다. §4 의 실측을 그대로 코드로 옮겼고, 변이 14개 중 14개가 잡힌다.

### 14.1 쓰는 모양

```cpp
class Probe final : public FakeComponentBase
{
    JBRO_REFLECT_BODY(Probe)

    JBRO_FIELD(int,   FieldRows,   Range(4, 40) | Category("Field")) = 20;
    JBRO_FIELD(float, DropSeconds, Name("낙하 간격") | Tooltip("..."))  = 0.5f;
    JBRO_FIELD(float, Elapsed,     NoSerialize())                    = 0.0f;
    JBRO_FIELD(double, Plain)                                        = 1.25;
};
```

`const PropertyTable& table = GetPropertyTable<Probe>();` 하나면 표가 나온다.
**END 매크로가 없고, 개수를 어디에도 적지 않는다.**

어트리뷰트는 `Name` / `Tooltip` / `Category` / `Range` / `ReadOnly` / `NoSerialize` 다.
전부 `constexpr` 값이고 `|` 로 겹친다 — 오타는 컴파일 에러다.

### 14.2 이름을 손으로 쓰지 않는 이유

`Detail::FieldSignature<&T::Field>()` 의 `__FUNCSIG__` 에서 잘라 온다.
MSVC 14.51 x64 실측 형식:

```
class std::basic_string_view<...> __cdecl JBro::Detail::FieldSignature<&Game::Player::Speed>(void)
```

마지막 `>(` 앞의 마지막 `::` 다음이 이름이다. private 멤버도 같은 방식으로 잡힌다
(클래스 안에서 멤버 포인터를 만들기 때문이다).

**필드 이름을 바꿨는데 등록된 이름이 옛것인 경로가 아예 없다.** 대신 컴파일러 서명 형식에
기대므로, 테스트가 `static_assert` 로 실제 이름 두 개를 붙잡아 둔다 — 툴체인을 올려서
형식이 바뀌면 이름이 조용히 틀리는 대신 컴파일이 멈춘다.

### 14.3 `__COUNTER__` 구멍은 시끄럽게 실패한다

인덱스를 `__COUNTER__` 로 매기므로, 클래스 본문에서 누가 `__COUNTER__` 를 한 번 더 쓰면
번호에 구멍이 나고 세는 쪽이 거기서 멈춘다 — **뒤의 필드가 조용히 사라진다.**
멈춘 자리 뒤로 여덟 칸을 더 보고, 뭔가 있으면 `static_assert` 로 멈춘다.

테스트가 그 상황을 일부러 만든다(`GapProbe`). 감지기를 껐을 때 실제로 우는지 확인하려면
감지될 구멍이 하나는 있어야 하는데, 변이 전에는 그게 없어서 **감지기를 꺼도 전 스위트가
통과했다.** 변이가 그것을 잡았고 테스트를 붙였다.

### 14.4 붙인 결정 두 가지

- **편집 메타데이터는 필요할 때만 붙는다.** 어트리뷰트가 하나도 없는 필드는
  `PropertyInfo::edit` 가 `nullptr` 이다 — 게임 빌드가 한글 툴팁 문자열을 통째로 안 들고 간다.
- **어트리뷰트 이름은 `JBro::Attribute` 에 있다.** 매크로가 함수 본문에 `using namespace` 를
  넣어 수식 없이 보이게 한다. `JBro` 직속에 `Name` / `Range` 를 두면 프렐류드의
  `using namespace JBro;` 가 사용자 코드와 충돌한다.

### 14.5 모르는 타입은 컴파일 에러다

`TypeDescriptorOf<T>` 특수화가 없는 타입을 필드로 쓰면 멈춘다. 기존 엔진은 모르는 타입을
만나면 로그 경고를 남기고 그 필드를 **조용히 빠뜨렸다** — 저장 파일에서 값이 사라지는데
아무도 모르는 실패다. 지금 등록된 것은 `bool`, 고정폭 정수 8개, `float`, `double` 이다.
`Vec2` 처럼 구조를 가진 타입은 **자기 모듈에서** 특수화한다 — Core 가 Framework 타입을
알 필요가 없다.

### 14.6 아직 아닌 것

- **보관함 둘**(§8.4)은 아직 없다. 지금은 `GetPropertyTable<T>()` 로 타입을 알 때만 물어본다.
  이름으로 찾으려면 표가 필요하다
- **빌트인 컴포넌트 8개**에 아직 안 붙였다
- **`ArrayOps` / `TableOps`** 는 여전히 선언만 있다. 필드가 컨테이너를 담기 시작할 때 붙인다

---

## 15. 보관함 둘과 빌트인 부착 (2026-09-14)

§10 의 나머지 절반이다. 이제 **인스펙터가 쓸 수 있는 상태**다 — 타입 이름만 알면 필드가 나온다.

### 15.1 보관함 둘 (`c64b8c6`)

`PropertyRegistry` 하나에 표가 둘이다(§8.4 확정 그대로).

| | 수명 | 채우는 쪽 | 비우는 때 |
|---|---|---|---|
| `Builtin()` | 엔진 | `JBRO_FIELD` | 없음 |
| `Script()` | 로드된 DLL | 트랜스파일러 생성 코드 | 모듈 언로드 |

스크립트 표는 `ScriptRegistry` 와 같은 `Local`/`Get`/`Bind` 를 갖는다 — DLL 이 등록하고
호스트가 읽으므로 바인딩하지 않으면 각자 자기 사본을 보게 된다(D-44).

**스크립트는 엔진 타입명을 못 가져간다.** `RegisterScript` 가 빌트인에 이미 있는 이름을
거절한다. 통과시키면 `Lookup` 이 조용히 빌트인 쪽을 주고, 스크립트 작성자는 자기 필드가
왜 안 보이는지 알 길이 없다.

### 15.2 구조체와 enum (`ff94c69`)

컴포넌트 필드가 스칼라만이 아니었다. `Vec2`, `Color`, `Matrix3x2`, enum 넷, `AssetHandle`.

**`TypeDescriptor` 에 `fields` 를 더했다.** 규칙은 하나다:

> 구조를 가진 타입은 **필드로** 말하고, 잎사귀 값은 **코덱으로** 말한다. 둘 다는 없다.

인스펙터도 직렬화도 필드를 타고 내려가다 코덱을 만나면 멈춘다. `Vec2` 가 무엇인지 아무도
알 필요가 없다 — 기존 엔진에서 같은 지식이 여섯 군데 흩어진 이유가 그것이었다.
크기는 88 → **96 바이트**(재서 넣었고, `static_assert` 가 변경을 먼저 잡았다).

**enum 은 숫자가 아니라 이름으로 저장한다.** 숫자면 나중에 값 하나를 가운데 끼워 넣는 순간
예전 파일이 전부 한 칸씩 밀린다. 이름 없는 값은 양방향 모두 거절한다 — 숫자로 흘려 쓰지도,
모르는 이름을 첫 값으로 떨어뜨리지도 않는다.

**핫 경로 타입은 클래스 밖에서 설명한다.** `Math2D.h` 에 매크로를 넣으면 벡터를 쓰는 모든
번역 단위가 리플렉션 기계를 물고 간다. `StaticPropertyTable` 이 밖에서 표를 엮고,
이름은 거기서도 멤버 포인터에서 나온다. **필드가 늘어나는 타입에는 쓰지 않는다** —
표가 선언과 떨어져 있으면 드리프트가 돌아온다.

### 15.3 빌트인 2D 컴포넌트 다섯 (`b7354f2`)

`Transform2D`(8) · `Camera2D`(6) · `SpriteRenderer2D`(8) · `Rigidbody2D`(7) · `Collider2D`(5).
`Framework2D::Initialize` 가 등록한다.

**저장하지 않는 필드를 두 종류 표시했다.**

- **월드 캐시**(`Transform2D`) — 저작 값에서 다시 계산된다. 저장하면 두 벌이 되어 어긋날 수
  있고, 인스펙터에서 고쳐 봐야 다음 갱신에 덮인다. 보이기는 하되 `ReadOnly` 다
- **시뮬레이션 값**(`Rigidbody2D::linearVelocity`) — 매 프레임 다시 쓰인다. 저장하면 씬을 열
  때마다 저장 순간의 속도로 튀어 나간다

**테스트가 모든 잎사귀를 훑는다.** 빌트인 다섯의 필드를 끝까지 내려가며 잎사귀마다 코덱이
있는지 본다. 저장할 방법이 없는 값은 직렬화기를 쓰기 전에 여기서 드러난다.

### 15.4 에셋 참조 — id 와 핸들로 나눴다 (`ff6b41f`)

`SpriteRenderer2D::sprite` 가 `AssetHandle` 하나였다. 그것은 `AssetTypes.h` 가 스스로
"이번 실행에서의 위치. 저장하지 않는다" 라고 적어 둔 타입이라, 저장하면 다음 실행에서
뜻이 없는 숫자가 된다. **씬이 어떤 스프라이트를 쓰는지 기억할 방법이 없었다.**

```cpp
JBRO_FIELD(AssetId,     spriteId);                                // 저작·저장
JBRO_FIELD(AssetHandle, sprite,   NoSerialize() | ReadOnly());    // 해석된 값
```

`Transform2D` 의 월드 캐시와 같은 형태다 — 저작 값은 저장하고, 그것에서 계산된 런타임 값은
저장하지 않고 고칠 수도 없다. **새 개념이 늘지 않는다.**

기각한 안: 컴포넌트가 `AssetId` 만 들고 렌더 추출이 매번 해석하는 안. 70 스프라이트 씬에서
조회가 71 → 141 이 된다(§3.5 성능 계약). `SpriteRenderItem` 까지 `AssetId` 로 올리는 안도
같은 조회가 렌더러로 밀릴 뿐이다. 지금 추출은 핸들을 그대로 복사하므로
([SpriteRender2DSystem.cpp:48]) 이 분리로 매 프레임 경로가 바뀌지 않는다.

**핸들을 채우는 것은 아직 없다.** `AssetSystem::Load` 가 `return {};` 인 스텁이라
해석 패스를 붙일 데가 없다. 그것이 생기면 씬 로드 뒤와 인스펙터 변경 시에 한 번씩 돌면 된다.

### 15.5 변이

- 보관함 10개 중 10개. 하나는 테스트 구멍이 아니라 **중복 코드**였다 — `TryAdd` 가 이미
  중복 키를 거절하는데 앞에서 `Find` 로 한 번 더 보고 있었다. 지웠다
- 구조체·enum 은 테스트를 먼저 붙였다
- 빌트인 14개 중 14개 — 한 번에

### 15.6 아직 아닌 것

- **`AssetId` → `AssetHandle` 해석 패스.** `AssetSystem` 이 실제로 로드하게 될 때
- **`ArrayOps` / `TableOps`** — 여전히 선언만. 필드가 컨테이너를 담기 시작할 때
- **`String` 설명자** — 아직 어떤 빌트인 필드도 문자열이 아니다

## 16. 3D 빌트인 컴포넌트 (2026-09-14, `926491b`)

2D 와 같은 작업이다. `Transform3D`(3) · `Camera3D`(1) · `MeshRenderer3D`(4) ·
`Rigidbody3D`(2) · `Collider3D`(1). **3D 쪽은 아직 골격이다**(D-53) — `Camera3D` 는 필드가
하나뿐이고 근평면·투영 방식이 없다. 개수를 테스트에 적어 두었으므로 늘어나면 먼저 운다.

같은 규칙을 그대로 따른다 — `MeshRenderer3D` 의 에셋 참조는 `AssetId`(저장) +
`AssetHandle`(해석된 값, 비저장)로 나뉘고, `Rigidbody3D::velocity` 는 시뮬레이션이 다시 쓰므로
저장하지 않는다.

### 16.1 등록 함수가 헤더에 있다

2D 는 `.cpp` 인데 3D 는 `inline` 이다. `JBroFramework3D` 는 소스가 없어서 `Utility` 로
두었고(`58522de`), 이 함수 하나 때문에 라이브러리로 되돌리는 것은 얻는 것보다 잃는 것이 많다.
inline 함수의 정적 지역 변수는 번역 단위마다 따로 생기지 않으므로 "한 번만 등록" 은 그대로다.

### 16.2 쿼터니언은 성분 넷으로 저장한다

인스펙터가 오일러각을 보여 주는 것은 **표현 문제**라 인스펙터가 정한다.
파일에 오일러각을 적으면 짐벌락과 각도 규약이 파일 형식에 들어온다.

### 16.3 변이 — 10개 중 9개, **남은 하나는 적어 둔다**

`return all;` 을 `return true;` 로 바꿔도 전 스위트가 통과한다. **2D 도 같다**(직접 확인했다).

이유는 테스트 구멍이 아니다. **등록 함수의 반환값을 아무도 쓰지 않는다** —
`Framework2D::Initialize` 도 `Framework3D::Initialize` 도 무시한다. 그리고 등록이 실패하는
유일한 경우는 같은 타입 이름이 두 번 등록되는 것인데, 그것을 테스트에서 만들어 내려면
미리 같은 이름을 넣어 두어야 하고, 결과가 `static` 에 한 번만 계산되므로 그러면 이번에는
**반대 방향(`return false`)이 잡히지 않는다.** 한 프로세스 안에서 두 방향을 동시에 못 박을 수 없다.

정말로 중요한 것 — **모든 빌트인이 이름으로 찾아진다** — 은 다른 테스트가 잡는다
(등록 하나를 빼면 `a builtin component must be findable by its own type name` 이 터진다).
반환값은 그 위의 진단 신호이고, 지금은 소비자가 없다.

**닫으려면 소비자를 만들어야 한다.** 같은 이름의 빌트인 컴포넌트가 둘이면 그것은 프로그래밍
오류이므로, 초기화가 거기서 멈추게 하는 것이 맞다. 그 판단은 `Framework*::Initialize` 의
실패 계약을 바꾸는 일이라 여기서 하지 않았다.

---

## 17. 직렬화 — 저장 쪽 (2026-09-14)

§10 의 3번이다. **읽는 쪽은 아직 없다** — §17.5.

### 17.1 정한 것 둘

`.jproject` 때와 달리 **기존 엔진의 `.jcanvas` 를 읽지 않는다.** 거기 있는 컴포넌트
(`Square2D`, `Circle2D`, `Text2D`, `Light2D`, `AudioPlayer`, `PolygonCollider2D`,
18필드짜리 `Rigidbody2D`)가 이 엔진에 **하나도 없어서** 읽어 봐야 거의 다 버려진다.
확장자·키 이름·들여쓰기·`ParentIndex` 관례만 따르고 새로 적는다. 컴포넌트가 채워진 뒤에
마이그레이션을 따로 본다.

**구조체는 이름 없이 나열한다.** `Position:` 아래 `- 1.5` / `- 2` 다. 좌표와 색은 씬 파일에서
압도적으로 흔해서 `x:`/`y:` 를 붙이면 파일이 세 배가 되고, 기존 엔진도 같은 모양이다.
대가는 **필드 선언 순서가 파일 형식이 된다**는 것이라, 이 표시는 필드가 늘지 않는 타입에만 붙인다
(`TypeDescriptor::writeFieldsAsSequence` — 모양으로 짐작하지 않고 타입이 스스로 말한다).

### 17.2 YAML 부분집합 (`e5cf75f`)

`.jproject` 파서는 재사용할 수 없었다. 그쪽은 키를 아는 채로 구조체에 곧장 담지만,
`.jcanvas` 는 **컴포넌트의 `Type:` 이 그 필드들보다 뒤에 온다** — 무엇인지 알기 전에 값을
이미 지나친 뒤다. 그래서 문서로 한 번 읽어 둔다.

**실제 레거시 씬 다섯 개를 읽는다.** 손으로 옮긴 표본이 아니다. 쓰는 모양은 실제 씬에서
복사한 조각과 **바이트 단위로 같은지** 테스트가 고정한다.

### 17.3 캔버스 저장 (`87d73d6`)

프로퍼티 표를 타고 내려가므로 **저장기가 `Transform2D` 가 무엇인지 모른다.**
기존 엔진과 일부러 다르게 한 것 둘:

- **`Type:` 을 맨 앞에 적는다.** 기존 엔진은 뒤에 적어서 읽는 쪽이 값을 먼저 지나가야 했다
- **`Transform2D` 를 따로 빼지 않는다.** 여기서는 트랜스폼도 그냥 컴포넌트다

작은 캔버스의 **정확한 텍스트**를 테스트에 박아 두었다. 필드를 더하거나 표기를 바꾸면
저장 형식이 조용히 바뀌는 대신 테스트가 먼저 운다.

### 17.4 여기서 잡힌 진짜 결함 (`d191960`)

**컴포넌트 타입 이름이 이름표에 없을 수 있었다.** 컴포넌트는 타입 **id** 만 들고 다니고,
저장은 그 id 로 이름을 되찾아 `Type:` 에 적는다. 그런데 이름을 넣는 곳이
`RegisterBuiltinProperties` 뿐이었다 — **저장이 등록의 부수 효과에 기대고 있었다.**

등록하지 않은 컴포넌트를 저장하려 하면 거절은 하는데 **그 오류가 정작 어느 타입인지 말하지
못했다.** 이름이 없으니까. 이제 `Canvas::AttachComponent` 가 부착할 때 이름을 넣는다.

### 17.5 아직 아닌 것 — 읽는 쪽

읽으려면 **이름으로 컴포넌트를 만드는 길**이 필요한데, 지금은 스크립트에만 있다
(`ScriptRegistry`·`Canvas::AttachScript`). 빌트인 컴포넌트에는 같은 것이 없다 —
`AttachComponent<T>` 는 타입을 컴파일 타임에 알아야 한다.

즉 저장의 반대쪽은 **컴포넌트 팩토리 레지스트리** 하나를 더 필요로 한다.
`ScriptRegistry` 와 같은 모양이면 되고, 프로퍼티 보관함과 같은 이유로 빌트인용·스크립트용이
이미 갈라져 있으므로 그 자리에 붙으면 된다.

### 17.6 변이

- YAML 15/15. 처음 10/15 였고 살아남은 셋이 전부 테스트 구멍이었다 —
  값 안의 콜론, 항목이 아닌 대시, 부분만 읽힌 숫자
- 캔버스 저장 11개 중 10개.

**닫지 못한 하나**: "뿌리에서 닿지 못한 오브젝트가 있으면 거절" 검사를 지워도 아무도 울지 않는다.
`GameObject::SetParent` 가 사이클을 **명시적으로 거절하고**, 모든 오브젝트는 부모 없이 생기므로
언제나 어느 뿌리에서든 닿는다 — 그 조건을 만들 방법이 지금 없다. 검사는 남긴다.
지우면 나중에 그 불변식이 깨졌을 때 씬이 조용히 일부를 잃는다.

