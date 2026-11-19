#include <JBro/Physics3D/World.h>

#include "JoltRuntime.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

namespace JBro::Physics3D
{
    namespace
    {
        // ── 브로드페이즈 층 ───────────────────────────────────────────
        // Jolt 의 권고대로 움직이지 않는 것과 움직이는 것 둘로 나눈다. 움직이지 않는 것끼리는 브로드페이즈에서부터 만나지 않는다.
        constexpr JPH::BroadPhaseLayer NonMovingLayer(0);
        constexpr JPH::BroadPhaseLayer MovingLayer(1);
        constexpr UInt32 BroadPhaseLayerCount = 2;

        // ── 오브젝트 층 ───────────────────────────────────────────────
        // 2D 처럼 바디마다 32 비트 `layer`·`mask` 를 받는다. Jolt 의 오브젝트 층은 16 비트 번호 하나라 둘이 들어가지 않는다 -
        // 그래서 (layer, mask, 움직이는가) 조합마다 번호를 하나 매기고, 짝 거르기는 그 번호로 표를 찾아 2D 와 같은 식으로 잰다.
        // 조합은 프로젝트의 레이어 수만큼 생기는 것이 보통이라 넉넉한 고정 표다. 표는 바디를 만들 때(메인 스레드)만 늘고,
        // 짝 거르기는 스텝 안(워커일 수 있다)에서 읽기만 한다 - 스텝과 바디 만들기는 둘 다 메인 스레드라 겹치지 않는다.
        constexpr UInt32 MaxLayerCombos = 1024;

        struct LayerCombo
        {
            UInt32 layer = 0;
            UInt32 mask = 0;
            Bool moving = false;
        };

        JPH::Vec3 ToJolt(const Vector3& value)
        {
            return JPH::Vec3(value.x, value.y, value.z);
        }

        JPH::Quat ToJolt(const Quaternion& value)
        {
            const JPH::Quat quat(value.x, value.y, value.z, value.w);
            const float length = quat.Length();
            return length > 1.0e-6f ? quat / length : JPH::Quat::sIdentity();
        }

        Vector3 FromJolt(JPH::Vec3Arg value)
        {
            return {value.GetX(), value.GetY(), value.GetZ()};
        }

        Quaternion FromJolt(JPH::QuatArg value)
        {
            return {value.GetX(), value.GetY(), value.GetZ(), value.GetW()};
        }

        JPH::BodyID ToJolt(BodyId body)
        {
            return JPH::BodyID(body.value);
        }

        JPH::EMotionType ToJolt(BodyType type)
        {
            switch (type)
            {
            case BodyType::Static:
                return JPH::EMotionType::Static;
            case BodyType::Kinematic:
                return JPH::EMotionType::Kinematic;
            case BodyType::Dynamic:
                return JPH::EMotionType::Dynamic;
            }
            return JPH::EMotionType::Static;
        }

        // 도형을 만든다. 크기가 0 이하면 만들지 않는다 - Jolt 는 그런 도형을 오류로 돌려주거나 단언으로 멈춘다.
        JPH::ShapeRefC MakeShape(const ShapeDesc& desc)
        {
            JPH::ShapeRefC inner;
            switch (desc.type)
            {
            case ShapeType::Box:
            {
                const Vector3& half = desc.halfExtents;
                if (false == (half.x > 0.0f && half.y > 0.0f && half.z > 0.0f))
                {
                    return {};
                }
                // 둥근 모서리(볼록 반지름)는 Jolt 가 가장 짧은 반 크기 안으로 스스로 줄인다(`BoxShape` 의 생성자) - 얇은 판도 받는다.
                JPH::BoxShapeSettings settings(ToJolt(half));
                JPH::ShapeSettings::ShapeResult result = settings.Create();
                if (result.HasError())
                {
                    return {};
                }
                inner = result.Get();
                break;
            }
            case ShapeType::Sphere:
            {
                if (false == (desc.radius > 0.0f))
                {
                    return {};
                }
                JPH::SphereShapeSettings settings(desc.radius);
                JPH::ShapeSettings::ShapeResult result = settings.Create();
                if (result.HasError())
                {
                    return {};
                }
                inner = result.Get();
                break;
            }
            case ShapeType::Capsule:
            {
                if (false == (desc.radius > 0.0f && desc.halfHeight > 0.0f))
                {
                    return {};
                }
                JPH::CapsuleShapeSettings settings(desc.halfHeight, desc.radius);
                JPH::ShapeSettings::ShapeResult result = settings.Create();
                if (result.HasError())
                {
                    return {};
                }
                inner = result.Get();
                break;
            }
            }
            if (inner == nullptr)
            {
                return {};
            }
            const Vector3& offset = desc.offset;
            if (offset.x == 0.0f && offset.y == 0.0f && offset.z == 0.0f)
            {
                return inner;
            }
            // 바디 원점에서 비켜 앉은 도형. 안쪽 도형을 먼저 만들어 넘긴다 - 설정 객체를 넘기면 그것을 참조 계수로 잡아 둔다.
            JPH::RotatedTranslatedShapeSettings settings(ToJolt(offset), JPH::Quat::sIdentity(), inner.GetPtr());
            JPH::ShapeSettings::ShapeResult result = settings.Create();
            if (result.HasError())
            {
                return {};
            }
            return result.Get();
        }

