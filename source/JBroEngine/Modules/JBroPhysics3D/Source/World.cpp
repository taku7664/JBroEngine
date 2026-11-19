#include <JBro/Physics3D/World.h>

#include "JoltRuntime.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <JBro/Types/Table.h>
#include <mutex>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <cmath>
#include <cstdint>
#include <cstring>
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

        // ── 잎 도형 표(D-421) ─────────────────────────────────────────────
        // 도형마다의 표면(마찰·반발)과 레이어는 커널이 바디마다 든 표에 있고, Jolt 의 잎 도형에는 그 표의 자리(꼬리표 + 번호)만 싣는다.
        // 접촉과 질의는 닿은 잎의 번호로 표를 찾는다 - 복합 도형 안에서도 `GetSubShapeUserData` 가 잎 도형의 것을 준다.
        // (전에는 마찰·반발을 userData 64 비트에 우겨 넣었다 - 레이어까지는 들어가지 않았다.)
        constexpr JPH::uint64 LeafTag = 0x4C45414600000000ull;
        constexpr JPH::uint64 LeafTagMask = 0xFFFFFFFF00000000ull;

        struct LeafInfo
        {
            float friction = 0.6f;
            float restitution = 0.0f;
            std::uint32_t layer = 0x00000001u;
            std::uint32_t mask = 0xFFFFFFFFu;
        };

        // 바디(번호)마다 잎 정보의 표다. 바디를 만들고 도형을 바꾸고 지울 때(메인 스레드, 스텝 밖)만 바뀌고, 스텝 안의 콜백(워커)과 질의는 읽기만 한다.
        class LeafTable
        {
        public:
            void Set(JPH::BodyID body, ArrayView<const ShapeDesc> shapes)
            {
                Array<LeafInfo> leaves;
                leaves.Reserve(shapes.Size());
                for (const ShapeDesc& desc : shapes)
                {
                    LeafInfo leaf;
                    leaf.friction = desc.friction;
                    leaf.restitution = desc.restitution;
                    leaf.layer = desc.layer;
                    leaf.mask = desc.mask;
                    leaves.Add(leaf);
                }
                const UInt32 key = body.GetIndexAndSequenceNumber();
                m_bodies.Remove(key);
                m_bodies.TryAdd(key, std::move(leaves));
            }

            void Remove(JPH::BodyID body)
            {
                m_bodies.Remove(UInt32(body.GetIndexAndSequenceNumber()));
            }

            // 바디의 잎 하나. 잎 도형의 userData 가 꼬리표를 달지 않았거나(복합 도형 자체·옮긴 도형) 모르는 바디면 널이다.
            const LeafInfo* Find(JPH::BodyID body, JPH::uint64 leafData) const
            {
                if ((leafData & LeafTagMask) != LeafTag)
                {
                    return nullptr;
                }
                const Array<LeafInfo>* leaves = m_bodies.Find(UInt32(body.GetIndexAndSequenceNumber()));
                const std::uint32_t index = static_cast<std::uint32_t>(leafData & 0xFFFFFFFFull);
                return leaves != nullptr && index < leaves->Size() ? &(*leaves)[index] : nullptr;
            }

            // 두 잎이 만나는가: 서로의 마스크가 상대 레이어를 품고, 레이어 충돌 표가 떼어 두지 않은 비트 쌍이 하나라도 있다.
            Bool Meet(const LeafInfo& a, const LeafInfo& b) const
            {
                if ((a.layer & b.mask) == 0 || (b.layer & a.mask) == 0)
                {
                    return false;
                }
                if (false == m_anyIgnored)
                {
                    return true;
                }
                for (std::uint32_t bit = 0; bit < 32; ++bit)
                {
                    if ((a.layer & (1u << bit)) != 0 && (b.layer & ~m_ignored[bit]) != 0)
                    {
                        return true;
                    }
                }
                return false;
            }

            void SetIgnored(const UInt32 (&rows)[32])
            {
                m_anyIgnored = false;
                for (std::uint32_t row = 0; row < 32; ++row)
                {
                    m_ignored[row] = rows[row];
                    m_anyIgnored = m_anyIgnored || rows[row] != 0;
                }
            }

        private:
            Table<UInt32, Array<LeafInfo>> m_bodies;
            std::uint32_t m_ignored[32] = {};
            Bool m_anyIgnored = false;
        };

        // 잎 도형 하나(원점 기준, offset 없이). 크기가 0 이하면 만들지 않는다 - Jolt 는 그런 도형을 오류로 돌려주거나 단언으로 멈춘다.
        JPH::ShapeRefC MakeLeaf(const ShapeDesc& desc, std::uint32_t leafIndex)
        {
            JPH::ShapeRefC inner;
            const JPH::uint64 surface = LeafTag | leafIndex;
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
                settings.mUserData = surface;
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
                settings.mUserData = surface;
                JPH::ShapeSettings::ShapeResult result = settings.Create();
                if (result.HasError())
                {
                    return {};
                }
                inner = result.Get();
                break;
            }
            case ShapeType::ConvexHull:
            {
                if (desc.points.Size() < 4)
                {
                    return {};
                }
                JPH::Array<JPH::Vec3> points;
                points.reserve(desc.points.Size());
                for (const Vector3& point : desc.points)
                {
                    points.push_back(ToJolt(point));
                }
                JPH::ConvexHullShapeSettings settings(points);
                settings.mUserData = surface;
                JPH::ShapeSettings::ShapeResult result = settings.Create();
                if (result.HasError())
                {
                    return {};
                }
                inner = result.Get();
                break;
            }
            case ShapeType::Mesh:
            {
                const std::size_t triangles = desc.indices.Size() / 3;
                if (triangles == 0 || desc.points.Size() < 3)
                {
                    return {};
                }
                JPH::VertexList vertices;
                vertices.reserve(desc.points.Size());
                for (const Vector3& point : desc.points)
                {
                    vertices.push_back(JPH::Float3(point.x, point.y, point.z));
                }
                JPH::IndexedTriangleList list;
                list.reserve(triangles);
                for (std::size_t index = 0; index < triangles; ++index)
                {
                    const std::uint32_t a = desc.indices[index * 3];
                    const std::uint32_t b = desc.indices[index * 3 + 1];
                    const std::uint32_t c = desc.indices[index * 3 + 2];
                    if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size())
                    {
                        return {};
                    }
                    list.push_back(JPH::IndexedTriangle(a, b, c, 0));
                }
                JPH::MeshShapeSettings settings(std::move(vertices), std::move(list));
                settings.mUserData = surface;
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
                settings.mUserData = surface;
                JPH::ShapeSettings::ShapeResult result = settings.Create();
                if (result.HasError())
                {
                    return {};
                }
                inner = result.Get();
                break;
            }
            }
            return inner;
        }

        Bool HasMesh(ArrayView<const ShapeDesc> shapes)
        {
            for (const ShapeDesc& desc : shapes)
            {
                if (desc.type == ShapeType::Mesh)
                {
                    return true;
                }
            }
            return false;
        }

        Bool IsZero(const Vector3& value)
        {
            return value.x == 0.0f && value.y == 0.0f && value.z == 0.0f;
        }

        // 바디의 도형. 하나면 잎(비켜 앉았으면 옮긴 도형), 여럿이면 각자 offset 에 앉힌 정적 복합 도형이다.
        JPH::ShapeRefC MakeBodyShape(ArrayView<const ShapeDesc> shapes)
        {
            if (shapes.Size() == 0)
            {
                return {};
            }
            if (shapes.Size() > 1)
            {
                JPH::StaticCompoundShapeSettings compound;
                for (std::size_t index = 0; index < shapes.Size(); ++index)
                {
                    JPH::ShapeRefC leaf = MakeLeaf(shapes[index], static_cast<std::uint32_t>(index));
                    if (leaf == nullptr)
                    {
                        return {};
                    }
                    compound.AddShape(ToJolt(shapes[index].offset), JPH::Quat::sIdentity(), leaf.GetPtr());
                }
                JPH::ShapeSettings::ShapeResult result = compound.Create();
                if (result.HasError())
                {
                    return {};
                }
                return result.Get();
            }
            const ShapeDesc& desc = shapes[0];
            JPH::ShapeRefC inner = MakeLeaf(desc, 0);
            if (inner == nullptr)
            {
                return {};
            }
            const Vector3& offset = desc.offset;
            if (IsZero(offset))
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
        // 접촉을 듣는다: (1) 마찰과 반발을 닿은 두 잎 도형의 값으로 섞고, (2) 바디 쌍마다 시작·유지·끝 사건을 모은다.
        // Jolt 는 잎 도형 쌍마다 알리므로 쌍마다 닿은 잎 수를 세어 0 에서 1 이 될 때가 시작, 1 에서 0 이 될 때가 끝이다.
        // 콜백은 Jolt 의 워커에서 올 수 있어 잠금 하나로 지킨다. 표와 배열은 미리 잡아 두어 흔한 경우 할당하지 않는다.
        class ContactTracker final : public JPH::ContactListener
        {
        public:
            struct Pair
            {
                ContactEvent event;
                UInt32 touching = 0;
                Bool reported = false;
                // 바디가 잠들어 Jolt 가 접촉을 비웠다. 떨어진 것이 아니다 - 깨어 다시 닿으면 새 시작이 아니다.
                Bool sleeping = false;
            };

            ContactTracker()
            {
                pairs.Reserve(256);
                begins.Reserve(64);
                stays.Reserve(256);
                ends.Reserve(64);
                pendingEnds.Reserve(64);
            }

            // **잎마다 거른다**(D-421). 바디 쌍은 넓은 판정에서 두 바디의 레이어를 합친 것으로 느슨하게 지났다 - 여기서 닿은 두 잎의 레이어·마스크·충돌 표로 다시 본다.
            // 이 접촉 하나만 받거나 버린다(한 바디 쌍의 다른 잎 쌍은 따로 묻는다).
            JPH::ValidateResult OnContactValidate(const JPH::Body& first, const JPH::Body& second, JPH::RVec3Arg baseOffset,
                const JPH::CollideShapeResult& result) override
            {
                (void)baseOffset;
                const LeafInfo* a = leaves->Find(first.GetID(), first.GetShape()->GetSubShapeUserData(result.mSubShapeID1));
                const LeafInfo* b = leaves->Find(second.GetID(), second.GetShape()->GetSubShapeUserData(result.mSubShapeID2));
                if (a == nullptr || b == nullptr || leaves->Meet(*a, *b))
                {
                    return JPH::ValidateResult::AcceptContact;
                }
                return JPH::ValidateResult::RejectContact;
            }

            void OnContactAdded(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
                JPH::ContactSettings& settings) override
            {
                Mix(first, second, manifold, settings);
                std::lock_guard<std::mutex> lock(mutex);
                const UInt64 key = Key(first.GetID(), second.GetID());
                Pair* pair = pairs.Find(key);
                if (pair == nullptr)
                {
                    Pair fresh;
                    fresh.event.bodyA = BodyId{first.GetID().GetIndexAndSequenceNumber()};
                    fresh.event.bodyB = BodyId{second.GetID().GetIndexAndSequenceNumber()};
                    fresh.event.userDataA = first.GetUserData();
                    fresh.event.userDataB = second.GetUserData();
                    fresh.event.isTrigger = first.IsSensor() || second.IsSensor();
                    pairs.TryAdd(key, fresh);
                    pair = pairs.Find(key);
                }
                const Bool waking = pair->touching == 0 && pair->sleeping;
                pair->sleeping = false;
                if (pair->touching++ == 0 && false == waking)
                {
                    ContactEvent event = pair->event;
                    Locate(first, manifold, event);
                    begins.Add(event);
                    // 시작한 스텝에는 시작만 있다(2D 와 같다).
                    pair->reported = true;
                }
            }

            void OnContactPersisted(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
                JPH::ContactSettings& settings) override
            {
                Mix(first, second, manifold, settings);
                std::lock_guard<std::mutex> lock(mutex);
                Pair* pair = pairs.Find(Key(first.GetID(), second.GetID()));
                if (pair == nullptr || pair->reported)
                {
                    return;
                }
                pair->reported = true;
                ContactEvent event = pair->event;
                Locate(first, manifold, event);
                stays.Add(event);
            }

            // **잠들어도 Jolt 는 접촉을 "제거" 로 알린다** - 잠든 바디의 접촉 캐시를 비우기 때문이다. 그래서 여기서는 세기만 줄이고,
            // 끝인지는 스텝 뒤(`Resolve`)에 바디가 잠들었는지 보고 정한다. 이 콜백 안에서는 바디를 볼 수 없다(Jolt 가 잠그고 있다).
            void OnContactRemoved(const JPH::SubShapeIDPair& removed) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                const UInt64 key = Key(removed.GetBody1ID(), removed.GetBody2ID());
                Pair* pair = pairs.Find(key);
                if (pair == nullptr || pair->touching == 0)
                {
                    return;
                }
                if (--pair->touching == 0)
                {
                    emptied.Add(key);
                }
            }

            // 스텝 뒤: 이번 스텝에 닿은 잎이 0 이 된 쌍을 가린다. 한쪽이라도 잠들었으면 잠든 쌍으로 두고, 아니면 끝이다.
            // 잠든 쌍인데 둘 다 깨어 있고 다시 닿지 않았으면(깨워서 옮겼다) 그때 끝이다.
            void Resolve(const JPH::BodyInterface& bodies)
            {
                std::lock_guard<std::mutex> lock(mutex);
                // 정적 바디는 Jolt 에서 늘 활성이 아니다 - 잠듦은 움직이는 바디에만 따진다.
                const auto asleep = [&bodies](const BodyId& id) {
                    const JPH::BodyID body(id.value);
                    return bodies.IsAdded(body) && bodies.GetMotionType(body) != JPH::EMotionType::Static && false == bodies.IsActive(body);
                };
                for (const UInt64 key : emptied)
                {
                    Pair* pair = pairs.Find(key);
                    if (pair == nullptr || pair->touching != 0)
                    {
                        continue;
                    }
                    if (asleep(pair->event.bodyA) || asleep(pair->event.bodyB))
                    {
                        pair->sleeping = true;
                        continue;
                    }
                    ends.Add(pair->event);
                    pairs.Remove(key);
                }
                emptied.Clear();
                removals.Clear();
                for (auto& entry : pairs)
                {
                    const Pair& pair = entry.MappedValue;
                    if (pair.sleeping && pair.touching == 0 && false == asleep(pair.event.bodyA) && false == asleep(pair.event.bodyB))
                    {
                        ends.Add(pair.event);
                        removals.Add(entry.KeyValue);
                    }
                }
                for (const UInt64 key : removals)
                {
                    pairs.Remove(key);
                }
            }

            // 스텝 뒤의 쓸기가 찾은 "지나친" 쌍이다(D-423). 지금 닿아 있는 쌍이면 이미 사건이 있으니 아무것도 하지 않는다. 아니면 이번 스텝에 시작,
            // 다음 스텝에 끝을 낸다 - 같은 스텝에 끝과 시작을 함께 내면 발송이 끝을 먼저 보내 차례가 뒤집힌다.
            void PassThrough(const JPH::Body& first, const JPH::Body& second)
            {
                std::lock_guard<std::mutex> lock(mutex);
                const UInt64 key = Key(first.GetID(), second.GetID());
                if (pairs.Find(key) != nullptr)
                {
                    return;
                }
                for (const UInt64 known : passed)
                {
                    if (known == key)
                    {
                        return;
                    }
                }
                passed.Add(key);
                ContactEvent event;
                event.bodyA = BodyId{first.GetID().GetIndexAndSequenceNumber()};
                event.bodyB = BodyId{second.GetID().GetIndexAndSequenceNumber()};
                event.userDataA = first.GetUserData();
                event.userDataB = second.GetUserData();
                event.isTrigger = true;
                begins.Add(event);
                pendingEnds.Add(event);
            }

            // 스텝 앞: 지난 사건을 비우고, 그 사이 지운 바디의 끝을 이번 끝으로 넘긴다.
            void BeginStep()
            {
                std::lock_guard<std::mutex> lock(mutex);
                begins.Clear();
                stays.Clear();
                ends.Clear();
                for (const ContactEvent& event : pendingEnds)
                {
                    ends.Add(event);
                }
                pendingEnds.Clear();
                passed.Clear();
                for (auto& entry : pairs)
                {
                    entry.MappedValue.reported = false;
                }
            }

            // 바디를 지우기 전에: 그 바디가 닿아 있던 쌍을 끝으로 알린다. Jolt 는 지운 바디의 접촉을 따로 알리지 않는다.
            void ForgetBody(JPH::BodyID body)
            {
                std::lock_guard<std::mutex> lock(mutex);
                removals.Clear();
                for (auto& entry : pairs)
                {
                    const ContactEvent& event = entry.MappedValue.event;
                    if (event.bodyA.value == body.GetIndexAndSequenceNumber() || event.bodyB.value == body.GetIndexAndSequenceNumber())
                    {
                        pendingEnds.Add(event);
                        removals.Add(entry.KeyValue);
                    }
                }
                for (const UInt64 key : removals)
                {
                    pairs.Remove(key);
                }
            }

            Array<ContactEvent> begins;
            Array<ContactEvent> stays;
            Array<ContactEvent> ends;
            // 잎 표. Impl 이 만들 때 걸어 둔다.
            const LeafTable* leaves = nullptr;

        private:
            static UInt64 Key(JPH::BodyID a, JPH::BodyID b)
            {
                std::uint64_t low = a.GetIndexAndSequenceNumber();
                std::uint64_t high = b.GetIndexAndSequenceNumber();
                if (low > high)
                {
                    const std::uint64_t swap = low;
                    low = high;
                    high = swap;
                }
                return (high << 32) | low;
            }

            // 사건을 처음 만든 쪽(bodyA)에서 본 접촉점과 A→B 법선으로 채운다. Jolt 의 법선은 콜백의 첫 바디에서 둘째로 간다.
            static void Locate(const JPH::Body& first, const JPH::ContactManifold& manifold, ContactEvent& event)
            {
                Vector3 normal = FromJolt(manifold.mWorldSpaceNormal);
                if (first.GetID().GetIndexAndSequenceNumber() != event.bodyA.value)
                {
                    normal = {-normal.x, -normal.y, -normal.z};
                }
                event.normal = normal;
                if (manifold.mRelativeContactPointsOn1.size() > 0)
                {
                    const JPH::RVec3 point = manifold.GetWorldSpaceContactPointOn1(0);
                    event.point = {static_cast<Float>(point.GetX()), static_cast<Float>(point.GetY()), static_cast<Float>(point.GetZ())};
                }
            }

            std::mutex mutex;
            Table<UInt64, Pair> pairs;
            Array<ContactEvent> pendingEnds;
            Array<UInt64> removals;
            Array<UInt64> emptied;
            // 이번 스텝에 "지나쳤다" 로 낸 쌍. 한 쌍을 두 번 내지 않는다.
            Array<UInt64> passed;

            void Mix(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
                JPH::ContactSettings& settings) const
            {
                const LeafInfo* a = leaves->Find(first.GetID(), first.GetShape()->GetSubShapeUserData(manifold.mSubShapeID1));
                const LeafInfo* b = leaves->Find(second.GetID(), second.GetShape()->GetSubShapeUserData(manifold.mSubShapeID2));
                const float frictionA = a != nullptr ? a->friction : first.GetFriction();
                const float restitutionA = a != nullptr ? a->restitution : first.GetRestitution();
                const float frictionB = b != nullptr ? b->friction : second.GetFriction();
                const float restitutionB = b != nullptr ? b->restitution : second.GetRestitution();
                settings.mCombinedFriction = std::sqrt(frictionA * frictionB);
                settings.mCombinedRestitution = restitutionA > restitutionB ? restitutionA : restitutionB;
            }
        };

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
            , singleThreaded(JPH::cMaxPhysicsJobs)
            , broadPhaseLayers(*this)
            , objectVsBroadPhase(*this)
            , objectPairs(*this)
        {
            physics.Init(settings.maxBodies, 0, settings.maxBodyPairs, settings.maxContactConstraints, broadPhaseLayers,
                objectVsBroadPhase, objectPairs);
            physics.SetGravity(ToJolt(settings.gravity));
            contacts.leaves = &leaves;
            physics.SetContactListener(&contacts);
        }

        // 바디의 넓은 판정 층: 잎들의 레이어를 합친 것과 마스크를 합친 것. 잎 쌍마다 다시 거르므로 여기서는 느슨해도 된다 - 넓게 지난 쌍만 묻는다.
        JPH::ObjectLayer ComboFor(ArrayView<const ShapeDesc> shapes, Bool moving)
        {
            std::uint32_t layer = 0;
            std::uint32_t mask = 0;
            for (const ShapeDesc& desc : shapes)
            {
                layer |= desc.layer;
                mask |= desc.mask;
            }
            return FindOrAddCombo(layer, mask, moving);
        }

        // 동적 바디의 질량을 도형의 관성 모양 그대로 `mass` 로 맞춘다. 축 고정도 여기서 먹는다(Jolt 는 둘을 함께 받는다).
        static void ApplyMass(JPH::Body& body, Float mass, JPH::EAllowedDOFs dofs)
        {
            if (false == body.IsDynamic())
            {
                return;
            }
            JPH::MassProperties properties = body.GetShape()->GetMassProperties();
            properties.ScaleToMass(mass > 0.0f ? mass.Get() : 1.0f);
            body.GetMotionProperties()->SetMassProperties(dofs, properties);
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
        // 워커가 0 이면 이것으로 돈다. 워커가 있으면 `pool` 이다(D-419).
        JPH::JobSystemSingleThreaded singleThreaded;
        OwnerPtr<JPH::JobSystemThreadPool> pool;
        UInt32 workerCount = 0;

        JPH::JobSystem* Jobs()
        {
            return pool.Get() != nullptr ? static_cast<JPH::JobSystem*>(pool.Get()) : static_cast<JPH::JobSystem*>(&singleThreaded);
        }
        BroadPhaseLayers broadPhaseLayers;
        ObjectVsBroadPhase objectVsBroadPhase;
        ObjectPairs objectPairs;
        LeafTable leaves;
        // 빠른 트리거의 쓸기(D-423). 스텝 앞의 질량 중심 자세다. 표는 미리 잡아 두어 흔한 경우 할당하지 않는다.
        struct Sweep
        {
            JPH::BodyID body;
            JPH::RMat44 from;
        };
        Array<Sweep> movedTriggers;
        Array<Sweep> movingBodies;
        JPH::BodyIDVector activeScratch;
        ContactTracker contacts;
        JPH::PhysicsSystem physics;

        // 질의가 잎마다 거르는 자리다(D-421). 바디 쌍의 거르기(아래 QueryLayers)를 지난 바디의 잎이 마스크와 겹쳐야 맞는다.
        class QueryShapes final : public JPH::ShapeFilter
        {
        public:
            QueryShapes(const Impl& owner, UInt32 mask) : m_owner(owner), m_mask(mask)
            {
            }

            bool ShouldCollide(const JPH::Shape* shape, const JPH::SubShapeID& subShape) const override
            {
                (void)subShape;
                const LeafInfo* leaf = m_owner.leaves.Find(mBodyID2, shape->GetUserData());
                return leaf == nullptr || (leaf->layer & m_mask) != 0;
            }

            bool ShouldCollide(const JPH::Shape* first, const JPH::SubShapeID& firstSub, const JPH::Shape* second,
                const JPH::SubShapeID& secondSub) const override
            {
                (void)first;
                (void)firstSub;
                return ShouldCollide(second, secondSub);
            }

        private:
            const Impl& m_owner;
            UInt32 m_mask;
        };

        // 질의가 레이어 마스크로 거르는 자리다. 표의 조합 번호에서 그 바디의 레이어를 찾는다.
        class QueryLayers final : public JPH::ObjectLayerFilter
        {
        public:
            QueryLayers(const Impl& owner, UInt32 mask) : m_owner(owner), m_mask(mask)
            {
            }

            bool ShouldCollide(JPH::ObjectLayer layer) const override
            {
                return (m_owner.combos[layer].layer & m_mask) != 0;
            }

        private:
            const Impl& m_owner;
            UInt32 m_mask;
        };

        // ── 빠른 트리거(D-423) ───────────────────────────────────────────
        // 스텝 앞: 깨어 있는 동적인 고체 몸의 질량 중심 자세를 떠 둔다. `MoveContinuously` 가 옮긴 트리거는 그때 이미 떠 두었다.
        void RecordMovingBodies()
        {
            movingBodies.Clear();
            physics.GetActiveBodies(JPH::EBodyType::RigidBody, activeScratch);
            const JPH::BodyLockInterfaceNoLock& locks = physics.GetBodyLockInterfaceNoLock();
            for (const JPH::BodyID& id : activeScratch)
            {
                JPH::BodyLockRead lock(locks, id);
                if (lock.Succeeded() && lock.GetBody().IsDynamic() && false == lock.GetBody().IsSensor())
                {
                    movingBodies.Add(Sweep{id, lock.GetBody().GetCenterOfMassTransform()});
                }
            }
        }

        // 쓸 상대를 고른다: 고체 몸이면 트리거만, 트리거면 고체만. 정적·키네마틱 상대는 트리거가 그것을 볼 때만(`triggerSeesStatic`).
        class SweepTargets final : public JPH::BodyFilter
        {
        public:
            SweepTargets(JPH::BodyID self, Bool sweeperIsTrigger, Bool seesStatic)
                : m_self(self), m_trigger(sweeperIsTrigger), m_seesStatic(seesStatic)
            {
            }

            bool ShouldCollide(const JPH::BodyID& id) const override
            {
                return id != m_self;
            }

            bool ShouldCollideLocked(const JPH::Body& body) const override
            {
                if (m_trigger)
                {
                    return false == body.IsSensor() && (body.IsDynamic() || m_seesStatic);
                }
                return body.IsSensor();
            }

        private:
            JPH::BodyID m_self;
            Bool m_trigger;
            Bool m_seesStatic;
        };

        // 쓸기 하나: 스텝 앞 자세에서 지금 자세까지 그 몸의 도형을 민다. 제 안쪽 반지름보다 덜 갔으면 끝 자리 판정으로 넉넉하다 - 쓸지 않는다.
        void SweepOne(const Sweep& sweep)
        {
            JPH::BodyLockRead lock(physics.GetBodyLockInterfaceNoLock(), sweep.body);
            if (false == lock.Succeeded())
            {
                return;
            }
            const JPH::Body& mover = lock.GetBody();
            const JPH::RMat44 to = mover.GetCenterOfMassTransform();
            const JPH::Vec3 travel = JPH::Vec3(to.GetTranslation() - sweep.from.GetTranslation());
            const float inner = mover.GetShape()->GetInnerRadius();
            if (travel.Length() <= inner)
            {
                return;
            }
            const JPH::RShapeCast cast(mover.GetShape(), JPH::Vec3::sOne(), sweep.from, travel);
            JPH::ShapeCastSettings settings;
            settings.mBackFaceModeConvex = JPH::EBackFaceMode::CollideWithBackFaces;
            JPH::AllHitCollisionCollector<JPH::CastShapeCollector> collector;
            const SweepTargets targets(mover.GetID(), mover.IsSensor(), mover.GetCollideKinematicVsNonDynamic());
            // 바디 쌍은 넓은 판정과 같은 층 거르기를 지난다. 잎마다의 레이어는 지나친 몸의 첫 잎과 이 몸의 잎들을 견준다(아래).
            const JPH::ObjectLayer self = mover.GetObjectLayer();
            class SelfPairs final : public JPH::ObjectLayerFilter
            {
            public:
                SelfPairs(const JPH::ObjectLayerPairFilter& pairs, JPH::ObjectLayer self) : m_pairs(pairs), m_self(self)
                {
                }
                bool ShouldCollide(JPH::ObjectLayer other) const override
                {
                    return m_pairs.ShouldCollide(m_self, other);
                }

            private:
                const JPH::ObjectLayerPairFilter& m_pairs;
                JPH::ObjectLayer m_self;
            };
            const SelfPairs layers(objectPairs, self);
            physics.GetNarrowPhaseQueryNoLock().CastShape(cast, settings, JPH::RVec3::sZero(), collector, {}, layers, targets);
            lock.ReleaseLock();
            for (const JPH::ShapeCastResult& hit : collector.mHits)
            {
                JPH::BodyLockRead first(physics.GetBodyLockInterfaceNoLock(), sweep.body);
                JPH::BodyLockRead second(physics.GetBodyLockInterfaceNoLock(), hit.mBodyID2);
                if (false == first.Succeeded() || false == second.Succeeded())
                {
                    continue;
                }
                // 잎마다의 레이어(D-421): 지나친 잎과 이 몸의 잎 가운데 하나라도 만나야 한다.
                const LeafInfo* other = leaves.Find(hit.mBodyID2, second.GetBody().GetShape()->GetSubShapeUserData(hit.mSubShapeID2));
                const LeafInfo* mine = leaves.Find(sweep.body, first.GetBody().GetShape()->GetSubShapeUserData(hit.mSubShapeID1));
                if (other != nullptr && mine != nullptr && false == leaves.Meet(*mine, *other))
                {
                    continue;
                }
                contacts.PassThrough(first.GetBody(), second.GetBody());
            }
        }

        // 스텝 뒤: 떠 둔 몸과 옮긴 트리거를 쓴다.
        void SweepAfterStep()
        {
            for (const Sweep& sweep : movingBodies)
            {
                SweepOne(sweep);
            }
            for (const Sweep& sweep : movedTriggers)
            {
                SweepOne(sweep);
            }
            movedTriggers.Clear();
        }

        // 맞은 바디의 userData 와 그 자리의 표면 법선을 채운다.
        void FillHit(const JPH::BodyID& id, const JPH::SubShapeID& subShape, JPH::RVec3Arg point, QueryHit& hit) const
        {
            hit.body = BodyId{id.GetIndexAndSequenceNumber()};
            hit.point = {static_cast<Float>(point.GetX()), static_cast<Float>(point.GetY()), static_cast<Float>(point.GetZ())};
            JPH::BodyLockRead lock(physics.GetBodyLockInterfaceNoLock(), id);
            if (lock.Succeeded())
            {
                hit.userData = lock.GetBody().GetUserData();
                hit.normal = FromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(subShape, point));
            }
        }
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
        const ArrayView<const ShapeDesc> shapes = def.shapes.Size() != 0 ? def.shapes : ArrayView<const ShapeDesc>(&def.shape, 1);
        // 메시 도형은 움직이지 않는 바디만 받는다(Jolt 의 메시는 부피가 없다). 어댑터가 움직이는 바디에서는 껍질로 바꿔 넘긴다.
        if (def.type != BodyType::Static && HasMesh(shapes))
        {
            return {};
        }
        JPH::ShapeRefC shape = MakeBodyShape(shapes);
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
        const JPH::ObjectLayer layer = m_impl->ComboFor(shapes, moving);
        if (layer == JPH::cObjectLayerInvalid)
        {
            return {};
        }
        JPH::BodyCreationSettings settings(shape.GetPtr(), JPH::RVec3(def.position.x, def.position.y, def.position.z),
            ToJolt(def.rotation), motion, layer);
        settings.mIsSensor = def.isTrigger;
        // 센서 바디는 키네마틱이라 그대로 두면 동적인 몸과만 겹친다. 움직이는 오브젝트의 트리거는 정적·키네마틱 몸도 보게 한다.
        settings.mCollideKinematicVsNonDynamic = def.isTrigger && def.triggerSeesStatic;
        // 바디의 마찰·반발은 첫 도형의 것이다. 실제 접촉은 `SurfaceMixer` 가 닿은 도형마다 다시 섞는다.
        settings.mFriction = shapes[0].friction;
        settings.mRestitution = shapes[0].restitution;
        settings.mGravityFactor = def.gravityScale;
        settings.mLinearDamping = def.linearDamping;
        settings.mAngularDamping = def.angularDamping;
        settings.mAllowSleeping = def.canSleep;
        settings.mAllowedDOFs = dofs;
        // **이어지는 판정(CCD)은 동적인 몸 모두에 저절로다**(D-425, 2D 의 D-234 와 같다 - 켜는 칸이 없다). Jolt 의 `LinearCast` 는 한 스텝에 제 안쪽 반지름의
        // 일정 몫보다 멀리 가는 몸만 쓸어 보므로, 느린 몸에는 값이 거의 들지 않는다. 트리거·키네마틱은 받지 않는다(트리거는 D-423 의 쓸기가 맡는다).
        settings.mMotionQuality = motion == JPH::EMotionType::Dynamic && false == def.isTrigger ? JPH::EMotionQuality::LinearCast
                                                                                               : JPH::EMotionQuality::Discrete;
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
        m_impl->leaves.Set(id, shapes);
        return BodyId{id.GetIndexAndSequenceNumber()};
    }

    void World::DestroyBody(BodyId body)
    {
        if (false == IsValid(body))
        {
            return;
        }
        m_impl->contacts.ForgetBody(ToJolt(body));
        JPH::BodyInterface& bodyInterface = m_impl->physics.GetBodyInterfaceNoLock();
        bodyInterface.RemoveBody(ToJolt(body));
        bodyInterface.DestroyBody(ToJolt(body));
        m_impl->leaves.Remove(ToJolt(body));
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

    void World::MoveContinuously(BodyId body, const Vector3& position, const Quaternion& rotation)
    {
        if (false == IsValid(body))
        {
            return;
        }
        Bool known = false;
        for (const Impl::Sweep& sweep : m_impl->movedTriggers)
        {
            known = known || sweep.body == ToJolt(body);
        }
        if (false == known)
        {
            m_impl->movedTriggers.Add(Impl::Sweep{ToJolt(body), m_impl->physics.GetBodyInterfaceNoLock().GetCenterOfMassTransform(ToJolt(body))});
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

    void World::SetAwake(BodyId body, Bool awake)
    {
        if (false == IsValid(body))
        {
            return;
        }
        JPH::BodyInterface& bodies = m_impl->physics.GetBodyInterfaceNoLock();
        if (bodies.GetMotionType(ToJolt(body)) == JPH::EMotionType::Static)
        {
            return;
        }
        if (awake)
        {
            bodies.ActivateBody(ToJolt(body));
        }
        else
        {
            bodies.DeactivateBody(ToJolt(body));
        }
    }

    Vector3 World::GetCenterOfMass(BodyId body) const
    {
        if (false == IsValid(body))
        {
            return {};
        }
        const JPH::RVec3 center = m_impl->physics.GetBodyInterfaceNoLock().GetCenterOfMassPosition(ToJolt(body));
        return {static_cast<Float>(center.GetX()), static_cast<Float>(center.GetY()), static_cast<Float>(center.GetZ())};
    }

    void World::ApplyForce(BodyId body, const Vector3& force)
    {
        if (IsValid(body))
        {
            m_impl->physics.GetBodyInterfaceNoLock().AddForce(ToJolt(body), ToJolt(force), JPH::EActivation::Activate);
        }
    }

    void World::ApplyTorque(BodyId body, const Vector3& torque)
    {
        if (IsValid(body))
        {
            m_impl->physics.GetBodyInterfaceNoLock().AddTorque(ToJolt(body), ToJolt(torque), JPH::EActivation::Activate);
        }
    }

    void World::ApplyImpulse(BodyId body, const Vector3& impulse)
    {
        if (IsValid(body))
        {
            m_impl->physics.GetBodyInterfaceNoLock().AddImpulse(ToJolt(body), ToJolt(impulse));
        }
    }

    void World::ApplyAngularImpulse(BodyId body, const Vector3& impulse)
    {
        if (IsValid(body))
        {
            m_impl->physics.GetBodyInterfaceNoLock().AddAngularImpulse(ToJolt(body), ToJolt(impulse));
        }
    }

    UInt64 World::GetUserData(BodyId body) const
    {
        if (false == IsValid(body))
        {
            return 0;
        }
        return m_impl->physics.GetBodyInterfaceNoLock().GetUserData(ToJolt(body));
    }

    Bool World::SetShapes(BodyId body, ArrayView<const ShapeDesc> shapes)
    {
        if (false == IsValid(body))
        {
            return false;
        }
        if (HasMesh(shapes) && m_impl->physics.GetBodyInterfaceNoLock().GetMotionType(ToJolt(body)) != JPH::EMotionType::Static)
        {
            return false;
        }
        JPH::ShapeRefC shape = MakeBodyShape(shapes);
        if (shape == nullptr)
        {
            return false;
        }
        // 잎 표를 먼저 바꾼다 - 새 도형의 잎 번호가 곧 이 표의 자리다. 넓은 판정 층도 새 잎들의 레이어로.
        m_impl->leaves.Set(ToJolt(body), shapes);
        const Bool moving = m_impl->physics.GetBodyInterfaceNoLock().GetMotionType(ToJolt(body)) != JPH::EMotionType::Static;
        const JPH::ObjectLayer layer = m_impl->ComboFor(shapes, moving);
        if (layer != JPH::cObjectLayerInvalid)
        {
            m_impl->physics.GetBodyInterfaceNoLock().SetObjectLayer(ToJolt(body), layer);
        }
        // 질량은 Jolt 가 밀도로 다시 셈하지 않게 하고(false), 지금의 질량을 새 도형의 관성 모양으로 다시 나눈다.
        m_impl->physics.GetBodyInterfaceNoLock().SetShape(ToJolt(body), shape.GetPtr(), false, JPH::EActivation::Activate);
        JPH::BodyLockWrite lock(m_impl->physics.GetBodyLockInterfaceNoLock(), ToJolt(body));
        if (lock.Succeeded() && lock.GetBody().IsDynamic())
        {
            JPH::Body& locked = lock.GetBody();
            const float mass = locked.GetMotionProperties()->GetInverseMass() > 0.0f ? 1.0f / locked.GetMotionProperties()->GetInverseMass() : 1.0f;
            Impl::ApplyMass(locked, mass, locked.GetMotionProperties()->GetAllowedDOFs());
        }
        return true;
    }

    void World::SetBodyProperties(BodyId body, const BodyDef& def)
    {
        if (false == IsValid(body))
        {
            return;
        }
        JPH::BodyInterface& bodyInterface = m_impl->physics.GetBodyInterfaceNoLock();
        {
            JPH::BodyLockWrite lock(m_impl->physics.GetBodyLockInterfaceNoLock(), ToJolt(body));
            if (false == lock.Succeeded())
            {
                return;
            }
            JPH::Body& locked = lock.GetBody();
            locked.SetAllowSleeping(def.canSleep);
            if (false == locked.IsStatic())
            {
                JPH::MotionProperties* motion = locked.GetMotionProperties();
                motion->SetGravityFactor(def.gravityScale);
                motion->SetLinearDamping(def.linearDamping);
                motion->SetAngularDamping(def.angularDamping);
                JPH::EAllowedDOFs dofs = AllowedDOFs(def);
                if (dofs == JPH::EAllowedDOFs::None)
                {
                    // 만들 때는 키네마틱으로 받았다. 제자리에서는 종류를 바꾸지 않으므로 모두 막힌 것을 받을 수 없다 - 어댑터가 다시 만든다.
                    dofs = JPH::EAllowedDOFs::All;
                }
                Impl::ApplyMass(locked, def.mass, dofs);
            }
        }
        const Bool moving = bodyInterface.GetMotionType(ToJolt(body)) != JPH::EMotionType::Static;
        // 성질을 바꾸면 깨운다(2D 와 같다) - 중력을 다시 켜도 잠든 채면 떨어지지 않는다.
        if (moving)
        {
            bodyInterface.ActivateBody(ToJolt(body));
        }
    }

    void World::SetIgnoredLayers(const UInt32 (&rows)[32])
    {
        m_impl->leaves.SetIgnored(rows);
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
        m_impl->contacts.BeginStep();
        m_impl->RecordMovingBodies();
        m_impl->physics.Update(deltaTime, 1, &m_impl->temp, m_impl->Jobs());
        m_impl->contacts.Resolve(m_impl->physics.GetBodyInterfaceNoLock());
        m_impl->SweepAfterStep();
    }

    void World::SetWorkerCount(UInt32 count)
    {
        const UInt32 clamped = count < MaxWorkerCount ? count : MaxWorkerCount;
        if (clamped == m_impl->workerCount)
        {
            return;
        }
        // 풀은 스텝 밖(메인 스레드)에서만 세우고 허문다. 허무는 동안 워커가 끝나기를 기다린다.
        m_impl->pool.Reset();
        if (clamped != 0)
        {
            m_impl->pool = MakeOwnerPtr<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(clamped.Get()));
        }
        m_impl->workerCount = clamped;
    }

    UInt32 World::GetWorkerCount() const
    {
        return m_impl->workerCount;
    }

    UInt32 RecommendWorkerCount(UInt32 work, UInt32 hardwareThreads)
    {
        constexpr UInt32 WorkPerWorker = 256;
        if (work < WorkPerWorker || hardwareThreads <= 1)
        {
            return 0;
        }
        UInt32 workers = work / WorkPerWorker;
        const UInt32 spare = hardwareThreads - 1;
        workers = workers < spare ? workers : spare;
        return workers < MaxWorkerCount ? workers : MaxWorkerCount;
    }

    ArrayView<const ContactEvent> World::GetBeginEvents() const
    {
        return m_impl->contacts.begins.View();
    }

    ArrayView<const ContactEvent> World::GetStayEvents() const
    {
        return m_impl->contacts.stays.View();
    }

    ArrayView<const ContactEvent> World::GetEndEvents() const
    {
        return m_impl->contacts.ends.View();
    }

    namespace
    {
        // 방향과 거리를 Jolt 가 받는 "끝까지의 벡터" 로. 쓸 수 없으면 거짓이다.
        Bool SweepVector(const Vector3& direction, Float distance, JPH::Vec3& sweep)
        {
            const JPH::Vec3 raw = ToJolt(direction);
            const float length = raw.Length();
            if (false == (length > 1.0e-6f) || false == (distance > 0.0f) || false == std::isfinite(distance.Get()))
            {
                return false;
            }
            sweep = raw * (distance.Get() / length);
            return true;
        }

        // 질의 하나에서 바디마다 한 번만 담는다.
        void AddUnique(Array<UInt64>& userData, Array<JPH::uint32>& seen, const JPH::BodyID& id, UInt64 data)
        {
            const JPH::uint32 raw = id.GetIndexAndSequenceNumber();
            for (const JPH::uint32 known : seen)
            {
                if (known == raw)
                {
                    return;
                }
            }
            seen.Add(raw);
            userData.Add(data);
        }
    }

    Bool World::Raycast(const Vector3& origin, const Vector3& direction, Float distance, UInt32 layerMask, QueryHit& hit) const
    {
        hit = {};
        JPH::Vec3 sweep;
        if (false == SweepVector(direction, distance, sweep))
        {
            return false;
        }
        const JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z), sweep);
        // 설정을 받는 길로 쏜다: 볼록 도형은 속이 찬 것(안에서 쏘면 그 자리에서 맞는다), 삼각형 메시는 겉면뿐이라 뒷면을 맞지 않는다(D-418).
        // 설정 없는 길은 삼각형의 뒷면도 맞아, 메시 상자 안에서 쏜 반직선이 윗면 안쪽에 걸렸다 - `RaycastAll` 과도 달랐다.
        JPH::RayCastSettings settings;
        settings.mBackFaceModeTriangles = JPH::EBackFaceMode::IgnoreBackFaces;
        settings.mTreatConvexAsSolid = true;
        JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
        const Impl::QueryLayers layers(*m_impl, layerMask);
        const Impl::QueryShapes leafFilter(*m_impl, layerMask);
        m_impl->physics.GetNarrowPhaseQueryNoLock().CastRay(ray, settings, collector, {}, layers, {}, leafFilter);
        if (false == collector.HadHit())
        {
            return false;
        }
        const JPH::RayCastResult& result = collector.mHit;
        m_impl->FillHit(result.mBodyID, result.mSubShapeID2, ray.GetPointOnRay(result.mFraction), hit);
        hit.distance = result.mFraction * distance;
        return true;
    }

    void World::RaycastAll(const Vector3& origin, const Vector3& direction, Float distance, UInt32 layerMask, Array<QueryHit>& hits) const
    {
        hits.Clear();
        JPH::Vec3 sweep;
        if (false == SweepVector(direction, distance, sweep))
        {
            return;
        }
        const JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z), sweep);
        JPH::RayCastSettings settings;
        // 뒷면은 맞지 않는다 - 상자 안에서 쏘면 그 상자의 안쪽 면에 걸리지 않는다.
        settings.mBackFaceModeConvex = JPH::EBackFaceMode::IgnoreBackFaces;
        JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
        const Impl::QueryLayers layers(*m_impl, layerMask);
        const Impl::QueryShapes leafFilter(*m_impl, layerMask);
        m_impl->physics.GetNarrowPhaseQueryNoLock().CastRay(ray, settings, collector, {}, layers, {}, leafFilter);
        collector.Sort();
        for (const JPH::RayCastResult& result : collector.mHits)
        {
            // 한 바디의 잎 여럿에 맞으면 가장 가까운 것 하나다.
            Bool seen = false;
            for (const QueryHit& known : hits)
            {
                seen = seen || known.body.value == result.mBodyID.GetIndexAndSequenceNumber();
            }
            if (seen)
            {
                continue;
            }
            QueryHit hit;
            m_impl->FillHit(result.mBodyID, result.mSubShapeID2, ray.GetPointOnRay(result.mFraction), hit);
            hit.distance = result.mFraction * distance;
            hits.Add(hit);
        }
    }

    void World::OverlapSphere(const Vector3& center, Float radius, UInt32 layerMask, Array<UInt64>& userData) const
    {
        userData.Clear();
        if (false == (radius > 0.0f))
        {
            return;
        }
        const JPH::SphereShape sphere(radius);
        JPH::CollideShapeSettings settings;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        const Impl::QueryLayers layers(*m_impl, layerMask);
        const Impl::QueryShapes leafFilter(*m_impl, layerMask);
        m_impl->physics.GetNarrowPhaseQueryNoLock().CollideShape(&sphere, JPH::Vec3::sOne(),
            JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z)), settings, JPH::RVec3::sZero(), collector, {}, layers, {}, leafFilter);
        Array<JPH::uint32> seen;
        for (const JPH::CollideShapeResult& result : collector.mHits)
        {
            JPH::BodyLockRead lock(m_impl->physics.GetBodyLockInterfaceNoLock(), result.mBodyID2);
            if (lock.Succeeded())
            {
                AddUnique(userData, seen, result.mBodyID2, lock.GetBody().GetUserData());
            }
        }
    }

    void World::OverlapBox(const Vector3& center, const Vector3& halfExtents, const Quaternion& rotation, UInt32 layerMask,
        Array<UInt64>& userData) const
    {
        userData.Clear();
        if (false == (halfExtents.x > 0.0f && halfExtents.y > 0.0f && halfExtents.z > 0.0f))
        {
            return;
        }
        const JPH::BoxShape box(ToJolt(halfExtents), 0.0f);
        JPH::CollideShapeSettings settings;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        const Impl::QueryLayers layers(*m_impl, layerMask);
        const Impl::QueryShapes leafFilter(*m_impl, layerMask);
        m_impl->physics.GetNarrowPhaseQueryNoLock().CollideShape(&box, JPH::Vec3::sOne(),
            JPH::RMat44::sRotationTranslation(ToJolt(rotation), JPH::RVec3(center.x, center.y, center.z)), settings, JPH::RVec3::sZero(),
            collector, {}, layers, {}, leafFilter);
        Array<JPH::uint32> seen;
        for (const JPH::CollideShapeResult& result : collector.mHits)
        {
            JPH::BodyLockRead lock(m_impl->physics.GetBodyLockInterfaceNoLock(), result.mBodyID2);
            if (lock.Succeeded())
            {
                AddUnique(userData, seen, result.mBodyID2, lock.GetBody().GetUserData());
            }
        }
    }

    namespace
    {
        // 구현 타입을 템플릿 인자로 받는다 - `World::Impl` 은 World 의 비공개 이름이라 밖의 함수가 이름을 쓸 수 없다.
        template<typename ImplT>
        Bool CastOne(const ImplT& impl, const JPH::Shape& shape, JPH::RMat44Arg start,
            const JPH::Vec3& sweep, Float distance, UInt32 layerMask, QueryHit& hit)
        {
            const JPH::RShapeCast cast(&shape, JPH::Vec3::sOne(), start, sweep);
            JPH::ShapeCastSettings settings;
            settings.mBackFaceModeConvex = JPH::EBackFaceMode::IgnoreBackFaces;
            JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
            const typename ImplT::QueryLayers layers(impl, layerMask);
            const typename ImplT::QueryShapes leafFilter(impl, layerMask);
            impl.physics.GetNarrowPhaseQueryNoLock().CastShape(cast, settings, JPH::RVec3::sZero(), collector, {}, layers, {}, leafFilter);
            if (false == collector.HadHit())
            {
                return false;
            }
            const JPH::ShapeCastResult& result = collector.mHit;
            impl.FillHit(result.mBodyID2, result.mSubShapeID2, JPH::RVec3(result.mContactPointOn2), hit);
            // 맞은 순간의 법선은 밀어낸 방향의 반대다(쏜 쪽을 향한다).
            const JPH::Vec3 axis = result.mPenetrationAxis;
            if (axis.LengthSq() > 1.0e-12f)
            {
                hit.normal = FromJolt(-axis.Normalized());
            }
            hit.distance = result.mFraction * distance;
            return true;
        }
    }

    Bool World::SphereCast(const Vector3& origin, Float radius, const Vector3& direction, Float distance, UInt32 layerMask, QueryHit& hit) const
    {
        hit = {};
        JPH::Vec3 sweep;
        if (false == (radius > 0.0f) || false == SweepVector(direction, distance, sweep))
        {
            return false;
        }
        const JPH::SphereShape sphere(radius);
        return CastOne(*m_impl, sphere, JPH::RMat44::sTranslation(JPH::RVec3(origin.x, origin.y, origin.z)), sweep,
            distance, layerMask, hit);
    }

    Bool World::BoxCast(const Vector3& center, const Vector3& halfExtents, const Quaternion& rotation, const Vector3& direction,
        Float distance, UInt32 layerMask, QueryHit& hit) const
    {
        hit = {};
        JPH::Vec3 sweep;
        if (false == (halfExtents.x > 0.0f && halfExtents.y > 0.0f && halfExtents.z > 0.0f) || false == SweepVector(direction, distance, sweep))
        {
            return false;
        }
        const JPH::BoxShape box(ToJolt(halfExtents), 0.0f);
        return CastOne(*m_impl, box,
            JPH::RMat44::sRotationTranslation(ToJolt(rotation), JPH::RVec3(center.x, center.y, center.z)), sweep, distance, layerMask, hit);
    }
}
