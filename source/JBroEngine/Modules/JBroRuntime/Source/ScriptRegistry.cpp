#include <JBro/Runtime/ScriptRegistry.h>

namespace JBro
{
    namespace
    {
        ScriptRegistry* g_boundScriptRegistry = nullptr;
    }

    ScriptRegistry& ScriptRegistry::Local()
    {
        static ScriptRegistry registry;
        return registry;
    }

    ScriptRegistry& ScriptRegistry::Get()
    {
        return g_boundScriptRegistry != nullptr ? *g_boundScriptRegistry : Local();
    }

    void ScriptRegistry::Bind(ScriptRegistry* registry)
    {
        g_boundScriptRegistry = registry;
    }

    bool ScriptRegistry::Register(const ScriptTypeInfo& info)
    {
        if (info.name == InvalidNameId
            || info.typeId == InvalidComponentTypeId
            || info.size == 0
            || info.alignment == 0
            || info.Construct == nullptr
            || info.Destruct == nullptr)
        {
            return false;
        }
        // Canvas 는 컴포넌트의 타입 id 로 스크립트 풀을 찾는다. 둘이 같은 값이어야
        // 그 조회가 성립한다 — 둘 다 같은 문자열에 건 같은 해시이기 때문이다.
        if (info.name != info.typeId)
        {
            return false;
        }
        if (m_types.Find(info.name) != nullptr)
        {
            return false;
        }
        return m_types.TryAdd(info.name, info);
    }

    void ScriptRegistry::Clear()
    {
        m_types.Clear();
    }

    const ScriptTypeInfo* ScriptRegistry::Find(NameId name) const
    {
        return m_types.Find(name);
    }

    const ScriptTypeInfo* ScriptRegistry::Find(const char* name) const
    {
        return Find(MakeNameId(name));
    }

    std::size_t ScriptRegistry::GetCount() const
    {
        return m_types.Size();
    }
}
