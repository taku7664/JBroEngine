#pragma once

#include <JBro/Types/Math3D.h>
#include <JBro/Types/SafePtr.h>

#include <cstdint>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

// 3D 물리 커널의 월드다(D-414, physics3d-plan §2.1·§3 의 1 단계).
//
// 충돌과 강체 풀이는 Jolt Physics 가 한다. **이 헤더는 Jolt 를 모른다** - JBro 의 값 타입만 받고 내며, Jolt 헤더는
// 이 모듈의 Source/ 안에서만 include 한다. 그래서 어댑터와 시험은 Jolt 의 include 경로 없이 이 헤더를 본다.
//
// 2D 커널(`JBro::Physics2D::World`)과 같은 모양이다: 캔버스와 컴포넌트를 모르고, 바디는 번호로만 가리키며, 어댑터가 컴포넌트의
// 아이디를 `userData` 로 달아 둔다. 메인 스레드 전용이다(스텝 안에서 Jolt 가 워커를 쓰는 것은 이 클래스 안의 일이다).
namespace JBro::Physics3D
{
    enum class BodyType : std::uint8_t
    {
        Static,
        Kinematic,
        Dynamic,
    };

    enum class ShapeType : std::uint8_t
    {
        Box,
        Sphere,
        // 로컬 Y 축으로 선 캡슐이다. `radius` 와 원통 부분의 높이 절반 `halfHeight`.
        Capsule,
    };

    inline constexpr UInt32 InvalidBodyValue = 0xFFFFFFFFu;

    // Jolt 의 바디 번호(자리 + 세대)를 그대로 싣는다. 지운 바디의 번호는 같은 자리를 다시 쓴 바디와 갈린다.
    struct BodyId
    {
        UInt32 value = InvalidBodyValue;
    };

    struct ShapeDesc
    {
        ShapeType type = ShapeType::Box;
        // 상자의 반 크기(월드 단위, 크기를 곱한 값).
        Vector3 halfExtents{0.5f, 0.5f, 0.5f};
        Float radius = 0.5f;
        Float halfHeight = 0.5f;
        // 바디 원점에서 도형 중심까지(바디 로컬).
        Vector3 offset;
    };

    struct BodyDef
    {
        BodyType type = BodyType::Dynamic;
        ShapeDesc shape;
        // 바디 원점과 회전(월드).
        Vector3 position;
        Quaternion rotation;
        Vector3 linearVelocity;
        // 라디안/초, 월드 축.
        Vector3 angularVelocity;
        // Dynamic 의 전체 질량. 관성은 도형에서 이 질량으로 구한다.
        Float mass = 1.0f;
        Float gravityScale = 1.0f;
        Float linearDamping = 0.0f;
        Float angularDamping = 0.0f;
        Float friction = 0.6f;
        Float restitution = 0.0f;
        // 참이면 부딪히지 않고 겹침만 알린다.
        Bool isTrigger = false;
        // 축 고정. 고정한 축으로는 움직이거나 돌지 않는다.
        Bool freezePositionX = false;
        Bool freezePositionY = false;
        Bool freezePositionZ = false;
        Bool freezeRotationX = false;
        Bool freezeRotationY = false;
        Bool freezeRotationZ = false;
        Bool canSleep = true;
        // 두 바디는 (A.layer & B.mask) 와 (B.layer & A.mask) 가 모두 0 이 아닐 때만 만난다(2D 와 같다).
        UInt32 layer = 0x00000001u;
        UInt32 mask = 0xFFFFFFFFu;
        UInt64 userData = 0;
    };

    struct WorldSettings
    {
        Vector3 gravity{0.0f, -9.81f, 0.0f};
        // Jolt 가 미리 잡는 표의 크기다. 넘으면 바디를 더 만들지 못한다.
        UInt32 maxBodies = 10240;
        UInt32 maxBodyPairs = 65536;
        UInt32 maxContactConstraints = 10240;
        // 스텝 하나가 쓰는 임시 메모리다. 미리 한 번 잡아 두므로 스텝 중에는 힙에서 받지 않는다.
        UInt32 tempAllocatorBytes = 10u * 1024u * 1024u;
    };

    // 커널이 Jolt 에 건넨 할당 훅을 지난 횟수다(프로세스 전체). 훅 뒤에는 크기 칸마다 놓인 블록을 다시 쓰는 풀이 있어,
    // `systemAllocations` 는 그 풀에 없어 OS 힙에서 받은 횟수다. 시험이 "스텝 중 힙 할당 0" 을 이것으로 본다.
    struct AllocationStats
    {
        UInt64 allocations = 0;
        UInt64 systemAllocations = 0;
        UInt64 frees = 0;
    };
    AllocationStats GetAllocationStats();

    class World
    {
    public:
        explicit World(const WorldSettings& settings = {});
        ~World();
        World(const World&) = delete;
        World& operator=(const World&) = delete;

        // 도형이 틀렸거나(크기 0 이하) 표가 찼으면 빈 번호다.
        BodyId CreateBody(const BodyDef& def);
        void DestroyBody(BodyId body);
        Bool IsValid(BodyId body) const;
        UInt32 GetBodyCount() const;

        // 원점과 회전을 옮긴다(순간 이동). 속도는 그대로다.
        void SetTransform(BodyId body, const Vector3& position, const Quaternion& rotation);
        Vector3 GetPosition(BodyId body) const;
        Quaternion GetRotation(BodyId body) const;
        Vector3 GetLinearVelocity(BodyId body) const;
        void SetLinearVelocity(BodyId body, const Vector3& velocity);
        Vector3 GetAngularVelocity(BodyId body) const;
        void SetAngularVelocity(BodyId body, const Vector3& velocity);
        Bool IsAwake(BodyId body) const;
        UInt64 GetUserData(BodyId body) const;

        void SetGravity(const Vector3& gravity);
        Vector3 GetGravity() const;

        // 시간을 deltaTime 만큼 진행한다. 0 이하면 아무것도 하지 않는다.
        void Step(Float deltaTime);

    private:
        struct Impl;
        OwnerPtr<Impl> m_impl;
    };
}
