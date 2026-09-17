#include "RenderBridge3D.h"

#include <JBro/Framework3DSystem/Math3DMatrix.h>
#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>
#include <JBro/Graphics/Renderer.h>

#include <algorithm>
#include <cmath>

namespace JBro::Internal
{
    bool BuildCamera3D(const RenderCamera3D& source, const Extent2D& extent, CameraParams& result)
    {
        if (extent.width == 0 || extent.height == 0)
        {
            return false;
        }
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        bool projected = false;
        if (source.projection == Component::CameraProjection3D::Perspective)
        {
            const float radians = source.verticalFieldOfView * (3.14159265f / 180.0f);
            projected = MakePerspectiveMatrix(radians, aspect, source.nearPlane, source.farPlane,
                result.projection);
        }
        else
        {
            projected = MakeOrthographicMatrix(source.orthographicSize, aspect, source.nearPlane,
                source.farPlane, result.projection);
        }
        if (false == projected)
        {
            return false;
        }
        result.view = MakeViewMatrix(source.position, source.rotation);
        for (float value : result.view.values)
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

    RenderResult SubmitRenderWorld3D(const RenderWorld3D& world, Renderer& renderer)
    {
        const RenderCamera3D* camera = world.GetCamera();
        if (camera == nullptr)
        {
            return RenderResult::NothingToSubmit;
        }
        CameraParams parameters;
        if (false == BuildCamera3D(*camera, renderer.GetFrameExtent(), parameters)
            || false == renderer.BeginView(parameters))
        {
            return RenderResult::Failed;
        }
        constexpr std::size_t BatchSize = 64;
        MeshSubmit batch[BatchSize];
        bool accepted = world.GetDroppedMeshCount() == 0;
        for (std::size_t offset = 0; offset < world.GetMeshCount(); offset += BatchSize)
        {
            const std::size_t count = (std::min)(BatchSize, world.GetMeshCount() - offset);
            for (std::size_t index = 0; index < count; ++index)
            {
                const MeshRenderItem& item = world.GetMesh(offset + index);
                MeshSubmit& submit = batch[index];
                submit.world = MakeTransformMatrix3D(item.position, item.rotation, item.scale);
                submit.mesh = item.mesh;
                submit.material = item.material;
                submit.tint[0] = item.tint.R;
                submit.tint[1] = item.tint.G;
                submit.tint[2] = item.tint.B;
                submit.tint[3] = item.tint.A;
            }
            if (false == renderer.SubmitMeshes({batch, static_cast<std::uint32_t>(count)}))
            {
                accepted = false;
                break;
            }
        }
        const bool closed = renderer.EndView();
        return (accepted && closed) ? RenderResult::Submitted : RenderResult::Failed;
    }
}
