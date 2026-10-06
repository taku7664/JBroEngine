#include <JBro/Physics2D/World.h>

#include "VectorMath.h"

#include <algorithm>
#include <cmath>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/ValueMath.h>

// 조인트의 준비·풀이·위치 보정이다(D-233, physics-plan §4 의 9-3). 접촉과 같은 서브스텝 안에서 돈다: 준비 → 따뜻한 시작 →
// 속도 반복(조인트를 접촉보다 먼저) → 위치 적분 → 위치 반복(조인트 뒤 접촉). 식은 Box2D 2.4 의 거리·회전 조인트이고,
// 축 고정(D-227)을 위해 질량은 축마다의 역질량으로 잰다.
namespace JBro::Physics2D
{
    using namespace Internal;

    namespace
    {
        constexpr Float Pi = 3.14159265358979323846f;
        // 한 번의 위치 반복이 옮기는 가장 큰 거리와 각도. 크게 벌어진 조인트가 한 번에 튀지 않게 한다.
        constexpr Float MaxJointLinearCorrection = 0.2f;
        constexpr Float MaxJointAngularCorrection = 8.0f * Pi / 180.0f;

        template <typename TBody>
        Float InverseMassAlong(const TBody& body, Vector2 d)
        {
            return d.x * d.x * body.inverseMassAxes.x + d.y * d.y * body.inverseMassAxes.y;
        }

        template <typename TBody>
        void ApplyImpulse(TBody& a, TBody& b, Vector2 rA, Vector2 rB, Vector2 impulse)
        {
            a.linearVelocity = Subtract(a.linearVelocity, Multiply(impulse, a.inverseMassAxes));
            a.angularVelocity -= a.inverseInertia * Cross(rA, impulse);
            b.linearVelocity = Add(b.linearVelocity, Multiply(impulse, b.inverseMassAxes));
            b.angularVelocity += b.inverseInertia * Cross(rB, impulse);
        }

        template <typename TBody>
        void ApplyPositionImpulse(TBody& a, TBody& b, Vector2 rA, Vector2 rB, Vector2 impulse)
        {
            a.center = Subtract(a.center, Multiply(impulse, a.inverseMassAxes));
            a.angle -= a.inverseInertia * Cross(rA, impulse);
            b.center = Add(b.center, Multiply(impulse, b.inverseMassAxes));
            b.angle += b.inverseInertia * Cross(rB, impulse);
        }

        template <typename TBody>
        Vector2 RelativeVelocity(const TBody& a, const TBody& b, Vector2 rA, Vector2 rB)
        {
            return Subtract(Add(b.linearVelocity, Cross(b.angularVelocity, rB)),
                Add(a.linearVelocity, Cross(a.angularVelocity, rA)));
        }

        // 2x2 대칭 행렬 [k11 k12; k12 k22] 로 x 를 푼다. 풀 수 없으면 0 이다.
        Vector2 Solve22(Float k11, Float k12, Float k22, Vector2 b)
        {
            const Float determinant = k11 * k22 - k12 * k12;
            if (determinant == 0.0f)
            {
                return {};
            }
            const Float inverse = 1.0f / determinant;
            return { inverse * (k22 * b.x - k12 * b.y), inverse * (k11 * b.y - k12 * b.x) };
        }
    }

    JointId World::AddJoint(JointType type, BodyId bodyA, BodyId bodyB, Bool collideConnected)
    {
        Body* a = FindBody(bodyA);
        // B 는 없어도 된다(월드에 건다). 있다고 적었는데 죽은 번호면 거절한다.
        const Bool toWorld = bodyB.index == InvalidIndex;
        Body* b = toWorld ? nullptr : FindBody(bodyB);
        if (a == nullptr || (false == toWorld && (b == nullptr || bodyA.index == bodyB.index)))
        {
            return {};
        }
        UInt32 index = 0;
        if (false == m_freeJoints.IsEmpty())
        {
            index = m_freeJoints.Last();
            m_freeJoints.RemoveAt(m_freeJoints.Size() - 1);
        }
        else
        {
            index = static_cast<std::uint32_t>(m_joints.Size());
            m_joints.Add({});
        }
        Joint& joint = m_joints[index];
        const UInt32 generation = joint.generation;
        joint = Joint{};
        joint.generation = generation;
        joint.alive = true;
        joint.type = type;
        joint.bodyA = bodyA.index;
        joint.bodyB = toWorld ? InvalidIndex : bodyB.index;
        joint.collideConnected = collideConnected;
        AddJointFilter(joint);
        WakeBody(*a);
        if (b != nullptr)
        {
            WakeBody(*b);
        }
        return { index, joint.generation };
    }

