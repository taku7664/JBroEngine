#include "SpriteViewerWindow.h"

#include "../Panel/InspectorPanel.h"

#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorUI.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/Scalar.h>

#include <algorithm>

namespace JBro
{
    namespace
    {
        constexpr std::size_t NoTab = static_cast<std::size_t>(-1);
        // 작은 그림은 키워 보인다. 16 픽셀짜리 시트를 16 픽셀로 보이면 칸을 누를 수 없다.
        // 창에 맞춰 키우되 서른두 배에서 멈춘다 - 그 너머로는 한 픽셀이 칸 하나보다 커진다.
        constexpr float MaxSheetZoom = 32.0f;
        constexpr float PreviewMaxSide = 192.0f;
    }

    void SpriteViewerWindow::Initialize(EditorApplication& editor)
    {
        m_editor = &editor;
    }

    void SpriteViewerWindow::Shutdown()
    {
        Clear();
        m_editor = nullptr;
    }

    void SpriteViewerWindow::Clear()
    {
        while (false == m_tabs.IsEmpty())
        {
            CloseTab(m_tabs.Size() - 1);
        }
        m_active = NoTab;
        m_selectNext = NoTab;
    }

    void SpriteViewerWindow::CloseTab(std::size_t index)
    {
        if (index >= m_tabs.Size())
        {
            return;
        }
        // **잡고 있던 스프라이트를 놓는다.** 놓지 않으면 탭을 닫아도 `CollectUnused` 가
        // 그 그림을 영영 내리지 못한다.
        if (AssetSystem* assets = m_editor != nullptr ? m_editor->GetAssetSystem() : nullptr)
        {
            if (assets->IsLoaded(m_tabs[index].spriteHandle))
            {
                assets->Release(m_tabs[index].spriteHandle);
            }
        }
        m_tabs.RemoveAt(index);
        if (m_active != NoTab && m_active >= m_tabs.Size())
        {
            m_active = m_tabs.IsEmpty() ? NoTab : m_tabs.Size() - 1;
        }
    }

    bool SpriteViewerWindow::Open(AssetId asset)
    {
        if (m_editor == nullptr || asset.IsNull())
        {
            return false;
        }
        const AssetRegistry& registry = m_editor->GetAssetRegistry();
        const AssetRecord* record = registry.Find(asset);
        // 그림 파일은 Texture 와 Sprite 두 레코드다. 어느 쪽을 받아도 연다.
        if (record == nullptr
            || (false == AssetTypeRules::IsImageType(record->type) && record->type != AssetType::Sprite))
        {
            return false;
        }
        // Texture 와 Sprite 를 둘 다 찾는다. 시트는 텍스처가, 칸은 스프라이트가 안다.
        AssetId texture = record->type == AssetType::Texture ? record->id : record->owner;
        AssetId sprite = record->type == AssetType::Sprite ? record->id : AssetId{};
        if (sprite.IsNull())
        {
            for (std::size_t index = 0; index < registry.GetCount(); ++index)
            {
                const AssetRecord& candidate = registry.GetRecord(index);
                if (candidate.type == AssetType::Sprite && candidate.owner == texture)
                {
                    sprite = candidate.id;
                    break;
                }
            }
        }
        if (texture.IsNull() || sprite.IsNull())
        {
            return false;
        }

        for (std::size_t index = 0; index < m_tabs.Size(); ++index)
        {
            if (m_tabs[index].texture == texture)
            {
                m_selectNext = index;
                SelectPicture(texture);
                return true;
            }
        }

        AssetSystem* assets = m_editor->GetAssetSystem();
        if (assets == nullptr)
        {
            return false;
        }
        Tab tab;
        tab.texture = texture;
        tab.sprite = sprite;
        // **탭이 열려 있는 동안 스프라이트를 잡는다.** 칸은 스프라이트가 들고, 옵션을 고치면
        // 제자리에서 다시 읽혀(asset-plan §2.7) 같은 핸들로 새 칸이 온다.
        tab.spriteHandle = assets->Load(sprite);
        if (false == assets->IsLoaded(tab.spriteHandle))
        {
            return false;
        }
        const AssetRecord* textureRecord = registry.Find(texture);
        tab.name = textureRecord != nullptr ? textureRecord->relativePath : String("?");
        m_tabs.Add(std::move(tab));
        m_selectNext = m_tabs.Size() - 1;
        // 옵션은 고른 에셋의 것을 고친다. 연 그림을 고른다.
        SelectPicture(texture);
        return true;
    }

