#include <JBro/Framework2DSystem/System/Text2DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Core/Log.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/TextStore.h>
#include <JBro/Types/Hash.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

namespace JBro::System
{
    Text2DSystem::~Text2DSystem()
    {
        m_library.Shutdown();
    }

    Int32 Text2DSystem::GetExecutionOrder() const
    {
        return ExecutionOrder;
    }

    void Text2DSystem::SetRenderWorld(RenderWorld2D* renderWorld)
    {
        m_renderWorld = renderWorld;
    }

    void Text2DSystem::SetResources(AssetSystem* assets, Renderer* renderer, TaskManager* tasks)
    {
        m_entries.Clear();
        m_library.Initialize(assets, renderer, tasks);
    }

    Bool Text2DSystem::GetLocalBounds(InstanceId text, Float& minX, Float& minY, Float& maxX, Float& maxY) const
    {
        const Entry* entry = m_entries.Find(text);
        if (entry == nullptr || false == entry->block.GetBounds(minX, minY, maxX, maxY))
        {
            return false;
        }
        const Float ppu = entry->screenSpace ? Float(1.0f) : entry->block.GetPixelsPerUnit();
        minX /= ppu;
        minY /= ppu;
        maxX /= ppu;
        maxY /= ppu;
        return true;
    }

    Float Text2DSystem::GetLaidOutFontSize(InstanceId text) const
    {
        const Entry* entry = m_entries.Find(text);
        Float minX = 0.0f;
        Float minY = 0.0f;
        Float maxX = 0.0f;
        Float maxY = 0.0f;
        return entry != nullptr && entry->block.GetBounds(minX, minY, maxX, maxY) ? entry->block.GetFittedSize() : Float(0.0f);
    }

    Bool Text2DSystem::IsMissingFont(InstanceId text) const
    {
        const Entry* entry = m_entries.Find(text);
        return entry != nullptr && entry->warnedMissingFont;
    }

    UInt32 Text2DSystem::GetDroppedGlyphCount() const
    {
        return m_droppedGlyphs;
    }

    void Text2DSystem::SetAtlasPageLimit(UInt32 pages)
    {
        m_library.SetPageLimit(pages);
    }

    const TextLibrary& Text2DSystem::GetLibrary() const
    {
        return m_library;
    }

    UInt64 Text2DSystem::GetRelayoutCount() const
    {
        return m_relayouts;
    }

    UInt32 Text2DSystem::GetCachedTextCount() const
    {
        return static_cast<JBro::UInt32>(m_entries.Size());
    }

    TextBlockSettings Text2DSystem::SettingsOf(const Component::Text2D& text)
    {
        TextBlockSettings settings;
        settings.text = text.text;
        settings.textKey = text.textKey;
        settings.fontId = text.fontId;
        settings.font = text.font;
        settings.fontSize = text.fontSize;
        settings.boxWidth = text.boxSize.x;
        settings.boxHeight = text.boxSize.y;
        settings.overflow = ToLayout(text.overflow);
        settings.wrapMode = ToLayout(text.wrapMode);
        settings.alignX = ToLayout(text.alignX);
        settings.alignY = ToLayout(text.alignY);
        settings.lineSpacing = text.lineSpacing;
        settings.letterSpacing = text.letterSpacing;
        settings.autoSize = text.autoSize;
        settings.minFontSize = text.minFontSize;
        settings.maxFontSize = text.maxFontSize;
        settings.pixelSnap = text.pixelSnap;
        settings.richText = text.richText;
        return settings;
    }

