#include <JBro/Physics2D/World.h>

#include "VectorMath.h"

#include <algorithm>
#include <cmath>

namespace JBro::Physics2D
{
    using namespace Internal;

    namespace
    {
        constexpr float Pi = 3.14159265358979323846f;
        // Baumgarte 계수와 위치 보정 한 번의 상한. Box2D 와 같은 값이다.
        constexpr float PositionCorrectionFactor = 0.2f;
        constexpr float MaxLinearCorrection = 0.2f;
        // 한 서브스텝에 움직일 수 있는 거리와 각도. 넘으면 속도를 줄인다 - 폭주를 막는 안전망이지 판정이 아니다.
        constexpr float MaxTranslationPerSubStep = 2.0f;
        constexpr float MaxRotationPerSubStep = 0.25f * Pi;

        Vec2 Tangent(Vec2 normal)
        {
            return { normal.y, -normal.x };
        }

        bool IsLess(std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t a3,
            std::uint32_t b0, std::uint32_t b1, std::uint32_t b2, std::uint32_t b3)
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
        std::uint32_t index = 0;
        if (false == m_freeBodies.IsEmpty())
        {
            index = m_freeBodies.Last();
            m_freeBodies.RemoveAt(m_freeBodies.Size() - 1);
        }
        else
        {
            index = static_cast<std::uint32_t>(m_bodies.Size());
            m_bodies.Emplace();
        }

        Body& body = m_bodies[index];
        const std::uint32_t generation = body.generation;
        body = Body{};
        body.generation = generation;
        body.alive = true;
        body.type = def.type;
        body.fixedRotation = def.fixedRotation;
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
        for (const std::uint32_t shapeIndex : body->shapes)
        {
            Shape& shape = m_shapes[shapeIndex];
            shape.alive = false;
            ++shape.generation;
            shape.pieces.Clear();
            m_freeShapes.Add(shapeIndex);
        }
        body->shapes.Clear();
        body->alive = false;
        ++body->generation;
        m_freeBodies.Add(id.index);
    }

    bool World::IsValid(BodyId body) const
    {
        return FindBody(body) != nullptr;
    }

    bool World::IsValid(ShapeId shape) const
    {
        return FindShape(shape) != nullptr;
    }

    ShapeId World::AddShape(std::uint32_t bodyIndex, const ShapeDef& def)
    {
        std::uint32_t index = 0;
        if (false == m_freeShapes.IsEmpty())
        {
            index = m_freeShapes.Last();
            m_freeShapes.RemoveAt(m_freeShapes.Size() - 1);
        }
        else
        {
            index = static_cast<std::uint32_t>(m_shapes.Size());
            m_shapes.Emplace();
        }

        Shape& shape = m_shapes[index];
        shape.alive = true;
        shape.isCircle = false;
        shape.isTrigger = def.isTrigger;
        shape.body = bodyIndex;
        shape.pieces.Clear();
        shape.circle = Circle{};
        shape.friction = def.friction;
        shape.restitution = def.restitution;
        shape.layer = def.layer;
        shape.mask = def.mask;
        shape.userData = def.userData;
        m_bodies[bodyIndex].shapes.Add(index);
        return { index, shape.generation };
    }

