#include <JBro/Editor/ComponentMenuTable.h>

#include <JBro/Core/Log.h>

#include <imgui.h>

namespace JBro
{
    namespace
    {
        // 그리는 동안 표가 바뀌지 않게 센다. 훅 안에서 다시 메뉴를 그리는 일도 있으니 깊이로 센다.
        class DrawScope
        {
        public:
            explicit DrawScope(std::uint32_t& depth)
                : m_depth(depth)
            {
                ++m_depth;
            }
            ~DrawScope()
            {
                --m_depth;
            }
            DrawScope(const DrawScope&) = delete;
            DrawScope& operator=(const DrawScope&) = delete;

        private:
            std::uint32_t& m_depth;
        };
    }

    bool ComponentMenuTable::Register(ComponentTypeId typeId, ComponentMenuDraw draw,
        ComponentMenuOwner owner, void* user)
    {
        if (typeId == InvalidComponentTypeId || draw == nullptr || owner == nullptr)
        {
            Log::Write(LogLevel::Error, "editor", "component menu: refused an entry without a type, function or owner");
            return false;
        }
        if (m_drawDepth > 0)
        {
            Log::Write(LogLevel::Error, "editor", "component menu: cannot register while a menu is being drawn");
            return false;
        }
        for (const Entry& entry : m_entries)
        {
            if (entry.typeId == typeId && entry.draw == draw && entry.owner == owner)
            {
                // 같은 것을 두 번 걸면 메뉴에 같은 항목이 두 줄 선다.
                Log::Write(LogLevel::Warning, "editor", "component menu: the same entry is already registered");
                return false;
            }
        }
        Entry entry;
        entry.typeId = typeId;
        entry.draw = draw;
        entry.owner = owner;
        entry.user = user;
        m_entries.Add(entry);
        return true;
    }

    std::uint32_t ComponentMenuTable::Unregister(ComponentMenuOwner owner)
    {
        if (m_drawDepth > 0)
        {
            Log::Write(LogLevel::Error, "editor", "component menu: cannot unregister while a menu is being drawn");
            return 0;
        }
        // 순서를 지키며 뗀다. 남은 항목의 등록 순서가 메뉴의 순서다.
        return static_cast<std::uint32_t>(m_entries.RemoveAll(
            [owner](const Entry& entry)
            {
                return entry.owner == owner;
            }));
    }

    bool ComponentMenuTable::Has(ComponentTypeId typeId) const
    {
        for (const Entry& entry : m_entries)
        {
            if (entry.typeId == typeId)
            {
                return true;
            }
        }
        return false;
    }

    std::uint32_t ComponentMenuTable::Count(ComponentTypeId typeId) const
    {
        std::uint32_t count = 0;
        for (const Entry& entry : m_entries)
        {
            if (entry.typeId == typeId)
            {
                ++count;
            }
        }
        return count;
    }

    bool ComponentMenuTable::DrawItems(const ComponentMenuContext& context, bool separatorFirst)
    {
        const DrawScope scope(m_drawDepth);
        ComponentMenuContext itemContext = context;
        ComponentMenuOwner previousOwner = nullptr;
        for (const Entry& entry : m_entries)
        {
            if (entry.typeId != context.address.typeId)
            {
                continue;
            }
            const bool first = previousOwner == nullptr;
            if ((first && separatorFirst) || (false == first && previousOwner != entry.owner))
            {
                ImGui::Separator();
            }
            previousOwner = entry.owner;
            itemContext.user = entry.user;
            if (false == entry.draw(itemContext))
            {
                return false;
            }
        }
        return true;
    }

    bool ComponentMenuTable::IsDrawing() const
    {
        return m_drawDepth > 0;
    }
}
