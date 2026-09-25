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

namespace JBro::System
{
    namespace
    {
        std::uint32_t PixelSizeOf(float fontSize)
        {
            // 비트맵은 정수 크기로 뜬다. 레이아웃도 같은 크기로 해야 글자 사이가 비트맵과 맞는다.
            const long rounded = std::isfinite(fontSize) ? std::lround(fontSize) : 0;
            return static_cast<std::uint32_t>(std::clamp<long>(rounded, 1, static_cast<long>(Text::GlyphAtlas::MaxPixelSize)));
        }

        void Mix(std::uint64_t& key, std::uint64_t value)
        {
            std::size_t seed = static_cast<std::size_t>(key);
            HashCombine(seed, static_cast<std::size_t>(value));
            key = static_cast<std::uint64_t>(seed);
        }

        std::uint64_t Bits(float value)
        {
            return std::bit_cast<std::uint32_t>(value);
        }

        // 컴포넌트(Tier S)와 커널(Tier E)이 같은 값을 따로 들고, 레이아웃은 정수로 옮긴다. 순서가 어긋나면 여기서 멈춘다.
        static_assert(static_cast<int>(Component::TextOverflow::Clip) == static_cast<int>(Text::Overflow::Clip));
        static_assert(static_cast<int>(Component::TextOverflow::Wrap) == static_cast<int>(Text::Overflow::Wrap));
        static_assert(static_cast<int>(Component::TextWrapMode::Character) == static_cast<int>(Text::WrapMode::Character));
        static_assert(static_cast<int>(Component::TextAlignX::Right) == static_cast<int>(Text::AlignX::Right));
        static_assert(static_cast<int>(Component::TextAlignX::Center) == static_cast<int>(Text::AlignX::Center));
        static_assert(static_cast<int>(Component::TextAlignY::Middle) == static_cast<int>(Text::AlignY::Middle));
        static_assert(static_cast<int>(Component::TextAlignY::Baseline) == static_cast<int>(Text::AlignY::Baseline));
        static_assert(static_cast<int>(Component::TextAlignY::Bottom) == static_cast<int>(Text::AlignY::Bottom));
    }

    Text2DSystem::~Text2DSystem()
    {
        m_library.Shutdown();
    }

    int Text2DSystem::GetExecutionOrder() const
    {
        return ExecutionOrder;
    }

    void Text2DSystem::SetRenderWorld(RenderWorld2D* renderWorld)
    {
        m_renderWorld = renderWorld;
    }

    void Text2DSystem::SetResources(AssetSystem* assets, Renderer* renderer)
    {
        m_entries.Clear();
        m_library.Initialize(assets, renderer);
    }

    void Text2DSystem::SetText(Component::Text2D& text, const char* utf8, std::uint32_t length)
    {
        TextStore::Get().Assign(text.text, utf8, utf8 != nullptr ? length : 0);
    }

    std::uint32_t Text2DSystem::GetTextLength(const Component::Text2D& text) const
    {
        return static_cast<std::uint32_t>(TextStore::Get().GetText(text.text).Size());
    }

    std::uint32_t Text2DSystem::CopyText(const Component::Text2D& text, char* buffer, std::uint32_t capacity) const
    {
        if (buffer == nullptr || capacity == 0)
        {
            return 0;
        }
        const ArrayView<const char> source = TextStore::Get().GetText(text.text);
        const std::uint32_t count = static_cast<std::uint32_t>(std::min<std::size_t>(source.Size(), capacity - 1));
        if (count > 0)
        {
            std::memcpy(buffer, source.Data(), count);
        }
        buffer[count] = '\0';
        return count;
    }

    bool Text2DSystem::GetLocalBounds(InstanceId text, float& minX, float& minY, float& maxX, float& maxY) const
    {
        const Entry* entry = m_entries.Find(text);
        if (entry == nullptr || false == entry->hasBounds)
        {
            return false;
        }
        minX = entry->bounds[0];
        minY = entry->bounds[1];
        maxX = entry->bounds[2];
        maxY = entry->bounds[3];
        return true;
    }

    const TextLibrary& Text2DSystem::GetLibrary() const
    {
        return m_library;
    }

    std::uint64_t Text2DSystem::GetRelayoutCount() const
    {
        return m_relayouts;
    }

    std::uint32_t Text2DSystem::GetCachedTextCount() const
    {
        return static_cast<std::uint32_t>(m_entries.Size());
    }

