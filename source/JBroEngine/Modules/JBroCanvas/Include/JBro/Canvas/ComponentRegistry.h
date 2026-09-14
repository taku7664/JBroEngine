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
    struct ComponentTypeInfo
    {
        NameId          name = InvalidNameId;
        ComponentTypeId typeId = InvalidComponentTypeId;
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

    private:
        Table<NameId, ComponentTypeInfo> m_types;
    };

    // 타입 하나를 표에 넣는다. 붙이는 함수가 이 자리에서 만들어지므로 타입별 풀 기계는
    // 그대로 쓰인다 — 여기서 따로 메모리를 잡지 않는다.
    template <typename T>
    bool RegisterComponentType()
    {
        static_assert(std::is_base_of_v<ComponentBase, T>,
            "a registered component must derive from ComponentBase");

        ComponentTypeInfo info;
        info.name = NameTable::Get().Intern(T::StaticTypeName());
        info.typeId = MakeStableTypeId(T::StaticTypeName());
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