    JointId World::CreateDistanceJoint(const DistanceJointDef& def)
    {
        const JointId id = AddJoint(JointType::Distance, def.bodyA, def.bodyB, def.collideConnected);
        if (Joint* joint = FindJoint(id))
        {
            joint->distance = def;
        }
        return id;
    }

    JointId World::CreateHingeJoint(const HingeJointDef& def)
    {
        const JointId id = AddJoint(JointType::Hinge, def.bodyA, def.bodyB, def.collideConnected);
        if (Joint* joint = FindJoint(id))
        {
            joint->hinge = def;
        }
        return id;
    }

    Bool World::SetDistanceJoint(JointId id, const DistanceJointDef& def)
    {
        Joint* joint = FindJoint(id);
        const UInt32 bodyB = def.bodyB.index == InvalidIndex ? InvalidIndex : def.bodyB.index;
        if (joint == nullptr || joint->type != JointType::Distance || joint->bodyA != def.bodyA.index || joint->bodyB != bodyB)
        {
            return false;
        }
        RemoveJointFilter(*joint);
        // 단단함·밧줄·용수철이 바뀌면 쌓인 임펄스의 뜻이 달라진다. 비우고 새로 쌓는다.
        if (joint->distance.maxLengthOnly != def.maxLengthOnly || (joint->distance.hertz > 0.0f) != (def.hertz > 0.0f))
        {
            joint->impulse = 0.0f;
            joint->lowerImpulse = 0.0f;
            joint->upperImpulse = 0.0f;
        }
        joint->distance = def;
        joint->collideConnected = def.collideConnected;
        AddJointFilter(*joint);
        WakeBody(m_bodies[joint->bodyA]);
        WakeBody(JointBody(joint->bodyB));
        return true;
    }

    Bool World::SetHingeJoint(JointId id, const HingeJointDef& def)
    {
        Joint* joint = FindJoint(id);
        const UInt32 bodyB = def.bodyB.index == InvalidIndex ? InvalidIndex : def.bodyB.index;
        if (joint == nullptr || joint->type != JointType::Hinge || joint->bodyA != def.bodyA.index || joint->bodyB != bodyB)
        {
            return false;
        }
        RemoveJointFilter(*joint);
        joint->hinge = def;
        joint->collideConnected = def.collideConnected;
        AddJointFilter(*joint);
        // 한계를 끄면 쌓인 한계 임펄스가 남아 밀지 않게 비운다. 모터도 같다.
        if (false == def.enableLimit)
        {
            joint->lowerImpulse = 0.0f;
            joint->upperImpulse = 0.0f;
        }
        if (false == def.enableMotor)
        {
            joint->motorImpulse = 0.0f;
        }
        WakeBody(m_bodies[joint->bodyA]);
        WakeBody(JointBody(joint->bodyB));
        return true;
    }

    void World::DestroyJoint(JointId id)
    {
        Joint* joint = FindJoint(id);
        if (joint == nullptr)
        {
            return;
        }
        RemoveJointFilter(*joint);
        if (m_bodies[joint->bodyA].alive)
        {
            WakeBody(m_bodies[joint->bodyA]);
        }
        if (joint->bodyB != InvalidIndex && m_bodies[joint->bodyB].alive)
        {
            WakeBody(m_bodies[joint->bodyB]);
        }
        joint->alive = false;
        ++joint->generation;
        m_freeJoints.Add(id.index);
    }

    Bool World::IsValid(JointId id) const
    {
        return FindJoint(id) != nullptr;
    }

    std::size_t World::GetJointCount() const
    {
        return m_joints.Size() - m_freeJoints.Size();
    }

    Float World::GetHingeAngle(JointId id) const
    {
        const Joint* joint = FindJoint(id);
        if (joint == nullptr || joint->type != JointType::Hinge)
        {
            return 0.0f;
        }
        const Float angleB = joint->bodyB == InvalidIndex ? Float(0.0f) : m_bodies[joint->bodyB].angle;
        return angleB - m_bodies[joint->bodyA].angle - joint->hinge.referenceAngle;
    }

    World::Joint* World::FindJoint(JointId id)
    {
        if (id.index >= m_joints.Size())
        {
            return nullptr;
        }
        Joint& joint = m_joints[id.index];
        return joint.alive && joint.generation == id.generation ? &joint : nullptr;
    }

    const World::Joint* World::FindJoint(JointId id) const
    {
        if (id.index >= m_joints.Size())
        {
            return nullptr;
        }
        const Joint& joint = m_joints[id.index];
        return joint.alive && joint.generation == id.generation ? &joint : nullptr;
    }