    void Text2DSystem::Submit(Canvas& canvas, const Component::Text2D& text, const Entry& entry)
    {
        GameObject* owner = Internal::CanvasAccess::GetOwner(text);
        const Layer* layer = owner != nullptr ? owner->GetLayer() : nullptr;
        if (layer != nullptr && false == layer->IsVisible())
        {
            return;
        }
        const auto* transform = canvas.FindComponentRaw<Component::Transform2D>(owner);
        if (transform == nullptr || false == transform->IsActiveComponent() || false == transform->worldValid)
        {
            return;
        }
        const TextBlock& block = entry.block;
        const Float ppu = entry.screenSpace ? Float(1.0f) : block.GetPixelsPerUnit();
        // 외곽선 폭(글자 픽셀)의 문턱은 글자마다다 - 리치 텍스트의 `<size>` 가 섞이면 거리장 픽셀 / 글자 픽셀이 글자마다 다르다.
        const Bool outlined = block.IsSdf() && text.outlineWidth > 0.0f && text.outlineColor.A > 0.0f;
        for (const GlyphQuad& quad : block.GetQuads())
        {
            const AssetHandle page = m_library.GetPageTexture(block.GetFont(quad.face), quad.page);
            if (page.generation == 0)
            {
                // 페이지가 아직 올라가지 않았다(렌더러가 거절했다). 흰 사각형을 그리지 않는다.
                continue;
            }
            SpriteRenderItem item;
            item.owner = owner;
            item.sourceId = text.GetInstanceId();
            item.layerOrder = layer != nullptr ? layer->GetOrder() : 0;
            item.screenSpace = entry.screenSpace;
            item.scaleMode = layer != nullptr ? layer->GetScaleMode() : ScreenScaleMode::FixedHeight;
            item.layerBlend = layer != nullptr ? layer->GetBlend() : LayerBlend::Normal;
            item.layerOpacity = layer != nullptr ? layer->GetOpacity() : Float(1.0f);
            item.layerParallax = layer != nullptr ? layer->GetParallax() : Float(1.0f);
            // 글리프 쿼드의 왼쪽 위를 오브젝트 로컬에 두고(피벗 {0, 1}), 오브젝트 월드로 옮긴다.
            Matrix3x2 local;
            local.m31 = quad.left / ppu;
            local.m32 = quad.top / ppu;
            item.world = MultiplyMatrix3x2(local, transform->world);
            item.texture = page;
            item.uvRect[0] = quad.uvRect[0];
            item.uvRect[1] = quad.uvRect[1];
            item.uvRect[2] = quad.uvRect[2];
            item.uvRect[3] = quad.uvRect[3];
            item.filter = block.GetFilter();
            item.tint = text.color;
            if (quad.hasTint)
            {
                // `<color>` 가 RGB 와 알파를 정하고, 텍스트 전체의 알파(color.A)를 곱한다 - 텍스트를 통째로 흐리게 하는 연출이 그대로 된다.
                item.tint = Color{ quad.tint[0] / 255.0f, quad.tint[1] / 255.0f, quad.tint[2] / 255.0f,
                    quad.tint[3] / 255.0f * text.color.A };
            }
            item.pivot = Vector2{ 0.0f, 1.0f };
            item.size = Vector2{ quad.width / ppu, quad.height / ppu };
            item.renderOrder = text.renderOrder;
            if (block.IsSdf())
            {
                item.sdfText = true;
                item.filter = TextureFilter::Linear;
                const Float channels[4] = { text.outlineColor.R, text.outlineColor.G, text.outlineColor.B, text.outlineColor.A };
                for (Int32 channel = 0; channel < 4; ++channel)
                {
                    item.outlineColor[channel] = static_cast<std::uint8_t>(std::lround(JBro::Clamp(channels[channel], 0.0f, 1.0f) * 255.0f));
                }
                const Float outlineEdge = outlined ? SdfOutlineEdge(text.outlineWidth, quad.sdfPerTextPixel, block.GetSdfSpread()) : Float(0.5f);
                item.outlineEdge = static_cast<std::uint16_t>(std::lround(JBro::Clamp(outlineEdge, 0.0f, 1.0f) * 65535.0f));
            }
            if (false == m_renderWorld->SubmitSprite(item))
            {
                ++m_droppedGlyphsThisFrame;
            }
        }
    }

