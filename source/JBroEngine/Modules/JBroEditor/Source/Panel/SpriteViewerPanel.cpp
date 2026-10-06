#include "SpriteViewerPanel.h"

#include "InspectorPanel.h"

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
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

namespace JBro
{
    namespace
    {
        // 작은 그림은 키워 보인다. 16 픽셀짜리 시트를 16 픽셀로 보이면 칸을 누를 수 없다.
        // 창에 맞춰 키우되 서른두 배에서 멈춘다 - 그 너머로는 한 픽셀이 칸 하나보다 커진다.
        constexpr Float MaxSheetZoom = 32.0f;
        // 큰 시트를 줄여 보는 쪽의 한계다(D-185). 기존 엔진과 같은 값이다 - 이보다 줄이면
        // 칸 테두리가 서로 붙어 자른 모양을 볼 수 없다.
        constexpr Float MinSheetZoom = 0.05f;
        constexpr Float PreviewMaxSide = 192.0f;
    }

    const char* SpriteViewerPanel::GetTitle() const
    {
        return TypeName;
    }

    const char* SpriteViewerPanel::GetDisplayTitle() const
    {
        return m_name.empty() ? Loc::TextOr(LocKeys::SpriteViewerTitle, "Sprite Viewer") : m_name.c_str();
    }

