#include <JBro/Editor/Widget/Button.h>

#include <imgui_internal.h>

#include <cstddef>

namespace JBro::Widget
{
    bool TextButton(
        const char* label, const ImVec2& size, const ImVec2& offset,
        ImGuiButtonFlags flags)
    {
        const ImVec2 startCursor = ImGui::GetCursorPos();
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        StyleScope style;
        style.PushColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        style.PushColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        style.PushColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
        ImGui::PushID(label);
        // 빈 이름으로 버튼을 그리고 글자를 그 위에 얹는다. 버튼에 이름을 주면
        // ImGui 가 제 나름대로 가운데를 잡아 `offset` 을 줄 자리가 없어진다.
        const bool pressed = ImGui::ButtonEx("", size, flags);
        const ImVec2 buttonSize = ImGui::GetItemRectSize();
        ImGui::SameLine();
        if (ImGui::IsItemHovered())
        {
            style.PushColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextSelectedBg));
        }
        const ImVec2 textPos =
            startCursor + (buttonSize - textSize) * 0.5f + offset;
        ImGui::SetCursorPos(textPos);
        if (IsSingleGlyph(label))
        {
            // 자리는 전처럼 글자 항목이 잡고(줄 높이·마지막 항목이 그대로다) 보이지 않게 둔다.
            // 그림은 잉크로 버튼 한가운데 그린다(D-277).
            const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
            const ImVec2 buttonMin = ImGui::GetItemRectMin();
            const ImVec2 buttonMax = ImGui::GetItemRectMax();
            {
                StyleScope hidden;
                hidden.PushColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
                ImGui::TextUnformatted(label);
            }
            DrawGlyphCentered(label, buttonMin + offset, buttonMax + offset, color);
        }
        else
        {
            ImGui::TextUnformatted(label);
        }
        ImGui::PopID();
        return pressed;
    }

    bool IsSingleGlyph(const char* text)
    {
        if (text == nullptr || *text == '\0')
        {
            return false;
        }
        unsigned int codePoint = 0;
        const int length = ImTextCharFromUtf8(&codePoint, text, nullptr);
        return text[length] == '\0';
    }

    namespace
    {
        // 글리프의 **잉크 상자**다 - 글자를 놓은 자리(왼쪽 위)에서 잰, 실제로 칠해지는 부분이다.
        //
        // 구운 글리프의 사각형(`X0..Y1`)은 잉크에 꼭 맞지 않는다. 래스터화의 여백과 글꼴이 적은 글리프 상자가 들어가,
        // 목록 손잡이(`drag-horizontal-variant`)는 사각형이 7 픽셀인데 칠해진 것은 위쪽 4 픽셀이었다. 그래서 아틀라스의
        // 픽셀을 읽어 조금이라도 칠해진 줄과 칸을 찾는다. 글리프의 UV 가 곧 사각형이라 텍셀을 사각형 좌표로 옮길 수 있다.
        bool MeasureInk(const ImFontGlyph& glyph, ImVec2& inkMin, ImVec2& inkMax)
        {
            const ImTextureData* texture = ImGui::GetIO().Fonts->TexData;
            if (texture == nullptr || texture->Pixels == nullptr || false == glyph.Visible)
            {
                return false;
            }
            const int left = static_cast<int>(glyph.U0 * texture->Width + 0.5f);
            const int top = static_cast<int>(glyph.V0 * texture->Height + 0.5f);
            const int right = static_cast<int>(glyph.U1 * texture->Width + 0.5f);
            const int bottom = static_cast<int>(glyph.V1 * texture->Height + 0.5f);
            if (right <= left || bottom <= top)
            {
                return false;
            }
            // 한 텍셀의 알파다. RGBA32 는 넷째 칸, Alpha8 은 그 하나다.
            const int alphaOffset = texture->Format == ImTextureFormat_Alpha8 ? 0 : 3;
            int inkLeft = right;
            int inkTop = bottom;
            int inkRight = left - 1;
            int inkBottom = top - 1;
            for (int y = top; y < bottom; ++y)
            {
                const unsigned char* row = texture->Pixels + static_cast<std::size_t>(y) * texture->GetPitch();
                for (int x = left; x < right; ++x)
                {
                    if (row[x * texture->BytesPerPixel + alphaOffset] == 0)
                    {
                        continue;
                    }
                    inkLeft = ImMin(inkLeft, x);
                    inkRight = ImMax(inkRight, x);
                    inkTop = ImMin(inkTop, y);
                    inkBottom = ImMax(inkBottom, y);
                }
            }
            if (inkRight < inkLeft)
            {
                return false;
            }
            const float texelWidth = (glyph.X1 - glyph.X0) / static_cast<float>(right - left);
            const float texelHeight = (glyph.Y1 - glyph.Y0) / static_cast<float>(bottom - top);
            inkMin = ImVec2(glyph.X0 + (inkLeft - left) * texelWidth, glyph.Y0 + (inkTop - top) * texelHeight);
            inkMax = ImVec2(glyph.X0 + (inkRight + 1 - left) * texelWidth, glyph.Y0 + (inkBottom + 1 - top) * texelHeight);
            return true;
        }

        // 글리프의 잉크 가운데다. 잉크를 못 재면 글리프 사각형의 가운데로 대신한다.
        ImVec2 InkCenter(const ImFontGlyph& glyph)
        {
            ImVec2 inkMin(glyph.X0, glyph.Y0);
            ImVec2 inkMax(glyph.X1, glyph.Y1);
            MeasureInk(glyph, inkMin, inkMax);
            return (inkMin + inkMax) * 0.5f;
        }
    }

    ImVec2 GlyphCenteredPosition(const char* glyph, const ImVec2& center, float fontSize)
    {
        unsigned int codePoint = 0;
        ImTextCharFromUtf8(&codePoint, glyph, nullptr);
        const float size = fontSize > 0.0f ? fontSize : ImGui::GetFontSize();
        ImFontBaked* baked = ImGui::GetFont()->GetFontBaked(size);
        const ImFontGlyph* found = baked->FindGlyph(static_cast<ImWchar>(codePoint));
        if (found == nullptr)
        {
            return center - ImGui::CalcTextSize(glyph) * 0.5f;
        }
        // 구운 크기와 그리는 크기가 다르면 글리프 좌표도 그만큼 늘어난다(`ImFont::RenderChar` 와 같다).
        const float scale = size / baked->Size;
        const ImVec2 inkCenter = InkCenter(*found) * scale;
        // **세로는 칸의 가운데가 아니라 같은 칸에 놓인 글자의 가운데에 맞춘다.** ImGui 는 글자를 줄 상자로 가운데
        // 잡는데, 글자의 잉크가 줄 상자 한가운데 있지는 않다(맑은 고딕 15 픽셀에서 1 픽셀 아래). 아이콘만 칸의
        // 한가운데 두면 옆 글자보다 그만큼 떠 보인다. 그 차이는 대문자 `H` 의 잉크 가운데와 줄 상자 가운데의 거리다 -
        // 글꼴에서 재는 값이라 글꼴이 바뀌어도 따라간다.
        float textDrop = 0.0f;
        if (const ImFontGlyph* capital = baked->FindGlyphNoFallback('H'))
        {
            textDrop = InkCenter(*capital).y * scale - ImGui::GetTextLineHeight() * (size / ImGui::GetFontSize()) * 0.5f;
        }
        // 픽셀에 맞춘다. 반 픽셀에 걸치면 선이 번진다.
        return ImFloor(center + ImVec2(0.0f, textDrop) - inkCenter + ImVec2(0.5f, 0.5f));
    }

    void DrawGlyphCentered(const char* glyph, const ImVec2& min, const ImVec2& max, ImU32 color, float fontSize)
    {
        const float size = fontSize > 0.0f ? fontSize : ImGui::GetFontSize();
        const ImVec2 position = GlyphCenteredPosition(glyph, (min + max) * 0.5f, size);
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), size, position, color, glyph);
    }

    void InlineIcon(const char* glyph)
    {
        InlineIcon(glyph, ImGui::GetColorU32(ImGuiCol_Text));
    }

    void InlineIcon(const char* glyph, ImU32 color)
    {
        const float lineHeight = ImGui::GetTextLineHeight();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        // 같은 줄의 글자가 내려앉는 만큼(켜기 칸·단추 뒤의 글자는 칸 여백만큼 내려간다) 아이콘도 내린다 - 아니면 칸 뒤의
        // 아이콘만 글자보다 떠 보인다(로그 창의 등급 필터에서 그랬다).
        const float textOffset = ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
        const ImVec2 min(cursor.x, cursor.y + textOffset);
        ImGui::Dummy(ImVec2(lineHeight, lineHeight + textOffset));
        DrawGlyphCentered(glyph, min, ImVec2(min.x + lineHeight, min.y + lineHeight), color);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    }

    void Icon(const char* glyph)
    {
        const float square = ImGui::GetFrameHeight();
        const ImVec2 min = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(square, square));
        DrawGlyphCentered(glyph, min, ImVec2(min.x + square, min.y + square), ImGui::GetColorU32(ImGuiCol_Text));
    }
}
