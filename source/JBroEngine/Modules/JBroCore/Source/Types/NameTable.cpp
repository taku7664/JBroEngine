#include <JBro/Types/NameTable.h>

#include <cstdio>

namespace JBro
{
    namespace
    {
        NameTable* g_boundNameTable = nullptr;
    }

    NameTable& NameTable::Local()
    {
        static NameTable table;
        return table;
    }

    NameTable& NameTable::Get()
    {
        return g_boundNameTable != nullptr ? *g_boundNameTable : Local();
    }

    void NameTable::Bind(NameTable* table)
    {
        g_boundNameTable = table;
    }

    NameId NameTable::Intern(const char* text)
    {
        if (text == nullptr || text[0] == '\0')
        {
            return InvalidNameId;
        }

        const NameId id = MakeNameId(text);
        if (const String* existing = m_texts.Find(id))
        {
            // FNV-1a 64 라 실제로 겹칠 일은 없다시피 하지만, 겹치면 한쪽 이름이 남의
            // 원문을 되찾게 된다. 조용히 덮지 않고 첫 원문을 지키며 사실을 남긴다.
            if (*existing != text)
            {
                ++m_collisionCount;
                std::fprintf(
                    stderr,
                    "JBro warning: name id collision between \"%s\" and \"%s\".\n",
                    existing->c_str(),
                    text);
            }
            return id;
        }

        m_texts.TryAdd(id, String(text));
        return id;
    }

    const char* NameTable::Resolve(NameId id) const
    {
        const String* text = m_texts.Find(id);
        return text != nullptr ? text->c_str() : "";
    }

    std::size_t NameTable::GetCount() const
    {
        return m_texts.Size();
    }

    std::size_t NameTable::GetCollisionCount() const
    {
        return m_collisionCount;
    }

    void NameTable::Clear()
    {
        m_texts.Clear();
        m_collisionCount = 0;
    }
}
