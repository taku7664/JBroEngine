#pragma once

#include <JBro/Framework2D/Math2D.h>
#include <JBro/Physics2D/BroadPhase.h>
#include <JBro/Physics2D/Collision.h>
#include <JBro/Physics2D/Geometry.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/Table.h>

#include <cstdint>

// 2D 물리 커널의 월드와 솔버다(D-199, physics-plan §3.3·§3.4·§4 의 3 단계).
//
// 캔버스와 컴포넌트를 모른다. 바디와 도형은 번호(index + generation)로만 가리키고, 어댑터가 컴포넌트의
// 아이디를 userData 로 달아 둔다. 메인 스레드 전용이다.
//
// **바디의 상태는 질량 중심의 위치와 각도다.** 트랜스폼 원점은 거기서 되짚는다. 기존 엔진은 임펄스를 면적
// 중심 기준으로 주면서 적분은 원점 기준으로 돌려, 중심이 원점에서 먼 도형(L·U)이 스텝마다 어긋났다
// (physics-plan §1.2 의 5).
namespace JBro::Physics2D
{
    namespace Internal
    {
        class WorkerPool;
    }

    // 물리 전용 워커의 상한이다(D-223). 명시한 값도 여기서 자른다.
    inline constexpr std::uint32_t MaxWorkerCount = 16;

    // 물리 일감에 맞는 워커 수(D-223). work 는 콜라이더 조각 수의 합이다. 처음(D-223)에는 좁은 판정만 나눠 이득이 작았다 - 실측에서
    // 1000 조각 아래는 잡음보다 나은 것이 없었고 2000 에서 약 20% 빨랐으며 워커 2~4 에서 멈췄다(physics-plan §4 의 8). 그래서 1024
    // 미만이면 0(메인 한 스레드), 1024 에 1 을 주고 1024 마다 하나씩 더하며, hardwareThreads - 1 과 4 를 넘지 않는다.
    // 접촉 풀이의 색 묶음도 나누면서(D-233, 풀 접촉 512 이상) 상자 2000 에서 약 2 배가 되었지만, 기준은 그대로 둔다.
    std::uint32_t RecommendWorkerCount(std::uint32_t work, std::uint32_t hardwareThreads);

    enum class BodyType : std::uint8_t
    {
        Static,
        Kinematic,
        Dynamic,
    };

    inline constexpr std::uint32_t InvalidIndex = 0xFFFFFFFFu;

    struct BodyId
    {
        std::uint32_t index = InvalidIndex;
        std::uint32_t generation = 0;
    };

    struct ShapeId
    {
        std::uint32_t index = InvalidIndex;
        std::uint32_t generation = 0;
    };

    struct JointId
    {
        std::uint32_t index = InvalidIndex;
        std::uint32_t generation = 0;
    };

    // **두 몸 사이의 거리를 지킨다**(D-232). bodyB 가 빈 번호면 몸 하나를 월드의 점(localAnchorB)에 잇는다.
    struct DistanceJointDef
    {
        BodyId bodyA;
        BodyId bodyB;
        // 몸의 로컬 점(트랜스폼 원점 기준, 크기를 곱한 값).
        Vec2   localAnchorA;
        Vec2   localAnchorB;
        float  length = 1.0f;
        // 참이면 밧줄이다: length 보다 멀어지지만 않게 하고 가까워지는 것은 막지 않는다.
        bool   maxLengthOnly = false;
        // 0 보다 크면 용수철이다(초당 떨림 수). 0 이면 단단하다. 밧줄에는 쓰지 않는다.
        float  hertz = 0.0f;
        float  dampingRatio = 0.0f;
        // 거짓이면 두 몸의 도형이 서로 부딪히지 않는다.
        bool   collideConnected = false;
    };

    // **한 점을 함께 쓰고 그 둘레로 돈다**(D-232). 각도는 라디안이다.
    struct HingeJointDef
    {
        BodyId bodyA;
        BodyId bodyB;
        Vec2   localAnchorA;
        Vec2   localAnchorB;
        // 만들 때의 상대 각도(B - A). 한계는 이것을 0 으로 잰다.
        float  referenceAngle = 0.0f;
        bool   enableLimit = false;
        float  lowerAngle = 0.0f;
        float  upperAngle = 0.0f;
        bool   enableMotor = false;
        // B 가 A 에 대해 도는 목표 속도(라디안/초)와, 그것을 위해 쓸 수 있는 가장 큰 토크.
        float  motorSpeed = 0.0f;
        float  maxMotorTorque = 0.0f;
        bool   collideConnected = false;
    };

