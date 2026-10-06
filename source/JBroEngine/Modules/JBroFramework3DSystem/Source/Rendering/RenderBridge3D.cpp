#include "RenderBridge3D.h"

#include <JBro/Framework3DSystem/Math3DMatrix.h>
#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Host/DebugDrawSystem.h>
#include <JBro/Runtime/GameObject.h>

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
        // 캔버스의 `LayerBlend` 와 렌더러의 `CompositeBlend` 는 같은 차례의 같은 열셋이다(D-283). 어긋나면 여기서 빌드가 멈춘다.
        static_assert(LayerBlendCount == CompositeBlendCount, "the canvas and the renderer list the same blends");
        static_assert(static_cast<std::uint32_t>(LayerBlend::Screen) == static_cast<std::uint32_t>(CompositeBlend::Screen)
                && static_cast<std::uint32_t>(LayerBlend::Difference) == static_cast<std::uint32_t>(CompositeBlend::Difference),
            "in the same order");

        CompositeBlend ToCompositeBlend3D(LayerBlend blend)
        {
            const std::uint32_t value = static_cast<std::uint32_t>(blend);
            return value < CompositeBlendCount ? static_cast<CompositeBlend>(value) : CompositeBlend::Normal;
        }

        // 모아 둔 메시를 이미 열린 뷰에 밀어 넣는다. 게임 뷰와 캔버스 뷰가 같은 목록을 쓴다.
        // `editorView` 면 에디터에서 감춘 오브젝트를 건너뛴다(D-163). 게임 뷰는 보지 않는다.
        bool PushMeshes(const RenderWorld3D& world, Renderer& renderer, bool editorView, std::uint16_t layerOrder)
        {
            constexpr std::size_t BatchSize = 64;
            MeshSubmit batch[BatchSize];
            bool accepted = world.GetDroppedMeshCount() == 0;
            std::size_t next = 0;
            while (next < world.GetMeshCount())
            {
                std::size_t count = 0;
                while (count < BatchSize && next < world.GetMeshCount())
                {
                    const MeshRenderItem& item = world.GetMesh(next);
                    ++next;
                    if ((editorView && item.owner != nullptr && item.owner->IsEditorHidden()) || item.layerOrder != layerOrder)
                    {
                        continue;
                    }
                    MeshSubmit& submit = batch[count];
                    ++count;
                    submit.world = MakeTransformMatrix3D(item.position, item.rotation, item.scale);
                    submit.mesh = item.mesh;
                    submit.material = item.material;
                    submit.tint[0] = item.tint.R;
                    submit.tint[1] = item.tint.G;
                    submit.tint[2] = item.tint.B;
                    submit.tint[3] = item.tint.A;
                }
                if (count == 0)
                {
                    break;
                }
                if (false == renderer.SubmitMeshes({batch, static_cast<std::uint32_t>(count)}))
                {
                    accepted = false;
                    break;
                }
            }
            return accepted;
        }

        // 3D 텍스트의 글자를 이 뷰의 카메라로 뒤→앞으로 늘어놓는다(D-222). 같은 텍스트의 글자는 한 자리(오브젝트 위치)라 거리가 같으므로
        // 낸 순서가 남는다. 카메라마다 한 번이다 - 레이어마다 다시 정렬하지 않고 이 차례에서 그 레이어 것만 고른다(D-280).
        void SortWorldTexts(const RenderWorld3D& world, bool editorView, const Vector3& cameraPosition)
        {
            Array<std::uint32_t>& order = world.GetTextOrderScratch();
            order.Clear();
            for (std::size_t index = 0; index < world.GetTextCount(); ++index)
            {
                const WorldTextRenderItem& item = world.GetText(index);
                if (editorView && item.owner != nullptr && item.owner->IsEditorHidden())
                {
                    continue;
                }
                order.Add(static_cast<std::uint32_t>(index));
            }
            const auto distance = [&](std::uint32_t index) {
                const Vector3& position = world.GetText(index).position;
                const float dx = position.x - cameraPosition.x;
                const float dy = position.y - cameraPosition.y;
                const float dz = position.z - cameraPosition.z;
                return dx * dx + dy * dy + dz * dz;
            };
            std::sort(order.Data(), order.Data() + order.Size(), [&](std::uint32_t left, std::uint32_t right) {
                const float leftDistance = distance(left);
                const float rightDistance = distance(right);
                if (leftDistance != rightDistance)
                {
                    return leftDistance > rightDistance;
                }
                return left < right;
            });
        }

        // 정렬해 둔 글자 중 이 레이어의 것을 낸다. 빌보드는 오브젝트 회전 대신 카메라 회전을 쓴다 - 판의 +Z 가 카메라 쪽이고 가로가
        // 카메라의 오른쪽이다.
        bool PushWorldTexts(const RenderWorld3D& world, Renderer& renderer, const Quaternion& cameraRotation, std::uint16_t layerOrder)
        {
            const Array<std::uint32_t>& order = world.GetTextOrderScratch();
            constexpr std::size_t BatchSize = 64;
            WorldTextSubmit batch[BatchSize];
            bool accepted = world.GetDroppedTextCount() == 0;
            std::size_t next = 0;
            while (next < order.Size())
            {
                std::uint32_t count = 0;
                while (count < BatchSize && next < order.Size())
                {
                    const WorldTextRenderItem& item = world.GetText(order[next]);
                    ++next;
                    if (item.layerOrder != layerOrder)
                    {
                        continue;
                    }
                    const Matrix4x4 object = MakeTransformMatrix3D(item.position, item.billboard ? cameraRotation : item.rotation, item.scale);
                    // 단위 쿼드(-0.5..0.5)를 글자 사각형으로: 가운데로 옮기고 폭·높이로 늘린다.
                    const Matrix4x4 glyph = MakeTransformMatrix3D(
                        Vector3{item.left + item.width * 0.5f, item.top - item.height * 0.5f, 0.0f}, Quaternion{},
                        Vector3{item.width, item.height, 1.0f});
                    WorldTextSubmit& submit = batch[count];
                    ++count;
                    submit.world = MultiplyMatrix4x4(object, glyph);
                    submit.texture = item.texture;
                    submit.tint[0] = item.tint.R;
                    submit.tint[1] = item.tint.G;
                    submit.tint[2] = item.tint.B;
                    submit.tint[3] = item.tint.A;
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        submit.uvRect[channel] = item.uvRect[channel];
                        submit.outlineColor[channel] = item.outlineColor[channel];
                    }
                    submit.filter = item.linearFilter ? SpriteFilter::Linear : SpriteFilter::Nearest;
                    submit.sdf = item.sdf;
                    submit.outlineEdge = item.outlineEdge;
                }
                if (count == 0)
                {
                    break;
                }
                if (false == renderer.SubmitWorldTexts({batch, count}))
                {
                    accepted = false;
                    break;
                }
            }
            return accepted;
        }
    }

    namespace
    {
        // 디버그 선을 월드 텍스트 사각형으로 낸다(D-243). 그 경로는 메시 뒤에 깊이를 보되 쓰지 않고 그리므로 선이 메시에 가려지고,
        // 텍스처가 비면 흰색이라 틴트가 선의 색이다. 사각형의 x 축은 선(길이만큼), y 축은 선과 시선에 모두 수직인 쪽(그 거리에서
        // 픽셀 두께만큼)이다 - 그래서 어느 쪽에서 봐도 선이 납작해지지 않는다.
        void PushDebugLines3D(const System::DebugDrawSystem& debugDraw, Renderer& renderer, const RenderCamera3D& camera,
            float viewportHeight)
        {
            const bool perspective = camera.projection == Component::CameraProjection3D::Perspective;
            const float halfFieldTangent = std::tan(camera.verticalFieldOfView * (3.14159265f / 180.0f) * 0.5f);
            const Vector3 forward = Rotate(camera.rotation, Vector3{0.0f, 0.0f, -1.0f});
            constexpr std::uint32_t BatchSize = 64;
            WorldTextSubmit batch[BatchSize];
            std::uint32_t count = 0;
            const std::uint32_t lineCount = debugDraw.GetLineCount();
            for (std::uint32_t index = 0; index < lineCount; ++index)
            {
                const DebugLine& line = debugDraw.GetLine(index);
                const Vector3 from{line.from[0], line.from[1], line.from[2]};
                const Vector3 to{line.to[0], line.to[1], line.to[2]};
                const Vector3 along = Subtract(to, from);
                const float length = Length(along);
                if (false == (length > 0.0f))
                {
                    continue;
                }
                const Vector3 center = Scale(Add(from, to), 0.5f);
                const Vector3 toCenter = Subtract(center, camera.position);
                float worldPerPixel = 2.0f * camera.orthographicSize / viewportHeight;
                if (perspective)
                {
                    const float depth = Dot(toCenter, forward);
                    // 카메라 뒤나 가까운 면 안쪽의 선은 그리지 않는다 - 두께가 정해지지 않는다.
                    if (depth <= camera.nearPlane)
                    {
                        continue;
                    }
                    worldPerPixel = 2.0f * depth * halfFieldTangent / viewportHeight;
                }
                const Vector3 sight = perspective ? toCenter : forward;
                Vector3 side = Cross(along, sight);
                if (Length(side) <= length * 1e-4f)
                {
                    // 선이 시선과 나란하다. 아무 수직이나 쓴다 - 점으로 보인다.
                    side = Cross(along, std::fabs(along.y) < 0.9f * length ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f});
                }
                side = Scale(Normalize(side), line.thickness * worldPerPixel);
                const Vector3 normal = Normalize(Cross(along, side));
                WorldTextSubmit& quad = batch[count];
                quad = WorldTextSubmit{};
                float* matrix = quad.world.values;
                matrix[0] = along.x;
                matrix[4] = along.y;
                matrix[8] = along.z;
                matrix[1] = side.x;
                matrix[5] = side.y;
                matrix[9] = side.z;
                matrix[2] = normal.x;
                matrix[6] = normal.y;
                matrix[10] = normal.z;
                matrix[3] = center.x;
                matrix[7] = center.y;
                matrix[11] = center.z;
                matrix[12] = 0.0f;
                matrix[13] = 0.0f;
                matrix[14] = 0.0f;
                matrix[15] = 1.0f;
                for (int channel = 0; channel < 4; ++channel)
                {
                    quad.tint[channel] = static_cast<float>(line.color[channel]) / 255.0f;
                }
                ++count;
                if (count == BatchSize)
                {
                    renderer.SubmitWorldTexts({batch, count});
                    count = 0;
                }
            }
            if (count > 0)
            {
                renderer.SubmitWorldTexts({batch, count});
            }
        }
    }

    namespace
    {
        // **레이어마다 뷰 하나다**(D-280). 포토샵의 레이어처럼 레이어는 따로 그린 한 장이고 차례대로 쌓인다 - 렌더러가 뷰마다 깊이를
        // 지우므로 뒤 레이어가 앞 레이어의 물체보다 멀어도 위에 보인다. 표준·불투명도 1 인 레이어는 타깃에 바로 그리고(텍스처 없음),
        // 블렌드나 불투명도가 걸린 레이어만 그 뷰를 제 텍스처에 그려 얹는다(`CameraParams::composite`). 대상을 지우는 것은 첫 뷰다.
        // 그릴 것이 없으면 뷰 하나로 지우기만 한다. 디버그 선은 맨 위 레이어의 뷰에 얹는다 - 그 뷰가 얹는 뷰면 따로 하나 더 연다.
        bool SubmitLayerViews(const RenderWorld3D& world, Renderer& renderer, const CameraParams& camera, bool editorView,
            const Vector3& cameraPosition, const Quaternion& cameraRotation, const System::DebugDrawSystem* debugDraw,
            const RenderCamera3D& lineCamera, float viewportHeight)
        {
            Array<std::uint16_t>& orders = world.GetLayerOrderScratch();
            orders.Clear();
            const auto collect = [&](GameObject* owner, std::uint16_t order) {
                if (editorView && owner != nullptr && owner->IsEditorHidden())
                {
                    return;
                }
                for (std::size_t at = 0; at < orders.Size(); ++at)
                {
                    if (orders[at] == order)
                    {
                        return;
                    }
                }
                if (orders.Size() < orders.Capacity())
                {
                    orders.Add(order);
                }
            };
            for (std::size_t index = 0; index < world.GetMeshCount(); ++index)
            {
                collect(world.GetMesh(index).owner, world.GetMesh(index).layerOrder);
            }
            for (std::size_t index = 0; index < world.GetTextCount(); ++index)
            {
                collect(world.GetText(index).owner, world.GetText(index).layerOrder);
            }
            std::sort(orders.Data(), orders.Data() + orders.Size());
            SortWorldTexts(world, editorView, cameraPosition);

            const auto findBlend = [&](std::uint16_t order, LayerBlend& blend, float& opacity) {
                for (std::size_t index = 0; index < world.GetMeshCount(); ++index)
                {
                    if (world.GetMesh(index).layerOrder == order)
                    {
                        blend = world.GetMesh(index).layerBlend;
                        opacity = world.GetMesh(index).layerOpacity;
                        return;
                    }
                }
                for (std::size_t index = 0; index < world.GetTextCount(); ++index)
                {
                    if (world.GetText(index).layerOrder == order)
                    {
                        blend = world.GetText(index).layerBlend;
                        opacity = world.GetText(index).layerOpacity;
                        return;
                    }
                }
            };

            bool accepted = true;
            bool linesDrawn = debugDraw == nullptr;
            for (std::size_t at = 0; at < orders.Size(); ++at)
            {
                LayerBlend blend = LayerBlend::Normal;
                float opacity = 1.0f;
                findBlend(orders[at], blend, opacity);
                CameraParams layerCamera = camera;
                layerCamera.composite = ToCompositeBlend3D(blend);
                layerCamera.compositeOpacity = opacity;
                if (false == renderer.BeginView(layerCamera))
                {
                    return false;
                }
                accepted = PushMeshes(world, renderer, editorView, orders[at]) && accepted;
                accepted = PushWorldTexts(world, renderer, cameraRotation, orders[at]) && accepted;
                const bool plain = blend == LayerBlend::Normal && opacity >= 1.0f;
                if (at + 1 == orders.Size() && plain && false == linesDrawn)
                {
                    PushDebugLines3D(*debugDraw, renderer, lineCamera, viewportHeight);
                    linesDrawn = true;
                }
                if (false == renderer.EndView())
                {
                    return false;
                }
            }
            if (orders.IsEmpty() || false == linesDrawn)
            {
                if (false == renderer.BeginView(camera))
                {
                    return false;
                }
                if (false == linesDrawn)
                {
                    PushDebugLines3D(*debugDraw, renderer, lineCamera, viewportHeight);
                }
                if (false == renderer.EndView())
                {
                    return false;
                }
            }
            return accepted;
        }
    }

    RenderResult SubmitEditorView3D(
        const RenderWorld3D& world, Renderer& renderer, const EditorViewDesc& view, const System::DebugDrawSystem* debugDraw)
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
        editor.rotation = FromEuler(Vector3{
            view.pitchDegrees * Degrees, view.yawDegrees * Degrees, 0.0f});
        // 카메라가 보는 쪽은 -z 다(오른손 좌표계의 뷰 규약). 그 반대로 물러난다.
        const Vector3 forward = Rotate(editor.rotation, Vector3{0.0f, 0.0f, -1.0f});
        const Vector3 target{view.centerX, view.centerY, view.centerZ};
        editor.position = Vector3{
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
        const bool accepted = SubmitLayerViews(world, renderer, parameters, true, editor.position, editor.rotation,
            view.debugDraw ? debugDraw : nullptr, editor, static_cast<float>(view.extent.height));
        return accepted ? RenderResult::Submitted : RenderResult::Failed;
    }

    RenderResult SubmitRenderWorld3D(const RenderWorld3D& world, Renderer& renderer, const System::DebugDrawSystem* debugDraw)
    {
        const RenderCamera3D* camera = world.GetCamera();
        if (camera == nullptr)
        {
            return RenderResult::NothingToSubmit;
        }
        CameraParams parameters;
        if (false == BuildCamera3D(*camera, renderer.GetFrameExtent(), parameters))
        {
            return RenderResult::Failed;
        }
        const bool showLines = debugDraw != nullptr && debugDraw->IsGameViewVisible();
        const bool accepted = SubmitLayerViews(world, renderer, parameters, false, camera->position, camera->rotation,
            showLines ? debugDraw : nullptr, *camera, static_cast<float>(renderer.GetFrameExtent().height));
        return accepted ? RenderResult::Submitted : RenderResult::Failed;
    }
}
