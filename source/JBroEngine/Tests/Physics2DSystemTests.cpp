#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2DSystem/System/Physics2DSystem.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/Array.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

// 2D 물리 어댑터(`System::Physics2DSystem`) 테스트(D-199·D-203, physics-plan §4 의 4 단계).
// 캔버스를 세우고 컴포넌트만으로 장면을 짠 뒤 시스템을 스텝한다. 커널 자체의 정확성은 Physics2D*Tests 가 잰다 -
// 여기서 보는 것은 컴포넌트와 커널 사이의 오감(동기화·되쓰기·이벤트 발송·질의)이다.
namespace
{
    using JBro::Array;
    using JBro::Vec2;
    using JBro::Component::BodyType2D;
    using JBro::Component::Collider2D;
    using JBro::Component::ColliderShape2D;
    using JBro::Component::Rigidbody2D;
    using JBro::Component::Transform2D;

    constexpr float Frame = 1.0f / 60.0f;
    constexpr float Slop = 0.005f;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    bool Near(float actual, float expected, float tolerance)
    {
        return std::fabs(actual - expected) <= tolerance;
    }

    // 받은 훅을 센다. 오브젝트마다 하나씩 붙인다.
    class ContactProbe final : public JBro::GameScript2D
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Tests::ContactProbe";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }

        void OnCollisionEnter(const JBro::Collision2D& hit) override
        {
            ++collisionEnter;
            lastEnter = hit;
        }

        void OnCollisionExit(const JBro::Collision2D& hit) override
        {
            ++collisionExit;
            lastExit = hit;
        }

        void OnTriggerEnter(const JBro::Collision2D& hit) override
        {
            ++triggerEnter;
            lastEnter = hit;
        }

        void OnTriggerExit(const JBro::Collision2D& hit) override
        {
            ++triggerExit;
        }

