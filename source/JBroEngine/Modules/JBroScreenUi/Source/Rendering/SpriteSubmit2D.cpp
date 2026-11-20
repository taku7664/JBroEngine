#include <JBro/ScreenUi/Rendering/SpriteSubmit2D.h>

#include <JBro/ScreenUi/Rendering/Parallax2D.h>
#include <JBro/Runtime/GameObject.h>

#include <algorithm>
#include <cmath>
#include <JBro/Types/Float.h>

namespace JBro::Internal
{
    namespace
    {


        // 캔버스의 `LayerBlend` 와 렌더러의 `CompositeBlend` 는 같은 차례의 같은 열셋이다(D-283). 어긋나면 여기서 빌드가 멈춘다.
        static_assert(LayerBlendCount == CompositeBlendCount, "the canvas and the renderer list the same blends");
        static_assert(static_cast<JBro::UInt32>(LayerBlend::Screen) == static_cast<JBro::UInt32>(CompositeBlend::Screen)
                && static_cast<JBro::UInt32>(LayerBlend::Difference) == static_cast<JBro::UInt32>(CompositeBlend::Difference),
            "in the same order");

        CompositeBlend ToCompositeBlend(LayerBlend blend)
        {
            const UInt32 value = static_cast<JBro::UInt32>(blend);
            return value < CompositeBlendCount ? static_cast<CompositeBlend>(value.Get()) : CompositeBlend::Normal;
        }

        Bool IsSelected(const Object::GameObject* owner, const InstanceId* selection, UInt32 count)
        {
            if (owner == nullptr)
            {
                return false;
            }
            const InstanceId id = owner->GetInstanceId();
            for (UInt32 index = 0; index < count; ++index)
            {
                if (selection[index] == id)
                {
                    return true;
                }
            }
            return false;
        }

        Bool IsInFocus(const Object::GameObject* owner, InstanceId focus)
        {
            for (const Object::GameObject* at = owner; at != nullptr; at = at->GetParent())
            {
                if (at->GetInstanceId() == focus)
                {
                    return true;
                }
            }
            return false;
        }
    }

    // `offsetX`·`offsetY` 는 월드에서 옮길 양이다(패럴랙스, D-286). 오브젝트 변환 뒤에 더한다 - 레이어 전체가 같이 움직인다.
    // `piece` 는 아이템의 어느 조각인가다(D-351). 온 조각이면 쿼드 하나가 아이템 전체다.
    SpriteSubmit BuildSprite(const SpriteRenderItem& source, Float offsetX, Float offsetY, const SpritePiece& piece)
    {
        // Built-in geometry is a centered unit quad. Apply pivot/size before the object transform.
        // 조각은 그려지는 사각형 안의 비율(왼쪽 아래 원점)이라, 크기를 조각만큼 줄이고 가운데를 조각의 가운데로 옮긴다.
        const Matrix3x2 geometry{(piece.gu1 - piece.gu0) * source.size.x, 0.0f, 0.0f, (piece.gv1 - piece.gv0) * source.size.y,
            ((piece.gu0 + piece.gu1) * 0.5f - source.pivot.x) * source.size.x,
            ((piece.gv0 + piece.gv1) * 0.5f - source.pivot.y) * source.size.y};
        const Matrix3x2 world = MultiplyMatrix3x2(geometry, source.world);
        SpriteSubmit result;
        // Framework uses row vectors; Graphics/HLSL uses column vectors.
        result.world.linear[0] = world.m11;
        result.world.linear[1] = world.m21;
        result.world.linear[2] = world.m12;
        result.world.linear[3] = world.m22;
        result.world.translation[0] = world.m31 + offsetX;
        result.world.translation[1] = world.m32 + offsetY;
        // 깊이 버퍼가 아직 없다. 그리는 순서는 RenderWorld2D 가 정렬로 끝내고,
        // 이 값은 그 정렬을 GPU 로 옮기기 전까지 평면을 유지한다.
        result.world.depth = 0.0f;
        result.texture = source.texture;
        // 텍스처 비율은 왼쪽 아래 원점이고 uv 의 v 는 위가 0 이라 세로는 뒤집어 잰다.
        result.uvRect[0] = source.uvRect[0] + piece.tu0 * source.uvRect[2];
        result.uvRect[1] = source.uvRect[1] + (1.0f - piece.tv1) * source.uvRect[3];
        result.uvRect[2] = (piece.tu1 - piece.tu0) * source.uvRect[2];
        result.uvRect[3] = (piece.tv1 - piece.tv0) * source.uvRect[3];
        // 에셋의 `TextureFilter` 가 렌더러의 `SpriteFilter` 로 간다(D-117). `Default` 는 라이브러리가 이미 풀었다.
        result.filter = source.filter == TextureFilter::Linear ? SpriteFilter::Linear : SpriteFilter::Nearest;
        result.material = source.material;
        result.tint[0] = source.tint.R;
        result.tint[1] = source.tint.G;
        result.tint[2] = source.tint.B;
        result.tint[3] = source.tint.A;
        if (source.sdfText)
        {
            result.shading = SpriteShading::SdfText;
            result.outlineColor[0] = source.outlineColor[0];
            result.outlineColor[1] = source.outlineColor[1];
            result.outlineColor[2] = source.outlineColor[2];
            result.outlineColor[3] = source.outlineColor[3];
            result.outlineEdge = source.outlineEdge;
        }
        return result;
    }

