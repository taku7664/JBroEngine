#include <JBro/Physics2D/World.h>

#include "VectorMath.h"
#include "WorkerPool.h"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

namespace JBro::Physics2D
{
    using namespace Internal;

    namespace
    {
        constexpr Float Pi = 3.14159265358979323846f;
        // Baumgarte 계수와 위치 보정 한 번의 상한. Box2D 와 같은 값이다.
        constexpr Float PositionCorrectionFactor = 0.2f;
        constexpr Float MaxLinearCorrection = 0.2f;
        // 한 서브스텝에 움직일 수 있는 거리와 각도. 넘으면 속도를 줄인다 - 폭주를 막는 안전망이지 판정이 아니다.
        constexpr Float MaxTranslationPerSubStep = 2.0f;
        constexpr Float MaxRotationPerSubStep = 0.25f * Pi;
        // 한 방향 발판이 막는 법선의 부채꼴(위에서 60° 안, cos 60°).
        constexpr Float OneWayCosine = 0.5f;

        Vector2 Tangent(Vector2 normal)
        {
            return { normal.y, -normal.x };
        }

        // 방향 d 로 본 역질량. 축을 고정하면 그 축의 성분이 빠진다(고정하지 않으면 inverseMass 와 같다).
        template <typename TBody>
        Float InverseMassAlong(const TBody& body, Vector2 d)
        {
            return d.x * d.x * body.inverseMassAxes.x + d.y * d.y * body.inverseMassAxes.y;
        }


        // 움직이는 몸만 쓴다. 정적·키네마틱 몸은 역질량이 0 이라 바뀔 것이 없고, 색 하나 안의 접촉 여럿이 같은 정적 몸을
        // 함께 쓰므로 쓰면 워커끼리 겹쳐 쓴다(D-234).
        template <typename TBody>
        void PushVelocity(TBody& a, TBody& b, Vector2 rA, Vector2 rB, Vector2 impulse)
        {
            if (a.type == BodyType::Dynamic)
            {
                a.linearVelocity = Subtract(a.linearVelocity, Multiply(impulse, a.inverseMassAxes));
                a.angularVelocity -= a.inverseInertia * Cross(rA, impulse);
            }
            if (b.type == BodyType::Dynamic)
            {
                b.linearVelocity = Add(b.linearVelocity, Multiply(impulse, b.inverseMassAxes));
                b.angularVelocity += b.inverseInertia * Cross(rB, impulse);
            }
        }

        template <typename TBody>
        void PushPosition(TBody& a, TBody& b, Vector2 rA, Vector2 rB, Vector2 impulse)
        {
            if (a.type == BodyType::Dynamic)
            {
                a.center = Subtract(a.center, Multiply(impulse, a.inverseMassAxes));
                a.angle -= a.inverseInertia * Cross(rA, impulse);
            }
            if (b.type == BodyType::Dynamic)
            {
                b.center = Add(b.center, Multiply(impulse, b.inverseMassAxes));
                b.angle += b.inverseInertia * Cross(rB, impulse);
            }
        }

        // 색 하나가 이보다 작으면 나누지 않는다. 워커를 깨우는 값이 푸는 값보다 크다.
        constexpr UInt32 MinParallelContactsPerColor = 32;
        constexpr UInt32 MinContactsPerChunk = 8;
        constexpr UInt32 OverflowColor = 64;
        // 풀 접촉이 이보다 적으면 색칠하지 않고 열쇠 순서 그대로 한 스레드에서 푼다. 색 순서는 쌓인 더미에서 수렴이 느리다
        // (10 층 더미가 5 초 안에 잠들지 못하고 옆으로 밀렸다) - 나눠 풀 이득이 있는 큰 장면에서만 쓴다. 워커 수가 아니라 접촉
        // 수로 정하므로 결과는 워커 수와 관계없이 같다.
        constexpr UInt32 ColoredSolveThreshold = 512;

        // 이보다 적은 후보는 나누지 않는다. 워커를 깨우고 모으는 비용이 판정보다 크다.
        constexpr UInt32 MinParallelCandidates = 64;
        constexpr UInt32 MinCandidatesPerChunk = 16;

        Bool IsLess(UInt32 a0, UInt32 a1, UInt32 a2, UInt32 a3,
            UInt32 b0, UInt32 b1, UInt32 b2, UInt32 b3)
        {
            if (a0 != b0)
            {
                return a0 < b0;
            }
            if (a1 != b1)
            {
                return a1 < b1;
            }
            if (a2 != b2)
            {
                return a2 < b2;
            }
            return a3 < b3;
        }
    }

    UInt32 RecommendWorkerCount(UInt32 work, UInt32 hardwareThreads)
    {
        // 1024 조각 아래는 나눗셈이 이미 0 이다(따로 거르던 조건은 뮤테이션으로 지워도 같아 뺐다).
        if (hardwareThreads <= 1)
        {
            return 0;
        }
        const UInt32 cap = JBro::Min(hardwareThreads - 1, 4u);
        return std::min(work / 1024, cap);
    }

    World::World()
    {
        // 월드에 거는 조인트의 상대다. 정적이고 역질량이 0 이라 풀이가 움직이지 못한다.
        m_ground.type = BodyType::Static;
        m_ground.alive = true;
    }

    World::~World() = default;

    void World::SetWorkerCount(UInt32 count)
    {
        count = std::min(count, MaxWorkerCount);
        if (count == 0)
        {
            m_workers.Reset();
            return;
        }
        if (m_workers.Get() == nullptr)
        {
            m_workers = MakeOwnerPtr<Internal::WorkerPool>();
        }
        m_workers->Start(count);
        if (m_workers->GetWorkerCount() == 0)
        {
            // 스레드가 없는 빌드다.
            m_workers.Reset();
        }
    }

    UInt32 World::GetWorkerCount() const
    {
        return m_workers.Get() != nullptr ? m_workers->GetWorkerCount() : UInt32(0);
    }

    StepStats World::GetLastStepStats() const
    {
        return m_lastStats;
    }

    WorldSettings& World::Settings()
    {
        return m_settings;
    }

    const WorldSettings& World::Settings() const
    {
        return m_settings;
    }

    World::Body* World::FindBody(BodyId body)
    {
        if (body.index >= m_bodies.Size())
        {
            return nullptr;
        }
        Body& found = m_bodies[body.index];
        return found.alive && found.generation == body.generation ? &found : nullptr;
    }

    const World::Body* World::FindBody(BodyId body) const
    {
        if (body.index >= m_bodies.Size())
        {
            return nullptr;
        }
        const Body& found = m_bodies[body.index];
        return found.alive && found.generation == body.generation ? &found : nullptr;
    }

    World::Shape* World::FindShape(ShapeId shape)
    {
        if (shape.index >= m_shapes.Size())
        {
            return nullptr;
        }
        Shape& found = m_shapes[shape.index];
        return found.alive && found.generation == shape.generation ? &found : nullptr;
    }

    const World::Shape* World::FindShape(ShapeId shape) const
    {
        if (shape.index >= m_shapes.Size())
        {
            return nullptr;
        }
        const Shape& found = m_shapes[shape.index];
        return found.alive && found.generation == shape.generation ? &found : nullptr;
    }

    BodyId World::CreateBody(const BodyDef& def)
    {
        UInt32 index = 0;
        if (false == m_freeBodies.IsEmpty())
        {
            index = m_freeBodies.Last();
            m_freeBodies.RemoveAt(m_freeBodies.Size() - 1);
        }
        else
        {
            index = static_cast<JBro::UInt32>(m_bodies.Size());
            m_bodies.Emplace();
        }

        Body& body = m_bodies[index];
        const UInt32 generation = body.generation;
        body = Body{};
        body.generation = generation;
        body.alive = true;
        body.type = def.type;
        body.fixedRotation = def.fixedRotation;
        body.freezePositionX = def.freezePositionX;
        body.freezePositionY = def.freezePositionY;
        body.canSleep = def.canSleep;
        body.origin = def.position;
        body.angle = def.angle;
        body.rotation = Rotation::FromAngle(def.angle);
        body.center = def.position;
        body.linearVelocity = def.linearVelocity;
        body.angularVelocity = def.angularVelocity;
        body.requestedMass = def.mass;
        body.gravityScale = def.gravityScale;
        body.linearDamping = def.linearDamping;
        body.angularDamping = def.angularDamping;
        body.userData = def.userData;
        UpdateMass(body);
        return { index, generation };
    }

    void World::DestroyBody(BodyId id)
    {
        Body* body = FindBody(id);
        if (body == nullptr)
        {
            return;
        }
        for (const UInt32 shapeIndex : body->shapes)
        {
            Shape& shape = m_shapes[shapeIndex];
            shape.alive = false;
            ++shape.generation;
            shape.pieces.Clear();
            m_freeShapes.Add(shapeIndex);
        }
        body->shapes.Clear();
        // 이 몸에 걸린 조인트는 함께 사라진다. 상대 몸은 깨운다(DestroyJoint 가 한다).
        for (UInt32 j = 0; j < m_joints.Size(); ++j)
        {
            const Joint& joint = m_joints[j];
            if (joint.alive && (joint.bodyA == id.index || joint.bodyB == id.index))
            {
                DestroyJoint({ j, joint.generation });
            }
        }
        body->alive = false;
        ++body->generation;
        m_freeBodies.Add(id.index);
    }

