#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
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
        scene.TransformOf(upright)->rotation = 1.5707963f;
        scene.Box(upright, { 2, 1 })->shape = ColliderShape2D::Capsule;
        scene.Run(3.0f);

        Check(scene.physics.GetShapeCount() == 5, "every capsule collider is a shape");
        Check(Near(scene.TransformOf(lying)->position.y, 0.5f, 2.0f * Slop)
            && Near(scene.TransformOf(lying)->rotation, 0.0f, 1.0e-3f), "a lying capsule rests a radius up, flat");
        Check(Near(scene.TransformOf(stretched)->position.y, 0.5f, 2.0f * Slop)
            && Near(scene.TransformOf(stretched)->rotation, 0.0f, 1.0e-3f),
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
    TestTheWiderQueries();
    TestAStaticBodyFollowsItsTransform();
    TestCapsuleColliders();
    TestAnAnimatedColliderKeepsItsContact();
    TestTheFixedStepDoesNotAllocate();
    TestAnEmptyPolygonCollidesAsItsSizeBox();
    TestScaleGrowsTheShape();
    TestAnOffCenterBodyTurnsAboutItsCenterOfMass();
    std::cout << "Physics2D system tests passed.\n";
    return 0;
}