    void Text2DSystem::DropUnseen()
    {
        // 떼인 컴포넌트의 캐시다. 이번 프레임에 못 본 것만 지운다(D-249).
        RemoveStaleEntries(m_entries, m_frame, m_scratchUnseen,
            [](const Entry& entry) { return entry.lastSeenFrame; });
    }

    void Text2DSystem::OnUpdate(Canvas& canvas, Float)
    {
        if (m_renderWorld == nullptr)
        {
            return;
        }
        ++m_frame;
        m_library.SyncProjectFonts();
        // 레이아웃 전에 넘친 아틀라스를 비운다. 비운 폰트의 텍스트는 아래에서 아틀라스 세대가 달라 다시 레이아웃된다.
        m_library.TrimAtlases(m_frame);

        // 1. 바뀐 것만 다시 레이아웃한다. 여기서 아틀라스에 새 글리프가 들어간다.
        canvas.ForEach<Component::Text2D>([&](Component::Text2D& text)
        {
            if (false == text.visible || false == text.IsActiveComponent())
            {
                if (Entry* hidden = m_entries.Find(text.GetInstanceId()))
                {
                    hidden->lastSeenFrame = m_frame;
                }
                return;
            }
            Entry& entry = m_entries.FindOrAdd(text.GetInstanceId());
            entry.lastSeenFrame = m_frame;
            {
                const GameObject* owner = Internal::CanvasAccess::GetOwner(text);
                const Layer* layer = owner != nullptr ? owner->GetLayer() : nullptr;
                entry.screenSpace = layer != nullptr && layer->GetSpace() == LayerSpace::Screen;
            }
            const TextBlock::UpdateResult result = entry.block.Update(m_library, SettingsOf(text));
            if (result == TextBlock::UpdateResult::NoFont)
            {
                if (false == entry.warnedMissingFont)
                {
                    Log::Write(LogLevel::Warning, "text",
                        "a Text2D has no usable font and is not drawn - set its fontId or add a project font");
                    entry.warnedMissingFont = true;
                }
                return;
            }
            entry.warnedMissingFont = false;
            if (result == TextBlock::UpdateResult::Relaid)
            {
                ++m_relayouts;
            }
        });

        // 2. 새 글리프가 들어간 페이지만 올린다. 새 글자가 없으면 아무것도 하지 않는다.
        m_library.UploadDirtyPages();

        // 3. 글리프마다 아이템을 낸다. 글자도 스프라이트 제출 상한(`RendererConfig::maxSpriteSubmissions`)을 나눠 쓴다(text-plan §3.3) -
        // 넘친 글자는 그려지지 않으므로 처음 넘친 프레임에 한 번 알린다(스프라이트의 넘침은 렌더러 통계가 센다).
        m_droppedGlyphsThisFrame = 0;
        canvas.ForEach<Component::Text2D>([&](Component::Text2D& text)
        {
            if (false == text.visible || false == text.IsActiveComponent())
            {
                return;
            }
            const Entry* entry = m_entries.Find(text.GetInstanceId());
            if (entry != nullptr && entry->block.GetFontCount() != 0)
            {
                Submit(canvas, text, *entry);
            }
        });

        m_droppedGlyphs = m_droppedGlyphsThisFrame;
        if (m_droppedGlyphs != 0 && false == m_warnedDroppedGlyphs)
        {
            Log::Write(LogLevel::Warning, "text",
                "%u glyphs were not drawn this frame - text shares the sprite submission limit; raise maxSpriteSubmissions",
                m_droppedGlyphs);
            m_warnedDroppedGlyphs = true;
        }
        if (m_droppedGlyphs == 0)
        {
            m_warnedDroppedGlyphs = false;
        }
        DropUnseen();
    }

    void Text2DSystem::OnShutdown(Canvas&)
    {
        m_entries.Clear();
        m_library.Shutdown();
    }
}
