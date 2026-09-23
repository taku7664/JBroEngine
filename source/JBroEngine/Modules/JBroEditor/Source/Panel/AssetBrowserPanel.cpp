#include "AssetBrowserPanel.h"

#include <JBro/Editor/Widget/AssetDrag.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorPaths.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/TextField.h>
#include <JBro/Editor/Widget/Tree.h>
#include <JBro/Editor/EditorUI.h>

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace JBro
{
    namespace
    {
        // 상대경로가 이보다 길면 꾸러미에 담지 않는다. 담을 수 없는 것을 끌게 두면
        // 놓는 순간 엉뚱한 파일이 움직인다.
        constexpr std::size_t MaxDragPath = 260;
        // 여럿을 담은 꾸러미의 한도다. 넘으면 여럿을 담지 않고 끄는 것 하나만 담는다 -
        // 잘린 묶음을 놓으면 고른 것 중 일부만 움직이고, 어디까지 갔는지 화면에 없다.
        constexpr std::size_t MaxDragBundle = 8192;

        // "art/enemies" 의 부모는 "art", "art" 의 부모는 "" 다.
        String ParentOf(const String& folder)
        {
            const std::size_t slash = folder.View().rfind('/');
            return slash == std::string_view::npos ? String() : folder.Substr(0, slash);
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
            entry.name = EditorPaths::LeafOfPath(record.relativePath);
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
        Widget::AssetDragHeader header;
        String bundle;
        if (Widget::AcceptAssetDrop(header, &bundle))
        {
            // 꾸러미에는 여럿이 줄로 갈려 들어 있다. 하나든 여럿이든 같은 길로 푼다.
            std::size_t start = 0;
            bool failed = false;
            while (start <= bundle.size())
            {
                const std::size_t breakAt = bundle.View().find('\n', start);
                const std::size_t until =
                    breakAt == std::string_view::npos ? bundle.size() : breakAt;
                const String moved = bundle.Substr(start, until - start);
                // **자기 안으로는 못 들어간다.** 폴더를 자기 아래로 옮기면 그 가지가 통째로 사라진다.
                if (false == moved.empty() && false == IsInside(folder, moved))
                {
                    if (false == m_editor->MoveAsset(moved.c_str(), folder.c_str()))
                    {
                        failed = true;
                    }
                }
                if (breakAt == std::string_view::npos)
                {
                    break;
                }
                start = breakAt + 1;
            }
            if (failed)
            {
                m_message = Loc::TextOr(LocKeys::AssetsMoveFailed, "that could not be moved");
            }
            m_selection.Clear();
            m_anchor.clear();
        }
        ImGui::EndDragDropTarget();
    }

    bool AssetBrowserPanel::IsSelected(const String& path) const
    {
        for (std::size_t index = 0; index < m_selection.Size(); ++index)
        {
            if (m_selection[index] == path)
            {
                return true;
            }
        }
        return false;
    }

    void AssetBrowserPanel::SelectOnly(const String& path)
    {
        m_selection.Clear();
        m_selection.Add(path);
    }

    void AssetBrowserPanel::ToggleSelected(const String& path)
    {
        for (std::size_t index = 0; index < m_selection.Size(); ++index)
        {
            if (m_selection[index] == path)
            {
                m_selection.RemoveAt(index);
                return;
            }
        }
        m_selection.Add(path);
    }

    void AssetBrowserPanel::SelectRange(const String& to)
    {
        // **보이는 차례를 쓴다.** 레지스트리의 차례가 아니라 지금 그려진 줄의 차례라야
        // 사람이 고른 것과 고른 결과가 같다 - 걸러 낸 줄이 사이에 끼면 둘이 갈린다.
        std::size_t from = m_visible.Size();
        std::size_t until = m_visible.Size();
        for (std::size_t index = 0; index < m_visible.Size(); ++index)
        {
            if (m_visible[index] == m_anchor)
            {
                from = index;
            }
            if (m_visible[index] == to)
            {
                until = index;
            }
        }
        if (from >= m_visible.Size() || until >= m_visible.Size())
        {
            // 닻이 이 화면에 없다(폴더를 옮겼거나 걸러졌다). 누른 것 하나만 고른다.
            SelectOnly(to);
            return;
        }
        if (from > until)
        {
            const std::size_t swap = from;
            from = until;
            until = swap;
        }
        m_selection.Clear();
        for (std::size_t index = from; index <= until; ++index)
        {
            m_selection.Add(m_visible[index]);
        }
    }

    void AssetBrowserPanel::DeleteSelection()
    {
        // **지우는 도중에 목록이 바뀐다.** 지울 때마다 레지스트리를 다시 훑으므로,
        // 고른 것을 먼저 베껴 두고 그 사본으로 돈다.
        Array<String> targets;
        for (std::size_t index = 0; index < m_selection.Size(); ++index)
        {
            targets.Add(m_selection[index]);
        }
        if (targets.IsEmpty() && false == m_pending.empty())
        {
            targets.Add(m_pending);
        }
        bool failed = false;
        for (std::size_t index = 0; index < targets.Size(); ++index)
        {
            if (false == m_editor->DeleteAsset(targets[index].c_str()))
            {
                failed = true;
            }
        }
        if (failed)
        {
            m_message = Loc::TextOr(LocKeys::AssetsDeleteFailed, "that could not be deleted");
        }
        m_selection.Clear();
        m_anchor.clear();
    }

    Array<String> AssetBrowserPanel::TargetsFor(const String& relativePath) const
    {
        Array<String> targets;
        // 우클릭한 것이 고른 것들 안에 있으면 고른 것 전부다. 여럿 골라 놓고 그중
        // 하나에 우클릭하는 것은 "이것들에 대해" 라는 뜻이다(오브젝트 메뉴와 같은 규칙).
        if (IsSelected(relativePath))
        {
            for (std::size_t index = 0; index < m_selection.Size(); ++index)
            {
                targets.Add(m_selection[index]);
            }
            return targets;
        }
        targets.Add(relativePath);
        return targets;
    }

    void AssetBrowserPanel::CutToClipboard(const String& relativePath)
    {
        m_fileClipboard = TargetsFor(relativePath);
        m_clipboardIsCut = true;
    }

    void AssetBrowserPanel::CopyToClipboard(const String& relativePath)
    {
        m_fileClipboard = TargetsFor(relativePath);
        m_clipboardIsCut = false;
    }

    void AssetBrowserPanel::PasteIntoFolder(const String& folder)
    {
        if (m_fileClipboard.IsEmpty() || m_editor == nullptr)
        {
            return;
        }
        // **붙이는 도중에 목록이 바뀐다.** 하나씩 옮기거나 복사할 때마다 레지스트리를
        // 다시 훑으므로, 클립보드를 먼저 베껴 두고 그 사본으로 돈다.
        Array<String> sources = m_fileClipboard;
        bool failed = false;
        for (std::size_t index = 0; index < sources.Size(); ++index)
        {
            const String& source = sources[index];
            // 제자리로 옮기는 것은 아무 일도 아니다. 복사는 제자리여도 복제로서 뜻이 있다.
            if (m_clipboardIsCut && EditorPaths::FolderOf(source.c_str()) == folder)
            {
                continue;
            }
            const bool moved = m_clipboardIsCut
                ? m_editor->MoveAsset(source.c_str(), folder.c_str())
                : false == m_editor->CopyAssetInto(source.c_str(), folder.c_str()).empty();
            if (false == moved)
            {
                failed = true;
            }
        }
        if (failed)
        {
            m_message = Loc::TextOr(LocKeys::AssetsPasteFailed, "that could not be pasted");
        }
        if (m_clipboardIsCut)
        {
            // **잘라낸 것은 한 번만 간다.** 남겨 두면 다음 붙여넣기가 이미 없는 파일을 찾는다.
            m_fileClipboard.Clear();
            m_selection.Clear();
            m_anchor.clear();
        }
    }

    void AssetBrowserPanel::HandleEntryInput(const Entry& entry)
    {
        // **누를 때가 아니라 끌지 않고 뗄 때 고른다**(D-154). 누르는 순간 고르면, 에셋을 끌어
        // 인스펙터의 칸에 놓으려는 손짓이 시작하자마자 인스펙터를 그 에셋의 화면으로 바꿔
        // 놓을 칸이 사라진다.
        //
        // **누른 줄에서 뗐을 때만이다.** 뗀 자리만 보면, 폴더를 눌러 연 순간 같은 자리에 새로 나타난
        // 파일이 골라진다(실제 에디터에서 그랬다). 누를 때 그 줄을 기억해 두고 뗄 때 견준다.
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            m_pressedPath = entry.record->relativePath;
        }
        const bool clicked = ImGui::IsItemHovered()
            && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
            && false == Widget::MouseWasDragged(ImGuiMouseButton_Left)
            && m_pressedPath == entry.record->relativePath;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && clicked)
        {
            m_pressedPath.clear();
        }
        // 오른쪽 누름은 **고른 것을 뒤엎지 않는다.** 여럿을 골라 놓고 그중 하나에 대고
        // 메뉴를 열었을 때 선택이 하나로 줄면, 여럿에 하려던 일이 하나에만 간다.
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)
            && false == IsSelected(entry.record->relativePath))
        {
            SelectOnly(entry.record->relativePath);
            m_anchor = entry.record->relativePath;
        }

        // 파일을 끌어 폴더에 놓을 수 있다. 꾸러미에는 상대경로를 담는다 -
        // 레코드 포인터는 다시 스캔하면 다른 것을 가리킨다.
        if (entry.record->relativePath.size() < MaxDragPath
            && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
        {
            // 끄는 것이 고른 것 안에 있으면 **고른 것이 다 간다**. 밖에 있으면 그것 하나다 -
            // 고르지 않은 줄을 끌었는데 엉뚱한 파일이 따라가면 안 된다.
            String bundle;
            std::size_t count = 0;
            if (IsSelected(entry.record->relativePath))
            {
                for (std::size_t index = 0; index < m_selection.Size(); ++index)
                {
                    if (bundle.size() + m_selection[index].size() + 1 >= MaxDragBundle)
                    {
                        // 담을 수 없는 것을 끌게 두면 놓는 순간 일부만 움직인다.
                        bundle.clear();
                        count = 0;
                        break;
                    }
                    if (count > 0)
                    {
                        bundle += "\n";
                    }
                    bundle += m_selection[index];
                    ++count;
                }
            }
            if (count == 0)
            {
                bundle = entry.record->relativePath;
                count = 1;
            }
            // 에셋 칸에 놓을 수도 있다(D-154). 칸은 경로가 아니라 아이디를 보므로 함께 싣는다 -
            // 그림이면 짝 Sprite 도 싣는다. 스프라이트 칸이 받는 것은 그쪽이다.
            AssetId paired;
            const AssetRegistry& registry = m_editor->GetAssetRegistry();
            for (std::size_t index = 0; index < registry.GetCount(); ++index)
            {
                const AssetRecord& record = registry.GetRecord(index);
                if (record.owner == entry.record->id)
                {
                    paired = record.id;
                    break;
                }
            }
            Widget::SetAssetDragPayload(entry.record->id, paired, bundle);
            if (count > 1)
            {
                Widget::TextF("%s +%d", entry.name, static_cast<int>(count - 1));
            }
            else
            {
                Widget::Text(entry.name);
            }
            ImGui::EndDragDropSource();
        }
        DrawEntryMenu(entry.record->relativePath, false);
        // 그림을 두 번 누르면 스프라이트 뷰어에서 연다(D-155). 기존 브라우저도 그랬다.
        // **캔버스는 편집하러 열린다**(D-174) - 기존도 두 번 누르는 것이 그 길이었다.
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            if (AssetTypeRules::IsImageType(entry.record->type))
            {
                m_editor->OpenSpriteViewer(entry.record->id);
            }
            else if (entry.record->type == AssetType::Canvas)
            {
                m_editor->RequestOpenCanvas(entry.record->relativePath.c_str());
            }
        }

        if (false == clicked)
        {
            return;
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyShift && false == m_anchor.empty())
        {
            SelectRange(entry.record->relativePath);
        }
        else if (io.KeyCtrl)
        {
            ToggleSelected(entry.record->relativePath);
            m_anchor = entry.record->relativePath;
        }
        else
        {
            SelectOnly(entry.record->relativePath);
            m_anchor = entry.record->relativePath;
        }
        // **인스펙터는 마지막에 누른 것 하나를 본다.** 여럿의 임포트 옵션을 한 화면에
        // 겹쳐 보이면 어느 값이 어느 파일의 것인지 알 수 없다.
        m_editor->SetSelectedAsset(entry.record->id);
    }

    void AssetBrowserPanel::DrawFileTile(const Entry& entry)
    {
        ImGui::PushID(entry.record->relativePath.c_str());
        const float side = m_iconSize;
        // 이름 한 줄의 자리를 밑에 둔다. 이름이 없으면 무엇을 고르는지 그림만으로 가려야 한다.
        const float labelHeight = ImGui::GetTextLineHeight();
        const ImVec2 cell(side, side + labelHeight + 6.0f);
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        ImGui::SetNextItemAllowOverlap();
        Widget::HitArea("##tile", cell,
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        HandleEntryInput(entry);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 imageMin(origin.x + 4.0f, origin.y + 2.0f);
        const ImVec2 imageMax(origin.x + side - 4.0f, origin.y + side - 4.0f);
        if (IsSelected(entry.record->relativePath))
        {
            draw->AddRectFilled(origin, ImVec2(origin.x + cell.x, origin.y + cell.y),
                ImGui::GetColorU32(ImGuiCol_Header), 3.0f);
        }
        const TextureHandle thumbnail = m_editor->GetAssetThumbnail(entry.record->id);
        if (thumbnail.IsValid())
        {
            // 칸 가운데에 비율을 지켜 넣는다(D-159). 늘여 붙이면 가로로 긴 시트가 찌그러진다.
            std::uint32_t sourceWidth = 0;
            std::uint32_t sourceHeight = 0;
            m_editor->GetAssetSourceSize(entry.record->id, sourceWidth, sourceHeight);
            const ImVec2 box(imageMax.x - imageMin.x, imageMax.y - imageMin.y);
            const ImVec2 fitted = Widget::FitInside(sourceWidth, sourceHeight, box);
            const ImVec2 fittedMin(imageMin.x + (box.x - fitted.x) * 0.5f, imageMin.y + (box.y - fitted.y) * 0.5f);
            draw->AddImage(static_cast<ImTextureID>(EditorUI::ToTextureId(thumbnail)),
                fittedMin, ImVec2(fittedMin.x + fitted.x, fittedMin.y + fitted.y));
        }
        else
        {
            // **그림이 없는 것도 자리를 지킨다.** 빈 칸이 접히면 목록이 프레임마다 움직인다.
            draw->AddRect(imageMin, imageMax, ImGui::GetColorU32(ImGuiCol_Border), 3.0f);
        }
        // 이름은 칸 안에서 잘린다. 줄바꿈으로 흘리면 칸마다 높이가 달라져 줄이 어긋난다.
        draw->PushClipRect(ImVec2(origin.x, origin.y + side - 2.0f),
            ImVec2(origin.x + cell.x, origin.y + cell.y), true);
        draw->AddText(ImVec2(origin.x + 4.0f, origin.y + side - 2.0f),
            ImGui::GetColorU32(ImGuiCol_Text), entry.name);
        draw->PopClipRect();
        ImGui::PopID();
    }

    void AssetBrowserPanel::DrawFile(const Entry& entry)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
            | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (IsSelected(entry.record->relativePath)
            || m_editor->GetSelectedAsset() == entry.record->id)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        // 줄의 정체는 상대경로다 - 같은 이름의 파일이 다른 폴더에 있어도 갈린다.
        ImGui::PushID(entry.record->relativePath.c_str());
        Widget::TreeDrawContext row;
        Widget::TreeBegin("##file", flags, &row);
        Widget::TreeEnd();
        // **줄 전체가 누름을 받는다.** 이름을 그린 뒤에 물으면 마지막 항목이 글자라 글자 밖의 줄은 눌러도 반응이 없다.
        HandleEntryInput(entry);

        if (row.IsVisible)
        {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(row.ContentRect.Min);
            Widget::Text(entry.name);
            ImGui::SameLine();
            Widget::HintText(AssetTypeRules::GetTypeName(entry.record->type));
            ImGui::SetCursorScreenPos(cursor);
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
                Widget::Text(EditorPaths::LeafOfPath(child));
                ImGui::SetCursorScreenPos(cursor);
            }
            if (clicked)
            {
                m_openFolder = child;
            }
            if (opened)
            {
                DrawFolderTree(child);
                Widget::TreePop();
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
            Widget::HintTextF("/");
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::PushID(part.c_str());
            if (Widget::TextButton(EditorPaths::LeafOfPath(part)))
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
                Widget::TextF("%s/", EditorPaths::LeafOfPath(child));
                ImGui::SetCursorScreenPos(cursor);
            }
            if (clicked)
            {
                m_openFolder = child;
            }
            ImGui::PopID();
        }

        bool any = false;
        std::size_t drawnTiles = 0;
        // 이번 프레임에 그린 차례를 새로 모은다. 범위 선택이 이 차례를 쓴다.
        m_visible.Clear();
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
            m_visible.Add(entry.record->relativePath);
            if (m_iconView)
            {
                // 칸은 오른쪽으로 흐르다 자리가 모자라면 다음 줄로 간다.
                const float remaining = ImGui::GetContentRegionAvail().x;
                if (drawnTiles > 0 && remaining >= m_iconSize)
                {
                    ImGui::SameLine(0.0f, 4.0f);
                }
                DrawFileTile(entry);
                ++drawnTiles;
            }
            else
            {
                DrawFile(entry);
            }
        }
        if (false == any)
        {
            Widget::HintTextF("%s",
                Loc::TextOr(LocKeys::AssetsFolderEmpty, "this folder has no assets"));
        }
    }

    void AssetBrowserPanel::DrawBackgroundMenu()
    {
        // 지금 연 폴더로 가져온다(D-156). 어디로 갈지 따로 묻지 않는다 - 보고 있는 폴더가 그 답이다.
        if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsImport, "Import...")))
        {
            m_editor->RequestImportAsset(m_openFolder.c_str());
        }
        // **새 캔버스**(D-174, 기존 `에셋 추가 ▸ 캔버스`). 만들고 바로 연다 - 만들어 놓고
        // 열리지 않으면 방금 만든 것이 어디 있는지 목록에서 찾아야 한다.
        if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsNewCanvas, "New Canvas")))
        {
            const String created = m_editor->CreateCanvasAsset(m_openFolder.c_str());
            if (false == created.empty())
            {
                m_editor->RequestOpenCanvas(created.c_str());
            }
        }
        if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsNewFolder, "New Folder")))
        {
            m_pending = m_openFolder;
            m_pendingIsFolder = true;
            m_nameBuffer = "New Folder";
            m_openNewFolder = true;
        }
        // 지금 보고 있는 폴더에 붙인다. 빈자리에서 연 메뉴이므로 그 폴더가 곧 목적지다.
        if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsPaste, "Paste"), nullptr,
                false == m_fileClipboard.IsEmpty(),
                Loc::TextOr(LocKeys::BlockedClipboardEmpty, "nothing has been copied")))
        {
            PasteIntoFolder(m_openFolder);
        }
        if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsReveal, "Show in Explorer")))
        {
            m_editor->RevealAsset(m_openFolder.c_str());
        }
        ImGui::Separator();
        if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsRescan, "Rescan")))
        {
            // 감시가 서지 않은 자리(폴더가 없다가 생긴 경우)에서 사람이 새로 고치는 길이다.
            m_editor->RescanAssets();
        }
    }

    void AssetBrowserPanel::DrawEntryMenu(const String& relativePath, bool isFolder)
    {
        if (false == Widget::BeginContextMenu("##AssetMenu"))
        {
            return;
        }
        // **여럿 골랐으면 몇 개인지 먼저 말한다**(D-175, 기존 `AssetBrowserSelectionCount`).
        // 지우기가 고른 것 전체에 가므로(D-141), 몇 개인지 모르고 누르면 놀란다.
        if (m_selection.Size() > 1)
        {
            Widget::HintTextF(
                Loc::TextOr(LocKeys::AssetsSelectionCount, "%d chosen"),
                static_cast<int>(m_selection.Size()));
            ImGui::Separator();
        }
        if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsRename, "Rename")))
        {
            m_pending = relativePath;
            m_pendingIsFolder = isFolder;
            m_nameBuffer = EditorPaths::LeafOfPath(relativePath);
            m_openRename = true;
        }
        // **복제**(D-175, 기존 `Duplicate`). 폴더는 복제하지 않는다 - 안의 파일마다 새 아이디를
        // 매겨야 해서 그냥 복사와 다르다.
        if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsDuplicate, "Duplicate"),
                nullptr, false == isFolder))
        {
            m_editor->DuplicateAsset(relativePath.c_str());
        }
        // ── 잘라내기 · 복사 · 붙여넣기 ─────────────────────────────────
        //
        // 폴더는 담지 않는다. 폴더를 통째로 옮기는 것은 안의 파일마다 아이디를 지켜야
        // 하는 다른 일이고, 그쪽은 끌어 놓기가 이미 한다.
        ImGui::Separator();
        if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsCut, "Cut"), nullptr,
                false == isFolder))
        {
            CutToClipboard(relativePath);
        }
        if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsCopyFile, "Copy"), nullptr,
                false == isFolder))
        {
            CopyToClipboard(relativePath);
        }
        {
            // 폴더에서 열었으면 그 폴더로, 파일에서 열었으면 지금 보고 있는 폴더로 간다.
            const String target = isFolder ? relativePath : m_openFolder;
            if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsPaste, "Paste"), nullptr,
                    false == m_fileClipboard.IsEmpty(),
                    Loc::TextOr(LocKeys::BlockedClipboardEmpty, "nothing has been copied")))
            {
                PasteIntoFolder(target);
            }
        }
        ImGui::Separator();
        if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsDelete, "Delete")))
        {
            m_pending = relativePath;
            m_pendingIsFolder = isFolder;
            m_openDelete = true;
        }
        if (false == isFolder)
        {
            // 그림이면 뷰어에서 열 수 있다. 그림이 아니면 항목을 잠근다 - 숨기면 그런 창이
            // 있다는 것조차 알 수 없다.
            const AssetRecord* record = m_editor->GetAssetRegistry().FindByPath(relativePath.c_str());
            const bool image = record != nullptr && AssetTypeRules::IsImageType(record->type);
            if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsOpenInSpriteViewer,
                    "Open in Sprite Viewer"), nullptr, image))
            {
                m_editor->OpenSpriteViewer(record->id);
            }
            // 캔버스면 그것을 **편집하러 연다**(D-174). 지금 캔버스의 내용이 그것으로 바뀐다.
            const bool canvas = record != nullptr && record->type == AssetType::Canvas;
            if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsOpenCanvas, "Open Canvas"),
                    nullptr, canvas))
            {
                m_editor->RequestOpenCanvas(relativePath.c_str());
            }
        }
        ImGui::Separator();
        if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsReveal, "Show in Explorer")))
        {
            m_editor->RevealAsset(relativePath.c_str());
        }
        // **경로 복사**(D-175, 기존 `CopyPath`). 밖의 프로그램에 그 파일을 넘길 때 쓴다 -
        // 에셋 폴더 기준 상대경로가 아니라 **실제 경로**여야 그쪽이 연다.
        if (Widget::MenuItem(Loc::TextOr(LocKeys::AssetsCopyPath, "Copy Path")))
        {
            const String absolute = EditorPaths::JoinPath(
                m_editor->GetAssetRoot().c_str(), relativePath.c_str());
            ImGui::SetClipboardText(absolute.c_str());
        }
        Widget::EndContextMenu();
    }

    void AssetBrowserPanel::DrawRenamePopup()
    {
        if (m_openRename)
        {
            Widget::OpenModal("##RenameAsset");
            m_openRename = false;
        }
        if (false == Widget::BeginModal("##RenameAsset"))
        {
            return;
        }
        Widget::Text(Loc::TextOr(LocKeys::AssetsRename, "Rename"));
        Widget::HintText(m_pending.c_str());
        ImGui::Spacing();
        Widget::TextField("##newName", m_nameBuffer).Width(260.0f).Draw();
        ImGui::Spacing();
        const bool valid = false == m_nameBuffer.empty();
        {
            Widget::DisableScope disabled(false == valid);
            if (Widget::Button(Loc::TextOr(LocKeys::CommonOk, "OK")))
            {
                if (false == m_editor->RenameAsset(m_pending.c_str(), m_nameBuffer.c_str()))
                {
                    m_message = Loc::TextOr(LocKeys::AssetsRenameFailed,
                        "that name is taken, or the file could not be renamed");
                }
                Widget::CloseModal();
            }
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (Widget::Button(Loc::TextOr(LocKeys::CommonCancel, "Cancel")))
        {
            Widget::CloseModal();
        }
        Widget::EndModal();
    }

    void AssetBrowserPanel::DrawDeletePopup()
    {
        if (m_openDelete)
        {
            Widget::OpenModal("##DeleteAsset");
            m_openDelete = false;
        }
        if (false == Widget::BeginModal("##DeleteAsset"))
        {
            return;
        }
        // **되돌릴 수 없다.** 그러니 무엇을 지우는지 보여 주고 묻는다.
        const bool many = false == m_pendingIsFolder && m_selection.Size() > 1;
        if (many)
        {
            Widget::TextF(Loc::TextOr(LocKeys::AssetsDeleteManyAsk, "delete these %d assets?"),
                static_cast<int>(m_selection.Size()));
            // 무엇이 사라지는지 한 줄씩 보여 준다. 개수만으로는 잘못 고른 것을 알 수 없다.
            for (std::size_t index = 0; index < m_selection.Size(); ++index)
            {
                Widget::HintText(m_selection[index].c_str());
            }
        }
        else
        {
            Widget::Text(m_pendingIsFolder
                ? Loc::TextOr(LocKeys::AssetsDeleteFolderAsk,
                    "delete this folder and everything in it?")
                : Loc::TextOr(LocKeys::AssetsDeleteAsk, "delete this asset?"));
            Widget::HintText(m_pending.c_str());
        }
        ImGui::Spacing();
        if (Widget::Button(Loc::TextOr(LocKeys::AssetsDelete, "Delete")))
        {
            if (m_pendingIsFolder)
            {
                if (false == m_editor->DeleteAsset(m_pending.c_str()))
                {
                    m_message =
                        Loc::TextOr(LocKeys::AssetsDeleteFailed, "that could not be deleted");
                }
            }
            else
            {
                DeleteSelection();
            }
            Widget::CloseModal();
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (Widget::Button(Loc::TextOr(LocKeys::CommonCancel, "Cancel")))
        {
            Widget::CloseModal();
        }
        Widget::EndModal();
    }

    void AssetBrowserPanel::DrawNewFolderPopup()
    {
        if (m_openNewFolder)
        {
            Widget::OpenModal("##NewFolder");
            m_openNewFolder = false;
        }
        if (false == Widget::BeginModal("##NewFolder"))
        {
            return;
        }
        Widget::Text(Loc::TextOr(LocKeys::AssetsNewFolder, "New Folder"));
        ImGui::Spacing();
        Widget::TextField("##folderName", m_nameBuffer).Width(260.0f).Draw();
        ImGui::Spacing();
        {
            Widget::DisableScope disabled(m_nameBuffer.empty());
            if (Widget::Button(Loc::TextOr(LocKeys::CommonOk, "OK")))
            {
                if (false == m_editor->CreateAssetFolder(m_pending.c_str(), m_nameBuffer.c_str()))
                {
                    m_message = Loc::TextOr(LocKeys::AssetsNewFolderFailed,
                        "that folder could not be made");
                }
                Widget::CloseModal();
            }
        }
        ImGui::SameLine(0.0f, 6.0f);
        if (Widget::Button(Loc::TextOr(LocKeys::CommonCancel, "Cancel")))
        {
            Widget::CloseModal();
        }
        Widget::EndModal();
    }

    void AssetBrowserPanel::OnDraw()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        if (false == m_editor->HasOpenProject())
        {
            Widget::HintText(Loc::TextOr(LocKeys::HierarchyNoProject, "no project is open"));
            return;
        }
        Widget::SearchBox("##filter", m_filter)
            .Hint(Loc::TextOr(LocKeys::CommonSearch, "Search"))
            .Width(200.0f)
            .Draw();
        ImGui::SameLine(0.0f, 8.0f);
        if (Widget::Button(m_iconView
                ? Loc::TextOr(LocKeys::AssetsListView, "List")
                : Loc::TextOr(LocKeys::AssetsIconView, "Icons")))
        {
            m_iconView = false == m_iconView;
        }
        Widget::HoveredTooltip(Loc::TextOr(LocKeys::AssetsViewTooltip,
            "switch between the list and the icons"));
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
            Widget::HintText(Loc::TextOr(LocKeys::AssetsEmpty, "the asset folder has no files"));
            // 파일이 하나도 없어도 폴더는 만들 수 있어야 한다.
            if (Widget::BeginContextMenu("##AssetsBackground", true))
            {
                DrawBackgroundMenu();
                Widget::EndContextMenu();
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
                Widget::Text(Loc::TextOr(LocKeys::AssetsRoot, "Assets"));
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
            if (Widget::BeginContextMenu("##AssetsBackground", true))
            {
                DrawBackgroundMenu();
                Widget::EndContextMenu();
            }
        }
        ImGui::EndChild();

        // 뗀 프레임이 지나면 누른 줄을 잊는다. 남겨 두면 다른 곳에서 누르고 그 줄 위에서 뗐을 때
        // 그 줄이 골라진다.
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            m_pressedPath.clear();
        }
        // 묻는 창은 칸 밖에서 연다. 칸 안에서 열면 그 칸에 갇혀 잘린다.
        DrawRenamePopup();
        DrawDeletePopup();
        DrawNewFolderPopup();
    }
}
