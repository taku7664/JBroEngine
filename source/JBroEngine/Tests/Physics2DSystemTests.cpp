#include <cstring>
#include <JBro/Framework2DSystem/BuiltinComponentTypes2D.h>
#include <JBro/Framework2D/BuiltinComponentProperties2D.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2DSystem/PhysicsThreads.h>
#include <JBro/Framework2DSystem/System/Physics2DSystem.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/Array.h>

#include <cmath>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
#include <iostream>
#include <stdexcept>

// 2D 물리 어댑터(`System::Physics2DSystem`) 테스트(D-199·D-207, physics-plan §4 의 4 단계).
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

        void OnCollisionStay(const JBro::Collision2D& hit) override
        {
            ++collisionStay;
            lastStay = hit;
        }

        void OnCollisionExit(const JBro::Collision2D& hit) override
        {
            ++collisionExit;
            lastExit = hit;
        }

        void OnTriggerStay(const JBro::Collision2D& hit) override
        {
            ++triggerStay;
            lastStay = hit;
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
        int collisionStay = 0;
        int collisionExit = 0;
        int triggerEnter = 0;
        int triggerStay = 0;
        int triggerExit = 0;
        JBro::Collision2D lastEnter;
        JBro::Collision2D lastStay;
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
        scene.TransformOf(diamond)->SetRotationRadian(JBro::Radian(0.78539816f));
        scene.Box(diamond, { 2, 2 });

        JBro::RaycastHit2D hit;
        const JBro::System::IPhysics2DSystem& queries = scene.physics;
        Check(queries.Raycast({ 1.5f, 5 }, { 0, -1 }, 10, hit, JBro::AllPhysicsLayers), "a ray dropped into the notch hits the cup");
        Check(Near(hit.point.y, 1.0f, 1.0e-4f) && Near(hit.normal.y, 1.0f, 1.0e-4f),
            "on the notch floor, not across the notch's mouth");
        Check(hit.other.GetInstanceId() == cup->GetInstanceId(), "naming the cup");

        Check(queries.Raycast({ 5, 0 }, { 1, 0 }, 10, hit, JBro::AllPhysicsLayers), "a ray hits the rotated box");
        Check(Near(hit.point.x, 10.0f - std::sqrt(2.0f), 1.0e-4f), "at its corner, not at an unrotated face");

        Array<JBro::GameObjectHandle> overlaps;
        queries.OverlapBox({ { 1.2f, 1.5f }, { 1.8f, 2.5f } }, overlaps, JBro::AllPhysicsLayers);
        Check(overlaps.IsEmpty(), "a box inside the notch overlaps nothing");
        queries.OverlapBox({ { 0.5f, 1.5f }, { 1.2f, 2.5f } }, overlaps, JBro::AllPhysicsLayers);
        Check(overlaps.Size() == 1 && overlaps[0].GetInstanceId() == cup->GetInstanceId(),
            "one reaching into the left pillar overlaps the cup");

        // 질의는 스텝을 기다리지 않는다. 옮긴 직후에 바로 맞다.
        scene.TransformOf(cup)->position = { 0, 10 };
        Check(false == queries.Raycast({ 1.5f, 5 }, { 0, -1 }, 3, hit, JBro::AllPhysicsLayers), "a query sees the cup's new place at once");
    }

    // **늘어난 질의(physics-plan §4 의 6).** x 축에 벽 A(레이어 1, 상자 안에 원 콜라이더 하나 더), 벽 B(레이어 2),
    // 원 C(레이어 1), 멀리 U 컵. 값은 손으로 푼 것이다.
    void TestTheWiderQueries()
    {
        Scene scene;
        JBro::GameObject* a = scene.Object("a", { 3, 0 });
        scene.Box(a, { 2, 2 });
        Collider2D* inner = scene.canvas.AttachComponent<Collider2D>(a);
        inner->shape = ColliderShape2D::Circle;
        inner->radius = 0.5f;
        JBro::GameObject* b = scene.Object("b", { 6, 0 });
        scene.Box(b, { 2, 2 })->layer = 0x2u;
        JBro::GameObject* c = scene.Object("c", { 9, 0 });
        Collider2D* round = scene.canvas.AttachComponent<Collider2D>(c);
        round->shape = ColliderShape2D::Circle;
        round->radius = 0.5f;
        JBro::GameObject* cup = scene.Object("cup", { 20, 0 });
        Collider2D* polygon = scene.canvas.AttachComponent<Collider2D>(cup);
        polygon->shape = ColliderShape2D::Polygon;
        polygon->points = UOutline();
        const JBro::System::IPhysics2DSystem& queries = scene.physics;
        const std::uint32_t all = JBro::AllPhysicsLayers;

        JBro::Array<JBro::RaycastHit2D> hits;
        queries.RaycastAll({ 0, 0 }, { 1, 0 }, 15, hits, all);
        Check(hits.Size() == 4, "a ray through everything hits every collider on its path, the inner circle too");
        Check(Near(hits[0].distance, 2.0f, 1.0e-4f) && hits[0].other.GetInstanceId() == a->GetInstanceId()
            && Near(hits[1].distance, 2.5f, 1.0e-4f) && hits[1].other.GetInstanceId() == a->GetInstanceId()
            && Near(hits[2].distance, 5.0f, 1.0e-4f) && hits[2].other.GetInstanceId() == b->GetInstanceId()
            && Near(hits[3].distance, 8.5f, 1.0e-4f) && hits[3].other.GetInstanceId() == c->GetInstanceId(),
            "sorted by distance: A's box, A's circle, B, C");
        // 거꾸로 쏘면 콜라이더를 도는 순서와 거리 순서가 어긋난다. 정렬이 없으면 여기서 드러난다.
        queries.RaycastAll({ 15, 0 }, { -1, 0 }, 15, hits, all);
        Check(hits.Size() == 4 && Near(hits[0].distance, 5.5f, 1.0e-4f) && hits[0].other.GetInstanceId() == c->GetInstanceId()
            && Near(hits[1].distance, 8.0f, 1.0e-4f) && Near(hits[2].distance, 11.0f, 1.0e-4f)
            && Near(hits[3].distance, 11.5f, 1.0e-4f),
            "a ray shot back along x is sorted too: C, B, A's box, A's circle");
        queries.RaycastAll({ 0, 0 }, { 1, 0 }, 15, hits, 0x1u);
        Check(hits.Size() == 3 && hits[2].other.GetInstanceId() == c->GetInstanceId(), "masking layer 1 skips B");
        JBro::RaycastHit2D hit;
        Check(queries.Raycast({ 0, 0 }, { 1, 0 }, 15, hit, 0x2u) && hit.other.GetInstanceId() == b->GetInstanceId()
            && Near(hit.distance, 5.0f, 1.0e-4f), "a ray on layer 2 goes through A and stops at B");
        queries.RaycastAll({ 20.5f, 5 }, { 0, -1 }, 10, hits, all);
        Check(hits.Size() == 1 && Near(hits[0].distance, 2.0f, 1.0e-4f),
            "a ray down the left pillar of the U is one hit, though the pillar may be more than one piece");

        Check(queries.OverlapPoint({ 3, 0 }, all).GetInstanceId() == a->GetInstanceId(), "a point inside A is A");
        Check(queries.OverlapPoint({ 4.5f, 0 }, all).GetInstanceId() == JBro::InvalidInstanceId,
            "a point between the walls is nothing");
        Check(queries.OverlapPoint({ 21.5f, 2 }, all).GetInstanceId() == JBro::InvalidInstanceId,
            "a point in the notch of the U is not the U");
        Check(queries.OverlapPoint({ 20.5f, 2 }, all).GetInstanceId() == cup->GetInstanceId(), "one in its pillar is");
        Check(queries.OverlapPoint({ 3, 0 }, 0x2u).GetInstanceId() == JBro::InvalidInstanceId, "and a mask hides A");

        JBro::Array<JBro::GameObjectHandle> found;
        queries.OverlapCircle({ 4.5f, 0 }, 0.6f, found, all);
        Check(found.Size() == 2, "a circle between the walls reaching both finds A and B, A once");
        queries.OverlapCircle({ 4.5f, 0 }, 0.4f, found, all);
        Check(found.IsEmpty(), "a smaller one reaches neither");
        queries.OverlapCircle({ 3, 0 }, 1.0f, found, all);
        Check(found.Size() == 1 && found[0].GetInstanceId() == a->GetInstanceId(),
            "a circle over both of A's colliders finds A once");
        queries.OverlapBox({ { 2.5f, -0.5f }, { 3.5f, 0.5f } }, found, 0x2u);
        Check(found.IsEmpty(), "a box over A on layer 2 finds nothing");

        Check(queries.CircleCast({ 0, 0 }, 0.5f, { 1, 0 }, 15, hit, all) && hit.other.GetInstanceId() == a->GetInstanceId(),
            "a circle swept along x hits A");
        Check(Near(hit.distance, 1.5f, 1.0e-4f) && Near(hit.normal.x, -1.0f, 1.0e-5f)
            && Near(hit.point.x, 2.0f, 1.0e-4f) && Near(hit.point.y, 0.0f, 1.0e-4f),
            "a radius short of A's face, touching it at (2, 0)");
        Check(queries.CircleCast({ 3, 0 }, 0.5f, { 1, 0 }, 15, hit, all) && hit.distance == 0.0f
            && Near(hit.point.x, 3.0f, 0.0f), "a circle that starts inside A reports zero at its own center");
        Check(queries.CircleCast({ 21.5f, 5 }, 0.3f, { 0, -1 }, 10, hit, all)
            && hit.other.GetInstanceId() == cup->GetInstanceId() && Near(hit.distance, 3.7f, 1.0e-4f),
            "a ball dropped into the notch of the U lands on the notch floor");

        Check(queries.BoxCast({ 0, 0 }, { 0.5f, 0.5f }, 0.0f, { 1, 0 }, 15, hit, all)
            && Near(hit.distance, 1.5f, 1.0e-4f) && Near(hit.point.x, 2.0f, 1.0e-4f) && Near(hit.point.y, 0.0f, 1.0e-3f),
            "a box swept along x stops face to face with A, touching at the middle of its face");
        Check(queries.BoxCast({ 0, 0 }, { 0.5f, 0.5f }, 0.78539816f, { 1, 0 }, 15, hit, all)
            && Near(hit.distance, 2.0f - std::sqrt(0.5f), 1.0e-4f) && Near(hit.point.x, 2.0f, 1.0e-4f),
            "a diamond swept along x touches A with its corner");
        Check(false == queries.BoxCast({ 0, 3 }, { 0.5f, 0.5f }, 0.0f, { 1, 0 }, 15, hit, all),
            "a box passing above everything misses");
    }

