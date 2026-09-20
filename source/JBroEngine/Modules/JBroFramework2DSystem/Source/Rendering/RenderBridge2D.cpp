#include "RenderBridge2D.h"

#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
#include <JBro/Graphics/Renderer.h>

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
            return result;
        }
    }

    namespace
    {
        // 모아 둔 스프라이트를 이미 열린 뷰에 밀어 넣는다. 게임 뷰와 캔버스 뷰가
        // 같은 목록을 쓰므로 이 부분만 따로 뗀다.
        bool PushSprites(const RenderWorld2D& world, Renderer& renderer)
        {
            constexpr std::size_t BatchSize = 64;
            SpriteSubmit batch[BatchSize];
            bool accepted = world.GetDroppedSpriteCount() == 0;
            for (std::size_t offset = 0; offset < world.GetSpriteCount(); offset += BatchSize)
            {
                const std::size_t count = (std::min)(BatchSize, world.GetSpriteCount() - offset);
                for (std::size_t index = 0; index < count; ++index)
                {
                    batch[index] = BuildSprite(world.GetSprite(offset + index));
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
        const bool accepted = PushSprites(world, renderer);
        const bool closed = renderer.EndView();
        return (accepted && closed) ? RenderResult::Submitted : RenderResult::Failed;
    }

    RenderResult SubmitRenderWorld2D(const RenderWorld2D& world, Renderer& renderer)
    {
        const RenderCamera2D* camera = world.GetCamera();
        if (camera == nullptr)
        {
            // 카메라가 없는 것은 오류가 아니다. 그릴 대상이 없을 뿐이다.
            return RenderResult::NothingToSubmit;
        }
        CameraParams parameters;
        // **창이 아니라 이번 프레임이 그려지는 크기다.** 에디터에서 게임은
        // 창과 다른 크기의 텍스처로 간다 - 창으로 잡으면 게임이 보는 화면이
        // 에디터 창 모양을 따라가고, 뷰포트가 타깃 밖으로 나간다.
        if (false == BuildCamera(*camera, renderer.GetFrameExtent(), parameters)
            || false == renderer.BeginView(parameters))
        {
            return RenderResult::Failed;
        }
        const bool accepted = PushSprites(world, renderer);
        const bool closed = renderer.EndView();
        return (accepted && closed) ? RenderResult::Submitted : RenderResult::Failed;
    }
}
