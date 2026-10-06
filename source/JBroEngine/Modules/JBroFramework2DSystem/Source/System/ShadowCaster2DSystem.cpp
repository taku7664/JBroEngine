#include <JBro/Framework2DSystem/System/ShadowCaster2DSystem.h>

#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/ShadowCaster2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Physics2D/Geometry.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

#include <cmath>

namespace JBro::System
{
    namespace
    {
        constexpr Float Pi = 3.14159265f;
        // 원과 캡슐의 둥근 끝을 몇 조각으로 펴는가. 그림자의 가장자리가 이 다각형의 변이다.
        constexpr UInt32 CircleSegments = 24;
        constexpr UInt32 ArcSegments = 12;

        // 콜라이더의 꼭짓점을 월드로 옮긴다: 크기를 곱한 로컬을 돌리고 옮긴다(물리와 같은 셈, D-199).
        struct ColliderPose
        {
            Vector2 position;
            Float cosine = 1.0f;
            Float sine = 0.0f;

            Vector2 ToWorld(Vector2 scaledLocal) const
            {
                return Vector2{ position.x + scaledLocal.x * cosine - scaledLocal.y * sine,
                    position.y + scaledLocal.x * sine + scaledLocal.y * cosine };
            }
        };

        // 꼭짓점 `count` 개(`pointAt(i)` 가 월드 자리를 준다)를 이은 변을 낸다. 닫힌 모양은 넓이가 음수면(시계 방향으로 감겼으면) 변의 방향을 뒤집어
        // 시계 반대 방향으로 맞춘다 - 렌더러는 바깥 법선이 오른쪽이라고 본다. 열린 모양(체인)은 두께가 없어 늘 제 그늘에 든다.
        template <typename PointAt>
        void EmitOutline(RenderWorld2D& world, UInt32 count, Bool closed, Bool selfShadow, Float parallax, PointAt pointAt)
        {
            if (count < 2 || (closed && count < 3))
            {
                return;
            }
            Float twiceArea = 0.0f;
            if (closed)
            {
                for (UInt32 at = 0; at < count; ++at)
                {
                    const Vector2 a = pointAt(at);
                    const Vector2 b = pointAt((at + 1) % count);
                    twiceArea += a.x * b.y - b.x * a.y;
                }
            }
            const Bool reverse = closed && twiceArea < 0.0f;
            const UInt32 edges = closed ? count : count - 1;
            for (UInt32 at = 0; at < edges; ++at)
            {
                const Vector2 a = pointAt(at);
                const Vector2 b = pointAt((at + 1) % count);
                ShadowEdge2DItem edge;
                edge.from = reverse ? b : a;
                edge.to = reverse ? a : b;
                edge.selfShadow = selfShadow || false == closed;
                edge.layerParallax = parallax;
                world.SubmitShadowEdge(edge);
            }
        }

        void EmitCollider(RenderWorld2D& world, const Component::Collider2D& collider, const Component::Transform2D& transform,
            Bool selfShadow, Float parallax)
        {
            ColliderPose pose;
            pose.position = transform.worldPosition;
            pose.cosine = std::cos(transform.worldRotation.Get());
            pose.sine = std::sin(transform.worldRotation.Get());
            const Vector2 scale = transform.worldScale;
            const auto scaled = [&](Float x, Float y) { return Vector2{ x * scale.x, y * scale.y }; };
            const Vector2 offset = collider.offset;
            switch (collider.shape)
            {
            case Component::ColliderShape2D::Circle:
            {
                // 원은 큰 쪽 크기로 커진다(물리가 그렇게 다룬다).
                const Float scaleX = std::fabs(scale.x);
                const Float scaleY = std::fabs(scale.y);
                const Float radius = collider.radius * (scaleX > scaleY ? scaleX : scaleY);
                const Vector2 center = scaled(offset.x, offset.y);
                EmitOutline(world, CircleSegments, true, selfShadow, parallax, [&](UInt32 at) {
                    const Float turn = 2.0f * Pi * static_cast<JBro::Float>(at) / static_cast<JBro::Float>(CircleSegments);
                    return pose.ToWorld(Vector2{ center.x + std::cos(turn) * radius, center.y + std::sin(turn) * radius });
                });
                return;
            }
            case Component::ColliderShape2D::Capsule:
            {
                // 물리와 같은 함수로 잰다: 크기를 곱한 `size` 상자에 꼭 맞는 알약이다.
                const Physics2D::ConvexPolygon capsule = Physics2D::MakeCapsuleInBox(
                    scaled(offset.x, offset.y), scaled(collider.size.x * 0.5f, collider.size.y * 0.5f));
                const Vector2 a = capsule.points[0];
                const Vector2 b = capsule.points[1];
                const Float length = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
                const Vector2 axis = length > 0.0f ? Vector2{ (b.x - a.x) / length, (b.y - a.y) / length } : Vector2{ 1.0f, 0.0f };
                const Vector2 side{ -axis.y, axis.x };
                constexpr UInt32 PerCap = ArcSegments + 1;
                EmitOutline(world, PerCap * 2, true, selfShadow, parallax, [&](UInt32 at) {
                    // b 쪽 반원(옆 -side 에서 축 방향을 지나 +side), 이어서 a 쪽 반원.
                    const Bool first = at < PerCap;
                    const UInt32 k = first ? at : at - PerCap;
                    const Vector2 cap = first ? b : a;
                    const Float start = first ? -0.5f * Pi : 0.5f * Pi;
                    const Float turn = start + Pi * static_cast<JBro::Float>(k) / static_cast<JBro::Float>(ArcSegments);
                    const Float c = std::cos(turn) * capsule.radius;
                    const Float s = std::sin(turn) * capsule.radius;
                    return pose.ToWorld(Vector2{ cap.x + axis.x * c + side.x * s, cap.y + axis.y * c + side.y * s });
                });
                return;
            }
            case Component::ColliderShape2D::Chain:
            {
                if (collider.points.IsEmpty())
                {
                    // 포인트가 없으면 `size.x` 폭의 가로 선분이다(물리와 같다).
                    const Float half = collider.size.x * 0.5f;
                    EmitOutline(world, 2, false, true, parallax, [&](UInt32 at) {
                        return pose.ToWorld(scaled(offset.x + (at == 0 ? Float(-half) : half), offset.y));
                    });
                    return;
                }
                const UInt32 count = static_cast<JBro::UInt32>(collider.points.Size());
                EmitOutline(world, count, collider.loop && count >= 3, true, parallax, [&](UInt32 at) {
                    const Vector2 point = collider.points[at];
                    return pose.ToWorld(scaled(point.x + offset.x, point.y + offset.y));
                });
                return;
            }
            case Component::ColliderShape2D::Polygon:
                if (collider.points.Size() >= 3)
                {
                    EmitOutline(world, static_cast<JBro::UInt32>(collider.points.Size()), true, selfShadow, parallax, [&](UInt32 at) {
                        const Vector2 point = collider.points[at];
                        return pose.ToWorld(scaled(point.x + offset.x, point.y + offset.y));
                    });
                    return;
                }
                // 꼭짓점이 모자라면 물리처럼 `size` 상자다.
                [[fallthrough]];
            case Component::ColliderShape2D::Box:
            default:
            {
                const Float halfX = collider.size.x * 0.5f;
                const Float halfY = collider.size.y * 0.5f;
                EmitOutline(world, 4, true, selfShadow, parallax, [&](UInt32 at) {
                    const Float x = (at == 0 || at == 3) ? Float(-halfX) : halfX;
                    const Float y = at < 2 ? Float(-halfY) : halfY;
                    return pose.ToWorld(scaled(offset.x + x, offset.y + y));
                });
                return;
            }
            }
        }
    }