#if defined(_MSC_VER) && defined(_DEBUG)
    int g_allocations = 0;
    int CountAllocations(int operation, void*, std::size_t, int, long, const unsigned char*, int)
    {
        if (operation == _HOOK_ALLOC || operation == _HOOK_REALLOC)
        {
            ++g_allocations;
        }
        return 1;
    }
#endif

    // **어댑터의 고정 스텝도 힙을 건드리지 않는다.** 동기화·되쓰기·훅 발송(닿아 있는 동안)·크기를 움직이는 콜라이더·질의를 함께 돈다.
    void TestTheFixedStepDoesNotAllocate()
    {
        Scene scene;
        JBro::GameObject* ground = scene.Object("ground", { 0, -0.5f });
        scene.Box(ground, { 40, 1 });
        JBro::GameObject* cup = scene.Object("cup", { 6, 0 });
        Collider2D* polygon = scene.canvas.AttachComponent<Collider2D>(cup);
        polygon->shape = ColliderShape2D::Polygon;
        polygon->points = UOutline();
        JBro::GameObject* box = scene.Object("box", { 0, 0.5f });
        Collider2D* animated = scene.Box(box, { 1, 1 });
        scene.Dynamic(box);
        scene.Probe(box);
        JBro::GameObject* pill = scene.Object("pill", { -3, 0.5f });
        scene.Box(pill, { 2, 1 })->shape = ColliderShape2D::Capsule;
        scene.Dynamic(pill);
        const JBro::System::IPhysics2DSystem& queries = scene.physics;
        JBro::RaycastHit2D hit;
        JBro::Array<JBro::RaycastHit2D> hits;
        JBro::Array<JBro::GameObjectHandle> found;
        hits.Reserve(16);
        found.Reserve(16);
        const auto step = [&](int i)
        {
            animated->size = { 1.0f + 0.04f * static_cast<float>(i % 5), 1.0f };
            scene.physics.FixedUpdate(scene.canvas, Frame);
            queries.Raycast({ -10, 0.25f }, { 1, 0 }, 30, hit, JBro::AllPhysicsLayers);
            queries.RaycastAll({ -10, 0.25f }, { 1, 0 }, 30, hits, JBro::AllPhysicsLayers);
            queries.OverlapCircle({ 6, 1 }, 1.5f, found, JBro::AllPhysicsLayers);
        };
        for (int i = 0; i < 120; ++i)
        {
            step(i);
        }
#if defined(_MSC_VER) && defined(_DEBUG)
        g_allocations = 0;
        const _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountAllocations);
        for (int i = 0; i < 120; ++i)
        {
            step(i);
        }
        _CrtSetAllocHook(previous);
        std::cout << "  CRT allocations during 120 fixed steps with an animated collider and queries: " << g_allocations << '\n';
        Check(g_allocations == 0, "the physics fixed step, an animated collider and queries do not touch the heap");
