#include <JBro/Graphics/Renderer.h>

#include <JBro/Types/Math3D.h>

#include <algorithm>
#include <cmath>

#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **3D 그림자**(D-445, tasks/lighting3d-plan.md §5). 방향광 하나의 캐스케이드 넷과 스포트 넷까지, 점광 넷까지의 큐브 면(D-446)을 한 장의 깊이 아틀라스에 그리고,
// 표준 머티리얼이 3 x 3 PCF 로 읽는다. 캐스케이드는 카메라 앞 `shadowDistance` 를 넷으로 나눈 조각마다 그 조각을 덮는 공(구)에 맞춘 직교 투영이다 -
// 공은 카메라가 돌아도 크기가 같고, 중심을 칸 크기로 맞춰 옮기면 카메라가 움직여도 그림자 가장자리가 떨지 않는다.
namespace JBro
{
    namespace
    {
        Matrix4x4 MultiplyMatrices(const Matrix4x4& left, const Matrix4x4& right)
        {
            Matrix4x4 result;
            for (UInt32 row = 0; row < 4; ++row)
            {
                for (UInt32 column = 0; column < 4; ++column)
                {
                    Float value = 0.0f;
                    for (UInt32 at = 0; at < 4; ++at)
                    {
                        value += left.values[(row * 4 + at).Get()] * right.values[(at * 4 + column).Get()];
                    }
                    result.values[(row * 4 + column).Get()] = value;
                }
            }
            return result;
        }

