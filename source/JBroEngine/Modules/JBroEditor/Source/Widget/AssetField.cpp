#include <JBro/Editor/Widget/AssetField.h>

#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Types/Array.h>

namespace JBro::Widget
{
    AssetField::AssetField(const char* id, ArrayView<const char* const> names,
        ArrayView<const AssetId> ids, AssetId& value)
        : m_id(id)
        , m_names(names)
        , m_ids(ids)
        , m_value(value)
    {
    }

    AssetField& AssetField::NoneText(const char* text)
    {
        m_noneText = text;
        return *this;
    }

    AssetField& AssetField::MissingText(const char* text)
    {
        m_missingText = text;
        return *this;
    }

    AssetField& AssetField::AllowClear(bool allow)
    {
        m_allowClear = allow;
        return *this;
    }

    AssetField& AssetField::Width(float width)
    {
        m_width = width;
        return *this;
    }

    bool AssetField::Draw() const
    {
        // 두 뷰가 어긋나면 짧은 쪽까지만 믿는다. 이름에 아이디가 없으면 고를 수 없다.
        const std::size_t count = m_names.Size() < m_ids.Size() ? m_names.Size() : m_ids.Size();
        const char* noneText = m_noneText != nullptr
            ? m_noneText : Loc::TextOr(LocKeys::AssetNone, "(none)");
        const char* missingText = m_missingText != nullptr
            ? m_missingText : Loc::TextOr(LocKeys::AssetMissing, "(missing asset)");

        // 비우기 항목이 맨 위에 서면 이름 번호가 한 칸 밀린다.
        const int offset = m_allowClear ? 1 : 0;
        Array<const char*> items;
        items.Reserve(count + static_cast<std::size_t>(offset));
        if (m_allowClear)
        {
            items.Add(noneText);
        }
        int current = -1;
        for (std::size_t index = 0; index < count; ++index)
        {
            items.Add(m_names[index]);
            if (current < 0 && m_ids[index] == m_value)
            {
                current = static_cast<int>(index) + offset;
            }
        }
        const bool isNull = m_value.IsNull();
        if (isNull && m_allowClear)
        {
            current = 0;
        }
        const int before = current;
        const bool changed = FilterCombo(m_id != nullptr ? m_id : "##asset",
            ArrayView<const char* const>(items.Data(), items.Size()), current)
            .EmptyText(isNull ? noneText : missingText)
            .Width(m_width)
            .Draw();
        if (false == changed || current == before || current < 0
            || static_cast<std::size_t>(current) >= items.Size())
        {
            return false;
        }
        const AssetId chosen = (m_allowClear && current == 0)
            ? AssetId{}
            : m_ids[static_cast<std::size_t>(current - offset)];
        if (chosen == m_value)
        {
            return false;
        }
        m_value = chosen;
        return true;
    }

    bool AssetField::operator()() const
    {
        return Draw();
    }
}