    struct BodyDef
    {
        BodyType      type = BodyType::Dynamic;
        // 트랜스폼 원점과 각도(월드).
        Vec2          position;
        float         angle = 0.0f;
        // 질량 중심의 속도.
        Vec2          linearVelocity;
        float         angularVelocity = 0.0f;
        // Dynamic 의 전체 질량. 트리거가 아닌 도형의 넓이에 고르게 나눈다.
        float         mass = 1.0f;
        float         gravityScale = 1.0f;
        float         linearDamping = 0.0f;
        float         angularDamping = 0.0f;
        bool          fixedRotation = false;
        // 축 고정(D-227). 그 축으로는 움직이지 않는다 - 중력·힘·접촉 어느 것도 그 축의 속도를 만들지 못한다.
        bool          freezePositionX = false;
        bool          freezePositionY = false;
        // 잠들 수 있는가(D-229). 거짓이면 멈춰 있어도 늘 깨어 있다.
        bool          canSleep = true;
        std::uint64_t userData = 0;
    };

    struct ShapeDef
    {
        float         friction = 0.6f;
        float         restitution = 0.0f;
        bool          isTrigger = false;
        // 두 도형은 (A.layer & B.mask) 와 (B.layer & A.mask) 가 모두 0 이 아닐 때만 만난다.
        std::uint32_t layer = 0x00000001u;
        std::uint32_t mask = 0xFFFFFFFFu;
        std::uint64_t userData = 0;
        // 한 방향 발판(D-232). 몸의 로컬 위(+y) 쪽에서 오는 것만 막는다: 접촉이 시작될 때 발판에서 상대로 향하는 법선이
        // 그 위와 60° 안이면 막고, 아니면(아래나 옆에서 왔다) 그 접촉이 끝날 때까지 없는 것으로 본다.
        bool          oneWay = false;
    };

    // 도형 쌍이 닿기 시작했거나 떨어졌다. 한 도형의 조각 여럿에 닿아도 쌍의 이벤트는 하나다.
    // 도형이 지워져 끝난 접촉도 끝으로 알리므로, 번호가 이미 죽었을 수 있다 - userData 를 함께 싣는 이유다.
    struct ContactEvent
    {
        ShapeId       shapeA;
        ShapeId       shapeB;
        std::uint64_t userDataA = 0;
        std::uint64_t userDataB = 0;
        bool          isTrigger = false;
        // 시작 이벤트의 대표 접촉점(월드)과 A→B 법선. 조각 여럿이 닿으면 가장 깊은 곳의 것이다.
        // 트리거와 끝 이벤트는 0 이다 - 트리거는 매니폴드의 뜻이 없고, 끝은 이미 떨어졌다.
        Vec2          point;
        Vec2          normal;
    };

    struct WorldSettings
    {
        Vec2          gravity{ 0.0f, -9.81f };
        // 레이어 충돌 표(D-232). 비트 j 가 선 행 i 는 레이어 i 와 j 가 서로 지나간다(대칭으로 채운다). 두 도형은 한쪽 레이어 비트
        // i 와 다른 쪽 비트 j 가운데 떼어 두지 않은 쌍이 하나라도 있으면 만난다. 비어 있으면 모두 만난다.
        std::uint32_t ignoredLayers[32] = {};
        std::uint32_t subSteps = 4;
        std::uint32_t velocityIterations = 8;
        std::uint32_t positionIterations = 3;
        // 다가오는 속도가 이보다 느리면 반발하지 않는다. 쌓인 물체가 떨며 튀지 않게 한다.
        float         restitutionThreshold = 1.0f;
        // 수면(D-229). 접촉으로 이어진 몸들(섬)이 모두 이 속도 아래로 timeToSleep 동안 머물면 함께 잠든다. 잠든 몸은
        // 적분과 솔버에서 빠진다. 섬 안의 하나라도 빨라지면 섬이 함께 깬다.
        bool          enableSleep = true;
        float         timeToSleep = 0.5f;
        float         linearSleepTolerance = 0.05f;
        float         angularSleepTolerance = 0.0349f;
    };

