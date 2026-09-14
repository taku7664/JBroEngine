#pragma once

#include <JBro/Canvas/Canvas.h>
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
        return ComponentRegistry::Get().Register(info);
    }
}
