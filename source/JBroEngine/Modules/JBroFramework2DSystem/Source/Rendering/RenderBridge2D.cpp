#include "RenderBridge2D.h"

#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Runtime/GameObject.h>

#include <algorithm>
#include <cmath>

namespace JBro::Internal
{
    namespace
    {
        Matrix4x4 ToColumnMatrix(const Matrix3x2& matrix)
        {
            // Framework uses row vectors; Graphics/HLSL uses column vectors in row-major storage.
            return {{matrix.m11, matrix.m21, 0.0f, matrix.m31,
                matrix.m12, matrix.m22, 0.0f, matrix.m32,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f}};
        }

        bool BuildCamera(const RenderCamera2D& source, Extent2D extent, CameraParams& result)
        {
            if (extent.width == 0 || extent.height == 0
                || false == std::isfinite(source.orthographicSize) || source.orthographicSize <= 0.0f
                || false == std::isfinite(source.nearPlane) || false == std::isfinite(source.farPlane)
                || source.nearPlane >= source.farPlane)
            {
                return false;
            }
            // PixelPerfect's reference resolution/scaling contract awaits user definition.
            if (source.projection != Component::CameraProjection2D::Orthographic)
            {
                return false;
            }
            const double halfHeight = source.orthographicSize;
            const double halfWidth = halfHeight * extent.width / extent.height;
            const double depth = static_cast<double>(source.farPlane) - source.nearPlane;
            result.view = ToColumnMatrix(source.view);
            result.projection = {{static_cast<float>(1.0 / halfWidth), 0.0f, 0.0f, 0.0f,
                0.0f, static_cast<float>(1.0 / halfHeight), 0.0f, 0.0f,
                0.0f, 0.0f, static_cast<float>(1.0 / depth), static_cast<float>(-source.nearPlane / depth),
                0.0f, 0.0f, 0.0f, 1.0f}};
            for (float value : result.projection.values)
            {
                if (false == std::isfinite(value))
                {
                    return false;
                }
            }
            result.viewport.width = static_cast<float>(extent.width);
            result.viewport.height = static_cast<float>(extent.height);
            result.clearColor[0] = source.clearColor.R;
            result.clearColor[1] = source.clearColor.G;
            result.clearColor[2] = source.clearColor.B;
            result.clearColor[3] = source.clearColor.A;
            return true;
        }

        SpriteSubmit BuildSprite(const SpriteRenderItem& source)
        {
            // Built-in geometry is a centered unit quad. Apply pivot/size before the object transform.
            const Matrix3x2 geometry{source.size.x, 0.0f, 0.0f, source.size.y,
                (0.5f - source.pivot.x) * source.size.x,
                (0.5f - source.pivot.y) * source.size.y};
            const Matrix3x2 world = MultiplyMatrix3x2(geometry, source.world);
            SpriteSubmit result;
            // Framework uses row vectors; Graphics/HLSL uses column vectors.
            result.world.linear[0] = world.m11;
            result.world.linear[1] = world.m21;
            result.world.linear[2] = world.m12;
            result.world.linear[3] = world.m22;
            result.world.translation[0] = world.m31;
            result.world.translation[1] = world.m32;
            // 깊이 버퍼가 아직 없다. 그리는 순서는 RenderWorld2D 가 정렬로 끝내고,
            // 이 값은 그 정렬을 GPU 로 옮기기 전까지 평면을 유지한다.
            result.world.depth = 0.0f;
            result.texture = source.texture;
            result.uvRect[0] = source.uvRect[0];
            result.uvRect[1] = source.uvRect[1];
            result.uvRect[2] = source.uvRect[2];
            result.uvRect[3] = source.uvRect[3];
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
    }

    namespace
    {
        // 모아 둔 스프라이트를 이미 열린 뷰에 밀어 넣는다. 게임 뷰와 캔버스 뷰가
        // 같은 목록을 쓰므로 이 부분만 따로 뗀다.
        //
        // `editorView` 면 **에디터에서 감춘 오브젝트를 건너뛴다**(D-163, 기존 `EditorHidden`). 게임 뷰는 보지 않는다 -
        // 감추는 것은 편집을 위한 것이지 게임의 모습이 아니다.
        // 어느 아이템을 그 뷰에 넣는가. 월드 뷰는 월드 레이어만, 화면 뷰는 그 맞춤 방식의 화면 레이어만이다(D-233).
        struct SpriteFilterRule
        {
            bool screenSpace = false;
            ScreenScaleMode scaleMode = ScreenScaleMode::FixedHeight;
            bool anyScaleMode = true;
        };

        bool PushSprites(const RenderWorld2D& world, Renderer& renderer, bool editorView, const SpriteFilterRule& rule,
            std::size_t first = 0, std::size_t last = static_cast<std::size_t>(-1))
        {
            constexpr std::size_t BatchSize = 64;
            SpriteSubmit batch[BatchSize];
            bool accepted = world.GetDroppedSpriteCount() == 0;
            std::size_t next = first;
            const std::size_t end = last < world.GetSpriteCount() ? last : world.GetSpriteCount();
            while (next < end)
            {
                std::size_t count = 0;
                while (count < BatchSize && next < end)
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
                    batch[count] = BuildSprite(item);
                    ++count;
                }
                if (count == 0)
                {
                    break;
                }
                if (false == renderer.SubmitSprites({batch, static_cast<std::uint32_t>(count)}))
                {
                    accepted = false;
                    break;
                }
            }
            return accepted;
        }
    }

