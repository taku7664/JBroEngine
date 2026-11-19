#include <JBro/Framework3DSystem/System/Physics3DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework3D/Component/Physics3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Physics3D/World.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Table.h>

#include <cmath>
#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::System
{
    namespace
    {
        // 바뀐 것만 커널에 다시 먹이려고 값을 섞어 둔 지문이다(2D 어댑터와 같은 FNV-1a).
        struct Fingerprint
        {
            UInt64 value = 14695981039346656037ull;

            void Mix(const void* data, std::size_t size)
            {
                const unsigned char* bytes = static_cast<const unsigned char*>(data);
                for (std::size_t i = 0; i < size; ++i)
                {
                    value = (value ^ bytes[i]) * 1099511628211ull;
                }
            }

            void Mix(Float number)
            {
                // -0 과 +0 은 같은 값이다. 비트로 섞으면 달라 보여 도형을 괜히 다시 만든다.
                const float normalized = number == 0.0f ? 0.0f : number.Get();
                Mix(&normalized, sizeof(normalized));
            }

            void Mix(const Vector3& vector)
            {
                Mix(vector.x);
                Mix(vector.y);
                Mix(vector.z);
            }

            void Mix(Bool flag)
            {
                const std::uint8_t byte = flag ? 1 : 0;
                Mix(&byte, sizeof(byte));
            }

            void Mix(UInt32 number)
            {
                const std::uint32_t raw = number;
                Mix(&raw, sizeof(raw));
            }
        };

        // 오브젝트의 월드 자세다. `Transform3DSystem` 과 같은 식으로 부모를 거슬러 겹친다 - 고정 스텝은 그 시스템보다 먼저 돌아
        // 월드 캐시가 지난 프레임의 것이거나 비어 있을 수 있다.
        struct Pose
        {
            Vector3 position;
            Quaternion rotation;
            Vector3 scale{1.0f, 1.0f, 1.0f};
        };

        Bool WorldPose(Canvas& canvas, Object::GameObject* object, Pose& pose)
        {
            if (object == nullptr)
            {
                return false;
            }
            const Component::Transform3D* transform = canvas.FindComponentRaw<Component::Transform3D>(object);
            if (transform == nullptr)
            {
                return false;
            }
            Pose parent;
            if (WorldPose(canvas, object->GetParent(), parent))
            {
                pose.position = Add(parent.position, Rotate(parent.rotation, Multiply(transform->position, parent.scale)));
                pose.rotation = Normalize(Multiply(parent.rotation, transform->rotation));
                pose.scale = Multiply(parent.scale, transform->scale);
                return true;
            }
            pose.position = transform->position;
            pose.rotation = Normalize(transform->rotation);
            pose.scale = transform->scale;
            return true;
        }

        Float Largest(Float a, Float b)
        {
            return a > b ? a : b;
        }

        // 콜라이더를 커널의 도형으로 굽는다. 크기는 월드 크기를 곱한다(2D 와 같다).
        Physics3D::ShapeDesc BakeShape(const Component::Collider3D& collider, const Vector3& scale)
        {
            const Vector3 magnitude{std::fabs(scale.x), std::fabs(scale.y), std::fabs(scale.z)};
            Physics3D::ShapeDesc desc;
            desc.offset = Multiply(collider.offset, scale);
            desc.friction = collider.friction;
            desc.restitution = collider.restitution;
            switch (collider.shape)
            {
            case Component::ColliderShape3D::Box:
                desc.type = Physics3D::ShapeType::Box;
                desc.halfExtents = {std::fabs(collider.size.x * magnitude.x) * 0.5f, std::fabs(collider.size.y * magnitude.y) * 0.5f,
                    std::fabs(collider.size.z * magnitude.z) * 0.5f};
                break;
            case Component::ColliderShape3D::Sphere:
                desc.type = Physics3D::ShapeType::Sphere;
                desc.radius = collider.radius * Largest(magnitude.x, Largest(magnitude.y, magnitude.z));
                break;
            case Component::ColliderShape3D::Capsule:
            {
                // 둥근 쪽은 X·Z 의 큰 크기를, 길이는 Y 크기를 따른다. 반지름 두 배보다 짧으면 원통이 없는 구다.
                const Float radius = collider.radius * Largest(magnitude.x, magnitude.z);
                const Float halfHeight = collider.height * magnitude.y * 0.5f - radius;
                if (halfHeight > 0.0f)
                {
                    desc.type = Physics3D::ShapeType::Capsule;
                    desc.radius = radius;
                    desc.halfHeight = halfHeight;
                }
                else
                {
                    desc.type = Physics3D::ShapeType::Sphere;
                    desc.radius = radius;
                }
                break;
            }
            }
            return desc;
        }

        void MixShape(Fingerprint& print, const Physics3D::ShapeDesc& desc)
        {
            const std::uint8_t type = static_cast<std::uint8_t>(desc.type);
            print.Mix(&type, sizeof(type));
            print.Mix(desc.halfExtents);
            print.Mix(desc.radius);
            print.Mix(desc.halfHeight);
            print.Mix(desc.offset);
            print.Mix(desc.friction);
            print.Mix(desc.restitution);
        }

        Physics3D::BodyType ToKernel(Component::BodyType3D type)
        {
            switch (type)
            {
            case Component::BodyType3D::Static:
                return Physics3D::BodyType::Static;
            case Component::BodyType3D::Kinematic:
                return Physics3D::BodyType::Kinematic;
            case Component::BodyType3D::Dynamic:
                return Physics3D::BodyType::Dynamic;
            }
            return Physics3D::BodyType::Static;
        }

        // 바디의 성질(제자리에서 바뀌는 것). 레이어·마스크는 첫 고체 콜라이더의 것이다.
        void FillProperties(const Component::Rigidbody3D* rigidbody, UInt32 layer, UInt32 mask, Physics3D::BodyDef& def)
        {
            def.layer = layer;
            def.mask = mask;
            if (rigidbody == nullptr)
            {
                return;
            }
            def.mass = rigidbody->mass;
            def.gravityScale = rigidbody->gravityScale;
            def.linearDamping = rigidbody->linearDamping;
            def.angularDamping = rigidbody->angularDamping;
            def.freezePositionX = rigidbody->freezePositionX;
            def.freezePositionY = rigidbody->freezePositionY;
            def.freezePositionZ = rigidbody->freezePositionZ;
            def.freezeRotationX = rigidbody->freezeRotationX;
            def.freezeRotationY = rigidbody->freezeRotationY;
            def.freezeRotationZ = rigidbody->freezeRotationZ;
            def.canSleep = rigidbody->canSleep;
        }

        UInt64 PropertiesPrint(const Physics3D::BodyDef& def)
        {
            Fingerprint print;
            print.Mix(def.mass);
            print.Mix(def.gravityScale);
            print.Mix(def.linearDamping);
            print.Mix(def.angularDamping);
            print.Mix(def.freezePositionX);
            print.Mix(def.freezePositionY);
            print.Mix(def.freezePositionZ);
            print.Mix(def.freezeRotationX);
            print.Mix(def.freezeRotationY);
            print.Mix(def.freezeRotationZ);
            print.Mix(def.canSleep);
            print.Mix(def.layer);
            print.Mix(def.mask);
            return print.value;
        }

        Bool SameVector(const Vector3& a, const Vector3& b)
        {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }

        Bool SameRotation(const Quaternion& a, const Quaternion& b)
        {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
        }
    }

    struct Physics3DSystem::State
    {
        // 한 오브젝트의 고체 콜라이더를 모으는 자리다. 오브젝트마다 하나를 두고 스텝마다 비워 다시 쓴다 - 배열이 자리를 지켜 할당하지 않는다.
        struct Group
        {
            Array<Physics3D::ShapeDesc> shapes;
            UInt32 layer = 0x00000001u;
            UInt32 mask = 0xFFFFFFFFu;
            Object::GameObject* object = nullptr;
            Pose pose;
        };

        struct BodyLink
        {
            Physics3D::BodyId body;
            Physics3D::BodyType type = Physics3D::BodyType::Static;
            InstanceId rigidbody = InvalidInstanceId;
            UInt64 shapes = 0;
            UInt64 properties = 0;
            // 지난 스텝에 이쪽에서 쓴 값. 스크립트나 에디터가 그 사이 바꿨는지를 이것과 견주어 안다.
            Vector3 writtenPosition;
            Quaternion writtenRotation;
            Vector3 writtenLinearVelocity;
            Vector3 writtenAngularVelocity;
            // 정적인 몸을 마지막으로 옮겨 둔 월드 자세.
            Vector3 pushedPosition;
            Quaternion pushedRotation;
            Bool seen = false;
            // 이번 스텝 안에서만 유효하다.
            Object::GameObject* object = nullptr;
            Component::Transform3D* transform = nullptr;
            Component::Rigidbody3D* rigidbodyComponent = nullptr;
        };

        // 트리거 콜라이더 하나가 센서 바디 하나다. 오브젝트의 자세를 따라간다.
        struct SensorLink
        {
            Physics3D::BodyId body;
            InstanceId object = InvalidInstanceId;
            UInt64 signature = 0;
            Bool seen = false;
        };

        Physics3D::World world;
        Table<InstanceId, Group> groups;
        Table<InstanceId, BodyLink> bodies;
        Table<InstanceId, SensorLink> sensors;
        Array<InstanceId> removals;
    };

    Physics3DSystem::Physics3DSystem() : m_state(MakeOwnerPtr<State>())
    {
    }

    Physics3DSystem::~Physics3DSystem() = default;

    Int32 Physics3DSystem::GetExecutionOrder() const
    {
        return 200;
    }

    void Physics3DSystem::SetGravity(const Vector3& gravity)
    {
        m_gravity = gravity;
    }

    Vector3 Physics3DSystem::GetGravity() const
    {
        return m_gravity;
    }

    std::size_t Physics3DSystem::GetBodyCount() const
    {
        return m_state->bodies.Size();
    }

    std::size_t Physics3DSystem::GetSensorCount() const
    {
        return m_state->sensors.Size();
    }

    void Physics3DSystem::OnInitialize(Canvas& canvas)
    {
        (void)canvas;
        m_state = MakeOwnerPtr<State>();
    }

    void Physics3DSystem::OnShutdown(Canvas& canvas)
    {
        (void)canvas;
        // 커널과 연결을 모두 버린다. 다시 켜면 처음부터 만든다(2D 와 같다).
        m_state = MakeOwnerPtr<State>();
    }

    void Physics3DSystem::OnFixedUpdate(Canvas& canvas, Float fixedDeltaTime)
    {
        if (false == (fixedDeltaTime > 0.0f))
        {
            return;
        }
        State& state = *m_state;
        Physics3D::World& world = state.world;

        for (auto& entry : state.groups)
        {
            entry.MappedValue.shapes.Clear();
            entry.MappedValue.object = nullptr;
        }
        for (auto& entry : state.bodies)
        {
            entry.MappedValue.seen = false;
            entry.MappedValue.object = nullptr;
            entry.MappedValue.transform = nullptr;
            entry.MappedValue.rigidbodyComponent = nullptr;
        }
        for (auto& entry : state.sensors)
        {
            entry.MappedValue.seen = false;
        }

        // ── 1. 콜라이더를 오브젝트마다 모은다. 트리거는 저마다 센서다. ───────────────
        canvas.ForEach<Component::Collider3D>([&](Component::Collider3D& collider) {
            if (false == collider.IsActiveComponent())
            {
                return;
            }
            Object::GameObject* object = Internal::CanvasAccess::GetOwner(collider);
            Pose pose;
            if (false == WorldPose(canvas, object, pose))
            {
                return;
            }
            const Physics3D::ShapeDesc desc = BakeShape(collider, pose.scale);
            if (collider.isTrigger)
            {
                Fingerprint print;
                MixShape(print, desc);
                print.Mix(UInt32(collider.layer));
                print.Mix(UInt32(collider.mask));
                const InstanceId colliderId = collider.GetInstanceId();
                State::SensorLink* link = state.sensors.Find(colliderId);
                if (link != nullptr && (link->signature != print.value || link->object != object->GetInstanceId()
                                           || false == world.IsValid(link->body)))
                {
                    world.DestroyBody(link->body);
                    state.sensors.Remove(colliderId);
                    link = nullptr;
                }
                if (link == nullptr)
                {
                    Physics3D::BodyDef def;
                    def.type = Physics3D::BodyType::Kinematic;
                    def.shape = desc;
                    def.isTrigger = true;
                    def.position = pose.position;
                    def.rotation = pose.rotation;
                    def.layer = collider.layer;
                    def.mask = collider.mask;
                    def.userData = colliderId;
                    State::SensorLink fresh;
                    fresh.body = world.CreateBody(def);
                    fresh.object = object->GetInstanceId();
                    fresh.signature = print.value;
                    state.sensors.TryAdd(colliderId, fresh);
                    link = state.sensors.Find(colliderId);
                }
                else
                {
                    // 센서는 오브젝트를 따라간다. 움직이는 몸의 자세는 지난 스텝의 되쓰기에서 왔다.
                    world.SetTransform(link->body, pose.position, pose.rotation);
                }
                link->seen = true;
                return;
            }
            State::Group* group = state.groups.Find(object->GetInstanceId());
            if (group == nullptr)
            {
                state.groups.TryAdd(object->GetInstanceId(), State::Group{});
                group = state.groups.Find(object->GetInstanceId());
            }
            if (group->shapes.Size() == 0)
            {
                // 레이어는 바디마다 하나라 첫 고체 콜라이더의 것을 쓴다.
                group->layer = collider.layer;
                group->mask = collider.mask;
            }
            group->shapes.Add(desc);
            group->object = object;
            group->pose = pose;
        });

        // ── 2. 바디를 맞춘다. 한 오브젝트에 바디 하나다. ─────────────────────────────
        for (auto& entry : state.groups)
        {
            State::Group& group = entry.MappedValue;
            if (group.object == nullptr || group.shapes.Size() == 0)
            {
                continue;
            }
            Object::GameObject* object = group.object;
            const InstanceId objectId = entry.KeyValue;
            Component::Transform3D* transform = canvas.FindComponentRaw<Component::Transform3D>(object);
            Component::Rigidbody3D* rigidbody = canvas.FindComponentRaw<Component::Rigidbody3D>(object);
            if (rigidbody != nullptr && false == rigidbody->IsActiveComponent())
            {
                rigidbody = nullptr;
            }
            const Physics3D::BodyType type = rigidbody != nullptr ? ToKernel(rigidbody->bodyType) : Physics3D::BodyType::Static;
            const InstanceId rigidbodyId = rigidbody != nullptr ? rigidbody->GetInstanceId() : InvalidInstanceId;
            Physics3D::BodyDef properties;
            FillProperties(rigidbody, group.layer, group.mask, properties);
            const UInt64 propertiesPrint = PropertiesPrint(properties);
            Fingerprint shapesPrint;
            for (const Physics3D::ShapeDesc& desc : group.shapes)
            {
                MixShape(shapesPrint, desc);
            }

            State::BodyLink* link = state.bodies.Find(objectId);
            // 종류가 바뀌면 다시 만든다. 도형·성질은 제자리에서 바꾼다 - 다시 만들면 닿아 있던 쌍이 끝나고 다시 시작한다.
            if (link != nullptr && (link->type != type || link->rigidbody != rigidbodyId || false == world.IsValid(link->body)))
            {
                world.DestroyBody(link->body);
                state.bodies.Remove(objectId);
                link = nullptr;
            }
            const Pose& pose = group.pose;
            if (link == nullptr)
            {
                Physics3D::BodyDef def = properties;
                def.type = type;
                def.shapes = group.shapes.View();
                def.position = pose.position;
                def.rotation = pose.rotation;
                if (rigidbody != nullptr)
                {
                    def.linearVelocity = rigidbody->linearVelocity;
                    def.angularVelocity = rigidbody->angularVelocity;
                }
                def.userData = objectId;
                State::BodyLink fresh;
                fresh.body = world.CreateBody(def);
                fresh.type = type;
                fresh.rigidbody = rigidbodyId;
                fresh.shapes = shapesPrint.value;
                fresh.properties = propertiesPrint;
                fresh.writtenPosition = transform->position;
                fresh.writtenRotation = transform->rotation;
                fresh.writtenLinearVelocity = def.linearVelocity;
                fresh.writtenAngularVelocity = def.angularVelocity;
                fresh.pushedPosition = pose.position;
                fresh.pushedRotation = pose.rotation;
                state.bodies.TryAdd(objectId, fresh);
                link = state.bodies.Find(objectId);
            }
            else
            {
                if (link->shapes != shapesPrint.value)
                {
                    world.SetShapes(link->body, group.shapes.View());
                    link->shapes = shapesPrint.value;
                }
                if (link->properties != propertiesPrint)
                {
                    world.SetBodyProperties(link->body, properties);
                    link->properties = propertiesPrint;
                }
                if (type == Physics3D::BodyType::Static)
                {
                    // 정적인 몸은 에디터·스크립트가 옮긴 자리로 따라간다.
                    if (false == SameVector(pose.position, link->pushedPosition) || false == SameRotation(pose.rotation, link->pushedRotation))
                    {
                        world.SetTransform(link->body, pose.position, pose.rotation);
                        link->pushedPosition = pose.position;
                        link->pushedRotation = pose.rotation;
                    }
                }
                else
                {
                    // 움직이는 몸의 자세는 커널이 주인이다. 지난번에 쓴 값과 다르면 그 사이 누가 옮긴 것이므로 따른다.
                    if (false == SameVector(transform->position, link->writtenPosition)
                        || false == SameRotation(transform->rotation, link->writtenRotation))
                    {
                        world.SetTransform(link->body, pose.position, pose.rotation);
                    }
                    if (false == SameVector(rigidbody->linearVelocity, link->writtenLinearVelocity))
                    {
                        world.SetLinearVelocity(link->body, rigidbody->linearVelocity);
                    }
                    if (false == SameVector(rigidbody->angularVelocity, link->writtenAngularVelocity))
                    {
                        world.SetAngularVelocity(link->body, rigidbody->angularVelocity);
                    }
                }
            }
            link->seen = true;
            link->object = object;
            link->transform = transform;
            link->rigidbodyComponent = rigidbody;
        }

        // ── 3. 사라진 것을 지운다. ──────────────────────────────────────────────────
        state.removals.Clear();
        for (auto& entry : state.bodies)
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
        state.removals.Clear();
        for (auto& entry : state.sensors)
        {
            if (false == entry.MappedValue.seen)
            {
                state.removals.Add(entry.KeyValue);
            }
        }
        for (const InstanceId id : state.removals)
        {
            world.DestroyBody(state.sensors.Find(id)->body);
            state.sensors.Remove(id);
        }
        // 콜라이더가 모두 사라진 오브젝트의 모으는 자리도 버린다 - 지운 오브젝트의 자리가 쌓이지 않게.
        state.removals.Clear();
        for (auto& entry : state.groups)
        {
            if (entry.MappedValue.object == nullptr)
            {
                state.removals.Add(entry.KeyValue);
            }
        }
        for (const InstanceId id : state.removals)
        {
            state.groups.Remove(id);
        }

        // ── 4. 스텝 ─────────────────────────────────────────────────────────────────
        world.SetGravity(m_gravity);
        world.Step(fixedDeltaTime);

        // ── 5. 되쓰기 ───────────────────────────────────────────────────────────────
        for (auto& entry : state.bodies)
        {
            State::BodyLink& link = entry.MappedValue;
            if (link.type == Physics3D::BodyType::Static || link.transform == nullptr || link.object == nullptr)
            {
                continue;
            }
            const Vector3 position = world.GetPosition(link.body);
            const Quaternion rotation = world.GetRotation(link.body);
            Vector3 localPosition = position;
            Quaternion localRotation = rotation;
            Pose parent;
            if (WorldPose(canvas, link.object->GetParent(), parent))
            {
                // 월드 자세를 부모의 것으로 되돌린다: 회전을 풀고 크기로 나눈다.
                const Quaternion inverse = Conjugate(parent.rotation);
                const Vector3 local = Rotate(inverse, Subtract(position, parent.position));
                localPosition = {parent.scale.x != 0.0f ? local.x / parent.scale.x : Float(0.0f),
                    parent.scale.y != 0.0f ? local.y / parent.scale.y : Float(0.0f),
                    parent.scale.z != 0.0f ? local.z / parent.scale.z : Float(0.0f)};
                localRotation = Normalize(Multiply(inverse, rotation));
            }
            link.transform->position = localPosition;
            link.transform->rotation = localRotation;
            // 로컬을 움직였으니 월드 캐시는 이번 프레임의 Transform3DSystem 이 다시 채운다(D-47).
            link.transform->worldValid = false;
            link.writtenPosition = localPosition;
            link.writtenRotation = localRotation;
            if (link.rigidbodyComponent != nullptr)
            {
                link.rigidbodyComponent->linearVelocity = world.GetLinearVelocity(link.body);
                link.rigidbodyComponent->angularVelocity = world.GetAngularVelocity(link.body);
                link.writtenLinearVelocity = link.rigidbodyComponent->linearVelocity;
                link.writtenAngularVelocity = link.rigidbodyComponent->angularVelocity;
            }
        }
    }
}
