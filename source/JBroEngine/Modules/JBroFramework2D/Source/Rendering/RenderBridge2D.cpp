#include "RenderBridge2D.h"

#include <JBro/Framework2D/Rendering/RenderWorld2D.h>
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
            SpriteSubmit result;
            result.world = ToColumnMatrix(MultiplyMatrix3x2(geometry, source.world));
            result.sprite = source.sprite;
            result.material = source.material;
            result.tint[0] = source.tint.R;
            result.tint[1] = source.tint.G;
            result.tint[2] = source.tint.B;
            result.tint[3] = source.tint.A;
            result.renderOrder = source.renderOrder;
            return result;
        }
    }

    bool SubmitRenderWorld2D(const RenderWorld2D& world, Renderer& renderer)
    {
        const RenderCamera2D* camera = world.GetCamera();
        if (camera == nullptr)
        {
            return true;
        }
        CameraParams parameters;
        if (false == BuildCamera(*camera, renderer.GetSurfaceExtent(), parameters)
            || false == renderer.BeginView(parameters))
        {
            return false;
        }
        constexpr std::size_t BatchSize = 64;
        SpriteSubmit batch[BatchSize];
        bool accepted = world.GetDroppedSpriteCount() == 0;
        for (std::size_t offset = 0; offset < world.GetSpriteCount(); offset += BatchSize)
        {
            const std::size_t count = (std::min)(BatchSize, world.GetSpriteCount() - offset);
            for (std::size_t index = 0; index < count; ++index)
            {
                batch[index] = BuildSprite(world.GetSprites()[offset + index]);
            }
            if (false == renderer.SubmitSprites({batch, static_cast<std::uint32_t>(count)}))
            {
                accepted = false;
                break;
            }
        }
        const bool closed = renderer.EndView();
        return accepted && closed;
    }
}
