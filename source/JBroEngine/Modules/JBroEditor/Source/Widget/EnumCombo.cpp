#include <JBro/Editor/Widget/EnumCombo.h>

#include <JBro/Editor/Widget/FilterCombo.h>

namespace JBro::Widget
{
    bool EnumCombo(const char* id, const EnumNames& names, void* value, float width)
    {
        if (names.ToIndex == nullptr || names.FromIndex == nullptr
            || names.names == nullptr || value == nullptr)
        {
            return false;
        }
        int current = names.ToIndex(value);
        const int before = current;
        // 몸은 `FilterCombo` 다(D-116). 이름이 한 화면에 다 들어가면 검색 칸은 소음이라
        // 넘칠 때만 그린다.
        const ArrayView<const char* const> items(names.names, names.count);
        const bool changed = FilterCombo(id != nullptr ? id : "##enum", items, current)
            .ShowFilter(names.count > static_cast<std::uint32_t>(FilterCombo::DefaultMaxVisibleItems))
            .Width(width)
            .Draw();
        // **범위를 벗어난 고름은 버린다.** 위젯이 그럴 일은 없지만, 이름표 개수와
        // 실제 값의 개수가 어긋난 타입이 오면 여기서 막아야 한다.
        if (false == changed || current == before
            || current < 0 || static_cast<std::uint32_t>(current) >= names.count)
        {
            return false;
        }
        names.FromIndex(value, current);
        return true;
    }
}