    World::Body& World::JointBody(UInt32 index)
    {
        return index == InvalidIndex ? m_ground : m_bodies[index];
    }

    UInt64 World::JointPairKey(UInt32 bodyA, UInt32 bodyB)
    {
        const UInt64 low = std::min(bodyA, bodyB);
        const UInt64 high = std::max(bodyA, bodyB);
        return (high << 32) | low;
    }

    void World::AddJointFilter(const Joint& joint)
    {
        if (joint.collideConnected || joint.bodyB == InvalidIndex)
        {
            return;
        }
        ++m_jointFilters[JointPairKey(joint.bodyA, joint.bodyB)];
    }

    void World::RemoveJointFilter(const Joint& joint)
    {
        if (joint.collideConnected || joint.bodyB == InvalidIndex)
        {
            return;
        }
        const UInt64 key = JointPairKey(joint.bodyA, joint.bodyB);
        UInt32* count = m_jointFilters.Find(key);
        if (count == nullptr)
        {
            return;
        }
        if (*count <= 1u)
        {
            m_jointFilters.Remove(key);
        }
        else
        {
            --*count;
        }
    }

    Bool World::IsJointSolved(const Joint& joint) const
    {
        const Body& a = m_bodies[joint.bodyA];
        const Body& b = joint.bodyB == InvalidIndex ? m_ground : m_bodies[joint.bodyB];
        return (a.type == BodyType::Dynamic && a.awake) || (b.type == BodyType::Dynamic && b.awake);
    }

    void World::PrepareJoints(Float h)
    {
        for (Joint& joint : m_joints)
        {
            if (false == joint.alive)
            {
                continue;
            }
            Body& a = m_bodies[joint.bodyA];
            Body& b = JointBody(joint.bodyB);
            // 깬 몸이 잠든 몸을 끌면 풀기 전에 깨운다(접촉과 같다).
            if (a.type == BodyType::Dynamic && b.type == BodyType::Dynamic && a.awake != b.awake)
            {
                WakeBody(a.awake ? b : a);
            }
            if (false == IsJointSolved(joint))
            {
                continue;
            }
            const Vector2 localA = joint.type == JointType::Distance ? joint.distance.localAnchorA : joint.hinge.localAnchorA;
            const Vector2 localB = joint.type == JointType::Distance ? joint.distance.localAnchorB : joint.hinge.localAnchorB;
            joint.rA = RotateVector(Rotation::FromAngle(a.angle), Subtract(localA, a.localCenter));
            joint.rB = RotateVector(Rotation::FromAngle(b.angle), Subtract(localB, b.localCenter));

            if (joint.type == JointType::Distance)
            {
                const Vector2 d = Subtract(Add(b.center, joint.rB), Add(a.center, joint.rA));
                joint.currentLength = Length(d);
                joint.axis = joint.currentLength > LinearSlop ? Scale(d, 1.0f / joint.currentLength) : Vector2{ 1.0f, 0.0f };
                const Float crA = Cross(joint.rA, joint.axis);
                const Float crB = Cross(joint.rB, joint.axis);
                const Float k = InverseMassAlong(a, joint.axis) + InverseMassAlong(b, joint.axis)
                    + a.inverseInertia * crA * crA + b.inverseInertia * crB * crB;
                joint.mass = k > 0.0f ? 1.0f / k : Float(0.0f);
                joint.gamma = 0.0f;
                joint.bias = 0.0f;
                joint.softMass = joint.mass;
                const DistanceJointDef& def = joint.distance;
                if (false == def.maxLengthOnly && def.hertz > 0.0f && k > 0.0f)
                {
                    // 용수철: 질량 m 의 떨림 ω = 2πf 로 k = mω², c = 2mζω. 한 서브스텝의 암시적 식이다.
                    const Float omega = 2.0f * Pi * def.hertz;
                    const Float stiffness = joint.mass * omega * omega;
                    const Float damping = 2.0f * joint.mass * def.dampingRatio * omega;
                    const Float gamma = h * (damping + h * stiffness);
                    joint.gamma = gamma > 0.0f ? 1.0f / gamma : Float(0.0f);
                    joint.bias = (joint.currentLength - def.length) * h * stiffness * joint.gamma;
                    joint.softMass = 1.0f / (k + joint.gamma);
                }
                else
                {
                    joint.impulse = def.maxLengthOnly ? Float(0.0f) : joint.impulse;
                }
            }
            else
            {
                const Vector2 rA = joint.rA;
                const Vector2 rB = joint.rB;
                joint.k11 = a.inverseMassAxes.x + b.inverseMassAxes.x + a.inverseInertia * rA.y * rA.y + b.inverseInertia * rB.y * rB.y;
                joint.k12 = -a.inverseInertia * rA.x * rA.y - b.inverseInertia * rB.x * rB.y;
                joint.k22 = a.inverseMassAxes.y + b.inverseMassAxes.y + a.inverseInertia * rA.x * rA.x + b.inverseInertia * rB.x * rB.x;
                const Float axial = a.inverseInertia + b.inverseInertia;
                joint.axialMass = axial > 0.0f ? 1.0f / axial : Float(0.0f);
                joint.angle = b.angle - a.angle - joint.hinge.referenceAngle;
            }
        }
    }