    void SpriteViewerWindow::SelectPicture(AssetId texture)
    {
        // **프레임을 고르는 중이면 고른 것을 바꾸지 않는다**(D-165). 인스펙터는 고르는 대상 오브젝트를 계속 보여야 한다.
        if (m_editor->IsSpriteFramePickActive() && m_editor->GetSpriteFramePickTexture() == texture)
        {
            return;
        }
        m_editor->SetSelectedAsset(texture);
    }

    bool SpriteViewerWindow::GetActiveFrame(std::uint32_t& frame) const
    {
        if (m_active == NoTab || m_active >= m_tabs.Size())
        {
            return false;
        }
        frame = m_tabs[m_active].frame;
        return true;
    }

    void SpriteViewerWindow::Draw(ImGuiID rootDock, const ImGuiWindowClass& rootClass)
    {
        if (m_editor == nullptr || m_tabs.IsEmpty())
        {
            m_dockNext = true;
            return;
        }
        // **뿌리 노드에 붙는다.** 메인 도크와 같은 칸에 탭으로 선다 - 기존과 같은 자리다.
        // 도구 창은 뿌리에 붙지 못하게 칸을 나눠 두었으니(D-134) 이 창도 뿌리의 칸을 든다.
        if (m_dockNext)
        {
            ImGui::SetNextWindowDockID(rootDock, ImGuiCond_Always);
            m_dockNext = false;
        }
        ImGui::SetNextWindowClass(&rootClass);
        // **열 때마다 앞으로 꺼낸다**(D-159). 메인 도크 탭 뒤에 가려진 채로 새 탭만 더하면, 두 번 누르기가
        // 아무 일도 하지 않은 것처럼 보인다(실제 에디터에서 그랬다). 가려진 창은 `Begin` 이 거짓이라 탭도
        // 고를 수 없다.
        if (m_selectNext != NoTab)
        {
            ImGui::SetNextWindowFocus();
        }
        // `###` 뒤가 식별자라 언어가 바뀌어도 도킹 자리를 잃지 않는다(D-80).
        String title = Loc::TextOr(LocKeys::SpriteViewerTitle, "Sprite Viewer");
        title.append("###SpriteViewer", 15);
        const bool visible = ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
        if (visible && Widget::BeginTabs("##spriteTabs"))
        {
            const float deltaTime = ImGui::GetIO().DeltaTime;
            for (std::size_t index = 0; index < m_tabs.Size(); ++index)
            {
                Tab& tab = m_tabs[index];
                Widget::IdScope id(static_cast<int>(index));
                const bool select = m_selectNext == index;
                if (Widget::BeginTab(tab.name.c_str(), &tab.open, select))
                {
                    if (m_active != index)
                    {
                        // 탭을 바꾸면 그 그림을 고른다 - 옵션 칸이 그 그림의 것이어야 한다.
                        m_active = index;
                        SelectPicture(tab.texture);
                    }
                    DrawTab(tab, deltaTime);
                    Widget::EndTab();
                }
            }
            m_selectNext = NoTab;
            Widget::EndTabs();
        }
        ImGui::End();

        // 닫힌 탭은 그리기가 끝난 뒤에 뺀다. 도는 중에 빼면 뒤의 탭이 한 칸씩 밀려 건너뛴다.
        for (std::size_t index = m_tabs.Size(); index > 0; --index)
        {
            if (false == m_tabs[index - 1].open)
            {
                CloseTab(index - 1);
                m_active = NoTab;
            }
        }
    }

    void SpriteViewerWindow::DrawTab(Tab& tab, float deltaTime)
    {
        // **고르는 중이면 위에 말해 준다**(D-165, 기존 `SpriteFramePick`). 누르면 칸이 바로 들어가므로, 보기만 하려던
        // 사람이 모르고 고치지 않게 한 줄로 알리고 그만둘 길을 둔다.
        if (m_editor->IsSpriteFramePickActive() && m_editor->GetSpriteFramePickTexture() == tab.texture)
        {
            Widget::SeverityTextF(Widget::Severity::Info, "%s",
                Loc::TextOr(LocKeys::SpriteViewerPickHint, "click a cell to use that frame"));
            ImGui::SameLine();
            if (Widget::Button(Loc::TextOr(LocKeys::CommonCancel, "Cancel")))
            {
                m_editor->CancelSpriteFramePick();
            }
        }
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (m_sheetWidth <= 0.0f)
        {
            m_sheetWidth = available.x * 0.6f;
        }
        const float sheetWidth = std::clamp(m_sheetWidth, 120.0f,
            (std::max)(120.0f, available.x - 200.0f));
        if (ImGui::BeginChild("##sheet", ImVec2(sheetWidth, 0.0f), ImGuiChildFlags_Borders))
        {
            DrawSheet(tab, ImGui::GetContentRegionAvail());
        }
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 0.0f);
        Widget::Splitter("##sheetSplit", true, 4.0f, &m_sheetWidth,
            120.0f, (std::max)(160.0f, available.x - 200.0f));
        ImGui::SameLine(0.0f, 0.0f);
        if (ImGui::BeginChild("##side", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            DrawPreview(tab, deltaTime);
        }
        ImGui::EndChild();
    }

