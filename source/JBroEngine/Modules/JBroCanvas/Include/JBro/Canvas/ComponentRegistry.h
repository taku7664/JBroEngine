#pragma once

#include <JBro/Canvas/Canvas.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/Table.h>

#include <cstddef>

namespace JBro
{
    // 빌트인 컴포넌트를 **이름으로** 붙이는 표다.
    //
    // 스크립트에는 이미 같은 길이 있다(`ScriptRegistry`·`Canvas::AttachScript`). 빌트인에는
    // 없었다 — `AttachComponent<T>` 는 타입을 컴파일 타임에 알아야 하고, 씬 파일을 읽는 쪽은
    // 글자 하나만 들고 있다.
    //
    // `ScriptRegistry` 와 다른 점 하나: **DLL 이 아니라 엔진이 채운다.** 그래서 Local/Bind 가
    // 없고 비우지도 않는다. 프로퍼티 보관함이 빌트인용과 스크립트용으로 갈린 것과 같은 이유다.
    //
    // 저장은 하지 않는다. 컴포넌트 메모리는 `Canvas` 의 타입별 풀이 계속 소유하며,
    // 이 표는 "그 풀에 하나 만들어라" 를 이름으로 부를 수 있게 할 뿐이다.
    // 한 오브젝트에 몇 개까지 붙는가(기존 엔진 `EComponentMultiplicity`, D-180).
    //
    // 기본이 `Multiple` 인 이유는 **막는 쪽이 정보를 가진 쪽이기 때문이다.** 콜라이더를
    // 여럿 붙이는 것은 흔한 일이고, 하나만 있어야 하는 타입은 그 타입을 등록하는 자리가
    // 그 사실을 안다. 반대로 기본을 `Single` 로 두면 등록을 빠뜨린 타입이 조용히 하나로
    // 묶여, 왜 두 번째가 안 붙는지 알 길이 없다.
    enum class ComponentMultiplicity : std::uint8_t
    {
        Single,
        Multiple
    };

    // 추가 목록에서 묶이는 갈래다. 기존 엔진의 `Type.Category` 와 같은 값을 쓴다 -
    // 로컬라이징 키 `component_category.<이름>` 으로 번역된다.
    namespace ComponentCategory
    {
        inline constexpr const char* Transform = "Transform";
        inline constexpr const char* Rendering = "Rendering";
        inline constexpr const char* Physics = "Physics";
        // 갈래를 대지 않은 타입이 묶이는 자리다.
        inline constexpr const char* Default = "Components";
    }

    struct ComponentTypeInfo
    {
        NameId          name = InvalidNameId;
        ComponentTypeId typeId = InvalidComponentTypeId;
        // 목록에서 묶이는 갈래다. 비어 있으면 `ComponentCategory::Default` 로 본다.
        const char* category = nullptr;
        ComponentMultiplicity multiplicity = ComponentMultiplicity::Multiple;
        // 오브젝트에 하나 붙이고 그것을 돌려준다. 실패하면 nullptr 이다.
        ComponentBase* (*Attach)(Canvas& canvas, GameObject* owner) = nullptr;
        // 붙인 것을 뗀다. **붙이는 함수와 짝으로 여기 둔다** - 풀이 메모리를
        // 돌려받으려면 정적 타입이 필요하고, 그것을 아는 자리가 여기뿐이다.
        // `GameObject::DetachComponent` 만 부르면 슬롯만 빠지고 풀 자리는 남는다.
        bool (*Detach)(Canvas& canvas, GameObject* owner, ComponentBase* component) = nullptr;
    };

    class ComponentRegistry final
    {
    public:
        ComponentRegistry() = default;
        ComponentRegistry(const ComponentRegistry&) = delete;
        ComponentRegistry& operator=(const ComponentRegistry&) = delete;

        static ComponentRegistry& Get();

        // 같은 이름이 이미 있으면 거절한다. 조용히 덮으면 어느 타입이 붙는지 알 수 없다.
        bool Register(const ComponentTypeInfo& info);

        const ComponentTypeInfo* Find(NameId name) const;
        const ComponentTypeInfo* Find(const char* name) const;
        std::size_t GetCount() const;

        // 등록된 타입 전부를 **이름 순으로** 늘어놓는다. 인스펙터의 "붙이기"
        // 목록이 이것으로 선다 - 표에 있는 것이 곧 붙일 수 있는 것이라,
        // 타입을 더해도 목록을 고칠 일이 없다.
        //
        // 이름 순인 이유는 표가 해시 순이기 때문이다. 그대로 내보내면 목록이
        // 실행할 때마다 다른 차례로 나와 눈이 자리를 못 외운다.
        Array<const ComponentTypeInfo*> CollectTypes() const;

        // 이 오브젝트에 이 타입을 **하나 더** 붙일 수 있는가(D-180).
        //
        // 기존 엔진의 `CReflectionRegistry::CanAddComponent` 와 같은 판정이다. 인스펙터가
        // 이것을 묻지 않으면 Transform2D 가 한 오브젝트에 둘씩 붙는데, 그 뒤로는 조회가
        // 먼저 붙은 쪽만 돌려주므로 사용자가 고친 값이 화면에 반영되지 않는다.
        //
        // 등록되지 않은 이름은 거짓이다. 붙일 방법이 없는 것을 붙일 수 있다고 말하지 않는다.
        bool CanAttach(const GameObject& object, NameId name) const;

    private:
        Table<NameId, ComponentTypeInfo> m_types;
    };

    // 타입 하나를 표에 넣는다. 붙이는 함수가 이 자리에서 만들어지므로 타입별 풀 기계는
    // 그대로 쓰인다 — 여기서 따로 메모리를 잡지 않는다.
    //
    // 갈래와 다중성은 **등록하는 자리가 댄다**(D-180). 타입 자신의 정적 멤버로 두지 않는
    // 이유는, 그 둘이 타입의 성질이 아니라 편집기의 규칙이기 때문이다 - 프레임워크 타입이
    // 에디터를 위해 자기 헤더에 갈래 이름을 적을 까닭이 없다.
    template <typename T>
    bool RegisterComponentType(const char* category = nullptr,
        ComponentMultiplicity multiplicity = ComponentMultiplicity::Multiple)
    {
        static_assert(std::is_base_of_v<ComponentBase, T>,
            "a registered component must derive from ComponentBase");

        ComponentTypeInfo info;
        info.name = NameTable::Get().Intern(T::StaticTypeName());
        info.typeId = MakeStableTypeId(T::StaticTypeName());
        info.category = category != nullptr ? category : ComponentCategory::Default;
        info.multiplicity = multiplicity;
        info.Attach = [](Canvas& canvas, GameObject* owner) -> ComponentBase*
        {
            return canvas.AttachComponent<T>(owner);
        };
        info.Detach = [](Canvas& canvas, GameObject* owner, ComponentBase* component) -> bool
        {
            // **타입이 맞는지 여기서 본다.** 아래 내림 변환은 맞을 때만 옳고,
            // 부르는 쪽이 표를 잘못 찾아왔는지 여기 말고는 알 자리가 없다.
            if (component == nullptr
                || component->GetTypeId() != MakeStableTypeId(T::StaticTypeName()))
            {
                return false;
            }
            return canvas.DetachComponent<T>(owner, static_cast<T*>(component));
        };
        return ComponentRegistry::Get().Register(info);
    }
}