    // 마지막 Step 의 좁은 판정 기록이다(D-223). 프로파일과 테스트가 본다.
    struct StepStats
    {
        // 마지막 서브스텝의 좁은 판정 후보(걸러진 브로드페이즈 쌍) 수.
        std::uint32_t candidates = 0;
        // 좁은 판정을 워커와 나눠 돈 서브스텝 수. 워커가 없거나 후보가 적으면 0 이다.
        std::uint32_t parallelSubSteps = 0;
        // 이어지는 판정이 몸을 멈춰 세운 횟수(서브스텝마다 몸 하나에 한 번).
        std::uint32_t continuousHits = 0;
        // 접촉 풀이의 색 묶음을 워커로 나눠 푼 횟수(D-233). 반복마다 센다.
        std::uint32_t parallelColors = 0;
        // Step 이 끝났을 때 깨어 있는·잠든 동적 몸의 수(D-229).
        std::uint32_t awakeBodies = 0;
        std::uint32_t sleepingBodies = 0;
    };

    class World
    {
    public:
        World();
        ~World();
        World(const World&) = delete;
        World& operator=(const World&) = delete;

        WorldSettings& Settings();
        const WorldSettings& Settings() const;

        BodyId CreateBody(const BodyDef& def);
        // 도형도 함께 지운다. 닿아 있던 쌍은 다음 Step 에서 끝 이벤트로 나온다.
        void   DestroyBody(BodyId body);
        bool   IsValid(BodyId body) const;

        // 외곽선은 바디 로컬(원점 기준, 크기를 곱한 뒤)이다. 오목하면 볼록 조각으로 나눠 자식으로 든다.
        // 실패하면 도형을 만들지 않고 out 을 비운다.
        PolygonError CreatePolygonShape(
            BodyId body, ArrayView<const Vec2> localOutline, const ShapeDef& def, ShapeId& out);
        ShapeId CreateCircleShape(BodyId body, const Circle& localCircle, const ShapeDef& def);
        // 선분 a-b(바디 로컬)에 반지름을 두른 캡슐. 조각 하나(두 점 + radius)로 든다. 두 점이 LinearSlop 안이면 원이다.
        ShapeId CreateCapsuleShape(BodyId body, Vec2 localA, Vec2 localB, float radius, const ShapeDef& def);
        // 체인(D-229): 점을 이은 선분 모음이고 조각마다 선분 하나다. 두께와 질량이 없고 두 면 모두에서 부딪힌다. loop 면 끝과 처음을
        // 잇는다. LinearSlop 안의 이웃 점은 합친다. 선분이 하나도 남지 않으면 만들지 않고 빈 번호를 돌려준다.
        ShapeId CreateChainShape(BodyId body, ArrayView<const Vec2> localPoints, bool loop, const ShapeDef& def);
        void    DestroyShape(ShapeId shape);

        // 도형의 모양만 바꾼다(크기 애니메이션, physics-plan §4 의 4 (1)). 번호와 표면 성질은 그대로라 닿아 있던 쌍이 계속 닿아
        // 있으면 끝·시작 이벤트가 나지 않는다. 모양의 종류(원·폴리곤·캡슐)도 바뀔 수 있다. 틀린 외곽선이면 모양을 두고 오류를 돌려준다.
        PolygonError SetPolygonGeometry(ShapeId shape, ArrayView<const Vec2> localOutline);
        bool         SetCircleGeometry(ShapeId shape, const Circle& localCircle);
        bool         SetCapsuleGeometry(ShapeId shape, Vec2 localA, Vec2 localB, float radius);
        bool         SetChainGeometry(ShapeId shape, ArrayView<const Vec2> localPoints, bool loop);
        // 마찰·반발·레이어·마스크를 바꾼다. 트리거 여부와 userData 는 도형을 새로 만들어야 바뀐다 - 트리거가 되면 훅의 종류가
        // 달라지므로 끝나고 새로 시작하는 것이 맞다.
        void         SetSurface(ShapeId shape, const ShapeDef& def);
        bool    IsValid(ShapeId shape) const;
        std::uint32_t GetChildCount(ShapeId shape) const;
        const ConvexPolygon* GetPolygonChild(ShapeId shape, std::uint32_t child) const;
        const ChainSegment*  GetChainChild(ShapeId shape, std::uint32_t child) const;