    std::uint64_t Text2DSystem::MakeOptionsKey(const Component::Text2D& text)
    {
        // 레이아웃을 바꾸는 필드만 넣는다. 색·그리기 순서는 캐시를 그대로 쓴다.
        std::uint64_t key = PixelSizeOf(text.fontSize);
        Mix(key, Bits(text.boxSize.x));
        Mix(key, Bits(text.boxSize.y));
        Mix(key, static_cast<std::uint64_t>(text.overflow));
        Mix(key, static_cast<std::uint64_t>(text.wrapMode));
        Mix(key, static_cast<std::uint64_t>(text.alignX));
        Mix(key, static_cast<std::uint64_t>(text.alignY));
        Mix(key, Bits(text.lineSpacing));
        Mix(key, Bits(text.letterSpacing));
        return key;
    }

    void Text2DSystem::Relayout(const Component::Text2D& text, Entry& entry, const FontView& font)
    {
        ++m_relayouts;
        const std::uint32_t pixelSize = PixelSizeOf(text.fontSize);
        Text::LayoutOptions options;
        options.fontSize = static_cast<float>(pixelSize);
        options.boxWidth = std::max(0.0f, text.boxSize.x);
        options.boxHeight = std::max(0.0f, text.boxSize.y);
        options.overflow = static_cast<Text::Overflow>(text.overflow);
        options.wrapMode = static_cast<Text::WrapMode>(text.wrapMode);
        options.alignX = static_cast<Text::AlignX>(text.alignX);
        options.alignY = static_cast<Text::AlignY>(text.alignY);
        options.lineSpacing = text.lineSpacing;
        options.letterSpacing = text.letterSpacing;

        entry.text = text.text;
        entry.textRevision = TextStore::Get().GetRevision(text.text);
        entry.font = text.font;
        entry.fontGeneration = font.dataGeneration;
        entry.optionsKey = MakeOptionsKey(text);
        entry.pixelsPerUnit = font.pixelsPerUnit;
        entry.filter = font.filter;
        entry.quads.Clear();
        entry.hasBounds = false;

        const Text::FontFace* faces[] = { font.face };
        if (entry.layout.Build(TextStore::Get().GetText(text.text), faces, options) != Text::LayoutError::None)
        {
            return;
        }

        // Clip 은 상자 밖으로 나간 글리프를 잘라 낸다 - 레이아웃은 줄만 버렸고, 여기서 반쯤 걸친 글리프의 사각형과 UV 를 줄인다.
        const bool clip = text.overflow == Component::TextOverflow::Clip && options.boxWidth > 0.0f && options.boxHeight > 0.0f;
        const float clipLeft = entry.layout.GetMinX();
        const float clipRight = entry.layout.GetMaxX();
        const float clipBottom = entry.layout.GetMinY();
        const float clipTop = entry.layout.GetMaxY();
        const float pageSize = static_cast<float>(font.atlas->GetPageSize());
        for (const Text::PositionedGlyph& glyph : entry.layout.GetGlyphs())
        {
            Text::AtlasGlyph cell;
            if (font.atlas->Ensure(*font.face, pixelSize, glyph.glyph, cell) != Text::AtlasError::None || cell.empty)
            {
                continue;
            }
            GlyphQuad quad;
            quad.left = glyph.x + static_cast<float>(cell.left);
            quad.top = glyph.y + static_cast<float>(cell.top);
            quad.width = static_cast<float>(cell.width);
            quad.height = static_cast<float>(cell.height);
            float u0 = static_cast<float>(cell.x) / pageSize;
            float v0 = static_cast<float>(cell.y) / pageSize;
            float u1 = static_cast<float>(cell.x + cell.width) / pageSize;
            float v1 = static_cast<float>(cell.y + cell.height) / pageSize;
            if (clip)
            {
                float right = quad.left + quad.width;
                float bottom = quad.top - quad.height;
                if (right <= clipLeft || quad.left >= clipRight || bottom >= clipTop || quad.top <= clipBottom)
                {
                    continue;
                }
                if (quad.left < clipLeft)
                {
                    u0 += (u1 - u0) * (clipLeft - quad.left) / quad.width;
                    quad.left = clipLeft;
                }
                if (right > clipRight)
                {
                    u1 -= (u1 - u0) * (right - clipRight) / (right - quad.left);
                    right = clipRight;
                }
                if (quad.top > clipTop)
                {
                    v0 += (v1 - v0) * (quad.top - clipTop) / quad.height;
                    quad.top = clipTop;
                }
                if (bottom < clipBottom)
                {
                    v1 -= (v1 - v0) * (clipBottom - bottom) / (quad.top - bottom);
                    bottom = clipBottom;
                }
                quad.width = right - quad.left;
                quad.height = quad.top - bottom;
            }
            quad.uvRect[0] = u0;
            quad.uvRect[1] = v0;
            quad.uvRect[2] = u1 - u0;
            quad.uvRect[3] = v1 - v0;
            quad.page = cell.page;
            entry.quads.Add(quad);
        }

        const float ppu = entry.pixelsPerUnit;
        entry.bounds[0] = entry.layout.GetMinX() / ppu;
        entry.bounds[1] = entry.layout.GetMinY() / ppu;
        entry.bounds[2] = entry.layout.GetMaxX() / ppu;
        entry.bounds[3] = entry.layout.GetMaxY() / ppu;
        entry.hasBounds = true;
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
        const float ppu = entry.pixelsPerUnit;
        for (const GlyphQuad& quad : entry.quads)
        {
            const AssetHandle page = m_library.GetPageTexture(entry.font, quad.page);
            if (page.generation == 0)
            {
                // 페이지가 아직 올라가지 않았다(렌더러가 거절했다). 흰 사각형을 그리지 않는다.
                continue;
            }
            SpriteRenderItem item;
            item.owner = owner;
            item.sourceId = text.GetInstanceId();
            item.layerOrder = layer != nullptr ? layer->GetOrder() : 0;
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
            item.filter = entry.filter;
            item.tint = text.color;
            item.pivot = Vec2{ 0.0f, 1.0f };
            item.size = Vec2{ quad.width / ppu, quad.height / ppu };
            item.renderOrder = text.renderOrder;
            m_renderWorld->SubmitSprite(item);
        }
    }