    RenderResult SubmitEditorView2D(
        const RenderWorld2D& world, Renderer& renderer, const EditorViewDesc& view)
    {
        if (false == view.target.IsValid() || view.extent.width == 0 || view.extent.height == 0)
        {
            return RenderResult::NothingToSubmit;
        }
        // **게임 카메라를 빌리지 않는다.** 캔버스 뷰는 카메라가 하나도 없는 캔버스도
        // 보여 주어야 하고, 배율과 위치는 편집하는 사람이 정한다.
        RenderCamera2D editor;
        editor.orthographicSize = view.orthographicSize;
        // 회전 없는 카메라의 뷰 행렬은 화면 한가운데를 원점으로 옮기는 것이다.
        editor.view = Matrix3x2{1.0f, 0.0f, 0.0f, 1.0f, -view.centerX, -view.centerY};
        editor.clearColor = Color{
            view.clearColor[0], view.clearColor[1], view.clearColor[2], view.clearColor[3]};

        CameraParams parameters;
        if (false == BuildCamera(editor, view.extent, parameters))
        {
            return RenderResult::Failed;
        }
        parameters.target = view.target;
        parameters.targetExtent = view.extent;
        if (false == renderer.BeginView(parameters))
        {
            return RenderResult::Failed;
        }
        // 캔버스 뷰는 월드 레이어만 보인다. 화면 레이어는 UI 보기(에디터 2 단계)의 것이다 - 좌표가 기준 픽셀이라 월드와 섞으면 백 배쯤 크다.
        const bool accepted = PushSprites(world, renderer, true, SpriteFilterRule{});
        const bool closed = renderer.EndView();
        return (accepted && closed) ? RenderResult::Submitted : RenderResult::Failed;
    }

    namespace
    {
        // 화면 레이어의 정사영이다(D-233). 가운데 원점, y 위, 기준 픽셀 - 앵커와 같은 `ComputeScreenExtent` 로 잰다.
        bool BuildScreenCamera(const ScreenExtent& extent, Extent2D target, CameraParams& result)
        {
            if (target.width == 0 || target.height == 0 || false == (extent.halfWidth > 0.0f) || false == (extent.halfHeight > 0.0f))
            {
                return false;
            }
            result.view = Matrix4x4{};
            result.projection = {{1.0f / extent.halfWidth, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f / extent.halfHeight, 0.0f, 0.0f,
                0.0f, 0.0f, 0.5f, 0.5f,
                0.0f, 0.0f, 0.0f, 1.0f}};
            result.viewport.width = static_cast<float>(target.width);
            result.viewport.height = static_cast<float>(target.height);
            // 월드 뷰가 없으면(카메라 없음) 이 뷰가 처음이라 대상을 지운다. 검정이다.
            result.clearColor[0] = 0.0f;
            result.clearColor[1] = 0.0f;
            result.clearColor[2] = 0.0f;
            result.clearColor[3] = 1.0f;
            return true;
        }

        // 그리는 순서의 화면 아이템을 맞춤 방식이 같은 것끼리 이어진 덩어리로 나눠 덩어리마다 뷰 하나에 그린다.
        bool SubmitScreenViews(const RenderWorld2D& world, Renderer& renderer, bool& submitted)
        {
            submitted = false;
            if (world.GetScreenSpriteCount() == 0)
            {
                return true;
            }
            ScreenSpaceFrame frame = world.GetScreenSpace();
            const Extent2D target = renderer.GetFrameExtent();
            if (false == (frame.targetWidth > 0.0f) || false == (frame.targetHeight > 0.0f))
            {
                frame.targetWidth = static_cast<float>(target.width);
                frame.targetHeight = static_cast<float>(target.height);
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
                if (false == ComputeScreenExtent(mode, frame, extent) || false == BuildScreenCamera(extent, target, parameters)
                    || false == renderer.BeginView(parameters))
                {
                    return false;
                }
                SpriteFilterRule rule;
                rule.screenSpace = true;
                rule.scaleMode = mode;
                rule.anyScaleMode = false;
                const bool accepted = PushSprites(world, renderer, false, rule, index, end);
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

    RenderResult SubmitRenderWorld2D(const RenderWorld2D& world, Renderer& renderer)
    {
        const RenderCamera2D* camera = world.GetCamera();
        bool worldSubmitted = false;
        // 카메라가 없는 것은 오류가 아니다. 월드를 그리지 않을 뿐이다 - 화면 레이어(메뉴만 있는 캔버스)는 아래에서 그린다.
        if (camera != nullptr)
        {
            CameraParams parameters;
            // **창이 아니라 이번 프레임이 그려지는 크기다.** 에디터에서 게임은
            // 창과 다른 크기의 텍스처로 간다 - 창으로 잡으면 게임이 보는 화면이
            // 에디터 창 모양을 따라가고, 뷰포트가 타깃 밖으로 나간다.
            if (false == BuildCamera(*camera, renderer.GetFrameExtent(), parameters)
                || false == renderer.BeginView(parameters))
            {
                return RenderResult::Failed;
            }
            const bool accepted = PushSprites(world, renderer, false, SpriteFilterRule{});
            const bool closed = renderer.EndView();
            if (false == accepted || false == closed)
            {
                return RenderResult::Failed;
            }
            worldSubmitted = true;
        }
        // 화면 레이어는 월드 위에 그린다(D-233). 렌더러는 대상을 첫 뷰에서만 지운다.
        bool screenSubmitted = false;
        if (false == SubmitScreenViews(world, renderer, screenSubmitted))
        {
            return RenderResult::Failed;
        }
        return worldSubmitted || screenSubmitted ? RenderResult::Submitted : RenderResult::NothingToSubmit;
    }
}
