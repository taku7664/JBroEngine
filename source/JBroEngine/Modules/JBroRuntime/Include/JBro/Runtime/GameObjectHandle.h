#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/Ref.h>

#include <type_traits>
#include <JBro/Types/Bool.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class GameScriptBase;

    namespace Object
    {
        class GameObject;
    }

    namespace Internal
    {
        struct GameObjectHandleAccess;
    }
}

// 스크립트가 보는 핸들이다(D-302). 이름은 엔진 타입과 같고, 프렐류드가 이 네임스페이스를 연다.
namespace JBro::Handle
{
    class Layer;

    // 스크립트에 노출하는 GameObject 전용 16B 핸들. 포인터 연산자는 의도적으로 없다.
    // **파괴를 요청한 순간부터 모든 사본이 무효다**(D-301 (6)) - 실제로 지워지는 안전 지점을 기다리지 않는다.
    class GameObject final
    {
    public:
        GameObject() = default;

        Bool IsValid() const;
        explicit operator bool() const;
        // Identity is the stored instance id; validity is checked separately by IsValid().
        Bool operator==(const GameObject& other) const;

        // 이 오브젝트와 자식을 지운다. 순회 중이면 안전 지점에서 지우지만, 핸들은 지금부터 무효이고 찾기에서 빠진다.
        void Destroy();
        void SetActive(Bool active);
        Bool IsActive() const;

        // 이름(D-297, D-301). `[]`·`Find` 는 이것만 본다.
        const char* GetName() const;
        void SetName(const char* name);
        Layer GetLayer() const;

        // 계층(D-301 (4)). 부모가 없으면 빈 핸들이다. `SetParent({})` 는 뿌리로 올린다. 자기 자손 밑으로는 옮기지 않는다.
        GameObject GetParent() const;
        void SetParent(GameObject parent);
        UInt32 GetChildCount() const;
        // 계층 패널의 차례다. 범위를 넘으면 경고 없이 빈 핸들이다.
        GameObject GetChild(UInt32 index) const;
        // 직계 자식 가운데 이름이 같은 첫 것이다. **한 단계만 본다** - `player["Weapon"]["Muzzle"]`.
        // 못 찾으면 경고 없이 빈 핸들이고, 빈 핸들에서 `[]` 를 또 부르면 그때 경고가 남는다. 매 프레임 부르지 않는다.
        GameObject operator[](const char* name) const;
        GameObject operator[](NameId name) const;
        // 깊이를 가리지 않고 찾는다(깊이 우선, 계층 패널의 차례). 자기 자신은 보지 않는다.
        GameObject FindInChildren(const char* name) const;

        // 태그(D-297). 무효 핸들은 로그를 남기고 빈 글자·거짓을 돌려준다.
        const char* GetTag() const;
        void SetTag(const char* tag);
        Bool CompareTag(const char* tag) const;

        // **`T` 는 컴포넌트 핸들이다**(D-302): `GetComponent<Transform2D>()`. 핸들의 타입 이름이 데이터 클래스와 같아 타입 id 로
        // 오브젝트의 컴포넌트 목록을 찾고, 있으면 채운 핸들을, 없으면 빈 핸들을 준다.
        template<typename T>
        T GetComponent() const;

        // 빌트인 컴포넌트를 붙이고 그 핸들을 준다(D-301 (5), D-302 (8)). 못 붙이면 빈 핸들이다.
        template<typename T>
        T AddComponent();

        // 이 오브젝트의 스크립트를 타입으로 찾는다(D-302). 스크립트는 컴포넌트가 아니므로 `GetComponent` 로는 찾지 않는다.
        template<typename T>
        Ref<T> GetScript() const;

        // 스크립트를 붙이고 `Ref<T>` 를 준다. `T` 가 등록되지 않은 타입이면 빈 참조다.
        template<typename T>
        Ref<T> AddScript();

        InstanceId GetInstanceId() const;

    private:
        friend class Object::GameObject;
        friend struct Internal::GameObjectHandleAccess;

        explicit GameObject(const Object::GameObject* object);
        Object::GameObject* Resolve() const;
        // 파괴를 요청한 것도 돌려준다. `Resolve` 가 그것을 거른다.
        Object::GameObject* ResolveLiveOrDestroying() const;
        static const Object::GameObject* FindDescendant(const Object::GameObject& parent, NameId name);
        InstanceRef FindComponentReference(ComponentTypeId typeId) const;
        InstanceRef FindScriptReference(ComponentTypeId typeId) const;
        InstanceRef AddComponentReference(ComponentTypeId typeId);
        InstanceRef AddScriptReference(const char* typeName);
        static void ReportInvalidAccess(const char* operation, InstanceId instanceId);

        mutable InstanceHandle m_cached;
        InstanceId             m_instanceId = InvalidInstanceId;
    };

    static_assert(sizeof(GameObject) == 16,
        "Handle::GameObject must remain a 16-byte script value");
    static_assert(std::is_standard_layout_v<GameObject>,
        "Handle::GameObject must remain standard layout");
    static_assert(std::is_trivially_copyable_v<GameObject>,
        "Handle::GameObject must remain trivially copyable");

    template<typename T>
    Ref<T> GameObject::GetScript() const
    {
        static constexpr ComponentTypeId TypeId = MakeStableTypeId(T::StaticTypeName());
        const InstanceRef found = FindScriptReference(TypeId);
        Ref<T> result;
        result.ObjectId = found.ObjectId;
        result.ComponentId = found.ComponentId;
        result.Cached = found.Cached;
        return result;
    }

    template<typename T>
    T GameObject::GetComponent() const
    {
        static_assert(std::is_base_of_v<Internal::ComponentHandle, T>,
            "GetComponent takes a component handle such as Transform2D; a script is found with GetScript");
        static constexpr ComponentTypeId TypeId = MakeStableTypeId(T::StaticTypeName());
        return Internal::ComponentHandleAccess::FromReference<T>(FindComponentReference(TypeId));
    }

    template<typename T>
    T GameObject::AddComponent()
    {
        static_assert(std::is_base_of_v<Internal::ComponentHandle, T>,
            "AddComponent takes a component handle such as Transform2D; a script is added with AddScript");
        static constexpr ComponentTypeId TypeId = MakeStableTypeId(T::StaticTypeName());
        return Internal::ComponentHandleAccess::FromReference<T>(AddComponentReference(TypeId));
    }

    template<typename T>
    Ref<T> GameObject::AddScript()
    {
        static_assert(std::is_base_of_v<GameScriptBase, T>, "AddScript takes a script type");
        const InstanceRef added = AddScriptReference(T::StaticTypeName());
        Ref<T> result;
        result.ObjectId = added.ObjectId;
        result.ComponentId = added.ComponentId;
        result.Cached = added.Cached;
        return result;
    }
}

// `GetLayer` 가 돌려주는 타입이다. 위의 핸들이 완전해진 뒤에 넣는다(둘이 서로를 돌려준다).
#include <JBro/Runtime/LayerHandle.h>