    Int32 ShadowCaster2DSystem::GetExecutionOrder() const
    {
        return 410;
    }

    void ShadowCaster2DSystem::SetRenderWorld(RenderWorld2D* renderWorld)
    {
        m_renderWorld = renderWorld;
    }

    void ShadowCaster2DSystem::SetSpriteLibrary(SpriteLibrary* library)
    {
        m_spriteLibrary = library;
    }

    void ShadowCaster2DSystem::ExtractRenderWorld(Canvas& canvas)
    {
        if (m_renderWorld == nullptr)
        {
            return;
        }
        canvas.ForEach<Component::ShadowCaster2D>([&](Component::ShadowCaster2D& caster)
        {
            if (false == caster.IsActiveComponent())
            {
                return;
            }
            GameObject* owner = Internal::CanvasAccess::GetOwner(caster);
            const Layer* layer = owner != nullptr ? owner->GetLayer() : nullptr;
            if (layer != nullptr && (false == layer->IsVisible() || layer->GetSpace() == LayerSpace::Screen))
            {
                return;
            }
            const auto* transform = canvas.FindComponentRaw<Component::Transform2D>(owner);
            if (transform == nullptr || false == transform->IsActiveComponent() || false == transform->worldValid)
            {
                return;
            }
            const Float parallax = layer != nullptr ? layer->GetParallax() : Float(1.0f);
            if (caster.shape == Component::ShadowShape2D::Collider)
            {
                const auto* collider = canvas.FindComponentRaw<Component::Collider2D>(owner);
                if (collider != nullptr && collider->IsActiveComponent())
                {
                    EmitCollider(*m_renderWorld, *collider, *transform, caster.selfShadow, parallax);
                }
                return;
            }
            // 스프라이트가 그려지는 사각형이다. 크기와 피벗은 스프라이트 추출과 같이 푼다(D-117).
            const auto* sprite = canvas.FindComponentRaw<Component::SpriteRenderer2D>(owner);
            if (sprite == nullptr || false == sprite->IsActiveComponent() || false == sprite->visible)
            {
                return;
            }
            AssetHandle texture;
            Float uvRect[4] = {};
            SpriteFrameView frame;
            const Bool resolved = m_spriteLibrary != nullptr && sprite->sprite.generation != 0
                && m_spriteLibrary->Resolve(sprite->sprite, sprite->frameIndex, texture, uvRect, &frame);
            const Vector2 size = (resolved && sprite->sizeMode == Component::SpriteSizeMode::FromSprite)
                ? Vector2{ frame.widthUnits, frame.heightUnits } : sprite->size;
            const Vector2 pivot = (resolved && sprite->pivotMode == Component::SpritePivotMode::FromSprite)
                ? Vector2{ frame.pivotX, frame.pivotY } : sprite->pivot;
            const Matrix3x2& world = transform->world;
            EmitOutline(*m_renderWorld, 4, true, caster.selfShadow, parallax, [&](UInt32 at) {
                const Float x = ((at == 0 || at == 3) ? Float(-pivot.x) : Float(1.0f - pivot.x)) * size.x;
                const Float y = (at < 2 ? Float(-pivot.y) : Float(1.0f - pivot.y)) * size.y;
                // 행 벡터 규약이다(`BuildSprite` 와 같다).
                return Vector2{ x * world.m11 + y * world.m21 + world.m31, x * world.m12 + y * world.m22 + world.m32 };
            });
        });
    }

    void ShadowCaster2DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        (void)deltaTime;
        ExtractRenderWorld(canvas);
    }
}
