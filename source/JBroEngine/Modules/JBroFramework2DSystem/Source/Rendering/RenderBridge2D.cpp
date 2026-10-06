#include "RenderBridge2D.h"

#include <JBro/Framework2DSystem/Rendering/CameraView2D.h>
#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
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

        // `offsetX`·`offsetY` 는 월드에서 옮길 양이다(패럴랙스, D-286). 오브젝트 변환 뒤에 더한다 - 레이어 전체가 같이 움직인다.
        SpriteSubmit BuildSprite(const SpriteRenderItem& source, Float offsetX = 0.0f, Float offsetY = 0.0f)
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
            result.world.translation[0] = world.m31 + offsetX;
            result.world.translation[1] = world.m32 + offsetY;
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
        // 어느 아이템을 그 뷰에 넣는가. 월드 뷰는 월드 레이어만, 화면 뷰는 그 맞춤 방식의 화면 레이어만이다(D-237).
        struct SpriteFilterRule
        {
            Bool screenSpace = false;
            ScreenScaleMode scaleMode = ScreenScaleMode::FixedHeight;
            Bool anyScaleMode = true;
            // 있으면 이 오브젝트와 그 자손의 아이템만 넣는다(D-252). 캔버스 뷰가 흰 막 위에 들어간 오브젝트를 다시 그릴 때다.
            InstanceId focus = InvalidInstanceId;
            // 있으면 이 오브젝트들의 아이템만 넣는다(D-276). 선택 외곽선의 마스크를 그릴 때다. 자손은 따로 고른 것만이다 -
            // 캔버스 뷰의 고르기가 자손까지 담는다(D-253·D-254).
            const InstanceId* selection = nullptr;
            UInt32 selectionCount = 0;
            // 레이어의 블렌드와 불투명도를 렌더러의 묶음으로 낸다(D-279). 선택 외곽선의 마스크는 그린 그대로의 모양이어야 하므로 끈다.
            Bool composite = true;
            // 있으면 월드 레이어의 패럴랙스를 이 뷰(월드 → 뷰)로 건다(D-286). 게임 화면만 준다 - 캔버스 뷰는 패럴랙스 없이 배치하는 자리다.
            const Matrix3x2* parallaxView = nullptr;
            // 빛을 받는 레이어의 아이템을 렌더러의 빛을 받는 구간으로 낸다(D-291). 라이트를 낸 월드 뷰만 켠다 - 선택 외곽선의 마스크·레이어 썸네일·
            // 화면 레이어는 빛을 보지 않는다.
            Bool lighting = false;
            // 0 이상이면 이 레이어 차례의 아이템만 넣는다(썸네일, D-288).
            Int32 layerOrder = -1;
        };

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

        Bool IsSelected(const GameObject* owner, const InstanceId* selection, UInt32 count)
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

        Bool IsInFocus(const GameObject* owner, InstanceId focus)
        {
            for (const GameObject* at = owner; at != nullptr; at = at->GetParent())
            {
                if (at->GetInstanceId() == focus)
                {
                    return true;
                }
            }
            return false;
        }

        Bool PushSprites(const RenderWorld2D& world, Renderer& renderer, Bool editorView, const SpriteFilterRule& rule,
            std::size_t first = 0, std::size_t last = static_cast<std::size_t>(-1))
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
                const Bool needsComposite = rule.composite
                    && (item.layerBlend != LayerBlend::Normal || item.layerOpacity < 1.0f);
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
                        && renderer.BeginLayer(ToCompositeBlend(item.layerBlend), item.layerOpacity);
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
                batch[count] = BuildSprite(item, offsetX, offsetY);
                ++count;
                if (count == BatchSize)
                {
                    flush();
                }
            }
            flush();
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
        if (false == view.screenSpace)
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

    RenderResult SubmitRenderWorld2D(const RenderWorld2D& world, Renderer& renderer, const System::DebugDrawSystem* debugDraw)
    {
        const RenderCamera2D* camera = world.GetCamera();
        // **이번 프레임이 그려지는 크기로 사각형을 다시 잰다**(D-239). 뷰포트가 대상 밖으로 나가면 렌더러가 프레임을 거절하므로 대상은 렌더러의
        // 것을 쓴다. 레터박스는 앵커를 잰 것과 같은 함수로 이번에 뽑힌 카메라에서 다시 건다 - 첫 프레임에는 앵커 쪽이 카메라를 아직 몰랐다.
        ScreenSpaceFrame frame = world.GetScreenSpace();
        const Extent2D target = renderer.GetFrameExtent();
        frame.targetWidth = static_cast<JBro::Float>(target.width);
        frame.targetHeight = static_cast<JBro::Float>(target.height);
        ApplyCameraArea(camera, frame);
        Bool worldSubmitted = false;
        // 카메라가 없는 것은 오류가 아니다. 월드를 그리지 않을 뿐이다 - 화면 레이어(메뉴만 있는 캔버스)는 아래에서 그린다.
        if (camera != nullptr)
        {
            CameraParams parameters;
            // **창이 아니라 이번 프레임이 그려지는 크기다.** 에디터에서 게임은
            // 창과 다른 크기의 텍스처로 간다 - 창으로 잡으면 게임이 보는 화면이
            // 에디터 창 모양을 따라가고, 뷰포트가 타깃 밖으로 나간다.
            if (false == BuildCamera(*camera, frame, parameters)
                || false == renderer.BeginView(parameters))
            {
                return RenderResult::Failed;
            }
            // 패럴랙스는 그리는 카메라와 같은 뷰로 건다(D-286). 그 뷰를 못 재면(그릴 수 없는 카메라) 위에서 이미 실패했다.
            CameraView2D cameraView;
            SpriteFilterRule rule;
            if (ComputeCameraView2D(*camera, frame, cameraView))
            {
                rule.parallaxView = &cameraView.view;
            }
            PushLights(world, renderer, rule.parallaxView);
            PushShadowEdges(world, renderer, rule.parallaxView);
            rule.lighting = true;
            const Bool accepted = PushSprites(world, renderer, false, rule);
            // 디버그 선은 월드 뷰 안에서 스프라이트 뒤에 그린다(D-243). 화면 레이어가 그 위에 온다.
            if (debugDraw != nullptr && debugDraw->IsGameViewVisible())
            {
                PushDebugLines2D(*debugDraw, renderer,
                    2.0f * camera->orthographicSize / static_cast<JBro::Float>(renderer.GetFrameExtent().height));
            }
            const Bool closed = renderer.EndView();
            if (false == accepted || false == closed)
            {
                return RenderResult::Failed;
            }
            worldSubmitted = true;
        }
        // 화면 레이어는 월드 위에 그린다(D-237). 렌더러는 대상을 첫 뷰에서만 지운다.
        Bool screenSubmitted = false;
        if (false == SubmitScreenViews(world, renderer, frame, screenSubmitted))
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
