#include <JBro/Framework3DSystem/System/Text3DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Core/Log.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/TextRendering/GlyphMesh.h>

#include <algorithm>
#include <cmath>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

namespace JBro::System
{
    Text3DSystem::~Text3DSystem()
    {
        m_library.Shutdown();
    }

    Int32 Text3DSystem::GetExecutionOrder() const
    {
        return ExecutionOrder;
    }

    void Text3DSystem::SetRenderWorld(RenderWorld3D* renderWorld)
    {
        m_renderWorld = renderWorld;
    }

    void Text3DSystem::SetResources(AssetSystem* assets, Renderer* renderer, TaskManager* tasks)
    {
        m_entries.Clear();
        m_library.Initialize(assets, renderer, tasks);
    }

    Bool Text3DSystem::GetLocalBounds(InstanceId text, Float& minX, Float& minY, Float& maxX, Float& maxY) const
    {
        const Entry* entry = m_entries.Find(text);
        if (entry == nullptr || false == entry->block.GetBounds(minX, minY, maxX, maxY))
        {
            return false;
        }
        const Float ppu = entry->block.GetPixelsPerUnit();
        minX /= ppu;
        minY /= ppu;
        maxX /= ppu;
        maxY /= ppu;
        return true;
    }

    Bool Text3DSystem::IsMissingFont(InstanceId text) const
    {
        const Entry* entry = m_entries.Find(text);
        return entry != nullptr && entry->warnedMissingFont;
    }

    const TextLibrary& Text3DSystem::GetLibrary() const
    {
        return m_library;
    }

    UInt32 Text3DSystem::GetDroppedGlyphCount() const
    {
        return m_droppedGlyphs;
    }

    UInt64 Text3DSystem::GetRelayoutCount() const
    {
        return m_relayouts;
    }

    UInt32 Text3DSystem::GetCachedTextCount() const
    {
        return static_cast<JBro::UInt32>(m_entries.Size());
    }

    TextBlockSettings Text3DSystem::SettingsOf(const Component::Text3D& text)
    {
        TextBlockSettings settings;
        settings.text = text.text;
        settings.textKey = text.textKey;
        settings.fontId = text.fontId;
        settings.font = text.font;
        settings.fontSize = text.fontSize;
        settings.boxWidth = text.boxWidth;
        settings.boxHeight = text.boxHeight;
        settings.overflow = ToLayout(text.overflow);
        settings.wrapMode = ToLayout(text.wrapMode);
        settings.alignX = ToLayout(text.alignX);
        settings.alignY = ToLayout(text.alignY);
        settings.lineSpacing = text.lineSpacing;
        settings.letterSpacing = text.letterSpacing;
        settings.richText = text.richText;
        return settings;
    }

    void Text3DSystem::Submit(Canvas& canvas, const Component::Text3D& text, const Entry& entry)
    {
        GameObject* owner = Internal::CanvasAccess::GetOwner(text);
        const Layer* layer = owner != nullptr ? owner->GetLayer() : nullptr;
        if (layer != nullptr && false == layer->IsVisible())
        {
            return;
        }
        const auto* transform = canvas.FindComponentRaw<Component::Transform3D>(owner);
        if (transform == nullptr || false == transform->IsActiveComponent() || false == transform->worldValid)
        {
            return;
        }
        const TextBlock& block = entry.block;
        const Float ppu = block.GetPixelsPerUnit();
        const Bool outlined = block.IsSdf() && text.outlineWidth > 0.0f && text.outlineColor.A > 0.0f;
        const Float outlineChannels[4] = { text.outlineColor.R, text.outlineColor.G, text.outlineColor.B, text.outlineColor.A };
        for (const GlyphQuad& quad : block.GetQuads())
        {
            const AssetHandle page = m_library.GetPageTexture(block.GetFont(quad.face), quad.page);
            if (page.generation == 0)
            {
                continue;
            }
            WorldTextRenderItem item;
            item.owner = owner;
            item.position = transform->worldPosition;
            item.rotation = transform->worldRotation;
            item.scale = transform->worldScale;
            item.billboard = text.facing == Component::TextFacing3D::Billboard;
            item.left = quad.left / ppu;
            item.top = quad.top / ppu;
            item.width = quad.width / ppu;
            item.height = quad.height / ppu;
            item.texture = page;
            for (Int32 channel = 0; channel < 4; ++channel)
            {
                item.uvRect[channel] = quad.uvRect[channel];
            }
            item.layerOrder = layer != nullptr ? layer->GetOrder() : 0;
            item.layerBlend = layer != nullptr ? layer->GetBlend() : LayerBlend::Normal;
            item.layerOpacity = layer != nullptr ? layer->GetOpacity() : Float(1.0f);
            item.layerParallax = layer != nullptr ? layer->GetParallax() : Float(1.0f);
            item.tint = text.color;
            if (quad.hasTint)
            {
                item.tint = Color{ quad.tint[0] / 255.0f, quad.tint[1] / 255.0f, quad.tint[2] / 255.0f,
                    quad.tint[3] / 255.0f * text.color.A };
            }
            item.linearFilter = block.IsSdf() || block.GetFilter() == TextureFilter::Linear;
            if (block.IsSdf())
            {
                item.sdf = true;
                for (Int32 channel = 0; channel < 4; ++channel)
                {
                    item.outlineColor[channel] =
                        static_cast<std::uint8_t>(std::lround(JBro::Clamp(outlineChannels[channel], 0.0f, 1.0f) * 255.0f));
                }
                const Float edge = outlined ? SdfOutlineEdge(text.outlineWidth, quad.sdfPerTextPixel, block.GetSdfSpread()) : Float(0.5f);
                item.outlineEdge = static_cast<std::uint16_t>(std::lround(JBro::Clamp(edge, 0.0f, 1.0f) * 65535.0f));
            }
            if (false == m_renderWorld->SubmitText(item))
            {
                ++m_droppedGlyphsThisFrame;
            }
        }
    }

    void Text3DSystem::DropUnseen()
    {
        // 떼인 컴포넌트의 캐시다. 이번 프레임에 못 본 것만 지운다(D-249).
        RemoveStaleEntries(m_entries, m_frame, m_scratchUnseen,
            [](const Entry& entry) { return entry.lastSeenFrame; });
    }

    void Text3DSystem::OnUpdate(Canvas& canvas, Float)
    {
        if (m_renderWorld == nullptr)
        {
            return;
        }
        ++m_frame;
        m_library.SyncProjectFonts();
        m_library.TrimAtlases(m_frame);

        canvas.ForEach<Component::Text3D>([&](Component::Text3D& text)
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
            const TextBlock::UpdateResult result = entry.block.Update(m_library, SettingsOf(text));
            if (result == TextBlock::UpdateResult::NoFont)
            {
                if (false == entry.warnedMissingFont)
                {
                    Log::Write(LogLevel::Warning, "text",
                        "a Text3D has no usable font and is not drawn - set its fontId or add a project font");
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

        m_library.UploadDirtyPages();

        m_droppedGlyphsThisFrame = 0;
        canvas.ForEach<Component::Text3D>([&](Component::Text3D& text)
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
                "%u 3D glyphs were not drawn this frame - raise the world text capacity (RendererConfig::maxWorldTextSubmissions)",
                m_droppedGlyphs);
            m_warnedDroppedGlyphs = true;
        }
        if (m_droppedGlyphs == 0)
        {
            m_warnedDroppedGlyphs = false;
        }
        DropUnseen();
    }

    void Text3DSystem::OnShutdown(Canvas&)
    {
        m_entries.Clear();
        m_library.Shutdown();
    }
}
