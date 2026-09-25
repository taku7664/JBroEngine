#pragma once

#include <JBro/Framework2D/Math2D.h>
#include <JBro/Physics2D/BroadPhase.h>
#include <JBro/Physics2D/Collision.h>
#include <JBro/Physics2D/Geometry.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/ArrayView.h>

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
    };

    struct WorldSettings
    {
        Vec2          gravity{ 0.0f, -9.81f };
        std::uint32_t subSteps = 4;
        std::uint32_t velocityIterations = 8;
        std::uint32_t positionIterations = 3;
        // 다가오는 속도가 이보다 느리면 반발하지 않는다. 쌓인 물체가 떨며 튀지 않게 한다.
        float         restitutionThreshold = 1.0f;
    };

    class World
    {
    public:
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
        void    DestroyShape(ShapeId shape);
        bool    IsValid(ShapeId shape) const;
        std::uint32_t GetChildCount(ShapeId shape) const;
        const ConvexPolygon* GetPolygonChild(ShapeId shape, std::uint32_t child) const;

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

        void Step(float deltaTime);

        // 마지막 Step 의 이벤트. 다음 Step 이 비운다.
        ArrayView<const ContactEvent> GetBeginEvents() const;
        ArrayView<const ContactEvent> GetEndEvents() const;

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
            bool                 isTrigger = false;
            std::uint32_t        generation = 0;
            std::uint32_t        body = InvalidIndex;
            Circle               circle;
            Array<ConvexPolygon> pieces;
            float                friction = 0.6f;
            float                restitution = 0.0f;
            std::uint32_t        layer = 1;
            std::uint32_t        mask = 0xFFFFFFFFu;
            std::uint64_t        userData = 0;
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

        struct TouchingPair
        {
            std::uint32_t shapeA = 0;
            std::uint32_t shapeB = 0;
            std::uint32_t generationA = 0;
            std::uint32_t generationB = 0;
            std::uint64_t userDataA = 0;
            std::uint64_t userDataB = 0;
            bool          isTrigger = false;
        };

        Body*        FindBody(BodyId body);
        const Body*  FindBody(BodyId body) const;
        Shape*       FindShape(ShapeId shape);
        const Shape* FindShape(ShapeId shape) const;
        ShapeId      AddShape(std::uint32_t bodyIndex, const ShapeDef& def);
        void         UpdateMass(Body& body);
        void         SyncOrigin(Body& body);

        void IntegrateVelocities(float h);
        void Collide();
        void PrepareContacts();
        void WarmStart();
        void SolveVelocities(float h);
        void ApplyRestitution();
        void IntegratePositions(float h);
        void SolvePositions();
        void UpdateTouching();

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
        Array<ContactEvent>  m_endEvents;
    };
}
