# 7. 리플렉션과 직렬화

인스펙터가 컴포넌트의 필드를 그리고, `.jcanvas` 가 그 값을 저장하려면 누군가 "이 타입에 이런 필드가 있고 메모리 어디에 있다" 를 알려 줘야 한다.
그 쪽지가 리플렉션이다. 이 엔진의 리플렉션은 **형식은 하나, 생산자는 둘**이다(D-56).

- 형식: `TypeDescriptor`(타입마다 한 장) + `PropertyInfo`(필드마다 한 장)
- 생산자 1: C++ 매크로 `JBRO_FIELD`. 빌트인 컴포넌트와 지금의 C++ 스크립트가 쓴다
- 생산자 2: `jbroc` 이 생성하는 코드 **[계획]**. JBroScript 가 쓴다

## 기존 엔진에서 무엇이 문제였나

새 설계는 기존 엔진의 `JPROP` 를 실측하고 나서 정했다. 요점만 적는다(자세한 것은 `tasks/jbroscript-plan.md` §2·§8.2).

- 파서가 **정규식**이었다. `Array<Ref<T>>` 처럼 꺾쇠가 겹치면 누락되고, 주석 안의 `JPROP` 은 매치됐다. 누락은 경고로만 남아 빌드는 성공하고 그 필드만 인스펙터에서 사라졌다.
- 필드의 진실이 소스가 아니라 **에디터가 만든 생성 파일**에 있었다. VS 에서 필드를 추가하고 빌드해도 에디터를 돌리기 전에는 존재하지 않았다.
- 프로퍼티 명세에 주소 찾는 법이 둘(`Offset` 과 `GetFieldPtr`)이었고 어느 쪽이 유효한지 구조가 말해 주지 않았다.
- 타입 집합이 18값 enum 으로 닫혀 있어 새 타입을 못 넣고 곁가지 필드를 달았다. 그 enum 의 `switch` 가 직렬화기 한 파일에만 여섯 벌 있었다.
- 타입의 성질(enum 이름표, Ref 대상)이 프로퍼티에 붙어 있어 17칸 중 5칸이 대부분 빈칸이었다.

C++26 리플렉션은 답이 아니었다. 이 툴체인(MSVC 14.51)에 없고, 있어도 전부 `consteval` 이라 런타임 표는 결국 생성해야 한다. `PropertyInfo` 설계는 그대로 필요하다.

## 필드를 선언하는 법: JBRO_FIELD

```cpp
class Player final : public GameScript2D
{
public:
    static constexpr const char* StaticTypeName() { return "Player"; }
    ComponentTypeId GetTypeId() const override { return MakeStableTypeId(StaticTypeName()); }

    JBRO_REFLECT_BODY(Player)                                   // 이 줄 다음부터 private 이다

    JBRO_FIELD(int,   FieldRows, Range(4, 40) | Category("Field")) = 20;
    JBRO_FIELD(float, Elapsed,   NoSerialize()) = 0.0f;
    JBRO_FIELD(float, Speed) = 1.0f;
    JBRO_FIELD(Vec2,  Anchor) { 0.5f, 0.5f };
};
```

- `JBRO_REFLECT_BODY(Type)` 은 클래스 본문을 연다. 여기서 매긴 `__COUNTER__` 값이 아래 필드들의 기준점이다.
- `JBRO_FIELD(타입, 이름, 어트리뷰트...)` 는 **선언과 등록을 한 토큰에 묶는다.** 기본값은 매크로 밖에 쓴다.
- 이 매크로가 선언한 필드는 **public** 이다. 등록 함수가 밖에서 불려야 하고, 인스펙터에 나오는 값이 클래스 밖에서 안 보이는 것도 앞뒤가 맞지 않는다.
- END 매크로는 없다. 필드 수는 `__COUNTER__` 차이로 스스로 센다. 사이에 다른 `__COUNTER__` 사용이 끼면 **시끄럽게 컴파일 에러**가 난다.
- 필드 이름은 손으로 적지 않는다. 멤버 포인터를 템플릿 인자로 받으면 컴파일러 서명 문자열에 이름이 들어 있고 거기서 잘라 쓴다.
  그래서 "이름을 바꿨는데 등록된 이름은 옛것인" 경로가 없다. 컴파일러 서명 형식에 기대므로 테스트가 실제 이름 하나를 `static_assert` 로 붙잡아 둔다.
  clang 은 서명 모양이 달라 지금은 MSVC 에서만 된다(ide-plan §4.1 실측).
