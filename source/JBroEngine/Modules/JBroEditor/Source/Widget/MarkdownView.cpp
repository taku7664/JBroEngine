#include <JBro/Editor/Widget/MarkdownView.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/EditorUI.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include "CodeHighlighting.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui_internal.h>

namespace JBro::Widget
{
    namespace
    {
        ImU32 Color(const ImVec4& value) { return ImGui::ColorConvertFloat4ToU32(value); }
        struct Glyph
        {
            UInt32 begin = 0, end = 0, style = 0;
            Float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f, size = 0.0f;
            ImFont* font = nullptr;
            ImU32 color = 0;
            String target, literal;
        };
        struct Decoration
        {
            enum class Kind { Box, Rule, Quote, Image, Code, Task } kind = Kind::Box;
            Float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
            UInt32 begin = 0, end = 0;
            MarkdownImage image;
            String label;
            Bool checked = false;
        };
        struct AnchorPosition { String name; Float y = 0.0f; };
        UInt32 Next(const String& text, UInt32 at)
        {
            ++at;
            while (at < text.size() && (static_cast<unsigned char>(text[at.Get()]) & 0xc0) == 0x80)
            {
                ++at;
            }
            return at;
        }
        Bool LinkAllowed(const String& target)
        {
            const auto colon = target.find(':');
            return !target.StartsWith("//") && (colon == String::npos || Markdown::IsExternalLink(target)
                || (colon == 1 && target.size() > 2));
        }
        void CanvasTooltip(const ImGuiLastItemData& canvas, const char* message)
        {
            auto& item = ImGui::GetCurrentContext()->LastItemData;
            const auto previous = item;
            item = canvas;
            HoveredTooltip(message);
            item = previous;
        }
    }
    struct MarkdownView::State
    {
        Markdown::Document document;
        MarkdownCallbacks callbacks;
        Array<Glyph> glyphs;
        Array<Decoration> decorations;
        Array<AnchorPosition> anchors;
        Array<Markdown::Match> matches;
        String query;
        UInt32 first = 0, last = 0, current = 0, visible = 0;
        UInt64 layoutRevision = 0;
        Float width = -1.0f, fontSize = 0.0f, extentX = 0.0f, extentY = 0.0f;
        Float dpiScale = 1.0f;
        ImFont* font = nullptr;
        ImFont* codeFont = nullptr;
        Bool dirty = true, find = false, focusFind = false, dragging = false, dragged = false;
        Float scrollTo = -1.0f;
        String pendingAnchor;
        void AddLiteral(const String& text, Float x, Float y, Float size, ImU32 color)
        {
            Glyph glyph;
            glyph.literal = text;
            glyph.x = x;
            glyph.y = y;
            glyph.size = size;
            glyph.height = size * EditorTheme::MarkdownLineHeight;
            glyph.font = font;
            glyph.color = color;
            glyph.width = font->CalcTextSizeA(size.Get(), FLT_MAX, 0, text.c_str()).x;
            glyphs.Add(std::move(glyph));
        }
        Float Text(const Markdown::Block& block, Float left, Float top, Float available, Float size, Bool code)
        {
            Float x = left, y = top;
            Float lineHeight = size * EditorTheme::MarkdownLineHeight;
            const ImU32 normal = Color(EditorTheme::Ink);
            Array<ImU32> colors;
            if (code && (block.language == "cpp" || block.language == "c++" || block.language == "C++"
                || block.language == "jscript" || block.language == "JBroScript"))
            {
                TextEditor tokenizer;
                tokenizer.SetText(document.text.View().substr(block.begin.Get(), (block.end - block.begin).Get()));
                tokenizer.SetLanguage(block.language == "cpp" || block.language == "c++" || block.language == "C++"
                    ? TextEditor::Language::Cpp() : JBroLanguage());
                const auto palette = CodePalette();
                colors.Resize((block.end - block.begin).Get());
                std::fill(colors.begin(), colors.end(), normal);
                const auto spans = tokenizer.GetColorizedSpans();
                UInt32 at = block.begin;
                std::size_t row = 0, column = 0, spanIndex = 0;
                while (at < block.end)
                {
                    while (spanIndex < spans.size() && (spans[spanIndex].end.line < row
                        || (spans[spanIndex].end.line == row && spans[spanIndex].end.index <= column)))
                    {
                        ++spanIndex;
                    }
                    const auto next = Next(document.text, at);
                    if (spanIndex < spans.size() && spans[spanIndex].start.line == row
                        && spans[spanIndex].start.index <= column && column < spans[spanIndex].end.index)
                    {
                        for (auto byte = at; byte < next; ++byte)
                        {
                            colors[(byte - block.begin).Get()] = palette.get(spans[spanIndex].color);
                        }
                    }
                    if (document.text[at.Get()] == '\n')
                    {
                        ++row;
                        column = 0;
                    }
                    else
                    {
                        ++column;
                    }
                    at = next;
                }
            }
            for (const auto& run : block.runs)
            {
                if (run.image)
                {
                    MarkdownImage image;
                    if (callbacks.image)
                    {
                        image = callbacks.image(callbacks.user, run.target);
                    }
                    if (x != left)
                    {
                        y += lineHeight;
                        x = left;
                    }
                    Decoration item;
                    item.kind = Decoration::Kind::Image;
                    item.x = left;
                    item.y = y;
                    item.width = available;
                    item.height = lineHeight * 2;
                    if (image.width > 0 && image.height > 0)
                    {
                        item.width = std::min(available.Get(), static_cast<float>(image.width.Get()));
                        item.height = item.width * static_cast<Float>(image.height.Get()) / static_cast<Float>(image.width.Get());
                    }
                    item.image = image;
                    item.begin = run.begin;
                    item.end = run.end;
                    item.label = document.text.Substr(run.begin.Get(), (run.end - run.begin).Get());
                    decorations.Add(std::move(item));
                    // Alt text remains part of the same logical selection, including missing images.
                    if (image.width == 0 || image.height == 0)
                    {
                        for (auto at = run.begin; at < run.end; at = Next(document.text, at))
                        {
                            const auto next = Next(document.text, at);
                            Glyph glyph;
                            glyph.begin = at;
                            glyph.end = next;
                            glyph.x = x;
                            glyph.y = y;
                            glyph.font = font;
                            glyph.size = size;
                            glyph.height = lineHeight;
                            glyph.width = font->CalcTextSizeA(size.Get(), FLT_MAX, 0, document.text.data() + at.Get(), document.text.data() + next.Get()).x;
                            glyph.color = Color(EditorTheme::InkMuted);
                            x += glyph.width;
                            glyphs.Add(std::move(glyph));
                        }
                    }
                    y += decorations[decorations.Size() - 1].height + lineHeight;
                    x = left;
                    continue;
                }
                ImFont* face = code || (run.style.Get() & Markdown::InlineCode) ? codeFont : font;
                for (auto at = run.begin; at < run.end;)
                {
                    const auto next = Next(document.text, at);
                    const char character = document.text[at.Get()];
                    Float glyphWidth = face->CalcTextSizeA(size.Get(), FLT_MAX, 0,
                        document.text.data() + at.Get(), document.text.data() + next.Get()).x;
                    if (character == '\t')
                    {
                        const Float tab = size * EditorTheme::MarkdownTabWidth;
                        glyphWidth = tab - std::fmod((x - left).Get(), tab.Get());
                    }
                    // Wrap at word boundaries where possible; long tokens still fit the viewport.
                    if (!code && character != '\n' && character != ' ' && x > left
                        && (at == run.begin || document.text[at.Get() - 1] == ' '))
                    {
                        auto wordEnd = at;
                        while (wordEnd < run.end && document.text[wordEnd.Get()] != ' ' && document.text[wordEnd.Get()] != '\n')
                        {
                            wordEnd = Next(document.text, wordEnd);
                        }
                        const Float wordWidth = face->CalcTextSizeA(size.Get(), FLT_MAX, 0,
                            document.text.data() + at.Get(), document.text.data() + wordEnd.Get()).x;
                        if (wordWidth <= available && x + wordWidth > left + available)
                        {
                            x = left;
                            y += lineHeight;
                        }
                    }
                    if (!code && x > left && x + glyphWidth > left + available)
                    {
                        x = left;
                        y += lineHeight;
                    }
                    Glyph glyph;
                    glyph.begin = at;
                    glyph.end = next;
                    glyph.x = x;
                    glyph.y = y;
                    glyph.width = character == '\n' ? size * EditorTheme::MarkdownNewlineWidth : glyphWidth;
                    glyph.height = lineHeight;
                    glyph.size = size;
                    glyph.font = face;
                    glyph.style = run.style | (block.kind == Markdown::Kind::Heading || block.kind == Markdown::Kind::HeaderCell ? Markdown::Bold : 0);
                    glyph.target = run.target;
                    glyph.color = !run.target.empty() ? Color(EditorTheme::AccentBright) : colors.IsEmpty() ? normal : colors[(at - block.begin).Get()];
                    glyphs.Add(std::move(glyph));
                    if (character == '\n')
                    {
                        x = left;
                        y += lineHeight;
                    }
                    else
                    {
                        x += glyphWidth;
                    }
                    extentX = std::max(extentX.Get(), x.Get());
                    at = next;
                }
            }
            return y + lineHeight;
        }
        Float Block(UInt32 index, Float left, Float top, Float available)
        {
            const auto& block = document.blocks[index.Get()];
            const Float gap = fontSize * EditorTheme::MarkdownBlockGap;
            const Float indent = fontSize * EditorTheme::MarkdownIndent;
            Float y = top;
            switch (block.kind)
            {
            case Markdown::Kind::Heading:
            {
                anchors.Add({block.anchor, y});
                const Float scale = EditorTheme::MarkdownHeadingScale[std::min(block.level.Get(), 6u) - 1];
                return Text(block, left, y + gap, available, fontSize * scale, false) + gap;
            }
            case Markdown::Kind::Paragraph:
            case Markdown::Kind::Html:
            case Markdown::Kind::Cell:
            case Markdown::Kind::HeaderCell:
                return Text(block, left, y, available, fontSize, false) + gap;
            case Markdown::Kind::Code:
            {
                const Float end = Text(block, left + gap, y + fontSize * 2, available - gap * 2, fontSize, true) + gap;
                Decoration box;
                box.kind = Decoration::Kind::Code;
                box.x = left;
                box.y = y;
                box.width = std::max(available.Get(), (extentX - left + gap).Get());
                box.height = end - y;
                box.begin = block.begin;
                box.end = block.end;
                box.label = block.language;
                decorations.Add(std::move(box));
                return end + gap;
            }
            case Markdown::Kind::Rule:
            {
                Decoration rule;
                rule.kind = Decoration::Kind::Rule;
                rule.x = left;
                rule.y = y + gap;
                rule.width = available;
                decorations.Add(std::move(rule));
                return y + gap * 2;
            }
            case Markdown::Kind::Table:
            {
                Array<UInt32> rows;
                for (const auto group : block.children)
                {
                    for (const auto row : document.blocks[group.Get()].children)
                    {
                        rows.Add(row);
                    }
                }
                for (const auto rowIndex : rows)
                {
                    const auto& row = document.blocks[rowIndex.Get()];
                    const Float cellWidth = std::max((fontSize * EditorTheme::MarkdownMinimumCellWidth).Get(),
                        available.Get() / static_cast<float>(std::max<std::size_t>(1, row.children.Size())));
                    Float rowEnd = y;
                    const auto firstGlyph = glyphs.Size();
                    std::size_t column = 0;
                    for (const auto cellIndex : row.children)
                    {
                        const auto& cell = document.blocks[cellIndex.Get()];
                        const auto glyphBegin = glyphs.Size();
                        const Float cellLeft = left + static_cast<Float>(column) * cellWidth;
                        rowEnd = std::max(rowEnd.Get(), Block(cellIndex, cellLeft + gap, y + gap, cellWidth - gap * 2).Get());
                        if (cell.alignment == 2 || cell.alignment == 3)
                        {
                            Float lineY = -1.0f, right = cellLeft;
                            std::size_t lineBegin = glyphBegin;
                            for (std::size_t g = glyphBegin; g <= glyphs.Size(); ++g)
                            {
                                if (g == glyphs.Size() || glyphs[g].y != lineY)
                                {
                                    const Float shift = std::max(0.0f, (cellLeft + cellWidth - gap - right).Get()) / (cell.alignment == 2 ? 2.0f : 1.0f);
                                    for (std::size_t k = lineBegin; k < g; ++k)
                                    {
                                        glyphs[k].x += shift;
                                    }
                                    lineBegin = g;
                                    if (g < glyphs.Size())
                                    {
                                        lineY = glyphs[g].y;
                                    }
                                }
                                if (g < glyphs.Size())
                                {
                                    right = glyphs[g].x + glyphs[g].width;
                                }
                            }
                        }
                        ++column;
                    }
                    column = 0;
                    for (const auto cellIndex : row.children)
                    {
                        Decoration cell;
                        cell.x = left + static_cast<Float>(column++) * cellWidth;
                        cell.y = y;
                        cell.width = cellWidth;
                        cell.height = rowEnd - y;
                        cell.label = document.blocks[cellIndex.Get()].kind == Markdown::Kind::HeaderCell ? "header" : "";
                        decorations.Add(std::move(cell));
                    }
                    // Cells are laid out separately; sort visual rows for culling and hit testing.
                    std::stable_sort(glyphs.begin() + firstGlyph, glyphs.end(), [](const Glyph& a, const Glyph& b) {
                        return a.y == b.y ? a.x < b.x : a.y < b.y;
                    });
                    extentX = std::max(extentX.Get(), (left + static_cast<Float>(column) * cellWidth).Get());
                    y = rowEnd;
                }
                return y + gap;
            }
            default: break;
            }
            Float childLeft = left, childWidth = available;
            if (block.kind == Markdown::Kind::Quote || block.kind == Markdown::Kind::Item)
            {
                childLeft += indent;
                childWidth -= indent;
                if (block.kind == Markdown::Kind::Item)
                {
                    if (block.task)
                    {
                        Decoration task;
                        task.kind = Decoration::Kind::Task;
                        task.x = left;
                        task.y = y + fontSize * (EditorTheme::MarkdownLineHeight - 1.0f) / 2.0f;
                        task.width = task.height = fontSize;
                        task.checked = block.checked;
                        decorations.Add(std::move(task));
                    }
                    else
                    {
                        String prefix = document.blocks[block.parent.Get()].kind == Markdown::Kind::OrderedList
                            ? std::to_string(block.number.Get()) + "." : "\xe2\x80\xa2";
                        AddLiteral(prefix, left, y, fontSize, Color(EditorTheme::InkMuted));
                    }
                }
            }
            if (!block.runs.IsEmpty())
            {
                y = Text(block, childLeft, y, childWidth, fontSize, false) + gap;
            }
            for (const auto child : block.children)
            {
                y = Block(child, childLeft, y, std::max(fontSize.Get(), childWidth.Get()));
            }
            if (block.kind == Markdown::Kind::Quote)
            {
                Decoration quote;
                quote.kind = Decoration::Kind::Quote;
                quote.x = left + gap;
                quote.y = top;
                quote.height = y - top;
                decorations.Add(std::move(quote));
            }
            return y;
        }
        void Layout(Float available)
        {
            glyphs.Clear();
            decorations.Clear();
            anchors.Clear();
            width = available;
            font = ImGui::GetFont();
            codeFont = EditorTheme::GetCodeFont();
            if (!codeFont)
            {
                codeFont = font;
            }
            fontSize = ImGui::GetFontSize();
            dpiScale = ImGui::GetWindowViewport()->DpiScale;
            extentX = available;
            extentY = document.blocks.IsEmpty() ? Float(0.0f) : Block(0, fontSize, fontSize,
                std::max(fontSize.Get(), (available - fontSize * 2).Get()));
            std::stable_sort(glyphs.begin(), glyphs.end(), [](const Glyph& a, const Glyph& b) {
                return a.y == b.y ? a.x < b.x : a.y < b.y;
            });
            dirty = false;
            ++layoutRevision;
            if (!pendingAnchor.empty())
            {
                for (const auto& anchor : anchors)
                {
                    if (anchor.name == pendingAnchor)
                    {
                        scrollTo = anchor.y;
                        break;
                    }
                }
                pendingAnchor.clear();
            }
        }
        UInt32 Hit(Float x, Float y) const
        {
            const Glyph* nearest = nullptr;
            Float bestVertical = FLT_MAX, bestHorizontal = FLT_MAX;
            const Float tolerance = fontSize * EditorTheme::MarkdownHeadingScale[0] * EditorTheme::MarkdownLineHeight;
            auto firstCandidate = std::lower_bound(glyphs.begin(), glyphs.end(), y - tolerance,
                [](const Glyph& glyph, Float top) { return glyph.y < top; });
            if (firstCandidate == glyphs.end() && !glyphs.IsEmpty())
            {
                firstCandidate = glyphs.end() - 1;
                const auto lastY = firstCandidate->y;
                while (firstCandidate != glyphs.begin() && (firstCandidate - 1)->y == lastY)
                {
                    --firstCandidate;
                }
            }
            for (auto it = firstCandidate; it != glyphs.end(); ++it)
            {
                const auto& glyph = *it;
                if (glyph.y > y + tolerance && nearest)
                {
                    break;
                }
                if (!glyph.literal.empty())
                {
                    continue;
                }
                const Float vertical = y < glyph.y ? glyph.y - y : y > glyph.y + glyph.height ? y - glyph.y - glyph.height : Float(0);
                const Float horizontal = x < glyph.x ? glyph.x - x : x > glyph.x + glyph.width ? x - glyph.x - glyph.width : Float(0);
                if (vertical < bestVertical || (vertical == bestVertical && horizontal < bestHorizontal))
                {
                    bestVertical = vertical;
                    bestHorizontal = horizontal;
                    nearest = &glyph;
                }
            }
            for (const auto& image : decorations)
            {
                if (image.kind == Decoration::Kind::Image && x >= image.x && x <= image.x + image.width
                    && y >= image.y && y <= image.y + image.height)
                {
                    return x < image.x + image.width / 2 ? image.begin : image.end;
                }
            }
            return nearest ? x < nearest->x + nearest->width / 2 ? nearest->begin : nearest->end : UInt32(0);
        }
        void ScrollMatch()
        {
            if (!matches.IsEmpty())
            {
                for (const auto& glyph : glyphs)
                {
                    if (glyph.begin >= matches[current.Get()].begin && glyph.begin < matches[current.Get()].end)
                    {
                        scrollTo = glyph.y;
                        break;
                    }
                }
                for (const auto& decoration : decorations)
                {
                    if (decoration.kind == Decoration::Kind::Image && decoration.begin < matches[current.Get()].end
                        && decoration.end > matches[current.Get()].begin)
                    {
                        scrollTo = decoration.y;
                        break;
                    }
                }
            }
        }
    };
    MarkdownView::MarkdownView() : m_state(MakeOwnerPtr<State>()) {}
    MarkdownView::~MarkdownView() = default;
    void MarkdownView::SetDocument(Markdown::Document document)
    {
        m_state->document = std::move(document);
        m_state->glyphs.Clear();
        m_state->decorations.Clear();
        m_state->first = m_state->last = 0;
        m_state->dragging = false;
        m_state->dirty = true;
        SetSearch(m_state->query);
    }
    const Markdown::Document& MarkdownView::GetDocument() const { return m_state->document; }
    void MarkdownView::SetCallbacks(const MarkdownCallbacks& callbacks) { m_state->callbacks = callbacks; }
    void MarkdownView::InvalidateLayout() { m_state->dirty = true; }
    void MarkdownView::Select(UInt32 begin, UInt32 end)
    {
        m_state->first = Markdown::Boundary(m_state->document.text, begin);
        m_state->last = Markdown::Boundary(m_state->document.text, end);
    }
    void MarkdownView::SelectAll() { Select(0, static_cast<UInt32>(m_state->document.text.size())); }
    String MarkdownView::SelectedText() const { return Markdown::Copy(m_state->document, m_state->first, m_state->last); }
    Bool MarkdownView::HasSelection() const { return m_state->first != m_state->last; }
    void MarkdownView::Copy() const
    {
        const String text = SelectedText();
        if (!text.empty())
        {
            ImGui::SetClipboardText(text.c_str());
        }
    }
    void MarkdownView::ShowFind() { m_state->find = true; m_state->focusFind = true; }
    void MarkdownView::SetSearch(const String& query)
    {
        m_state->query = query;
        m_state->matches = Markdown::Find(m_state->document, query);
        m_state->current = 0;
        m_state->ScrollMatch();
    }
    void MarkdownView::NextMatch(Bool backwards)
    {
        auto& s = *m_state;
        if (!s.matches.IsEmpty())
        {
            s.current = static_cast<UInt32>((s.current.Get() + s.matches.Size() + (backwards ? -1 : 1)) % s.matches.Size());
            s.ScrollMatch();
        }
    }
    UInt32 MarkdownView::MatchCount() const { return static_cast<UInt32>(m_state->matches.Size()); }
    Bool MarkdownView::GoToAnchor(const String& anchor)
    {
        auto& s = *m_state;
        const String decoded = Markdown::DecodeTarget(anchor);
        if (s.dirty)
        {
            s.pendingAnchor = decoded;
            return true;
        }
        for (const auto& position : s.anchors)
        {
            if (position.name == decoded)
            {
                s.scrollTo = position.y;
                return true;
            }
        }
        return false;
    }
    UInt64 MarkdownView::LayoutRevision() const { return m_state->layoutRevision; }
    UInt32 MarkdownView::VisibleGlyphCount() const { return m_state->visible; }
    void MarkdownView::Render(const char* id)
    {
        auto& s = *m_state;
        IdScope ids(id);
        if (s.find)
        {
            if (s.focusFind)
            {
                ImGui::SetKeyboardFocusHere();
                s.focusFind = false;
            }
            String next = s.query;
            const char* previousLabel = Loc::TextOr(LocKeys::MarkdownPrevious, "Previous");
            const char* nextLabel = Loc::TextOr(LocKeys::MarkdownNext, "Next");
            const char* closeLabel = Loc::TextOr(LocKeys::MarkdownCloseFind, "Close");
            const UInt32 total = static_cast<UInt32>(s.matches.Size());
            const UInt32 shown = total > 0 ? s.current + 1 : UInt32(0);
            char count[48];
            std::snprintf(count, sizeof(count), "%u / %u", shown.Get(), total.Get());
            const Float controlsWidth = ImGui::CalcTextSize(previousLabel).x + ImGui::CalcTextSize(nextLabel).x
                + ImGui::CalcTextSize(closeLabel).x + ImGui::CalcTextSize(count).x
                + ImGui::GetStyle().FramePadding.x * 6.0f + ImGui::GetStyle().ItemSpacing.x * 4.0f;
            const Float availableWidth = ImGui::GetContentRegionAvail().x;
            const Bool wrapControls = availableWidth < controlsWidth + ImGui::GetFontSize() * EditorTheme::MarkdownMinimumCellWidth.Get();
            const Bool searchWasActive = IsTextInputActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            SearchBox("##find", next).Hint(Loc::TextOr(LocKeys::MarkdownFind, "Find in document"))
                .Width(wrapControls ? availableWidth : availableWidth - controlsWidth).Draw();
            if (next != s.query)
            {
                SetSearch(next);
            }
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Enter))
            {
                NextMatch(ImGui::GetIO().KeyShift);
                s.focusFind = searchWasActive;
            }
            if (!wrapControls)
            {
                ImGui::SameLine();
            }
            Text(count);
            {
                DisableScope unavailable(s.matches.IsEmpty());
                ImGui::SameLine();
                if (Button(previousLabel))
                {
                    NextMatch(true);
                }
                DisabledReason(s.matches.IsEmpty(), Loc::TextOr(LocKeys::CommonNoMatches, "No matches."));
                ImGui::SameLine();
                if (Button(nextLabel))
                {
                    NextMatch();
                }
                DisabledReason(s.matches.IsEmpty(), Loc::TextOr(LocKeys::CommonNoMatches, "No matches."));
            }
            ImGui::SameLine();
            if (Button(closeLabel))
            {
                s.find = false;
            }
        }
        ImGui::BeginChild("##body", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        const Float available = ImGui::GetCurrentWindow()->InnerRect.GetWidth() - ImGui::GetStyle().WindowPadding.x * 2.0f;
        if (s.dirty || s.width != available || s.font != ImGui::GetFont() || s.fontSize != ImGui::GetFontSize()
            || s.dpiScale != ImGui::GetWindowViewport()->DpiScale
            || s.codeFont != (EditorTheme::GetCodeFont() ? EditorTheme::GetCodeFont() : ImGui::GetFont()))
        {
            s.Layout(available);
        }
        if (s.scrollTo >= 0)
        {
            ImGui::SetScrollY(std::max(0.0f, (s.scrollTo - s.fontSize).Get()));
            s.scrollTo = -1.0f;
        }
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const auto clipMin = ImGui::GetWindowPos();
        const auto clipMax = ImVec2(clipMin.x + ImGui::GetWindowSize().x, clipMin.y + ImGui::GetWindowSize().y);
        const auto mouse = ImGui::GetIO().MousePos;
        auto* draw = ImGui::GetWindowDrawList();
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##selection", ImVec2(std::max(available.Get(), s.extentX.Get()), std::max(s.extentY.Get(), ImGui::GetContentRegionAvail().y)));
        const Bool hovered = ImGui::IsItemHovered();
        const auto canvasItem = ImGui::GetCurrentContext()->LastItemData;
        const Float mx = mouse.x - origin.x, my = mouse.y - origin.y;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const auto at = s.Hit(mx, my);
            if (!ImGui::GetIO().KeyShift)
            {
                s.first = at;
            }
            s.last = at;
            s.dragging = true;
            s.dragged = false;
        }
        if (s.dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            s.dragged = s.dragged || ImGui::IsMouseDragging(ImGuiMouseButton_Left);
            if (s.dragged)
            {
                s.last = s.Hit(mx, my);
                const Float delta = mouse.y < clipMin.y ? mouse.y - clipMin.y : mouse.y > clipMax.y ? mouse.y - clipMax.y : 0;
                ImGui::SetScrollY(ImGui::GetScrollY() + delta.Get() * ImGui::GetIO().DeltaTime * EditorTheme::MarkdownAutoScrollSpeed.Get());
                const Float horizontal = mouse.x < clipMin.x ? mouse.x - clipMin.x : mouse.x > clipMax.x ? mouse.x - clipMax.x : 0;
                ImGui::SetScrollX(ImGui::GetScrollX() + horizontal.Get() * ImGui::GetIO().DeltaTime * EditorTheme::MarkdownAutoScrollSpeed.Get());
            }
        }
        const Bool released = s.dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        const auto low = std::min(s.first.Get(), s.last.Get()), high = std::max(s.first.Get(), s.last.Get());
        for (const auto& decoration : s.decorations)
        {
            const ImVec2 p(origin.x + decoration.x.Get(), origin.y + decoration.y.Get());
            const ImVec2 q(p.x + decoration.width.Get(), p.y + decoration.height.Get());
            if (q.y < clipMin.y || p.y > clipMax.y)
            {
                continue;
            }
            if (decoration.kind == Decoration::Kind::Rule || decoration.kind == Decoration::Kind::Quote)
            {
                draw->AddLine(p, q, Color(EditorTheme::LineStrong), EditorTheme::MarkdownBorderWidth.Get());
            }
            else if (decoration.kind == Decoration::Kind::Task)
            {
                draw->AddRect(p, q, Color(EditorTheme::InkMuted));
                if (decoration.checked)
                {
                    ImGui::RenderCheckMark(draw, ImVec2(p.x + decoration.width.Get() * 0.15f, p.y + decoration.height.Get() * 0.15f),
                        Color(EditorTheme::Ink), decoration.height.Get() * 0.7f);
                }
            }
            else if (decoration.kind == Decoration::Kind::Image)
            {
                if (decoration.image.texture.IsValid())
                {
                    draw->AddImage(static_cast<ImTextureID>(EditorUI::ToTextureId(decoration.image.texture).Get()), p, q);
                }
                else
                {
                    draw->AddRect(p, q, Color(EditorTheme::LineStrong));
                }
                if (low < decoration.end.Get() && high > decoration.begin.Get())
                {
                    draw->AddRectFilled(p, q, Color(EditorTheme::Fade(EditorTheme::Accent, EditorTheme::MarkdownSelectionAlpha)));
                }
                const auto match = std::lower_bound(s.matches.begin(), s.matches.end(), decoration.begin,
                    [](const Markdown::Match& candidate, UInt32 position) { return candidate.end <= position; });
                if (match != s.matches.end() && match->begin < decoration.end)
                {
                    draw->AddRect(p, q, Color(EditorTheme::Amber), 0.0f, 0, EditorTheme::MarkdownBorderWidth.Get() * 2.0f);
                }
                if (ImGui::IsMouseHoveringRect(p, q))
                {
                    CanvasTooltip(canvasItem, (decoration.label + "\n" + decoration.image.message).c_str());
                }
            }
            else
            {
                draw->AddRectFilled(p, q, Color(decoration.kind == Decoration::Kind::Code ? EditorTheme::Workspace
                    : decoration.label == "header" ? EditorTheme::Raised : EditorTheme::Panel));
                draw->AddRect(p, q, Color(EditorTheme::Line));
                if (decoration.kind == Decoration::Kind::Code)
                {
                    draw->AddText(ImVec2(p.x + s.fontSize.Get() * EditorTheme::MarkdownBlockGap.Get(), p.y), Color(EditorTheme::InkMuted), decoration.label.c_str());
                    ImGui::SetCursorScreenPos(ImVec2(q.x - s.fontSize.Get() * EditorTheme::MarkdownCopyButtonWidth.Get(), p.y));
                    IdScope codeId(static_cast<Int32>(decoration.begin.Get()));
                    DisableScope emptyCode(decoration.begin == decoration.end);
                    if (Button(Loc::TextOr(LocKeys::MenuCopy, "Copy")))
                    {
                        ImGui::SetClipboardText(Markdown::Copy(s.document, decoration.begin, decoration.end).c_str());
                    }
                    DisabledReason(decoration.begin == decoration.end, Loc::TextOr(LocKeys::CodeEditorEmptyDocument, "The document is empty."));
                }
            }
        }
        s.visible = 0;
        // Sorted visual positions make the skipped prefix logarithmic for large documents.
        const auto start = std::lower_bound(s.glyphs.begin(), s.glyphs.end(), clipMin.y - origin.y - s.fontSize.Get() * EditorTheme::MarkdownHeadingScale[0].Get() * EditorTheme::MarkdownLineHeight.Get(),
            [](const Glyph& glyph, float y) { return glyph.y < y; });
        for (auto it = start; it != s.glyphs.end(); ++it)
        {
            const auto& glyph = *it;
            const ImVec2 p(origin.x + glyph.x.Get(), origin.y + glyph.y.Get());
            if (p.y > clipMax.y)
            {
                break;
            }
            ++s.visible;
            const ImVec2 q(p.x + glyph.width.Get(), p.y + glyph.height.Get());
            if (glyph.literal.empty())
            {
                if (glyph.style.Get() & Markdown::InlineCode)
                {
                    draw->AddRectFilled(p, q, Color(EditorTheme::Raised));
                }
                const auto match = std::lower_bound(s.matches.begin(), s.matches.end(), glyph.begin,
                    [](const Markdown::Match& candidate, UInt32 position) { return candidate.end <= position; });
                if (match != s.matches.end() && glyph.end > match->begin)
                {
                    draw->AddRectFilled(p, q, Color(EditorTheme::Fade(EditorTheme::Amber,
                        static_cast<std::size_t>(match - s.matches.begin()) == s.current.Get()
                            ? EditorTheme::MarkdownCurrentMatchAlpha : EditorTheme::MarkdownSelectionAlpha)));
                }
                if (glyph.begin < high && glyph.end > low)
                {
                    draw->AddRectFilled(p, q, Color(EditorTheme::Fade(EditorTheme::Accent, EditorTheme::MarkdownSelectionAlpha)));
                }
            }
            const char* begin = glyph.literal.empty() ? s.document.text.data() + glyph.begin.Get() : glyph.literal.data();
            const char* end = glyph.literal.empty() ? s.document.text.data() + glyph.end.Get() : begin + glyph.literal.size();
            if (*begin != '\n' && *begin != '\t')
            {
                const auto vertexBegin = draw->VtxBuffer.Size;
                draw->AddText(glyph.font, glyph.size.Get(), p, glyph.color, begin, end);
                if (glyph.style.Get() & Markdown::Bold)
                {
                    draw->AddText(glyph.font, glyph.size.Get(), ImVec2(p.x + EditorTheme::MarkdownBoldOffset.Get(), p.y), glyph.color, begin, end);
                }
                if (glyph.style.Get() & Markdown::Italic)
                {
                    // Shear this glyph's emitted vertices; font metrics and hit rectangles stay shared.
                    const auto vertexEnd = draw->VtxBuffer.Size;
                    for (auto v = vertexBegin; v < vertexEnd; ++v)
                    {
                        draw->VtxBuffer[v].pos.x += (p.y + glyph.size.Get() - draw->VtxBuffer[v].pos.y) * EditorTheme::MarkdownItalicShear.Get();
                    }
                }
            }
            if (glyph.style.Get() & Markdown::Strike)
            {
                draw->AddLine(ImVec2(p.x, p.y + glyph.size.Get() / 2), ImVec2(q.x, p.y + glyph.size.Get() / 2), glyph.color);
            }
            if (!glyph.target.empty() && ImGui::IsMouseHoveringRect(p, q))
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                const Bool allowed = LinkAllowed(glyph.target);
                CanvasTooltip(canvasItem, allowed ? glyph.target.c_str() : Loc::TextOr(LocKeys::MarkdownUnsupportedLink, "This link scheme is not supported."));
                if (released && !s.dragged && !ImGui::GetIO().KeyShift && allowed)
                {
                    if (glyph.target.StartsWith("#"))
                    {
                        GoToAnchor(glyph.target.Substr(1));
                    }
                    else if (s.callbacks.openLink)
                    {
                        s.callbacks.openLink(s.callbacks.user, glyph.target);
                    }
                }
            }
        }
        if (released)
        {
            s.dragging = false;
        }
        ImGui::EndChild();
    }
}