    Bool World::IsValid(BodyId body) const
    {
        return FindBody(body) != nullptr;
    }

    Bool World::IsValid(ShapeId shape) const
    {
        return FindShape(shape) != nullptr;
    }

    ShapeId World::AddShape(UInt32 bodyIndex, const ShapeDef& def)
    {
        UInt32 index = 0;
        if (false == m_freeShapes.IsEmpty())
        {
            index = m_freeShapes.Last();
            m_freeShapes.RemoveAt(m_freeShapes.Size() - 1);
        }
        else
        {
            index = static_cast<JBro::UInt32>(m_shapes.Size());
            m_shapes.Emplace();
        }

        Shape& shape = m_shapes[index];
        shape.alive = true;
        shape.isCircle = false;
        shape.isChain = false;
        shape.segments.Clear();
        shape.isTrigger = def.isTrigger;
        shape.body = bodyIndex;
        shape.pieces.Clear();
        shape.circle = Circle{};
        shape.friction = def.friction;
        shape.restitution = def.restitution;
        shape.layer = def.layer;
        shape.mask = def.mask;
        shape.oneWay = def.oneWay;
        shape.userData = def.userData;
        m_bodies[bodyIndex].shapes.Add(index);
        return { index, shape.generation };
    }

    PolygonError World::CreatePolygonShape(
        BodyId bodyId, ArrayView<const Vector2> localOutline, const ShapeDef& def, ShapeId& out)
    {
        out = {};
        Body* body = FindBody(bodyId);
        if (body == nullptr)
        {
            return PolygonError::TooFewPoints;
        }

        Array<ConvexPolygon> pieces;
        const PolygonError error = DecomposePolygon(localOutline, pieces);
        if (error != PolygonError::None)
        {
            return error;
        }

        out = AddShape(bodyId.index, def);
        m_shapes[out.index].pieces.Swap(pieces);
        UpdateMass(m_bodies[bodyId.index]);
        return PolygonError::None;
    }

    ShapeId World::CreateCircleShape(BodyId bodyId, const Circle& localCircle, const ShapeDef& def)
    {
        if (FindBody(bodyId) == nullptr || localCircle.radius <= 0.0f)
        {
            return {};
        }
        const ShapeId id = AddShape(bodyId.index, def);
        m_shapes[id.index].isCircle = true;
        m_shapes[id.index].circle = localCircle;
        UpdateMass(m_bodies[bodyId.index]);
        return id;
    }

    ShapeId World::CreateCapsuleShape(BodyId bodyId, Vector2 localA, Vector2 localB, Float radius, const ShapeDef& def)
    {
        if (FindBody(bodyId) == nullptr || radius <= 0.0f)
        {
            return {};
        }
        if (Length(Subtract(localB, localA)) <= LinearSlop)
        {
            Circle circle;
            circle.center = Scale(Add(localA, localB), 0.5f);
            circle.radius = radius;
            return CreateCircleShape(bodyId, circle, def);
        }
        ConvexPolygon capsule;
        capsule.points[0] = localA;
        capsule.points[1] = localB;
        capsule.count = 2;
        capsule.radius = radius;
        const ShapeId id = AddShape(bodyId.index, def);
        m_shapes[id.index].pieces.Add(capsule);
        UpdateMass(m_bodies[bodyId.index]);
        return id;
    }

    Bool World::BuildChain(ArrayView<const Vector2> points, Bool loop, Array<Vector2>& filtered, Array<ChainSegment>& out)
    {
        out.Clear();
        filtered.Clear();
        // 이웃과 LinearSlop 안인 점은 한 점으로 본다. 닫힌 체인은 끝점이 첫 점과 같으면 한 번만 센다.
        for (const Vector2& point : points)
        {
            if (false == filtered.IsEmpty()
                && LengthSquared(Subtract(point, filtered.Last())) <= LinearSlop * LinearSlop)
            {
                continue;
            }
            filtered.Add(point);
        }
        if (loop && filtered.Size() > 2
            && LengthSquared(Subtract(filtered.Last(), filtered[0])) <= LinearSlop * LinearSlop)
        {
            filtered.RemoveAt(filtered.Size() - 1);
        }
        const std::size_t kept = filtered.Size();
        if (kept < 2 || (loop && kept < 3))
        {
            filtered.Clear();
            return false;
        }
        const std::size_t segmentCount = loop ? kept : kept - 1;
        for (std::size_t s = 0; s < segmentCount; ++s)
        {
            ChainSegment& segment = out.Emplace();
            segment.p1 = filtered[s];
            segment.p2 = filtered[(s + 1) % kept];
            if (loop || s > 0)
            {
                segment.previous = filtered[(s + kept - 1) % kept];
                segment.hasPrevious = true;
            }
            if (loop || s + 2 < kept)
            {
                segment.next = filtered[(s + 2) % kept];
                segment.hasNext = true;
            }
        }
        return true;
    }

    ShapeId World::CreateChainShape(BodyId bodyId, ArrayView<const Vector2> localPoints, Bool loop, const ShapeDef& def)
    {
        if (FindBody(bodyId) == nullptr)
        {
            return {};
        }
        Array<ChainSegment> segments;
        if (false == BuildChain(localPoints, loop, m_scratchChainPoints, segments))
        {
            return {};
        }
        const ShapeId id = AddShape(bodyId.index, def);
        m_shapes[id.index].isChain = true;
        m_shapes[id.index].segments.Swap(segments);
        UpdateMass(m_bodies[bodyId.index]);
        return id;
    }

    Bool World::SetChainGeometry(ShapeId id, ArrayView<const Vector2> localPoints, Bool loop)
    {
        Shape* shape = FindShape(id);
        if (shape == nullptr || false == BuildChain(localPoints, loop, m_scratchChainPoints, m_scratchSegments))
        {
            return false;
        }
        shape->isCircle = false;
        shape->circle = Circle{};
        shape->pieces.Clear();
        shape->isChain = true;
        shape->segments.Swap(m_scratchSegments);
        UpdateMass(m_bodies[shape->body]);
        return true;
    }

    PolygonError World::SetPolygonGeometry(ShapeId id, ArrayView<const Vector2> localOutline)
    {
        Shape* shape = FindShape(id);
        if (shape == nullptr)
        {
            return PolygonError::TooFewPoints;
        }
        const PolygonError error = DecomposePolygon(localOutline, m_scratchPieces, m_decompose);
        if (error != PolygonError::None)
        {
            return error;
        }
        shape->isCircle = false;
        shape->isChain = false;
        shape->segments.Clear();
        shape->circle = Circle{};
        shape->pieces.Swap(m_scratchPieces);
        UpdateMass(m_bodies[shape->body]);
        return PolygonError::None;
    }

    Bool World::SetCircleGeometry(ShapeId id, const Circle& localCircle)
    {
        Shape* shape = FindShape(id);
        if (shape == nullptr || localCircle.radius <= 0.0f)
        {
            return false;
        }
        shape->isCircle = true;
        shape->isChain = false;
        shape->segments.Clear();
        shape->circle = localCircle;
        shape->pieces.Clear();
        UpdateMass(m_bodies[shape->body]);
        return true;
    }

    Bool World::SetCapsuleGeometry(ShapeId id, Vector2 localA, Vector2 localB, Float radius)
    {
        Shape* shape = FindShape(id);
        if (shape == nullptr || radius <= 0.0f)
        {
            return false;
        }
        if (Length(Subtract(localB, localA)) <= LinearSlop)
        {
            Circle circle;
            circle.center = Scale(Add(localA, localB), 0.5f);
            circle.radius = radius;
            return SetCircleGeometry(id, circle);
        }
        ConvexPolygon capsule;
        capsule.points[0] = localA;
        capsule.points[1] = localB;
        capsule.count = 2;
        capsule.radius = radius;
        shape->isCircle = false;
        shape->isChain = false;
        shape->segments.Clear();
        shape->circle = Circle{};
        shape->pieces.Clear();
        shape->pieces.Add(capsule);
        UpdateMass(m_bodies[shape->body]);
        return true;
    }

    void World::SetSurface(ShapeId id, const ShapeDef& def)
    {
        Shape* shape = FindShape(id);
        if (shape == nullptr)
        {
            return;
        }
        shape->friction = def.friction;
        shape->restitution = def.restitution;
        shape->layer = def.layer;
        shape->mask = def.mask;
        shape->oneWay = def.oneWay;
    }

    void World::DestroyShape(ShapeId id)
    {
        Shape* shape = FindShape(id);
        if (shape == nullptr)
        {
            return;
        }
        Body& body = m_bodies[shape->body];
        const std::size_t at = body.shapes.IndexOf(id.index);
        if (at < body.shapes.Size())
        {
            body.shapes.RemoveAt(at);
        }
        shape->alive = false;
        ++shape->generation;
        shape->pieces.Clear();
        shape->segments.Clear();
        m_freeShapes.Add(id.index);
        UpdateMass(body);
    }

    UInt32 World::GetChildCount(ShapeId id) const
    {
        const Shape* shape = FindShape(id);
        if (shape == nullptr)
        {
            return 0;
        }
        if (shape->isChain)
        {
            return static_cast<JBro::UInt32>(shape->segments.Size());
        }
        return shape->isCircle ? UInt32(1u) : static_cast<JBro::UInt32>(shape->pieces.Size());
    }

