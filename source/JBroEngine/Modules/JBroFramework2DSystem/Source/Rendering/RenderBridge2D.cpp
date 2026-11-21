#include "RenderBridge2D.h"
#include <JBro/ScreenUi/Rendering/SpriteSubmit2D.h>

#include <JBro/Framework2DSystem/Rendering/CameraView2D.h>
#include <JBro/ScreenUi/Rendering/RenderWorld2D.h>
#include <JBro/ScreenUi/Rendering/SpritePieces.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Host/DebugDrawSystem.h>
#include <JBro/Runtime/GameObject.h>

#include <algorithm>
#include <cmath>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

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

        // `frame` 의 그려지는 사각형이 뷰포트다(D-239). 보이는 범위와 뷰는 `ComputeCameraView2D` 가 잰다 - 버튼의 역투영과 같은 함수다.
        Bool BuildCamera(const RenderCamera2D& source, const ScreenSpaceFrame& frame, CameraParams& result)
        {
            CameraView2D view;
            ScreenArea area;
            if (false == ComputeCameraView2D(source, frame, view) || false == GetScreenArea(frame, area))
            {
                return false;
            }
            const double halfHeight = view.halfHeight;
            const double halfWidth = view.halfWidth;
            const double depth = static_cast<double>(source.farPlane) - source.nearPlane;
            result.view = ToColumnMatrix(view.view);
            result.projection = {{static_cast<JBro::Float>(1.0 / halfWidth), 0.0f, 0.0f, 0.0f,
                0.0f, static_cast<JBro::Float>(1.0 / halfHeight), 0.0f, 0.0f,
                0.0f, 0.0f, static_cast<JBro::Float>(1.0 / depth), static_cast<JBro::Float>(-source.nearPlane / depth),
                0.0f, 0.0f, 0.0f, 1.0f}};
            for (Float value : result.projection.values)
            {
                if (false == std::isfinite(value))
                {
                    return false;
                }
            }
            result.viewport.x = area.x;
            result.viewport.y = area.y;
            result.viewport.width = area.width;
            result.viewport.height = area.height;
            result.clearColor[0] = source.clearColor.R;
            result.clearColor[1] = source.clearColor.G;
            result.clearColor[2] = source.clearColor.B;
            result.clearColor[3] = source.clearColor.A;
            return true;
        }


    }

    namespace
    {
        // **렌더 월드의 라이트를 열린 뷰에 낸다**(D-291). 색에 세기를 곱해 넘긴다(알파는 보지 않는다). `parallaxView` 를 주면 라이트를 그 레이어의
        // 패럴랙스만큼 옮긴다 - 같은 레이어의 스프라이트와 함께 움직여야 제 자리를 비춘다. 렌더러가 넘치는 라이트를 버리고 센다.
        void PushLights(const RenderWorld2D& world, Renderer& renderer, const Matrix3x2* parallaxView)
        {
            constexpr std::size_t BatchSize = 32;
            Light2DSubmit batch[BatchSize];
            std::size_t count = 0;
            const auto flush = [&]() {
                if (count != 0)
                {
                    renderer.SubmitLights2D({batch, static_cast<JBro::UInt32>(count)});
                }
                count = 0;
            };
            for (std::size_t index = 0; index < world.GetLightCount(); ++index)
            {
                const Light2DRenderItem& item = world.GetLight(index);
                Light2DSubmit& light = batch[count];
                light = Light2DSubmit{};
                light.kind = item.type == Component::Light2DType::Global ? Light2DKind::Global
                    : item.type == Component::Light2DType::Spot          ? Light2DKind::Spot
                                                                         : Light2DKind::Point;
                Float offsetX = 0.0f;
                Float offsetY = 0.0f;
                if (parallaxView != nullptr && item.layerParallax != 1.0f
                    && false == ComputeParallaxOffset2D(*parallaxView, item.layerParallax, offsetX, offsetY))
                {
                    offsetX = 0.0f;
                    offsetY = 0.0f;
                }
                light.position[0] = item.position.x + offsetX;
                light.position[1] = item.position.y + offsetY;
                light.direction[0] = item.direction.x;
                light.direction[1] = item.direction.y;
                light.color[0] = item.color.R * item.intensity;
                light.color[1] = item.color.G * item.intensity;
                light.color[2] = item.color.B * item.intensity;
                light.innerRadius = item.innerRadius;
                light.outerRadius = item.outerRadius;
                light.innerAngle = Radian(item.innerAngle);
                light.outerAngle = Radian(item.outerAngle);
                light.castShadows = item.castShadows;
                light.shadowSoftness = item.shadowSoftness;
                ++count;
                if (count == BatchSize)
                {
                    flush();
                }
            }
            flush();
        }

        // **렌더 월드의 그림자 변을 열린 뷰에 낸다**(D-291 3 단계). 라이트처럼 가림막이 놓인 레이어의 패럴랙스만큼 옮긴다(게임 화면만).
        void PushShadowEdges(const RenderWorld2D& world, Renderer& renderer, const Matrix3x2* parallaxView)
        {
            constexpr std::size_t BatchSize = 64;
            ShadowEdge2D batch[BatchSize];
            std::size_t count = 0;
            const auto flush = [&]() {
                if (count != 0)
                {
                    renderer.SubmitShadowEdges2D({batch, static_cast<JBro::UInt32>(count)});
                }
                count = 0;
            };
            for (std::size_t index = 0; index < world.GetShadowEdgeCount(); ++index)
            {
                const ShadowEdge2DItem& item = world.GetShadowEdge(index);
                Float offsetX = 0.0f;
                Float offsetY = 0.0f;
                if (parallaxView != nullptr && item.layerParallax != 1.0f
                    && false == ComputeParallaxOffset2D(*parallaxView, item.layerParallax, offsetX, offsetY))
                {
                    offsetX = 0.0f;
                    offsetY = 0.0f;
                }
                ShadowEdge2D& edge = batch[count];
                edge.from[0] = item.from.x + offsetX;
                edge.from[1] = item.from.y + offsetY;
                edge.to[0] = item.to.x + offsetX;
                edge.to[1] = item.to.y + offsetY;
                edge.selfShadow = item.selfShadow;
                ++count;
                if (count == BatchSize)
                {
                    flush();
                }
            }
            flush();
        }
    }

    namespace
    {
        // 디버그 선을 흰 스프라이트 사각형으로 낸다(D-243). 새 파이프라인이 없다 - 텍스처가 빈 스프라이트는 흰색이라 틴트가 선의 색이다.
        // 사각형의 x 축은 선 방향(길이만큼), y 축은 그 수직(픽셀 두께를 이 뷰의 월드 길이로 바꾼 만큼)이다.
        //
        // **프레임의 성패에 들지 않는다.** 렌더러의 제출 상한에 걸려 선이 못 들어가도 게임 화면은 그대로 나간다 - 버린 것은 저장소가 센다.
        void PushDebugLines2D(const System::DebugDrawSystem& debugDraw, Renderer& renderer, Float worldPerPixel)
        {
            constexpr UInt32 BatchSize = 64;
            SpriteSubmit batch[BatchSize];
            UInt32 count = 0;
            const UInt32 lineCount = debugDraw.GetLineCount();
            for (UInt32 index = 0; index < lineCount; ++index)
            {
                const DebugLine& line = debugDraw.GetLine(index);
                const Float dx = line.to[0] - line.from[0];
                const Float dy = line.to[1] - line.from[1];
                const Float length = std::sqrt(dx * dx + dy * dy);
                if (false == (length > 0.0f))
                {
                    continue;
                }
                const Float width = line.thickness * worldPerPixel;
                SpriteSubmit& sprite = batch[count];
                sprite = SpriteSubmit{};
                sprite.world.linear[0] = dx;
                sprite.world.linear[2] = dy;
                sprite.world.linear[1] = -dy / length * width;
                sprite.world.linear[3] = dx / length * width;
                sprite.world.translation[0] = (line.from[0] + line.to[0]) * 0.5f;
                sprite.world.translation[1] = (line.from[1] + line.to[1]) * 0.5f;
                for (Int32 channel = 0; channel < 4; ++channel)
                {
                    sprite.tint[channel] = static_cast<JBro::Float>(line.color[channel]) / 255.0f;
                }
                ++count;
                if (count == BatchSize)
                {
                    renderer.SubmitSprites({batch, count});
                    count = 0;
                }
            }
            if (count > 0)
            {
                renderer.SubmitSprites({batch, count});
            }
        }
    }

    RenderResult SubmitEditorView2D(
        const RenderWorld2D& world, Renderer& renderer, const EditorViewDesc& view, const System::DebugDrawSystem* debugDraw)
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

        // 편집 카메라는 대상 전체에 그린다. 기준 해상도는 `Orthographic` 에 쓰이지 않는다.
        ScreenSpaceFrame frame;
        frame.targetWidth = static_cast<JBro::Float>(view.extent.width);
        frame.targetHeight = static_cast<JBro::Float>(view.extent.height);
        CameraParams parameters;
        if (false == BuildCamera(editor, frame, parameters))
        {
            return RenderResult::Failed;
        }
        // 캔버스 뷰는 월드 보기면 월드 레이어만, UI 보기면 화면 레이어만 보인다(D-237) - 화면 좌표는 기준 픽셀이라 섞으면 안 된다.
        SpriteFilterRule rule;
        rule.screenSpace = view.screenSpace;
        rule.layerPostProcess = view.postProcess;
        Bool accepted = true;

        // **선택 외곽선의 마스크를 먼저 그린다**(D-276, 기존 `COutlineRenderer2D::RenderMask`). 같은 카메라로 고른 것의 스프라이트만
        // 투명하게 지운 마스크에 그린다 - 그려진 그대로라 회전·텍스처 알파·틴트가 다 들어간다. 본 뷰가 다 그린 뒤 렌더러가
        // 이 마스크를 키워 둘레만 칠한다. 마스크가 본 뷰보다 먼저 나가야 그 둘레를 칠할 때 마스크가 차 있다.
        const Bool outline = view.selection != nullptr && view.selectionCount != 0
            && view.outlineMask.IsValid() && view.outlineScratch.IsValid();
        if (outline)
        {
            CameraParams mask = parameters;
            mask.target = view.outlineMask;
            mask.targetExtent = view.extent;
            for (Float& channel : mask.clearColor)
            {
                channel = 0.0f;
            }
            if (false == renderer.BeginView(mask))
            {
                return RenderResult::Failed;
            }
            SpriteFilterRule selected = rule;
            selected.selection = view.selection;
            selected.selectionCount = view.selectionCount;
            selected.composite = false;
            selected.lighting = false;
            accepted = PushSprites(world, renderer, true, selected);
            if (false == renderer.EndView())
            {
                return RenderResult::Failed;
            }
        }

        parameters.target = view.target;
        parameters.targetExtent = view.extent;
        // 캔버스 뷰도 게임 카메라의 후처리로 보인다(D-344, 계획 §2.6). 단추로 끄고, UI 보기는 늘 없다. 선택 외곽선은 후처리 뒤다.
        if (view.postProcess && false == view.screenSpace)
        {
            parameters.postProcess = world.GetPostProcess();
        }
        if (outline)
        {
            constexpr UInt32 OutlinePixels = 2;
            parameters.outlineMask = view.outlineMask;
            parameters.outlineScratch = view.outlineScratch;
            parameters.outlineWidth = OutlinePixels;
        }
        if (false == renderer.BeginView(parameters))
        {
            return RenderResult::Failed;
        }
        // 캔버스 뷰도 게임과 같은 빛으로 보인다(D-291). 월드 보기만이다 - 화면 레이어는 빛을 받지 않는다. 패럴랙스는 걸지 않는다(배치하는 자리다).
        // 캔버스 뷰의 라이팅 단추를 끄면 라이트를 내지 않아 빛을 받는 레이어도 원래 색이다.
        if (false == view.screenSpace && view.lighting)
        {
            PushLights(world, renderer, nullptr);
            PushShadowEdges(world, renderer, nullptr);
            rule.lighting = true;
        }
        accepted = PushSprites(world, renderer, true, rule) && accepted;
        // **들어가 있으면 나머지를 흰 막으로 가린다**(D-252, 기존 캔버스 뷰의 포커스 오버레이). 장면을 다 그리고, 화면을 덮는
        // 반투명 흰 사각형을 얹고, 들어간 오브젝트와 그 자손만 그 위에 한 번 더 그린다 - 렌더러는 낸 순서대로 그린다.
        // 막은 텍스처 없는 스프라이트라 흰색이고 틴트의 알파가 짙기다. 뒤의 오브젝트에 가린 조각도 막 위로 올라와 다 보인다.
        if (view.focusObject != InvalidInstanceId)
        {
            constexpr Float VeilOpacity = 0.7f;
            const Float halfHeight = view.orthographicSize;
            const Float halfWidth = halfHeight * static_cast<JBro::Float>(view.extent.width) / static_cast<JBro::Float>(view.extent.height);
            SpriteSubmit veil;
            veil.world.linear[0] = halfWidth * 2.0f;
            veil.world.linear[3] = halfHeight * 2.0f;
            veil.world.translation[0] = view.centerX;
            veil.world.translation[1] = view.centerY;
            veil.tint[3] = VeilOpacity;
            accepted = renderer.SubmitSprite(veil) && accepted;
            SpriteFilterRule focused = rule;
            focused.focus = view.focusObject;
            accepted = PushSprites(world, renderer, true, focused) && accepted;
        }
        // 디버그 선은 월드 좌표라 월드 보기에만 그린다(D-243).
        if (debugDraw != nullptr && view.debugDraw && false == view.screenSpace)
        {
            PushDebugLines2D(*debugDraw, renderer, 2.0f * view.orthographicSize / static_cast<JBro::Float>(view.extent.height));
        }
        const Bool closed = renderer.EndView();
        return (accepted && closed) ? RenderResult::Submitted : RenderResult::Failed;
    }

    namespace
    {
        // **뷰의 담는 영역을 덮는 빛 안 받는 사각형**(D-362, 둘째 뷰의 지우기). 렌더러는 대상을 첫 뷰에서만 지우므로, 앞 뷰가 그린 대상에 그리는 카메라는
        // 이것을 맨 먼저 내 제 뷰포트를 제 지우기 색으로 칠한다. 뷰(월드 → 뷰)를 뒤집어 뷰 공간 [-반폭, 반폭] x [-반높이, 반높이] 를 월드로 옮긴다.
        // 뒤집을 수 없으면 거짓이다. 알파는 1 이다(지우기는 덮어쓴다).
        Bool BuildViewFill(const CameraView2D& cameraView, const Color& color, SpriteSubmit& result)
        {
            const Matrix3x2& view = cameraView.view;
            const Float determinant = view.m11 * view.m22 - view.m12 * view.m21;
            if (false == std::isfinite(determinant.Get()) || std::fabs(determinant) < 1e-12f || cameraView.halfWidth <= 0.0f || cameraView.halfHeight <= 0.0f)
            {
                return false;
            }
            // 행 벡터의 역행렬이다.
            Matrix3x2 inverse;
            inverse.m11 = view.m22 / determinant;
            inverse.m12 = -view.m12 / determinant;
            inverse.m21 = -view.m21 / determinant;
            inverse.m22 = view.m11 / determinant;
            inverse.m31 = -(view.m31 * inverse.m11 + view.m32 * inverse.m21);
            inverse.m32 = -(view.m31 * inverse.m12 + view.m32 * inverse.m22);
            const Matrix3x2 geometry{cameraView.halfWidth * 2.0f, 0.0f, 0.0f, cameraView.halfHeight * 2.0f, 0.0f, 0.0f};
            const Matrix3x2 world = MultiplyMatrix3x2(geometry, inverse);
            result = SpriteSubmit{};
            result.world.linear[0] = world.m11;
            result.world.linear[1] = world.m21;
            result.world.linear[2] = world.m12;
            result.world.linear[3] = world.m22;
            result.world.translation[0] = world.m31;
            result.world.translation[1] = world.m32;
            result.tint[0] = color.R;
            result.tint[1] = color.G;
            result.tint[2] = color.B;
            result.tint[3] = 1.0f;
            return true;
        }
    }

    RenderResult SubmitRenderWorld2D(const RenderWorld2D& world, Renderer& renderer, const System::DebugDrawSystem* debugDraw)
    {
        const RenderCamera2D* primary = world.GetCamera();
        // **이번 프레임이 그려지는 크기로 사각형을 다시 잰다**(D-239). 뷰포트가 대상 밖으로 나가면 렌더러가 프레임을 거절하므로 대상은 렌더러의
        // 것을 쓴다. 레터박스는 앵커를 잰 것과 같은 함수로 이번에 뽑힌 카메라에서 다시 건다 - 첫 프레임에는 앵커 쪽이 카메라를 아직 몰랐다.
        ScreenSpaceFrame base = world.GetScreenSpace();
        const Extent2D target = renderer.GetFrameExtent();
        base.targetWidth = static_cast<JBro::Float>(target.width);
        base.targetHeight = static_cast<JBro::Float>(target.height);
        base.areaX = 0.0f;
        base.areaY = 0.0f;
        base.areaWidth = 0.0f;
        base.areaHeight = 0.0f;
        Bool worldSubmitted = false;
        // **뷰포트마다 뷰 하나다**(D-359, D-438). 목록 차례이고 각자 제 뷰포트(와 그 안의 레터박스)에 그린다. 조명·그림자·패럴랙스는 그 뷰의 카메라로 다시 건다.
        // 디버그 선은 첫 뷰에만 그린다. 카메라가 없는 것은 오류가 아니다 - 화면 레이어(메뉴만 있는 캔버스)는 아래에서 그린다.
        const auto drawCamera = [&](const RenderCamera2D& camera, Bool first, std::size_t index) -> RenderResult
        {
            ScreenSpaceFrame frame = base;
            // 렌더 텍스처에 그리는 카메라(D-362)는 그 텍스처가 대상이다 - 크기·뷰포트를 그 텍스처로 잰다.
            const Bool toTexture = camera.target.IsValid() && camera.targetExtent.width != 0 && camera.targetExtent.height != 0;
            if (toTexture)
            {
                frame.targetWidth = static_cast<JBro::Float>(camera.targetExtent.width);
                frame.targetHeight = static_cast<JBro::Float>(camera.targetExtent.height);
            }
            // 이 대상에서 사각형이 비면(픽셀 칸으로 대상 밖에 놓였다, D-439) 그 뷰는 그리지 않는다 - 오류가 아니다.
            if (false == ApplyCameraViewport(camera, frame))
            {
                return RenderResult::NothingToSubmit;
            }
            CameraParams parameters;
            if (toTexture)
            {
                parameters.target = camera.target;
                parameters.targetExtent = camera.targetExtent;
            }
            // 카메라의 후처리다(D-344). 화면 레이어는 뒤의 다른 뷰라 후처리를 받지 않는다. 분할 화면은 카메라마다 제 프로필이다(D-361) - 자리가 없으면 주 카메라의 것.
            parameters.postProcess = world.GetCameraPostProcess(index);
            // **창이 아니라 이번 프레임이 그려지는 크기다.** 에디터에서 게임은 창과 다른 크기의 텍스처로 간다 - 창으로 잡으면 게임이 보는 화면이
            // 에디터 창 모양을 따라가고, 뷰포트가 타깃 밖으로 나간다.
            if (false == BuildCamera(camera, frame, parameters) || false == renderer.BeginView(parameters))
            {
                return RenderResult::Failed;
            }
            // 패럴랙스는 그리는 카메라와 같은 뷰로 건다(D-286). 그 뷰를 못 재면(그릴 수 없는 카메라) 위에서 이미 실패했다.
            CameraView2D cameraView;
            SpriteFilterRule rule;
            if (ComputeCameraView2D(camera, frame, cameraView))
            {
                rule.parallaxView = &cameraView.view;
                // 앞 뷰가 이미 지운 대상이면 제 뷰포트를 제 지우기 색으로 먼저 칠한다(D-362). 빛을 받지 않는다 - 뷰는 빛을 끈 채 시작한다.
                SpriteSubmit fill;
                if (false == first && camera.clearViewport && BuildViewFill(cameraView, camera.clearColor, fill) && false == renderer.SubmitSprite(fill))
                {
                    renderer.EndView();
                    return RenderResult::Failed;
                }
            }
            PushLights(world, renderer, rule.parallaxView);
            PushShadowEdges(world, renderer, rule.parallaxView);
            rule.lighting = true;
            if (toTexture)
            {
                rule.excludeTexture = camera.targetSprite;
            }
            // 뷰포트의 레이어 필터(D-438). 거르지 않는 뷰는 마스크를 주지 않는다.
            if (camera.layers.filtered)
            {
                rule.layers = &camera.layers;
            }
            const Bool accepted = PushSprites(world, renderer, false, rule);
            // 디버그 선은 월드 뷰 안에서 스프라이트 뒤에 그린다(D-243). 화면 레이어가 그 위에 온다. 렌더 텍스처에는 그리지 않는다.
            if (first && false == toTexture && debugDraw != nullptr && debugDraw->IsSimulationViewVisible())
            {
                PushDebugLines2D(*debugDraw, renderer,
                    2.0f * camera.orthographicSize / static_cast<JBro::Float>(renderer.GetFrameExtent().height));
            }
            const Bool closed = renderer.EndView();
            return accepted && closed ? RenderResult::Submitted : RenderResult::Failed;
        };
        const ArrayView<const RenderCamera2D> cameras = world.GetCameras();
        if (cameras.Size() > 0)
        {
            // 렌더 텍스처에 그리는 카메라가 먼저다(D-362) - 같은 프레임에 화면의 스프라이트가 그 그림을 본다. 대상이 풀리지 않은 것은 그리지 않는다.
            for (UInt32 index = 0; index < cameras.Size(); ++index)
            {
                const RenderCamera2D& camera = cameras[index];
                if (camera.renderTexture.generation != 0 && camera.target.IsValid()
                    && drawCamera(camera, true, static_cast<std::size_t>(index.Get())) == RenderResult::Failed)
                {
                    return RenderResult::Failed;
                }
            }
            // 화면의 카메라다. 첫 화면 카메라가 대상을 지운다(`first`).
            Bool firstScreen = true;
            for (UInt32 index = 0; index < cameras.Size(); ++index)
            {
                const RenderCamera2D& camera = cameras[index];
                if (camera.renderTexture.generation != 0)
                {
                    continue;
                }
                const RenderResult drawn = drawCamera(camera, firstScreen, static_cast<std::size_t>(index.Get()));
                if (drawn == RenderResult::Failed)
                {
                    return RenderResult::Failed;
                }
                if (drawn == RenderResult::Submitted)
                {
                    firstScreen = false;
                    worldSubmitted = true;
                }
            }
        }
        else if (primary != nullptr)
        {
            // 카메라를 직접 건 쪽(시험)이다 - 주 카메라 하나.
            if (drawCamera(*primary, true, static_cast<std::size_t>(-1)) == RenderResult::Failed)
            {
                return RenderResult::Failed;
            }
            worldSubmitted = true;
        }
        // 화면 레이어는 월드 위에 그린다(D-237). 렌더러는 대상을 첫 뷰에서만 지운다. 사각형은 주 카메라의 뷰포트가 전체일 때만 그 레터박스다 - 분할 화면의 HUD 는 화면 전체다.
        ScreenSpaceFrame screenFrame = base;
        ApplyCameraArea(primary, screenFrame);
        Bool screenSubmitted = false;
        if (false == SubmitScreenViews(world, renderer, screenFrame, screenSubmitted))
        {
            return RenderResult::Failed;
        }
        return worldSubmitted || screenSubmitted ? RenderResult::Submitted : RenderResult::NothingToSubmit;
    }

    RenderResult SubmitLayerThumbnail2D(const RenderWorld2D& world, Renderer& renderer, const LayerThumbnailDesc& thumbnail, const Layer& layer)
    {
        if (false == thumbnail.target.IsValid() || thumbnail.extent.width == 0 || thumbnail.extent.height == 0)
        {
            return RenderResult::NothingToSubmit;
        }
        // **게임 화면과 같은 셈이다** - 화면 기준·레터박스·패럴랙스. 크기만 썸네일의 것이다.
        const RenderCamera2D* camera = world.GetCamera();
        ScreenSpaceFrame frame = world.GetScreenSpace();
        frame.targetWidth = static_cast<JBro::Float>(thumbnail.extent.width);
        frame.targetHeight = static_cast<JBro::Float>(thumbnail.extent.height);
        ApplyCameraArea(camera, frame);
        CameraParams parameters;
        SpriteFilterRule rule;
        rule.composite = false;
        rule.layerOrder = static_cast<JBro::Int32>(layer.GetOrder());
        CameraView2D cameraView;
        Bool drawable = false;
        if (layer.GetSpace() == LayerSpace::Screen)
        {
            ScreenArea area;
            ScreenExtent extent;
            drawable = GetScreenArea(frame, area) && ComputeScreenExtent(layer.GetScaleMode(), frame, extent)
                && BuildScreenCamera(extent, area, parameters);
            rule.screenSpace = true;
            rule.scaleMode = layer.GetScaleMode();
            rule.anyScaleMode = false;
        }
        else if (camera != nullptr && BuildCamera(*camera, frame, parameters))
        {
            drawable = true;
            if (ComputeCameraView2D(*camera, frame, cameraView))
            {
                rule.parallaxView = &cameraView.view;
            }
        }
        if (false == drawable)
        {
            // 그릴 카메라가 없다. 바탕만 지운다 - 빈 칸과 지난 그림이 남은 칸은 다르다.
            parameters = CameraParams{};
        }
        parameters.target = thumbnail.target;
        parameters.targetExtent = thumbnail.extent;
        for (Int32 channel = 0; channel < 4; ++channel)
        {
            parameters.clearColor[channel] = thumbnail.clearColor[channel];
        }
        if (false == renderer.BeginView(parameters))
        {
            return RenderResult::Failed;
        }
        const Bool accepted = false == drawable || PushSprites(world, renderer, false, rule);
        const Bool closed = renderer.EndView();
        return accepted && closed ? RenderResult::Submitted : RenderResult::Failed;
    }
}
