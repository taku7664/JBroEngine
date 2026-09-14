#pragma once

#include <imgui.h>

#include <cstdint>

// 에디터 공용 위젯 계층이다(ProjectRule §11, D-78).
//
// **패널이 ImGui 를 직접 부르지 않는다.** 기존 엔진의 `Application/Editor/ImItem/` 이
// 같은 자리이고, 거기서 깎아 놓은 것을 여기로 옮긴다. 이름에서 `Im` 접두어를 떼고
// 네임스페이스로 옮겼다 - 접두어를 쓰지 않는 것이 이 코드베이스의 규칙이고,
// `Widget::List` 가 `ImList` 만큼 읽힌다.
namespace JBro::Widget
{
    // 무엇이 잘못됐는지의 무게다. 색과 머리글자가 여기서 나온다.
    enum class Severity : std::uint8_t
    {
        Info,
        Success,
        Warning,
        Error
    };

    bool IsEmptyText(const char* text);
    ImVec4 SeverityColor(Severity severity);
    // 아이콘 글꼴이 아직 없다. 글자로 대신한다 - 빈 자리보다 낫다.
    const char* SeverityPrefix(Severity severity);
    ImVec4 WithAlpha(ImVec4 color, float alpha);
    ImVec4 ScaleColor(ImVec4 color, float scale);
    // 빈 글자면 아무것도 하지 않는다. 부르는 쪽이 분기하지 않아도 되게.
    void HoveredTooltip(const char* text, ImGuiHoveredFlags flags = ImGuiHoveredFlags_None);

    // 밀어 넣은 만큼 세어 두었다가 알아서 빼낸다. 중간에 돌아 나가는 길이 생겨도
    // 스타일 스택이 어긋나지 않는다 - ImGui 는 그 어긋남을 다음 프레임에야 말한다.
    class StyleScope
    {
    public:
        StyleScope() = default;
        ~StyleScope();

        StyleScope(const StyleScope&) = delete;
        StyleScope& operator=(const StyleScope&) = delete;

        template <typename T>
        void PushVar(ImGuiStyleVar index, const T& value)
        {
            ImGui::PushStyleVar(index, value);
            ++m_vars;
        }
        template <typename T>
        void PushColor(ImGuiCol index, const T& value)
        {
            ImGui::PushStyleColor(index, value);
            ++m_colors;
        }
        void Pop();

    private:
        int m_vars = 0;
        int m_colors = 0;
    };

    class DisableScope
    {
    public:
        explicit DisableScope(bool disable = true);
        ~DisableScope();

        DisableScope(const DisableScope&) = delete;
        DisableScope& operator=(const DisableScope&) = delete;

        bool IsDisabled() const;

    private:
        bool m_disabled = false;
    };

    // 값이 유효하지 않은 칸에 빨간 테두리를 두른다.
    //
    // **테마의 `FrameBorderSize` 가 0 이라 색만 바꾸면 아무것도 안 보인다** - 두께도
    // 같이 준다. 범위 안에 그려지는 프레임 위젯 전부에 걸리므로 글자 칸뿐 아니라
    // 드래그·슬라이더에도 쓸 수 있다.
    class InvalidScope
    {
    public:
        explicit InvalidScope(bool invalid = true);
        ~InvalidScope();

        InvalidScope(const InvalidScope&) = delete;
        InvalidScope& operator=(const InvalidScope&) = delete;

    private:
        bool m_invalid = false;
    };

    // `ImGui::PushID` 를 짝 맞춰 빼 준다.
    class IdScope
    {
    public:
        explicit IdScope(const char* id);
        explicit IdScope(int id);
        explicit IdScope(const void* id);
        ~IdScope();

        IdScope(const IdScope&) = delete;
        IdScope& operator=(const IdScope&) = delete;
    };
}