    Bool PushSprites(const RenderWorld2D& world, Renderer& renderer, Bool editorView, const SpriteFilterRule& rule,
        std::size_t first, std::size_t last)
    {
        constexpr std::size_t BatchSize = 64;
        SpriteSubmit batch[BatchSize];
        Bool accepted = world.GetDroppedSpriteCount() == 0;
        std::size_t next = first;
        const std::size_t end = last < world.GetSpriteCount() ? last : world.GetSpriteCount();
        // **얹어야 하는 레이어는 렌더러의 묶음으로 낸다**(D-279). 정렬이 레이어 차례를 맨 위에 두므로 한 레이어의 아이템은 이어서 온다 -
        // 레이어가 바뀌는 자리에서 모아 둔 것을 먼저 내고 묶음을 닫고 연다. 묶음을 열지 못하면(상한) 그 레이어는 그대로 그린다.
        constexpr Int32 NoLayer = -1;
        Int32 openLayer = NoLayer;
        Bool layerBegun = false;
        // 열린 빛을 받는 구간이 있는가(D-291). 레이어 묶음과 따로 논다 - 렌더러가 둘을 겹쳐 받는다.
        Bool lit = false;
        // 열린 클립 구간(D-351). 마스크가 같은 아이템은 정렬상 대개 이어져 오므로 구간이 적다. 사각형은 패럴랙스만큼 같이 옮긴다.
        Bool clipOpen = false;
        Bool clipRefused = false;
        Float openClip[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        std::size_t count = 0;
        // 렌더러가 하나라도 거절하면 그 뒤는 내지 않는다(제출 상한). 앞에서 버린 아이템이 있었던 것은 결과만 바꾼다.
        Bool refused = false;
        const auto flush = [&]() {
            if (count != 0 && false == renderer.SubmitSprites({batch, static_cast<JBro::UInt32>(count)}))
            {
                refused = true;
            }
            count = 0;
        };
        while (next < end && false == refused)
        {
            const SpriteRenderItem& item = world.GetSprite(next);
            ++next;
            if (editorView && item.owner != nullptr && item.owner->IsEditorHidden())
            {
                continue;
            }
            if (item.screenSpace != rule.screenSpace || (false == rule.anyScaleMode && item.scaleMode != rule.scaleMode))
            {
                continue;
            }
            if (rule.excludeTexture.generation != 0 && item.texture.index == rule.excludeTexture.index
                && item.texture.generation == rule.excludeTexture.generation)
            {
                continue;
            }
            if (rule.focus != InvalidInstanceId && false == IsInFocus(item.owner, rule.focus))
            {
                continue;
            }
            if (rule.selection != nullptr && false == IsSelected(item.owner, rule.selection, rule.selectionCount))
            {
                continue;
            }
            if (rule.layerOrder >= 0 && item.layerOrder != static_cast<std::uint16_t>(rule.layerOrder))
            {
                continue;
            }
            if (rule.layers != nullptr && false == rule.layers->Draws(item.layerOrder))
            {
                continue;
            }
            const PostProcessStack* layerPost =
                rule.composite && rule.layerPostProcess ? &world.GetLayerPostProcess(item.layerOrder) : nullptr;
            const Bool needsComposite = rule.composite
                && (item.layerBlend != LayerBlend::Normal || item.layerOpacity < 1.0f || (layerPost != nullptr && layerPost->entries.size != 0));
            const Bool wantLit = rule.lighting && item.layerLit && false == item.screenSpace;
            if (wantLit != lit)
            {
                flush();
                renderer.SetSpriteLighting(wantLit);
                lit = wantLit;
            }
            const Int32 wanted = needsComposite ? Int32(static_cast<JBro::Int32>(item.layerOrder)) : NoLayer;
            if (wanted != openLayer)
            {
                flush();
                if (layerBegun)
                {
                    renderer.EndLayer();
                }
                layerBegun = wanted != NoLayer
                    && renderer.BeginLayer(ToCompositeBlend(item.layerBlend), item.layerOpacity,
                        layerPost != nullptr ? *layerPost : PostProcessStack{});
                openLayer = wanted;
            }
            Float offsetX = 0.0f;
            Float offsetY = 0.0f;
            if (rule.parallaxView != nullptr && false == item.screenSpace && item.layerParallax != 1.0f
                && false == ComputeParallaxOffset2D(*rule.parallaxView, item.layerParallax, offsetX, offsetY))
            {
                offsetX = 0.0f;
                offsetY = 0.0f;
            }
            Float clip[4] = { item.clip[0] + offsetX, item.clip[1] + offsetY, item.clip[2] + offsetX, item.clip[3] + offsetY };
            // 구간 상한을 넘어 한 번 거절당하면 이 뷰에서는 더 묻지 않는다 - 아이템마다 묶음을 끊고 다시 묻는 일이 없다.
            const Bool wantClip = item.clipped && false == clipRefused;
            const Bool sameClip = wantClip == clipOpen
                && (false == clipOpen || (clip[0] == openClip[0] && clip[1] == openClip[1] && clip[2] == openClip[2] && clip[3] == openClip[3]));
            if (false == sameClip)
            {
                flush();
                clipOpen = wantClip && renderer.SetSpriteClip(clip);
                clipRefused = wantClip && false == clipOpen;
                if (false == wantClip)
                {
                    renderer.SetSpriteClip(nullptr);
                }
                for (UInt32 edge = 0; edge < 4; ++edge)
                {
                    openClip[edge] = clip[edge];
                }
            }
            if (item.drawMode == Component::SpriteDrawMode::Simple)
            {
                batch[count] = BuildSprite(item, offsetX, offsetY, WholeSpritePiece());
                ++count;
                if (count == BatchSize)
                {
                    flush();
                }
                continue;
            }
            // 9-슬라이스·타일·채우기는 조각 여럿이다(D-351). 묶음이 차면 그때그때 낸다.
            SpritePiece pieces[MaxSpritePieces];
            const UInt32 pieceCount = BuildSpritePieces(item, pieces, MaxSpritePieces);
            for (UInt32 index = 0; index < pieceCount; ++index)
            {
                batch[count] = BuildSprite(item, offsetX, offsetY, pieces[index]);
                ++count;
                if (count == BatchSize)
                {
                    flush();
                }
            }
        }
        flush();
        if (clipOpen)
        {
            renderer.SetSpriteClip(nullptr);
        }
        if (layerBegun)
        {
            renderer.EndLayer();
        }
        // 뒤에 같은 뷰에 낼 것(포커스 막·디버그 선)은 빛을 받지 않는다.
        if (lit)
        {
            renderer.SetSpriteLighting(false);
        }
        return accepted && false == refused;
    }

    // 화면 레이어의 정사영이다(D-237). 가운데 원점, y 위, 기준 픽셀 - 앵커와 같은 `ComputeScreenExtent` 로 잰다.
    // 뷰포트는 그려지는 사각형이다 - `PixelPerfect` 카메라의 레터박스 안에 화면 레이어도 그린다(D-239).
    Bool BuildScreenCamera(const ScreenExtent& extent, const ScreenArea& area, CameraParams& result)
    {
        if (false == (area.width > 0.0f) || false == (area.height > 0.0f)
            || false == (extent.halfWidth > 0.0f) || false == (extent.halfHeight > 0.0f))
        {
            return false;
        }
        result.view = Matrix4x4{};
        result.projection = {{1.0f / extent.halfWidth, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f / extent.halfHeight, 0.0f, 0.0f,
            0.0f, 0.0f, 0.5f, 0.5f,
            0.0f, 0.0f, 0.0f, 1.0f}};
        result.viewport.x = area.x;
        result.viewport.y = area.y;
        result.viewport.width = area.width;
        result.viewport.height = area.height;
        // 월드 뷰가 없으면(카메라 없음) 이 뷰가 처음이라 대상을 지운다. 검정이다.
        result.clearColor[0] = 0.0f;
        result.clearColor[1] = 0.0f;
        result.clearColor[2] = 0.0f;
        result.clearColor[3] = 1.0f;
        return true;
    }

    // 그리는 순서의 화면 아이템을 맞춤 방식이 같은 것끼리 이어진 덩어리로 나눠 덩어리마다 뷰 하나에 그린다.
    Bool SubmitScreenViews(const RenderWorld2D& world, Renderer& renderer, const ScreenSpaceFrame& frame, Bool& submitted)
    {
        submitted = false;
        if (world.GetScreenSpriteCount() == 0)
        {
            return true;
        }
        ScreenArea area;
        if (false == GetScreenArea(frame, area))
        {
            return false;
        }
        std::size_t index = 0;
        const std::size_t count = world.GetSpriteCount();
        while (index < count)
        {
            if (false == world.GetSprite(index).screenSpace)
            {
                ++index;
                continue;
            }
            const ScreenScaleMode mode = world.GetSprite(index).scaleMode;
            std::size_t end = index + 1;
            while (end < count && (false == world.GetSprite(end).screenSpace || world.GetSprite(end).scaleMode == mode))
            {
                ++end;
            }
            ScreenExtent extent;
            CameraParams parameters;
            if (false == ComputeScreenExtent(mode, frame, extent) || false == BuildScreenCamera(extent, area, parameters)
                || false == renderer.BeginView(parameters))
            {
                return false;
            }
            SpriteFilterRule rule;
            rule.screenSpace = true;
            rule.scaleMode = mode;
            rule.anyScaleMode = false;
            const Bool accepted = PushSprites(world, renderer, false, rule, index, end);
            if (false == renderer.EndView() || false == accepted)
            {
                return false;
            }
            submitted = true;
            index = end;
        }
        return true;
    }
}