        // 원점과 각도를 옮긴다(순간 이동). 속도는 그대로다.
        void  SetTransform(BodyId body, Vec2 position, float angle);
        Vec2  GetPosition(BodyId body) const;
        float GetAngle(BodyId body) const;
        Vec2  GetWorldCenter(BodyId body) const;
        Vec2  GetLinearVelocity(BodyId body) const;
        void  SetLinearVelocity(BodyId body, Vec2 velocity);
        float GetAngularVelocity(BodyId body) const;
        void  SetAngularVelocity(BodyId body, float velocity);
        // 질량·로컬 질량 중심·그 중심 기준 관성. Static·Kinematic 은 0 이다.
        MassData GetMassData(BodyId body) const;
        // 몸의 성질을 제자리에서 바꾼다(D-227): 질량·중력 배율·감쇠·회전 고정·축 고정. 종류·자세·속도·userData 는 두고,
        // 번호가 그대로라 닿아 있던 쌍은 이어진다.
        void SetBodyProperties(BodyId body, const BodyDef& def);
        // 수면(D-229). 깨우면 섬이 다음 Step 에 함께 깬다. 순간 이동·속도·힘·충격량·성질·도형을 바꾸면 저절로 깬다.
        void SetAwake(BodyId body, bool awake);
        bool IsAwake(BodyId body) const;

        // 힘과 토크는 다음 Step 한 번 동안 서브스텝마다 가해지고 Step 끝에 비워진다. 충격량은 속도를 바로 바꾼다.
        // 동적인 몸만 받고, 고정한 축과 회전은 받지 않는다(D-227).
        void ApplyForce(BodyId body, Vec2 force, Vec2 worldPoint);
        void ApplyForceToCenter(BodyId body, Vec2 force);
        void ApplyTorque(BodyId body, float torque);
        void ApplyLinearImpulse(BodyId body, Vec2 impulse, Vec2 worldPoint);
        void ApplyLinearImpulseToCenter(BodyId body, Vec2 impulse);
        void ApplyAngularImpulse(BodyId body, float impulse);

        void Step(float deltaTime);

        // 좁은 판정을 나눌 물리 전용 워커 수(D-223). 0 이면 메인 한 스레드이고 워커를 세우지 않는다. `MaxWorkerCount` 에서 자르고,
        // 스레드가 없는 빌드(웹)는 늘 0 이다. 결과는 워커 수와 관계없이 같다 - 접촉을 후보 순서대로 모은 뒤 열쇠로 정렬한다.
        void          SetWorkerCount(std::uint32_t count);
        std::uint32_t GetWorkerCount() const;
        StepStats     GetLastStepStats() const;

        // **조인트**(D-232). 몸이 없거나 두 몸이 같으면 빈 번호다. 몸을 지우면 그 몸에 걸린 조인트도 사라진다.
        JointId CreateDistanceJoint(const DistanceJointDef& def);
        JointId CreateHingeJoint(const HingeJointDef& def);
        // 같은 두 몸의 조인트 성질을 제자리에서 바꾼다(쌓인 임펄스를 이어받는다). 몸이나 종류가 다르면 거짓이다 - 새로 만든다.
        bool SetDistanceJoint(JointId id, const DistanceJointDef& def);
        bool SetHingeJoint(JointId id, const HingeJointDef& def);
        void DestroyJoint(JointId id);
        bool IsValid(JointId id) const;
        std::size_t GetJointCount() const;
        // 경첩의 지금 상대 각도(B - A - 기준 각)다.
        float GetHingeAngle(JointId id) const;

        // 마지막 Step 의 이벤트. 다음 Step 이 비운다.
        ArrayView<const ContactEvent> GetBeginEvents() const;
        ArrayView<const ContactEvent> GetEndEvents() const;
        // 지난 Step 에도 닿아 있었고 이번에도 닿아 있는 쌍(D-232). 점과 법선은 이번 것이다. 시작한 Step 에는 시작만 있다.
        // 두 몸이 모두 잠들었거나 멈춰 있으면(정적·잠든 동적) 싣지 않는다 - 잠든 더미가 매 스텝 알림을 쏟지 않게 한다.
        ArrayView<const ContactEvent> GetStayEvents() const;