    void SpriteViewerWindow::DrawSheet(Tab& tab, const ImVec2& area)
    {
        const TextureHandle sheet = m_editor->GetAssetThumbnail(tab.texture, SheetMaxSide);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (false == sheet.IsValid() || false == m_editor->GetAssetSourceSize(tab.texture, width, height)
            || width == 0 || height == 0)
        {
            // 그림은 프레임마다 몇 장씩만 만들어진다. 다음 프레임에 선다.
            Widget::HintText(Loc::TextOr(LocKeys::SpriteViewerLoading, "reading the picture"));
            return;
        }
        const float zoom = (std::min)(MaxSheetZoom,
            (std::min)(area.x / static_cast<float>(width), area.y / static_cast<float>(height)));
        const ImVec2 size(static_cast<float>(width) * zoom, static_cast<float>(height) * zoom);
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        // 누름 자리를 먼저 두고 그 위에 그림과 격자를 그린다.
        const bool clicked = Widget::HitArea("##sheetHit", size);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddImage(static_cast<ImTextureID>(EditorUI::ToTextureId(sheet)), origin,
            ImVec2(origin.x + size.x, origin.y + size.y));

        const AssetSystem* assets = m_editor->GetAssetSystem();
        const SpriteData* data = assets != nullptr ? assets->GetSprite(tab.spriteHandle) : nullptr;
        if (data == nullptr || data->frames.IsEmpty())
        {
            return;
        }
        if (tab.frame >= data->frames.Size())
        {
            // 옵션을 고쳐 칸이 줄었다. 넘친 번호를 들고 있으면 미리보기가 없는 칸을 가리킨다.
            tab.frame = static_cast<std::uint32_t>(data->frames.Size() - 1);
        }

        // **칸마다 테두리를 두른다.** 자른 모양이 보여야 자르는 옵션을 고칠 수 있다.
        const ImU32 cellColor = IM_COL32(255, 255, 255, 90);
        const ImU32 chosenColor = IM_COL32(255, 168, 64, 255);
        for (std::size_t index = 0; index < data->frames.Size(); ++index)
        {
            const SpriteFrame& frame = data->frames[index];
            const ImVec2 min(origin.x + static_cast<float>(frame.x) * zoom,
                origin.y + static_cast<float>(frame.y) * zoom);
            const ImVec2 max(min.x + static_cast<float>(frame.width) * zoom,
                min.y + static_cast<float>(frame.height) * zoom);
            const bool chosen = index == tab.frame;
            draw->AddRect(min, max, chosen ? chosenColor : cellColor, 0.0f, 0, chosen ? 2.0f : 1.0f);
        }

        if (clicked)
        {
            // 누른 자리를 픽셀로 바꿔 그 픽셀을 담은 칸을 고른다. 칸 사이 틈을 누르면 그대로다.
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float pixelX = (mouse.x - origin.x) / zoom;
            const float pixelY = (mouse.y - origin.y) / zoom;
            for (std::size_t index = 0; index < data->frames.Size(); ++index)
            {
                const SpriteFrame& frame = data->frames[index];
                if (pixelX >= static_cast<float>(frame.x)
                    && pixelX < static_cast<float>(frame.x + frame.width)
                    && pixelY >= static_cast<float>(frame.y)
                    && pixelY < static_cast<float>(frame.y + frame.height))
                {
                    tab.frame = static_cast<std::uint32_t>(index);
                    tab.playing = false;
                    // 인스펙터가 이 그림으로 고르는 중이면 누른 칸이 곧 답이다.
                    if (m_editor->IsSpriteFramePickActive() && m_editor->GetSpriteFramePickTexture() == tab.texture)
                    {
                        m_editor->CompleteSpriteFramePick(tab.frame);
                    }
                    break;
                }
            }
        }
    }