        // 4 x 4 역행렬(여인수). 행렬식이 0 이면 거짓이다.
        Bool InvertMatrix(const Matrix4x4& matrix, Matrix4x4& result)
        {
            const Float* m = matrix.values;
            Float inverse[16];
            inverse[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
            inverse[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
            inverse[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
            inverse[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
            inverse[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
            inverse[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
            inverse[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
            inverse[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
            inverse[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
            inverse[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
            inverse[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
            inverse[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
            inverse[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
            inverse[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
            inverse[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
            inverse[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
            const Float determinant = m[0] * inverse[0] + m[1] * inverse[4] + m[2] * inverse[8] + m[3] * inverse[12];
            if (false == std::isfinite(determinant.Get()) || std::fabs(determinant.Get()) < 1.0e-20f)
            {
                return false;
            }
            const Float scale = 1.0f / determinant;
            for (Int32 at = 0; at < 16; ++at)
            {
                result.values[at.Get()] = inverse[at.Get()] * scale;
            }
            return true;
        }

        // 점을 옮기고 w 로 나눈다.
        Vector3 ProjectPoint(const Matrix4x4& matrix, Float x, Float y, Float z)
        {
            const Float* m = matrix.values;
            const Float w = m[12] * x + m[13] * y + m[14] * z + m[15];
            const Float scale = std::fabs(w.Get()) > 1.0e-12f ? Float(1.0f / w) : Float(1.0f);
            return Vector3{(m[0] * x + m[1] * y + m[2] * z + m[3]) * scale, (m[4] * x + m[5] * y + m[6] * z + m[7]) * scale,
                (m[8] * x + m[9] * y + m[10] * z + m[11]) * scale};
        }

        // `forward` 를 보는(-Z) 뷰 행렬이다. 위는 월드 +Y 이고, 곧장 위아래를 보면 +X 다.
        void LightBasis(const Vector3& forward, Vector3& right, Vector3& up)
        {
            const Vector3 helper = std::fabs(forward.y.Get()) < 0.99f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
            right = Normalize(Cross(forward, helper));
            up = Cross(right, forward);
        }

        Matrix4x4 LightView(const Vector3& eye, const Vector3& forward)
        {
            Vector3 right;
            Vector3 up;
            LightBasis(forward, right, up);
            return Matrix4x4{{
                right.x, right.y, right.z, -Dot(right, eye),
                up.x, up.y, up.z, -Dot(up, eye),
                -forward.x, -forward.y, -forward.z, Dot(forward, eye),
                0.0f, 0.0f, 0.0f, 1.0f}};
        }

        // 깊이 0..1, near 가 0 이다(Math3DMatrix 의 것과 같은 식, 가로세로 1).
        Matrix4x4 LightOrthographic(Float halfSize, Float nearPlane, Float farPlane)
        {
            const Float depth = farPlane - nearPlane;
            return Matrix4x4{{
                1.0f / halfSize, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f / halfSize, 0.0f, 0.0f,
                0.0f, 0.0f, -1.0f / depth, -nearPlane / depth,
                0.0f, 0.0f, 0.0f, 1.0f}};
        }

        Matrix4x4 LightPerspective(Float fieldOfView, Float nearPlane, Float farPlane)
        {
            const Float focal = 1.0f / std::tan(fieldOfView.Get() * 0.5f);
            const Float depth = nearPlane - farPlane;
            return Matrix4x4{{
                focal, 0.0f, 0.0f, 0.0f,
                0.0f, focal, 0.0f, 0.0f,
                0.0f, 0.0f, farPlane / depth, nearPlane * farPlane / depth,
                0.0f, 0.0f, -1.0f, 0.0f}};
        }

        // 방향광의 깊이 바이어스(직교, 깊이가 고르다)와 스포트의 것(원근, 멀수록 깊이 차가 작다). 여드름은 주로 법선 밀기가 막는다.
        constexpr Float DirectionalBias = 0.0005f;
        constexpr Float SpotBias = 0.00002f;
        // 그림자 맵 뒤로 더 물러서는 거리다 - 카메라에 보이지 않는 곳의 물체도 보이는 곳에 그림자를 드리운다.
        constexpr Float CasterReach = 50.0f;

        // 점광의 큐브 면(D-446). 차례는 +X·-X·+Y·-Y·+Z·-Z 이고 셰이더의 `PointFace` 가 같은 차례로 고른다.
        constexpr Float PointFaceDirections[6][3] = {
            {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}};
        // 면은 90 도보다 조금 넓게 그린다 - 면 가장자리의 3 x 3 PCF 가 칸 안의 실제 깊이를 읽게. 반 칸(256 텍셀)에서 4 텍셀을 여유로 둔다.
        // 셰이더는 방향의 가장 큰 축으로 면을 고르므로, 고른 면에서 점은 늘 가장자리보다 4 텍셀 안쪽이다.
        constexpr Float PointFaceMargin = 4.0f;
        // 점광의 가까운 면이다. 먼 면은 `range`(없으면 100)다.
        constexpr Float PointNear = 0.05f;

        // 상자와 공 [x, y, z, 반지름] 이 겹치는가(D-446).
        Bool BoxTouchesSphere(const Float* low, const Float* high, const Float* sphere)
        {
            Float distance = 0.0f;
            for (Int32 axis = 0; axis < 3; ++axis)
            {
                const Float value = sphere[axis.Get()];
                const Float outside = value < low[axis.Get()] ? Float(low[axis.Get()] - value)
                    : (value > high[axis.Get()] ? Float(value - high[axis.Get()]) : Float(0.0f));
                distance += outside * outside;
            }
            return distance <= sphere[3] * sphere[3];
        }
    }

    Bool Renderer::InvertMatrix4x4(const Matrix4x4& matrix, Matrix4x4& result)
    {
        return InvertMatrix(matrix, result);
    }

    UInt32 Renderer::PointShadowTileX(UInt32 tile)
    {
        return (tile % (ShadowTileSize * 4 / PointShadowTileSize)) * PointShadowTileSize;
    }

    UInt32 Renderer::PointShadowTileY(UInt32 tile)
    {
        return ShadowTileSize * 2 + (tile / (ShadowTileSize * 4 / PointShadowTileSize)) * PointShadowTileSize;
    }

    Bool Renderer::PrepareViewShadows(UInt32 viewIndex, GpuShadowConstants& out) const
    {
        out = m_noShadows;
        const ViewPacket& view = m_views[viewIndex.Get()];
        if (false == view.lighting3D || (view.shadowSun < 0 && view.shadowSpotCount == 0 && view.shadowPointCount == 0))
        {
            return false;
        }
        Bool any = false;
        const Float* v = view.camera.view.values;
        const Vector3 eye{-(v[0] * v[3] + v[4] * v[7] + v[8] * v[11]), -(v[1] * v[3] + v[5] * v[7] + v[9] * v[11]),
            -(v[2] * v[3] + v[6] * v[7] + v[10] * v[11])};
        const Vector3 forward = Normalize(Vector3{-v[8], -v[9], -v[10]});
        out.viewForward[0] = forward.x;
        out.viewForward[1] = forward.y;
        out.viewForward[2] = forward.z;
        out.params[1] = DirectionalBias;
        out.params[3] = SpotBias;

        Matrix4x4 inverse;
        if (view.shadowSun >= 0 && InvertMatrix(MultiplyMatrices(view.camera.projection, view.camera.view), inverse))
        {
            const GpuLight3D& sun = m_lights3D[(view.light3DOffset + static_cast<UInt32>(view.shadowSun.Get())).Get()];
            const Vector3 travel = Normalize(Vector3{sun.directionKind[0], sun.directionKind[1], sun.directionKind[2]});
            // 화면 네 모서리의 가까운·먼 점이다. 같은 모서리의 두 점 사이에서 카메라 깊이는 곧게 변하므로, 깊이의 비율로 사이를 잇는다.
            const Float corners[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}};
            Vector3 nearCorner[4];
            Vector3 farCorner[4];
            for (Int32 at = 0; at < 4; ++at)
            {
                nearCorner[at.Get()] = ProjectPoint(inverse, corners[at.Get()][0], corners[at.Get()][1], 0.0f);
                farCorner[at.Get()] = ProjectPoint(inverse, corners[at.Get()][0], corners[at.Get()][1], 1.0f);
            }
            const Float nearDepth = Dot(Subtract(nearCorner[0], eye), forward);
            const Float farDepth = Dot(Subtract(farCorner[0], eye), forward);
            const Float reach = view.camera.shadowDistance > 0.0f ? view.camera.shadowDistance : Float(50.0f);
            const Float shadowEnd = farDepth < nearDepth + reach ? farDepth : Float(nearDepth + reach);
            if (std::isfinite(nearDepth.Get()) && std::isfinite(farDepth.Get()) && farDepth > nearDepth && shadowEnd > nearDepth)
            {
                // 나누기는 고른 것과 로그의 섞음(0.75)이다 - 가까운 캐스케이드가 좁아 앞쪽 그림자가 촘촘하다.
                Float splits[5];
                splits[0] = nearDepth;
                const Float logStart = nearDepth > 0.05f ? nearDepth : Float(0.05f);
                for (Int32 at = 1; at <= 4; ++at)
                {
                    const Float fraction = static_cast<JBro::Float>(at.Get()) / 4.0f;
                    const Float uniform = nearDepth + (shadowEnd - nearDepth) * fraction;
                    const Float logarithmic = logStart * std::pow((shadowEnd / logStart).Get(), fraction.Get());
                    Float split = logarithmic * 0.75f + uniform * 0.25f;
                    split = split > splits[(at - 1).Get()] ? split : Float(splits[(at - 1).Get()] + 0.001f);
                    splits[at.Get()] = split;
                }
                splits[4] = shadowEnd;
                Vector3 right;
                Vector3 up;
                LightBasis(travel, right, up);
                for (UInt32 cascade = 0; cascade < MaxShadowCascades; ++cascade)
                {
                    Vector3 points[8];
                    for (Int32 corner = 0; corner < 4; ++corner)
                    {
                        const Vector3 ray = Subtract(farCorner[corner.Get()], nearCorner[corner.Get()]);
                        const Float a = (splits[cascade.Get()] - nearDepth) / (farDepth - nearDepth);
                        const Float b = (splits[(cascade + 1).Get()] - nearDepth) / (farDepth - nearDepth);
                        points[corner.Get()] = Add(nearCorner[corner.Get()], Scale(ray, a));
                        points[(corner + 4).Get()] = Add(nearCorner[corner.Get()], Scale(ray, b));
                    }
                    Vector3 center{0.0f, 0.0f, 0.0f};
                    for (const Vector3& point : points)
                    {
                        center = Add(center, Scale(point, 0.125f));
                    }
                    Float radius = 0.0f;
                    for (const Vector3& point : points)
                    {
                        const Float distance = Length(Subtract(point, center));
                        radius = distance > radius ? distance : radius;
                    }
                    // 공의 크기를 1/16 단위로 올려 둔다 - 카메라가 돌 때 소수점 아래가 흔들려 칸 크기가 바뀌지 않게.
                    radius = std::ceil((radius * 16.0f).Get()) / 16.0f;
                    if (false == (radius > 0.0f))
                    {
                        continue;
                    }
                    const Float texel = radius * 2.0f / static_cast<JBro::Float>(ShadowTileSize.Get());
                    // 중심을 라이트 평면에서 칸 크기로 맞춘다 - 카메라가 움직여도 같은 칸에 같은 물체가 떨어진다.
                    const Float alongRight = Dot(center, right);
                    const Float alongUp = Dot(center, up);
                    const Float snappedRight = std::floor((alongRight / texel).Get()) * texel;
                    const Float snappedUp = std::floor((alongUp / texel).Get()) * texel;
                    center = Add(center, Add(Scale(right, snappedRight - alongRight), Scale(up, snappedUp - alongUp)));
                    const Vector3 lightEye = Subtract(center, Scale(travel, radius + CasterReach));
                    const Matrix4x4 lightView = LightView(lightEye, travel);
                    const Matrix4x4 lightProjection = LightOrthographic(radius, 0.0f, radius * 2.0f + CasterReach);
                    out.cascades[cascade.Get()] = MultiplyMatrices(lightProjection, lightView);
                    out.cascadeSplit[cascade.Get()] = splits[(cascade + 1).Get()];
                    out.cascadeTexel[cascade.Get()] = texel;
                }
                out.viewForward[3] = static_cast<JBro::Float>(view.shadowSun.Get());
                out.params[0] = view.shadowSunStrength;
                out.params[2] = static_cast<JBro::Float>(MaxShadowCascades.Get());
                any = true;
            }
        }

        for (UInt32 slot = 0; slot < view.shadowSpotCount && slot < MaxShadowSpots; ++slot)
        {
            const Int32 index = view.shadowSpots[slot.Get()];
            const GpuLight3D& spot = m_lights3D[(view.light3DOffset + static_cast<UInt32>(index.Get())).Get()];
            const Vector3 position{spot.positionRange[0], spot.positionRange[1], spot.positionRange[2]};
            const Vector3 travel = Normalize(Vector3{spot.directionKind[0], spot.directionKind[1], spot.directionKind[2]});
            // 원뿔보다 조금 넓게 본다 - 가장자리의 PCF 가 칸 밖을 읽지 않게.
            const Float angle = view.shadowSpotAngle[slot.Get()] + 0.05f;
            const Float fieldOfView = angle < 3.0f ? angle : Float(3.0f);
            const Float farPlane = spot.positionRange[3] > 0.0f ? spot.positionRange[3] : Float(100.0f);
            const Matrix4x4 lightView = LightView(position, travel);
            const Matrix4x4 lightProjection = LightPerspective(fieldOfView, 0.05f, farPlane);
            out.spots[slot.Get()] = MultiplyMatrices(lightProjection, lightView);
            out.spotLight[slot.Get()] = static_cast<JBro::Float>(index.Get());
            out.spotTexel[slot.Get()] = 2.0f * std::tan((fieldOfView * 0.5f).Get()) / static_cast<JBro::Float>(ShadowTileSize.Get());
            out.spotStrength[slot.Get()] = view.shadowSpotStrength[slot.Get()];
            any = true;
        }

        // 점광(D-446): 면 여섯마다 그 축을 보는 원근이다. 시야각은 90 도에 여유 텍셀만큼 넓힌 것이다.
        const Float halfTile = static_cast<JBro::Float>(PointShadowTileSize.Get()) * 0.5f;
        const Float halfTangent = halfTile / (halfTile - PointFaceMargin);
        const Float pointFieldOfView = 2.0f * std::atan(halfTangent.Get());
        for (UInt32 slot = 0; slot < view.shadowPointCount && slot < MaxShadowPoints; ++slot)
        {
            const Int32 index = view.shadowPoints[slot.Get()];
            const GpuLight3D& point = m_lights3D[(view.light3DOffset + static_cast<UInt32>(index.Get())).Get()];
            const Vector3 position{point.positionRange[0], point.positionRange[1], point.positionRange[2]};
            const Float farPlane = point.positionRange[3] > 0.0f ? point.positionRange[3] : Float(100.0f);
            const Matrix4x4 lightProjection = LightPerspective(pointFieldOfView, PointNear, farPlane);
            for (UInt32 face = 0; face < 6; ++face)
            {
                const Float* direction = PointFaceDirections[face.Get()];
                out.points[(slot * 6 + face).Get()] = MultiplyMatrices(lightProjection, LightView(position, Vector3{direction[0], direction[1], direction[2]}));
            }
            out.pointLight[slot.Get()] = static_cast<JBro::Float>(index.Get());
            out.pointTexel[slot.Get()] = 2.0f * halfTangent / static_cast<JBro::Float>(PointShadowTileSize.Get());
            out.pointStrength[slot.Get()] = view.shadowPointStrength[slot.Get()];
            any = true;
        }
        return any;
    }

    Bool Renderer::AddShadowPass(UInt32 viewIndex, RenderGraphTexture& atlas)
    {
        atlas = {};
        while (m_viewShadows.Size() <= viewIndex.Get())
        {
            m_viewShadows.Add(m_noShadows);
        }
        const ViewPacket& view = m_views[viewIndex.Get()];
        if (view.runCount == 0 || false == m_shadowPipeline.IsValid())
        {
            return true;
        }
        Bool casters = false;
        for (UInt32 at = 0; at < view.runCount && false == casters; ++at)
        {
            casters = m_meshRuns[(view.runOffset + at).Get()].castShadows;
        }
        GpuShadowConstants block;
        if (false == casters || false == PrepareViewShadows(viewIndex, block))
        {
            return true;
        }
        if (false == m_shadowAtlas.IsValid())
        {
            // 장치는 프레임 안에서 자원을 만들지 않는다. 바라 두고 다음 프레임을 열기 전에 만든다 - 이 프레임은 그림자 없이 그린다.
            m_shadowAtlasWanted = true;
            return true;
        }
        m_viewShadows[viewIndex.Get()] = block;
        atlas = m_graph.ImportTexture(m_shadowAtlas, "ShadowAtlas");
        const RenderGraphPass pass = m_graph.AddPass(RenderGraphPassKind::Raster, "Shadow", &Renderer::ExecuteShadows, viewIndex);
        ShadowPassData data;
        data.renderer = this;
        data.view = viewIndex;
        return atlas.IsValid() && pass.IsValid()
            && m_graph.SetDepthAttachment(pass, atlas, LoadOperation::Clear, StoreOperation::Store, false)
            && m_graph.SetPassData(pass, data);
    }

    Bool Renderer::ExecuteShadows(RenderGraphContext& context, const void* data)
    {
        const ShadowPassData& pass = *static_cast<const ShadowPassData*>(data);
        Renderer& renderer = *pass.renderer;
        const ViewPacket& view = renderer.m_views[pass.view.Get()];
        const GpuShadowConstants& shadows = renderer.m_viewShadows[pass.view.Get()];
        IRHICommandContext& commands = context.GetCommands();
        const BufferHandle instances = renderer.m_meshInstanceBuffers[renderer.m_frame.slot];
        if (false == commands.SetGraphicsPipeline(renderer.m_shadowPipeline)
            || false == commands.SetVertexBuffer(1, instances, static_cast<JBro::UInt32>(sizeof(GpuMeshInstance)), 0))
        {
            return false;
        }
        // 지금 걸린 그림자 파이프라인이 스킨 쪽인가다(D-458). 위에서 일반 쪽을 걸었다.
        Bool skinnedBound = false;
        // `reach` 가 있으면(점광, D-446) 그 공 [x, y, z, 반지름] 과 겹치지 않는 구간은 그리지 않는다.
        const auto drawTile = [&](const Matrix4x4& lightMatrix, UInt32 x, UInt32 y, UInt32 tile, const Float* reach) {
            Viewport viewport;
            viewport.x = static_cast<JBro::Float>(x.Get());
            viewport.y = static_cast<JBro::Float>(y.Get());
            viewport.width = static_cast<JBro::Float>(tile.Get());
            viewport.height = static_cast<JBro::Float>(tile.Get());
            commands.SetViewport(viewport);
            commands.SetScissor({static_cast<JBro::Int32>(x.Get()), static_cast<JBro::Int32>(y.Get()),
                static_cast<JBro::Int32>((x + tile).Get()), static_cast<JBro::Int32>((y + tile).Get())});
            MeshPushConstants push;
            push.viewProjection = lightMatrix;
            const JArrayView<std::byte> pushBytes{reinterpret_cast<const std::byte*>(&push), static_cast<JBro::UInt32>(sizeof(push))};
            if (false == commands.SetGraphicsConstants(pushBytes))
            {
                return false;
            }
            // 스킨 메시(D-458)는 뼈를 b1 에서 읽는 파이프라인으로 그린다. 파이프라인을 바꾸면 루트 시그니처가 바뀌므로 푸시 상수와 인스턴스 흐름을 다시 건다.
            // 어느 쪽이 걸려 있는지는 타일을 넘어 이어진다(`skinnedBound`, 아래 람다 밖).
            const auto usePipeline = [&](Bool skinned) {
                if (skinned == skinnedBound)
                {
                    return true;
                }
                skinnedBound = skinned;
                return commands.SetGraphicsPipeline(skinned ? renderer.m_skinnedShadowPipeline : renderer.m_shadowPipeline)
                    && commands.SetVertexBuffer(1, instances, static_cast<JBro::UInt32>(sizeof(GpuMeshInstance)), 0)
                    && commands.SetGraphicsConstants(pushBytes);
            };
            for (UInt32 at = 0; at < view.runCount; ++at)
            {
                const MeshRun& run = renderer.m_meshRuns[(view.runOffset + at).Get()];
                const MeshResource* mesh = run.castShadows ? renderer.FindMesh(run.mesh) : nullptr;
                if (mesh == nullptr || (reach != nullptr && false == BoxTouchesSphere(run.boundsMin, run.boundsMax, reach)))
                {
                    continue;
                }
                const Bool skinned = run.skinPalette != 0 && mesh->skinBuffer.IsValid() && renderer.m_skinnedShadowPipeline.IsValid();
                if (false == usePipeline(skinned))
                {
                    return false;
                }
                if (skinned
                    && (false == commands.SetVertexBuffer(2, mesh->skinBuffer, static_cast<JBro::UInt32>(sizeof(MeshSkinVertex)), 0)
                        || false == commands.SetConstantBuffer(1, renderer.m_skinBuffers[renderer.m_frame.slot],
                            static_cast<std::size_t>((run.skinPalette - 1).Get()) * SkinPaletteBytes, SkinPaletteBytes)))
                {
                    return false;
                }
                if (false == commands.SetVertexBuffer(0, mesh->vertexBuffer, static_cast<JBro::UInt32>(sizeof(MeshVertex)), 0)
                    || false == commands.SetIndexBuffer(mesh->indexBuffer, IndexFormat::UInt32, 0)
                    || false == commands.DrawIndexedInstanced(mesh->indexCount, run.instanceCount, 0, 0, run.firstInstance))
                {
                    return false;
                }
            }
            return true;
        };
        const UInt32 cascades = static_cast<UInt32>(static_cast<std::uint32_t>(shadows.params[2].Get()));
        for (UInt32 cascade = 0; cascade < cascades && cascade < MaxShadowCascades; ++cascade)
        {
            if (false == drawTile(shadows.cascades[cascade.Get()], (cascade % 2) * ShadowTileSize, (cascade / 2) * ShadowTileSize, ShadowTileSize, nullptr))
            {
                return false;
            }
        }
        for (UInt32 slot = 0; slot < MaxShadowSpots; ++slot)
        {
            if (shadows.spotLight[slot.Get()] >= 0.0f
                && false == drawTile(shadows.spots[slot.Get()], (slot % 2 + 2) * ShadowTileSize, (slot / 2) * ShadowTileSize, ShadowTileSize, nullptr))
            {
                return false;
            }
        }
        for (UInt32 slot = 0; slot < MaxShadowPoints; ++slot)
        {
            if (false == (shadows.pointLight[slot.Get()] >= 0.0f))
            {
                continue;
            }
            const UInt32 index = static_cast<UInt32>(static_cast<std::uint32_t>(shadows.pointLight[slot.Get()].Get()));
            const GpuLight3D& point = renderer.m_lights3D[(view.light3DOffset + index).Get()];
            const Float reach[4] = {point.positionRange[0], point.positionRange[1], point.positionRange[2],
                point.positionRange[3] > 0.0f ? point.positionRange[3] : Float(100.0f)};
            for (UInt32 face = 0; face < 6; ++face)
            {
                const UInt32 tile = slot * 6 + face;
                if (false == drawTile(shadows.points[tile.Get()], PointShadowTileX(tile), PointShadowTileY(tile), PointShadowTileSize, reach))
                {
                    return false;
                }
            }
        }
        return true;
    }
}