- 모르는 타입을 필드로 쓰면 컴파일 에러다. `TypeDescriptorOf<T>` 특수화가 없기 때문이다.

### 어트리뷰트

전부 `constexpr` 값이고 `|` 로 겹친다. **오타는 컴파일 에러다.** 기존 엔진은 모르는 어트리뷰트를 경고로 넘겨서 오타 난 `Range` 하나가 조용히 사라졌다.

| 어트리뷰트 | 뜻 |
|---|---|
| `Name("표시 이름")` | 인스펙터 라벨. 없으면 필드 이름 |
| `Tooltip("설명")` | 인스펙터 툴팁 |
| `Category("묶음")` | 인스펙터에서 묶어 보이는 이름 |
| `Range(min, max)` | 슬라이더 범위 |
| `ReadOnly()` | 인스펙터에서 편집 불가. 시스템이 채우는 캐시에 쓴다 |
| `NoSerialize()` | 인스펙터에는 나오되 저장하지 않는다. 속도처럼 매 프레임 다시 쓰이는 값에 쓴다 |

같은 어트리뷰트를 두 번 쓰면 오른쪽이 이긴다. `ReadOnly`·`NoSerialize` 는 끄는 쪽만 있어 한 번이라도 썼으면 꺼진다.
어트리뷰트 이름은 `JBRO_FIELD` 안에서만 수식 없이 보인다. `JBro` 직속에 `Name`·`Range` 같은 흔한 이름을 두면 프렐류드의 `using namespace JBro;` 뒤에 사용자 코드와 충돌한다.

편집 메타(표시 이름·툴팁·카테고리·범위·읽기 전용)가 하나도 없으면 `PropertyInfo::edit` 가 `nullptr` 이 되어 게임 빌드에서 그 문자열이 통째로 빠진다.

### enum

```cpp
namespace JBro::Component { enum class SpriteFlip : std::uint8_t { None, Horizontal, Vertical, Both }; }

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::SpriteFlip, "Component::SpriteFlip",
        { Component::SpriteFlip::None,       "None" },
        { Component::SpriteFlip::Horizontal, "Horizontal" },
        { Component::SpriteFlip::Vertical,   "Vertical" },
        { Component::SpriteFlip::Both,       "Both" });
}
```

저장 파일에는 숫자가 아니라 **이름**이 적힌다. 숫자로 적으면 나중에 값을 가운데 끼워 넣는 순간 예전 파일이 전부 한 칸씩 밀린다.

### 구조체와 벡터

필드가 늘어나지 않는 값 타입(`Vec2`, `Color`, `Matrix3x2`)은 클래스 본문을 건드리지 않고 `TypeDescriptorOf<T>` 특수화로 표를 엮는다.
`Math2D.h` 가 매 프레임 경로에 있어서 거기에 리플렉션 기계를 넣으면 벡터를 쓰는 모든 번역 단위가 함께 물고 가기 때문이다. 표는 `Math2DReflection.h` 에 따로 있다.

```cpp
template <> struct TypeDescriptorOf<Vec2>
{
    static const TypeDescriptor& Get()
    {
        static const FieldEntry entries[] = { MakeFieldEntry<&Vec2::x>(), MakeFieldEntry<&Vec2::y>() };
        static const StaticPropertyTable<2> fields { entries };
        static const TypeDescriptor descriptor = MakeVectorTypeDescriptor<Vec2>("JBro.Vec2", fields.Get());
        return descriptor;
    }
};
```

- `MakeStructTypeDescriptor<T>` : 필드를 가진 타입. 코덱을 주지 않는다. 저장도 인스펙터도 필드를 타고 내려가 잎사귀에서 코덱을 만난다.
- `MakeVectorTypeDescriptor<T>` : 같은 종류 값을 늘어놓은 구조체. 저장 파일에 **이름 없이** 나열되고(`- 1.5` / `- 2`), 인스펙터에 **한 줄**로 그려진다.
  선언 순서가 곧 파일 형식이 되므로 **필드가 늘어나는 타입에는 쓰지 않는다.**

