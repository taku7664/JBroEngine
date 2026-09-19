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
            result.material = source.material;
            result.tint[0] = source.tint.R;
            result.tint[1] = source.tint.G;
            result.tint[2] = source.tint.B;
            result.tint[3] = source.tint.A;
            return result;
        }
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
        const bool closed = renderer.EndView();
        return (accepted && closed) ? RenderResult::Submitted : RenderResult::Failed;
    }
}