    void SpriteViewerWindow::DrawPreview(Tab& tab, float deltaTime)
    {
        const AssetSystem* assets = m_editor->GetAssetSystem();
        const SpriteData* data = assets != nullptr ? assets->GetSprite(tab.spriteHandle) : nullptr;
        const TextureHandle sheet = m_editor->GetAssetThumbnail(tab.texture, SheetMaxSide);
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        const bool ready = data != nullptr && false == data->frames.IsEmpty() && sheet.IsValid()
            && m_editor->GetAssetSourceSize(tab.texture, width, height) && width != 0 && height != 0;

        if (ready)
        {
            const std::uint32_t count = static_cast<std::uint32_t>(data->frames.Size());
            if (tab.playing && tab.framesPerSecond > 0.0f)
            {
                // 칸 하나의 시간이 쌓이면 넘긴다. 프레임 시간이 길어도 한 번에 여러 칸을 건너뛰어
                // 재생 속도를 지킨다.
                tab.clock += deltaTime;
                const float step = 1.0f / tab.framesPerSecond;
                while (tab.clock >= step)
                {
                    tab.clock -= step;
                    tab.frame = (tab.frame + 1) % count;
                }
            }
            if (tab.frame >= count)
            {
                tab.frame = count - 1;
            }
            // **고른 칸을 크게 본다.** 원래 비율을 지킨다 - 늘여 붙이면 픽셀 아트가 기울어 보인다.
            const SpriteFrame& frame = data->frames[tab.frame];
            const float widthAvailable = ImGui::GetContentRegionAvail().x;
            const float side = (std::min)(PreviewMaxSide, widthAvailable);
            const ImVec2 size = Widget::FitInside(frame.width, frame.height, ImVec2(side, side));
            const ImVec2 uvMin(static_cast<float>(frame.x) / static_cast<float>(width),
                static_cast<float>(frame.y) / static_cast<float>(height));
            const ImVec2 uvMax(static_cast<float>(frame.x + frame.width) / static_cast<float>(width),
                static_cast<float>(frame.y + frame.height) / static_cast<float>(height));
            Widget::Image(sheet, size, uvMin, uvMax);

            Widget::FormLayout layout("##playback");
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::SpriteViewerFrame, "Frame")), [&]() {
                int value = static_cast<int>(tab.frame);
                if (Widget::SliderInt("##frame", value, 0, static_cast<int>(count) - 1))
                {
                    tab.frame = static_cast<std::uint32_t>(std::clamp(value, 0, static_cast<int>(count) - 1));
                    tab.playing = false;
                }
            });
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::SpriteViewerFps, "Frames per second")), [&]() {
                Widget::SliderFloat("##fps", tab.framesPerSecond, 1.0f, 60.0f);
            });
            layout.FullRow([&]() {
                if (Widget::Button(tab.playing
                        ? Loc::TextOr(LocKeys::SpriteViewerStop, "Stop")
                        : Loc::TextOr(LocKeys::SpriteViewerPlay, "Play")))
                {
                    tab.playing = false == tab.playing;
                    tab.clock = 0.0f;
                }
                ImGui::SameLine(0.0f, 8.0f);
                Widget::HintTextF("%u / %u", tab.frame + 1, count);
            });
        }
        else
        {
            Widget::HintText(Loc::TextOr(LocKeys::SpriteViewerLoading, "reading the picture"));
        }

        ImGui::Separator();
        // **옵션은 인스펙터와 같은 함수로 그린다.** 고른 에셋이 이 그림이 아니면(다른 곳에서 다른 것을
        // 골랐으면) 그리지 않고 고르는 길을 준다 - 남의 메타를 이 창에서 고치게 두면 무엇을 고치는지
        // 화면과 파일이 갈린다.
        if (false == (m_editor->GetSelectedAsset() == tab.texture))
        {
            Widget::HintText(Loc::TextOr(LocKeys::SpriteViewerSelectToEdit,
                "select this picture to edit its import options"));
            if (Widget::Button(Loc::TextOr(LocKeys::SpriteViewerSelect, "Select")))
            {
                m_editor->SetSelectedAsset(tab.texture);
            }
            return;
        }
        const AssetMetaFile* meta = m_editor->GetSelectedAssetMeta();
        InspectorPanel* inspector = static_cast<InspectorPanel*>(m_editor->FindPanel("Inspector"));
        if (meta != nullptr && inspector != nullptr)
        {
            inspector->DrawAssetOptions(*meta);
        }
    }
}