        JPH::EAllowedDOFs AllowedDOFs(const BodyDef& def)
        {
            std::uint32_t dofs = static_cast<std::uint32_t>(JPH::EAllowedDOFs::All);
            const auto clear = [&dofs](Bool frozen, JPH::EAllowedDOFs dof) {
                if (frozen)
                {
                    dofs &= ~static_cast<std::uint32_t>(dof);
                }
            };
            clear(def.freezePositionX, JPH::EAllowedDOFs::TranslationX);
            clear(def.freezePositionY, JPH::EAllowedDOFs::TranslationY);
            clear(def.freezePositionZ, JPH::EAllowedDOFs::TranslationZ);
            clear(def.freezeRotationX, JPH::EAllowedDOFs::RotationX);
            clear(def.freezeRotationY, JPH::EAllowedDOFs::RotationY);
            clear(def.freezeRotationZ, JPH::EAllowedDOFs::RotationZ);
            return static_cast<JPH::EAllowedDOFs>(dofs);
        }
    }

    struct World::Impl
    {
        class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
        {
        public:
            explicit BroadPhaseLayers(const Impl& owner) : m_owner(owner)
            {
            }

            JPH::uint GetNumBroadPhaseLayers() const override
            {
                return BroadPhaseLayerCount;
            }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return m_owner.combos[layer].moving ? MovingLayer : NonMovingLayer;
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                return layer == MovingLayer ? "Moving" : "NonMoving";
            }
#endif

        private:
            const Impl& m_owner;
        };

        class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            explicit ObjectVsBroadPhase(const Impl& owner) : m_owner(owner)
            {
            }

            bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
            {
                // 움직이지 않는 것은 움직이는 것과만 만난다.
                return m_owner.combos[layer].moving || broadPhase == MovingLayer;
            }

