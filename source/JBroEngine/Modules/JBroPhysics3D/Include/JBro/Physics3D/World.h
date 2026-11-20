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
        // `points` 를 감싼 볼록 껍질이다(D-418). 어떤 바디에도 쓴다. 점이 넷보다 적으면 만들지 않는다. 한 평면의 점이면 Jolt 가 납작한 껍질을 만든다.
        ConvexHull,
        // `points` 와 `indices`(삼각형마다 셋, 바깥에서 볼 때 반시계)의 삼각형 메시다(D-418). 앞면 쪽에서 오는 것만 막는다. **움직이지 않는 바디만** 받는다 - Jolt 의 메시 도형은 부피가 없다.
        Mesh,
    };

    inline constexpr UInt32 InvalidBodyValue = 0xFFFFFFFFu;

    // 물리 전용 워커의 상한이다(D-419, 2D 의 `Physics2D::MaxWorkerCount` 와 같은 값). 명시한 값도 여기서 자른다.
    inline constexpr UInt32 MaxWorkerCount = 16;
    // 한 스텝을 나누는 충돌 스텝의 상한이다(D-436). 값이 곧 비용의 배수에 가깝다.
    inline constexpr UInt32 MaxCollisionSteps = 8;

    // 3D 물리 일감(켜진 콜라이더 수)에 맞는 워커 수(D-419). 실측(physics3d-plan §3.5)으로 정한 기준이다: 256 미만이면 0(메인 한 스레드),
    // 그 위로는 256 마다 하나씩 더하되 `hardwareThreads - 1` 과 `MaxWorkerCount` 를 넘지 않는다.
    UInt32 RecommendWorkerCount(UInt32 work, UInt32 hardwareThreads);

    // Jolt 의 바디 번호(자리 + 세대)를 그대로 싣는다. 지운 바디의 번호는 같은 자리를 다시 쓴 바디와 갈린다.
    struct BodyId
    {
        UInt32 value = InvalidBodyValue;
    };

    inline constexpr UInt32 InvalidJointIndex = 0xFFFFFFFFu;

    // 조인트 번호(D-426). 자리와 세대다 - 지운 조인트의 번호는 같은 자리를 다시 쓴 조인트와 갈린다.
    // 바디를 지우면 그 바디에 걸린 조인트도 함께 지워진다(`IsValid` 가 거짓이 된다).
    struct JointId
    {
        UInt32 index = InvalidJointIndex;
        UInt32 generation = 0;
    };

    inline constexpr UInt32 InvalidCharacterIndex = 0xFFFFFFFFu;

    // 캐릭터 번호(D-427). 자리와 세대다.
    struct CharacterId
    {
        UInt32 index = InvalidCharacterIndex;
        UInt32 generation = 0;
    };

    // **캐릭터**(D-427). Jolt 의 `CharacterVirtual` 이다 - 힘으로 움직이는 몸이 아니라, 스크립트가 준 이동을 막히면 미끄러지며 따르는 선 캡슐이다
    // (유니티의 CharacterController 자리). 돌지 않는다. 비탈은 maxSlope 까지 오르고, stepHeight 까지의 턱은 걸어 오르며, 걷던 바닥에는 붙어 내려간다.
    // 동적인 몸을 민다. 다른 몸과 질의가 캐릭터를 보도록 같은 캡슐의 키네마틱 몸 하나를 든다(`GetCharacterBody`) - 사건과 질의에서 캐릭터는 그 몸이고 userData 도 그 몸에 있다.
    struct CharacterDef
    {
        // 캐릭터 원점(월드).
        Vector3 position;
        // 원점에서 캡슐 중심까지(월드 축).
        Vector3 center;
        Float radius = 0.5f;
        // 원통 부분의 높이 절반. 0 이면 구다.
        Float halfHeight = 0.5f;
        // 라디안.
        Float maxSlope = 0.785398f;
        Float stepHeight = 0.3f;
        UInt32 layer = 0x00000001u;
        UInt32 mask = 0xFFFFFFFFu;
        UInt64 userData = 0;
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
        // 껍질·메시의 점(바디 로컬, 크기를 곱한 값)과 메시의 삼각형. 만드는 동안만 읽는다 - 커널이 사본을 만든다.
        ArrayView<const Vector3> points;
        ArrayView<const UInt32> indices;
        // 표면 성질은 도형마다다(2D 의 콜라이더마다와 같다). 두 도형이 닿으면 마찰은 둘의 기하 평균, 반발은 큰 쪽이다(Jolt 의 기본 섞기).
        Float friction = 0.6f;
        Float restitution = 0.0f;
        // **레이어도 도형마다다**(D-421, 2D 와 같다). 두 도형은 (A.layer & B.mask) 와 (B.layer & A.mask) 가 모두 0 이 아니고, 레이어 충돌 표가 둘을
        // 떼어 두지 않았을 때만 만난다. 한 바디의 도형 여럿이 저마다 다른 레이어여도 저마다 거른다.
        UInt32 layer = 0x00000001u;
        UInt32 mask = 0xFFFFFFFFu;
    };

    // **동적인 몸은 이어지는 판정(CCD)을 저절로 쓴다**(D-425) - 한 스텝에 제 크기보다 멀리 가도 얇은 벽을 뚫지 않는다. 동적인 몸끼리도 본다.
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
        UInt64 userData = 0;
    };

    // **거리 조인트**(D-426, 2D 의 `DistanceJointDef` 와 같은 뜻). 두 앵커 사이 거리를 [minDistance, maxDistance] 에 둔다.
    // 앵커는 바디 원점 기준 로컬이다(크기를 곱한 값). bodyB 가 비었으면 localAnchorB 는 월드의 점이다.
    // frequency 가 0 보다 크면 한계가 용수철이다(초당 떨림 수, dampingRatio 는 0~1).
    struct DistanceJointDef
    {
        BodyId bodyA;
        BodyId bodyB;
        Vector3 localAnchorA;
        Vector3 localAnchorB;
        Float minDistance = 1.0f;
        Float maxDistance = 1.0f;
        Float frequency = 0.0f;
        Float dampingRatio = 0.0f;
        // 거짓이면 두 바디의 도형이 서로 부딪히지 않는다.
        Bool collideConnected = false;
    };

    // **경첩 조인트**(D-426). 두 앵커가 한 점에 붙고, 축 둘레로만 돈다. 축과 법선은 바디마다 로컬이고(B 가 없으면 월드),
    // 두 바디의 법선이 월드에서 겹칠 때가 각 0 이다 - 그래서 처음 이을 때 B 쪽 축·법선을 A 의 것으로 떠 두면 그때의 자세가 0 이다.
    // **각은 A 가 B 에 대해** 축 둘레로 오른손으로 돈 것이다(라디안). 한계는 [-pi, 0]·[0, pi] 에서 자른다(Jolt 의 경첩이 반 바퀴까지 받는다).
    struct HingeJointDef
    {
        BodyId bodyA;
        BodyId bodyB;
        Vector3 localAnchorA;
        Vector3 localAnchorB;
        Vector3 localAxisA{1.0f, 0.0f, 0.0f};
        Vector3 localNormalA{0.0f, 1.0f, 0.0f};
        Vector3 localAxisB{1.0f, 0.0f, 0.0f};
        Vector3 localNormalB{0.0f, 1.0f, 0.0f};
        Bool enableLimit = false;
        Float lowerAngle = -0.785398f;
        Float upperAngle = 0.785398f;
        // 모터는 A 를 B 에 대해 motorSpeed(라디안/초)로 돌리고, 그러려고 쓸 수 있는 가장 큰 토크가 maxMotorTorque 다.
        Bool enableMotor = false;
        Float motorSpeed = 0.0f;
        Float maxMotorTorque = 1000.0f;
        Bool collideConnected = false;
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
        // 맞은 도형 - 그 바디에 준 도형 목록(`CreateBody` 는 하나, `SetShapes` 는 여럿)에서의 자리다(D-435). 어댑터는 이것으로 콜라이더를 가린다.
        UInt32 shape = 0;
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

        // 원점과 회전을 옮긴다(순간 이동). 속도는 그대로다. 지나온 길은 없는 것으로 본다.
        void SetTransform(BodyId body, const Vector3& position, const Quaternion& rotation);
        // **이어진 움직임으로 옮긴다**(D-423). 자리는 `SetTransform` 처럼 바로 바뀌지만, 다음 `Step` 이 지금 자리에서 새 자리까지의 길을 쓸어
        // 그 사이에 지나친 것을 트리거 사건으로 알린다(아래 "빠른 트리거"). 오브젝트를 따라가는 트리거 센서가 쓴다. 한 스텝에 여러 번 부르면 첫 자리에서 쓴다.
        void MoveContinuously(BodyId body, const Vector3& position, const Quaternion& rotation);
        Vector3 GetPosition(BodyId body) const;
        Quaternion GetRotation(BodyId body) const;
        Vector3 GetLinearVelocity(BodyId body) const;
        void SetLinearVelocity(BodyId body, const Vector3& velocity);
        Vector3 GetAngularVelocity(BodyId body) const;
        void SetAngularVelocity(BodyId body, const Vector3& velocity);
        Bool IsAwake(BodyId body) const;
        // 깨우거나 재운다(D-420). 움직이지 않는 몸은 아무것도 하지 않는다.
        void SetAwake(BodyId body, Bool awake);
        // 질량 중심(월드). 위치를 준 힘의 토크를 그 스텝의 질량 중심으로 풀 때 쓴다.
        Vector3 GetCenterOfMass(BodyId body) const;

        // **힘과 충격량**(D-420, 2D 와 같은 뜻). 힘·토크는 다음 Step 한 번 동안 가해지고 Step 이 끝나면 비워진다. 충격량은 속도를 바로 바꾼다.
        // 동적인 몸만 받는다(Jolt 가 다른 몸에는 아무것도 하지 않는다). 막은 축으로는 움직이지 않는다. 받으면 깬다.
        void ApplyForce(BodyId body, const Vector3& force);
        void ApplyTorque(BodyId body, const Vector3& torque);
        void ApplyImpulse(BodyId body, const Vector3& impulse);
        void ApplyAngularImpulse(BodyId body, const Vector3& impulse);
        UInt64 GetUserData(BodyId body) const;

        // 도형을 제자리에서 바꾼다(콜라이더를 더하거나 크기를 바꿨다). 번호·자세·속도는 그대로이고 질량은 지금의 질량으로 다시 나눈다.
        // 비어 있거나 틀린 도형이면 거짓이고 도형은 그대로다.
        Bool SetShapes(BodyId body, ArrayView<const ShapeDesc> shapes);
        // 성질을 제자리에서 바꾼다: 질량·중력 배율·감쇠·축 고정·잠듦. 종류·도형(레이어는 도형에 있다)·자세·속도·userData·트리거는 두고,
        // 번호가 그대로라 닿아 있던 쌍이 이어진다(2D 의 `SetBodyProperties` 와 같다).
        void SetBodyProperties(BodyId body, const BodyDef& def);

        // **레이어 충돌 표**(D-421, 2D 의 `WorldSettings::ignoredLayers` 와 같은 뜻). 비트 j 가 선 행 i 는 레이어 i 와 j 가 서로 지나간다(대칭으로 채운다).
        // 두 도형은 한쪽 레이어 비트 i 와 다른 쪽 비트 j 가운데 떼어 두지 않은 쌍이 하나라도 있으면 만난다. 비어 있으면 모두 만난다.
        // 스텝 밖(메인 스레드)에서만 바꾼다.
        void SetIgnoredLayers(const UInt32 (&rows)[32]);

        void SetGravity(const Vector3& gravity);
        Vector3 GetGravity() const;

        // ── 조인트(D-426) ────────────────────────────────────────────────
        // 바디가 없거나(B 는 비울 수 있다) A 와 B 가 같으면 빈 번호다. 걸면 두 바디를 깨운다.
        // 바디의 도형을 바꾸면(`SetShapes`) 그 바디의 조인트를 같은 정의로 다시 세운다 - 질량 중심이 옮겨도 앵커는 바디 원점 기준이다.
        JointId CreateDistanceJoint(const DistanceJointDef& def);
        JointId CreateHingeJoint(const HingeJointDef& def);
        // 성질을 바꾼다. 두 바디는 만들 때와 같아야 한다(다르면 아무것도 하지 않는다 - 지우고 새로 만든다).
        void SetDistanceJoint(JointId joint, const DistanceJointDef& def);
        void SetHingeJoint(JointId joint, const HingeJointDef& def);
        void DestroyJoint(JointId joint);
        Bool IsValid(JointId joint) const;
        UInt32 GetJointCount() const;
        // 경첩의 지금 각(라디안, A 가 B 에 대해). 경첩이 아니면 0 이다.
        Float GetHingeAngle(JointId joint) const;
        // 조인트가 마지막 충돌 스텝에 두 바디 사이에 건 힘의 크기(N)와 돌림힘의 크기(N·m)다(D-437, 끊는 힘을 견준다. 2D 와 같은 뜻).
        // Jolt 제약의 람다(임펄스)를 충돌 스텝 길이로 나눈다. 경첩의 돌림힘은 축에 수직인 두 성분과 한계·모터를 합친 크기다.
        // 거리 조인트의 돌림힘은 0 이다. 없는 조인트와 아직 스텝을 돌지 않은 월드는 0 이다.
        Float GetJointForce(JointId joint) const;
        Float GetJointTorque(JointId joint) const;

        // ── 캐릭터(D-427) ────────────────────────────────────────────────
        // 크기가 틀렸거나 표가 찼으면 빈 번호다.
        CharacterId CreateCharacter(const CharacterDef& def);
        void DestroyCharacter(CharacterId character);
        Bool IsValid(CharacterId character) const;
        UInt32 GetCharacterCount() const;
        // 모양(중심·반지름·높이)·비탈·턱·레이어를 바꾼다. 자리(position)와 userData 는 두고 쓰지 않는다.
        void SetCharacterShape(CharacterId character, const CharacterDef& def);
        // 순간 이동이다. 쌓아 둔 이동은 그대로다.
        void SetCharacterPosition(CharacterId character, const Vector3& position);
        // **다음 Step 에 이만큼 옮긴다**(여러 번 부르면 더한다). 중력은 넣지 않는다 - 떨어지게 하려면 이동에 넣는다(유니티의 `Move` 와 같다).
        void MoveCharacter(CharacterId character, const Vector3& displacement);
        Vector3 GetCharacterPosition(CharacterId character) const;
        // 마지막 Step 에 실제로 간 속도(막혀 덜 갔으면 덜 간 만큼).
        Vector3 GetCharacterVelocity(CharacterId character) const;
        // 마지막 Step 뒤 걸을 수 있는 바닥(maxSlope 이하)에 서 있는가.
        Bool IsCharacterGrounded(CharacterId character) const;
        BodyId GetCharacterBody(CharacterId character) const;

        // 시간을 deltaTime 만큼 진행한다. 0 이하면 아무것도 하지 않는다.
        void Step(Float deltaTime);

        // 스텝을 나눌 물리 전용 워커 수(D-419). 0 이면 메인 한 스레드이고 워커를 세우지 않는다. `MaxWorkerCount` 에서 자른다.
        // 워커는 엔진의 태스크 모듈이 든다 - Jolt 의 잡은 `JBro::WorkerPool`(어느 스레드에서나 할당 없이 넣는 워커 묶음) 위에서 돈다(D-428).
        // `TaskManager` 가 아닌 까닭: 그것은 메인 스레드에서만 일을 받고 넣을 때마다 묶음을 힙에 만든다. Jolt 는 워커 안에서 스텝마다 잡을 수십 개 만든다.
        // 같은 수를 다시 주면 아무것도 하지 않는다.
        void SetWorkerCount(UInt32 count);
        UInt32 GetWorkerCount() const;
        // **충돌 스텝**(D-436). 한 `Step` 을 이만큼 나눠 충돌을 다시 찾고 푼다(Jolt 의 collisionSteps, 2D 의 서브스텝과 같은 뜻).
        // 기본 1. 연속 충돌(D-425)은 이동만 보므로, 회전만 빠른 몸(빨리 도는 긴 막대)이 얇은 벽을 뚫으면 이것을 올린다 - 비용은 거의 그 배수다.
        // 1 아래는 1, `MaxCollisionSteps` 위는 그 값으로 자른다.
        void SetCollisionSteps(UInt32 steps);
        UInt32 GetCollisionSteps() const;

        // 마지막 Step 의 접촉 사건. 다음 Step 이 비운다. 끝에는 그 사이 지운 바디의 접촉도 든다.
        //
        // **빠른 트리거**(D-423). 스텝은 끝 자리만 보므로 한 스텝에 제 크기보다 멀리 가는 몸은 얇은 트리거를 건너뛴다. 그래서 스텝 뒤에, 그 스텝에
        // 제 안쪽 반지름보다 멀리 간 동적인 몸은 트리거만을, `MoveContinuously` 로 그만큼 옮긴 트리거는 고체만을 그 길을 따라 쓸어(도형 밀기),
        // 끝 자리에서는 닿아 있지 않은 것을 "지나쳤다" 로 본다: 이번 스텝에 시작, 다음 스텝에 끝이 나온다.
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
