#pragma once

#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Runtime/Component.h>
#include <JBro/Runtime/GameScriptBase.h>

#include <cstdint>

namespace JBro
{
    namespace Object
    {
        class GameObject;
    }

    // 오브젝트의 어느 목록에 붙은 것인가(D-271). 컴포넌트와 스크립트는 따로 산다.
    enum class AttachedKind : std::uint8_t
    {
        Component,
        Script,
    };

    // 오브젝트에 붙은 것 하나 - 빌트인 컴포넌트이거나 스크립트다. 짧게 빌리는 포인터다.
    //
    // 런타임은 둘을 따로 든다(D-271). 에디터의 필드 편집·스냅숏·되돌리기·붙이고 떼기는 둘을 **같은 길로** 다룬다 -
    // 리플렉션이 그 타입의 객체 주소 하나로 필드를 읽고 쓰므로, 무엇이든 주소·타입·켜짐만 알면 된다.
    // 한쪽 길만 따로 두면 스크립트에서만 되돌리기가 빠지는 날이 온다.
    struct AttachedRef
    {
        ComponentBase* component = nullptr;
        GameScriptBase* script = nullptr;

        AttachedRef() = default;
        AttachedRef(ComponentBase* attached)
            : component(attached)
        {
        }
        AttachedRef(GameScriptBase* attached)
            : script(attached)
        {
        }

        explicit operator bool() const
        {
            return component != nullptr || script != nullptr;
        }
        AttachedKind GetKind() const
        {
            return script != nullptr ? AttachedKind::Script : AttachedKind::Component;
        }
        // 그 타입의 객체 주소다. 프로퍼티 표의 자리가 이것 기준이다.
        void* GetInstance() const;
        ComponentTypeId GetTypeId() const;
        bool IsEnabled() const;
        void SetEnabled(bool enabled) const;
        bool operator==(const AttachedRef& other) const
        {
            return component == other.component && script == other.script;
        }
    };

    // 컴포넌트나 스크립트를 가리키는 법이다. 둘 다 이것으로 가리킨다 - 어느 목록인지는 `kind` 가 말한다.
    //
    // **포인터로는 안 된다**(D-72 와 같은 이유). 오브젝트를 지웠다 되살리면
    // 컴포넌트도 새로 만들어지고 주소가 달라진다. 그렇다고 오브젝트처럼 번호를
    // 매기지도 않는다 - 되살리는 쪽이 컴포넌트마다 번호를 도로 걸어 주어야 하고,
    // 그러려면 캔버스 파일에 없는 것을 스냅샷에 넣어야 한다.
    //
    // 대신 **같은 타입 중 몇 번째인가**로 가리킨다. 되살리기는 뜬 순서대로 다시
    // 붙이므로 그 순서가 곧 같은 자리다. 캔버스 파일도 같은 방식으로 적는다.
    //
    // 프로퍼티 커맨드도 이것을 쓰므로 컴포넌트 커맨드와 떼어 둔다 - 컴포넌트
    // 커맨드는 스냅샷을, 스냅샷은 프로퍼티 커맨드를 끌어온다.
    struct ComponentAddress
    {
        EditorObjectId objectId = InvalidEditorObjectId;
        ComponentTypeId typeId = 0;
        std::uint32_t ordinal = 0;
        AttachedKind kind = AttachedKind::Component;

        bool Equals(const ComponentAddress& other) const;
    };

    // 오브젝트의 그 목록에서 그 자리의 것을 찾는다. 없으면 비어 있다.
    AttachedRef FindAttachedAt(Object::GameObject& object, AttachedKind kind, ComponentTypeId typeId,
        std::uint32_t ordinal);
    // 같은 목록의 같은 타입 중 몇 번째인지. 그 오브젝트에 없으면 거짓이다.
    bool FindAttachedOrdinal(const Object::GameObject& object, AttachedRef attached, std::uint32_t& ordinal);
    // 번호로 오브젝트를 찾고 그 자리의 것을 찾는다.
    AttachedRef ResolveAttached(const EditorObjectRegistry& registry, const ComponentAddress& address);
    // 붙은 것의 주소를 만든다. 오브젝트에 번호가 없으면 매긴다. 그 오브젝트에 붙어 있지 않으면 거짓이다.
    bool MakeAttachedAddress(EditorObjectRegistry& registry, Object::GameObject& object, AttachedRef attached,
        ComponentAddress& address);
    // 목록 안의 자리(타입을 가리지 않는다). 붙어 있지 않으면 거짓이다.
    bool FindAttachedIndex(const Object::GameObject& object, AttachedRef attached, std::size_t& index);

    // 아래는 **빌트인 컴포넌트만** 다룬다. 기즈모·캔버스 뷰처럼 컴포넌트 타입을 아는 자리가 쓴다.
    // 오브젝트에서 그 자리의 컴포넌트를 찾는다. 없으면 nullptr 이다.
    ComponentBase* FindComponentAt(Object::GameObject& object, ComponentTypeId typeId,
        std::uint32_t ordinal);
    // 컴포넌트가 같은 타입 중 몇 번째인지. 그 오브젝트에 없으면 거짓이다.
    bool FindComponentOrdinal(const Object::GameObject& object, const ComponentBase& component,
        std::uint32_t& ordinal);

    // 번호로 오브젝트를 찾고 그 자리의 컴포넌트를 찾는다. 어느 쪽이든 없거나 스크립트 주소면 nullptr 이다.
    ComponentBase* ResolveComponent(const EditorObjectRegistry& registry,
        const ComponentAddress& address);
    // 오브젝트에 붙은 컴포넌트의 주소를 만든다. 오브젝트에 번호가 없으면 매긴다.
    // 그 오브젝트에 붙어 있지 않으면 거짓이다.
    bool MakeComponentAddress(EditorObjectRegistry& registry, Object::GameObject& object,
        const ComponentBase& component, ComponentAddress& address);
}