    const ChainSegment* World::GetChainChild(ShapeId id, UInt32 child) const
    {
        const Shape* shape = FindShape(id);
        if (shape == nullptr || false == shape->isChain || child >= shape->segments.Size())
        {
            return nullptr;
        }
        return &shape->segments[child];
    }

    const ConvexPolygon* World::GetPolygonChild(ShapeId id, UInt32 child) const
    {
        const Shape* shape = FindShape(id);
        if (shape == nullptr || shape->isCircle || child >= shape->pieces.Size())
        {
            return nullptr;
        }
        return &shape->pieces[child];
    }

    void World::SyncOrigin(Body& body)
    {
        body.rotation = Rotation::FromAngle(body.angle);
        body.origin = Subtract(body.center, RotateVector(body.rotation, body.localCenter));
    }

    void World::UpdateMass(Body& body)
    {
        body.mass = 0.0f;
        body.inverseMass = 0.0f;
        body.inertia = 0.0f;
        body.inverseInertia = 0.0f;
        body.localCenter = {};

        if (body.type == BodyType::Dynamic)
        {
            // 넓이 비례로 나누기 위해 밀도 1 로 모은 뒤 요청한 질량에 맞춘다.
            Array<MassData>& parts = m_massParts;
            parts.Clear();
            for (const UInt32 shapeIndex : body.shapes)
            {
                const Shape& shape = m_shapes[shapeIndex];
                if (shape.isTrigger)
                {
                    continue;
                }
                if (shape.isCircle)
                {
                    parts.Add(ComputeCircleMass(shape.circle.center, shape.circle.radius, 1.0f));
                    continue;
                }
                for (const ConvexPolygon& piece : shape.pieces)
                {
                    parts.Add(ComputePolygonMass(piece, 1.0f));
                }
            }
            const MassData combined = CombineMass(parts.View());
            body.mass = body.requestedMass > 0.0f ? body.requestedMass : Float(1.0f);
            body.inverseMass = 1.0f / body.mass;
            if (combined.mass > 0.0f)
            {
                body.localCenter = combined.center;
                body.inertia = combined.inertia * (body.mass / combined.mass);
            }
            // 도형이 없으면 돌지 않는다. 넓이가 없는 물체의 관성을 지어내지 않는다.
            if (body.inertia > 0.0f && false == body.fixedRotation)
            {
                body.inverseInertia = 1.0f / body.inertia;
            }
        }

        body.coreExtent = 0.0f;
        for (const UInt32 shapeIndex : body.shapes)
        {
            const Shape& shape = m_shapes[shapeIndex];
            if (shape.isTrigger || shape.isChain)
            {
                continue;
            }
            const auto keep = [&body](Float extent) {
                if (extent > 0.0f && (body.coreExtent <= 0.0f || extent < body.coreExtent))
                {
                    body.coreExtent = extent;
                }
            };
            if (shape.isCircle)
            {
                keep(shape.circle.radius);
                continue;
            }
            for (const ConvexPolygon& piece : shape.pieces)
            {
                Vector2 middle;
                for (UInt32 i = 0; i < piece.count; ++i)
                {
                    middle = Add(middle, piece.points[i]);
                }
                middle = Scale(middle, 1.0f / static_cast<JBro::Float>(piece.count));
                Float nearest = piece.count > 2 ? FLT_MAX : 0.0f;
                for (UInt32 i = 0; piece.count > 2 && i < piece.count; ++i)
                {
                    const Vector2 a = piece.points[i];
                    const Vector2 edge = Subtract(piece.points[(i + 1) % piece.count], a);
                    const Float length = Length(edge);
                    if (length > 0.0f)
                    {
                        nearest = std::fmin(nearest, std::fabs(Cross(edge, Subtract(middle, a))) / length);
                    }
                }
                keep((nearest == FLT_MAX ? Float(0.0f) : nearest) + piece.radius);
            }
        }

        WakeBody(body);
        body.inverseMassAxes = {
            body.freezePositionX ? Float(0.0f) : body.inverseMass,
            body.freezePositionY ? Float(0.0f) : body.inverseMass };

        // 원점은 그대로 두고 중심을 새 로컬 중심에 맞춘다. 도형을 붙이는 것이 물체를 옮기지 않는다.
        body.center = Add(body.origin, RotateVector(body.rotation, body.localCenter));
    }

    void World::SetTransform(BodyId id, Vector2 position, Float angle)
    {
        Body* body = FindBody(id);
        if (body == nullptr)
        {
            return;
        }
        body->origin = position;
        body->angle = angle;
        body->rotation = Rotation::FromAngle(angle);
        body->center = Add(position, RotateVector(body->rotation, body->localCenter));
        WakeBody(*body);
    }

    Vector2 World::GetPosition(BodyId id) const
    {
        const Body* body = FindBody(id);
        return body != nullptr ? body->origin : Vector2{};
    }

    Float World::GetAngle(BodyId id) const
    {
        const Body* body = FindBody(id);
        return body != nullptr ? body->angle : Float(0.0f);
    }

    Vector2 World::GetWorldCenter(BodyId id) const
    {
        const Body* body = FindBody(id);
        return body != nullptr ? body->center : Vector2{};
    }

    Vector2 World::GetLinearVelocity(BodyId id) const
    {
        const Body* body = FindBody(id);
        return body != nullptr ? body->linearVelocity : Vector2{};
    }

    void World::SetLinearVelocity(BodyId id, Vector2 velocity)
    {
        Body* body = FindBody(id);
        if (body != nullptr && body->type != BodyType::Static)
        {
            body->linearVelocity = {
                body->freezePositionX ? Float(0.0f) : velocity.x,
                body->freezePositionY ? Float(0.0f) : velocity.y };
            if (velocity.x != 0.0f || velocity.y != 0.0f)
            {
                WakeBody(*body);
            }
        }
    }

    Float World::GetAngularVelocity(BodyId id) const
    {
        const Body* body = FindBody(id);
        return body != nullptr ? body->angularVelocity : Float(0.0f);
    }

    void World::SetAngularVelocity(BodyId id, Float velocity)
    {
        Body* body = FindBody(id);
        if (body != nullptr && body->type != BodyType::Static && false == body->fixedRotation)
        {
            body->angularVelocity = velocity;
            if (velocity != 0.0f)
            {
                WakeBody(*body);
            }
        }
    }

    void World::SetBodyProperties(BodyId id, const BodyDef& def)
    {
        Body* body = FindBody(id);
        if (body == nullptr)
        {
            return;
        }
        body->requestedMass = def.mass;
        body->gravityScale = def.gravityScale;
        body->linearDamping = def.linearDamping;
        body->angularDamping = def.angularDamping;
        body->fixedRotation = def.fixedRotation;
        body->freezePositionX = def.freezePositionX;
        body->freezePositionY = def.freezePositionY;
        body->canSleep = def.canSleep;
        WakeBody(*body);
        if (body->fixedRotation)
        {
            body->angularVelocity = 0.0f;
        }
        body->linearVelocity = {
            body->freezePositionX ? Float(0.0f) : body->linearVelocity.x,
            body->freezePositionY ? Float(0.0f) : body->linearVelocity.y };
        UpdateMass(*body);
    }

    void World::ApplyForce(BodyId id, Vector2 force, Vector2 worldPoint)
    {
        Body* body = FindBody(id);
        if (body == nullptr || body->type != BodyType::Dynamic)
        {
            return;
        }
        WakeBody(*body);
        body->force = Add(body->force, force);
        body->torque += Cross(Subtract(worldPoint, body->center), force);
    }

    void World::ApplyForceToCenter(BodyId id, Vector2 force)
    {
        Body* body = FindBody(id);
        if (body == nullptr || body->type != BodyType::Dynamic)
        {
            return;
        }
        WakeBody(*body);
        body->force = Add(body->force, force);
    }

    void World::ApplyTorque(BodyId id, Float torque)
    {
        Body* body = FindBody(id);
        if (body == nullptr || body->type != BodyType::Dynamic)
        {
            return;
        }
        WakeBody(*body);
        body->torque += torque;
    }

    void World::ApplyLinearImpulse(BodyId id, Vector2 impulse, Vector2 worldPoint)
    {
        Body* body = FindBody(id);
        if (body == nullptr || body->type != BodyType::Dynamic)
        {
            return;
        }
        WakeBody(*body);
        body->linearVelocity = Add(body->linearVelocity, Multiply(impulse, body->inverseMassAxes));
        body->angularVelocity += body->inverseInertia * Cross(Subtract(worldPoint, body->center), impulse);
    }

    void World::ApplyLinearImpulseToCenter(BodyId id, Vector2 impulse)
    {
        Body* body = FindBody(id);
        if (body == nullptr || body->type != BodyType::Dynamic)
        {
            return;
        }
        WakeBody(*body);
        body->linearVelocity = Add(body->linearVelocity, Multiply(impulse, body->inverseMassAxes));
    }

    void World::ApplyAngularImpulse(BodyId id, Float impulse)
    {
        Body* body = FindBody(id);
        if (body == nullptr || body->type != BodyType::Dynamic)
        {
            return;
        }
        WakeBody(*body);
        body->angularVelocity += body->inverseInertia * impulse;
    }

