#include "AssetBrowserPanel.h"

#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/TextField.h>
#include <JBro/Editor/Widget/Tree.h>

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace JBro
{
    namespace
    {
        // 끌고 다니는 꾸러미의 이름이다. 에셋 브라우저 안에서만 받는다.
        constexpr const char* AssetDragPayload = "JBRO_ASSET_MOVE";
        // 상대경로가 이보다 길면 꾸러미에 담지 않는다. 담을 수 없는 것을 끌게 두면
        // 놓는 순간 엉뚱한 파일이 움직인다.
        constexpr std::size_t MaxDragPath = 260;

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

    bool AssetBrowserPanel::IsInside(const String& path, const String& folder)
    {
        if (folder.empty())
        {
            return true;
        }
        const std::string_view where = path.View();
        return where == folder.View()
            || (where.size() > folder.size() && where.substr(0, folder.size()) == folder.View()
                && where[folder.size()] == '/');
    }

    void AssetBrowserPanel::Collect()
    {
        const AssetRegistry& registry = m_editor->GetAssetRegistry();
        // 레지스트리는 편집 시점에만 바뀐다. 판번호가 같으면 지난 프레임의 목록이 그대로 맞다 - 레코드 포인터도.
        if (m_collected && m_collectedRevision == registry.GetRevision())
        {
            return;
        }
        m_collected = true;
        m_collectedRevision = registry.GetRevision();
        m_entries.Clear();
        m_folders.Clear();
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

        // 열어 둔 폴더가 그 사이에 사라졌으면 뿌리로 돌아간다. 없는 폴더를 열어 두면
        // 오른쪽 칸이 영원히 비어 있고 왜 그런지 화면에서 알 수 없다.
        if (false == m_openFolder.empty())
        {
            bool alive = false;
            for (std::size_t index = 0; index < m_folders.Size() && false == alive; ++index)
            {
                alive = m_folders[index] == m_openFolder;
            }
            if (false == alive)
            {
                m_openFolder.clear();
            }
        }
    }

    bool AssetBrowserPanel::FolderHasMatch(const String& folder) const
    {
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            const Entry& entry = m_entries[index];
            if (IsInside(entry.folder, folder)
                && Widget::MatchesFilter(entry.name, m_filter.c_str()))
            {
                return true;
            }
        }
        return false;
    }

    void AssetBrowserPanel::DrawFolderDropTarget(const String& folder)
    {
        if (false == ImGui::BeginDragDropTarget())
        {
            return;
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragPayload))
        {
            const String moved(static_cast<const char*>(payload->Data));
            // **자기 안으로는 못 들어간다.** 폴더를 자기 아래로 옮기면 그 가지가 통째로 사라진다.
            if (false == IsInside(folder, moved))
            {
                if (false == m_editor->MoveAsset(moved.c_str(), folder.c_str()))
                {
                    m_message = Loc::TextOr(LocKeys::AssetsMoveFailed, "that could not be moved");
                }
            }
        }
        ImGui::EndDragDropTarget();
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

        // 파일을 끌어 폴더에 놓을 수 있다. 꾸러미에는 상대경로를 담는다 -
        // 레코드 포인터는 다시 스캔하면 다른 것을 가리킨다.
        if (entry.record->relativePath.size() < MaxDragPath
            && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
        {
            ImGui::SetDragDropPayload(AssetDragPayload, entry.record->relativePath.c_str(),
                entry.record->relativePath.size() + 1);
            ImGui::TextUnformatted(entry.name);
            ImGui::EndDragDropSource();
        }
        DrawEntryMenu(entry.record->relativePath, false);

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

    void AssetBrowserPanel::DrawFolderTree(const String& folder)
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
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
            if (m_openFolder == child)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            Widget::TreeDrawContext row;
            const bool opened = Widget::TreeBegin("##folder", flags, &row);
            Widget::TreeEnd();
            const bool clicked = ImGui::IsItemClicked() && false == ImGui::IsItemToggledOpen();
            DrawFolderDropTarget(child);
            DrawEntryMenu(child, true);
            if (row.IsVisible)
            {
                const ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(row.ContentRect.Min);
                ImGui::TextUnformatted(LeafOf(child));
                ImGui::SetCursorScreenPos(cursor);
            }
            if (clicked)
            {
                m_openFolder = child;
            }
            if (opened)
            {
                DrawFolderTree(child);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    void AssetBrowserPanel::DrawBreadcrumb()
    {
        // 뿌리부터 지금 연 폴더까지. 누르면 그 자리로 간다.
        if (Widget::TextButton(Loc::TextOr(LocKeys::AssetsRoot, "Assets")))
        {
            m_openFolder.clear();
        }
        if (m_openFolder.empty())
        {
            return;
        }
        // 조상을 모아 뒤집는다. 경로는 뒤에서 앞으로만 자를 수 있다.
        Array<String> chain;
        for (String walk = m_openFolder; false == walk.empty(); walk = ParentOf(walk))
        {
            chain.Add(walk);
        }
        for (std::size_t step = chain.Size(); step > 0; --step)
        {
            const String& part = chain[step - 1];
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextDisabled("/");
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::PushID(part.c_str());
            if (Widget::TextButton(LeafOf(part)))
            {
                m_openFolder = part;
            }
            ImGui::PopID();
        }
    }

    void AssetBrowserPanel::DrawContents()
    {
        // 지금 연 폴더의 하위 폴더를 먼저, 그다음 파일을.
        const bool searching = m_filter.size() > 0;
        for (std::size_t index = 0; index < m_folders.Size(); ++index)
        {
            const String& child = m_folders[index];
            if (ParentOf(child) != m_openFolder
                || (searching && false == FolderHasMatch(child)))
            {
                continue;
            }
            ImGui::PushID(child.c_str());
            Widget::TreeDrawContext row;
            Widget::TreeBegin("##sub",
                ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
                    | ImGuiTreeNodeFlags_SpanAvailWidth, &row);
            Widget::TreeEnd();
            const bool clicked = ImGui::IsItemClicked();
            DrawFolderDropTarget(child);
            DrawEntryMenu(child, true);
            if (row.IsVisible)
            {
                const ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(row.ContentRect.Min);
                // 폴더임을 글자로 말한다. 아이콘 글꼴이 없어도 갈린다.
                ImGui::Text("%s/", LeafOf(child));
                ImGui::SetCursorScreenPos(cursor);
            }
            if (clicked)
            {
                m_openFolder = child;
            }
            ImGui::PopID();
        }

        bool any = false;
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            const Entry& entry = m_entries[index];
            // 찾는 중이면 폴더를 가리지 않는다 - 어디에 있든 걸린 것을 보여 주는 쪽이 찾는 일이다.
            const bool here = searching
                ? IsInside(entry.folder, m_openFolder)
                : entry.folder == m_openFolder;
            if (false == here || false == Widget::MatchesFilter(entry.name, m_filter.c_str()))
            {
                continue;
            }
            any = true;
            DrawFile(entry);
        }
        if (false == any)
        {
            ImGui::TextDisabled("%s",
                Loc::TextOr(LocKeys::AssetsFolderEmpty, "this folder has no assets"));
        }
    }

    void AssetBrowserPanel::DrawBackgroundMenu()
    {
        if (ImGui::MenuItem(Loc::TextOr(LocKeys::AssetsNewFolder, "New Folder")))
        {
            m_pending = m_openFolder;
            m_pendingIsFolder = true;
            m_nameBuffer = "New Folder";
            m_openNewFolder = true;
        }
        if (ImGui::MenuItem(Loc::TextOr(LocKeys::AssetsReveal, "Show in Explorer")))
        {
            m_editor->RevealAsset(m_openFolder.c_str());
        }
        ImGui::Separator();
        if (ImGui::MenuItem(Loc::TextOr(LocKeys::AssetsRescan, "Rescan")))
        {
            // 감시가 서지 않은 자리(폴더가 없다가 생긴 경우)에서 사람이 새로 고치는 길이다.
            m_editor->RescanAssets();
        }
    }

    void AssetBrowserPanel::DrawEntryMenu(const String& relativePath, bool isFolder)
    {
        if (false == ImGui::BeginPopupContextItem("##AssetMenu"))
        {
            return;
        }
        if (ImGui::MenuItem(Loc::TextOr(LocKeys::AssetsRename, "Rename")))
        {
            m_pending = relativePath;
            m_pendingIsFolder = isFolder;
            m_nameBuffer = LeafOf(relativePath);
            m_openRename = true;
        }
        if (ImGui::MenuItem(Loc::TextOr(LocKeys::AssetsDelete, "Delete")))
        {
            m_pending = relativePath;
            m_pendingIsFolder = isFolder;
            m_openDelete = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(Loc::TextOr(LocKeys::AssetsReveal, "Show in Explorer")))
        {
            m_editor->RevealAsset(relativePath.c_str());
        }
        ImGui::EndPopup();
    }

    void AssetBrowserPanel::DrawRenamePopup()
    {
        if (m_openRename)
        {
            ImGui::OpenPopup("##RenameAsset");
            m_openRename = false;
        }
        if (false == ImGui::BeginPopupModal("##RenameAsset", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        ImGui::TextUnformatted(Loc::TextOr(LocKeys::AssetsRename, "Rename"));
        ImGui::TextDisabled("%s", m_pending.c_str());
        ImGui::Spacing();
        Widget::TextField("##newName", m_nameBuffer).Width(260.0f).Draw();
        ImGui::Spacing();
        const bool valid = false == m_nameBuffer.empty();
        {
            Widget::DisableScope disabled(false == valid);
            if (ImGui::Button(Loc::TextOr(LocKeys::CommonOk, "OK")))
            {
                if (false == m_editor->RenameAsset(m_pending.c_str(), m_nameBuffer.c_str()))
                {
                    m_message = Loc::TextOr(LocKeys::AssetsRenameFailed,
                        "that name is taken, or the file could not be renamed");
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button(Loc::TextOr(LocKeys::CommonCancel, "Cancel")))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void AssetBrowserPanel::DrawDeletePopup()
    {
        if (m_openDelete)
        {
            ImGui::OpenPopup("##DeleteAsset");
            m_openDelete = false;
        }
        if (false == ImGui::BeginPopupModal("##DeleteAsset", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        // **되돌릴 수 없다.** 그러니 무엇을 지우는지 보여 주고 묻는다.
        ImGui::TextUnformatted(m_pendingIsFolder
            ? Loc::TextOr(LocKeys::AssetsDeleteFolderAsk, "delete this folder and everything in it?")
            : Loc::TextOr(LocKeys::AssetsDeleteAsk, "delete this asset?"));
        ImGui::TextDisabled("%s", m_pending.c_str());
        ImGui::Spacing();
        if (ImGui::Button(Loc::TextOr(LocKeys::AssetsDelete, "Delete")))
        {
            if (false == m_editor->DeleteAsset(m_pending.c_str()))
            {
                m_message = Loc::TextOr(LocKeys::AssetsDeleteFailed, "that could not be deleted");
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button(Loc::TextOr(LocKeys::CommonCancel, "Cancel")))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void AssetBrowserPanel::DrawNewFolderPopup()
    {
        if (m_openNewFolder)
        {
            ImGui::OpenPopup("##NewFolder");
            m_openNewFolder = false;
        }
        if (false == ImGui::BeginPopupModal("##NewFolder", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        ImGui::TextUnformatted(Loc::TextOr(LocKeys::AssetsNewFolder, "New Folder"));
        ImGui::Spacing();
        Widget::TextField("##folderName", m_nameBuffer).Width(260.0f).Draw();
        ImGui::Spacing();
        {
            Widget::DisableScope disabled(m_nameBuffer.empty());
            if (ImGui::Button(Loc::TextOr(LocKeys::CommonOk, "OK")))
            {
                if (false == m_editor->CreateAssetFolder(m_pending.c_str(), m_nameBuffer.c_str()))
                {
                    m_message = Loc::TextOr(LocKeys::AssetsNewFolderFailed,
                        "that folder could not be made");
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button(Loc::TextOr(LocKeys::CommonCancel, "Cancel")))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
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
            .Width(200.0f)
            .Draw();
        ImGui::SameLine(0.0f, 12.0f);
        DrawBreadcrumb();
        if (false == m_editor->IsWatchingAssets())
        {
            // 감시가 서지 않았거나 워커가 죽었다. 사용자가 "왜 반영이 안 되지" 로 겪지 않게 말한다.
            Widget::StatusBadge(Loc::TextOr(LocKeys::AssetsNotWatching, "the asset folder is not being watched"))
                .Level(Widget::Severity::Warning)
                .Draw();
        }
        if (false == m_message.empty())
        {
            Widget::ValidationMessage(Widget::Severity::Error, m_message.c_str()).Draw();
        }
        ImGui::Separator();

        Collect();
        if (m_entries.IsEmpty() && m_folders.IsEmpty())
        {
            ImGui::TextDisabled("%s", Loc::TextOr(LocKeys::AssetsEmpty, "the asset folder has no files"));
            // 파일이 하나도 없어도 폴더는 만들 수 있어야 한다.
            if (ImGui::BeginPopupContextWindow("##AssetsBackground",
                    ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                DrawBackgroundMenu();
                ImGui::EndPopup();
            }
            DrawNewFolderPopup();
            return;
        }

        // **두 칸이다.** 왼쪽은 폴더 나무, 오른쪽은 지금 연 폴더의 내용.
        const float available = ImGui::GetContentRegionAvail().x;
        const float treeWidth = std::clamp(m_treeWidth, 80.0f, (std::max)(80.0f, available - 120.0f));
        if (ImGui::BeginChild("##tree", ImVec2(treeWidth, 0.0f), ImGuiChildFlags_Borders))
        {
            // 뿌리 줄. 여기에 놓으면 에셋 폴더의 맨 위로 옮긴다.
            ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_Leaf
                | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (m_openFolder.empty())
            {
                rootFlags |= ImGuiTreeNodeFlags_Selected;
            }
            Widget::TreeDrawContext rootRow;
            Widget::TreeBegin("##root", rootFlags, &rootRow);
            Widget::TreeEnd();
            const bool rootClicked = ImGui::IsItemClicked();
            DrawFolderDropTarget(String());
            if (rootRow.IsVisible)
            {
                const ImVec2 cursor = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(rootRow.ContentRect.Min);
                ImGui::TextUnformatted(Loc::TextOr(LocKeys::AssetsRoot, "Assets"));
                ImGui::SetCursorScreenPos(cursor);
            }
            if (rootClicked)
            {
                m_openFolder.clear();
            }
            DrawFolderTree(String());
        }
        ImGui::EndChild();

        ImGui::SameLine(0.0f, 0.0f);
        Widget::Splitter("##assetSplit", true, 4.0f, &m_treeWidth,
            80.0f, (std::max)(120.0f, available - 120.0f));
        ImGui::SameLine(0.0f, 0.0f);

        if (ImGui::BeginChild("##contents", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            DrawContents();
            if (ImGui::BeginPopupContextWindow("##AssetsBackground",
                    ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                DrawBackgroundMenu();
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();

        // 묻는 창은 칸 밖에서 연다. 칸 안에서 열면 그 칸에 갇혀 잘린다.
        DrawRenamePopup();
        DrawDeletePopup();
        DrawNewFolderPopup();
    }
}
