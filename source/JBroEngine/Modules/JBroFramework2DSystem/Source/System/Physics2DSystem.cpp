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

        void BakeOutline(const Component::Collider2D& collider, Vec2 scale, Array<Vec2>& outline)
        {
            outline.Clear();
            for (const Vec2& point : collider.points)
            {
                outline.Add(Bake(point, collider.offset, scale));
            }
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
                Physics2D::DecomposePolygon(outline.View(), cache->pieces);
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

    bool Physics2DSystem::Raycast(
        Vec2 origin,
        Vec2 direction,
        float distance,
        Collision2D& hit) const
    {
        hit = {};
        if (m_canvas == nullptr)
        {
            return false;
        }

        const float lengthSquared = direction.x * direction.x + direction.y * direction.y;
        if (distance < 0.0f || lengthSquared <= DirectionEpsilonSquared)
        {
            return false;
        }
        const float inverseLength = 1.0f / std::sqrt(lengthSquared);
        direction = { direction.x * inverseLength, direction.y * inverseLength };

        Canvas& canvas = *m_canvas;
        State& state = *m_state;
        float closest = std::numeric_limits<float>::max();
        canvas.ForEach<Component::Collider2D>([&](Component::Collider2D& collider)
        {
            if (false == collider.IsActiveComponent())
            {
                return;
            }
            GameObject* owner = Internal::CanvasAccess::GetOwner(collider);
            Internal::ObjectPose objectPose;
            if (owner == nullptr || false == Internal::CalculateObjectPose(canvas, owner, objectPose))
            {
                return;
            }
            const Physics2D::Pose pose = ToPose(objectPose);

            float candidate = 0.0f;
            Vec2 normal;
            bool intersects = false;
            if (collider.shape == Component::ColliderShape2D::Box)
            {
                Vec2 corners[4];
                BakeBox(collider, objectPose.scale, corners);
                intersects = Physics2D::RaycastPolygon(
                    BoxPolygon(corners), pose, origin, direction, distance, candidate, normal);
            }
            else if (collider.shape == Component::ColliderShape2D::Circle)
            {
                intersects = Physics2D::RaycastCircle(
                    BakeCircle(collider, objectPose.scale), pose, origin, direction, distance, candidate, normal);
            }
            else if (collider.shape == Component::ColliderShape2D::Polygon)
            {
                for (const Physics2D::ConvexPolygon& piece : state.PiecesFor(collider, objectPose.scale))
                {
                    float pieceDistance = 0.0f;
                    Vec2 pieceNormal;
                    if (Physics2D::RaycastPolygon(piece, pose, origin, direction, distance, pieceDistance, pieceNormal)
                        && (false == intersects || pieceDistance < candidate))
                    {
                        intersects = true;
                        candidate = pieceDistance;
                        normal = pieceNormal;
                    }
                }
            }

            if (false == intersects || candidate >= closest)
            {
                return;
            }
            closest = candidate;
            hit.other = owner->GetScriptHandle();
            hit.bodyType = Internal::GetBodyType(canvas, owner);
            hit.point = { origin.x + direction.x * candidate, origin.y + direction.y * candidate };
            hit.normal = normal;
        });
        return hit.other.GetInstanceId() != InvalidInstanceId;
    }

    void Physics2DSystem::OverlapBox(
        const Rect& area,
        Array<GameObjectHandle>& results) const
    {
        results.Clear();
        if (m_canvas == nullptr)
        {
            return;
        }

        Canvas& canvas = *m_canvas;
        State& state = *m_state;
        Physics2D::ConvexPolygon box;
        box.points[0] = { area.min.x, area.min.y };
        box.points[1] = { area.max.x, area.min.y };
        box.points[2] = { area.max.x, area.max.y };
        box.points[3] = { area.min.x, area.max.y };
        box.count = 4;
        const Physics2D::Pose identity;

        canvas.ForEach<Component::Collider2D>([&](Component::Collider2D& collider)
        {
            if (false == collider.IsActiveComponent())
            {
                return;
            }
            GameObject* owner = Internal::CanvasAccess::GetOwner(collider);
            Internal::ObjectPose objectPose;
            if (owner == nullptr || false == Internal::CalculateObjectPose(canvas, owner, objectPose))
            {
                return;
            }
            const Physics2D::Pose pose = ToPose(objectPose);

            bool overlaps = false;
            if (collider.shape == Component::ColliderShape2D::Box)
            {
                Vec2 corners[4];
                BakeBox(collider, objectPose.scale, corners);
                overlaps = Physics2D::OverlapPolygons(box, identity, BoxPolygon(corners), pose);
            }
            else if (collider.shape == Component::ColliderShape2D::Circle)
            {
                overlaps = Physics2D::OverlapPolygonAndCircle(
                    box, identity, BakeCircle(collider, objectPose.scale), pose);
            }
            else if (collider.shape == Component::ColliderShape2D::Polygon)
            {
                for (const Physics2D::ConvexPolygon& piece : state.PiecesFor(collider, objectPose.scale))
                {
                    if (Physics2D::OverlapPolygons(box, identity, piece, pose))
                    {
                        overlaps = true;
                        break;
                    }
                }
            }
            if (false == overlaps)
            {
                return;
            }

            const InstanceId ownerId = owner->GetInstanceId();
            for (const GameObjectHandle& existing : results)
            {
                if (existing.GetInstanceId() == ownerId)
                {
                    return;
                }
            }
            results.Add(owner->GetScriptHandle());
        });
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
            // 캡슐은 아직 커널에 없다(physics-plan §4 의 7). 충돌하지 않는다.
            if (false == collider.IsActiveComponent() || collider.shape == Component::ColliderShape2D::Capsule)
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
            if (link != nullptr)
            {
                world.DestroyShape(link->shape);
            }

            Physics2D::ShapeDef def;
            def.friction = collider.friction;
            def.restitution = collider.restitution;
            def.isTrigger = collider.isTrigger;
            def.layer = collider.layer;
            def.mask = collider.mask;
            def.userData = colliderId;

            State::ShapeLink fresh;
            fresh.collider = colliderId;
            fresh.object = object->GetInstanceId();
            fresh.owner = object->GetScriptHandle();
            fresh.ownerObject = object->SafeFromThis();
            fresh.signature = signature;
            fresh.seen = true;
            if (collider.shape == Component::ColliderShape2D::Circle)
            {
                fresh.shape = world.CreateCircleShape(body->body, BakeCircle(collider, pose.scale), def);
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