이 방식은 표가 선언과 떨어져 있어 필드를 더하고 표를 안 고치면 조용히 빠진다. 그래서 필드가 늘어나는 타입에는 `JBRO_FIELD` 를 쓴다.

## 표의 모양

### TypeDescriptor: 타입마다 한 장

```cpp
struct TypeDescriptor
{
    NameId        typeName;                // "float", "JBro.Vec2", "Component::SpriteFlip"
    std::uint32_t size, alignment;
    bool          triviallyCopyable;       // 참고용. 복사는 언제나 codec->Assign
    bool          writeFieldsAsSequence;   // 벡터형: 이름 없이 나열

    const ArrayOps* arrayOps;              // != nullptr 이면 배열. element 유효
    const TableOps* tableOps;              // != nullptr 이면 표. key/value 유효
    const TypeDescriptor* element;
    const TypeDescriptor* key;
    const TypeDescriptor* value;

    const EnumNames*     enumNames;        // enum 일 때만
    const RefTarget*     refTarget;        // Ref<T> 일 때만
    const PropertyTable* fields;           // 구조체일 때만. 자기 필드로 말한다
    const ValueCodec*    codec;            // 잎사귀 값일 때만
};
```

**닫힌 타입 enum 이 없다.** "배열인가?" 는 `arrayOps` 의 존재가 답하고, "무슨 타입인가?" 는 `typeName` 이 답한다. 새 타입을 더해도 코어를 건드리지 않는다.

### PropertyInfo: 필드마다 한 장

```cpp
struct PropertyInfo
{
    NameId name;
    const TypeDescriptor* type;
    void*       (*Address)(void* owner) noexcept;         // 소유 객체 주소 → 필드 주소
    const void* (*ConstAddress)(const void* owner) noexcept;
    bool serialize;                                       // false = 인스펙터엔 나오되 저장 안 함
    const PropertyEditInfo* edit;                         // 게임 빌드에서는 nullptr 이어도 된다
};

struct PropertyEditInfo { const char* displayName; const char* tooltip; const char* category; bool hasRange; float rangeMin, rangeMax; bool editable; };
struct PropertyTable    { const PropertyInfo* properties; std::uint32_t count; };
```

접근 방법이 **하나**다. 오프셋이 아니라 접근자 함수라서 private 멤버도, 가상 함수를 가진 파생 클래스도, 생성 코드도 같은 방식이다.
타입·크기는 여기 적지 않는다. `type` 이 가리키는 곳에만 있다. 같은 사실을 두 군데 적지 않는다.

이 구조체들은 호스트와 게임 DLL 사이를 넘는다. 크기가 `static_assert` 로 고정돼 있다(`PropertyEditInfo` 40B 등).

### ValueCodec: 잎사귀 값 하나를 글자로

```cpp
struct ValueCodec
{
    bool (*ToText)(const void* value, char* buffer, std::size_t capacity, std::size_t& required) noexcept;
    bool (*FromText)(void* value, const char* text, std::size_t length) noexcept;   // 못 읽으면 value 를 건드리지 않는다
    bool (*Equals)(const void* left, const void* right) noexcept;                 // "기본값으로 되돌리기", undo 의 변경 판정
    void (*Assign)(void* destination, const void* source) noexcept;               // memcpy 로 대신하지 않는다
};
```

코덱은 **컨테이너를 모르고 YAML 도 모른다.** 알맹이만 내놓고 따옴표·들여쓰기는 직렬화기가 처리한다.
YAML 을 아는 곳이 여러 군데면 그런 것이 여기저기서 터진다. 기존 엔진의 `.jproject` 파서가 여러 줄 스칼라에서 두 번 죽었다.

`Assign` 이 따로 있는 이유는 `String` 처럼 내용이 밖에 있는 타입을 `memcpy` 하면 얕은 복사가 되어 먼저 죽는 쪽이 남은 쪽을 망가뜨리기 때문이다.
기존 엔진의 undo 가 그렇게 되돌렸고 스스로 위험으로 적어 두었다.