    private:
        struct Body
        {
            BodyType      type = BodyType::Dynamic;
            bool          alive = false;
            bool          fixedRotation = false;
            std::uint32_t generation = 0;
            Vec2          origin;
            float         angle = 0.0f;
            Rotation      rotation;
            Vec2          localCenter;
            Vec2          center;
            Vec2          linearVelocity;
            float         angularVelocity = 0.0f;
            float         requestedMass = 1.0f;
            float         mass = 0.0f;
            float         inverseMass = 0.0f;
            // 축마다의 역질량. 고정한 축은 0 이다. 접촉 임펄스와 위치 보정이 이것으로 몸을 민다.
            Vec2          inverseMassAxes;
            bool          freezePositionX = false;
            bool          freezePositionY = false;
            // 이번 Step 동안 가할 힘과 토크(질량 중심 기준).
            Vec2          force;
            float         torque = 0.0f;
            // 수면. 잠든 몸은 속도가 0 이고 움직이지 않는다.
            bool          awake = true;
            bool          canSleep = true;
            float         sleepTime = 0.0f;
            // 도형이 품은 가장 얇은 두께의 절반(원은 반지름, 조각은 중심에서 가장 가까운 변까지). 한 서브스텝에 이것보다 멀리 가면
            // 이어지는 판정(CCD)으로 정적인 도형을 뚫지 않게 한다(D-233). 도형이 없으면 0 이다.
            float         coreExtent = 0.0f;
            float         inertia = 0.0f;
            float         inverseInertia = 0.0f;
            float         gravityScale = 1.0f;
            float         linearDamping = 0.0f;
            float         angularDamping = 0.0f;
            std::uint64_t userData = 0;
            Array<std::uint32_t> shapes;
            // 서브스텝 시작의 중심과 각도. 위치 보정이 접촉점의 현재 깊이를 되짚는 기준이다.
            Vec2          startCenter;
            float         startAngle = 0.0f;
        };

        struct Shape
        {
            bool                 alive = false;
            bool                 isCircle = false;
            bool                 isChain = false;
            bool                 isTrigger = false;
            std::uint32_t        generation = 0;
            std::uint32_t        body = InvalidIndex;
            Circle               circle;
            Array<ConvexPolygon> pieces;
            Array<ChainSegment>  segments;
            float                friction = 0.6f;
            float                restitution = 0.0f;
            std::uint32_t        layer = 1;
            std::uint32_t        mask = 0xFFFFFFFFu;
            std::uint64_t        userData = 0;
            bool                 oneWay = false;
        };

        struct Proxy
        {
            std::uint32_t shape = 0;
            std::uint32_t child = 0;
        };

        struct Contact
        {
            // 열쇠. 이 넷이 같으면 다음 서브스텝에서 같은 접촉이고 누적 임펄스를 이어받는다.
            std::uint32_t shapeA = 0;
            std::uint32_t childA = 0;
            std::uint32_t shapeB = 0;
            std::uint32_t childB = 0;
            std::uint32_t bodyA = 0;
            std::uint32_t bodyB = 0;
            bool          isTrigger = false;
            // 한 방향 발판이 이 접촉을 흘려보낸다. 시작할 때 정하고, 같은 열쇠의 접촉이 이어지는 동안 물려받는다.
            bool          disabled = false;
            float         friction = 0.0f;
            float         restitution = 0.0f;
            Manifold      manifold;
            float         normalImpulse[2] = {};
            float         tangentImpulse[2] = {};
            // 준비 단계에서 채운다.
            Vec2          anchorA[2];
            Vec2          anchorB[2];
            float         baseSeparation[2] = {};
            float         normalMass[2] = {};
            float         tangentMass[2] = {};
            float         approachSpeed[2] = {};
        };

        // 브로드페이즈 쌍 가운데 걸러진 것. 좁은 판정은 이것마다 따로 돌 수 있다(A 가 폴리곤-원의 폴리곤 쪽).
        struct Candidate
        {
            std::uint32_t shapeA = 0;
            std::uint32_t childA = 0;
            std::uint32_t shapeB = 0;
            std::uint32_t childB = 0;
            bool          isTrigger = false;
        };