    void World::WakeBody(Body& body)
    {
        body.awake = true;
        body.sleepTime = 0.0f;
    }

    void World::SetAwake(BodyId id, Bool awake)
    {
        Body* body = FindBody(id);
        if (body == nullptr || body->type == BodyType::Static)
        {
            return;
        }
        if (awake)
        {
            WakeBody(*body);
            return;
        }
        body->awake = false;
        body->linearVelocity = {};
        body->angularVelocity = 0.0f;
        body->sleepTime = m_settings.timeToSleep;
    }

    Bool World::IsAwake(BodyId id) const
    {
        const Body* body = FindBody(id);
        return body != nullptr && body->awake;
    }

    MassData World::GetMassData(BodyId id) const
    {
        MassData data;
        const Body* body = FindBody(id);
        if (body != nullptr)
        {
            data.mass = body->mass;
            data.center = body->localCenter;
            data.inertia = body->inertia;
        }
        return data;
    }

    ArrayView<const ContactEvent> World::GetBeginEvents() const
    {
        return m_beginEvents.View();
    }

    ArrayView<const ContactEvent> World::GetEndEvents() const
    {
        return m_endEvents.View();
    }

    ArrayView<const ContactEvent> World::GetStayEvents() const
    {
        return m_stayEvents.View();
    }

    void World::Step(Float deltaTime)
    {
        m_beginEvents.Clear();
        m_endEvents.Clear();
        m_stayEvents.Clear();
        m_lastStats = {};
        if (deltaTime <= 0.0f)
        {
            return;
        }

        // 중력이 바뀌면 잠든 몸도 새 중력을 받아야 한다.
        if (m_settings.gravity.x != m_lastGravity.x || m_settings.gravity.y != m_lastGravity.y)
        {
            for (Body& body : m_bodies)
            {
                if (body.alive)
                {
                    WakeBody(body);
                }
            }
            m_lastGravity = m_settings.gravity;
        }
        const UInt32 subSteps = std::max<UInt32>(1u, m_settings.subSteps);
        const Float h = deltaTime / static_cast<JBro::Float>(subSteps);
        for (UInt32 step = 0; step < subSteps; ++step)
        {
            IntegrateVelocities(h);
            Collide();
            ColorContacts();
            PrepareContacts();
            PrepareJoints(h);
            WarmStart();
            WarmStartJoints();
            for (UInt32 i = 0; i < m_settings.velocityIterations; ++i)
            {
                // 조인트를 먼저 푼다. 접촉이 나중에 풀려야 조인트가 몸을 벽 속으로 끌어들이지 못한다.
                SolveJoints(h);
                SolveVelocities(h);
            }
            ApplyRestitution();
            IntegratePositions(h);
            for (UInt32 i = 0; i < m_settings.positionIterations; ++i)
            {
                SolveJointPositions();
                SolvePositions();
            }
            for (Body& body : m_bodies)
            {
                if (body.alive && body.type != BodyType::Static && body.awake)
                {
                    SyncOrigin(body);
                }
            }
        }
        UpdateTouching();
        UpdateSleep(deltaTime);
        for (Body& body : m_bodies)
        {
            body.force = {};
            body.torque = 0.0f;
        }
    }

    Manifold World::ComputeManifold(const Candidate& candidate) const
    {
        const Shape& shapeA = m_shapes[candidate.shapeA];
        const Shape& shapeB = m_shapes[candidate.shapeB];
        const Body& bodyA = m_bodies[shapeA.body];
        const Body& bodyB = m_bodies[shapeB.body];
        const Pose poseA{ bodyA.origin, bodyA.rotation };
        const Pose poseB{ bodyB.origin, bodyB.rotation };
        if (shapeA.isChain)
        {
            const ChainSegment& segment = shapeA.segments[candidate.childA];
            return shapeB.isCircle
                ? CollideChainSegmentAndCircle(segment, poseA, shapeB.circle, poseB)
                : CollideChainSegmentAndPolygon(segment, poseA, shapeB.pieces[candidate.childB], poseB);
        }
        if (shapeA.isCircle)
        {
            return CollideCircles(shapeA.circle, poseA, shapeB.circle, poseB);
        }
        if (shapeB.isCircle)
        {
            return CollidePolygonAndCircle(shapeA.pieces[candidate.childA], poseA, shapeB.circle, poseB);
        }
        return CollidePolygons(shapeA.pieces[candidate.childA], poseA, shapeB.pieces[candidate.childB], poseB);
    }

    void World::CollideCandidates(void* context, UInt32 begin, UInt32 end)
    {
        World& world = *static_cast<World*>(context);
        for (UInt32 i = begin; i < end; ++i)
        {
            world.m_candidateManifolds[i] = world.ComputeManifold(world.m_candidates[i]);
        }
    }

    void World::IntegrateVelocities(Float h)
    {
        for (Body& body : m_bodies)
        {
            if (false == body.alive || body.type != BodyType::Dynamic || false == body.awake)
            {
                continue;
            }
            body.linearVelocity = Add(body.linearVelocity, Scale(m_settings.gravity, body.gravityScale * h));
            body.linearVelocity = Add(body.linearVelocity, Scale(Multiply(body.force, body.inverseMassAxes), h));
            body.angularVelocity += h * body.inverseInertia * body.torque;
            // 감쇠는 1 / (1 + h·c) 로 곱한다. 1 - h·c 와 달리 큰 값에서도 부호가 뒤집히지 않는다.
            body.linearVelocity = Scale(body.linearVelocity, 1.0f / (1.0f + h * body.linearDamping));
            body.angularVelocity *= 1.0f / (1.0f + h * body.angularDamping);
            if (body.fixedRotation)
            {
                body.angularVelocity = 0.0f;
            }
            // 고정한 축은 중력도 받지 않는다.
            if (body.freezePositionX)
            {
                body.linearVelocity.x = 0.0f;
            }
            if (body.freezePositionY)
            {
                body.linearVelocity.y = 0.0f;
            }
        }
    }