### ArrayOps / TableOps: 타입을 모르고 컨테이너를 만지는 법

```cpp
struct ArrayOps
{
    std::size_t (*GetSize)(const void* array);
    void*       (*GetElement)(void* array, std::size_t index);
    const void* (*GetConstElement)(const void* array, std::size_t index);
    bool        (*AddDefault)(void* array);
    bool        (*RemoveAt)(void* array, std::size_t index);
    bool        (*Move)(void* array, std::size_t from, std::size_t to);   // to 는 뺀 뒤의 번호
    void        (*Clear)(void* array);
};

struct TableOps
{
    std::size_t (*GetSize)(const void* table);
    std::size_t (*BeginSlot)(const void* table);                      // 커서 순회. 삽입·삭제하면 커서 무효
    std::size_t (*NextSlot)(const void* table, std::size_t slot);
    const void* (*GetKeyAt)(const void* table, std::size_t slot);
    void*       (*GetValueAt)(void* table, std::size_t slot);
    bool  (*ContainsKey)(const void* table, const void* key);          // 수정은 키로 지목한다
    bool  (*InsertDefault)(void* table, const void* key);
    bool  (*RemoveKey)(void* table, const void* key);
    void* (*FindValue)(void* table, const void* key);
    bool (*ConstructKey)(void* storage);   void (*DestructKey)(void* key);      // 부르는 쪽이 준비한 자리에 기본 생성
    bool (*ConstructValue)(void* storage); void (*DestructValue)(void* value);
    void  (*Clear)(void* table);
};
```

기존 엔진의 `ReflectArrayOps`/`ReflectTableOps` 를 그대로 가져왔다. 거기 값비싸게 적힌 교훈 둘이 있다.
**표는 슬롯 번호로 원소를 지목하면 안 된다**(open addressing 이라 리해시 한 번에 번호가 전부 바뀐다). **키·값을 `memcpy` 로 복사할 수 없다.**

기존 엔진이 못 닫은 것 하나가 여기서 닫힌다. 스크립트 필드에 owning `Array`/`Table` 을 둘 수 있다. 모든 연산이 DLL 이 제공한 함수 포인터를 거치므로 할당도 해제도 DLL 안에서 일어난다.

## 보관함은 둘이다

빌트인 컴포넌트와 사용자 스크립트의 표를 **한 표에 섞지 않는다**(D-56, 계획서 §8.4). 수명이 다르기 때문이다.

| 보관함 | 수명 | 채우는 쪽 | 접근 |
|---|---|---|---|
| 빌트인 표 | 엔진 | `RegisterBuiltinComponentProperties2D()` / `3D()` 가 `JBRO_FIELD` 표를 이름으로 등록 | `PropertyRegistry::Builtin()` |
| 스크립트 표 | 로드된 DLL | DLL 이 `RegisterScriptProperties<T>()` 로 등록. 모듈이 내려가면 `Clear()` | `PropertyRegistry::Script()` (호스트 것에 바인딩) |

`PropertyRegistry::Lookup(typeName)` 이 두 군데를 찾아본다. 빌트인이 먼저다. 스크립트가 엔진 타입 이름을 덮지 못하게 하기 위해서다.

```cpp
class PropertyRegistry final
{
public:
    static PropertyRegistry& Builtin();
    static PropertyRegistry& Script();              // 바인딩된 것, 없으면 ScriptLocal()
    static void BindScript(PropertyRegistry* registry);
    static bool RegisterBuiltin(NameId typeName, const PropertyTable& table);
    static bool RegisterScript (NameId typeName, const PropertyTable& table);
    static const PropertyTable* Lookup(NameId typeName);
    static const PropertyTable* Lookup(const char* typeName);
};
template<typename T> bool RegisterBuiltinProperties();
template<typename T> bool RegisterScriptProperties();
```

주의 하나. 프로퍼티 표를 **처음 물어보기 전에 `NameTable` 이 바인딩돼 있어야 한다.** DLL 이 호스트의 이름표를 받기 전에 표를 만들면 이름이 DLL 쪽 사본에 들어가고 호스트가 되찾지 못한다.

## 저장: .jcanvas