#endif
    }

    // **크기를 움직이는 콜라이더는 닿아 있는 동안 훅을 되풀이하지 않는다.** 전에는 모양이 바뀔 때마다 도형을 지우고 만들어
    // 스텝마다 끝·시작이 불렸다. 트리거로 바꾸는 것은 훅의 종류가 바뀌므로 끝나고 새로 시작한다.
    void TestAnAnimatedColliderKeepsItsContact()
    {
        Scene scene;
        JBro::GameObject* ground = scene.Object("ground", { 0, -0.5f });
        scene.Box(ground, { 40, 1 });
        JBro::GameObject* box = scene.Object("box", { 0, 0.5f });
        Collider2D* collider = scene.Box(box, { 1, 1 });
        scene.Dynamic(box);
        ContactProbe* probe = scene.Probe(box);
        scene.Run(0.5f);
        Check(probe->collisionEnter == 1, "the box lands once");

        for (int i = 0; i < 60; ++i)
        {
            collider->size = { 1.0f + 0.04f * static_cast<float>(i % 5), 1.0f };
            scene.physics.FixedUpdate(scene.canvas, Frame);
        }
        Check(probe->collisionEnter == 1 && probe->collisionExit == 0, "resizing it every step keeps the one contact");
        Check(Near(scene.TransformOf(box)->position.y, 0.5f, 2.0f * Slop), "and it stays on the ground");

        collider->isTrigger = true;
        scene.Run(0.2f);
        Check(probe->collisionExit == 1 && probe->triggerEnter == 1, "turning it into a trigger ends the collision");

        // 모양이 틀린 외곽선이 되면 도형이 없어지고 닿아 있던 것은 끝난다. 레이어를 바꿔 걸러도 끝난다(제자리에서 바꾼 표면).
        JBro::GameObject* second = scene.Object("second", { 5, 0.5f });
        Collider2D* outline = scene.Box(second, { 1, 1 });
        outline->shape = ColliderShape2D::Polygon;
        outline->points = { { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } };
        scene.Dynamic(second);
        ContactProbe* secondProbe = scene.Probe(second);
        JBro::GameObject* third = scene.Object("third", { -5, 0.5f });
        Collider2D* layered = scene.Box(third, { 1, 1 });
        scene.Dynamic(third);
        ContactProbe* thirdProbe = scene.Probe(third);
        scene.Run(0.5f);
        Check(secondProbe->collisionEnter == 1 && thirdProbe->collisionEnter == 1, "two more boxes land");
        const std::size_t shapes = scene.physics.GetShapeCount();
        outline->points = { { 0, 0 }, { 1, 1 }, { 1, 0 }, { 0, 1 } };
        scene.physics.FixedUpdate(scene.canvas, Frame);
        Check(scene.physics.GetShapeCount() == shapes - 1 && secondProbe->collisionExit == 1,
            "a collider bent into a bow tie loses its shape and its contact");
        scene.canvas.FindComponentRaw<Collider2D>(ground)->mask = 0x1u;
        layered->layer = 0x2u;
        scene.Run(0.1f);
        Check(thirdProbe->collisionExit == 1, "and one moved to a layer the ground does not take lets go");
    }

    // **찌그러지거나 뒤집힌 부모 아래의 몸은 가만히 있으면 제 로컬 회전을 지킨다(physics-plan §4 의 4 (3)).** 물리의 각도는
    // `Transform2D` 의 `worldRotation` 과 같이 회전의 합이다 - 캔버스 뷰가 콜라이더를 그리는 규칙이고, 되쓰기가 그 역이다.
    // 전에는 월드 행렬 첫 행의 각도를 써서, 회전 + 비균등 크기인 부모 아래에서는 첫 스텝에 로컬 회전이 저절로 바뀌고
    // 뒤집힌 부모 아래에서는 부호가 뒤집혔다.
    void TestABodyUnderASkewedOrMirroredParentKeepsItsRotation()
    {
        Scene scene;
        scene.physics.SetGravity({ 0, 0 });
        JBro::GameObject* skewed = scene.Object("skewed", { 0, 0 });
        scene.TransformOf(skewed)->SetRotationRadian(JBro::Radian(0.5f));
        scene.TransformOf(skewed)->scale = { 2, 1 };
        JBro::GameObject* mirrored = scene.Object("mirrored", { 10, 0 });
        scene.TransformOf(mirrored)->scale = { -1, 1 };

        JBro::GameObject* children[2] = {};
        JBro::GameObject* parents[2] = { skewed, mirrored };
        for (int i = 0; i < 2; ++i)
        {
            children[i] = scene.canvas.CreateObject(i == 0 ? "skewedChild" : "mirroredChild");
            children[i]->SetParent(parents[i]);
            Transform2D* local = scene.canvas.AttachComponent<Transform2D>(children[i]);
            local->position = { 1, 0.5f };
            local->SetRotationRadian(JBro::Radian(0.3f));
            scene.Box(children[i], { 1, 0.5f });
            scene.Dynamic(children[i]);
        }
        scene.Run(0.5f);

        for (int i = 0; i < 2; ++i)
        {
            const Transform2D* local = scene.TransformOf(children[i]);
            Check(Near(local->GetRotationRadian(), 0.3f, 1.0e-4f), i == 0
                ? "a body at rest under a rotated, stretched parent keeps its local rotation"
                : "and one under a mirrored parent keeps its sign");
            Check(Near(local->position.x, 1.0f, 1.0e-4f) && Near(local->position.y, 0.5f, 1.0e-4f), "and its local place");
        }
    }

    // **워커 수는 다음 고정 스텝에 커널로 가고, 결과를 바꾸지 않는다(D-223).** 같은 상자 더미를 워커 0 과 3 의 두 캔버스에서 돌린다.
    void TestTheWorkerCountReachesTheKernel()
    {
        Scene serial;
        Scene parallel;
        JBro::GameObject* serialBoxes[60] = {};
        JBro::GameObject* parallelBoxes[60] = {};
        Scene* scenes[2] = { &serial, &parallel };
        JBro::GameObject** boxes[2] = { serialBoxes, parallelBoxes };
        for (int s = 0; s < 2; ++s)
        {
            Scene& scene = *scenes[s];
            JBro::GameObject* ground = scene.Object("ground", { 0, -0.5f });
            scene.Box(ground, { 80, 1 });
            for (int i = 0; i < 60; ++i)
            {
                const float x = -30.0f + static_cast<float>(i % 30) * 2.0f;
                const float y = 0.5f + static_cast<float>(i / 30) * 1.0f;
                boxes[s][i] = scene.Object("box", { x, y });
                scene.Box(boxes[s][i], { 1, 1 });
                scene.Dynamic(boxes[s][i]);
            }
        }
        parallel.physics.SetWorkerCount(3);
        Check(parallel.physics.GetWorkerCount() == 0, "the count waits for the next fixed step");
        serial.Run(1.0f);
        parallel.Run(1.0f);
#if !defined(__EMSCRIPTEN__)
        Check(parallel.physics.GetWorkerCount() == 3, "and then the kernel runs three workers");
#endif
        for (int i = 0; i < 60; ++i)
        {
            const JBro::Vec2 a = serial.TransformOf(serialBoxes[i])->position;
            const JBro::Vec2 b = parallel.TransformOf(parallelBoxes[i])->position;
            Check(a.x == b.x && a.y == b.y, "every box lands where the single-thread canvas put it");
        }
    }

    // **물리 일감 세기(D-223).** 켜진 콜라이더마다 1, 포인트가 넷을 넘는 폴리곤은 포인트 수 - 2, 꺼진 것은 0.
    void TestCountingPhysicsWork()
    {
        Scene scene;
        JBro::GameObject* box = scene.Object("box", { 0, 0 });
        scene.Box(box, { 1, 1 });
        JBro::GameObject* round = scene.Object("round", { 3, 0 });
        scene.Box(round, { 1, 1 })->shape = ColliderShape2D::Circle;
        JBro::GameObject* cup = scene.Object("cup", { 6, 0 });
        Collider2D* polygon = scene.canvas.AttachComponent<Collider2D>(cup);
        polygon->shape = ColliderShape2D::Polygon;
        polygon->points = UOutline();
        JBro::GameObject* off = scene.Object("off", { 9, 0 });
        scene.Box(off, { 1, 1 })->SetEnabled(false);
        const std::uint32_t uPieces = static_cast<std::uint32_t>(UOutline().Size() - 2);
        Check(JBro::CountPhysicsWork(scene.canvas) == 2 + uPieces,
            "a box and a circle count one each, the U its points less two, the disabled one nothing");
    }

    // **스크립트의 힘·충격량과 축 고정·각 감쇠(D-227).** 컴포넌트에 쌓은 것이 다음 고정 스텝에 한 번 먹고, 성질을 바꿔도 접촉이 이어진다.
    void TestRigidbodyForcesLocksAndDamping()
    {
        Scene scene;
        scene.physics.SetGravity({ 0, 0 });
        // 원점에서 떨어뜨려 둔다 - 위치를 준 충격량의 토크가 질량 중심으로 풀려야 맞는 자리다.
        JBro::GameObject* box = scene.Object("box", { 3, 2 });
        scene.Box(box, { 1, 1 });
        Rigidbody2D* body = scene.Dynamic(box);
        body->mass = 2.0f;
        scene.Run(Frame);
        body->AddForce({ 4, 0 });
        scene.physics.FixedUpdate(scene.canvas, Frame);
        Check(Near(body->linearVelocity.x, 2.0f * Frame, 1.0e-6f), "a force added by a script acts for the next fixed step");
        scene.physics.FixedUpdate(scene.canvas, Frame);
        Check(Near(body->linearVelocity.x, 2.0f * Frame, 1.0e-6f), "only that one");
        body->linearVelocity = { 0, 0 };
        const JBro::Vec2 at = scene.TransformOf(box)->position;
        body->AddImpulseAtPosition({ 1, 0 }, { at.x, at.y + 0.5f });
        scene.physics.FixedUpdate(scene.canvas, Frame);
        Check(Near(body->linearVelocity.x, 0.5f, 1.0e-5f) && Near(body->angularVelocity, -1.5f, 1.0e-4f),
            "an impulse above the center pushes and turns, about the center of mass");
        body->angularVelocity = 0.0f;
        body->AddAngularImpulse(1.0f);
        body->AddTorque(0.0f);
        scene.physics.FixedUpdate(scene.canvas, Frame);
        Check(Near(body->angularVelocity, 3.0f, 1.0e-4f), "an angular impulse of 1 turns an inertia of 1/3 at 3 rad/s");

        body->angularDamping = 5.0f;
        body->freezePositionX = true;
        body->AddImpulse({ 3, 0 });
        scene.Run(1.0f);
        Check(std::fabs(body->angularVelocity) < 0.1f, "angular damping from the component slows the spin");
        Check(Near(body->linearVelocity.x, 0.0f, 0.0f), "and a body locked in x takes no push along it");
    }

    // **질량을 바꿔도 서 있는 상자의 접촉은 이어진다(D-227).** 전에는 성질이 바뀌면 바디를 다시 만들어 끝·시작 훅이 불렸다.
    void TestChangingTheMassKeepsTheContact()
    {
        Scene scene;
        JBro::GameObject* ground = scene.Object("ground", { 0, -0.5f });
        scene.Box(ground, { 40, 1 });
        JBro::GameObject* box = scene.Object("box", { 0, 0.5f });
        scene.Box(box, { 1, 1 });
        Rigidbody2D* body = scene.Dynamic(box);
        ContactProbe* probe = scene.Probe(box);
        scene.Run(0.5f);
        Check(probe->collisionEnter == 1, "the box lands");
        body->mass = 10.0f;
        body->linearDamping = 1.0f;
        scene.Run(0.2f);
        Check(probe->collisionEnter == 1 && probe->collisionExit == 0, "changing its mass and damping keeps the one contact");
    }

    // **체인 콜라이더와 수면 API(D-229).** 체인 바닥에 떨어진 상자가 서서 잠들고, 레이가 체인에 맞으며, WakeUp 으로 깬다.
    void TestChainCollidersAndSleep()
    {
        Scene scene;
        JBro::GameObject* ground = scene.Object("ground", { 0, 0 });
        Collider2D* chain = scene.canvas.AttachComponent<Collider2D>(ground);
        chain->shape = ColliderShape2D::Chain;
        chain->points = { { -10, 0 }, { -2, 0 }, { 2, 0 }, { 10, 0 } };
        JBro::GameObject* box = scene.Object("box", { 0, 3 });
        scene.Box(box, { 1, 1 });
        Rigidbody2D* body = scene.Dynamic(box);
        scene.Run(3.0f);
        Check(Near(scene.TransformOf(box)->position.y, 0.5f, 2.0f * Slop), "a box lands on a chain floor");
        Check(body->IsSleeping(), "and falls asleep on it");

        JBro::RaycastHit2D hit;
        const JBro::System::IPhysics2DSystem& queries = scene.physics;
        Check(queries.Raycast({ 6, 5 }, { 0, -1 }, 10, hit, JBro::AllPhysicsLayers)
            && hit.other.GetInstanceId() == ground->GetInstanceId() && Near(hit.distance, 5.0f, 1.0e-4f)
            && Near(hit.normal.y, 1.0f, 1.0e-5f), "a ray down hits the chain, its normal facing the ray");
        Check(queries.Raycast({ 6, -5 }, { 0, 1 }, 10, hit, JBro::AllPhysicsLayers) && Near(hit.normal.y, -1.0f, 1.0e-5f),
            "and from below too, both faces answer");

        body->WakeUp();
        scene.physics.FixedUpdate(scene.canvas, Frame);
        Check(false == body->IsSleeping(), "WakeUp wakes it on the next fixed step");
        body->canSleep = false;
        scene.Run(2.0f);
        Check(false == body->IsSleeping(), "and one that may not sleep stays awake");

        chain->loop = true;
        scene.Run(0.1f);
        Check(scene.physics.GetShapeCount() == 2, "looping the chain reshapes it in place");
        Check(JBro::CountPhysicsWork(scene.canvas) == 1 + 4, "a looped chain of four points is four segments of work");

        // 삼각형 체인: 닫으면 (10,10)-(-10,0) 변(기울기 0.5)이 생긴다. y = 5 로 쏜 레이가 열리면 세로 변(x = 10), 닫히면 그 닫는 변(x = 0)에 맞는다.
        chain->points = { { -10, 0 }, { 10, 0 }, { 10, 10 } };
        chain->loop = false;
        Check(queries.Raycast({ -9, 5 }, { 1, 0 }, 30, hit, JBro::AllPhysicsLayers) && Near(hit.distance, 19.0f, 1.0e-4f),
            "an open triangle chain has no closing edge for the ray");
        chain->loop = true;
        Check(queries.Raycast({ -9, 5 }, { 1, 0 }, 30, hit, JBro::AllPhysicsLayers) && Near(hit.distance, 9.0f, 1.0e-4f),
            "a looped one does");
        JBro::GameObject* ball = scene.Object("ball", { -6.0f, 6.0f });
        Collider2D* round = scene.canvas.AttachComponent<Collider2D>(ball);
        round->shape = ColliderShape2D::Circle;
        round->radius = 0.3f;
        scene.Dynamic(ball);
        scene.Run(1.0f);
        Check(scene.TransformOf(ball)->position.y > 1.5f, "and the looped chain's closing edge holds a ball dropped on it");
    }

    // **캡슐 콜라이더는 `size` 상자에 꼭 맞는 알약이다(physics-plan §4 의 7).** 누운 것은 반지름만큼 떠서 서고, 한 축으로 늘인
    // 것도 캡슐로 남고, 질의는 둥근 끝 옆의 빈 곳을 캡슐로 보지 않는다.
    void TestCapsuleColliders()
    {
        Scene scene;
        JBro::GameObject* ground = scene.Object("ground", { 0, -0.5f });
        scene.Box(ground, { 40, 1 });
        JBro::GameObject* lying = scene.Object("lying", { -5, 2 });
        scene.Box(lying, { 2, 1 })->shape = ColliderShape2D::Capsule;
        scene.Dynamic(lying);
        JBro::GameObject* stretched = scene.Object("stretched", { 5, 2 });
        scene.TransformOf(stretched)->scale = { 3, 1 };
        scene.Box(stretched, { 1, 1 })->shape = ColliderShape2D::Capsule;
        scene.Dynamic(stretched);
        JBro::GameObject* post = scene.Object("post", { 20, 0 });
        scene.Box(post, { 2, 1 })->shape = ColliderShape2D::Capsule;
        JBro::GameObject* upright = scene.Object("upright", { 30, 0 });
        scene.TransformOf(upright)->SetRotationRadian(JBro::Radian(1.5707963f));
        scene.Box(upright, { 2, 1 })->shape = ColliderShape2D::Capsule;
        scene.Run(3.0f);

        Check(scene.physics.GetShapeCount() == 5, "every capsule collider is a shape");
        Check(Near(scene.TransformOf(lying)->position.y, 0.5f, 2.0f * Slop)
            && Near(scene.TransformOf(lying)->GetRotationRadian(), 0.0f, 1.0e-3f), "a lying capsule rests a radius up, flat");
        Check(Near(scene.TransformOf(stretched)->position.y, 0.5f, 2.0f * Slop)
            && Near(scene.TransformOf(stretched)->GetRotationRadian(), 0.0f, 1.0e-3f),
            "one stretched from a circle along x lies the same way");

        JBro::RaycastHit2D hit;
        const JBro::System::IPhysics2DSystem& queries = scene.physics;
        const float stretchedX = scene.TransformOf(stretched)->position.x;
        Check(queries.Raycast({ stretchedX + 3.0f, 0.5f }, { -1, 0 }, 10, hit, JBro::AllPhysicsLayers)
            && hit.other.GetInstanceId() == stretched->GetInstanceId() && Near(hit.distance, 1.5f, 1.0e-3f),
            "and it is 3 long, not the unit circle it was stretched from");
        Check(queries.Raycast({ 25, 0.4f }, { -1, 0 }, 10, hit, JBro::AllPhysicsLayers)
            && hit.other.GetInstanceId() == post->GetInstanceId() && Near(hit.distance, 4.2f, 1.0e-4f),
            "a ray along x at 0.4 hits the post's round end (core 19.5..20.5, radius 0.5) at x = 20.8");
        Check(queries.Raycast({ 30, 5 }, { 0, -1 }, 10, hit, JBro::AllPhysicsLayers)
            && hit.other.GetInstanceId() == upright->GetInstanceId() && Near(hit.distance, 4.0f, 1.0e-4f),
            "a turned capsule stands, its top a length and a radius up");
        Check(queries.OverlapPoint({ 20.8f, 0 }, JBro::AllPhysicsLayers).GetInstanceId() == post->GetInstanceId(),
            "a point in the round end is the post");
        Check(queries.OverlapPoint({ 20.9f, 0.4f }, JBro::AllPhysicsLayers).GetInstanceId() == JBro::InvalidInstanceId,
            "one in the corner of its size box is not");
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

    // **포인트가 없는 폴리곤은 `size` 상자로 부딪힌다.** 캔버스 뷰가 그 상자를 그리고 편집의 출발점으로 주므로,
    // 모양을 Polygon 으로 막 바꾼 콜라이더가 보이는 것과 다르게(아예 없는 것처럼) 굴면 안 된다.
    void TestAnEmptyPolygonCollidesAsItsSizeBox()
    {
        Scene scene;
        JBro::GameObject* ground = scene.Object("ground", { 0, -0.5f });
        scene.Box(ground, { 40, 1 });
        JBro::GameObject* box = scene.Object("box", { 0, 3 });
        Collider2D* shape = scene.canvas.AttachComponent<Collider2D>(box);
        shape->shape = ColliderShape2D::Polygon;
        shape->size = { 2, 2 };
        scene.Dynamic(box);
        scene.Run(2.0f);
        Check(scene.physics.GetShapeCount() == 2, "the empty polygon still makes a shape");
        Check(Near(scene.TransformOf(box)->position.y, 1.0f, 2.0f * Slop), "and rests one unit up, on its 2x2 box");
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
        Check(Near(JBro::Radian(scene.TransformOf(l)->GetRotationRadian()), 3.14159265f, 1.0e-3f), "half a turn in a second");
        Check(Near(position.x, 1.5f, 1.0e-3f) && Near(position.y, 2.5f, 1.0e-3f),
            "and the origin lands across the center of mass");
        Check(Near(body->angularVelocity, 3.14159265f, 1.0e-4f), "keeping its spin");
    }

    // **이어지는 접촉은 고정 스텝마다 Stay 로 온다(D-233).** 시작한 스텝은 Enter 만이고, 몸이 잠들면 멈춘다. 잠들지 않는 공을
    // 트리거 안에 띄워 두면 스텝마다 양쪽이 받고, 트리거의 Stay 에는 법선이 없다.
    void TestStayHooksComeEveryStepWhileTouching()
    {
        {
            Scene scene;
            JBro::GameObject* ground = scene.Object("ground", { 0, -0.5f });
            scene.Box(ground, { 40, 1 });
            JBro::GameObject* box = scene.Object("box", { 0, 0.6f });
            scene.Box(box, { 1, 1 });
            scene.Dynamic(box);
            ContactProbe* boxProbe = scene.Probe(box);
            scene.Run(0.2f);
            Check(boxProbe->collisionEnter == 1, "the box lands");
            const int staysAfterLanding = boxProbe->collisionStay;
            scene.Run(0.2f);
            Check(boxProbe->collisionStay >= staysAfterLanding + 10, "while it settles it hears a stay every fixed step");
            Check(Near(boxProbe->lastStay.normal.y, -1.0f, 1.0e-3f), "each stay carries the box's own normal");
            scene.Run(2.0f);
            const int staysAsleep = boxProbe->collisionStay;
            scene.Run(0.5f);
            Check(boxProbe->collisionStay == staysAsleep, "once it sleeps the stays stop");
            Check(boxProbe->collisionExit == 0, "and it never left");
        }
        {
            Scene scene;
            JBro::GameObject* zone = scene.Object("zone", { 0, 0 });
            Collider2D* sensor = scene.Box(zone, { 4, 4 });
            sensor->isTrigger = true;
            ContactProbe* zoneProbe = scene.Probe(zone);
            JBro::GameObject* ball = scene.Object("ball", { 0, 0 });
            Collider2D* round = scene.canvas.AttachComponent<Collider2D>(ball);
            round->shape = ColliderShape2D::Circle;
            round->radius = 0.25f;
            Rigidbody2D* body = scene.Dynamic(ball);
            body->gravityScale = 0.0f;
            body->canSleep = false;
            ContactProbe* ballProbe = scene.Probe(ball);
            for (int i = 0; i < 10; ++i)
            {
                scene.physics.FixedUpdate(scene.canvas, Frame);
            }
            Check(zoneProbe->triggerEnter == 1 && zoneProbe->triggerStay == 9, "the zone hears one enter, then a stay each step");
            Check(ballProbe->triggerEnter == 1 && ballProbe->triggerStay == 9, "and so does the ball");
            Check(ballProbe->lastStay.other.GetInstanceId() == zone->GetInstanceId(), "a stay names the other object");
            Check(zoneProbe->lastStay.normal.x == 0.0f && zoneProbe->lastStay.normal.y == 0.0f, "a trigger stay carries no normal");
            Check(zoneProbe->collisionStay == 0, "and no collision stay is called");
        }
    }

    // **한 방향 발판 콜라이더(D-233).** 밑에서 뛰어올라 뚫고 지나가는 동안에는 훅이 없고, 위에 얹힐 때 시작을 한 번 받는다.
    void TestAOneWayColliderLetsThingsUpThrough()
    {
        Scene scene;
        JBro::GameObject* platform = scene.Object("platform", { 0, 0 });
        Collider2D* ledge = scene.Box(platform, { 6, 0.5f });
        ledge->oneWay = true;
        ContactProbe* platformProbe = scene.Probe(platform);
        JBro::GameObject* box = scene.Object("box", { 0, -1.5f });
        scene.Box(box, { 1, 1 });
        Rigidbody2D* body = scene.Dynamic(box);
        body->linearVelocity = { 0, 8 };
        scene.Run(0.12f);
        Check(scene.TransformOf(box)->position.y > -1.0f && platformProbe->collisionEnter == 0,
            "jumping up through the ledge is heard by no one");
        scene.Run(1.5f);
        Check(Near(scene.TransformOf(box)->position.y, 0.75f, 0.02f), "the box ends on top of the ledge");
        Check(platformProbe->collisionEnter == 1, "and the ledge hears it land once");
        ledge->oneWay = false;
        scene.Run(0.1f);
        Check(Near(scene.TransformOf(box)->position.y, 0.75f, 0.02f), "turning oneWay off leaves it standing there");

        // 막는 발판에 밑에서 쳐올리면 튕겨 떨어지고, 그 자리에서 oneWay 를 켜면 같은 도형이 흘려보낸다.
        JBro::GameObject* jumper = scene.Object("jumper", { 5, -1.5f });
        scene.Box(jumper, { 0.5f, 0.5f });
        Rigidbody2D* jumperBody = scene.Dynamic(jumper);
        JBro::GameObject* ceiling = scene.Object("ceiling", { 5, 0 });
        Collider2D* roof = scene.Box(ceiling, { 2, 0.5f });
        jumperBody->linearVelocity = { 0, 8 };
        scene.Run(0.3f);
        Check(scene.TransformOf(jumper)->position.y < -0.4f, "a solid ceiling stops a jump from below");
        roof->oneWay = true;
        scene.Run(0.5f);
        jumperBody->linearVelocity = { 0, 8 };
        scene.Run(1.0f);
        Check(scene.TransformOf(jumper)->position.y > 0.3f, "switching oneWay on lets the next jump through onto it");
    }

    // **레이어 충돌 표가 시스템을 거쳐 커널에 간다(D-233).** 떼어 둔 두 레이어의 상자는 서로 지나간다.
    void TestTheLayerTableReachesTheKernel()
    {
        Scene scene;
        std::uint32_t rows[JBro::PhysicsLayerCount] = {};
        rows[1] = 1u << 2;
        rows[2] = 1u << 1;
        scene.physics.SetIgnoredLayers(rows);
        JBro::GameObject* ground = scene.Object("ground", { 0, -0.5f });
        scene.Box(ground, { 20, 1 });
        JBro::GameObject* lower = scene.Object("lower", { 0, 0.5f });
        scene.Box(lower, { 1, 1 })->layer = 1u << 1;
        scene.Dynamic(lower);
        JBro::GameObject* upper = scene.Object("upper", { 0, 3 });
        scene.Box(upper, { 1, 1 })->layer = 1u << 2;
        scene.Dynamic(upper);
        scene.Run(1.5f);
        Check(Near(scene.TransformOf(upper)->position.y, 0.5f, 0.03f), "a box on a separated layer falls through the other onto the ground");
    }

    // **조인트 컴포넌트(D-233).** 경첩은 처음 이어질 때 핀 자리를 월드로 적고 그 둘레로 흔들린다. 한계와 모터는 "이 오브젝트가
    // 상대에 대해" 의 반시계 양수 각도다. 거리 조인트는 처음 거리를 적어 그만큼 매달고, 거리를 바꾸면 제자리에서 바뀌며,
    // 상대 오브젝트가 사라지면 조인트도 없어진다.
    void TestJointComponents()
    {
        using JBro::Component::DistanceJoint2D;
        using JBro::Component::HingeJoint2D;
        const float degree = 3.14159265f / 180.0f;
        {
            // 막대는 크기 (2, 1) 의 상자 하나이고 왼쪽 끝(로컬 -0.5, 크기를 곱해 -1)을 (2, 1) 의 핀에 건다. 20° 기울어 시작하므로
            // 한계 [-30°, 10°] 는 그 자리를 0 으로 잰다 - 떨어지면 20 - 30 = -10° 에서 선다. 한계가 비대칭이라 부호가 뒤집히면
            // 다른 각에서 선다.
            Scene scene;
            const float start = 20.0f * degree;
            JBro::GameObject* rod = scene.Object("rod", { 2.0f + std::cos(start), 1.0f + std::sin(start) });
            scene.TransformOf(rod)->SetRotationRadian(JBro::Radian(start));
            scene.TransformOf(rod)->scale = { 2, 1 };
            scene.Box(rod, { 1.0f, 0.2f });
            scene.Dynamic(rod);
            HingeJoint2D* hinge = scene.canvas.AttachComponent<HingeJoint2D>(rod);
            hinge->anchor = { -0.5f, 0 };
            hinge->useLimits = true;
            hinge->lowerAngle = -30.0f;
            hinge->upperAngle = 10.0f;
            scene.Run(1.5f);
            Check(scene.physics.GetJointCount() == 1, "a hinge on a body becomes one kernel joint");
            Check(Near(hinge->connectedAnchor.x, 2.0f, 1.0e-3f) && Near(hinge->connectedAnchor.y, 1.0f, 1.0e-3f),
                "with no partner the pin is written as the scaled anchor's world point");
            Check(Near(JBro::Radian(scene.TransformOf(rod)->GetRotationRadian()), -10.0f * degree, 2.0f * degree),
                "the falling rod turns clockwise and the lower limit, measured from where it started, holds it");
            const Vec2 end = scene.TransformOf(rod)->position;
            const JBro::Radian angle = scene.TransformOf(rod)->GetRotationRadian();
            Check(Near(end.x - std::cos(angle.Get()), 2.0f, 0.02f) && Near(end.y - std::sin(angle.Get()), 1.0f, 0.02f),
                "and its end stays on the pin");
            // 모터로 반시계로 들어 올리면 위 한계(시작에서 +10°)에서 선다.
            hinge->useMotor = true;
            hinge->motorSpeed = 180.0f;
            hinge->maxMotorTorque = 1000.0f;
            scene.Run(1.5f);
            Check(Near(JBro::Radian(scene.TransformOf(rod)->GetRotationRadian()), 30.0f * degree, 2.0f * degree),
                "a motor lifting it counterclockwise stops at the upper limit");
        }
        {
            Scene scene;
            scene.physics.SetGravity({ 0, 0 });
            JBro::GameObject* wheel = scene.Object("wheel", { 0, 0 });
            Collider2D* round = scene.canvas.AttachComponent<Collider2D>(wheel);
            round->shape = ColliderShape2D::Circle;
            Rigidbody2D* body = scene.Dynamic(wheel);
            HingeJoint2D* hinge = scene.canvas.AttachComponent<HingeJoint2D>(wheel);
            hinge->useMotor = true;
            hinge->motorSpeed = 90.0f;
            scene.Run(0.5f);
            Check(Near(body->angularVelocity, 90.0f * degree, 0.01f), "a motor turns the object counterclockwise at its speed");
        }
        {
            Scene scene;
            JBro::GameObject* hook = scene.Object("hook", { 0, 5 });
            scene.Box(hook, { 0.2f, 0.2f });
            JBro::GameObject* weight = scene.Object("weight", { 0, 3 });
            scene.Box(weight, { 0.5f, 0.5f });
            scene.Dynamic(weight);
            DistanceJoint2D* joint = scene.canvas.AttachComponent<DistanceJoint2D>(weight);
            joint->connectedObject = hook->GetScriptHandle();
            scene.Run(1.0f);
            Check(Near(joint->distance, 2.0f, 1.0e-4f), "the first distance is written from the two anchors");
            Check(Near(scene.TransformOf(weight)->position.y, 3.0f, 0.02f), "and the weight hangs there");
            joint->distance = 1.0f;
            scene.Run(1.5f);
            Check(scene.physics.GetJointCount() == 1 && Near(scene.TransformOf(weight)->position.y, 4.0f, 0.03f),
                "a shorter distance pulls it up with the same joint");
            joint->maxDistanceOnly = true;
            joint->distance = 3.0f;
            scene.Run(1.5f);
            Check(Near(scene.TransformOf(weight)->position.y, 2.0f, 0.03f), "as a rope it falls to its length");
            joint->SetEnabled(false);
            scene.Run(0.2f);
            Check(scene.physics.GetJointCount() == 0 && scene.TransformOf(weight)->position.y < 1.9f,
                "switching the joint off removes it and the weight falls");
            joint->SetEnabled(true);
            JBro::GameObject* stand = scene.Object("stand", { 0, -3 });
            scene.Box(stand, { 4, 1 });
            scene.Run(2.0f);
            Check(scene.physics.GetJointCount() == 1, "switching it on joins them again");
            Check(scene.canvas.DestroyObject(hook), "the hook is destroyed");
            scene.canvas.FlushPendingDestroy();
            scene.Run(0.5f);
            Check(scene.physics.GetJointCount() == 0 && scene.TransformOf(weight)->position.y < 1.5f,
                "without its partner the joint is gone and the weight falls");
        }
    }

    // **오브젝트 참조는 캔버스 파일에 파일 안 번호로 적힌다(D-233).** 뒤에 오는 오브젝트를 가리켜도 읽힌 뒤 그 오브젝트를 잡고,
    // 빈 참조는 빈 채로 온다. 파일 밖(되돌리기 글자)에서는 이번 실행의 번호다.
    void TestAnObjectReferenceSurvivesTheCanvasFile()
    {
        using JBro::Component::DistanceJoint2D;
        Check(JBro::Component::RegisterBuiltinComponentTypes2D() && JBro::Component::RegisterBuiltinComponentProperties2D(),
            "the 2D components register");
        JBro::Canvas canvas{ JBro::CreateDefaultAllocator() };
        JBro::GameObject* first = canvas.CreateObject("first");
        JBro::GameObject* second = canvas.CreateObject("second");
        DistanceJoint2D* forward = canvas.AttachComponent<DistanceJoint2D>(first);
        forward->connectedObject = second->GetScriptHandle();
        DistanceJoint2D* empty = canvas.AttachComponent<DistanceJoint2D>(second);
        (void)empty;
        JBro::String text;
        JBro::CanvasFileError error;
        Check(JBro::WriteCanvasText(canvas, text, error), "the canvas is written");
        Check(text.find("connectedObject: 1") != JBro::String::npos, "the reference is written as the file index of its object");
        Check(text.find("connectedObject: \"\"") != JBro::String::npos || text.find("connectedObject: \n") != JBro::String::npos
                || text.find("connectedObject:\n") != JBro::String::npos,
            "and an empty one as nothing");

        JBro::Canvas read{ JBro::CreateDefaultAllocator() };
        Check(JBro::ReadCanvasText(read, text.c_str(), text.size(), error), "the canvas reads back");
        JBro::GameObject* readFirst = nullptr;
        JBro::GameObject* readSecond = nullptr;
        read.ForEachObject([&](JBro::GameObject& object) {
            if (std::strcmp(object.GetTag(), "first") == 0)
            {
                readFirst = &object;
            }
            if (std::strcmp(object.GetTag(), "second") == 0)
            {
                readSecond = &object;
            }
        });
        Check(readFirst != nullptr && readSecond != nullptr, "both objects come back");
        DistanceJoint2D* readForward = read.FindComponentRaw<DistanceJoint2D>(readFirst);
        DistanceJoint2D* readEmpty = read.FindComponentRaw<DistanceJoint2D>(readSecond);
        Check(readForward != nullptr && readForward->connectedObject.GetInstanceId() == readSecond->GetInstanceId(),
            "a reference to a later object finds the new copy of it");
        Check(readForward->connectedObject.GetInstanceId() != second->GetInstanceId(), "not the object it was written from");
        Check(readEmpty != nullptr && false == readEmpty->connectedObject.IsValid(), "an empty reference stays empty");

        const JBro::TypeDescriptor& type = JBro::TypeDescriptorOf<JBro::GameObjectHandle>::Get();
        char buffer[64];
        std::size_t required = 0;
        Check(type.codec->ToText(&forward->connectedObject, buffer, sizeof(buffer), required) && buffer[0] == '@',
            "outside a canvas file the text is this run's object number");
        JBro::GameObjectHandle parsed;
        Check(type.codec->FromText(&parsed, buffer, std::strlen(buffer)) && parsed.GetInstanceId() == second->GetInstanceId(),
            "and it reads back to the same object");
        Check(false == type.codec->FromText(&parsed, "3", 1), "a bare file index means nothing outside a canvas file");
    }

    // **질의는 경계로 먼저 거른다(D-234).** 3 m 간격 격자의 콜라이더 100 개 가운데 짧은 반직선과 작은 원은 곁의 몇 개만 들여다보고,
    // 멀리 떨어진 질의는 하나도 보지 않는다. 결과는 거르기 전과 같다.
    void TestQueriesSkipFarColliders()
    {
        Scene scene;
        for (int x = 0; x < 10; ++x)
        {
            for (int y = 0; y < 10; ++y)
            {
                JBro::GameObject* object = scene.Object("cell", { 3.0f * static_cast<float>(x), 3.0f * static_cast<float>(y) });
                if ((x + y) % 2 == 0)
                {
                    scene.Box(object, { 1, 1 });
                }
                else
                {
                    Collider2D* round = scene.canvas.AttachComponent<Collider2D>(object);
                    round->shape = ColliderShape2D::Circle;
                    round->radius = 0.5f;
                }
            }
        }
        const JBro::System::IPhysics2DSystem& queries = scene.physics;
        JBro::RaycastHit2D hit;
        Check(queries.Raycast({ 7, 6 }, { 1, 0 }, 3, hit, JBro::AllPhysicsLayers) && Near(hit.distance, 1.5f, 1.0e-4f),
            "a short ray still hits the circle next to it");
        Check(scene.physics.GetLastQueryColliderCount() <= 2, "and looks at no more than the colliders along it");
        Array<JBro::GameObjectHandle> found;
        queries.OverlapCircle({ 12, 12 }, 0.2f, found, JBro::AllPhysicsLayers);
        Check(found.Size() == 1 && scene.physics.GetLastQueryColliderCount() == 1, "a small circle looks at one collider");
        // 원 콜라이더(중심 (9, 6), 반지름 0.5)의 가장자리만 걸치는 질의도 찾는다 - 경계는 반지름을 품는다.
        queries.OverlapCircle({ 9.6f, 6.0f }, 0.2f, found, JBro::AllPhysicsLayers);
        Check(found.Size() == 1, "a query touching only a circle's edge still finds it");
        // 원을 민 스윕은 반지름만큼 옆의 콜라이더도 본다 - (1.8, 0.8) 에서 반지름 0.4 로 오른쪽으로 밀면 (3, 0) 의 원(반지름 0.5)에 걸린다.
        Check(queries.CircleCast({ 1.8f, 0.8f }, 0.4f, { 1, 0 }, 5, hit, JBro::AllPhysicsLayers) && hit.distance > 0.5f && hit.distance < 1.0f,
            "a circle cast finds a collider beside its line within its radius");
        // 가로로 네 배 늘린 상자의 먼 끝도 찾는다 - 경계는 크기를 곱한다.
        JBro::GameObject* stretched = scene.Object("stretched", { 40, 0 });
        scene.TransformOf(stretched)->scale = { 4, 1 };
        scene.Box(stretched, { 1, 1 });
        Check(queries.OverlapPoint({ 41.8f, 0.0f }, JBro::AllPhysicsLayers).GetInstanceId() == stretched->GetInstanceId(),
            "the far end of a scaled box is still found");
        Check(false == queries.Raycast({ 100, 100 }, { 0, 1 }, 5, hit, JBro::AllPhysicsLayers)
                && scene.physics.GetLastQueryColliderCount() == 0,
            "a query far away looks at none");
    }
}