    void World::Collide()
    {
        m_previousContacts.Swap(m_contacts);
        m_contacts.Clear();
        m_proxies.Clear();
        m_proxyBounds.Clear();

        const Float margin = 0.5f * SpeculativeDistance;
        for (UInt32 shapeIndex = 0; shapeIndex < m_shapes.Size(); ++shapeIndex)
        {
            const Shape& shape = m_shapes[shapeIndex];
            if (false == shape.alive)
            {
                continue;
            }
            const Body& body = m_bodies[shape.body];
            const Pose pose{ body.origin, body.rotation };
            const UInt32 childCount = shape.isChain ? static_cast<JBro::UInt32>(shape.segments.Size())
                : shape.isCircle ? UInt32(1u) : static_cast<JBro::UInt32>(shape.pieces.Size());
            for (UInt32 child = 0; child < childCount; ++child)
            {
                Rect bounds;
                if (shape.isChain)
                {
                    const Vector2 a = TransformPoint(pose, shape.segments[child].p1);
                    const Vector2 b = TransformPoint(pose, shape.segments[child].p2);
                    bounds = UnionRect(MakeRectFromPoint(a), b);
                }
                else
                {
                    bounds = shape.isCircle
                        ? ComputeCircleBounds(shape.circle, pose)
                        : ComputePolygonBounds(shape.pieces[child], pose);
                }
                bounds = ExpandRect(bounds, margin);
                m_proxies.Add({ shapeIndex, child });
                m_proxyBounds.Add(bounds);
            }
        }

        m_broadPhase.FindPairs(m_proxyBounds.View(), m_pairs);

        // 1. 거르기(메인). 2. 좁은 판정(후보마다 따로 - 워커가 있으면 나눈다). 3. 접촉 모으기(메인, 후보 순서대로).
        // 2 가 제 칸에만 쓰고 3 이 순서를 지키므로 결과는 워커 수와 관계없이 같다.
        m_candidates.Clear();
        for (const ProxyPair& pair : m_pairs)
        {
            Proxy proxyA = m_proxies[pair.first];
            Proxy proxyB = m_proxies[pair.second];
            if (proxyA.shape == proxyB.shape)
            {
                continue;
            }
            const Shape* shapeA = &m_shapes[proxyA.shape];
            const Shape* shapeB = &m_shapes[proxyB.shape];
            if (shapeA->body == shapeB->body)
            {
                continue;
            }
            const Bool isTrigger = shapeA->isTrigger || shapeB->isTrigger;
            const BodyType typeA = m_bodies[shapeA->body].type;
            const BodyType typeB = m_bodies[shapeB->body].type;
            // 단단한 접촉은 한쪽이라도 움직이는 몸이어야 뜻이 있다. 트리거는 한쪽만 멈춰 있지 않으면 된다.
            if (isTrigger)
            {
                if (typeA == BodyType::Static && typeB == BodyType::Static)
                {
                    continue;
                }
            }
            else if (typeA != BodyType::Dynamic && typeB != BodyType::Dynamic)
            {
                continue;
            }
            if ((shapeA->layer & shapeB->mask) == 0u || (shapeB->layer & shapeA->mask) == 0u)
            {
                continue;
            }
            if (false == LayersMeet(shapeA->layer, shapeB->layer))
            {
                continue;
            }
            // collideConnected 가 거짓인 조인트로 이은 두 몸은 서로 부딪히지 않는다(D-233).
            if (false == m_jointFilters.IsEmpty() && m_jointFilters.Contains(JointPairKey(shapeA->body, shapeB->body)))
            {
                continue;
            }

            // 체인끼리는 부딪히지 않는다(둘 다 두께가 없다).
            if (shapeA->isChain && shapeB->isChain)
            {
                continue;
            }
            // A 는 체인 > 폴리곤 > 원 차례로 고른다: 체인 판정은 체인이, 폴리곤-원 판정은 폴리곤이 A 다.
            const Int32 rankA = shapeA->isChain ? 0 : (shapeA->isCircle ? 2 : 1);
            const Int32 rankB = shapeB->isChain ? 0 : (shapeB->isCircle ? 2 : 1);
            if (rankA > rankB)
            {
                std::swap(proxyA, proxyB);
            }
            Candidate& candidate = m_candidates.Emplace();
            candidate.shapeA = proxyA.shape;
            candidate.childA = proxyA.child;
            candidate.shapeB = proxyB.shape;
            candidate.childB = proxyB.child;
            candidate.isTrigger = isTrigger;
        }

        const UInt32 candidateCount = static_cast<JBro::UInt32>(m_candidates.Size());
        m_candidateManifolds.Resize(candidateCount);
        const UInt32 workers = GetWorkerCount();
        m_lastStats.candidates = candidateCount;
        if (workers == 0 || candidateCount < MinParallelCandidates)
        {
            CollideCandidates(this, 0, candidateCount);
        }
        else
        {
            ++m_lastStats.parallelSubSteps;
            // 한 스레드에 네 조각쯤 돌아가게 잘라, 판정이 무거운 쌍이 몰려도 남는 스레드가 나머지를 가져간다.
            const UInt32 grain = std::max(MinCandidatesPerChunk, candidateCount / (4 * (workers + 1)));
            m_workers->ParallelFor(candidateCount, grain, &World::CollideCandidates, this);
        }

        for (UInt32 i = 0; i < candidateCount; ++i)
        {
            const Manifold& manifold = m_candidateManifolds[i];
            if (manifold.count == 0)
            {
                continue;
            }
            const Candidate& candidate = m_candidates[i];
            const Shape& shapeA = m_shapes[candidate.shapeA];
            const Shape& shapeB = m_shapes[candidate.shapeB];
            Contact& contact = m_contacts.Emplace();
            contact.shapeA = candidate.shapeA;
            contact.childA = candidate.childA;
            contact.shapeB = candidate.shapeB;
            contact.childB = candidate.childB;
            contact.bodyA = shapeA.body;
            contact.bodyB = shapeB.body;
            contact.isTrigger = candidate.isTrigger;
            // Box2D 와 같은 섞기: 마찰은 기하 평균, 반발은 큰 쪽.
            contact.friction = std::sqrt(shapeA.friction * shapeB.friction);
            contact.restitution = std::fmax(shapeA.restitution, shapeB.restitution);
            contact.manifold = manifold;
        }

        // 깨어 있는 동적 몸이 잠든 몸에 닿으면 풀기 전에 깨운다 - 잠든 채 임펄스를 받으면 움직이지 않는 벽처럼 군다.
        for (const Contact& contact : m_contacts)
        {
            if (contact.isTrigger)
            {
                continue;
            }
            Body& a = m_bodies[contact.bodyA];
            Body& b = m_bodies[contact.bodyB];
            if (a.type == BodyType::Dynamic && b.type == BodyType::Dynamic && a.awake != b.awake)
            {
                WakeBody(a.awake ? b : a);
            }
        }

        const auto byKey = [](const Contact& left, const Contact& right)
        {
            return IsLess(left.shapeA, left.childA, left.shapeB, left.childB,
                right.shapeA, right.childA, right.shapeB, right.childB);
        };
        std::sort(m_contacts.begin(), m_contacts.end(), byKey);

        // 직전 서브스텝의 접촉과 열쇠로 맞춘다(둘 다 정렬돼 있어 한 번 훑으면 된다). 같은 점 번호면 누적 임펄스를
        // 이어받는다 - 워밍스타트가 끊기면 쌓인 상자가 매 스텝 처음부터 버텨야 해서 흔들린다.
        // 새로 생기거나 사라진 접촉에 잠든 몸이 끼면 깨운다(D-229) - 밑의 바닥을 옮기거나 지우거나, 잠든 몸 곁에 도형이 새로 생겼다.
        std::size_t previous = 0;
        for (Contact& contact : m_contacts)
        {
            while (previous < m_previousContacts.Size() && byKey(m_previousContacts[previous], contact))
            {
                WakeSleepingIn(m_previousContacts[previous]);
                ++previous;
            }
            if (previous >= m_previousContacts.Size() || byKey(contact, m_previousContacts[previous]))
            {
                contact.disabled = PassesOneWay(contact);
                WakeSleepingIn(contact);
                continue;
            }
            const Contact& old = m_previousContacts[previous];
            // 맞춘 것은 지나간다. 남겨 두면 다음 접촉이 그것을 "사라진 접촉" 으로 본다.
            ++previous;
            contact.disabled = old.disabled;
            for (UInt32 i = 0; i < contact.manifold.count; ++i)
            {
                for (UInt32 j = 0; j < old.manifold.count; ++j)
                {
                    if (old.manifold.points[j].id == contact.manifold.points[i].id)
                    {
                        contact.normalImpulse[i] = old.normalImpulse[j];
                        contact.tangentImpulse[i] = old.tangentImpulse[j];
                        break;
                    }
                }
            }
        }
        for (; previous < m_previousContacts.Size(); ++previous)
        {
            WakeSleepingIn(m_previousContacts[previous]);
        }
    }

    void World::WakeSleepingIn(const Contact& contact)
    {
        if (contact.isTrigger || contact.disabled)
        {
            return;
        }
        Body& a = m_bodies[contact.bodyA];
        Body& b = m_bodies[contact.bodyB];
        if (a.alive && a.type == BodyType::Dynamic && false == a.awake)
        {
            WakeBody(a);
        }
        if (b.alive && b.type == BodyType::Dynamic && false == b.awake)
        {
            WakeBody(b);
        }
    }

    void World::PrepareContacts()
    {
        for (Body& body : m_bodies)
        {
            body.startCenter = body.center;
            body.startAngle = body.angle;
        }

        for (Contact& contact : m_contacts)
        {
            if (false == IsSolved(contact))
            {
                continue;
            }
            const Body& a = m_bodies[contact.bodyA];
            const Body& b = m_bodies[contact.bodyB];
            const Vector2 normal = contact.manifold.normal;
            const Vector2 tangent = Tangent(normal);
            for (UInt32 i = 0; i < contact.manifold.count; ++i)
            {
                const ManifoldPoint& point = contact.manifold.points[i];
                const Vector2 rA = Subtract(point.point, a.center);
                const Vector2 rB = Subtract(point.point, b.center);
                contact.anchorA[i] = rA;
                contact.anchorB[i] = rB;
                // 현재 깊이 = dot(중심 변위 + 돌아간 팔의 차, n) + base. 시작에서는 manifold 의 깊이와 같다.
                contact.baseSeparation[i] = point.separation - Dot(Subtract(rB, rA), normal);

                const Float rnA = Cross(rA, normal);
                const Float rnB = Cross(rB, normal);
                const Float normalK = InverseMassAlong(a, normal) + InverseMassAlong(b, normal)
                    + a.inverseInertia * rnA * rnA + b.inverseInertia * rnB * rnB;
                contact.normalMass[i] = normalK > 0.0f ? 1.0f / normalK : Float(0.0f);

                const Float rtA = Cross(rA, tangent);
                const Float rtB = Cross(rB, tangent);
                const Float tangentK = InverseMassAlong(a, tangent) + InverseMassAlong(b, tangent)
                    + a.inverseInertia * rtA * rtA + b.inverseInertia * rtB * rtB;
                contact.tangentMass[i] = tangentK > 0.0f ? 1.0f / tangentK : Float(0.0f);

                const Vector2 relative = Subtract(
                    Add(b.linearVelocity, Cross(b.angularVelocity, rB)),
                    Add(a.linearVelocity, Cross(a.angularVelocity, rA)));
                contact.approachSpeed[i] = Dot(relative, normal);
            }
        }
    }

    void World::WarmStart()
    {
        ForEachColor(&World::WarmStartJob, true);
    }

    void World::WarmStartContact(Contact& contact)
    {
        {
            Body& a = m_bodies[contact.bodyA];
            Body& b = m_bodies[contact.bodyB];
            const Vector2 normal = contact.manifold.normal;
            const Vector2 tangent = Tangent(normal);
            for (UInt32 i = 0; i < contact.manifold.count; ++i)
            {
                const Vector2 impulse = Add(
                    Scale(normal, contact.normalImpulse[i]), Scale(tangent, contact.tangentImpulse[i]));
                PushVelocity(a, b, contact.anchorA[i], contact.anchorB[i], impulse);
            }
        }
    }

    void World::SolveVelocities(Float h)
    {
        m_solveInverseH = 1.0f / h;
        ForEachColor(&World::SolveVelocityJob, true);
    }