    Bool SpriteViewerPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }

    void SpriteViewerPanel::OnUpdate(Float deltaTime)
    {
        (void)deltaTime;
        // 그리기 전에 지난 프레임에 앞에 있었는지를 받아 둔다. 그리기에서 이번에 앞으로 왔는지 견준다.
        m_wasVisible = IsVisible();
    }

    void SpriteViewerPanel::OnDestroy()
    {
        if (AssetSystem* assets = m_editor != nullptr ? m_editor->GetAssetSystem() : nullptr)
        {
            if (assets->IsLoaded(m_spriteHandle))
            {
                assets->Release(m_spriteHandle);
            }
        }
        m_spriteHandle = {};
    }

    Bool SpriteViewerPanel::ResolvePicture(EditorApplication& editor, AssetId asset, AssetId& texture, AssetId& sprite)
    {
        if (asset.IsNull())
        {
            return false;
        }
        const AssetRegistry& registry = editor.GetAssetRegistry();
        const AssetRecord* record = registry.Find(asset);
        // 그림 파일은 Texture 와 Sprite 두 레코드다. 어느 쪽을 받아도 연다.
        if (record == nullptr
            || (false == AssetTypeRules::IsImageType(record->type) && record->type != AssetType::Sprite))
        {
            return false;
        }
        // Texture 와 Sprite 를 둘 다 찾는다. 시트는 텍스처가, 칸은 스프라이트가 안다.
        texture = record->type == AssetType::Texture ? record->id : record->owner;
        sprite = record->type == AssetType::Sprite ? record->id : AssetId{};
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
        return false == texture.IsNull() && false == sprite.IsNull();
    }

    Bool SpriteViewerPanel::Show(AssetId texture, AssetId sprite)
    {
        AssetSystem* assets = m_editor != nullptr ? m_editor->GetAssetSystem() : nullptr;
        if (assets == nullptr)
        {
            return false;
        }
        // **열려 있는 동안 스프라이트를 잡는다.** 칸은 스프라이트가 들고, 옵션을 고치면
        // 제자리에서 다시 읽혀(asset-plan §2.7) 같은 핸들로 새 칸이 온다.
        m_spriteHandle = assets->Load(sprite);
        if (false == assets->IsLoaded(m_spriteHandle))
        {
            return false;
        }
        m_texture = texture;
        m_sprite = sprite;
        const AssetRecord* textureRecord = m_editor->GetAssetRegistry().Find(texture);
        m_name = textureRecord != nullptr ? textureRecord->relativePath : String("?");
        return true;
    }

    void SpriteViewerPanel::SelectPicture()
    {
        // **프레임을 고르는 중이면 고른 것을 바꾸지 않는다**(D-165). 인스펙터는 고르는 대상 오브젝트를 계속 보여야 한다.
        if (m_editor->IsSpriteFramePickActive() && m_editor->GetSpriteFramePickTexture() == m_texture)
        {
            return;
        }
        m_editor->SetSelectedAsset(m_texture);
    }

    void SpriteViewerPanel::OnDraw()
    {
        // 다른 뷰어 탭에서 이 탭으로 오면 이 그림을 고른다 - 옵션 칸이 이 그림의 것이어야 한다.
        if (false == m_wasVisible)
        {
            SelectPicture();
        }
        const Float deltaTime = ImGui::GetIO().DeltaTime;
        // **고르는 중이면 위에 말해 준다**(D-165, 기존 `SpriteFramePick`). 누르면 칸이 바로 들어가므로, 보기만 하려던
        // 사람이 모르고 고치지 않게 한 줄로 알리고 그만둘 길을 둔다.
        if (m_editor->IsSpriteFramePickActive() && m_editor->GetSpriteFramePickTexture() == m_texture)
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
        const Float sheetWidth = JBro::Clamp(m_sheetWidth, 120.0f,
            (std::max)(120.0f, available.x - 200.0f));
        if (ImGui::BeginChild("##sheet", ImVec2(sheetWidth, 0.0f), ImGuiChildFlags_Borders))
        {
            DrawSheet(ImGui::GetContentRegionAvail());
        }
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 0.0f);
        Widget::Splitter("##sheetSplit", true, 4.0f, &m_sheetWidth,
            120.0f, (std::max)(160.0f, available.x - 200.0f));
        ImGui::SameLine(0.0f, 0.0f);
        if (ImGui::BeginChild("##side", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            DrawPreview(deltaTime);
        }
        ImGui::EndChild();
    }

    void SpriteViewerPanel::DrawSheet(const ImVec2& area)
    {
        const TextureHandle sheet = m_editor->GetAssetThumbnail(m_texture, SheetMaxSide);
        UInt32 width = 0;
        UInt32 height = 0;
        if (false == sheet.IsValid() || false == m_editor->GetAssetSourceSize(m_texture, width, height)
            || width == 0 || height == 0)
        {
            // 그림은 프레임마다 몇 장씩만 만들어진다. 다음 프레임에 선다.
            Widget::HintText(Loc::TextOr(LocKeys::SpriteViewerLoading, "reading the picture"));
            return;
        }
        // **칸에 맞춘 배율이 바닥이다**(D-185). 사람이 확대를 고르지 않았으면 그것을 쓴다 -
        // 처음 열었을 때 시트 전체가 보여야 어디를 볼지 고를 수 있다.
        const Float fitZoom = JBro::Clamp(
            (std::min)(area.x / static_cast<float>(width), area.y / static_cast<float>(height)),
            MinSheetZoom, MaxSheetZoom);
        // 확대 줄. 기존 엔진도 슬라이더와 `창에 맞추기` 단추를 나란히 두었다.
        {
            Float chosen = m_sheetZoom > 0.0f ? m_sheetZoom : fitZoom;
            if (Widget::SliderField("##zoom", chosen, MinSheetZoom, MaxSheetZoom).Width(160.0f)())
            {
                m_sheetZoom = chosen;
            }
            Widget::HoveredTooltip(Loc::TextOr(LocKeys::SpriteViewerZoom, "Zoom"));
            ImGui::SameLine(0.0f, 6.0f);
            if (Widget::Button(Loc::TextOr(LocKeys::SpriteViewerFit, "Fit")))
            {
                // 0 으로 돌려놓는다. 지금 값을 넣으면 창을 늘렸을 때 다시 어긋난다.
                m_sheetZoom = 0.0f;
            }
            ImGui::SameLine(0.0f, 12.0f);
            Widget::Checkbox(Loc::TextOr(LocKeys::SpriteViewerShowPivot, "Show pivot"),
                m_showPivot);
        }
        const Float zoom = m_sheetZoom > 0.0f ? m_sheetZoom : fitZoom;
        const ImVec2 size(static_cast<float>(width) * zoom, static_cast<float>(height) * zoom);
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        // 누름 자리를 먼저 두고 그 위에 그림과 격자를 그린다.
        const Bool clicked = Widget::HitArea("##sheetHit", size);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddImage(static_cast<ImTextureID>(EditorUI::ToTextureId(sheet)), origin,
            ImVec2(origin.x + size.x, origin.y + size.y));

        const AssetSystem* assets = m_editor->GetAssetSystem();
        const SpriteData* data = assets != nullptr ? assets->GetSprite(m_spriteHandle) : nullptr;
        if (data == nullptr || data->frames.IsEmpty())
        {
            return;
        }
        if (m_frame >= data->frames.Size())
        {
            // 옵션을 고쳐 칸이 줄었다. 넘친 번호를 들고 있으면 미리보기가 없는 칸을 가리킨다.
            m_frame = static_cast<std::uint32_t>(data->frames.Size() - 1);
        }

        // **칸마다 테두리를 두른다.** 자른 모양이 보여야 자르는 옵션을 고칠 수 있다.
        const ImU32 cellColor = IM_COL32(255, 255, 255, 90);
        const ImU32 chosenColor = IM_COL32(255, 168, 64, 255);
        const ImU32 hoverColor = IM_COL32(120, 200, 255, 220);
        // 마우스가 가리킨 칸을 이 자리에서 정한다(D-185, 기존 `가리킴`). 시트 위에 있지
        // 않으면 없음이다 - 지난 프레임의 값을 들고 있으면 마우스를 뺀 뒤에도 남는다.
        m_hoveredFrame = -1;
        const Bool overSheet = ImGui::IsItemHovered();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const Float hoverX = (mouse.x - origin.x) / zoom;
        const Float hoverY = (mouse.y - origin.y) / zoom;
        for (std::size_t index = 0; index < data->frames.Size(); ++index)
        {
            const SpriteFrame& frame = data->frames[index];
            if (overSheet && m_hoveredFrame < 0
                && hoverX >= static_cast<float>(frame.x)
                && hoverX < static_cast<float>(frame.x + frame.width)
                && hoverY >= static_cast<float>(frame.y)
                && hoverY < static_cast<float>(frame.y + frame.height))
            {
                m_hoveredFrame = static_cast<int>(index);
            }
        }
        for (std::size_t index = 0; index < data->frames.Size(); ++index)
        {
            const SpriteFrame& frame = data->frames[index];
            const ImVec2 min(origin.x + static_cast<float>(frame.x) * zoom,
                origin.y + static_cast<float>(frame.y) * zoom);
            const ImVec2 max(min.x + static_cast<float>(frame.width) * zoom,
                min.y + static_cast<float>(frame.height) * zoom);
            const Bool chosen = index == m_frame;
            const Bool hovered = static_cast<int>(index) == m_hoveredFrame;
            const ImU32 color = hovered ? hoverColor : (chosen ? chosenColor : cellColor);
            draw->AddRect(min, max, color, 0.0f, 0, (hovered || chosen) ? 2.0f : 1.0f);
            // 피벗은 **칸마다** 다를 수 있다. 시트에서 한눈에 견주려면 다 그려야 한다.
            if (m_showPivot)
            {
                const Float pivotX = min.x + static_cast<float>(frame.width) * frame.pivotX * zoom;
                const Float pivotY = min.y + static_cast<float>(frame.height) * frame.pivotY * zoom;
                constexpr Float Arm = 4.0f;
                draw->AddLine(ImVec2(pivotX - Arm, pivotY), ImVec2(pivotX + Arm, pivotY),
                    chosenColor, 1.5f);
                draw->AddLine(ImVec2(pivotX, pivotY - Arm), ImVec2(pivotX, pivotY + Arm),
                    chosenColor, 1.5f);
            }
        }

        if (clicked && m_hoveredFrame >= 0)
        {
            // 누른 자리의 칸은 이미 위에서 찾아 두었다. 칸 사이 틈을 누르면 그대로다.
            m_frame = static_cast<std::uint32_t>(m_hoveredFrame);
            m_playing = false;
            // 인스펙터가 이 그림으로 고르는 중이면 누른 칸이 곧 답이다.
            if (m_editor->IsSpriteFramePickActive()
                && m_editor->GetSpriteFramePickTexture() == m_texture)
            {
                m_editor->CompleteSpriteFramePick(m_frame);
            }
        }

        // **가리킨 칸의 자리와 크기를 적는다**(D-185, 기존 `가리킴`). 자르는 옵션을 고칠 때
        // 지금 칸이 몇 픽셀인지가 유일하게 알고 싶은 값인데, 그림만 봐서는 셀 수 없다.
        if (m_hoveredFrame >= 0
            && static_cast<std::size_t>(m_hoveredFrame) < data->frames.Size())
        {
            const SpriteFrame& frame = data->frames[static_cast<std::size_t>(m_hoveredFrame)];
            Widget::HintTextF(
                Loc::TextOr(LocKeys::SpriteViewerHoveredFrame, "hovering %d (%d, %d) %d x %d"),
                m_hoveredFrame, static_cast<int>(frame.x), static_cast<int>(frame.y),
                static_cast<int>(frame.width), static_cast<int>(frame.height));
        }
    }

    void SpriteViewerPanel::DrawPreview(Float deltaTime)
    {
        const AssetSystem* assets = m_editor->GetAssetSystem();
        const SpriteData* data = assets != nullptr ? assets->GetSprite(m_spriteHandle) : nullptr;
        const TextureHandle sheet = m_editor->GetAssetThumbnail(m_texture, SheetMaxSide);
        UInt32 width = 0;
        UInt32 height = 0;
        const Bool ready = data != nullptr && false == data->frames.IsEmpty() && sheet.IsValid()
            && m_editor->GetAssetSourceSize(m_texture, width, height) && width != 0 && height != 0;

        if (ready)
        {
            const UInt32 count = static_cast<std::uint32_t>(data->frames.Size());
            if (m_playing && m_framesPerSecond > 0.0f)
            {
                // 칸 하나의 시간이 쌓이면 넘긴다. 프레임 시간이 길어도 한 번에 여러 칸을 건너뛰어
                // 재생 속도를 지킨다.
                m_clock += deltaTime;
                const Float step = 1.0f / m_framesPerSecond;
                while (m_clock >= step)
                {
                    m_clock -= step;
                    m_frame = (m_frame + 1) % count;
                }
            }
            if (m_frame >= count)
            {
                m_frame = count - 1;
            }
            // **고른 칸을 크게 본다.** 원래 비율을 지킨다 - 늘여 붙이면 픽셀 아트가 기울어 보인다.
            const SpriteFrame& frame = data->frames[m_frame];
            const Float widthAvailable = ImGui::GetContentRegionAvail().x;
            const Float side = (std::min)(PreviewMaxSide, widthAvailable);
            const ImVec2 size = Widget::FitInside(frame.width, frame.height, ImVec2(side, side));
            const ImVec2 uvMin(static_cast<float>(frame.x) / static_cast<float>(width),
                static_cast<float>(frame.y) / static_cast<float>(height));
            const ImVec2 uvMax(static_cast<float>(frame.x + frame.width) / static_cast<float>(width),
                static_cast<float>(frame.y + frame.height) / static_cast<float>(height));
            const ImVec2 previewOrigin = ImGui::GetCursorScreenPos();
            Widget::Image(sheet, size, uvMin, uvMax);
            // 미리보기에도 피벗을 찍는다(D-185). 시트에서는 칸이 작아 잘 보이지 않는다.
            if (m_showPivot)
            {
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const Float pivotX = previewOrigin.x + size.x * frame.pivotX;
                const Float pivotY = previewOrigin.y + size.y * frame.pivotY;
                constexpr Float Arm = 7.0f;
                const ImU32 color = IM_COL32(255, 168, 64, 255);
                draw->AddLine(ImVec2(pivotX - Arm, pivotY), ImVec2(pivotX + Arm, pivotY), color, 1.5f);
                draw->AddLine(ImVec2(pivotX, pivotY - Arm), ImVec2(pivotX, pivotY + Arm), color, 1.5f);
            }

            Widget::FormLayout layout("##playback");
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::SpriteViewerFrame, "Frame")), [&]() {
                if (Widget::SliderField("##frame", m_frame, 0u, count - 1u)())
                {
                    m_playing = false;
                }
            });
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::SpriteViewerFps, "Frames per second")), [&]() {
                Widget::SliderField("##fps", m_framesPerSecond, 1.0f, 60.0f)();
            });
            layout.FullRow([&]() {
                if (Widget::Button(m_playing
                        ? Loc::TextOr(LocKeys::SpriteViewerStop, "Stop")
                        : Loc::TextOr(LocKeys::SpriteViewerPlay, "Play")))
                {
                    m_playing = false == m_playing;
                    m_clock = 0.0f;
                }
                ImGui::SameLine(0.0f, 8.0f);
                Widget::HintTextF("%u / %u", m_frame + 1, count);
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
        if (false == (m_editor->GetSelectedAsset() == m_texture))
        {
            Widget::HintText(Loc::TextOr(LocKeys::SpriteViewerSelectToEdit,
                "select this picture to edit its import options"));
            if (Widget::Button(Loc::TextOr(LocKeys::SpriteViewerSelect, "Select")))
            {
                m_editor->SetSelectedAsset(m_texture);
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