        int collisionEnter = 0;
        int collisionExit = 0;
        int triggerEnter = 0;
        int triggerExit = 0;
        JBro::Collision2D lastEnter;
        JBro::Collision2D lastExit;
    };

    struct Scene
    {
        JBro::Canvas canvas{ JBro::CreateDefaultAllocator() };
        JBro::System::Physics2DSystem physics;

        Scene()
        {
            physics.Initialize(canvas);
        }

        ~Scene()
        {
            physics.Shutdown(canvas);
        }

        JBro::GameObject* Object(const char* name, Vec2 position)
        {
            JBro::GameObject* object = canvas.CreateObject(name);
            canvas.AttachComponent<Transform2D>(object)->position = position;
            return object;
        }

        Collider2D* Box(JBro::GameObject* object, Vec2 size)
        {
            Collider2D* collider = canvas.AttachComponent<Collider2D>(object);
            collider->shape = ColliderShape2D::Box;
            collider->size = size;
            return collider;
        }

        Rigidbody2D* Dynamic(JBro::GameObject* object)
        {
            return canvas.AttachComponent<Rigidbody2D>(object);
        }

        ContactProbe* Probe(JBro::GameObject* object)
        {
            return canvas.AttachComponent<ContactProbe>(object);
        }

        void Run(float seconds)
        {
            const int steps = static_cast<int>(seconds / Frame + 0.5f);
            for (int i = 0; i < steps; ++i)
            {
                physics.FixedUpdate(canvas, Frame);
            }
        }

        Transform2D* TransformOf(JBro::GameObject* object)
        {
            return canvas.FindComponentRaw<Transform2D>(object);
        }
    };

    Array<Vec2> UOutline()
    {
        return { { 0, 0 }, { 3, 0 }, { 3, 3 }, { 2, 3 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } };
    }

    // **떨어진 상자가 바닥에 얹히고, 양쪽 스크립트가 시작을 한 번씩 받는다.** 법선은 각자 자기 → 상대 쪽이다.
    // 순간 이동으로 떼면 끝을 한 번씩 받는다. 콜라이더만 있는 바닥은 정적인 몸이다.
    void TestAFallingBoxLandsAndBothScriptsHearIt()
    {
        Scene scene;
        JBro::GameObject* ground = scene.Object("ground", { 0, -0.5f });
        scene.Box(ground, { 40, 1 });
        ContactProbe* groundProbe = scene.Probe(ground);

        JBro::GameObject* box = scene.Object("box", { 0, 2 });
        scene.Box(box, { 1, 1 });
        Rigidbody2D* body = scene.Dynamic(box);
        ContactProbe* boxProbe = scene.Probe(box);

        scene.Run(0.3f);
        Check(body->linearVelocity.y < -2.0f, "while falling the kernel's velocity is written back to the component");
        // Transform2DSystem 이 채운 캐시를 흉내 낸다. 물리가 로컬을 옮겼으면 이 캐시는 헌 것이어야 한다.
        scene.TransformOf(box)->worldValid = true;
        scene.Run(1.7f);
        Check(scene.physics.GetBodyCount() == 2 && scene.physics.GetShapeCount() == 2,
            "one body and one shape per object reach the kernel");
        Check(Near(scene.TransformOf(box)->position.y, 0.5f, 2.0f * Slop), "the box rests on the ground");
        Check(std::fabs(body->linearVelocity.y) < 0.01f, "and its velocity is written back as still");
        Check(Near(scene.TransformOf(ground)->position.y, -0.5f, 0.0f), "the static ground never moves");
        Check(false == scene.TransformOf(box)->worldValid, "moving the box invalidates its world cache");

        Check(boxProbe->collisionEnter == 1 && groundProbe->collisionEnter == 1, "both scripts hear the landing once");
        Check(boxProbe->lastEnter.other.GetInstanceId() == ground->GetInstanceId(), "the box hears about the ground");
        Check(groundProbe->lastEnter.other.GetInstanceId() == box->GetInstanceId(), "and the ground about the box");
        Check(Near(boxProbe->lastEnter.normal.y, -1.0f, 1.0e-3f), "the box's normal points from itself down to the ground");
        Check(Near(groundProbe->lastEnter.normal.y, 1.0f, 1.0e-3f), "the ground's points up to the box");
        Check(boxProbe->lastEnter.bodyType == BodyType2D::Static, "the box learns the ground is static");
        Check(groundProbe->lastEnter.bodyType == BodyType2D::Dynamic, "and the ground that the box is dynamic");
        Check(Near(boxProbe->lastEnter.point.y, 0.0f, 0.05f), "the contact point is on the ground's top face");
        Check(boxProbe->collisionExit == 0 && boxProbe->triggerEnter == 0, "nothing else is heard");

        // 스크립트가 자리를 옮기면 커널이 따라간다(순간 이동).
        scene.TransformOf(box)->position = { 0, 5 };
        scene.physics.FixedUpdate(scene.canvas, Frame);
        Check(scene.TransformOf(box)->position.y > 4.9f, "a teleported box starts from where the script put it");
        Check(boxProbe->collisionExit == 1 && groundProbe->collisionExit == 1, "and both scripts hear it leave once");
        Check(boxProbe->lastExit.normal.x == 0.0f && boxProbe->lastExit.normal.y == 0.0f,
            "an exit carries no normal");
    }

    // **오목한 폴리곤 콜라이더.** 넓은 상자가 U 의 두 기둥에 함께 얹혀도 시작은 한 번이고, U 의 홈에 떨어뜨린 상자는
    // 안벽을 뚫지 않고 홈 바닥에 선다 - 기존 엔진이 틀렸던 두 장면이다(physics-plan §1.2).
    void TestAConcavePolygonColliderHoldsWhatFallsOnAndIntoIt()
    {
        Scene scene;
        JBro::GameObject* cup = scene.Object("cup", { 0, 0 });
        Collider2D* polygon = scene.canvas.AttachComponent<Collider2D>(cup);
        polygon->shape = ColliderShape2D::Polygon;
        polygon->points = UOutline();
        ContactProbe* cupProbe = scene.Probe(cup);

        JBro::GameObject* lid = scene.Object("lid", { 1.5f, 3.6f });
        scene.Box(lid, { 3, 1 });
        scene.Dynamic(lid);

        JBro::GameObject* pebble = scene.Object("pebble", { 1.5f, 2.0f });
        scene.Box(pebble, { 0.6f, 0.6f });
        scene.Dynamic(pebble);

        scene.Run(2.0f);
        Check(Near(scene.TransformOf(lid)->position.y, 3.5f, 2.0f * Slop), "the lid rests on both pillars");
        const Vec2 pebblePosition = scene.TransformOf(pebble)->position;
        Check(Near(pebblePosition.y, 1.3f, 2.0f * Slop), "the pebble lands on the notch floor");
        Check(pebblePosition.x > 1.3f - Slop && pebblePosition.x < 1.7f + Slop, "between the inner walls");
        Check(cupProbe->collisionEnter == 2, "the cup hears each of the two once, though the lid touches two pieces");
    }

    // **트리거는 밀지 않고 트리거 훅만 부른다.**
    void TestATriggerReportsWithoutPushing()
    {
        Scene scene;
        JBro::GameObject* zone = scene.Object("zone", { 0, 2 });
        Collider2D* sensor = scene.Box(zone, { 4, 1 });
        sensor->isTrigger = true;
        ContactProbe* zoneProbe = scene.Probe(zone);

        JBro::GameObject* ball = scene.Object("ball", { 0, 4 });
        Collider2D* round = scene.canvas.AttachComponent<Collider2D>(ball);
        round->shape = ColliderShape2D::Circle;
        round->radius = 0.25f;
        scene.Dynamic(ball);
        ContactProbe* ballProbe = scene.Probe(ball);

        scene.Run(1.5f);
        Check(scene.TransformOf(ball)->position.y < 1.0f, "the ball falls straight through the zone");
        Check(zoneProbe->triggerEnter == 1 && zoneProbe->triggerExit == 1, "the zone hears it enter and leave once");
        Check(ballProbe->triggerEnter == 1 && ballProbe->triggerExit == 1, "and so does the ball");
        Check(zoneProbe->collisionEnter == 0 && ballProbe->collisionEnter == 0, "no collision hook is called");
        Check(zoneProbe->lastEnter.normal.x == 0.0f && zoneProbe->lastEnter.normal.y == 0.0f,
            "a trigger carries no normal");
    }

    // **닿아 있던 상대가 사라지면 남은 쪽이 끝을 받는다.** 콜라이더를 끄는 것도 같다.
    void TestLosingAPartnerEndsTheContact()
    {
        Scene scene;
        JBro::GameObject* ground = scene.Object("ground", { 0, -0.5f });
        Collider2D* floor = scene.Box(ground, { 40, 1 });

        JBro::GameObject* box = scene.Object("box", { 0, 0.5f });
        scene.Box(box, { 1, 1 });
        scene.Dynamic(box);
        ContactProbe* boxProbe = scene.Probe(box);

        scene.Run(0.5f);
        Check(boxProbe->collisionEnter == 1, "the box starts on the ground");

        floor->SetEnabled(false);
        scene.physics.FixedUpdate(scene.canvas, Frame);
        Check(boxProbe->collisionExit == 1, "switching the ground's collider off ends the contact");
        Check(boxProbe->lastExit.other.GetInstanceId() == ground->GetInstanceId(), "naming the ground");

        floor->SetEnabled(true);
        scene.TransformOf(box)->position = { 0, 0.5f };
        scene.Run(0.5f);
        Check(boxProbe->collisionEnter == 2, "switching it back on starts a new contact");

        Check(scene.canvas.DestroyObject(ground), "the ground is destroyed");
        scene.physics.FixedUpdate(scene.canvas, Frame);
        Check(boxProbe->collisionExit == 2, "destroying the ground ends the contact too");
        Check(false == boxProbe->lastExit.other.IsValid(), "and the handle to the destroyed ground is dead");
        Check(scene.physics.GetBodyCount() == 1, "the ground's body leaves the kernel");
    }

    // **바디가 남는 쪽의 콜라이더를 꺼도 그 도형은 빠진다.** 위에서는 끈 쪽이 바디째 사라져 도형도 함께 없어졌다.
    // 여기서는 Rigidbody2D 가 있는 상자의 콜라이더를 끈다 - 상자는 바닥을 지나 떨어지고, 두 쪽 다 끝을 받는다.
    void TestSwitchingOffAMovingBodysColliderLetsItFall()
    {
        Scene scene;
        JBro::GameObject* ground = scene.Object("ground", { 0, -0.5f });
        scene.Box(ground, { 40, 1 });
        ContactProbe* groundProbe = scene.Probe(ground);
        JBro::GameObject* box = scene.Object("box", { 0, 0.5f });
        Collider2D* shape = scene.Box(box, { 1, 1 });
        scene.Dynamic(box);
        scene.Run(0.5f);
        Check(groundProbe->collisionEnter == 1, "the box starts on the ground");

        shape->SetEnabled(false);
        scene.Run(0.5f);
        Check(scene.physics.GetBodyCount() == 2 && scene.physics.GetShapeCount() == 1,
            "the box keeps its body but loses its shape");
        Check(scene.TransformOf(box)->position.y < 0.0f, "so it falls through the ground");
        Check(groundProbe->collisionExit == 1, "and the ground hears it leave");
    }

    // **재생을 멈췄다 다시 켜면 끝 이벤트가 튀지 않고, 닿아 있는 것은 처음처럼 시작한다.**
    void TestRestartingDoesNotReplayOldContacts()
    {
        Scene scene;
        JBro::GameObject* ground = scene.Object("ground", { 0, -0.5f });
        scene.Box(ground, { 40, 1 });
        JBro::GameObject* box = scene.Object("box", { 0, 0.5f });
        scene.Box(box, { 1, 1 });
        scene.Dynamic(box);
        ContactProbe* boxProbe = scene.Probe(box);
        scene.Run(0.5f);
        Check(boxProbe->collisionEnter == 1, "the box starts on the ground");

        scene.physics.Shutdown(scene.canvas);
        Check(scene.physics.GetBodyCount() == 0, "shutting down drops the kernel");
        scene.physics.Initialize(scene.canvas);
        scene.physics.FixedUpdate(scene.canvas, Frame);
        Check(boxProbe->collisionExit == 0, "no stale exit follows a restart");
        Check(boxProbe->collisionEnter == 2, "the contact begins again");
    }

    // **질의는 지금의 컴포넌트를 보고, 폴리곤과 돌린 상자도 맞게 잰다.**
    void TestQueriesSeePolygonsAndRotatedBoxes()
    {
        Scene scene;
        JBro::GameObject* cup = scene.Object("cup", { 0, 0 });
        Collider2D* polygon = scene.canvas.AttachComponent<Collider2D>(cup);
        polygon->shape = ColliderShape2D::Polygon;
        polygon->points = UOutline();

        JBro::GameObject* diamond = scene.Object("diamond", { 10, 0 });
        scene.TransformOf(diamond)->rotation = 0.78539816f;
        scene.Box(diamond, { 2, 2 });

        JBro::Collision2D hit;
        const JBro::System::IPhysics2DSystem& queries = scene.physics;
        Check(queries.Raycast({ 1.5f, 5 }, { 0, -1 }, 10, hit), "a ray dropped into the notch hits the cup");
        Check(Near(hit.point.y, 1.0f, 1.0e-4f) && Near(hit.normal.y, 1.0f, 1.0e-4f),
            "on the notch floor, not across the notch's mouth");
        Check(hit.other.GetInstanceId() == cup->GetInstanceId(), "naming the cup");

        Check(queries.Raycast({ 5, 0 }, { 1, 0 }, 10, hit), "a ray hits the rotated box");
        Check(Near(hit.point.x, 10.0f - std::sqrt(2.0f), 1.0e-4f), "at its corner, not at an unrotated face");

        Array<JBro::GameObjectHandle> overlaps;
        queries.OverlapBox({ { 1.2f, 1.5f }, { 1.8f, 2.5f } }, overlaps);
        Check(overlaps.IsEmpty(), "a box inside the notch overlaps nothing");
        queries.OverlapBox({ { 0.5f, 1.5f }, { 1.2f, 2.5f } }, overlaps);
        Check(overlaps.Size() == 1 && overlaps[0].GetInstanceId() == cup->GetInstanceId(),
            "one reaching into the left pillar overlaps the cup");

        // 질의는 스텝을 기다리지 않는다. 옮긴 직후에 바로 맞다.
        scene.TransformOf(cup)->position = { 0, 10 };
        Check(false == queries.Raycast({ 1.5f, 5 }, { 0, -1 }, 3, hit), "a query sees the cup's new place at once");
    }

    // **정적인 몸은 옮긴 자리로 따라간다.** 에디터나 스크립트가 바닥을 내리면 그 위의 상자도 따라 내려간다.
    void TestAStaticBodyFollowsItsTransform()
    {
        Scene scene;
        JBro::GameObject* ground = scene.Object("ground", { 0, -0.5f });
        scene.Box(ground, { 40, 1 });
        JBro::GameObject* box = scene.Object("box", { 0, 0.5f });
        scene.Box(box, { 1, 1 });
        scene.Dynamic(box);
        scene.Run(0.5f);
        Check(Near(scene.TransformOf(box)->position.y, 0.5f, 2.0f * Slop), "the box starts on the ground");

        scene.TransformOf(ground)->position = { 0, -3.5f };
        scene.Run(1.5f);
        Check(Near(scene.TransformOf(box)->position.y, -2.5f, 2.0f * Slop), "and ends on the ground's new place");
    }

    // **트랜스폼의 크기는 도형에 곱해진다.** 두 배로 키운 상자는 두 배 큰 자리에서 멈춘다.
    void TestScaleGrowsTheShape()
    {
        Scene scene;
        JBro::GameObject* ground = scene.Object("ground", { 0, -0.5f });
        scene.Box(ground, { 40, 1 });
        JBro::GameObject* box = scene.Object("box", { 0, 3 });
        scene.TransformOf(box)->scale = { 2, 2 };
        scene.Box(box, { 1, 1 });
        scene.Dynamic(box);
        scene.Run(2.0f);
        Check(Near(scene.TransformOf(box)->position.y, 1.0f, 2.0f * Slop), "a box scaled by two rests one unit up");
    }

    // **질량 중심이 원점에서 먼 몸도 제자리에서 돈다.** L 자 콜라이더를 중력 없이 돌리면 원점이 중심 둘레를 돈다.
    void TestAnOffCenterBodyTurnsAboutItsCenterOfMass()
    {
        Scene scene;
        scene.physics.SetGravity({ 0, 0 });
        JBro::GameObject* l = scene.Object("l", { 0, 0 });
        Collider2D* shape = scene.canvas.AttachComponent<Collider2D>(l);
        shape->shape = ColliderShape2D::Polygon;
        shape->points = { { 0, 0 }, { 2, 0 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } };
        Rigidbody2D* body = scene.Dynamic(l);
        body->angularVelocity = 3.14159265f;
        scene.Run(1.0f);
        // L 의 면적 중심은 (0.75, 1.25) 다(2x1 바닥과 1x2 기둥, 넓이가 같다). 반 바퀴 돌면 원점은 중심을 지나 맞은편으로
        // 간다: 원점 = 2·중심. 기존 엔진처럼 원점을 기준으로 돌렸다면 원점은 (0, 0) 에 남는다.
        const Vec2 position = scene.TransformOf(l)->position;
        Check(Near(scene.TransformOf(l)->rotation, 3.14159265f, 1.0e-3f), "half a turn in a second");
        Check(Near(position.x, 1.5f, 1.0e-3f) && Near(position.y, 2.5f, 1.0e-3f),
            "and the origin lands across the center of mass");
        Check(Near(body->angularVelocity, 3.14159265f, 1.0e-4f), "keeping its spin");
    }
}

int RunPhysics2DSystemTests()
{
    TestAFallingBoxLandsAndBothScriptsHearIt();
    TestAConcavePolygonColliderHoldsWhatFallsOnAndIntoIt();
    TestATriggerReportsWithoutPushing();
    TestLosingAPartnerEndsTheContact();
    TestSwitchingOffAMovingBodysColliderLetsItFall();
    TestRestartingDoesNotReplayOldContacts();
    TestQueriesSeePolygonsAndRotatedBoxes();
    TestAStaticBodyFollowsItsTransform();
    TestScaleGrowsTheShape();
    TestAnOffCenterBodyTurnsAboutItsCenterOfMass();
    std::cout << "Physics2D system tests passed.\n";
    return 0;
}