    void Text2DSystem::DropUnseen()
    {
        // 떼인 컴포넌트의 캐시다. 표를 도는 동안 지우지 않고 모아 둔 뒤 지운다(용량을 다시 쓴다).
        m_scratchUnseen.Clear();
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        {
            if (it->MappedValue.lastSeenFrame != m_frame)
            {
                m_scratchUnseen.Add(it->KeyValue);
            }
        }
        for (const InstanceId id : m_scratchUnseen)
        {
            m_entries.Remove(id);
        }
    }

    void Text2DSystem::OnUpdate(Canvas& canvas, float)
    {
        if (m_renderWorld == nullptr)
        {
            return;
        }
        ++m_frame;
        const TextStore& store = TextStore::Get();

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
            FontView font;
            if (false == m_library.Acquire(text.font, font))
            {
                if (false == entry.warnedMissingFont)
                {
                    Log::Write(LogLevel::Warning, "text", "a Text2D has no usable font and is not drawn - set its fontId");
                    entry.warnedMissingFont = true;
                }
                // 폰트가 돌아오면 핸들이 달라 다시 레이아웃된다.
                entry.font = {};
                entry.quads.Clear();
                entry.hasBounds = false;
                return;
            }
            entry.warnedMissingFont = false;
            const bool stale = entry.text.index != text.text.index
                || entry.text.generation != text.text.generation
                || entry.textRevision != store.GetRevision(text.text)
                || entry.font.index != text.font.index
                || entry.font.generation != text.font.generation
                || entry.fontGeneration != font.dataGeneration
                || entry.optionsKey != MakeOptionsKey(text);
            if (stale)
            {
                Relayout(text, entry, font);
            }
        });

        // 2. 새 글리프가 들어간 페이지만 올린다. 새 글자가 없으면 아무것도 하지 않는다.
        m_library.UploadDirtyPages();

        // 3. 글리프마다 아이템을 낸다.
        canvas.ForEach<Component::Text2D>([&](Component::Text2D& text)
        {
            if (false == text.visible || false == text.IsActiveComponent())
            {
                return;
            }
            const Entry* entry = m_entries.Find(text.GetInstanceId());
            if (entry != nullptr && entry->font.generation != 0)
            {
                Submit(canvas, text, *entry);
            }
        });

        DropUnseen();
    }

    void Text2DSystem::OnShutdown(Canvas&)
    {
        m_entries.Clear();
        m_library.Shutdown();
    }
}