    PolygonError World::CreatePolygonShape(
        BodyId bodyId, ArrayView<const Vec2> localOutline, const ShapeDef& def, ShapeId& out)
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
        m_freeShapes.Add(id.index);
        UpdateMass(body);
    }

    std::uint32_t World::GetChildCount(ShapeId id) const
    {
        const Shape* shape = FindShape(id);
        if (shape == nullptr)
        {
            return 0;
        }
        return shape->isCircle ? 1u : static_cast<std::uint32_t>(shape->pieces.Size());
    }

    const ConvexPolygon* World::GetPolygonChild(ShapeId id, std::uint32_t child) const
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
            Array<MassData> parts;
            for (const std::uint32_t shapeIndex : body.shapes)
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
            body.mass = body.requestedMass > 0.0f ? body.requestedMass : 1.0f;
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

        // 원점은 그대로 두고 중심을 새 로컬 중심에 맞춘다. 도형을 붙이는 것이 물체를 옮기지 않는다.
        body.center = Add(body.origin, RotateVector(body.rotation, body.localCenter));
    }

    void World::SetTransform(BodyId id, Vec2 position, float angle)
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
    }

    Vec2 World::GetPosition(BodyId id) const
    {
        const Body* body = FindBody(id);
        return body != nullptr ? body->origin : Vec2{};
    }

    float World::GetAngle(BodyId id) const
    {
        const Body* body = FindBody(id);
        return body != nullptr ? body->angle : 0.0f;
    }

    Vec2 World::GetWorldCenter(BodyId id) const
    {
        const Body* body = FindBody(id);
        return body != nullptr ? body->center : Vec2{};
    }

    Vec2 World::GetLinearVelocity(BodyId id) const
    {
        const Body* body = FindBody(id);
        return body != nullptr ? body->linearVelocity : Vec2{};
    }

    void World::SetLinearVelocity(BodyId id, Vec2 velocity)
    {
        Body* body = FindBody(id);
        if (body != nullptr && body->type != BodyType::Static)
        {
            body->linearVelocity = velocity;
        }
    }

    float World::GetAngularVelocity(BodyId id) const
    {
        const Body* body = FindBody(id);
        return body != nullptr ? body->angularVelocity : 0.0f;
    }

    void World::SetAngularVelocity(BodyId id, float velocity)
    {
        Body* body = FindBody(id);
        if (body != nullptr && body->type != BodyType::Static && false == body->fixedRotation)
        {
            body->angularVelocity = velocity;
        }
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

    void World::Step(float deltaTime)
    {
        m_beginEvents.Clear();
        m_endEvents.Clear();
        if (deltaTime <= 0.0f)
        {
            return;
        }

        const std::uint32_t subSteps = std::max<std::uint32_t>(1u, m_settings.subSteps);
        const float h = deltaTime / static_cast<float>(subSteps);
        for (std::uint32_t step = 0; step < subSteps; ++step)
        {
            IntegrateVelocities(h);
            Collide();
            PrepareContacts();
            WarmStart();
            for (std::uint32_t i = 0; i < m_settings.velocityIterations; ++i)
            {
                SolveVelocities(h);
            }
            ApplyRestitution();
            IntegratePositions(h);
            for (std::uint32_t i = 0; i < m_settings.positionIterations; ++i)
            {
                SolvePositions();
            }
            for (Body& body : m_bodies)
            {
                if (body.alive && body.type != BodyType::Static)
                {
                    SyncOrigin(body);
                }
            }
        }
        UpdateTouching();
    }

    void World::IntegrateVelocities(float h)
    {
        for (Body& body : m_bodies)
        {
            if (false == body.alive || body.type != BodyType::Dynamic)
            {
                continue;
            }
            body.linearVelocity = Add(body.linearVelocity, Scale(m_settings.gravity, body.gravityScale * h));
            // 감쇠는 1 / (1 + h·c) 로 곱한다. 1 - h·c 와 달리 큰 값에서도 부호가 뒤집히지 않는다.
            body.linearVelocity = Scale(body.linearVelocity, 1.0f / (1.0f + h * body.linearDamping));
            body.angularVelocity *= 1.0f / (1.0f + h * body.angularDamping);
            if (body.fixedRotation)
            {
                body.angularVelocity = 0.0f;
            }
        }
    }

    void World::Collide()
    {
        m_previousContacts.Swap(m_contacts);
        m_contacts.Clear();
        m_proxies.Clear();
        m_proxyBounds.Clear();

        const float margin = 0.5f * SpeculativeDistance;
        for (std::uint32_t shapeIndex = 0; shapeIndex < m_shapes.Size(); ++shapeIndex)
        {
            const Shape& shape = m_shapes[shapeIndex];
            if (false == shape.alive)
            {
                continue;
            }
            const Body& body = m_bodies[shape.body];
            const Pose pose{ body.origin, body.rotation };
            const std::uint32_t childCount = shape.isCircle ? 1u : static_cast<std::uint32_t>(shape.pieces.Size());
            for (std::uint32_t child = 0; child < childCount; ++child)
            {
                Rect bounds = shape.isCircle
                    ? ComputeCircleBounds(shape.circle, pose)
                    : ComputePolygonBounds(shape.pieces[child], pose);
                bounds.min = { bounds.min.x - margin, bounds.min.y - margin };
                bounds.max = { bounds.max.x + margin, bounds.max.y + margin };
                m_proxies.Add({ shapeIndex, child });
                m_proxyBounds.Add(bounds);
            }
        }

        m_broadPhase.FindPairs(m_proxyBounds.View(), m_pairs);

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
            const bool isTrigger = shapeA->isTrigger || shapeB->isTrigger;
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

            // 폴리곤-원 판정은 폴리곤이 A 다.
            if (shapeA->isCircle && false == shapeB->isCircle)
            {
                std::swap(proxyA, proxyB);
                std::swap(shapeA, shapeB);
            }

            const Body& bodyA = m_bodies[shapeA->body];
            const Body& bodyB = m_bodies[shapeB->body];
            const Pose poseA{ bodyA.origin, bodyA.rotation };
            const Pose poseB{ bodyB.origin, bodyB.rotation };
            Manifold manifold;
            if (shapeA->isCircle)
            {
                manifold = CollideCircles(shapeA->circle, poseA, shapeB->circle, poseB);
            }
            else if (shapeB->isCircle)
            {
                manifold = CollidePolygonAndCircle(shapeA->pieces[proxyA.child], poseA, shapeB->circle, poseB);
            }
            else
            {
                manifold = CollidePolygons(
                    shapeA->pieces[proxyA.child], poseA, shapeB->pieces[proxyB.child], poseB);
            }
            if (manifold.count == 0)
            {
                continue;
            }

            Contact& contact = m_contacts.Emplace();
            contact.shapeA = proxyA.shape;
            contact.childA = proxyA.child;
            contact.shapeB = proxyB.shape;
            contact.childB = proxyB.child;
            contact.bodyA = shapeA->body;
            contact.bodyB = shapeB->body;
            contact.isTrigger = isTrigger;
            // Box2D 와 같은 섞기: 마찰은 기하 평균, 반발은 큰 쪽.
            contact.friction = std::sqrt(shapeA->friction * shapeB->friction);
            contact.restitution = std::fmax(shapeA->restitution, shapeB->restitution);
            contact.manifold = manifold;
        }

        const auto byKey = [](const Contact& left, const Contact& right)
        {
            return IsLess(left.shapeA, left.childA, left.shapeB, left.childB,
                right.shapeA, right.childA, right.shapeB, right.childB);
        };
        std::sort(m_contacts.begin(), m_contacts.end(), byKey);

        // 직전 서브스텝의 접촉과 열쇠로 맞춘다(둘 다 정렬돼 있어 한 번 훑으면 된다). 같은 점 번호면 누적 임펄스를
        // 이어받는다 - 워밍스타트가 끊기면 쌓인 상자가 매 스텝 처음부터 버텨야 해서 흔들린다.
        std::size_t previous = 0;
        for (Contact& contact : m_contacts)
        {
            while (previous < m_previousContacts.Size() && byKey(m_previousContacts[previous], contact))
            {
                ++previous;
            }
            if (previous >= m_previousContacts.Size() || byKey(contact, m_previousContacts[previous]))
            {
                continue;
            }
            const Contact& old = m_previousContacts[previous];
            for (std::uint32_t i = 0; i < contact.manifold.count; ++i)
            {
                for (std::uint32_t j = 0; j < old.manifold.count; ++j)
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
            if (contact.isTrigger)
            {
                continue;
            }
            const Body& a = m_bodies[contact.bodyA];
            const Body& b = m_bodies[contact.bodyB];
            const Vec2 normal = contact.manifold.normal;
            const Vec2 tangent = Tangent(normal);
            for (std::uint32_t i = 0; i < contact.manifold.count; ++i)
            {
                const ManifoldPoint& point = contact.manifold.points[i];
                const Vec2 rA = Subtract(point.point, a.center);
                const Vec2 rB = Subtract(point.point, b.center);
                contact.anchorA[i] = rA;
                contact.anchorB[i] = rB;
                // 현재 깊이 = dot(중심 변위 + 돌아간 팔의 차, n) + base. 시작에서는 manifold 의 깊이와 같다.
                contact.baseSeparation[i] = point.separation - Dot(Subtract(rB, rA), normal);

                const float rnA = Cross(rA, normal);
                const float rnB = Cross(rB, normal);
                const float normalK = a.inverseMass + b.inverseMass
                    + a.inverseInertia * rnA * rnA + b.inverseInertia * rnB * rnB;
                contact.normalMass[i] = normalK > 0.0f ? 1.0f / normalK : 0.0f;

                const float rtA = Cross(rA, tangent);
                const float rtB = Cross(rB, tangent);
                const float tangentK = a.inverseMass + b.inverseMass
                    + a.inverseInertia * rtA * rtA + b.inverseInertia * rtB * rtB;
                contact.tangentMass[i] = tangentK > 0.0f ? 1.0f / tangentK : 0.0f;

                const Vec2 relative = Subtract(
                    Add(b.linearVelocity, Cross(b.angularVelocity, rB)),
                    Add(a.linearVelocity, Cross(a.angularVelocity, rA)));
                contact.approachSpeed[i] = Dot(relative, normal);
            }
        }
    }

    void World::WarmStart()
    {
        for (Contact& contact : m_contacts)
        {
            if (contact.isTrigger)
            {
                continue;
            }
            Body& a = m_bodies[contact.bodyA];
            Body& b = m_bodies[contact.bodyB];
            const Vec2 normal = contact.manifold.normal;
            const Vec2 tangent = Tangent(normal);
            for (std::uint32_t i = 0; i < contact.manifold.count; ++i)
            {
                const Vec2 impulse = Add(
                    Scale(normal, contact.normalImpulse[i]), Scale(tangent, contact.tangentImpulse[i]));
                a.linearVelocity = Subtract(a.linearVelocity, Scale(impulse, a.inverseMass));
                a.angularVelocity -= a.inverseInertia * Cross(contact.anchorA[i], impulse);
                b.linearVelocity = Add(b.linearVelocity, Scale(impulse, b.inverseMass));
                b.angularVelocity += b.inverseInertia * Cross(contact.anchorB[i], impulse);
            }
        }
    }

    void World::SolveVelocities(float h)
    {
        const float inverseH = 1.0f / h;
        for (Contact& contact : m_contacts)
        {
            if (contact.isTrigger)
            {
                continue;
            }
            Body& a = m_bodies[contact.bodyA];
            Body& b = m_bodies[contact.bodyB];
            const Vec2 normal = contact.manifold.normal;
            const Vec2 tangent = Tangent(normal);

            // 마찰을 먼저 푼다. 한계는 지금까지 쌓인 법선 임펄스의 μ 배다.
            for (std::uint32_t i = 0; i < contact.manifold.count; ++i)
            {
                const Vec2 rA = contact.anchorA[i];
                const Vec2 rB = contact.anchorB[i];
                const Vec2 relative = Subtract(
                    Add(b.linearVelocity, Cross(b.angularVelocity, rB)),
                    Add(a.linearVelocity, Cross(a.angularVelocity, rA)));
                const float speed = Dot(relative, tangent);
                const float limit = contact.friction * contact.normalImpulse[i];
                const float old = contact.tangentImpulse[i];
                const float next = std::clamp(old - contact.tangentMass[i] * speed, -limit, limit);
                const Vec2 impulse = Scale(tangent, next - old);
                contact.tangentImpulse[i] = next;
                a.linearVelocity = Subtract(a.linearVelocity, Scale(impulse, a.inverseMass));
                a.angularVelocity -= a.inverseInertia * Cross(rA, impulse);
                b.linearVelocity = Add(b.linearVelocity, Scale(impulse, b.inverseMass));
                b.angularVelocity += b.inverseInertia * Cross(rB, impulse);
            }

            for (std::uint32_t i = 0; i < contact.manifold.count; ++i)
            {
                const Vec2 rA = contact.anchorA[i];
                const Vec2 rB = contact.anchorB[i];
                const Vec2 relative = Subtract(
                    Add(b.linearVelocity, Cross(b.angularVelocity, rB)),
                    Add(a.linearVelocity, Cross(a.angularVelocity, rA)));
                const float speed = Dot(relative, normal);
                // 아직 떨어져 있으면 그 틈만큼은 이 서브스텝에 다가와도 된다(미리 만든 접촉). 박힌 것은 여기서 밀어내지
                // 않는다 - 속도로 밀면 튀어 오르는 에너지가 생긴다. 위치 보정이 따로 뺀다.
                const float separation = contact.manifold.points[i].separation;
                const float bias = separation > 0.0f ? separation * inverseH : 0.0f;
                const float old = contact.normalImpulse[i];
                const float next = std::fmax(old - contact.normalMass[i] * (speed + bias), 0.0f);
                const Vec2 impulse = Scale(normal, next - old);
                contact.normalImpulse[i] = next;
                a.linearVelocity = Subtract(a.linearVelocity, Scale(impulse, a.inverseMass));
                a.angularVelocity -= a.inverseInertia * Cross(rA, impulse);
                b.linearVelocity = Add(b.linearVelocity, Scale(impulse, b.inverseMass));
                b.angularVelocity += b.inverseInertia * Cross(rB, impulse);
            }
        }
    }

    void World::ApplyRestitution()
    {
        for (Contact& contact : m_contacts)
        {
            if (contact.isTrigger || contact.restitution <= 0.0f)
            {
                continue;
            }
            Body& a = m_bodies[contact.bodyA];
            Body& b = m_bodies[contact.bodyB];
            const Vec2 normal = contact.manifold.normal;
            for (std::uint32_t i = 0; i < contact.manifold.count; ++i)
            {
                // 빠르게 다가왔고 실제로 밀어낸 점만 튕긴다.
                if (contact.approachSpeed[i] > -m_settings.restitutionThreshold || contact.normalImpulse[i] <= 0.0f)
                {
                    continue;
                }
                const Vec2 rA = contact.anchorA[i];
                const Vec2 rB = contact.anchorB[i];
                const Vec2 relative = Subtract(
                    Add(b.linearVelocity, Cross(b.angularVelocity, rB)),
                    Add(a.linearVelocity, Cross(a.angularVelocity, rA)));
                const float speed = Dot(relative, normal);
                const float old = contact.normalImpulse[i];
                const float next = std::fmax(
                    old - contact.normalMass[i] * (speed + contact.restitution * contact.approachSpeed[i]), 0.0f);
                const Vec2 impulse = Scale(normal, next - old);
                contact.normalImpulse[i] = next;
                a.linearVelocity = Subtract(a.linearVelocity, Scale(impulse, a.inverseMass));
                a.angularVelocity -= a.inverseInertia * Cross(rA, impulse);
                b.linearVelocity = Add(b.linearVelocity, Scale(impulse, b.inverseMass));
                b.angularVelocity += b.inverseInertia * Cross(rB, impulse);
            }
        }
    }

    void World::IntegratePositions(float h)
    {
        for (Body& body : m_bodies)
        {
            if (false == body.alive || body.type == BodyType::Static)
            {
                continue;
            }
            const float translation = Length(body.linearVelocity) * h;
            if (translation > MaxTranslationPerSubStep)
            {
                body.linearVelocity = Scale(body.linearVelocity, MaxTranslationPerSubStep / translation);
            }
            const float rotation = std::fabs(body.angularVelocity) * h;
            if (rotation > MaxRotationPerSubStep)
            {
                body.angularVelocity *= MaxRotationPerSubStep / rotation;
            }
            body.center = Add(body.center, Scale(body.linearVelocity, h));
            body.angle += body.angularVelocity * h;
        }
    }

    void World::SolvePositions()
    {
        for (const Contact& contact : m_contacts)
        {
            if (contact.isTrigger)
            {
                continue;
            }
            Body& a = m_bodies[contact.bodyA];
            Body& b = m_bodies[contact.bodyB];
            const Vec2 normal = contact.manifold.normal;
            for (std::uint32_t i = 0; i < contact.manifold.count; ++i)
            {
                // 서브스텝 시작 뒤로 돈 만큼 팔을 돌린다.
                const Vec2 rA = RotateVector(Rotation::FromAngle(a.angle - a.startAngle), contact.anchorA[i]);
                const Vec2 rB = RotateVector(Rotation::FromAngle(b.angle - b.startAngle), contact.anchorB[i]);
                const Vec2 moved = Subtract(Subtract(b.center, b.startCenter), Subtract(a.center, a.startCenter));
                const float separation = Dot(Add(moved, Subtract(rB, rA)), normal) + contact.baseSeparation[i];

                // LinearSlop 만큼은 박힌 채 둔다. 0 으로 맞추면 닿았다 떨어졌다 하며 접촉이 끊긴다.
                const float correction = std::clamp(
                    PositionCorrectionFactor * (separation + LinearSlop), -MaxLinearCorrection, 0.0f);
                if (correction >= 0.0f)
                {
                    continue;
                }
                const float rnA = Cross(rA, normal);
                const float rnB = Cross(rB, normal);
                const float k = a.inverseMass + b.inverseMass
                    + a.inverseInertia * rnA * rnA + b.inverseInertia * rnB * rnB;
                if (k <= 0.0f)
                {
                    continue;
                }
                const Vec2 impulse = Scale(normal, -correction / k);
                a.center = Subtract(a.center, Scale(impulse, a.inverseMass));
                a.angle -= a.inverseInertia * Cross(rA, impulse);
                b.center = Add(b.center, Scale(impulse, b.inverseMass));
                b.angle += b.inverseInertia * Cross(rB, impulse);
            }
        }
    }

    void World::UpdateTouching()
    {
        m_previousTouching.Swap(m_touching);
        m_touching.Clear();

        for (const Contact& contact : m_contacts)
        {
            std::uint32_t deepestIndex = 0;
            for (std::uint32_t i = 1; i < contact.manifold.count; ++i)
            {
                if (contact.manifold.points[i].separation < contact.manifold.points[deepestIndex].separation)
                {
                    deepestIndex = i;
                }
            }
            const float deepest = contact.manifold.points[deepestIndex].separation;
            // 트리거는 실제로 겹쳐야, 단단한 접촉은 위치 보정이 남기는 LinearSlop 안이면 닿은 것이다.
            const float limit = contact.isTrigger ? 0.0f : LinearSlop;
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
                const bool swapped = pair.shapeA != contact.shapeA;
                pair.normal = swapped
                    ? Vec2{ -contact.manifold.normal.x, -contact.manifold.normal.y }
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

        // 두 정렬된 목록을 맞대어 새로 생긴 것은 시작, 사라진 것은 끝이다. 지워진 도형은 generation 이 달라
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
                ++i;
                ++j;
            }
        }
    }
}