    void World::WarmStartJoints()
    {
        for (Joint& joint : m_joints)
        {
            if (false == joint.alive || false == IsJointSolved(joint))
            {
                continue;
            }
            Body& a = m_bodies[joint.bodyA];
            Body& b = JointBody(joint.bodyB);
            if (joint.type == JointType::Distance)
            {
                const Float total = joint.impulse + joint.lowerImpulse + joint.upperImpulse;
                ApplyImpulse(a, b, joint.rA, joint.rB, Scale(joint.axis, total));
                continue;
            }
            ApplyImpulse(a, b, joint.rA, joint.rB, joint.linearImpulse);
            const Float axial = joint.motorImpulse + joint.lowerImpulse - joint.upperImpulse;
            a.angularVelocity -= a.inverseInertia * axial;
            b.angularVelocity += b.inverseInertia * axial;
        }
    }

    void World::SolveJoints(Float h)
    {
        const Float inverseH = 1.0f / h;
        for (Joint& joint : m_joints)
        {
            if (false == joint.alive || false == IsJointSolved(joint))
            {
                continue;
            }
            Body& a = m_bodies[joint.bodyA];
            Body& b = JointBody(joint.bodyB);

            if (joint.type == JointType::Distance)
            {
                const DistanceJointDef& def = joint.distance;
                const Float speed = Dot(joint.axis, RelativeVelocity(a, b, joint.rA, joint.rB));
                if (def.maxLengthOnly)
                {
                    // 밧줄: 남은 여유(length - 지금 길이)만큼은 이 서브스텝에 벌어져도 된다. 당기는 쪽(음수)으로만 쌓는다.
                    const Float slack = def.length - joint.currentLength;
                    const Float old = joint.upperImpulse;
                    joint.upperImpulse = std::fmin(0.0f, old - joint.mass * (speed - std::fmax(slack, 0.0f) * inverseH));
                    ApplyImpulse(a, b, joint.rA, joint.rB, Scale(joint.axis, joint.upperImpulse - old));
                }
                else if (def.hertz > 0.0f)
                {
                    const Float impulse = -joint.softMass * (speed + joint.bias + joint.gamma * joint.impulse);
                    joint.impulse += impulse;
                    ApplyImpulse(a, b, joint.rA, joint.rB, Scale(joint.axis, impulse));
                }
                else
                {
                    const Float impulse = -joint.mass * speed;
                    joint.impulse += impulse;
                    ApplyImpulse(a, b, joint.rA, joint.rB, Scale(joint.axis, impulse));
                }
                continue;
            }

            const HingeJointDef& def = joint.hinge;
            if (def.enableMotor)
            {
                const Float speed = b.angularVelocity - a.angularVelocity - def.motorSpeed;
                const Float old = joint.motorImpulse;
                const Float limit = def.maxMotorTorque * h;
                joint.motorImpulse = JBro::Clamp(old - joint.axialMass * speed, -limit, limit);
                const Float impulse = joint.motorImpulse - old;
                a.angularVelocity -= a.inverseInertia * impulse;
                b.angularVelocity += b.inverseInertia * impulse;
            }
            if (def.enableLimit)
            {
                // 아래 한계. 아직 떨어져 있으면 그 틈만큼은 이 서브스텝에 다가와도 된다(미리 만든 접촉과 같다).
                {
                    const Float gap = joint.angle - def.lowerAngle;
                    const Float bias = gap > 0.0f ? gap * inverseH : Float(0.0f);
                    const Float speed = b.angularVelocity - a.angularVelocity;
                    const Float old = joint.lowerImpulse;
                    joint.lowerImpulse = std::fmax(old - joint.axialMass * (speed + bias), 0.0f);
                    const Float impulse = joint.lowerImpulse - old;
                    a.angularVelocity -= a.inverseInertia * impulse;
                    b.angularVelocity += b.inverseInertia * impulse;
                }
                {
                    const Float gap = def.upperAngle - joint.angle;
                    const Float bias = gap > 0.0f ? gap * inverseH : Float(0.0f);
                    const Float speed = a.angularVelocity - b.angularVelocity;
                    const Float old = joint.upperImpulse;
                    joint.upperImpulse = std::fmax(old - joint.axialMass * (speed + bias), 0.0f);
                    const Float impulse = joint.upperImpulse - old;
                    a.angularVelocity += a.inverseInertia * impulse;
                    b.angularVelocity -= b.inverseInertia * impulse;
                }
            }
            // 점을 맞춘다.
            const Vector2 relative = RelativeVelocity(a, b, joint.rA, joint.rB);
            const Vector2 impulse = Scale(Solve22(joint.k11, joint.k12, joint.k22, relative), -1.0f);
            joint.linearImpulse = Add(joint.linearImpulse, impulse);
            ApplyImpulse(a, b, joint.rA, joint.rB, impulse);
        }
    }

