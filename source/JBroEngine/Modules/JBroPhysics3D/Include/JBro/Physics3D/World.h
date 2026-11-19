#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
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
        // 표면 성질은 도형마다다(2D 의 콜라이더마다와 같다). 두 도형이 닿으면 마찰은 둘의 기하 평균, 반발은 큰 쪽이다(Jolt 의 기본 섞기).
        Float friction = 0.6f;
        Float restitution = 0.0f;
    };

    struct BodyDef
    {
        BodyType type = BodyType::Dynamic;
        // 도형 하나. `shapes` 가 비어 있지 않으면 이것 대신 그 모두를 한 복합 도형으로 든다(한 오브젝트의 콜라이더 여럿).
        ShapeDesc shape;
        ArrayView<const ShapeDesc> shapes;
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
        // 참이면 부딪히지 않고 겹침만 알린다.
        Bool isTrigger = false;
        // 트리거가 정적·키네마틱 몸과의 겹침도 알리는가. 움직이는 오브젝트의 트리거만 켠다 - 정적인 트리거가 정적인 바닥과 겹친 것은
        // 2D 처럼 사건이 아니다(움직이지 않는 둘은 만나지 않는다).
        Bool triggerSeesStatic = false;
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

    // 두 바디가 닿기 시작했거나(시작) 계속 닿아 있거나(유지) 떨어졌다(끝). 바디 쌍마다 하나다 - 복합 도형의 잎 여럿이 닿아도 하나다.
    // 바디가 지워져 끝난 접촉도 끝으로 알리므로 번호가 이미 죽었을 수 있다 - userData 를 함께 싣는 까닭이다(2D 와 같다).
    struct ContactEvent
    {
        BodyId bodyA;
        BodyId bodyB;
        UInt64 userDataA = 0;
        UInt64 userDataB = 0;
        // 둘 중 하나가 트리거(센서)다.
        Bool isTrigger = false;
        // 시작·유지의 대표 접촉점(월드)과 A→B 법선. 끝은 0 이다 - 이미 떨어졌다.
        Vector3 point;
        Vector3 normal;
    };

    // 질의 하나의 결과다. normal 은 맞은 표면의 바깥(쏜 쪽을 향한다)이고 distance 는 출발점에서 맞은 자리까지다.
    // 출발부터 겹쳐 있으면 distance 0 이다.
    struct QueryHit
    {
        BodyId body;
        UInt64 userData = 0;
        Vector3 point;
        Vector3 normal;
        Float distance = 0.0f;
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

        // 도형을 제자리에서 바꾼다(콜라이더를 더하거나 크기를 바꿨다). 번호·자세·속도는 그대로이고 질량은 지금의 질량으로 다시 나눈다.
        // 비어 있거나 틀린 도형이면 거짓이고 도형은 그대로다.
        Bool SetShapes(BodyId body, ArrayView<const ShapeDesc> shapes);
        // 성질을 제자리에서 바꾼다: 질량·중력 배율·감쇠·축 고정·잠듦·`layer`/`mask`. 종류·도형·자세·속도·userData·트리거는 두고,
        // 번호가 그대로라 닿아 있던 쌍이 이어진다(2D 의 `SetBodyProperties` 와 같다).
        void SetBodyProperties(BodyId body, const BodyDef& def);

        void SetGravity(const Vector3& gravity);
        Vector3 GetGravity() const;

        // 시간을 deltaTime 만큼 진행한다. 0 이하면 아무것도 하지 않는다.
        void Step(Float deltaTime);

        // 마지막 Step 의 접촉 사건. 다음 Step 이 비운다. 끝에는 그 사이 지운 바디의 접촉도 든다.
        ArrayView<const ContactEvent> GetBeginEvents() const;
        ArrayView<const ContactEvent> GetStayEvents() const;
        ArrayView<const ContactEvent> GetEndEvents() const;

        // ── 질의 ─────────────────────────────────────────────────────────────
        // 마지막으로 맞춘 바디들을 본다(스텝 사이에 바꾼 자세도 본다). `layerMask` 와 바디의 레이어가 겹치는 것만 대상이고 트리거도 든다.
        // `direction` 은 정규화하지 않아도 되고 0 이거나 `distance` 가 0 이하면 맞지 않는다. 결과 배열은 비운 뒤 채운다.
        Bool Raycast(const Vector3& origin, const Vector3& direction, Float distance, UInt32 layerMask, QueryHit& hit) const;
        // 경로의 바디 전부를 거리 순으로. 바디마다 한 번이다.
        void RaycastAll(const Vector3& origin, const Vector3& direction, Float distance, UInt32 layerMask, Array<QueryHit>& hits) const;
        // 구·돌린 상자와 겹치는 바디의 userData. 바디마다 한 번이다.
        void OverlapSphere(const Vector3& center, Float radius, UInt32 layerMask, Array<UInt64>& userData) const;
        void OverlapBox(const Vector3& center, const Vector3& halfExtents, const Quaternion& rotation, UInt32 layerMask,
            Array<UInt64>& userData) const;
        // 구·돌린 상자를 밀어 처음 닿는 바디. point 는 맞은 순간의 접촉점이다.
        Bool SphereCast(const Vector3& origin, Float radius, const Vector3& direction, Float distance, UInt32 layerMask, QueryHit& hit) const;
        Bool BoxCast(const Vector3& center, const Vector3& halfExtents, const Quaternion& rotation, const Vector3& direction,
            Float distance, UInt32 layerMask, QueryHit& hit) const;

    private:
        struct Impl;
        OwnerPtr<Impl> m_impl;
    };
}
