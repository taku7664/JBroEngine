#include <JBro/Editor/Widget/EnumCombo.h>

namespace JBro::Widget
{
    bool EnumCombo(const char* id, const EnumNames& names, void* value, float width)
    {
        if (names.ToIndex == nullptr || names.FromIndex == nullptr
            || names.names == nullptr || value == nullptr)
        {
            return false;
        }
        if (width != 0.0f)
        {
            ImGui::SetNextItemWidth(width);
        }
        int current = names.ToIndex(value);
        if (false == ImGui::Combo(id != nullptr ? id : "##enum", &current,
            names.names, static_cast<int>(names.count)))
        {
            return false;
        }
        // **범위를 벗어난 고름은 버린다.** `Combo` 가 그럴 일은 없지만, 이름표
        // 개수와 실제 값의 개수가 어긋난 타입이 오면 여기서 막아야 한다.
        if (current < 0 || static_cast<std::size_t>(current) >= names.count)
        {
            return false;
        }
        names.FromIndex(value, current);
        return true;
    }
}
