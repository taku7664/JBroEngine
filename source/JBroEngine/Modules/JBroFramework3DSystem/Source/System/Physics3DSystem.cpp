#include <JBro/Framework3DSystem/System/Physics3DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework3D/Component/Physics3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3D/Scripting/GameScript.h>
#include <JBro/Framework3DSystem/Physics/ColliderMesh.h>
#include <JBro/Core/Log.h>
#include <JBro/Runtime/ScriptCallGuard.h>
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
            // 레이어는 콜라이더마다다(D-421) - 한 바디의 콜라이더 여럿도 저마다 거른다.
            desc.layer = collider.layer;
            desc.mask = collider.mask;
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
            case Component::ColliderShape3D::Mesh:
                // 점과 삼각형은 어댑터의 메시 사본이 채운다(`State::MeshCache`).
                desc.type = Physics3D::ShapeType::Mesh;
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
            print.Mix(desc.layer);
            print.Mix(desc.mask);
            const UInt64 points = desc.points.Size();
            const UInt64 indices = desc.indices.Size();
            print.Mix(&points, sizeof(points));
            print.Mix(&indices, sizeof(indices));
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
        void FillProperties(const Component::Rigidbody3D* rigidbody, Physics3D::BodyDef& def)
        {
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
        // 메시 콜라이더 하나의 점 사본이다(D-418). 메시·세대·크기가 바뀔 때만 다시 읽고 다시 곱한다 - 프레임마다 메시를 읽지 않는다.
        struct MeshCache
        {
            AssetId id;
            AssetHandle handle;
            UInt32 dataGeneration = 0;
            Vector3 scale{1.0f, 1.0f, 1.0f};
            Bool loaded = false;
            Bool scaled = false;
            Bool warned = false;
            Bool seen = false;
            // 다시 읽거나 곱할 때마다 오른다. 바디의 도형 지문에 섞는다 - 점의 수가 같아도 바뀐 것을 안다.
            UInt64 version = 0;
            Array<Vector3> local;
            Array<Vector3> points;
            Array<UInt32> indices;
        };

        struct Group
        {
            Array<Physics3D::ShapeDesc> shapes;
            // 메시 사본의 판을 섞은 값. 도형 지문에 더한다.
            UInt64 meshVersions = 0;
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

        // 사건과 질의가 커널의 userData(오브젝트 아이디)에서 오브젝트를 찾는 표다. 스텝마다 본 오브젝트의 표시를 새로 하고,
        // 사건을 보낸 뒤 이번에 보지 못한 것을 지운다 - 이번 스텝에 지운 오브젝트의 끝 훅까지는 남는다.
        struct Owner
        {
            Handle::GameObject handle;
            SafePtr<Object::GameObject> object;
            UInt64 stamp = 0;
        };

        Physics3D::World world;
        Table<InstanceId, Owner> owners;
        UInt64 stamp = 0;
        // 훅을 부를 스크립트를 먼저 모으는 자리와 질의 결과를 받는 자리. 멤버로 두어 부를 때마다 할당하지 않는다.
        Array<GameScript3D*> hookTargets;
        Array<Physics3D::QueryHit> queryHits;
        Array<UInt64> queryData;
        Table<InstanceId, Group> groups;
        Table<InstanceId, BodyLink> bodies;
        Table<InstanceId, SensorLink> sensors;
        Table<InstanceId, MeshCache> meshes;
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

    void Physics3DSystem::SetIgnoredLayers(const UInt32 (&rows)[PhysicsLayerCount])
    {
        for (std::uint32_t row = 0; row < PhysicsLayerCount; ++row)
        {
            m_ignoredLayers[row] = rows[row];
        }
    }

    void Physics3DSystem::SetWorkerCount(UInt32 count)
    {
        m_workerCount = count;
    }

    UInt32 Physics3DSystem::GetWorkerCount() const
    {
        return m_state->world.GetWorkerCount();
    }

    void Physics3DSystem::SetAssets(const AssetSystem* assets)
    {
        m_assets = assets;
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
        m_canvas = &canvas;
        m_state = MakeOwnerPtr<State>();
    }

    void Physics3DSystem::OnShutdown(Canvas& canvas)
    {
        (void)canvas;
        m_canvas = nullptr;
        // 커널과 연결을 모두 버린다. 다시 켜면 처음부터 만든다(2D 와 같다).
        m_state = MakeOwnerPtr<State>();
    }

    void Physics3DSystem::SyncBodies(Canvas& canvas)
    {
        State& state = *m_state;
        Physics3D::World& world = state.world;
        const auto markOwner = [&state](Object::GameObject* object) {
            State::Owner* owner = state.owners.Find(object->GetInstanceId());
            if (owner == nullptr)
            {
                State::Owner fresh;
                fresh.handle = object->GetScriptHandle();
                fresh.object = object->SafeFromThis();
                state.owners.TryAdd(object->GetInstanceId(), fresh);
                owner = state.owners.Find(object->GetInstanceId());
            }
            owner->stamp = state.stamp;
        };

        for (auto& entry : state.groups)
        {
            entry.MappedValue.shapes.Clear();
            entry.MappedValue.meshVersions = 0;
            entry.MappedValue.object = nullptr;
        }
        for (auto& entry : state.meshes)
        {
            entry.MappedValue.seen = false;
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
            Physics3D::ShapeDesc desc = BakeShape(collider, pose.scale);
            markOwner(object);
            UInt64 meshVersion = 0;
            if (collider.shape == Component::ColliderShape3D::Mesh)
            {
                // **메시 콜라이더**(D-418). 움직이는 몸(켜진 동적·키네마틱)과 트리거(키네마틱 센서)는 삼각형 메시를 받지 못하므로 볼록 껍질이다.
                const Component::Rigidbody3D* body = canvas.FindComponentRaw<Component::Rigidbody3D>(object);
                const Bool movingBody = body != nullptr && body->IsActiveComponent() && body->bodyType != Component::BodyType3D::Static;
                const Bool hull = collider.convex || movingBody || collider.isTrigger;
                const InstanceId colliderId = collider.GetInstanceId();
                State::MeshCache* cache = state.meshes.Find(colliderId);
                if (cache == nullptr)
                {
                    state.meshes.TryAdd(colliderId, State::MeshCache{});
                    cache = state.meshes.Find(colliderId);
                }
                cache->seen = true;
                const ColliderMeshSource source = FindColliderMeshSource(canvas, *object, collider, m_assets);
                if (false == cache->loaded || cache->id != source.id || cache->handle.index != source.handle.index
                    || cache->handle.generation != source.handle.generation || cache->dataGeneration != source.dataGeneration)
                {
                    cache->id = source.id;
                    cache->handle = source.handle;
                    cache->dataGeneration = source.dataGeneration;
                    cache->loaded = GatherColliderMesh(m_assets, source, cache->local, cache->indices);
                    cache->scaled = false;
                    ++cache->version;
                }
                if (false == cache->loaded)
                {
                    // 아직 쓸 메시가 없다(실리는 중이거나 비었다). 다음 스텝에 다시 본다.
                    return;
                }
                if (false == cache->scaled || false == SameVector(cache->scale, pose.scale))
                {
                    cache->points.Clear();
                    for (const Vector3& point : cache->local)
                    {
                        cache->points.Add(Multiply(point, pose.scale));
                    }
                    cache->scale = pose.scale;
                    cache->scaled = true;
                    ++cache->version;
                }
                if (hull && false == collider.convex && false == cache->warned)
                {
                    cache->warned = true;
                    Log::Write(LogLevel::Warning, "physics3d",
                        "%s: a mesh collider that is not convex is used as its convex hull on a moving body or as a trigger",
                        object->GetName());
                }
                desc.type = hull ? Physics3D::ShapeType::ConvexHull : Physics3D::ShapeType::Mesh;
                desc.points = cache->points.View();
                desc.indices = cache->indices.View();
                meshVersion = cache->version;
            }
            if (collider.isTrigger)
            {
                // 움직이는 오브젝트(켜진 동적·키네마틱 몸)의 트리거만 정적인 것과의 겹침을 본다(2D 와 같다).
                const Component::Rigidbody3D* carrier = canvas.FindComponentRaw<Component::Rigidbody3D>(object);
                const Bool moving = carrier != nullptr && carrier->IsActiveComponent() && carrier->bodyType != Component::BodyType3D::Static;
                Fingerprint print;
                MixShape(print, desc);
                print.Mix(&meshVersion, sizeof(meshVersion));
                print.Mix(moving);
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
                    def.triggerSeesStatic = moving;
                    def.position = pose.position;
                    def.rotation = pose.rotation;
                    // 사건과 질의가 오브젝트를 찾도록 센서도 오브젝트 아이디를 싣는다.
                    def.userData = object->GetInstanceId();
                    State::SensorLink fresh;
                    fresh.body = world.CreateBody(def);
                    fresh.object = object->GetInstanceId();
                    fresh.signature = print.value;
                    state.sensors.TryAdd(colliderId, fresh);
                    link = state.sensors.Find(colliderId);
                }
                else
                {
                    // 센서는 오브젝트를 따라간다. 움직이는 몸의 자세는 지난 스텝의 되쓰기에서 왔다. **이어진 움직임으로 옮긴다**(D-423) -
                    // 다음 스텝이 지나온 길을 쓸어, 한 스텝에 멀리 간 트리거가 사이의 것을 건너뛰지 않는다.
                    world.MoveContinuously(link->body, pose.position, pose.rotation);
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
            group->shapes.Add(desc);
            group->meshVersions = group->meshVersions * 1099511628211ull + meshVersion;
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
            FillProperties(rigidbody, properties);
            const UInt64 propertiesPrint = PropertiesPrint(properties);
            Fingerprint shapesPrint;
            for (const Physics3D::ShapeDesc& desc : group.shapes)
            {
                MixShape(shapesPrint, desc);
            }
            shapesPrint.Mix(&group.meshVersions, sizeof(group.meshVersions));

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
        // 이번에 보지 못한 메시 콜라이더의 사본을 버린다.
        state.removals.Clear();
        for (auto& entry : state.meshes)
        {
            if (false == entry.MappedValue.seen)
            {
                state.removals.Add(entry.KeyValue);
            }
        }
        for (const InstanceId id : state.removals)
        {
            state.meshes.Remove(id);
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

    }

    void Physics3DSystem::ApplyPendingForces()
    {
        State& state = *m_state;
        Physics3D::World& world = state.world;
        for (auto& entry : state.bodies)
        {
            State::BodyLink* link = &entry.MappedValue;
            if (false == link->seen || link->rigidbodyComponent == nullptr)
            {
                continue;
            }
            Component::Rigidbody3D* rigidbody = link->rigidbodyComponent;
            const Physics3D::BodyType type = link->type;
            {
                // 스크립트가 쌓은 힘·충격량을 이번 스텝에 먹인다(D-420). 위치를 준 것의 토크는 지금의 질량 중심으로 푼다:
                // 점 p 의 힘 F 가 질량 중심 c 에 주는 토크는 (p - c) x F = p x F - c x F 이고, p x F 는 컴포넌트가 이미 모아 두었다.
                const Component::PendingForces3D pending = rigidbody->TakePendingForces();
                if (type == Physics3D::BodyType::Dynamic)
                {
                    const Vector3 center = world.GetCenterOfMass(link->body);
                    const Vector3 force = Add(pending.forceAtCenter, pending.forceAtPoints);
                    const Vector3 torque = Subtract(Add(pending.torque, pending.forceMoment), Cross(center, pending.forceAtPoints));
                    const Vector3 impulse = Add(pending.impulseAtCenter, pending.impulseAtPoints);
                    const Vector3 angular = Subtract(Add(pending.angularImpulse, pending.impulseMoment), Cross(center, pending.impulseAtPoints));
                    const Vector3 zero{};
                    if (false == SameVector(force, zero))
                    {
                        world.ApplyForce(link->body, force);
                    }
                    if (false == SameVector(torque, zero))
                    {
                        world.ApplyTorque(link->body, torque);
                    }
                    if (false == SameVector(impulse, zero))
                    {
                        world.ApplyImpulse(link->body, impulse);
                    }
                    if (false == SameVector(angular, zero))
                    {
                        world.ApplyAngularImpulse(link->body, angular);
                    }
                    if (pending.wake)
                    {
                        world.SetAwake(link->body, true);
                    }
                }
            }
        }
    }

    void Physics3DSystem::OnFixedUpdate(Canvas& canvas, Float fixedDeltaTime)
    {
        if (false == (fixedDeltaTime > 0.0f))
        {
            return;
        }
        State& state = *m_state;
        Physics3D::World& world = state.world;
        ++state.stamp;
        // **동기화는 스텝과 따로다**(D-422). 질의도 같은 동기화를 거쳐 지금의 컴포넌트를 본다. 힘을 먹이는 것과 사건은 스텝에만 있다.
        SyncBodies(canvas);
        ApplyPendingForces();

        // ── 4. 스텝 ─────────────────────────────────────────────────────────────────
        world.SetGravity(m_gravity);
        // 프로젝트의 레이어 충돌 표(D-421). 표 하나를 옮기는 값싼 일이라 스텝마다 먹인다.
        world.SetIgnoredLayers(m_ignoredLayers);
        // 같은 수면 커널이 아무것도 하지 않는다 - 스텝마다 풀을 세웠다 허물지 않는다.
        world.SetWorkerCount(m_workerCount);
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
                link.rigidbodyComponent->m_sleeping = false == world.IsAwake(link.body);
                link.rigidbodyComponent->linearVelocity = world.GetLinearVelocity(link.body);
                link.rigidbodyComponent->angularVelocity = world.GetAngularVelocity(link.body);
                link.writtenLinearVelocity = link.rigidbodyComponent->linearVelocity;
                link.writtenAngularVelocity = link.rigidbodyComponent->angularVelocity;
            }
        }

        // ── 6. 사건 ─────────────────────────────────────────────────────────────────
        DispatchEvents(canvas);
        state.removals.Clear();
        for (auto& entry : state.owners)
        {
            if (entry.MappedValue.stamp != state.stamp)
            {
                state.removals.Add(entry.KeyValue);
            }
        }
        for (const InstanceId id : state.removals)
        {
            state.owners.Remove(id);
        }
    }

    namespace
    {
        Component::BodyType3D BodyTypeOf(Canvas& canvas, Object::GameObject* object)
        {
            Component::Rigidbody3D* body = object != nullptr ? canvas.FindComponentRaw<Component::Rigidbody3D>(object) : nullptr;
            if (body == nullptr || false == body->IsActiveComponent())
            {
                return Component::BodyType3D::Static;
            }
            return body->bodyType;
        }
    }

    void Physics3DSystem::DispatchEvents(Canvas& canvas)
    {
        State& state = *m_state;
        const ArrayView<const Physics3D::ContactEvent> begins = state.world.GetBeginEvents();
        const ArrayView<const Physics3D::ContactEvent> stays = state.world.GetStayEvents();
        const ArrayView<const Physics3D::ContactEvent> ends = state.world.GetEndEvents();
        if (begins.IsEmpty() && stays.IsEmpty() && ends.IsEmpty())
        {
            return;
        }

        enum class Phase : std::uint8_t { Enter, Stay, Exit };

        // 훅이 오브젝트를 지워도 지나간 객체 위에서 다음 훅이 불리지 않게, 발송 내내 파괴를 미룬다(2D 와 같다).
        Canvas::IterationGuard guard(canvas);
        const auto deliver = [&](Object::GameObject* self, const Collision3D& hit, Phase phase, Bool trigger) {
            if (self == nullptr || false == self->IsActiveInHierarchy())
            {
                return;
            }
            // 먼저 모은 뒤 부른다. 훅이 스크립트를 붙이거나 떼면 목록이 흔들린다.
            state.hookTargets.Clear();
            for (const ScriptSlot& slot : self->GetScripts())
            {
                GameScriptBase* script = slot.reference.TryGet();
                if (script != nullptr && script->IsActiveScript())
                {
                    // 3D 프로젝트의 스크립트는 모두 GameScript3D 다. 등록이 컴파일 시간에 그것을 막는다(D-363).
                    state.hookTargets.Add(static_cast<GameScript3D*>(script));
                }
            }
            // 스크립트 훅은 `ScriptCallGuard` 를 거친다(D-330).
            for (GameScript3D* script : state.hookTargets)
            {
                if (trigger)
                {
                    if (phase == Phase::Enter)
                    {
                        ScriptCallGuard::Call("OnTriggerEnter", [script, &hit] { script->OnTriggerEnter(hit); });
                    }
                    else if (phase == Phase::Stay)
                    {
                        ScriptCallGuard::Call("OnTriggerStay", [script, &hit] { script->OnTriggerStay(hit); });
                    }
                    else
                    {
                        ScriptCallGuard::Call("OnTriggerExit", [script, &hit] { script->OnTriggerExit(hit); });
                    }
                }
                else if (phase == Phase::Enter)
                {
                    ScriptCallGuard::Call("OnCollisionEnter", [script, &hit] { script->OnCollisionEnter(hit); });
                }
                else if (phase == Phase::Stay)
                {
                    ScriptCallGuard::Call("OnCollisionStay", [script, &hit] { script->OnCollisionStay(hit); });
                }
                else
                {
                    ScriptCallGuard::Call("OnCollisionExit", [script, &hit] { script->OnCollisionExit(hit); });
                }
            }
        };

        const auto dispatch = [&](const Physics3D::ContactEvent& event, Phase phase) {
            const State::Owner* ownerA = state.owners.Find(static_cast<InstanceId>(event.userDataA));
            const State::Owner* ownerB = state.owners.Find(static_cast<InstanceId>(event.userDataB));
            Object::GameObject* objectA = ownerA != nullptr ? ownerA->object.TryGet() : nullptr;
            Object::GameObject* objectB = ownerB != nullptr ? ownerB->object.TryGet() : nullptr;
            // 한 오브젝트의 트리거와 그 오브젝트의 몸이 겹친 것은 자기 자신이다 - 알리지 않는다.
            if (event.userDataA == event.userDataB)
            {
                return;
            }
            // 각 스크립트는 자기 쪽에서 본 법선(자기 → 상대)을 받는다.
            Collision3D forA;
            forA.other = ownerB != nullptr ? ownerB->handle : Handle::GameObject{};
            forA.bodyType = BodyTypeOf(canvas, objectB);
            forA.point = event.point;
            forA.normal = event.normal;
            Collision3D forB;
            forB.other = ownerA != nullptr ? ownerA->handle : Handle::GameObject{};
            forB.bodyType = BodyTypeOf(canvas, objectA);
            forB.point = event.point;
            forB.normal = {-event.normal.x, -event.normal.y, -event.normal.z};
            deliver(objectA, forA, phase, event.isTrigger);
            deliver(objectB, forB, phase, event.isTrigger);
        };

        // 끝을 먼저 보낸다 - 같은 스텝에 떨어졌다 다시 닿은 쌍이 끝·시작 차례로 보이게(2D 와 같다).
        for (const Physics3D::ContactEvent& event : ends)
        {
            dispatch(event, Phase::Exit);
        }
        for (const Physics3D::ContactEvent& event : begins)
        {
            dispatch(event, Phase::Enter);
        }
        for (const Physics3D::ContactEvent& event : stays)
        {
            dispatch(event, Phase::Stay);
        }
    }

    // ── 질의 ─────────────────────────────────────────────────────────────────────
    // **질의는 지금의 컴포넌트를 본다**(D-422, 2D 와 같다). 묻기 전에 바디를 컴포넌트에 맞춘다 - 이번 프레임에 만든 콜라이더, 스크립트가 옮긴 오브젝트,
    // 바꾼 크기가 바로 걸린다. 맞추기는 바뀐 것만 커널에 먹이지만 콜라이더를 한 번 훑는다(2D 의 질의도 콜라이더를 훑는다).
    void Physics3DSystem::SyncForQuery() const
    {
        if (m_canvas == nullptr)
        {
            return;
        }
        const_cast<Physics3DSystem*>(this)->SyncBodies(*m_canvas);
    }

    namespace
    {
        template<typename StateT>
        void ToScriptHit(Canvas* canvas, const StateT& state, const Physics3D::QueryHit& from, RaycastHit3D& hit)
        {
            hit = {};
            const auto* owner = state.owners.Find(static_cast<InstanceId>(from.userData));
            Object::GameObject* object = owner != nullptr ? owner->object.TryGet() : nullptr;
            hit.other = owner != nullptr ? owner->handle : Handle::GameObject{};
            hit.bodyType = canvas != nullptr ? BodyTypeOf(*canvas, object) : Component::BodyType3D::Static;
            hit.point = from.point;
            hit.normal = from.normal;
            hit.distance = from.distance;
        }

        // 커널의 userData(오브젝트 아이디) 목록을 오브젝트 핸들로. 한 오브젝트의 몸과 트리거가 함께 걸려도 한 번이다.
        template<typename StateT>
        void ToHandles(const StateT& state, const Array<UInt64>& userData, Array<Handle::GameObject>& results)
        {
            results.Clear();
            for (std::size_t index = 0; index < userData.Size(); ++index)
            {
                const auto* owner = state.owners.Find(static_cast<InstanceId>(userData[index]));
                if (owner == nullptr || owner->object.TryGet() == nullptr)
                {
                    continue;
                }
                Bool known = false;
                for (std::size_t before = 0; before < index; ++before)
                {
                    known = known || userData[before] == userData[index];
                }
                if (false == known)
                {
                    results.Add(owner->handle);
                }
            }
        }
    }

    Bool Physics3DSystem::Raycast(Vector3 origin, Vector3 direction, Float distance, RaycastHit3D& hit, UInt32 layerMask) const
    {
        SyncForQuery();
        Physics3D::QueryHit found;
        if (false == m_state->world.Raycast(origin, direction, distance, layerMask, found))
        {
            hit = {};
            return false;
        }
        ToScriptHit(m_canvas, *m_state, found, hit);
        return true;
    }

    void Physics3DSystem::RaycastAll(Vector3 origin, Vector3 direction, Float distance, Array<RaycastHit3D>& hits, UInt32 layerMask) const
    {
        SyncForQuery();
        hits.Clear();
        m_state->world.RaycastAll(origin, direction, distance, layerMask, m_state->queryHits);
        for (const Physics3D::QueryHit& found : m_state->queryHits)
        {
            RaycastHit3D hit;
            ToScriptHit(m_canvas, *m_state, found, hit);
            hits.Add(hit);
        }
    }

    void Physics3DSystem::OverlapSphere(Vector3 center, Float radius, Array<Handle::GameObject>& results, UInt32 layerMask) const
    {
        SyncForQuery();
        m_state->world.OverlapSphere(center, radius, layerMask, m_state->queryData);
        ToHandles(*m_state, m_state->queryData, results);
    }

    void Physics3DSystem::OverlapBox(Vector3 center, Vector3 halfExtents, Quaternion rotation, Array<Handle::GameObject>& results,
        UInt32 layerMask) const
    {
        SyncForQuery();
        m_state->world.OverlapBox(center, halfExtents, rotation, layerMask, m_state->queryData);
        ToHandles(*m_state, m_state->queryData, results);
    }

    Bool Physics3DSystem::SphereCast(Vector3 origin, Float radius, Vector3 direction, Float distance, RaycastHit3D& hit,
        UInt32 layerMask) const
    {
        SyncForQuery();
        Physics3D::QueryHit found;
        if (false == m_state->world.SphereCast(origin, radius, direction, distance, layerMask, found))
        {
            hit = {};
            return false;
        }
        ToScriptHit(m_canvas, *m_state, found, hit);
        return true;
    }

    Bool Physics3DSystem::BoxCast(Vector3 center, Vector3 halfExtents, Quaternion rotation, Vector3 direction, Float distance,
        RaycastHit3D& hit, UInt32 layerMask) const
    {
        SyncForQuery();
        Physics3D::QueryHit found;
        if (false == m_state->world.BoxCast(center, halfExtents, rotation, direction, distance, layerMask, found))
        {
            hit = {};
            return false;
        }
        ToScriptHit(m_canvas, *m_state, found, hit);
        return true;
    }
}