    void World::SolveContactVelocity(Contact& contact, Float inverseH)
    {
        {
            Body& a = m_bodies[contact.bodyA];
            Body& b = m_bodies[contact.bodyB];
            const Vector2 normal = contact.manifold.normal;
            const Vector2 tangent = Tangent(normal);

            // 마찰을 먼저 푼다. 한계는 지금까지 쌓인 법선 임펄스의 μ 배다.
            for (UInt32 i = 0; i < contact.manifold.count; ++i)
            {
                const Vector2 rA = contact.anchorA[i];
                const Vector2 rB = contact.anchorB[i];
                const Vector2 relative = Subtract(
                    Add(b.linearVelocity, Cross(b.angularVelocity, rB)),
                    Add(a.linearVelocity, Cross(a.angularVelocity, rA)));
                const Float speed = Dot(relative, tangent);
                const Float limit = contact.friction * contact.normalImpulse[i];
                const Float old = contact.tangentImpulse[i];
                const Float next = JBro::Clamp(old - contact.tangentMass[i] * speed, -limit, limit);
                const Vector2 impulse = Scale(tangent, next - old);
                contact.tangentImpulse[i] = next;
                PushVelocity(a, b, rA, rB, impulse);
            }

            for (UInt32 i = 0; i < contact.manifold.count; ++i)
            {
                const Vector2 rA = contact.anchorA[i];
                const Vector2 rB = contact.anchorB[i];
                const Vector2 relative = Subtract(
                    Add(b.linearVelocity, Cross(b.angularVelocity, rB)),
                    Add(a.linearVelocity, Cross(a.angularVelocity, rA)));
                const Float speed = Dot(relative, normal);
                // 아직 떨어져 있으면 그 틈만큼은 이 서브스텝에 다가와도 된다(미리 만든 접촉). 박힌 것은 여기서 밀어내지
                // 않는다 - 속도로 밀면 튀어 오르는 에너지가 생긴다. 위치 보정이 따로 뺀다.
                const Float separation = contact.manifold.points[i].separation;
                const Float bias = separation > 0.0f ? separation * inverseH : Float(0.0f);
                const Float old = contact.normalImpulse[i];
                const Float next = std::fmax(old - contact.normalMass[i] * (speed + bias), 0.0f);
                const Vector2 impulse = Scale(normal, next - old);
                contact.normalImpulse[i] = next;
                PushVelocity(a, b, rA, rB, impulse);
            }
        }
    }

    void World::ApplyRestitution()
    {
        ForEachColor(&World::RestitutionJob, true);
    }

    void World::ApplyContactRestitution(Contact& contact)
    {
        if (contact.restitution <= 0.0f)
        {
            return;
        }
        {
            Body& a = m_bodies[contact.bodyA];
            Body& b = m_bodies[contact.bodyB];
            const Vector2 normal = contact.manifold.normal;
            for (UInt32 i = 0; i < contact.manifold.count; ++i)
            {
                // 빠르게 다가왔고 실제로 밀어낸 점만 튕긴다.
                if (contact.approachSpeed[i] > -m_settings.restitutionThreshold || contact.normalImpulse[i] <= 0.0f)
                {
                    continue;
                }
                const Vector2 rA = contact.anchorA[i];
                const Vector2 rB = contact.anchorB[i];
                const Vector2 relative = Subtract(
                    Add(b.linearVelocity, Cross(b.angularVelocity, rB)),
                    Add(a.linearVelocity, Cross(a.angularVelocity, rA)));
                const Float speed = Dot(relative, normal);
                const Float old = contact.normalImpulse[i];
                const Float next = std::fmax(
                    old - contact.normalMass[i] * (speed + contact.restitution * contact.approachSpeed[i]), 0.0f);
                const Vector2 impulse = Scale(normal, next - old);
                contact.normalImpulse[i] = next;
                PushVelocity(a, b, rA, rB, impulse);
            }
        }
    }

    void World::ColorContacts()
    {
        const UInt32 contactCount = static_cast<JBro::UInt32>(m_contacts.Size());
        m_bodyColors.Resize(m_bodies.Size());
        for (UInt64& used : m_bodyColors)
        {
            used = 0u;
        }
        m_contactColors.Resize(contactCount);
        UInt32 counts[OverflowColor + 1] = {};
        UInt32 solved = 0;
        for (const Contact& contact : m_contacts)
        {
            solved += IsSolved(contact) ? 1u : 0u;
        }
        const Bool colored = solved >= ColoredSolveThreshold;
        for (UInt32 i = 0; i < contactCount; ++i)
        {
            const Contact& contact = m_contacts[i];
            if (false == IsSolved(contact))
            {
                m_contactColors[i] = 0xFFu;
                continue;
            }
            if (false == colored)
            {
                m_contactColors[i] = static_cast<std::uint8_t>(OverflowColor);
                ++counts[OverflowColor];
                continue;
            }
            const Bool dynamicA = m_bodies[contact.bodyA].type == BodyType::Dynamic;
            const Bool dynamicB = m_bodies[contact.bodyB].type == BodyType::Dynamic;
            UInt64 used = 0u;
            if (dynamicA)
            {
                used |= m_bodyColors[contact.bodyA];
            }
            if (dynamicB)
            {
                used |= m_bodyColors[contact.bodyB];
            }
            const UInt32 color = used == ~0ull ? OverflowColor : UInt32(static_cast<JBro::UInt32>(std::countr_one(used.Get())));
            if (color < OverflowColor)
            {
                const UInt64 bit = 1ull << color;
                if (dynamicA)
                {
                    m_bodyColors[contact.bodyA] |= bit;
                }
                if (dynamicB)
                {
                    m_bodyColors[contact.bodyB] |= bit;
                }
            }
            m_contactColors[i] = static_cast<std::uint8_t>(color);
            ++counts[color];
        }
        m_colorStarts[0] = 0;
        for (UInt32 color = 0; color <= OverflowColor; ++color)
        {
            m_colorStarts[color + 1] = m_colorStarts[color] + counts[color];
        }
        m_colorOrder.Resize(m_colorStarts[OverflowColor + 1]);
        UInt32 cursor[OverflowColor + 1];
        for (UInt32 color = 0; color <= OverflowColor; ++color)
        {
            cursor[color] = m_colorStarts[color];
        }
        // 색 안에서는 접촉의 열쇠 순서를 지킨다 - 같은 입력이면 늘 같은 순서다.
        for (UInt32 i = 0; i < contactCount; ++i)
        {
            const std::uint8_t color = m_contactColors[i];
            if (color != 0xFFu)
            {
                m_colorOrder[cursor[color]++] = i;
            }
        }
    }

    void World::ForEachColor(ContactJob job, Bool allowParallel)
    {
        const UInt32 workers = GetWorkerCount();
        for (UInt32 color = 0; color <= OverflowColor; ++color)
        {
            const UInt32 begin = m_colorStarts[color];
            const UInt32 count = m_colorStarts[color + 1] - begin;
            if (count == 0)
            {
                continue;
            }
            m_colorOffset = begin;
            if (allowParallel && workers > 0 && color < OverflowColor && count >= MinParallelContactsPerColor)
            {
                ++m_lastStats.parallelColors;
                const UInt32 grain = std::max(MinContactsPerChunk, count / (4 * (workers + 1)));
                m_workers->ParallelFor(count, grain, job, this);
            }
            else
            {
                job(this, 0, count);
            }
        }
    }

    void World::WarmStartJob(void* context, UInt32 begin, UInt32 end)
    {
        World& world = *static_cast<World*>(context);
        for (UInt32 i = begin; i < end; ++i)
        {
            world.WarmStartContact(world.m_contacts[world.m_colorOrder[world.m_colorOffset + i]]);
        }
    }

    void World::SolveVelocityJob(void* context, UInt32 begin, UInt32 end)
    {
        World& world = *static_cast<World*>(context);
        for (UInt32 i = begin; i < end; ++i)
        {
            world.SolveContactVelocity(world.m_contacts[world.m_colorOrder[world.m_colorOffset + i]], world.m_solveInverseH);
        }
    }

    void World::RestitutionJob(void* context, UInt32 begin, UInt32 end)
    {
        World& world = *static_cast<World*>(context);
        for (UInt32 i = begin; i < end; ++i)
        {
            world.ApplyContactRestitution(world.m_contacts[world.m_colorOrder[world.m_colorOffset + i]]);
        }
    }

    void World::SolvePositionJob(void* context, UInt32 begin, UInt32 end)
    {
        World& world = *static_cast<World*>(context);
        for (UInt32 i = begin; i < end; ++i)
        {
            world.SolveContactPosition(world.m_contacts[world.m_colorOrder[world.m_colorOffset + i]]);
        }
    }