    void World::SolveJointPositions()
    {
        for (Joint& joint : m_joints)
        {
            if (false == joint.alive || false == IsJointSolved(joint))
            {
                continue;
            }
            Body& a = m_bodies[joint.bodyA];
            Body& b = JointBody(joint.bodyB);

            if (joint.type == JointType::Distance)
            {
                const DistanceJointDef& def = joint.distance;
                // 용수철은 위치를 맞추지 않는다 - 늘어나는 것이 그것의 뜻이다.
                if (false == def.maxLengthOnly && def.hertz > 0.0f)
                {
                    continue;
                }
                const Vector2 rA = RotateVector(Rotation::FromAngle(a.angle), Subtract(def.localAnchorA, a.localCenter));
                const Vector2 rB = RotateVector(Rotation::FromAngle(b.angle), Subtract(def.localAnchorB, b.localCenter));
                const Vector2 d = Subtract(Add(b.center, rB), Add(a.center, rA));
                const Float length = Length(d);
                if (length <= LinearSlop)
                {
                    continue;
                }
                const Vector2 axis = Scale(d, 1.0f / length);
                Float error = length - def.length;
                if (def.maxLengthOnly && error <= 0.0f)
                {
                    continue;
                }
                error = JBro::Clamp(error, -MaxJointLinearCorrection, MaxJointLinearCorrection);
                const Float crA = Cross(rA, axis);
                const Float crB = Cross(rB, axis);
                const Float k = InverseMassAlong(a, axis) + InverseMassAlong(b, axis)
                    + a.inverseInertia * crA * crA + b.inverseInertia * crB * crB;
                if (k <= 0.0f)
                {
                    continue;
                }
                ApplyPositionImpulse(a, b, rA, rB, Scale(axis, -error / k));
                continue;
            }

            const HingeJointDef& def = joint.hinge;
            if (def.enableLimit && joint.axialMass > 0.0f)
            {
                const Float angle = b.angle - a.angle - def.referenceAngle;
                Float error = 0.0f;
                if (angle < def.lowerAngle)
                {
                    error = std::fmax(angle - def.lowerAngle, -MaxJointAngularCorrection);
                }
                else if (angle > def.upperAngle)
                {
                    error = std::fmin(angle - def.upperAngle, MaxJointAngularCorrection);
                }
                const Float impulse = -joint.axialMass * error;
                a.angle -= a.inverseInertia * impulse;
                b.angle += b.inverseInertia * impulse;
            }
            const Vector2 rA = RotateVector(Rotation::FromAngle(a.angle), Subtract(def.localAnchorA, a.localCenter));
            const Vector2 rB = RotateVector(Rotation::FromAngle(b.angle), Subtract(def.localAnchorB, b.localCenter));
            Vector2 error = Subtract(Add(b.center, rB), Add(a.center, rA));
            const Float errorLength = Length(error);
            if (errorLength > MaxJointLinearCorrection)
            {
                error = Scale(error, MaxJointLinearCorrection / errorLength);
            }
            const Float k11 = a.inverseMassAxes.x + b.inverseMassAxes.x + a.inverseInertia * rA.y * rA.y + b.inverseInertia * rB.y * rB.y;
            const Float k12 = -a.inverseInertia * rA.x * rA.y - b.inverseInertia * rB.x * rB.y;
            const Float k22 = a.inverseMassAxes.y + b.inverseMassAxes.y + a.inverseInertia * rA.x * rA.x + b.inverseInertia * rB.x * rB.x;
            ApplyPositionImpulse(a, b, rA, rB, Scale(Solve22(k11, k12, k22, error), -1.0f));
        }
    }
}