        struct TouchingPair
        {
            std::uint32_t shapeA = 0;
            std::uint32_t shapeB = 0;
            std::uint32_t generationA = 0;
            std::uint32_t generationB = 0;
            std::uint64_t userDataA = 0;
            std::uint64_t userDataB = 0;
            bool          isTrigger = false;
            float         depth = 0.0f;
            Vec2          point;
            Vec2          normal;
        };

        Body*        FindBody(BodyId body);
        const Body*  FindBody(BodyId body) const;
        Shape*       FindShape(ShapeId shape);
        const Shape* FindShape(ShapeId shape) const;
        ShapeId      AddShape(std::uint32_t bodyIndex, const ShapeDef& def);
        // 점 모음을 체인 선분으로 만든다. 선분이 없으면 거짓이고 out 은 비어 있다.
        static bool  BuildChain(ArrayView<const Vec2> points, bool loop, Array<Vec2>& filtered, Array<ChainSegment>& out);
        void         UpdateMass(Body& body);
        void         SyncOrigin(Body& body);

        void IntegrateVelocities(float h);
        void Collide();
        // 워커에서도 돈다. 도형·바디를 읽기만 하고 제 칸의 매니폴드만 쓴다.
        Manifold    ComputeManifold(const Candidate& candidate) const;
        static void CollideCandidates(void* context, std::uint32_t begin, std::uint32_t end);
        void PrepareContacts();
        void WarmStart();
        void SolveVelocities(float h);
        void ApplyRestitution();
        void IntegratePositions(float h);
        void SolvePositions();
        void UpdateTouching();
        // 섬을 묶어 잠재우고 깨운다. Step 끝에 한 번 돈다.
        void UpdateSleep(float deltaTime);
        void WakeBody(Body& body);
        void WakeSleepingIn(const Contact& contact);
        std::uint32_t FindIsland(std::uint32_t body);
        // 깨어 있는 동적 몸이 끼어야 접촉을 푼다. 둘 다 잠들었거나 멈춘 몸이면 풀 것이 없다.
        bool IsSolved(const Contact& contact) const;
        // **접촉 색칠**(D-233). 움직이는 몸을 함께 쓰지 않는 접촉끼리 한 색으로 묶는다. 한 색 안의 접촉은 서로 모르고 풀 수
        // 있어 워커로 나눈다. 워커 수와 관계없이 늘 이 순서로 푼다 - 결과가 워커 수에 따라 달라지지 않는다.
        void ColorContacts();
        using ContactJob = void (*)(void* context, std::uint32_t begin, std::uint32_t end);
        void ForEachColor(ContactJob job, bool allowParallel);
        void WarmStartContact(Contact& contact);
        void SolveContactVelocity(Contact& contact, float inverseH);
        void ApplyContactRestitution(Contact& contact);
        void SolveContactPosition(const Contact& contact);
        static void WarmStartJob(void* context, std::uint32_t begin, std::uint32_t end);
        static void SolveVelocityJob(void* context, std::uint32_t begin, std::uint32_t end);
        static void RestitutionJob(void* context, std::uint32_t begin, std::uint32_t end);
        static void SolvePositionJob(void* context, std::uint32_t begin, std::uint32_t end);
        // 빠른 몸을 정적·키네마틱 도형 앞에서 멈춘다(D-233). startCenter 는 이 서브스텝이 시작할 때의 질량 중심이다.
        void ClampToFirstHit(Body& body, std::uint32_t bodyIndex, Vec2 startCenter);

        enum class JointType : std::uint8_t
        {
            Distance,
            Hinge,
        };

        struct Joint
        {
            bool             alive = false;
            std::uint32_t    generation = 0;
            JointType        type = JointType::Distance;
            // B 가 InvalidIndex 면 월드다(움직이지 않는 빈 몸 m_ground 로 푼다).
            std::uint32_t    bodyA = InvalidIndex;
            std::uint32_t    bodyB = InvalidIndex;
            // 정의. 몸 번호 칸은 bodyA·bodyB 가 대신한다.
            DistanceJointDef distance;
            HingeJointDef    hinge;
            bool             collideConnected = false;
            // 준비 단계가 채운다.
            Vec2             rA;
            Vec2             rB;
            Vec2             axis;
            float            currentLength = 0.0f;
            float            mass = 0.0f;
            float            softMass = 0.0f;
            float            gamma = 0.0f;
            float            bias = 0.0f;
            float            k11 = 0.0f;
            float            k12 = 0.0f;
            float            k22 = 0.0f;
            float            axialMass = 0.0f;
            float            angle = 0.0f;
            // 쌓인 임펄스. Step 을 넘어 이어진다(따뜻한 시작).
            float            impulse = 0.0f;
            float            lowerImpulse = 0.0f;
            float            upperImpulse = 0.0f;
            float            motorImpulse = 0.0f;
            Vec2             linearImpulse;
        };

