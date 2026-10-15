#include <JBro/Editor/Widget/ScriptOutline.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/TextField.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Tree.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <imgui_internal.h>
#include <JBro/Types/ValueMath.h>

namespace JBro::Widget
{
    namespace
    {
        const char* KindIcon(Code::SummaryKind kind)
        {
            return kind == Code::SummaryKind::Type ? Icons::GameObject : kind == Code::SummaryKind::Field ? Icons::ScriptVariable : Icons::ScriptFunction;
        }
        void ClippedText(const char* text, ImVec2 minimum, ImVec2 maximum, ImU32 color)
        {
            if (maximum.x <= minimum.x || maximum.y <= minimum.y)
            {
                return;
            }
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::PushClipRect(minimum, maximum, true);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::SetCursorScreenPos(minimum);
            Text(text);
            ImGui::PopStyleColor();
            ImGui::PopClipRect();
            ImGui::SetCursorScreenPos(cursor);
        }
    }
    void ScriptSummaryHeader(const char* filename, const char* path, UInt32 fields, UInt32 functions, const char* status, Bool warning)
    {
        const Float height = ImGui::GetTextLineHeight();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const Float width = ImGui::GetContentRegionAvail().x;
        if (EditorTheme::HasIconFont())
        {
            DrawGlyphCentered(Icons::File, at, ImVec2(at.x + height, at.y + height), ImGui::GetColorU32(EditorTheme::InkMuted));
        }
        ClippedText(filename, ImVec2(at.x + height + 6.0f, at.y), ImVec2(at.x + width, at.y + height), ImGui::GetColorU32(EditorTheme::Ink));
        ImGui::Dummy(ImVec2(width, height));
        HoveredTooltip(path);
        HintTextF(Loc::TextOr(LocKeys::ScriptSummaryCounts, "%u fields  /  %u functions"), fields, functions);
        if (status != nullptr && status[0] != '\0')
        {
            const ImVec4 color = warning ? EditorTheme::Amber : EditorTheme::InkMuted;
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            WrappedText(status);
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
    }
    Bool BeginScriptOutline()
    {
        const Float height = Clamp(ImGui::GetContentRegionAvail().y * 0.35f, 140.0f, 240.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f, 5.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorTheme::Workspace);
        return ImGui::BeginChild("##scriptOutline", ImVec2(0.0f, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    }
    void EndScriptOutline()
    {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::Spacing();
    }
    Bool ScriptOutline(ArrayView<const ScriptOutlineItem> items, String& selected)
    {
        Bool changed = false;
        Array<Bool> expanded;
        for (const auto& item : items)
        {
            while (expanded.Size() > item.depth.Get())
            {
                expanded.RemoveAt(expanded.Size() - 1);
            }
            Bool visible = true;
            for (Bool open : expanded)
            {
                visible &= open;
            }
            Bool open = false;
            if (visible)
            {
                ImGui::PushID(item.key.c_str());
                if (item.depth != 0)
                {
                    ImGui::Indent(item.depth.Get() * 12.0f);
                }
                const Bool type = item.kind == Code::SummaryKind::Type;
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen
                    | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_OpenOnArrow;
                if (selected == item.key)
                {
                    flags |= ImGuiTreeNodeFlags_Selected;
                }
                if (!type)
                {
                    flags |= ImGuiTreeNodeFlags_Leaf;
                }
                else
                {
                    flags |= ImGuiTreeNodeFlags_DefaultOpen;
                }
                TreeDrawContext row;
                open = TreeBegin("##entry", flags, &row);
                TreeEnd();
                const Bool clicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
                HoveredTooltip(item.tooltip.c_str());
                if (row.IsVisible)
                {
                    const ImVec2 origin = row.ContentRect.Min;
                    const ImVec2 maximum = row.ContentRect.Max;
                    const Float iconSize = ImGui::GetTextLineHeight();
                    if (EditorTheme::HasIconFont())
                    {
                        DrawGlyphCentered(KindIcon(item.kind), origin, ImVec2(origin.x + iconSize, origin.y + iconSize),
                            ImGui::GetColorU32(EditorTheme::InkMuted));
                    }
                    const Float nameX = origin.x + iconSize + 5.0f;
                    const Float nameWidth = ImGui::CalcTextSize(item.name.c_str()).x;
                    const Float detailWidth = ImGui::CalcTextSize(item.detail.c_str()).x;
                    const Float detailX = Max(nameX + nameWidth + 12.0f, maximum.x - detailWidth - 8.0f);
                    ClippedText(item.name.c_str(), ImVec2(nameX, origin.y), ImVec2(Min(detailX - 8.0f, maximum.x - 6.0f), maximum.y), ImGui::GetColorU32(EditorTheme::Ink));
                    if (detailX < maximum.x - 25.0f)
                    {
                        ClippedText(item.detail.c_str(), ImVec2(detailX, origin.y), ImVec2(maximum.x - 6.0f, maximum.y), ImGui::GetColorU32(EditorTheme::InkMuted));
                    }
                }
                if (clicked)
                {
                    selected = item.key;
                    changed = true;
                }
                if (item.depth != 0)
                {
                    ImGui::Unindent(item.depth.Get() * 12.0f);
                }
                ImGui::PopID();
            }
            if (item.kind == Code::SummaryKind::Type)
            {
                expanded.Add(open);
            }
        }
        return changed;
    }
    Bool BeginScriptDetails()
    {
        return ImGui::BeginChild("##scriptDetails", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    }
    void EndScriptDetails()
    {
        ImGui::EndChild();
    }
    void ScriptMemberHeader(const char* name, const char* type, const char* qualifiers, Code::SummaryKind kind)
    {
        if (EditorTheme::HasIconFont())
        {
            InlineIcon(KindIcon(kind), ImGui::GetColorU32(EditorTheme::InkMuted));
        }
        Text(name);
        ImGui::SameLine(0.0f, 8.0f);
        HintText(type);
        if (qualifiers != nullptr && qualifiers[0] != '\0')
        {
            HintText(qualifiers);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }
    void ScriptSummaryNote(const char* text, Bool error)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, error ? EditorTheme::Danger : EditorTheme::InkMuted);
        WrappedText(text);
        ImGui::PopStyleColor();
    }
    Bool ScriptInitializerField(const char* id, String& text, Bool& present, Bool invalid)
    {
        IdScope scope(id);
        if (!present)
        {
            if (Button(Loc::TextOr(LocKeys::ScriptSummaryAddInitializer, "Add initializer"), Icons::Plus))
            {
                present = true;
            }
            return false;
        }
        const Float buttonWidth = ImGui::GetFrameHeight();
        const Float width = Max(40.0f, ImGui::GetContentRegionAvail().x - buttonWidth - 4.0f);
        const Bool committed = TextField("##value", text).Width(width).CommitOnFinish().Invalid(invalid).Draw();
        ImGui::SameLine(0.0f, 4.0f);
        const Bool removed = IconButton("##remove", Icons::Xmark).Size(ImVec2(buttonWidth, buttonWidth))
            .Tooltip(Loc::TextOr(LocKeys::ScriptSummaryRemoveInitializer, "Remove initializer")).Draw();
        if (removed)
        {
            present = false;
        }
        return committed || removed;
    }
}
