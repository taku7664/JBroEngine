#pragma once

#include <JBro/Editor/EditorObjectRegistry.h>
#include <JBro/Runtime/Component.h>

#include <cstdint>

namespace JBro
{
    class GameObject;

    // 컴포넌트를 가리키는 법이다.
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

        bool Equals(const ComponentAddress& other) const;
    };

    // 오브젝트에서 그 자리의 컴포넌트를 찾는다. 없으면 nullptr 이다.
    ComponentBase* FindComponentAt(GameObject& object, ComponentTypeId typeId,
        std::uint32_t ordinal);
    // 컴포넌트가 같은 타입 중 몇 번째인지. 그 오브젝트에 없으면 거짓이다.
    bool FindComponentOrdinal(const GameObject& object, const ComponentBase& component,
        std::uint32_t& ordinal);

    // 번호로 오브젝트를 찾고 그 자리의 컴포넌트를 찾는다. 어느 쪽이든 없으면 nullptr 이다.
    ComponentBase* ResolveComponent(const EditorObjectRegistry& registry,
        const ComponentAddress& address);
    // 오브젝트에 붙은 컴포넌트의 주소를 만든다. 오브젝트에 번호가 없으면 매긴다.
    // 그 오브젝트에 붙어 있지 않으면 거짓이다.
    bool MakeComponentAddress(EditorObjectRegistry& registry, GameObject& object,
        const ComponentBase& component, ComponentAddress& address);
}