        JointId      AddJoint(JointType type, BodyId bodyA, BodyId bodyB, bool collideConnected);
        Joint*       FindJoint(JointId id);
        const Joint* FindJoint(JointId id) const;
        Body&        JointBody(std::uint32_t index);
        bool         IsJointSolved(const Joint& joint) const;
        void         PrepareJoints(float h);
        void         WarmStartJoints();
        void         SolveJoints(float h);
        void         SolveJointPositions();
        // collideConnected 가 거짓인 조인트가 이은 몸 쌍. 값은 그런 조인트의 수다.
        static std::uint64_t JointPairKey(std::uint32_t bodyA, std::uint32_t bodyB);
        void         AddJointFilter(const Joint& joint);
        void         RemoveJointFilter(const Joint& joint);
        bool PassesOneWay(const Contact& contact) const;
        bool LayersMeet(std::uint32_t layerA, std::uint32_t layerB) const;

        WorldSettings        m_settings;
        Array<Body>          m_bodies;
        Array<Shape>         m_shapes;
        Array<std::uint32_t> m_freeBodies;
        Array<std::uint32_t> m_freeShapes;

        // 매 서브스텝 스크래치. 용량이 찬 뒤로는 할당하지 않는다.
        Array<Proxy>         m_proxies;
        Array<Rect>          m_proxyBounds;
        Array<ProxyPair>     m_pairs;
        SweepAndPrune        m_broadPhase;
        Array<Contact>       m_contacts;
        Array<Contact>       m_previousContacts;
        Array<TouchingPair>  m_touching;
        Array<TouchingPair>  m_previousTouching;
        Array<ContactEvent>  m_beginEvents;
        Array<Candidate>     m_candidates;
        Array<Manifold>      m_candidateManifolds;
        // 워커 수가 0 이 아닐 때만 있다.
        OwnerPtr<Internal::WorkerPool> m_workers;
        StepStats            m_lastStats;
        // 섬 찾기(합치고 찾기)의 부모 표와 섬마다의 가장 짧은 잠든 시간. 용량이 찬 뒤로는 할당하지 않는다.
        Array<std::uint32_t> m_islandParent;
        Array<float>         m_islandSleepTime;
        Vec2                 m_lastGravity{ 0.0f, -9.81f };
        // 모양 바꾸기의 분해 결과. 도형의 조각 배열과 맞바꿔 두 배열 모두 용량이 남는다.
        Array<ConvexPolygon> m_scratchPieces;
        DecomposeScratch     m_decompose;
        Array<ChainSegment>  m_scratchSegments;
        Array<Vec2>          m_scratchChainPoints;
        // 질량을 모으는 자리. 모양을 바꿀 때마다 부르므로 용량을 남겨 둔다.
        Array<MassData>      m_massParts;
        Array<ContactEvent>  m_endEvents;
        Array<ContactEvent>  m_stayEvents;
        // 색칠 스크래치. 색 64 는 넘친 것이라 한 스레드에서 푼다. 용량이 찬 뒤로는 할당하지 않는다.
        Array<std::uint64_t> m_bodyColors;
        Array<std::uint8_t>  m_contactColors;
        Array<std::uint32_t> m_colorOrder;
        std::uint32_t        m_colorStarts[66] = {};
        // 색 하나를 나눠 풀 때 워커가 읽는 값. 나누기 전에 적고 도는 동안 바꾸지 않는다.
        std::uint32_t        m_colorOffset = 0;
        float                m_solveInverseH = 0.0f;
        Array<Joint>         m_joints;
        Array<std::uint32_t> m_freeJoints;
        Table<std::uint64_t, std::uint32_t> m_jointFilters;
        // 월드에 거는 조인트의 상대다. 정적이고 질량이 없다.
        Body                 m_ground;
    };
}
