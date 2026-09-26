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

    void Text2DSystem::SetResources(AssetSystem* assets, Renderer* renderer, TaskManager* tasks)
    {
        m_entries.Clear();
        m_library.Initialize(assets, renderer, tasks);
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

    float Text2DSystem::GetLaidOutFontSize(InstanceId text) const
    {
        const Entry* entry = m_entries.Find(text);
        return entry != nullptr && entry->hasBounds ? entry->fittedSize : 0.0f;
    }

    bool Text2DSystem::IsMissingFont(InstanceId text) const
    {
        const Entry* entry = m_entries.Find(text);
        return entry != nullptr && entry->warnedMissingFont;
    }

    std::uint32_t Text2DSystem::GetDroppedGlyphCount() const
    {
        return m_droppedGlyphs;
    }

    void Text2DSystem::SetAtlasPageLimit(std::uint32_t pages)
    {
        m_library.SetPageLimit(pages);
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
        // 크기는 값 그대로 넣는다. SDF 는 소수 크기로 레이아웃하므로 반올림한 픽셀이 같아도 다시 해야 한다.
        std::uint64_t key = Bits(text.fontSize);
        Mix(key, Bits(text.boxSize.x));
        Mix(key, Bits(text.boxSize.y));
        Mix(key, static_cast<std::uint64_t>(text.overflow));
        Mix(key, static_cast<std::uint64_t>(text.wrapMode));
        Mix(key, static_cast<std::uint64_t>(text.alignX));
        Mix(key, static_cast<std::uint64_t>(text.alignY));
        Mix(key, Bits(text.lineSpacing));
        Mix(key, Bits(text.letterSpacing));
        Mix(key, text.autoSize ? 1u : 0u);
        Mix(key, Bits(text.minFontSize));
        Mix(key, Bits(text.maxFontSize));
        Mix(key, text.pixelSnap ? 1u : 0u);
        Mix(key, text.richText ? 1u : 0u);
        return key;
    }

    bool Text2DSystem::GatherFonts(
        const Component::Text2D& text, AssetHandle* handles, FontView* views, std::uint32_t& count)
    {
        // **`fontId` 가 비면 프로젝트의 첫 폰트다**(D-200 (6)). 아이디를 적었는데 그 폰트가 없으면 대신 기본 폰트로 그리지 않는다 -
        // 고른 폰트가 깨졌다는 것이 보여야 한다(경고가 뜬다).
        count = 0;
        const ArrayView<const AssetHandle> project = m_library.GetProjectFonts();
        AssetHandle primary = text.font;
        if (text.fontId.IsNull())
        {
            primary = project.Size() > 0 ? project[0] : AssetHandle{};
        }
        if (false == m_library.Acquire(primary, views[0]))
        {
            return false;
        }
        handles[0] = primary;
        count = 1;
        // 나머지 프로젝트 폰트가 폴백이다. 기본 폰트와 같은 것은 건너뛰고, 열리지 않는 것은 빼고 간다.
        for (std::size_t index = 0; index < project.Size() && count < MaxFaces; ++index)
        {
            const AssetHandle fallback = project[index];
            if (fallback.index == primary.index && fallback.generation == primary.generation)
            {
                continue;
            }
            if (m_library.Acquire(fallback, views[count]))
            {
                handles[count] = fallback;
                ++count;
            }
        }
        return true;
    }

    void Text2DSystem::Relayout(const Component::Text2D& text, Entry& entry, const AssetHandle* handles,
        const FontView* views, std::uint32_t count)
    {
        const FontView& font = views[0];
        ++m_relayouts;
        // **SDF 는 크기를 반올림하지 않는다**(4 단계). 거리장 한 벌을 키우고 줄이므로 크기가 조금씩 바뀌는 연출(트윈)에 새 글리프가
        // 생기지 않는다 - 비트맵은 정수 크기마다 새로 떠 아틀라스가 크기 수만큼 자랐다(text-plan §7).
        const bool sdf = font.renderMode == FontRenderMode::Sdf;
        const float maxSize = static_cast<float>(Text::GlyphAtlas::MaxPixelSize);
        const auto sizeOf = [&](float requested) {
            return sdf ? std::clamp(std::isfinite(requested) ? requested : 1.0f, 1.0f, maxSize)
                       : static_cast<float>(GlyphPixelSize(requested));
        };
        float layoutSize = sizeOf(text.fontSize);
        Text::LayoutOptions options;
        options.fontSize = layoutSize;
        options.boxWidth = std::max(0.0f, text.boxSize.x);
        options.boxHeight = std::max(0.0f, text.boxSize.y);
        options.overflow = static_cast<Text::Overflow>(text.overflow);
        options.wrapMode = static_cast<Text::WrapMode>(text.wrapMode);
        options.alignX = static_cast<Text::AlignX>(text.alignX);
        options.alignY = static_cast<Text::AlignY>(text.alignY);
        options.lineSpacing = text.lineSpacing;
        options.letterSpacing = text.letterSpacing;
        options.richText = text.richText;
        // 비트맵은 정수 크기마다 뜨므로 `<size>` 도 정수로 잰다.
        options.wholePixelMarkup = false == sdf;

        entry.text = text.text;
        entry.textRevision = TextStore::Get().GetRevision(text.text);
        entry.fontCount = count;
        for (std::uint32_t face = 0; face < count; ++face)
        {
            entry.fonts[face] = handles[face];
            entry.fontGenerations[face] = views[face].dataGeneration;
            entry.atlasGenerations[face] = views[face].atlasGeneration;
        }
        entry.optionsKey = MakeOptionsKey(text);
        entry.pixelsPerUnit = font.pixelsPerUnit;
        entry.sdf = sdf;
        entry.sdfSpread = font.sdfSpread;
        entry.filter = font.filter;
        entry.quads.Clear();
        entry.hasBounds = false;

        const Text::FontFace* faces[MaxFaces] = {};
        for (std::uint32_t face = 0; face < count; ++face)
        {
            faces[face] = views[face].face;
        }
        const ArrayView<const Text::FontFace* const> faceView(faces, count);
        const ArrayView<const char> utf8 = TextStore::Get().GetText(text.text);
        // 자동 크기는 상자가 있을 때만 뜻이 있다. 크기를 먼저 찾고, 그 크기로 레이아웃이 남는다.
        const bool fitToBox = text.autoSize && (options.boxWidth > 0.0f || options.boxHeight > 0.0f);
        Text::LayoutError built = Text::LayoutError::None;
        if (fitToBox)
        {
            float chosen = layoutSize;
            built = entry.layout.BuildToFit(utf8, faceView, options, sizeOf(std::min(text.minFontSize, text.maxFontSize)),
                sizeOf(std::max(text.minFontSize, text.maxFontSize)), sdf ? 0.0f : 1.0f, chosen);
            layoutSize = chosen;
        }
        else
        {
            built = entry.layout.Build(utf8, faceView, options);
        }
        entry.fittedSize = layoutSize;
        if (built != Text::LayoutError::None)
        {
            return;
        }
        GlyphMeshOptions mesh;
        mesh.sdf = sdf;
        mesh.sdfSize = font.sdfSize;
        mesh.sdfSpread = font.sdfSpread;
        mesh.pixelSnap = text.pixelSnap;
        // Clip 은 상자 밖으로 나간 글리프를 잘라 낸다 - 레이아웃은 줄만 버렸고, 반쯤 걸친 글리프의 사각형과 UV 를 줄인다.
        mesh.clip = text.overflow == Component::TextOverflow::Clip && options.boxWidth > 0.0f && options.boxHeight > 0.0f;
        BuildGlyphQuads(entry.layout, views, count, mesh, entry.quads);

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
        // 외곽선 폭(글자 픽셀)의 문턱은 글자마다다 - 리치 텍스트의 `<size>` 가 섞이면 거리장 픽셀 / 글자 픽셀이 글자마다 다르다.
        const bool outlined = entry.sdf && text.outlineWidth > 0.0f && text.outlineColor.A > 0.0f;
        for (const GlyphQuad& quad : entry.quads)
        {
            const AssetHandle page = m_library.GetPageTexture(entry.fonts[quad.face], quad.page);
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
            if (quad.hasTint)
            {
                // `<color>` 가 RGB 와 알파를 정하고, 텍스트 전체의 알파(color.A)를 곱한다 - 텍스트를 통째로 흐리게 하는 연출이 그대로 된다.
                item.tint = Color{ quad.tint[0] / 255.0f, quad.tint[1] / 255.0f, quad.tint[2] / 255.0f,
                    quad.tint[3] / 255.0f * text.color.A };
            }
            item.pivot = Vec2{ 0.0f, 1.0f };
            item.size = Vec2{ quad.width / ppu, quad.height / ppu };
            item.renderOrder = text.renderOrder;
            if (entry.sdf)
            {
                item.sdfText = true;
                item.filter = TextureFilter::Linear;
                const float channels[4] = { text.outlineColor.R, text.outlineColor.G, text.outlineColor.B, text.outlineColor.A };
                for (int channel = 0; channel < 4; ++channel)
                {
                    item.outlineColor[channel] = static_cast<std::uint8_t>(std::lround(std::clamp(channels[channel], 0.0f, 1.0f) * 255.0f));
                }
                const float outlineEdge = outlined ? SdfOutlineEdge(text.outlineWidth, quad.sdfPerTextPixel, entry.sdfSpread) : 0.5f;
                item.outlineEdge = static_cast<std::uint16_t>(std::lround(std::clamp(outlineEdge, 0.0f, 1.0f) * 65535.0f));
            }
            if (false == m_renderWorld->SubmitSprite(item))
            {
                ++m_droppedGlyphsThisFrame;
            }
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
            AssetHandle handles[MaxFaces];
            FontView views[MaxFaces];
            std::uint32_t count = 0;
            if (false == GatherFonts(text, handles, views, count))
            {
                if (false == entry.warnedMissingFont)
                {
                    Log::Write(LogLevel::Warning, "text",
                        "a Text2D has no usable font and is not drawn - set its fontId or add a project font");
                    entry.warnedMissingFont = true;
                }
                // 폰트가 돌아오면 face 수가 달라 다시 레이아웃된다.
                entry.fontCount = 0;
                entry.quads.Clear();
                entry.hasBounds = false;
                return;
            }
            entry.warnedMissingFont = false;
            bool stale = entry.text.index != text.text.index
                || entry.text.generation != text.text.generation
                || entry.textRevision != store.GetRevision(text.text)
                || entry.fontCount != count
                || entry.optionsKey != MakeOptionsKey(text);
            for (std::uint32_t face = 0; false == stale && face < count; ++face)
            {
                stale = entry.fonts[face].index != handles[face].index
                    || entry.fonts[face].generation != handles[face].generation
                    || entry.fontGenerations[face] != views[face].dataGeneration
                    || entry.atlasGenerations[face] != views[face].atlasGeneration;
            }
            if (stale)
            {
                Relayout(text, entry, handles, views, count);
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
            if (entry != nullptr && entry->fontCount != 0)
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