    void World::IntegratePositions(Float h)
    {
        for (Body& body : m_bodies)
        {
            if (false == body.alive || body.type == BodyType::Static || false == body.awake)
            {
                continue;
            }
            const Float translation = Length(body.linearVelocity) * h;
            if (translation > MaxTranslationPerSubStep)
            {
                body.linearVelocity = Scale(body.linearVelocity, MaxTranslationPerSubStep / translation);
            }
            const Float rotation = std::fabs(body.angularVelocity) * h;
            if (rotation > MaxRotationPerSubStep)
            {
                body.angularVelocity *= MaxRotationPerSubStep / rotation;
            }
            const Vector2 startCenter = body.center;
            body.center = Add(body.center, Scale(body.linearVelocity, h));
            body.angle += body.angularVelocity * h;
            // 이 서브스텝에 자기 두께의 절반보다 멀리 가는 동적 몸만 이어서 본다. 그보다 느리면 미리 만든 접촉이 잡는다.
            if (body.type == BodyType::Dynamic && body.coreExtent > 0.0f && Length(body.linearVelocity) * h > 0.5f * body.coreExtent)
            {
                ClampToFirstHit(body, static_cast<JBro::UInt32>(&body - m_bodies.Data()), startCenter);
            }
        }
    }

    void World::ClampToFirstHit(Body& body, UInt32 bodyIndex, Vector2 startCenter)
    {
        const Vector2 move = Subtract(body.center, startCenter);
        const Float length = Length(move);
        if (length <= LinearSlop)
        {
            return;
        }
        const Vector2 direction = Scale(move, 1.0f / length);
        // 도형은 돌지 않는다고 보고 끝 각도로 민다. 도는 몸의 모서리는 다음 서브스텝의 접촉이 맡는다.
        const Rotation rotation = Rotation::FromAngle(body.angle);
        const Pose start{ Subtract(startCenter, RotateVector(rotation, body.localCenter)), rotation };
        const Pose end{ Subtract(body.center, RotateVector(rotation, body.localCenter)), rotation };
        Float best = length;
        Bool hit = false;
        for (const UInt32 ownIndex : body.shapes)
        {
            const Shape& own = m_shapes[ownIndex];
            if (own.isTrigger || own.isChain)
            {
                continue;
            }
            const UInt32 ownPieces = own.isCircle ? UInt32(1u) : static_cast<JBro::UInt32>(own.pieces.Size());
            for (UInt32 p = 0; p < ownPieces; ++p)
            {
                Rect swept = own.isCircle ? ComputeCircleBounds(own.circle, start) : ComputePolygonBounds(own.pieces[p], start);
                const Rect finish = own.isCircle ? ComputeCircleBounds(own.circle, end) : ComputePolygonBounds(own.pieces[p], end);
                swept.min = { std::fmin(swept.min.x, finish.min.x), std::fmin(swept.min.y, finish.min.y) };
                swept.max = { std::fmax(swept.max.x, finish.max.x), std::fmax(swept.max.y, finish.max.y) };
                for (UInt32 targetIndex = 0; targetIndex < m_shapes.Size(); ++targetIndex)
                {
                    const Shape& target = m_shapes[targetIndex];
                    if (false == target.alive || target.isTrigger || target.oneWay || target.body == bodyIndex)
                    {
                        continue;
                    }
                    const Body& other = m_bodies[target.body];
                    if (other.type == BodyType::Dynamic)
                    {
                        continue;
                    }
                    if ((own.layer & target.mask) == 0u || (target.layer & own.mask) == 0u || false == LayersMeet(own.layer, target.layer))
                    {
                        continue;
                    }
                    const Pose targetPose{ other.origin, other.rotation };
                    const UInt32 children = target.isCircle ? UInt32(1u)
                        : static_cast<JBro::UInt32>(target.isChain ? UInt32(target.segments.Size()) : UInt32(target.pieces.Size()));
                    for (UInt32 c = 0; c < children; ++c)
                    {
                        ConvexPolygon segment;
                        const ConvexPolygon* piece = nullptr;
                        if (target.isChain)
                        {
                            segment.points[0] = target.segments[c].p1;
                            segment.points[1] = target.segments[c].p2;
                            segment.count = 2;
                            piece = &segment;
                        }
                        else if (false == target.isCircle)
                        {
                            piece = &target.pieces[c];
                        }
                        const Rect box = piece != nullptr ? ComputePolygonBounds(*piece, targetPose) : ComputeCircleBounds(target.circle, targetPose);
                        if (false == box.Intersects(swept))
                        {
                            continue;
                        }
                        Float distance = 0.0f;
                        Vector2 normal;
                        Bool found = false;
                        if (own.isCircle)
                        {
                            const Vector2 center = TransformPoint(start, own.circle.center);
                            found = piece != nullptr
                                ? CastCircle(center, own.circle.radius, direction, best, *piece, targetPose, distance, normal)
                                : CastCircle(center, own.circle.radius, direction, best, target.circle, targetPose, distance, normal);
                        }
                        else
                        {
                            found = piece != nullptr
                                ? CastPolygon(own.pieces[p], start, direction, best, *piece, targetPose, distance, normal)
                                : CastPolygon(own.pieces[p], start, direction, best, target.circle, targetPose, distance, normal);
                        }
                        // 출발부터 닿아 있던 것(거리 0)은 이미 접촉이 맡고 있다. 그것으로 멈추면 바닥 위를 미끄러지는 몸이 서 버린다.
                        if (found && distance > 0.0f && distance < best)
                        {
                            best = distance;
                            hit = true;
                        }
                    }
                }
            }
        }
        if (false == hit)
        {
            return;
        }
        // 닿는 자리에서 LinearSlop 만큼 앞에 세운다. 다음 서브스텝의 미리 만든 접촉이 거기서 받는다.
        body.center = Add(startCenter, Scale(direction, std::fmax(best - LinearSlop, 0.0f)));
        ++m_lastStats.continuousHits;
    }

    void World::SolvePositions()
    {
        ForEachColor(&World::SolvePositionJob, true);
    }

    void World::SolveContactPosition(const Contact& contact)
    {
        {
            Body& a = m_bodies[contact.bodyA];
            Body& b = m_bodies[contact.bodyB];
            const Vector2 normal = contact.manifold.normal;
            for (UInt32 i = 0; i < contact.manifold.count; ++i)
            {
                // 서브스텝 시작 뒤로 돈 만큼 팔을 돌린다.
                const Vector2 rA = RotateVector(Rotation::FromAngle(a.angle - a.startAngle), contact.anchorA[i]);
                const Vector2 rB = RotateVector(Rotation::FromAngle(b.angle - b.startAngle), contact.anchorB[i]);
                const Vector2 moved = Subtract(Subtract(b.center, b.startCenter), Subtract(a.center, a.startCenter));
                const Float separation = Dot(Add(moved, Subtract(rB, rA)), normal) + contact.baseSeparation[i];

                // LinearSlop 만큼은 박힌 채 둔다. 0 으로 맞추면 닿았다 떨어졌다 하며 접촉이 끊긴다.
                const Float correction = JBro::Clamp(
                    PositionCorrectionFactor * (separation + LinearSlop), -MaxLinearCorrection, 0.0f);
                if (correction >= 0.0f)
                {
                    continue;
                }
                const Float rnA = Cross(rA, normal);
                const Float rnB = Cross(rB, normal);
                const Float k = InverseMassAlong(a, normal) + InverseMassAlong(b, normal)
                    + a.inverseInertia * rnA * rnA + b.inverseInertia * rnB * rnB;
                if (k <= 0.0f)
                {
                    continue;
                }
                const Vector2 impulse = Scale(normal, -correction / k);
                PushPosition(a, b, rA, rB, impulse);
            }
        }
    }

    Bool World::LayersMeet(UInt32 layerA, UInt32 layerB) const
    {
        for (UInt32 bits = layerA; bits != 0u; bits &= bits - 1u)
        {
            const UInt32 i = static_cast<JBro::UInt32>(std::countr_zero(bits.Get()));
            if ((layerB & ~m_settings.ignoredLayers[i]) != 0u)
            {
                return true;
            }
        }
        return false;
    }

    Bool World::PassesOneWay(const Contact& contact) const
    {
        if (contact.isTrigger)
        {
            return false;
        }
        // 법선은 A → B 다. 발판에서 상대로 향하게 돌려 발판의 위와 잰다.
        const auto passes = [this, &contact](UInt32 shapeIndex, Bool isA)
        {
            const Shape& shape = m_shapes[shapeIndex];
            if (false == shape.oneWay)
            {
                return false;
            }
            const Vector2 up = RotateVector(m_bodies[shape.body].rotation, Vector2{ 0.0f, 1.0f });
            const Vector2 outward = isA ? contact.manifold.normal : Scale(contact.manifold.normal, -1.0f);
            return Dot(outward, up) < OneWayCosine;
        };
        return passes(contact.shapeA, true) || passes(contact.shapeB, false);
    }

    Bool World::IsSolved(const Contact& contact) const
    {
        if (contact.disabled)
        {
            return false;
        }
        if (contact.isTrigger)
        {
            return false;
        }
        const Body& a = m_bodies[contact.bodyA];
        const Body& b = m_bodies[contact.bodyB];
        return (a.type == BodyType::Dynamic && a.awake) || (b.type == BodyType::Dynamic && b.awake);
    }

    UInt32 World::FindIsland(UInt32 body)
    {
        UInt32 root = body;
        while (m_islandParent[root] != root)
        {
            root = m_islandParent[root];
        }
        // 길 줄이기.
        while (m_islandParent[body] != root)
        {
            const UInt32 next = m_islandParent[body];
            m_islandParent[body] = root;
            body = next;
        }
        return root;
    }

