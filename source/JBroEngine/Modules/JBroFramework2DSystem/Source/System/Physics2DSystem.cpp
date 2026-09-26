#include <JBro/Framework2DSystem/System/Physics2DSystem.h>

#include "Physics2DGeometry.h"

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Physics2D/Collision.h>
#include <JBro/Physics2D/Geometry.h>
#include <JBro/Physics2D/World.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/Table.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace JBro::System
{
    namespace
    {
        constexpr float DirectionEpsilonSquared = 0.000000000001f;

        Physics2D::BodyType ToKernel(Component::BodyType2D type)
        {
            switch (type)
            {
            case Component::BodyType2D::Kinematic:
                return Physics2D::BodyType::Kinematic;
            case Component::BodyType2D::Dynamic:
                return Physics2D::BodyType::Dynamic;
            default:
                return Physics2D::BodyType::Static;
            }
        }

        // 값이 바뀌었는지 보는 지문이다. 같은 값이면 같은 수가 나와, 매 스텝 도형을 다시 만들지 않게 한다.
        struct Fingerprint
        {
            std::uint64_t value = 14695981039346656037ull;

            void Mix(const void* data, std::size_t size)
            {
                const unsigned char* bytes = static_cast<const unsigned char*>(data);
                for (std::size_t i = 0; i < size; ++i)
                {
                    value ^= bytes[i];
                    value *= 1099511628211ull;
                }
            }

            void Mix(float number)
            {
                // -0 과 +0 은 같은 값이다. 비트로 섞으면 달라 보여 도형을 괜히 다시 만든다.
                const float normalized = number == 0.0f ? 0.0f : number;
                Mix(&normalized, sizeof(normalized));
            }

            void Mix(Vec2 vector)
            {
                Mix(vector.x);
                Mix(vector.y);
            }
        };

        float MaxAbs(Vec2 scale)
        {
            return std::fmax(std::fabs(scale.x), std::fabs(scale.y));
        }

        Vec2 Bake(Vec2 local, Vec2 offset, Vec2 scale)
        {
            return { (local.x + offset.x) * scale.x, (local.y + offset.y) * scale.y };
        }

        // 크기를 곱한 바디 로컬의 상자 네 점. 크기가 음수면 감긴 방향이 뒤집히지만 커널의 정리가 되돌린다.
        void BakeBox(const Component::Collider2D& collider, Vec2 scale, Vec2 out[4])
        {
            const Vec2 half = { collider.size.x * 0.5f, collider.size.y * 0.5f };
            out[0] = Bake({ -half.x, -half.y }, collider.offset, scale);
            out[1] = Bake({ half.x, -half.y }, collider.offset, scale);
            out[2] = Bake({ half.x, half.y }, collider.offset, scale);
            out[3] = Bake({ -half.x, half.y }, collider.offset, scale);
        }

        // 질의용 상자. 커널의 정리를 거치지 않으므로 뒤집혔으면 여기서 반시계로 되돌린다.
        Physics2D::ConvexPolygon BoxPolygon(const Vec2 corners[4])
        {
            Physics2D::ConvexPolygon polygon;
            const float cross = (corners[1].x - corners[0].x) * (corners[2].y - corners[1].y)
                - (corners[1].y - corners[0].y) * (corners[2].x - corners[1].x);
            for (std::uint32_t i = 0; i < 4; ++i)
            {
                polygon.points[i] = cross >= 0.0f ? corners[i] : corners[3 - i];
            }
            polygon.count = 4;
            return polygon;
        }

        Physics2D::Circle BakeCircle(const Component::Collider2D& collider, Vec2 scale)
        {
            // 원은 한 축으로만 커져도 원으로 남으므로 더 큰 쪽으로 잰다(에디터 그림과 같다, D-143).
            Physics2D::Circle circle;
            circle.center = Bake({}, collider.offset, scale);
            circle.radius = collider.radius * MaxAbs(scale);
            return circle;
        }

        // 크기를 곱한 `size` 상자에 꼭 맞는 캡슐(physics-plan §4 의 7). 긴 축으로 눕고 반지름은 짧은 쪽 반폭이라, 한 축으로만
        // 늘여도 캡슐로 남는다. 캔버스 뷰가 같은 함수로 그린다.
        Physics2D::ConvexPolygon BakeCapsule(const Component::Collider2D& collider, Vec2 scale)
        {
            return Physics2D::MakeCapsuleInBox(Bake({}, collider.offset, scale),
                { collider.size.x * 0.5f * scale.x, collider.size.y * 0.5f * scale.y });
        }

        void BakeOutline(const Component::Collider2D& collider, Vec2 scale, Array<Vec2>& outline)
        {
            outline.Clear();
            // 꼭짓점이 없는 폴리곤은 `size` 상자다. 모양을 Polygon 으로 막 바꾼 콜라이더가 아무것에도 부딪히지 않으면
            // 캔버스 뷰가 그 상자를 그려 편집의 출발점으로 주는 것(기존 엔진의 절차적 빌드)과 어긋난다.
            if (collider.points.IsEmpty())
            {
                Vec2 corners[4];
                BakeBox(collider, scale, corners);
                outline.Append(corners, 4);
                return;
            }
            for (const Vec2& point : collider.points)
            {
                outline.Add(Bake(point, collider.offset, scale));
            }
        }

        // 이미 있는 도형의 모양을 콜라이더에 맞춘다. 만들 때와 같은 갈래다. 틀린 외곽선이면 false 이고 모양은 그대로다.
        bool Reshape(Physics2D::World& world, Physics2D::ShapeId shape, const Component::Collider2D& collider, Vec2 scale,
            Array<Vec2>& outline)
        {
            if (collider.shape == Component::ColliderShape2D::Circle)
            {
                return world.SetCircleGeometry(shape, BakeCircle(collider, scale));
            }
            if (collider.shape == Component::ColliderShape2D::Capsule)
            {
                const Physics2D::ConvexPolygon capsule = BakeCapsule(collider, scale);
                return world.SetCapsuleGeometry(shape, capsule.points[0], capsule.points[1], capsule.radius);
            }
            if (collider.shape == Component::ColliderShape2D::Box)
            {
                Vec2 corners[4];
                BakeBox(collider, scale, corners);
                outline.Clear();
                outline.Append(corners, 4);
            }
            else
            {
                BakeOutline(collider, scale, outline);
            }
            return world.SetPolygonGeometry(shape, outline.View()) == Physics2D::PolygonError::None;
        }

        Physics2D::Pose ToPose(const Internal::ObjectPose& pose)
        {
            return { pose.position, Physics2D::Rotation::FromAngle(pose.angle) };
        }

        // 콜라이더 모양의 지문. 크기(트랜스폼)를 섞는 것은 크기를 도형에 미리 곱해 두기 때문이다.
        std::uint64_t ShapeSignature(const Component::Collider2D& collider, Vec2 scale)
        {
            Fingerprint print;
            const std::uint8_t shape = static_cast<std::uint8_t>(collider.shape);
            print.Mix(&shape, sizeof(shape));
            print.Mix(collider.offset);
            print.Mix(collider.size);
            print.Mix(collider.radius);
            print.Mix(scale);
            const std::uint8_t trigger = collider.isTrigger ? 1 : 0;
            print.Mix(&trigger, sizeof(trigger));
            print.Mix(collider.friction);
            print.Mix(collider.restitution);
            print.Mix(&collider.layer, sizeof(collider.layer));
            print.Mix(&collider.mask, sizeof(collider.mask));
            const std::uint64_t count = collider.points.Size();
            print.Mix(&count, sizeof(count));
            for (const Vec2& point : collider.points)
            {
                print.Mix(point);
            }
            return print.value;
        }

        std::uint64_t BodyParameters(const Component::Rigidbody2D* body)
        {
            Fingerprint print;
            if (body == nullptr)
            {
                return print.value;
            }
            print.Mix(body->mass);
            print.Mix(body->gravityScale);
            print.Mix(body->linearDamping);
            const std::uint8_t fixed = body->fixedRotation ? 1 : 0;
            print.Mix(&fixed, sizeof(fixed));
            return print.value;
        }
    }

    struct Physics2DSystem::State
    {
        struct BodyLink
        {
            Physics2D::BodyId   body;
            Physics2D::BodyType type = Physics2D::BodyType::Static;
            InstanceId          rigidbody = InvalidInstanceId;
            std::uint64_t       parameters = 0;
            // 지난 스텝에 이쪽에서 쓴 값. 스크립트나 에디터가 그 사이 바꿨는지를 이것과 견주어 안다.
            Vec2                writtenPosition;
            float               writtenRotation = 0.0f;
            Vec2                writtenVelocity;
            float               writtenAngularVelocity = 0.0f;
            // 정적인 몸을 마지막으로 옮겨 둔 월드 자리.
            Vec2                pushedPosition;
            float               pushedAngle = 0.0f;
            bool                seen = false;
            // 이번 스텝 안에서만 유효하다. 동기화마다 다시 잡는다.
            GameObject*             object = nullptr;
            Component::Transform2D* transform = nullptr;
            Component::Rigidbody2D* rigidbodyComponent = nullptr;
        };

        struct ShapeLink
        {
            Physics2D::ShapeId shape;
            InstanceId         collider = InvalidInstanceId;
            InstanceId         object = InvalidInstanceId;
            // 스크립트에 넘길 값(handle)과 호스트가 부를 자리(SafePtr) 둘 다 든다. 오브젝트가 사라지면 둘 다 죽는다.
            GameObjectHandle   owner;
            SafePtr<GameObject> ownerObject;
            std::uint64_t      signature = 0;
            // 트리거 여부가 바뀌면 도형을 새로 만든다(훅의 종류가 바뀐다). 나머지는 제자리에서 바꾼다.
            bool               isTrigger = false;
            bool               seen = false;
        };

        struct PieceCache
        {
            std::uint64_t                   signature = 0;
            bool                            built = false;
            Array<Physics2D::ConvexPolygon> pieces;
        };

        Physics2D::World              world;
        Table<InstanceId, BodyLink>   bodies;
        Table<InstanceId, ShapeLink>  shapes;
        // 이번 스텝에 지운 콜라이더의 연결. 그 끝 이벤트가 이번 커널 스텝에서 나오므로 발송할 때까지만 둔다.
        Array<ShapeLink>              retired;
        Array<InstanceId>             removals;
        // 질의용 폴리곤 조각. 질의는 지금의 컴포넌트를 보지만 분해는 꼭짓점이 바뀔 때 한 번만 한다.
        Table<InstanceId, PieceCache> pieces;
        Array<Vec2>                   outline;
        Physics2D::DecomposeScratch   decompose;

        // 오브젝트의 컴포넌트 중 스크립트를 가려내는 표(주소 정렬). ScriptSystem 과 같은 방식이고, 실행 순서 판번호가
        // 움직였을 때만 다시 만든다 - 프레임 경로에 dynamic_cast 를 두지 않는다(§9).
        Array<GameScriptBase*>        collectedScripts;
        Array<const ComponentBase*>   scriptKeys;
        std::uint64_t                 scriptRevision = std::numeric_limits<std::uint64_t>::max();
        Array<GameScript2D*>          hookTargets;

        const Array<Physics2D::ConvexPolygon>& PiecesFor(const Component::Collider2D& collider, Vec2 scale)
        {
            const InstanceId id = collider.GetInstanceId();
            PieceCache* cache = pieces.Find(id);
            if (cache == nullptr)
            {
                pieces.TryAdd(id, PieceCache{});
                cache = pieces.Find(id);
            }
            const std::uint64_t signature = ShapeSignature(collider, scale);
            if (false == cache->built || cache->signature != signature)
            {
                cache->signature = signature;
                cache->built = true;
                BakeOutline(collider, scale, outline);
                // 틀린 외곽선이면 조각이 비고, 질의에 걸리지 않는다. 충돌과 같은 판단이다.
                Physics2D::DecomposePolygon(outline.View(), cache->pieces, decompose);
            }
            return cache->pieces;
        }

        bool IsScript(const ComponentBase* component) const
        {
            return component != nullptr
                && std::binary_search(scriptKeys.begin(), scriptKeys.end(), component);
        }
    };

    Physics2DSystem::Physics2DSystem()
        : m_state(MakeOwnerPtr<State>())
    {
    }

    Physics2DSystem::~Physics2DSystem() = default;

    int Physics2DSystem::GetExecutionOrder() const
    {
        return 200;
    }

    void Physics2DSystem::SetGravity(Vec2 gravity)
    {
        m_gravity = gravity;
    }

    Vec2 Physics2DSystem::GetGravity() const
    {
        return m_gravity;
    }

    std::size_t Physics2DSystem::GetBodyCount() const
    {
        return m_state->bodies.Size();
    }

    std::size_t Physics2DSystem::GetShapeCount() const
    {
        std::size_t count = 0;
        for (const auto& entry : m_state->shapes)
        {
            if (m_state->world.IsValid(entry.MappedValue.shape))
            {
                ++count;
            }
        }
        return count;
    }

    namespace
    {
        // 질의 하나가 도형마다 받는 것. 폴리곤 콜라이더는 볼록 조각마다 한 번씩 불린다.
        struct QueryShape
        {
            GameObject*                     owner = nullptr;
            const Component::Collider2D*    collider = nullptr;
            const Physics2D::ConvexPolygon* polygon = nullptr;
            const Physics2D::Circle*        circle = nullptr;
            Physics2D::Pose                 pose;
        };

        bool NormalizeDirection(Vec2& direction)
        {
            const float lengthSquared = direction.x * direction.x + direction.y * direction.y;
            if (lengthSquared <= DirectionEpsilonSquared)
            {
                return false;
            }
            const float inverse = 1.0f / std::sqrt(lengthSquared);
            direction = { direction.x * inverse, direction.y * inverse };
            return true;
        }
    }

    template<typename Fn>
    void Physics2DSystem::ForEachQueryShape(std::uint32_t layerMask, Fn&& visit) const
    {
        if (m_canvas == nullptr)
        {
            return;
        }
        Canvas& canvas = *m_canvas;
        State& state = *m_state;
        canvas.ForEach<Component::Collider2D>([&](Component::Collider2D& collider)
        {
            if (false == collider.IsActiveComponent() || (collider.layer & layerMask) == 0u)
            {
                return;
            }
            GameObject* owner = Internal::CanvasAccess::GetOwner(collider);
            Internal::ObjectPose objectPose;
            if (owner == nullptr || false == Internal::CalculateObjectPose(canvas, owner, objectPose))
            {
                return;
            }
            QueryShape shape;
            shape.owner = owner;
            shape.collider = &collider;
            shape.pose = ToPose(objectPose);
            if (collider.shape == Component::ColliderShape2D::Circle)
            {
                const Physics2D::Circle circle = BakeCircle(collider, objectPose.scale);
                shape.circle = &circle;
                visit(shape);
                return;
            }
            if (collider.shape == Component::ColliderShape2D::Box)
            {
                Vec2 corners[4];
                BakeBox(collider, objectPose.scale, corners);
                const Physics2D::ConvexPolygon box = BoxPolygon(corners);
                shape.polygon = &box;
                visit(shape);
                return;
            }
            if (collider.shape == Component::ColliderShape2D::Capsule)
            {
                const Physics2D::ConvexPolygon capsule = BakeCapsule(collider, objectPose.scale);
                if (capsule.radius <= 0.0f)
                {
                    return;
                }
                shape.polygon = &capsule;
                visit(shape);
                return;
            }
            for (const Physics2D::ConvexPolygon& piece : state.PiecesFor(collider, objectPose.scale))
            {
                shape.polygon = &piece;
                visit(shape);
            }
        });
    }

    namespace
    {
        RaycastHit2D MakeHit(Canvas& canvas, GameObject* owner, Vec2 point, Vec2 normal, float distance)
        {
            RaycastHit2D hit;
            hit.other = owner->GetScriptHandle();
            hit.bodyType = Internal::GetBodyType(canvas, owner);
            hit.point = point;
            hit.normal = normal;
            hit.distance = distance;
            return hit;
        }

        void AddUnique(Array<GameObjectHandle>& results, GameObject* owner)
        {
            const InstanceId id = owner->GetInstanceId();
            for (const GameObjectHandle& existing : results)
            {
                if (existing.GetInstanceId() == id)
                {
                    return;
                }
            }
            results.Add(owner->GetScriptHandle());
        }

        bool RayShape(const QueryShape& shape, Vec2 origin, Vec2 direction, float maxDistance,
            float& distance, Vec2& normal)
        {
            return shape.circle != nullptr
                ? Physics2D::RaycastCircle(*shape.circle, shape.pose, origin, direction, maxDistance, distance, normal)
                : Physics2D::RaycastPolygon(*shape.polygon, shape.pose, origin, direction, maxDistance, distance, normal);
        }
    }

    bool Physics2DSystem::Raycast(
        Vec2 origin, Vec2 direction, float distance, RaycastHit2D& hit, std::uint32_t layerMask) const
    {
        hit = {};
        if (m_canvas == nullptr || distance < 0.0f || false == NormalizeDirection(direction))
        {
            return false;
        }
        bool found = false;
        ForEachQueryShape(layerMask, [&](const QueryShape& shape)
        {
            float candidate = 0.0f;
            Vec2 normal;
            if (RayShape(shape, origin, direction, distance, candidate, normal)
                && (false == found || candidate < hit.distance))
            {
                found = true;
                hit = MakeHit(*m_canvas, shape.owner,
                    { origin.x + direction.x * candidate, origin.y + direction.y * candidate }, normal, candidate);
            }
        });
        return found;
    }

    void Physics2DSystem::RaycastAll(
        Vec2 origin, Vec2 direction, float distance, Array<RaycastHit2D>& hits, std::uint32_t layerMask) const
    {
        hits.Clear();
        if (m_canvas == nullptr || distance < 0.0f || false == NormalizeDirection(direction))
        {
            return;
        }
        // 콜라이더마다 한 번이다. 폴리곤의 조각 여럿에 걸려도 그 콜라이더의 가장 가까운 것 하나다 - 조각은 우리 사정이다.
        const Component::Collider2D* current = nullptr;
        std::size_t currentIndex = 0;
        ForEachQueryShape(layerMask, [&](const QueryShape& shape)
        {
            float candidate = 0.0f;
            Vec2 normal;
            if (false == RayShape(shape, origin, direction, distance, candidate, normal))
            {
                return;
            }
            const RaycastHit2D hit = MakeHit(*m_canvas, shape.owner,
                { origin.x + direction.x * candidate, origin.y + direction.y * candidate }, normal, candidate);
            if (shape.collider == current)
            {
                if (candidate < hits[currentIndex].distance)
                {
                    hits[currentIndex] = hit;
                }
                return;
            }
            current = shape.collider;
            currentIndex = hits.Size();
            hits.Add(hit);
        });
        std::sort(hits.begin(), hits.end(), [](const RaycastHit2D& left, const RaycastHit2D& right)
        {
            return left.distance < right.distance;
        });
    }

    void Physics2DSystem::OverlapBox(
        const Rect& area, Array<GameObjectHandle>& results, std::uint32_t layerMask) const
    {
        results.Clear();
        Physics2D::ConvexPolygon box;
        box.points[0] = { area.min.x, area.min.y };
        box.points[1] = { area.max.x, area.min.y };
        box.points[2] = { area.max.x, area.max.y };
        box.points[3] = { area.min.x, area.max.y };
        box.count = 4;
        const Physics2D::Pose identity;
        ForEachQueryShape(layerMask, [&](const QueryShape& shape)
        {
            const bool overlaps = shape.circle != nullptr
                ? Physics2D::OverlapPolygonAndCircle(box, identity, *shape.circle, shape.pose)
                : Physics2D::OverlapPolygons(box, identity, *shape.polygon, shape.pose);
            if (overlaps)
            {
                AddUnique(results, shape.owner);
            }
        });
    }

    GameObjectHandle Physics2DSystem::OverlapPoint(Vec2 point, std::uint32_t layerMask) const
    {
        GameObjectHandle found;
        ForEachQueryShape(layerMask, [&](const QueryShape& shape)
        {
            if (found.GetInstanceId() != InvalidInstanceId)
            {
                return;
            }
            const bool inside = shape.circle != nullptr
                ? Physics2D::ContainsPoint(*shape.circle, shape.pose, point)
                : Physics2D::ContainsPoint(*shape.polygon, shape.pose, point);
            if (inside)
            {
                found = shape.owner->GetScriptHandle();
            }
        });
        return found;
    }

    void Physics2DSystem::OverlapCircle(
        Vec2 center, float radius, Array<GameObjectHandle>& results, std::uint32_t layerMask) const
    {
        results.Clear();
        if (radius < 0.0f)
        {
            return;
        }
        Physics2D::Circle probe;
        probe.center = center;
        probe.radius = radius;
        const Physics2D::Pose identity;
        ForEachQueryShape(layerMask, [&](const QueryShape& shape)
        {
            const bool overlaps = shape.circle != nullptr
                ? Physics2D::OverlapCircles(probe, identity, *shape.circle, shape.pose)
                : Physics2D::OverlapPolygonAndCircle(*shape.polygon, shape.pose, probe, identity);
            if (overlaps)
            {
                AddUnique(results, shape.owner);
            }
        });
    }

    bool Physics2DSystem::CircleCast(Vec2 origin, float radius, Vec2 direction, float distance,
        RaycastHit2D& hit, std::uint32_t layerMask) const
    {
        hit = {};
        if (m_canvas == nullptr || distance < 0.0f || radius < 0.0f || false == NormalizeDirection(direction))
        {
            return false;
        }
        bool found = false;
        ForEachQueryShape(layerMask, [&](const QueryShape& shape)
        {
            float candidate = 0.0f;
            Vec2 normal;
            const bool touched = shape.circle != nullptr
                ? Physics2D::CastCircle(origin, radius, direction, distance, *shape.circle, shape.pose, candidate, normal)
                : Physics2D::CastCircle(origin, radius, direction, distance, *shape.polygon, shape.pose, candidate, normal);
            if (touched && (false == found || candidate < hit.distance))
            {
                found = true;
                const Vec2 center{ origin.x + direction.x * candidate, origin.y + direction.y * candidate };
                // 맞은 순간 원이 표면에 닿는 자리다. 출발부터 겹쳤으면 원의 중심이다.
                const Vec2 point = candidate == 0.0f
                    ? center
                    : Vec2{ center.x - normal.x * radius, center.y - normal.y * radius };
                hit = MakeHit(*m_canvas, shape.owner, point, normal, candidate);
            }
        });
        return found;
    }

    bool Physics2DSystem::BoxCast(Vec2 center, Vec2 halfExtents, float angle, Vec2 direction, float distance,
        RaycastHit2D& hit, std::uint32_t layerMask) const
    {
        hit = {};
        if (m_canvas == nullptr || distance < 0.0f || halfExtents.x < 0.0f || halfExtents.y < 0.0f
            || false == NormalizeDirection(direction))
        {
            return false;
        }
        Physics2D::ConvexPolygon box;
        box.points[0] = { -halfExtents.x, -halfExtents.y };
        box.points[1] = { halfExtents.x, -halfExtents.y };
        box.points[2] = { halfExtents.x, halfExtents.y };
        box.points[3] = { -halfExtents.x, halfExtents.y };
        box.count = 4;
        const Physics2D::Pose start{ center, Physics2D::Rotation::FromAngle(angle) };
        bool found = false;
        ForEachQueryShape(layerMask, [&](const QueryShape& shape)
        {
            float candidate = 0.0f;
            Vec2 normal;
            const bool touched = shape.circle != nullptr
                ? Physics2D::CastPolygon(box, start, direction, distance, *shape.circle, shape.pose, candidate, normal)
                : Physics2D::CastPolygon(box, start, direction, distance, *shape.polygon, shape.pose, candidate, normal);
            if (false == touched || (found && candidate >= hit.distance))
            {
                return;
            }
            found = true;
            const Vec2 moved{ center.x + direction.x * candidate, center.y + direction.y * candidate };
            Vec2 point = moved;
            if (candidate > 0.0f)
            {
                // 상자에서 법선의 반대쪽으로 가장 나온 점들(면이 닿으면 그 면의 가운데)이 닿은 자리다.
                const Physics2D::Pose at{ moved, start.rotation };
                float lowest = 0.0f;
                Vec2 corners[4];
                for (std::uint32_t k = 0; k < 4; ++k)
                {
                    corners[k] = Physics2D::TransformPoint(at, box.points[k]);
                    const float along = corners[k].x * normal.x + corners[k].y * normal.y;
                    lowest = k == 0 ? along : std::fmin(lowest, along);
                }
                Vec2 sum;
                int count = 0;
                for (const Vec2& corner : corners)
                {
                    if (corner.x * normal.x + corner.y * normal.y <= lowest + Physics2D::LinearSlop)
                    {
                        sum = { sum.x + corner.x, sum.y + corner.y };
                        ++count;
                    }
                }
                point = { sum.x / static_cast<float>(count), sum.y / static_cast<float>(count) };
            }
            hit = MakeHit(*m_canvas, shape.owner, point, normal, candidate);
        });
        return found;
    }

    void Physics2DSystem::OnInitialize(Canvas& canvas)
    {
        m_canvas = &canvas;
        m_state = MakeOwnerPtr<State>();
    }

    void Physics2DSystem::OnShutdown(Canvas& canvas)
    {
        (void)canvas;
        m_canvas = nullptr;
        // 커널과 연결을 모두 버린다. 다시 켜면 처음부터 만든다 - 재생을 멈추고 다시 켰을 때 남은 끝 이벤트가 튀지 않는다.
        m_state = MakeOwnerPtr<State>();
    }

    void Physics2DSystem::OnFixedUpdate(Canvas& canvas, float fixedDeltaTime)
    {
        if (fixedDeltaTime <= 0.0f)
        {
            return;
        }
        State& state = *m_state;
        Physics2D::World& world = state.world;

        for (auto& entry : state.bodies)
        {
            entry.MappedValue.seen = false;
            entry.MappedValue.object = nullptr;
            entry.MappedValue.transform = nullptr;
            entry.MappedValue.rigidbodyComponent = nullptr;
        }
        for (auto& entry : state.shapes)
        {
            entry.MappedValue.seen = false;
        }

        // ── 1. 바디 ─────────────────────────────────────────────────────────────────
        // 한 오브젝트에 바디 하나다. 켜진 Rigidbody2D 가 있으면 그 종류이고, 콜라이더만 있으면 정적이다.
        const auto ensureBody = [&](GameObject* object) -> State::BodyLink*
        {
            if (object == nullptr)
            {
                return nullptr;
            }
            State::BodyLink* link = state.bodies.Find(object->GetInstanceId());
            if (link != nullptr && link->seen)
            {
                return link;
            }

            Component::Transform2D* transform = canvas.FindComponentRaw<Component::Transform2D>(object);
            Internal::ObjectPose pose;
            if (transform == nullptr || false == Internal::CalculateObjectPose(canvas, object, pose))
            {
                return nullptr;
            }
            Component::Rigidbody2D* rigidbody = canvas.FindComponentRaw<Component::Rigidbody2D>(object);
            if (rigidbody != nullptr && false == rigidbody->IsActiveComponent())
            {
                rigidbody = nullptr;
            }
            const Physics2D::BodyType type =
                rigidbody != nullptr ? ToKernel(rigidbody->bodyType) : Physics2D::BodyType::Static;
            const InstanceId rigidbodyId = rigidbody != nullptr ? rigidbody->GetInstanceId() : InvalidInstanceId;
            const std::uint64_t parameters = BodyParameters(rigidbody);

            // 종류나 질량 같은 성질이 바뀌면 바디를 다시 만든다. 드문 일이라 커널에 성질을 바꾸는 길을 따로 두지 않는다.
            if (link != nullptr
                && (link->type != type || link->rigidbody != rigidbodyId || link->parameters != parameters
                    || false == world.IsValid(link->body)))
            {
                world.DestroyBody(link->body);
                state.bodies.Remove(object->GetInstanceId());
                link = nullptr;
            }

            if (link == nullptr)
            {
                Physics2D::BodyDef def;
                def.type = type;
                def.position = pose.position;
                def.angle = pose.angle;
                if (rigidbody != nullptr)
                {
                    def.linearVelocity = rigidbody->linearVelocity;
                    def.angularVelocity = rigidbody->angularVelocity;
                    def.mass = rigidbody->mass;
                    def.gravityScale = rigidbody->gravityScale;
                    def.linearDamping = rigidbody->linearDamping;
                    def.fixedRotation = rigidbody->fixedRotation;
                }
                def.userData = object->GetInstanceId();

                State::BodyLink fresh;
                fresh.body = world.CreateBody(def);
                fresh.type = type;
                fresh.rigidbody = rigidbodyId;
                fresh.parameters = parameters;
                fresh.writtenPosition = transform->position;
                fresh.writtenRotation = transform->rotation;
                fresh.writtenVelocity = def.linearVelocity;
                fresh.writtenAngularVelocity = def.angularVelocity;
                fresh.pushedPosition = pose.position;
                fresh.pushedAngle = pose.angle;
                state.bodies.TryAdd(object->GetInstanceId(), fresh);
                link = state.bodies.Find(object->GetInstanceId());
            }
            else if (type == Physics2D::BodyType::Static)
            {
                // 정적인 몸은 에디터·스크립트가 옮긴 자리로 따라간다.
                if (pose.position.x != link->pushedPosition.x || pose.position.y != link->pushedPosition.y
                    || pose.angle != link->pushedAngle)
                {
                    world.SetTransform(link->body, pose.position, pose.angle);
                    link->pushedPosition = pose.position;
                    link->pushedAngle = pose.angle;
                }
            }
            else
            {
                // 움직이는 몸의 자세는 커널이 주인이다. 다만 지난번에 쓴 값과 다르면 그 사이 누가 옮긴 것이므로 따른다.
                if (transform->position.x != link->writtenPosition.x || transform->position.y != link->writtenPosition.y
                    || transform->rotation != link->writtenRotation)
                {
                    world.SetTransform(link->body, pose.position, pose.angle);
                }
                if (rigidbody->linearVelocity.x != link->writtenVelocity.x
                    || rigidbody->linearVelocity.y != link->writtenVelocity.y)
                {
                    world.SetLinearVelocity(link->body, rigidbody->linearVelocity);
                }
                if (rigidbody->angularVelocity != link->writtenAngularVelocity)
                {
                    world.SetAngularVelocity(link->body, rigidbody->angularVelocity);
                }
            }

            link->seen = true;
            link->object = object;
            link->transform = transform;
            link->rigidbodyComponent = rigidbody;
            return link;
        };

        canvas.ForEach<Component::Rigidbody2D>([&](Component::Rigidbody2D& rigidbody)
        {
            if (rigidbody.IsActiveComponent())
            {
                ensureBody(Internal::CanvasAccess::GetOwner(rigidbody));
            }
        });

        // ── 2. 도형 ─────────────────────────────────────────────────────────────────
        canvas.ForEach<Component::Collider2D>([&](Component::Collider2D& collider)
        {
            if (false == collider.IsActiveComponent())
            {
                return;
            }
            GameObject* object = Internal::CanvasAccess::GetOwner(collider);
            State::BodyLink* body = ensureBody(object);
            if (body == nullptr)
            {
                return;
            }
            Internal::ObjectPose pose;
            Internal::CalculateObjectPose(canvas, object, pose);
            const std::uint64_t signature = ShapeSignature(collider, pose.scale);

            const InstanceId colliderId = collider.GetInstanceId();
            State::ShapeLink* link = state.shapes.Find(colliderId);
            // 틀린 외곽선이라 도형을 못 만든 연결(번호가 비어 있다)도 값이 그대로면 그대로 둔다 - 스텝마다 다시 분해하지 않는다.
            if (link != nullptr && link->signature == signature && link->object == object->GetInstanceId()
                && (world.IsValid(link->shape) || link->shape.index == Physics2D::InvalidIndex))
            {
                link->seen = true;
                return;
            }
            Physics2D::ShapeDef def;
            def.friction = collider.friction;
            def.restitution = collider.restitution;
            def.isTrigger = collider.isTrigger;
            def.layer = collider.layer;
            def.mask = collider.mask;
            def.userData = colliderId;

            // 같은 오브젝트의 살아 있는 도형이면 제자리에서 바꾼다. 스텝마다 지우고 만들면 크기를 움직이는 콜라이더가
            // 닿아 있는 동안 끝·시작 훅을 스텝마다 되풀이한다(physics-plan §4 의 4 (1)).
            if (link != nullptr && link->object == object->GetInstanceId() && world.IsValid(link->shape)
                && link->isTrigger == collider.isTrigger)
            {
                world.SetSurface(link->shape, def);
                if (false == Reshape(world, link->shape, collider, pose.scale, state.outline))
                {
                    // 틀린 외곽선이 되었다. 만들 때와 같이 도형이 없는 연결로 둔다 - 닿아 있던 쌍은 끝으로 나온다.
                    world.DestroyShape(link->shape);
                    link->shape = {};
                }
                link->signature = signature;
                link->seen = true;
                return;
            }
            if (link != nullptr)
            {
                world.DestroyShape(link->shape);
            }

            State::ShapeLink fresh;
            fresh.collider = colliderId;
            fresh.object = object->GetInstanceId();
            fresh.owner = object->GetScriptHandle();
            fresh.ownerObject = object->SafeFromThis();
            fresh.signature = signature;
            fresh.isTrigger = collider.isTrigger;
            fresh.seen = true;
            if (collider.shape == Component::ColliderShape2D::Circle)
            {
                fresh.shape = world.CreateCircleShape(body->body, BakeCircle(collider, pose.scale), def);
            }
            else if (collider.shape == Component::ColliderShape2D::Capsule)
            {
                const Physics2D::ConvexPolygon capsule = BakeCapsule(collider, pose.scale);
                fresh.shape = world.CreateCapsuleShape(
                    body->body, capsule.points[0], capsule.points[1], capsule.radius, def);
            }
            else
            {
                if (collider.shape == Component::ColliderShape2D::Box)
                {
                    Vec2 corners[4];
                    BakeBox(collider, pose.scale, corners);
                    state.outline.Clear();
                    state.outline.Append(corners, 4);
                }
                else
                {
                    BakeOutline(collider, pose.scale, state.outline);
                }
                world.CreatePolygonShape(body->body, state.outline.View(), def, fresh.shape);
            }

            if (link != nullptr)
            {
                *link = fresh;
            }
            else
            {
                state.shapes.TryAdd(colliderId, fresh);
            }
        });

        // ── 3. 사라진 것 ─────────────────────────────────────────────────────────────
        state.removals.Clear();
        for (const auto& entry : state.shapes)
        {
            if (false == entry.MappedValue.seen)
            {
                state.removals.Add(entry.KeyValue);
            }
        }
        for (const InstanceId id : state.removals)
        {
            State::ShapeLink* link = state.shapes.Find(id);
            world.DestroyShape(link->shape);
            state.retired.Add(*link);
            state.shapes.Remove(id);
            state.pieces.Remove(id);
        }
        state.removals.Clear();
        for (const auto& entry : state.bodies)
        {
            if (false == entry.MappedValue.seen)
            {
                state.removals.Add(entry.KeyValue);
            }
        }
        for (const InstanceId id : state.removals)
        {
            world.DestroyBody(state.bodies.Find(id)->body);
            state.bodies.Remove(id);
        }

        // ── 4. 스텝 ─────────────────────────────────────────────────────────────────
        world.Settings().gravity = m_gravity;
        world.Step(fixedDeltaTime);

        // ── 5. 되쓰기 ───────────────────────────────────────────────────────────────
        for (auto& entry : state.bodies)
        {
            State::BodyLink& link = entry.MappedValue;
            if (link.type == Physics2D::BodyType::Static || link.transform == nullptr || link.object == nullptr)
            {
                continue;
            }
            const Vec2 origin = world.GetPosition(link.body);
            const float angle = world.GetAngle(link.body);

            Vec2 localPosition = origin;
            float localRotation = angle;
            GameObject* parent = link.object->GetParent();
            Internal::ObjectPose parentPose;
            if (parent != nullptr && Internal::CalculateObjectPose(canvas, parent, parentPose))
            {
                // 월드 자리를 부모 로컬로 되돌린다(찌그러짐 없는 부모 행렬을 전제한다).
                const Matrix3x2& m = parentPose.matrix;
                const float determinant = m.m11 * m.m22 - m.m12 * m.m21;
                if (determinant != 0.0f)
                {
                    const float dx = origin.x - m.m31;
                    const float dy = origin.y - m.m32;
                    localPosition = {
                        (dx * m.m22 - dy * m.m21) / determinant,
                        (dy * m.m11 - dx * m.m12) / determinant };
                }
                localRotation = angle - parentPose.angle;
            }

            link.transform->position = localPosition;
            link.transform->rotation = localRotation;
            // 로컬을 움직였으니 월드 캐시는 이번 프레임의 Transform2DSystem 이 다시 채운다(D-47).
            link.transform->worldValid = false;
            link.writtenPosition = localPosition;
            link.writtenRotation = localRotation;
            if (link.rigidbodyComponent != nullptr)
            {
                link.rigidbodyComponent->linearVelocity = world.GetLinearVelocity(link.body);
                link.rigidbodyComponent->angularVelocity = world.GetAngularVelocity(link.body);
                link.writtenVelocity = link.rigidbodyComponent->linearVelocity;
                link.writtenAngularVelocity = link.rigidbodyComponent->angularVelocity;
            }
        }

        // ── 6. 이벤트 ───────────────────────────────────────────────────────────────
        DispatchEvents(canvas);
        state.retired.Clear();
    }

    void Physics2DSystem::DispatchEvents(Canvas& canvas)
    {
        State& state = *m_state;
        const ArrayView<const Physics2D::ContactEvent> begins = state.world.GetBeginEvents();
        const ArrayView<const Physics2D::ContactEvent> ends = state.world.GetEndEvents();
        if (begins.IsEmpty() && ends.IsEmpty())
        {
            return;
        }

        const std::uint64_t revision = canvas.GetScriptOrderRevision();
        if (revision != state.scriptRevision)
        {
            canvas.CollectScripts(state.collectedScripts);
            state.scriptKeys.Clear();
            for (GameScriptBase* script : state.collectedScripts)
            {
                if (script != nullptr)
                {
                    state.scriptKeys.Add(static_cast<const ComponentBase*>(script));
                }
            }
            std::sort(state.scriptKeys.begin(), state.scriptKeys.end());
            state.scriptRevision = revision;
        }

        // 콜라이더 아이디 → 오브젝트. 이번 스텝에 지운 콜라이더는 떠나보낸 연결에서 찾는다(끝 이벤트가 그것이다).
        const auto ownerOf = [&state](std::uint64_t colliderId) -> const State::ShapeLink*
        {
            if (const State::ShapeLink* live = state.shapes.Find(static_cast<InstanceId>(colliderId)))
            {
                return live;
            }
            for (const State::ShapeLink& old : state.retired)
            {
                if (old.collider == colliderId)
                {
                    return &old;
                }
            }
            return nullptr;
        };

        enum class Phase : std::uint8_t { Enter, Exit };

        // 훅이 오브젝트를 지워도 지나간 객체 위에서 다음 훅이 불리지 않게, 발송 내내 파괴를 미룬다(§8).
        Canvas::IterationGuard guard(canvas);
        const auto deliver = [&](GameObject* self, const Collision2D& hit, Phase phase, bool trigger)
        {
            if (self == nullptr || false == self->IsActiveInHierarchy())
            {
                return;
            }
            // 먼저 모은 뒤 부른다. 훅이 컴포넌트를 붙이거나 떼면 슬롯 배열이 흔들린다.
            state.hookTargets.Clear();
            for (const ComponentSlot& slot : self->GetComponents())
            {
                ComponentBase* component = slot.reference.TryGet();
                if (state.IsScript(component) && component->IsActiveComponent())
                {
                    // 2D 프로젝트의 스크립트는 모두 GameScript2D 다. 등록이 컴파일 시간에 그것을 막는다(D-207).
                    state.hookTargets.Add(static_cast<GameScript2D*>(static_cast<GameScriptBase*>(component)));
                }
            }
            for (GameScript2D* script : state.hookTargets)
            {
                if (trigger)
                {
                    if (phase == Phase::Enter)
                    {
                        script->OnTriggerEnter(hit);
                    }
                    else
                    {
                        script->OnTriggerExit(hit);
                    }
                }
                else if (phase == Phase::Enter)
                {
                    script->OnCollisionEnter(hit);
                }
                else
                {
                    script->OnCollisionExit(hit);
                }
            }
        };

        const auto dispatch = [&](const Physics2D::ContactEvent& event, Phase phase)
        {
            const State::ShapeLink* linkA = ownerOf(event.userDataA);
            const State::ShapeLink* linkB = ownerOf(event.userDataB);
            const GameObjectHandle handleA = linkA != nullptr ? linkA->owner : GameObjectHandle{};
            const GameObjectHandle handleB = linkB != nullptr ? linkB->owner : GameObjectHandle{};
            GameObject* objectA = linkA != nullptr ? linkA->ownerObject.TryGet() : nullptr;
            GameObject* objectB = linkB != nullptr ? linkB->ownerObject.TryGet() : nullptr;

            // 각 스크립트는 자기 쪽에서 본 법선(자기 → 상대)을 받는다.
            Collision2D forA;
            forA.other = handleB;
            forA.bodyType = Internal::GetBodyType(canvas, objectB);
            forA.point = event.point;
            forA.normal = event.normal;
            Collision2D forB;
            forB.other = handleA;
            forB.bodyType = Internal::GetBodyType(canvas, objectA);
            forB.point = event.point;
            forB.normal = { -event.normal.x, -event.normal.y };
            deliver(objectA, forA, phase, event.isTrigger);
            deliver(objectB, forB, phase, event.isTrigger);
        };

        for (const Physics2D::ContactEvent& event : ends)
        {
            dispatch(event, Phase::Exit);
        }
        for (const Physics2D::ContactEvent& event : begins)
        {
            dispatch(event, Phase::Enter);
        }
    }
}