- 형식은 YAML 부분집합이다. 읽기·쓰기 모두 `JBro/Core/Yaml.h` 의 파서 하나를 쓴다.
- 값을 쓰고 읽는 걸음은 `ReflectedYaml` 하나다. 캔버스 파일·되돌리기 스냅샷·인스펙터 글자 편집이 전부 같은 함수를 부른다(D-86).
  따로 걸어 내려가면 한쪽만 고쳐진다. 실제로 캔버스 파일이 컨테이너를 거절하는 동안 스냅샷은 컨테이너를 조용히 건너뛰고 있었다.
- **기본값과 같은 프로퍼티도 전부 쓴다**(계획서 §8.6). 빼면 파일은 작아지지만 나중에 기본값을 바꾸면 기존 씬이 조용히 같이 바뀐다.
- 구조체는 `writeFieldsAsSequence` 면 이름 없이 나열하고, 아니면 필드 이름을 붙인다. enum 은 이름으로 적는다.
- 계층은 평평한 목록 + 부모 인덱스로 적는다. 앞에서부터 만들면 부모가 늘 먼저 있다.
- 읽기는 **모르는 필드·타입을 조용히 넘기지 않고 어디서 멈췄는지 말한다.** 줄 번호가 붙은 `ReflectedYamlError` 를 돌려준다.
- 기존 엔진의 `.jcanvas` 는 읽지 않는다. 거기 있는 컴포넌트가 이 엔진에 하나도 없어서 읽어 봐야 거의 다 버려진다. `.jproject` 는 기존 것을 그대로 읽는다.

```cpp
// JBro/Reflection/ReflectedYaml.h
struct ReflectedYamlError { String message; String fieldName; };

bool ReflectedValueToText(const ValueCodec& codec, const void* value, String& text);
bool WriteReflectedValue(YamlWriter& writer, const char* key, const TypeDescriptor& type, const void* value, ReflectedYamlError& error);
bool ReadReflectedValue (const YamlDocument& document, std::uint32_t node, const TypeDescriptor& type, void* value, ReflectedYamlError& error);
bool ReadReflectedFields(const YamlDocument& document, std::uint32_t node, const PropertyTable& table, void* value,
                         const char* const* skip, std::size_t skipCount, ReflectedYamlError& error);

// JBro/Canvas/CanvasFile.h  (Tier E)
struct CanvasFileError { String message; String objectName; String typeName; String fieldName; };

bool WriteCanvasText(Canvas& canvas, String& text, CanvasFileError& error);
bool SaveCanvasFile (Canvas& canvas, const char* path, CanvasFileError& error);
bool ReadCanvasText (Canvas& canvas, const char* text, std::size_t length, CanvasFileError& error);   // 빈 캔버스에만 읽는다
bool LoadCanvasFile (Canvas& canvas, const char* path, CanvasFileError& error);
```

기존 엔진 파일과 일부러 다르게 한 것이 둘 있다. `Type:` 을 컴포넌트의 맨 앞에 적고(읽는 쪽이 타입을 먼저 안다), `Transform2D` 를 `Components` 밖으로 빼지 않는다(예외가 하나 줄면 규칙도 하나 준다).
파일에 있는데 코드에 없는 필드는 실패이고, 코드에 있는데 파일에 없는 필드는 기본값으로 둔다. 필드를 더한 것은 예전 씬을 못 읽을 이유가 아니지만, 지운 것은 값을 버린다는 뜻이라 사람이 알아야 한다.

에셋 참조는 `AssetId`(저장)와 `AssetHandle`(런타임, 저장 안 함)로 나뉜다. 핸들을 채우는 해석 패스는 `AssetSystem::Load` 가 스텁이라 **아직 없다.**

## 어디에 시험이 있나

`ReflectionFieldTests`(매크로와 이름 추출), `ReflectionShapeTests`(표 크기와 POD), `ReflectionCompoundTests`(구조체 안의 구조체·목록),
`ReflectionContainerTests`(`ArrayOps`/`TableOps`), `PropertyRegistryTests`(보관함 둘), `BuiltinComponentPropertyTests`(빌트인 열 개),
`YamlTests`·`ReflectedYamlTests`·`CanvasFileTests`(저장→로드→저장 동일성, 기존 엔진 `.jcanvas` 다섯 개 파싱).
