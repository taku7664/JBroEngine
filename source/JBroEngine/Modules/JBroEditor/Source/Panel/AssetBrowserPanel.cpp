#include "AssetBrowserPanel.h"

#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/Tree.h>

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace JBro
{
    namespace
    {
        // "art/enemies" 의 부모는 "art", "art" 의 부모는 "" 다.
        String ParentOf(const String& folder)
        {
            const std::size_t slash = folder.View().rfind('/');
            return slash == std::string_view::npos ? String() : folder.Substr(0, slash);
        }

        const char* LeafOf(const String& folder)
        {
            const std::size_t slash = folder.View().rfind('/');
            return slash == std::string_view::npos ? folder.c_str() : folder.c_str() + slash + 1;
        }
    }

    const char* AssetBrowserPanel::GetTitle() const
    {
        return "Assets";
    }

    const char* AssetBrowserPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::PanelAssets, "Assets");
    }

    bool AssetBrowserPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }

    void AssetBrowserPanel::Collect()
    {
        m_entries.Clear();
        m_folders.Clear();
        const AssetRegistry& registry = m_editor->GetAssetRegistry();
        for (std::size_t index = 0; index < registry.GetCount(); ++index)
        {
            const AssetRecord& record = registry.GetRecord(index);
            // 이미지의 Sprite 레코드는 Texture 와 같은 파일이다. 줄은 하나다.
            if (record.type == AssetType::Sprite && false == record.owner.IsNull())
            {
                continue;
            }
            Entry entry;
            entry.record = &record;
            entry.folder = ParentOf(record.relativePath);
            entry.name = LeafOf(record.relativePath);
            m_entries.Add(entry);
            // 조상 폴더까지 전부 등록한다 - 파일이 깊이 있어도 중간 폴더가 나무에 있어야 한다.
            for (String folder = entry.folder; false == folder.empty(); folder = ParentOf(folder))
            {
                bool known = false;
                for (std::size_t at = 0; at < m_folders.Size() && false == known; ++at)
                {
                    known = m_folders[at] == folder;
                }
                if (false == known)
                {
                    m_folders.Add(folder);
                }
            }
        }
        std::sort(m_folders.Data(), m_folders.Data() + m_folders.Size(),
            [](const String& left, const String& right) { return left.View() < right.View(); });
        std::sort(m_entries.Data(), m_entries.Data() + m_entries.Size(),
            [](const Entry& left, const Entry& right) {
                return left.record->relativePath.View() < right.record->relativePath.View();
            });
    }

    bool AssetBrowserPanel::FolderHasMatch(const String& folder) const
    {
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            const Entry& entry = m_entries[index];
            const std::string_view where = entry.folder.View();
            const bool inside = where == folder.View()
                || (where.size() > folder.size() && where.substr(0, folder.size()) == folder.View()
                    && where[folder.size()] == '/');
            if (inside && Widget::MatchesFilter(entry.name, m_filter.c_str()))
            {
                return true;
            }
        }
        return false;
    }

    void AssetBrowserPanel::DrawFile(const Entry& entry)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
            | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (m_editor->GetSelectedAsset() == entry.record->id)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        // 줄의 정체는 상대경로다 - 같은 이름의 파일이 다른 폴더에 있어도 갈린다.
        ImGui::PushID(entry.record->relativePath.c_str());
        Widget::TreeDrawContext row;
        Widget::TreeBegin("##file", flags, &row);
        Widget::TreeEnd();
        // **줄 전체가 누름을 받는다.** 이름을 그린 뒤에 물으면 마지막 항목이 글자라 글자 밖의 줄은 눌러도 반응이 없다.
        const bool clicked = ImGui::IsItemClicked();
        if (row.IsVisible)
        {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(row.ContentRect.Min);
            ImGui::TextUnformatted(entry.name);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", AssetTypeRules::GetTypeName(entry.record->type));
            ImGui::SetCursorScreenPos(cursor);
        }
        if (clicked)
        {
            m_editor->SetSelectedAsset(entry.record->id);
        }
        ImGui::PopID();
    }

    void AssetBrowserPanel::DrawFolder(const String& folder)
    {
        const bool searching = m_filter.size() > 0;
        for (std::size_t index = 0; index < m_folders.Size(); ++index)
        {
            const String& child = m_folders[index];
            if (ParentOf(child) != folder || (searching && false == FolderHasMatch(child)))
            {
                continue;
            }
            ImGui::PushID(child.c_str());
            const bool opened = Widget::TreeEx("##folder",
                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth
                    | ImGuiTreeNodeFlags_DefaultOpen,
                [&]() { ImGui::TextUnformatted(LeafOf(child)); });
            if (opened)
            {
                DrawFolder(child);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            const Entry& entry = m_entries[index];
            if (entry.folder != folder || false == Widget::MatchesFilter(entry.name, m_filter.c_str()))
            {
                continue;
            }
            DrawFile(entry);
        }
    }

    void AssetBrowserPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        if (false == m_editor->HasOpenProject())
        {
            ImGui::TextDisabled("%s", Loc::TextOr(LocKeys::HierarchyNoProject, "no project is open"));
            return;
        }
        Widget::SearchBox("##filter", m_filter)
            .Hint(Loc::TextOr(LocKeys::CommonSearch, "Search"))
            .Draw();
        ImGui::Spacing();

        Collect();
        if (m_entries.IsEmpty())
        {
            ImGui::TextDisabled("%s", Loc::TextOr(LocKeys::AssetsEmpty, "the asset folder has no files"));
            return;
        }
        DrawFolder(String());
    }
}