        private:
            const Impl& m_owner;
        };

        class ObjectPairs final : public JPH::ObjectLayerPairFilter
        {
        public:
            explicit ObjectPairs(const Impl& owner) : m_owner(owner)
            {
            }

            bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override
            {
                const LayerCombo& a = m_owner.combos[first];
                const LayerCombo& b = m_owner.combos[second];
                if (false == a.moving && false == b.moving)
                {
                    return false;
                }
                return (a.layer & b.mask) != 0 && (b.layer & a.mask) != 0;
            }

        private:
            const Impl& m_owner;
        };

        explicit Impl(const WorldSettings& settings)
            : runtime()
            , temp(settings.tempAllocatorBytes)
            , jobs(JPH::cMaxPhysicsJobs)
            , broadPhaseLayers(*this)
            , objectVsBroadPhase(*this)
            , objectPairs(*this)
        {
            physics.Init(settings.maxBodies, 0, settings.maxBodyPairs, settings.maxContactConstraints, broadPhaseLayers,
                objectVsBroadPhase, objectPairs);
            physics.SetGravity(ToJolt(settings.gravity));
        }

        // 조합의 번호. 처음 보는 조합이면 표에 더한다. 표가 찼으면 무효 층이다.
        JPH::ObjectLayer FindOrAddCombo(UInt32 layer, UInt32 mask, Bool moving)
        {
            for (UInt32 index = 0; index < comboCount; ++index)
            {
                const LayerCombo& combo = combos[index];
                if (combo.layer == layer && combo.mask == mask && combo.moving == moving)
                {
                    return static_cast<JPH::ObjectLayer>(index);
                }
            }
            if (comboCount >= MaxLayerCombos)
            {
                return JPH::cObjectLayerInvalid;
            }
            combos[comboCount] = {layer, mask, moving};
            return static_cast<JPH::ObjectLayer>(comboCount++);
        }

        // 먼저 서고 나중에 내려가야 한다: Jolt 의 할당 훅과 타입 등록이 아래 모두보다 오래 산다.
        Internal::JoltRuntime runtime;
        LayerCombo combos[MaxLayerCombos];
        UInt32 comboCount = 0;
        JPH::TempAllocatorImpl temp;
        JPH::JobSystemSingleThreaded jobs;
        BroadPhaseLayers broadPhaseLayers;
        ObjectVsBroadPhase objectVsBroadPhase;
        ObjectPairs objectPairs;
        JPH::PhysicsSystem physics;
    };

    World::World(const WorldSettings& settings) : m_impl(MakeOwnerPtr<Impl>(settings))
    {
    }

    World::~World()
    {
        // 남은 바디를 지운다. PhysicsSystem 은 지울 때 바디를 놓아주지만 도형의 참조 계수는 바디가 지워져야 내려간다.
        JPH::BodyIDVector bodies;
        m_impl->physics.GetBodies(bodies);
        JPH::BodyInterface& bodyInterface = m_impl->physics.GetBodyInterfaceNoLock();
        for (const JPH::BodyID& id : bodies)
        {
            if (bodyInterface.IsAdded(id))
            {
                bodyInterface.RemoveBody(id);
            }
            bodyInterface.DestroyBody(id);
        }
    }

    BodyId World::CreateBody(const BodyDef& def)
    {
        JPH::ShapeRefC shape = MakeShape(def.shape);
        if (shape == nullptr)
        {
            return {};
        }
        JPH::EMotionType motion = ToJolt(def.type);
        JPH::EAllowedDOFs dofs = AllowedDOFs(def);
        // 모든 축을 고정한 동적 바디는 Jolt 가 받지 않는다(자유도 0). 움직이지 않는 것과 같으므로 키네마틱으로 둔다 -
        // 정적으로 두면 다른 정적 바디와 만나지 않는다는 것 말고는 같지만, 스크립트가 속도를 주면 키네마틱만 움직인다.
        if (motion == JPH::EMotionType::Dynamic && dofs == JPH::EAllowedDOFs::None)
        {
            motion = JPH::EMotionType::Kinematic;
            dofs = JPH::EAllowedDOFs::All;
        }
        const Bool moving = motion != JPH::EMotionType::Static;
        const JPH::ObjectLayer layer = m_impl->FindOrAddCombo(def.layer, def.mask, moving);
        if (layer == JPH::cObjectLayerInvalid)
        {
            return {};
        }
        JPH::BodyCreationSettings settings(shape.GetPtr(), JPH::RVec3(def.position.x, def.position.y, def.position.z),
            ToJolt(def.rotation), motion, layer);
        settings.mIsSensor = def.isTrigger;
        settings.mFriction = def.friction;
        settings.mRestitution = def.restitution;
        settings.mGravityFactor = def.gravityScale;
        settings.mLinearDamping = def.linearDamping;
        settings.mAngularDamping = def.angularDamping;
        settings.mAllowSleeping = def.canSleep;
        settings.mAllowedDOFs = dofs;
        settings.mUserData = def.userData;
        if (motion != JPH::EMotionType::Static)
        {
            settings.mLinearVelocity = ToJolt(def.linearVelocity);
            settings.mAngularVelocity = ToJolt(def.angularVelocity);
        }
        if (motion == JPH::EMotionType::Dynamic)
        {
            // 질량은 받은 값, 관성은 도형에서 그 질량으로 구한다.
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = def.mass > 0.0f ? def.mass.Get() : 1.0f;
        }
        JPH::BodyInterface& bodyInterface = m_impl->physics.GetBodyInterfaceNoLock();
        const JPH::BodyID id = bodyInterface.CreateAndAddBody(
            settings, motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        if (id.IsInvalid())
        {
            return {};
        }
        return BodyId{id.GetIndexAndSequenceNumber()};
    }

    void World::DestroyBody(BodyId body)
    {
        if (false == IsValid(body))
        {
            return;
        }
        JPH::BodyInterface& bodyInterface = m_impl->physics.GetBodyInterfaceNoLock();
        bodyInterface.RemoveBody(ToJolt(body));
        bodyInterface.DestroyBody(ToJolt(body));
    }

    Bool World::IsValid(BodyId body) const
    {
        if (body.value == InvalidBodyValue)
        {
            return false;
        }
        return m_impl->physics.GetBodyInterfaceNoLock().IsAdded(ToJolt(body));
    }

    UInt32 World::GetBodyCount() const
    {
        return m_impl->physics.GetNumBodies();
    }

    void World::SetTransform(BodyId body, const Vector3& position, const Quaternion& rotation)
    {
        if (false == IsValid(body))
        {
            return;
        }
        m_impl->physics.GetBodyInterfaceNoLock().SetPositionAndRotation(ToJolt(body),
            JPH::RVec3(position.x, position.y, position.z), ToJolt(rotation), JPH::EActivation::Activate);
    }

    Vector3 World::GetPosition(BodyId body) const
    {
        if (false == IsValid(body))
        {
            return {};
        }
        const JPH::RVec3 position = m_impl->physics.GetBodyInterfaceNoLock().GetPosition(ToJolt(body));
        return {static_cast<Float>(position.GetX()), static_cast<Float>(position.GetY()), static_cast<Float>(position.GetZ())};
    }

    Quaternion World::GetRotation(BodyId body) const
    {
        if (false == IsValid(body))
        {
            return {};
        }
        return FromJolt(m_impl->physics.GetBodyInterfaceNoLock().GetRotation(ToJolt(body)));
    }

    Vector3 World::GetLinearVelocity(BodyId body) const
    {
        if (false == IsValid(body))
        {
            return {};
        }
        return FromJolt(m_impl->physics.GetBodyInterfaceNoLock().GetLinearVelocity(ToJolt(body)));
    }

    void World::SetLinearVelocity(BodyId body, const Vector3& velocity)
    {
        if (false == IsValid(body))
        {
            return;
        }
        m_impl->physics.GetBodyInterfaceNoLock().SetLinearVelocity(ToJolt(body), ToJolt(velocity));
    }

    Vector3 World::GetAngularVelocity(BodyId body) const
    {
        if (false == IsValid(body))
        {
            return {};
        }
        return FromJolt(m_impl->physics.GetBodyInterfaceNoLock().GetAngularVelocity(ToJolt(body)));
    }

    void World::SetAngularVelocity(BodyId body, const Vector3& velocity)
    {
        if (false == IsValid(body))
        {
            return;
        }
        m_impl->physics.GetBodyInterfaceNoLock().SetAngularVelocity(ToJolt(body), ToJolt(velocity));
    }

    Bool World::IsAwake(BodyId body) const
    {
        if (false == IsValid(body))
        {
            return false;
        }
        return m_impl->physics.GetBodyInterfaceNoLock().IsActive(ToJolt(body));
    }

    UInt64 World::GetUserData(BodyId body) const
    {
        if (false == IsValid(body))
        {
            return 0;
        }
        return m_impl->physics.GetBodyInterfaceNoLock().GetUserData(ToJolt(body));
    }

    void World::SetGravity(const Vector3& gravity)
    {
        m_impl->physics.SetGravity(ToJolt(gravity));
    }

    Vector3 World::GetGravity() const
    {
        return FromJolt(m_impl->physics.GetGravity());
    }

    void World::Step(Float deltaTime)
    {
        if (false == (deltaTime > 0.0f))
        {
            return;
        }
        m_impl->physics.Update(deltaTime, 1, &m_impl->temp, &m_impl->jobs);
    }
}