int RunPhysics2DSystemTests()
{
    TestAFallingBoxLandsAndBothScriptsHearIt();
    TestAConcavePolygonColliderHoldsWhatFallsOnAndIntoIt();
    TestATriggerReportsWithoutPushing();
    TestStayHooksComeEveryStepWhileTouching();
    TestAOneWayColliderLetsThingsUpThrough();
    TestJointComponents();
    TestTheLayerTableReachesTheKernel();
    TestAnObjectReferenceSurvivesTheCanvasFile();
    TestLosingAPartnerEndsTheContact();
    TestSwitchingOffAMovingBodysColliderLetsItFall();
    TestRestartingDoesNotReplayOldContacts();
    TestQueriesSeePolygonsAndRotatedBoxes();
    TestTheWiderQueries();
    TestQueriesSkipFarColliders();
    TestAStaticBodyFollowsItsTransform();
    TestCapsuleColliders();
    TestTheWorkerCountReachesTheKernel();
    TestCountingPhysicsWork();
    TestRigidbodyForcesLocksAndDamping();
    TestChangingTheMassKeepsTheContact();
    TestChainCollidersAndSleep();
    TestAnAnimatedColliderKeepsItsContact();
    TestTheFixedStepDoesNotAllocate();
    TestABodyUnderASkewedOrMirroredParentKeepsItsRotation();
    TestAnEmptyPolygonCollidesAsItsSizeBox();
    TestScaleGrowsTheShape();
    TestAnOffCenterBodyTurnsAboutItsCenterOfMass();
    std::cout << "Physics2D system tests passed.\n";
    return 0;
}