    void World::UpdateSleep(Float deltaTime)
    {
        const UInt32 count = static_cast<JBro::UInt32>(m_bodies.Size());
        m_islandParent.Resize(count);
        m_islandSleepTime.Resize(count);
        for (UInt32 i = 0; i < count; ++i)
        {
            m_islandParent[i] = i;
            m_islandSleepTime[i] = FLT_MAX;
        }

        // 1. 몸마다 느린 시간을 잰다. 잠들 수 없는 몸과 움직이는 키네마틱은 0 이다.
        for (Body& body : m_bodies)
        {
            if (false == body.alive || body.type == BodyType::Static)
            {
                continue;
            }
            const Bool slow = LengthSquared(body.linearVelocity)
                    <= m_settings.linearSleepTolerance * m_settings.linearSleepTolerance
                && std::fabs(body.angularVelocity) <= m_settings.angularSleepTolerance;
            if (false == m_settings.enableSleep || false == body.canSleep || false == slow)
            {
                body.sleepTime = 0.0f;
            }
            else if (body.awake)
            {
                body.sleepTime += deltaTime;
            }
        }

        // 2. 단단한 접촉으로 이어진 움직이는 몸들을 한 섬으로 묶는다. 멈춘 몸은 섬을 잇지 않는다.
        for (const Contact& contact : m_contacts)
        {
            if (contact.isTrigger || contact.disabled || contact.manifold.count == 0)
            {
                continue;
            }
            const Body& a = m_bodies[contact.bodyA];
            const Body& b = m_bodies[contact.bodyB];
            if (a.type == BodyType::Static || b.type == BodyType::Static)
            {
                continue;
            }
            const UInt32 rootA = FindIsland(contact.bodyA);
            const UInt32 rootB = FindIsland(contact.bodyB);
            if (rootA != rootB)
            {
                m_islandParent[std::max(rootA, rootB)] = std::min(rootA, rootB);
            }
        }

        // 조인트로 이은 움직이는 몸도 한 섬이다(D-233) - 매달린 몸 하나만 잠들면 조인트가 잠든 몸을 끌지 못한다.
        for (const Joint& joint : m_joints)
        {
            if (false == joint.alive || joint.bodyB == InvalidIndex)
            {
                continue;
            }
            if (m_bodies[joint.bodyA].type == BodyType::Static || m_bodies[joint.bodyB].type == BodyType::Static)
            {
                continue;
            }
            const UInt32 rootA = FindIsland(joint.bodyA);
            const UInt32 rootB = FindIsland(joint.bodyB);
            if (rootA != rootB)
            {
                m_islandParent[std::max(rootA, rootB)] = std::min(rootA, rootB);
            }
        }

        // 3. 섬마다 가장 짧은 느린 시간. 그것이 timeToSleep 에 이르면 섬이 잠들고, 모자라면 섬이 모두 깬다.
        for (UInt32 i = 0; i < count; ++i)
        {
            const Body& body = m_bodies[i];
            if (false == body.alive || body.type == BodyType::Static)
            {
                continue;
            }
            const UInt32 root = FindIsland(i);
            m_islandSleepTime[root] = std::fmin(m_islandSleepTime[root], body.sleepTime);
        }
        m_lastStats.awakeBodies = 0;
        m_lastStats.sleepingBodies = 0;
        for (UInt32 i = 0; i < count; ++i)
        {
            Body& body = m_bodies[i];
            if (false == body.alive || body.type == BodyType::Static)
            {
                continue;
            }
            const Bool asleep = m_islandSleepTime[FindIsland(i)] >= m_settings.timeToSleep;
            if (asleep)
            {
                body.awake = false;
                body.linearVelocity = {};
                body.angularVelocity = 0.0f;
            }
            else
            {
                body.awake = true;
            }
            if (body.type == BodyType::Dynamic)
            {
                if (body.awake)
                {
                    ++m_lastStats.awakeBodies;
                }
                else
                {
                    ++m_lastStats.sleepingBodies;
                }
            }
        }
    }

    void World::UpdateTouching()
    {
        m_previousTouching.Swap(m_touching);
        m_touching.Clear();

        for (const Contact& contact : m_contacts)
        {
            // 한 방향 발판이 흘려보내는 접촉은 닿은 것이 아니다.
            if (contact.disabled)
            {
                continue;
            }
            UInt32 deepestIndex = 0;
            for (UInt32 i = 1; i < contact.manifold.count; ++i)
            {
                if (contact.manifold.points[i].separation < contact.manifold.points[deepestIndex].separation)
                {
                    deepestIndex = i;
                }
            }
            const Float deepest = contact.manifold.points[deepestIndex].separation;
            // 트리거는 실제로 겹쳐야, 단단한 접촉은 위치 보정이 남기는 LinearSlop 안이면 닿은 것이다.
            const Float limit = contact.isTrigger ? Float(0.0f) : LinearSlop;
            if (deepest >= limit)
            {
                continue;
            }
            TouchingPair pair;
            pair.shapeA = std::min(contact.shapeA, contact.shapeB);
            pair.shapeB = std::max(contact.shapeA, contact.shapeB);
            pair.generationA = m_shapes[pair.shapeA].generation;
            pair.generationB = m_shapes[pair.shapeB].generation;
            pair.userDataA = m_shapes[pair.shapeA].userData;
            pair.userDataB = m_shapes[pair.shapeB].userData;
            pair.isTrigger = contact.isTrigger;
            pair.depth = deepest;
            if (false == contact.isTrigger)
            {
                pair.point = contact.manifold.points[deepestIndex].point;
                // 쌍은 도형 번호 순으로 적으므로, 번호가 뒤집혔으면 법선도 뒤집어 A→B 를 지킨다.
                const Bool swapped = pair.shapeA != contact.shapeA;
                pair.normal = swapped
                    ? Vector2{ -contact.manifold.normal.x, -contact.manifold.normal.y }
                    : contact.manifold.normal;
            }
            m_touching.Add(pair);
        }

        const auto byShapes = [](const TouchingPair& left, const TouchingPair& right)
        {
            if (left.shapeA != right.shapeA)
            {
                return left.shapeA < right.shapeA;
            }
            if (left.shapeB != right.shapeB)
            {
                return left.shapeB < right.shapeB;
            }
            if (left.generationA != right.generationA)
            {
                return left.generationA < right.generationA;
            }
            return left.generationB < right.generationB;
        };
        std::sort(m_touching.begin(), m_touching.end(), byShapes);
        // 조각 여럿이 같은 도형 쌍에 닿으면 하나로 줄인다.
        std::size_t unique = 0;
        for (std::size_t i = 0; i < m_touching.Size(); ++i)
        {
            if (unique > 0 && false == byShapes(m_touching[unique - 1], m_touching[i]))
            {
                // 같은 쌍의 다른 조각이다. 대표 접촉은 가장 깊은 것으로 남긴다.
                if (m_touching[i].depth < m_touching[unique - 1].depth)
                {
                    m_touching[unique - 1].depth = m_touching[i].depth;
                    m_touching[unique - 1].point = m_touching[i].point;
                    m_touching[unique - 1].normal = m_touching[i].normal;
                }
                continue;
            }
            m_touching[unique] = m_touching[i];
            ++unique;
        }
        m_touching.Resize(unique);

        const auto toEvent = [](const TouchingPair& pair)
        {
            ContactEvent event;
            event.shapeA = { pair.shapeA, pair.generationA };
            event.shapeB = { pair.shapeB, pair.generationB };
            event.userDataA = pair.userDataA;
            event.userDataB = pair.userDataB;
            event.isTrigger = pair.isTrigger;
            event.point = pair.point;
            event.normal = pair.normal;
            return event;
        };

        // 이어지는 쌍은 한쪽이라도 깨어 움직이는 몸이 있을 때만 알린다.
        const auto moving = [this](const TouchingPair& pair)
        {
            const Body& a = m_bodies[m_shapes[pair.shapeA].body];
            const Body& b = m_bodies[m_shapes[pair.shapeB].body];
            return (a.type != BodyType::Static && a.awake) || (b.type != BodyType::Static && b.awake);
        };

        // 두 정렬된 목록을 맞대어 새로 생긴 것은 시작, 사라진 것은 끝, 둘 다 있으면 이어짐이다. 지워진 도형은 generation 이 달라
        // 새 목록에 같은 열쇠로 나오지 않으므로 끝으로 잡힌다.
        std::size_t i = 0;
        std::size_t j = 0;
        while (i < m_touching.Size() || j < m_previousTouching.Size())
        {
            if (j >= m_previousTouching.Size()
                || (i < m_touching.Size() && byShapes(m_touching[i], m_previousTouching[j])))
            {
                m_beginEvents.Add(toEvent(m_touching[i]));
                ++i;
            }
            else if (i >= m_touching.Size() || byShapes(m_previousTouching[j], m_touching[i]))
            {
                ContactEvent ended = toEvent(m_previousTouching[j]);
                ended.point = {};
                ended.normal = {};
                m_endEvents.Add(ended);
                ++j;
            }
            else
            {
                if (moving(m_touching[i]))
                {
                    m_stayEvents.Add(toEvent(m_touching[i]));
                }
                ++i;
                ++j;
            }
        }
    }
}
