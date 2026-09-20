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

    namespace
    {
        // 모아 둔 메시를 이미 열린 뷰에 밀어 넣는다. 게임 뷰와 캔버스 뷰가 같은 목록을 쓴다.
        bool PushMeshes(const RenderWorld3D& world, Renderer& renderer)
        {
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
            return accepted;
        }
    }

    RenderResult SubmitEditorView3D(
        const RenderWorld3D& world, Renderer& renderer, const EditorViewDesc& view)
    {
        if (false == view.target.IsValid() || view.extent.width == 0 || view.extent.height == 0)
        {
            return RenderResult::NothingToSubmit;
        }
        // **게임 카메라를 빌리지 않는다.** 캔버스 뷰는 카메라가 하나도 없는 캔버스도
        // 보여 주어야 하고, 어디서 볼지는 편집하는 사람이 정한다.
        //
        // 궤도 카메라다: 바라보는 점 둘레를 도는 자리에 선다. 각에서 방향을 내고,
        // 그 방향의 반대로 `distance` 만큼 물러난 곳이 카메라 자리다.
        constexpr float Degrees = 3.14159265f / 180.0f;
        RenderCamera3D editor;
        editor.projection = Component::CameraProjection3D::Perspective;
        editor.verticalFieldOfView = view.verticalFieldOfView;
        editor.rotation = FromEuler(Vec3{
            view.pitchDegrees * Degrees, view.yawDegrees * Degrees, 0.0f});
        // 카메라가 보는 쪽은 -z 다(오른손 좌표계의 뷰 규약). 그 반대로 물러난다.
        const Vec3 forward = Rotate(editor.rotation, Vec3{0.0f, 0.0f, -1.0f});
        const Vec3 target{view.centerX, view.centerY, view.centerZ};
        editor.position = Vec3{
            target.x - forward.x * view.distance,
            target.y - forward.y * view.distance,
            target.z - forward.z * view.distance};
        editor.clearColor = Color{
            view.clearColor[0], view.clearColor[1], view.clearColor[2], view.clearColor[3]};

        CameraParams parameters;
        if (false == BuildCamera3D(editor, view.extent, parameters))
        {
            return RenderResult::Failed;
        }
        parameters.target = view.target;
        parameters.targetExtent = view.extent;
        if (false == renderer.BeginView(parameters))
        {
            return RenderResult::Failed;
        }
        const bool accepted = PushMeshes(world, renderer);
        const bool closed = renderer.EndView();
        return (accepted && closed) ? RenderResult::Submitted : RenderResult::Failed;
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
        const bool accepted = PushMeshes(world, renderer);
        const bool closed = renderer.EndView();
        return (accepted && closed) ? RenderResult::Submitted : RenderResult::Failed;
    }
}
