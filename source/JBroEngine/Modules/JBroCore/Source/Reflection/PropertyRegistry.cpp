#include <JBro/Reflection/PropertyRegistry.h>

namespace JBro
{
    namespace
    {
        PropertyRegistry* g_boundScriptProperties = nullptr;
    }

    PropertyRegistry& PropertyRegistry::Builtin()
    {
        // 바인딩이 없다. 빌트인 컴포넌트는 각 모듈이 자기 사본에 등록하고,
        // 읽는 쪽도 같은 사본이다 — 경계를 넘는 것은 스크립트 쪽뿐이다.
        static PropertyRegistry registry;
        return registry;
    }

    PropertyRegistry& PropertyRegistry::ScriptLocal()
    {
        static PropertyRegistry registry;
        return registry;
    }

    PropertyRegistry& PropertyRegistry::Script()
    {
        return g_boundScriptProperties != nullptr ? *g_boundScriptProperties : ScriptLocal();
    }

    void PropertyRegistry::BindScript(PropertyRegistry* registry)
    {
        g_boundScriptProperties = registry;
    }

    bool PropertyRegistry::RegisterBuiltin(NameId typeName, const PropertyTable& table)
    {
        return Builtin().Register(typeName, table);
    }

    bool PropertyRegistry::RegisterScript(NameId typeName, const PropertyTable& table)
    {
        // 빌트인이 가진 이름이면 거절한다. 넣어 두어 봐야 Lookup 이 빌트인 쪽을 주므로,
        // 스크립트 작성자는 자기 필드가 왜 안 보이는지 알 길이 없다.
        if (Builtin().Find(typeName) != nullptr)
        {
            return false;
        }
        return Script().Register(typeName, table);
    }

    const PropertyTable* PropertyRegistry::Lookup(NameId typeName)
    {
        const PropertyTable* found = Builtin().Find(typeName);
        if (found != nullptr)
        {
            return found;
        }
        return Script().Find(typeName);
    }

    const PropertyTable* PropertyRegistry::Lookup(const char* typeName)
    {
        return Lookup(MakeNameId(typeName));
    }

    bool PropertyRegistry::Register(NameId typeName, const PropertyTable& table)
    {
        if (typeName == InvalidNameId)
        {
            return false;
        }
        // 필드가 없는 타입도 등록할 수 있어야 한다 — "물어봤더니 없더라" 와
        // "아직 등록 안 됐다" 는 다른 답이다. 다만 개수와 배열은 서로 맞아야 한다.
        if ((table.count == 0) != (table.properties == nullptr))
        {
            return false;
        }
        // 같은 이름이 이미 있으면 TryAdd 가 거절한다. 앞에서 Find 로 한 번 더 보면
        // 같은 조회를 두 번 하고, 두 판정이 갈라질 수 있는 자리만 생긴다.
        return m_tables.TryAdd(typeName, table);
    }

    void PropertyRegistry::Clear()
    {
        m_tables.Clear();
    }

    const PropertyTable* PropertyRegistry::Find(NameId typeName) const
    {
        return m_tables.Find(typeName);
    }

    const PropertyTable* PropertyRegistry::Find(const char* typeName) const
    {
        return Find(MakeNameId(typeName));
    }

    std::size_t PropertyRegistry::GetCount() const
    {
        return m_tables.Size();
    }
}
