#include <JBro/Physics2D/World.h>

#include <cmath>
#include <utility>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

// 2D 물리 커널의 월드와 솔버 테스트(D-199, physics-plan §4 의 3 단계).
// 기존 엔진의 오목 폴리곤 결함이 시뮬레이션에서 어떻게 보였는지(홈 안의 상자가 벽을 뚫음, L 자 물체가 제 중심을
// 벗어나 돎)를 그대로 재현하는 장면으로 붙잡는다.
namespace
{
    using JBro::Array;
    using JBro::ArrayView;
    using JBro::Vec2;
    using JBro::Physics2D::BodyDef;
    using JBro::Physics2D::BodyId;
    using JBro::Physics2D::BodyType;
    using JBro::Physics2D::ShapeDef;
    using JBro::Physics2D::ShapeId;
    using JBro::Physics2D::World;

    constexpr float Frame = 1.0f / 60.0f;

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

    float Length(Vec2 v)
    {
        return std::sqrt(v.x * v.x + v.y * v.y);
    }

    Array<Vec2> BoxOutline(float halfWidth, float halfHeight)
    {
        return { { -halfWidth, -halfHeight }, { halfWidth, -halfHeight },
                 { halfWidth, halfHeight }, { -halfWidth, halfHeight } };
    }

    Array<Vec2> UOutline()
    {
        return { { 0, 0 }, { 3, 0 }, { 3, 3 }, { 2, 3 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } };
    }

    Array<Vec2> LOutline()
    {
        return { { 0, 0 }, { 2, 0 }, { 2, 1 }, { 1, 1 }, { 1, 3 }, { 0, 3 } };
    }

    ShapeId AddPolygon(World& world, BodyId body, const Array<Vec2>& outline, const ShapeDef& def = {})
    {
        ShapeId shape;
        Check(world.CreatePolygonShape(body, outline.View(), def, shape) == JBro::Physics2D::PolygonError::None,
            "the test polygon is accepted");
        return shape;
    }

    BodyId AddBody(World& world, BodyType type, Vec2 position, float angle = 0.0f)
    {
        BodyDef def;
        def.type = type;
        def.position = position;
        def.angle = angle;
        return world.CreateBody(def);
    }

    BodyId AddGround(World& world, const ShapeDef& def = {})
    {
        const BodyId ground = AddBody(world, BodyType::Static, { 0, -0.5f });
        AddPolygon(world, ground, BoxOutline(20.0f, 0.5f), def);
        return ground;
    }

    void Run(World& world, float seconds)
    {
        const int steps = static_cast<int>(seconds / Frame + 0.5f);
        for (int i = 0; i < steps; ++i)
        {
            world.Step(Frame);
        }
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

    void TestABoxFallsAndRestsOnTheGround()
    {
        World world;
        AddGround(world);
        const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 2 });
        AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
        Run(world, 3.0f);

        const Vec2 position = world.GetPosition(box);
        Check(Near(position.y, 0.5f, 2.0f * JBro::Physics2D::LinearSlop), "the box rests on the ground");
        Check(Near(position.x, 0.0f, 1.0e-3f), "without drifting sideways");
        Check(Near(world.GetAngle(box), 0.0f, 1.0e-3f), "or tipping");
        Check(Length(world.GetLinearVelocity(box)) < 0.01f, "and is still");
    }

    // **U 의 홈에 떨어뜨리거나 홈 안에서 옆으로 민 상자는 홈을 벗어나지 않는다.** 기존 엔진에서는 안벽의 법선이 도형
    // 중심 쪽으로 뒤집혀 상자가 벽 속으로 밀렸다(physics-plan §1.2 의 1).
    void TestABoxInTheNotchOfAUStaysInside()
    {
        World world;
        const BodyId u = AddBody(world, BodyType::Static, { 0, 0 });
        AddPolygon(world, u, UOutline());

        const BodyId dropped = AddBody(world, BodyType::Dynamic, { 1.5f, 2.5f });
        AddPolygon(world, dropped, BoxOutline(0.3f, 0.3f));
        Run(world, 2.0f);
        Vec2 position = world.GetPosition(dropped);
        Check(Near(position.y, 1.3f, 2.0f * JBro::Physics2D::LinearSlop), "a box dropped into the notch lands on its floor");
        Check(position.x > 1.3f - JBro::Physics2D::LinearSlop && position.x < 1.7f + JBro::Physics2D::LinearSlop,
            "between the inner walls");

        for (const float push : { 4.0f, -4.0f })
        {
            world.SetTransform(dropped, { 1.5f, 1.3f }, 0.0f);
            world.SetLinearVelocity(dropped, { push, 0 });
            world.SetAngularVelocity(dropped, 0.0f);
            Run(world, 1.5f);
            position = world.GetPosition(dropped);
            Check(position.x > 1.3f - 2.0f * JBro::Physics2D::LinearSlop
                && position.x < 1.7f + 2.0f * JBro::Physics2D::LinearSlop,
                "a box shoved sideways in the notch stops at the inner wall, not in it");
            Check(position.y > 1.0f, "and does not sink through the notch floor");
        }
    }

    // **L 자 물체는 제 질량 중심을 기준으로 돈다.** 외력 없이 돌리면 중심은 제자리이고 원점이 그 둘레를 돈다.
    // 기존 엔진은 원점을 기준으로 돌려 중심이 스텝마다 어긋났다(physics-plan §1.2 의 5).
    void TestAnLShapedBodyTurnsAboutItsCenterOfMass()
    {
        World world;
        world.Settings().gravity = { 0, 0 };
        BodyDef def;
        def.angularVelocity = 2.0f;
        const BodyId l = world.CreateBody(def);
        AddPolygon(world, l, LOutline());

        const JBro::Physics2D::MassData outline = JBro::Physics2D::ComputeOutlineMass(LOutline().View(), 1.0f);
        const JBro::Physics2D::MassData mass = world.GetMassData(l);
        Check(Near(mass.center.x, outline.center.x, 1.0e-4f) && Near(mass.center.y, outline.center.y, 1.0e-4f),
            "the body's center of mass is the L's centroid");
        Check(Near(mass.mass, 1.0f, 1.0e-6f), "and it weighs what was asked");
        Check(Near(mass.inertia, outline.inertia / outline.mass, 1.0e-4f), "with the L's inertia scaled to that mass");

        const Vec2 center = world.GetWorldCenter(l);
        Run(world, 1.0f);
        const Vec2 after = world.GetWorldCenter(l);
        Check(Near(after.x, center.x, 1.0e-4f) && Near(after.y, center.y, 1.0e-4f), "the center of mass stays put");
        Check(Near(world.GetAngle(l), 2.0f, 1.0e-3f), "while the body turns two radians in a second");
        Check(Near(world.GetAngularVelocity(l), 2.0f, 1.0e-4f), "and keeps its spin");
        // 원점 = 중심 - R(각도)·로컬 중심. 원점이 중심을 따라 붙거나 제자리에 있으면 트랜스폼이 그림과 어긋난다.
        const float angle = world.GetAngle(l);
        const Vec2 local = mass.center;
        const Vec2 expected = {
            after.x - (std::cos(angle) * local.x - std::sin(angle) * local.y),
            after.y - (std::sin(angle) * local.x + std::cos(angle) * local.y) };
        const Vec2 origin = world.GetPosition(l);
        Check(Near(origin.x, expected.x, 1.0e-4f) && Near(origin.y, expected.y, 1.0e-4f),
            "so the origin swings around the center, a rotated local center away from it");
    }

    // **박힌 채 생긴 물체는 위치 보정이 튕기지 않고 빼낸다.** 미리 만든 접촉 덕에 떨어뜨린 물체는 거의 박히지 않으므로,
    // 위치 보정이 실제로 일하는 것은 이렇게 겹쳐 놓았을 때다(에디터에서 겹쳐 배치하고 재생을 누르는 경우).
    void TestAnOverlappingBodyIsPushedOutWithoutBouncing()
    {
        World world;
        AddGround(world);
        const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 0.3f });
        AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
        float highest = 0.0f;
        for (int i = 0; i < 60; ++i)
        {
            world.Step(Frame);
            highest = std::fmax(highest, world.GetPosition(box).y);
        }
        Check(Near(world.GetPosition(box).y, 0.5f, 2.0f * JBro::Physics2D::LinearSlop),
            "a box spawned 0.2 into the ground ends up resting on it");
        Check(highest < 0.5f + 0.02f, "without being launched upward");
        Check(Length(world.GetLinearVelocity(box)) < 0.01f, "and without gaining speed");
    }

    // **쌓기.** 맨 위에 공을 얹는다. 공을 가장 먼저 만들면 폴리곤-원 쌍이 도형 번호와 다른 순서로 나오는데,
    // 접촉을 열쇠 순으로 다시 정렬하지 않으면 직전 스텝과 짝짓기가 어긋나 스택 전체의 워밍스타트가 끊긴다.
    void TestAStackOfTenBoxesSettles()
    {
        World world;
        JBro::Physics2D::Circle ball;
        ball.radius = 0.3f;
        const BodyId top = AddBody(world, BodyType::Dynamic, { 0, 10.4f });
        world.CreateCircleShape(top, ball, {});
        AddGround(world);
        Array<BodyId> boxes;
        for (int i = 0; i < 10; ++i)
        {
            const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 0.5f + static_cast<float>(i) * 1.01f });
            AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
            boxes.Add(box);
        }
        Run(world, 5.0f);
        for (int i = 0; i < 10; ++i)
        {
            const Vec2 position = world.GetPosition(boxes[i]);
            Check(Length(world.GetLinearVelocity(boxes[i])) < 0.05f, "every box in the stack comes to rest");
            Check(Near(position.x, 0.0f, 0.05f), "and the stack stays upright");
            Check(Near(position.y, 0.5f + static_cast<float>(i), 0.1f), "each box sits on the one below");
        }
        Check(Near(world.GetPosition(top).y, 10.3f, 0.1f), "and the ball stays on top");
        Check(Length(world.GetLinearVelocity(top)) < 0.05f, "at rest");
    }

    // **감쇠는 1 / (1 + h·c) 로 줄인다.** 1 초에 c = 1 이면 약 e^-1 로 준다.
    void TestLinearDampingSlowsABody()
    {
        World world;
        world.Settings().gravity = { 0, 0 };
        BodyDef def;
        def.linearVelocity = { 10, 0 };
        def.linearDamping = 1.0f;
        const BodyId damped = world.CreateBody(def);
        def.linearDamping = 0.0f;
        const BodyId free = world.CreateBody(def);
        Run(world, 1.0f);
        const float expected = 10.0f * std::pow(1.0f + Frame / 4.0f, -240.0f);
        Check(Near(world.GetLinearVelocity(damped).x, expected, 0.01f), "damping 1 slows 10 to about 3.7 in a second");
        Check(Near(world.GetLinearVelocity(free).x, 10.0f, 0.0f), "and no damping keeps the speed");
    }

    // **같은 입력은 비트까지 같은 결과다.** 브로드페이즈 쌍과 접촉을 열쇠 순으로 정렬해 두는 이유다.
    void TestTheSameSceneRunsTheSameTwice()
    {
        float results[2][6] = {};
        for (int run = 0; run < 2; ++run)
        {
            World world;
            AddGround(world);
            const BodyId u = AddBody(world, BodyType::Static, { 5, 0 });
            AddPolygon(world, u, UOutline());
            const BodyId a = AddBody(world, BodyType::Dynamic, { 6.5f, 3.0f }, 0.3f);
            AddPolygon(world, a, BoxOutline(0.3f, 0.3f));
            const BodyId b = AddBody(world, BodyType::Dynamic, { 0.2f, 2.0f }, 0.7f);
            AddPolygon(world, b, LOutline());
            JBro::Physics2D::Circle circle;
            circle.radius = 0.4f;
            const BodyId c = AddBody(world, BodyType::Dynamic, { -1.0f, 4.0f });
            world.CreateCircleShape(c, circle, {});
            Run(world, 2.0f);
            const Vec2 pa = world.GetPosition(a);
            const Vec2 pb = world.GetPosition(b);
            const Vec2 pc = world.GetPosition(c);
            const float values[6] = { pa.x, pa.y, pb.x, pb.y, pc.x, pc.y };
            std::memcpy(results[run], values, sizeof(values));
        }
        Check(std::memcmp(results[0], results[1], sizeof(results[0])) == 0, "two runs agree bit for bit");
    }

    // **마찰.** 30 도 경사에서 tan 30 = 0.577 보다 큰 마찰이면 머물고, 작으면 미끄러진다.
    void TestFrictionHoldsOrLetsGoOnASlope()
    {
        for (const float friction : { 0.8f, 0.1f })
        {
            World world;
            ShapeDef def;
            def.friction = friction;
            const float angle = 0.52359878f;
            const BodyId slope = AddBody(world, BodyType::Static, { 0, 0 }, angle);
            AddPolygon(world, slope, BoxOutline(20.0f, 0.5f), def);
            const Vec2 normal = { -std::sin(angle), std::cos(angle) };
            const Vec2 start = { normal.x * 1.0f, normal.y * 1.0f };
            const BodyId box = AddBody(world, BodyType::Dynamic, start, angle);
            AddPolygon(world, box, BoxOutline(0.5f, 0.5f), def);
            Run(world, 2.0f);
            const Vec2 end = world.GetPosition(box);
            const float moved = Length({ end.x - start.x, end.y - start.y });
            if (friction > 0.6f)
            {
                Check(moved < 0.05f, "a grippy box stays on the slope");
            }
            else
            {
                Check(moved > 1.0f, "a slippery box slides down");
            }
        }
    }

    // **반발.** 반발이 있으면 튀어 오르고, 없으면 바닥에 붙는다.
    void TestRestitutionBounces()
    {
        for (const float restitution : { 0.8f, 0.0f })
        {
            World world;
            AddGround(world);
            ShapeDef def;
            def.restitution = restitution;
            JBro::Physics2D::Circle ball;
            ball.radius = 0.5f;
            const BodyId body = AddBody(world, BodyType::Dynamic, { 0, 3.0f });
            world.CreateCircleShape(body, ball, def);

            bool landed = false;
            float highest = 0.0f;
            for (int i = 0; i < 240; ++i)
            {
                world.Step(Frame);
                if (world.GetBeginEvents().Size() > 0)
                {
                    landed = true;
                }
                if (landed)
                {
                    highest = std::fmax(highest, world.GetPosition(body).y);
                }
            }
            Check(landed, "the ball reaches the ground");
            if (restitution > 0.0f)
            {
                Check(highest > 1.5f, "a bouncy ball rises well above the floor again");
            }
            else
            {
                Check(highest < 0.6f, "a dead ball stays down");
            }
        }
    }

    // **이벤트는 도형 쌍마다 하나다.** 넓은 상자가 U 의 두 기둥 윗면(한 도형의 두 조각)에 함께 얹혀도 시작은 한 번이고,
    // 순간 이동으로 떼면 끝이 한 번이다. userData 가 실려 온다.
    void TestContactEventsArePerShapePair()
    {
        World world;
        ShapeDef uDef;
        uDef.userData = 11;
        const BodyId u = AddBody(world, BodyType::Static, { 0, 0 });
        AddPolygon(world, u, UOutline(), uDef);

        ShapeDef boxDef;
        boxDef.userData = 22;
        const BodyId box = AddBody(world, BodyType::Dynamic, { 1.5f, 3.6f });
        AddPolygon(world, box, BoxOutline(1.5f, 0.5f), boxDef);

        int begins = 0;
        for (int i = 0; i < 120; ++i)
        {
            world.Step(Frame);
            for (const JBro::Physics2D::ContactEvent& event : world.GetBeginEvents())
            {
                ++begins;
                const bool matches = (event.userDataA == 11 && event.userDataB == 22)
                    || (event.userDataA == 22 && event.userDataB == 11);
                Check(matches, "the begin event names both shapes");
                Check(false == event.isTrigger, "and is not a trigger");
                // U 가 먼저 만든 도형이라 A 다. 상자는 위에 얹히므로 A→B 법선은 위(+y)이고, 접촉점은 기둥 윗면 y = 3 이다.
                Check(event.userDataA == 11, "the lower shape index is A");
                Check(Near(event.normal.x, 0.0f, 1.0e-4f) && Near(event.normal.y, 1.0f, 1.0e-4f),
                    "the begin normal points from A (the U) to B (the box)");
                Check(Near(event.point.y, 3.0f, 0.05f)
                    && ((event.point.x >= 0.0f && event.point.x <= 1.0f) || (event.point.x >= 2.0f && event.point.x <= 3.0f)),
                    "and the point sits on top of one of the pillars");
            }
            Check(world.GetEndEvents().IsEmpty(), "nothing ends while the box rests");
        }
        Check(begins == 1, "resting on two pillars of one shape begins once");
        Check(Near(world.GetPosition(box).y, 3.5f, 2.0f * JBro::Physics2D::LinearSlop), "the box rests on both pillars");

        world.SetTransform(box, { 1.5f, 10.0f }, 0.0f);
        world.Step(Frame);
        Check(world.GetEndEvents().Size() == 1, "lifting it away ends once");

        // 닿아 있는 몸을 지우면 다음 스텝에 끝이 나온다. 번호는 죽었지만 userData 가 남는다.
        world.SetTransform(box, { 1.5f, 3.5f }, 0.0f);
        world.SetLinearVelocity(box, { 0, 0 });
        Run(world, 0.5f);
        world.DestroyBody(box);
        Check(false == world.IsValid(box), "the destroyed body is gone");
        world.Step(Frame);
        Check(world.GetEndEvents().Size() == 1, "a destroyed body ends its contacts");
        const JBro::Physics2D::ContactEvent& ended = world.GetEndEvents()[0];
        Check(ended.userDataA == 22 || ended.userDataB == 22, "and the event still carries its user data");
    }

    // **트리거는 밀지 않고 알리기만 한다.**
    void TestTriggersReportButDoNotPush()
    {
        World world;
        ShapeDef trigger;
        trigger.isTrigger = true;
        const BodyId zone = AddBody(world, BodyType::Static, { 0, 2 });
        AddPolygon(world, zone, BoxOutline(2.0f, 0.5f), trigger);

        JBro::Physics2D::Circle ball;
        ball.radius = 0.25f;
        const BodyId body = AddBody(world, BodyType::Dynamic, { 0, 4 });
        world.CreateCircleShape(body, ball, {});

        int begins = 0;
        int ends = 0;
        for (int i = 0; i < 90; ++i)
        {
            world.Step(Frame);
            for (const JBro::Physics2D::ContactEvent& event : world.GetBeginEvents())
            {
                Check(event.isTrigger, "entering the zone is a trigger event");
                Check(event.normal.x == 0.0f && event.normal.y == 0.0f && event.point.x == 0.0f && event.point.y == 0.0f,
                    "and carries no contact point or normal");
                ++begins;
            }
            ends += static_cast<int>(world.GetEndEvents().Size());
        }
        Check(begins == 1 && ends == 1, "the ball enters and leaves the zone once each");
        Check(world.GetPosition(body).y < 1.0f, "and falls straight through it");
    }

    // **레이어와 마스크가 맞지 않으면 만나지 않는다.**
    void TestLayersFilterContacts()
    {
        World world;
        ShapeDef groundDef;
        groundDef.layer = 0x2u;
        AddGround(world, groundDef);
        ShapeDef ghost;
        ghost.mask = ~0x2u;
        const BodyId body = AddBody(world, BodyType::Dynamic, { 0, 1 });
        AddPolygon(world, body, BoxOutline(0.5f, 0.5f), ghost);
        Run(world, 1.0f);
        Check(world.GetPosition(body).y < -1.0f, "a box that masks out the ground's layer falls through it");
    }

    // **움직이는 키네마틱 몸은 동적 몸을 밀고, 저는 밀리지 않는다.**
    void TestAKinematicBodyPushesADynamicOne()
    {
        World world;
        AddGround(world);
        const BodyId pusher = AddBody(world, BodyType::Kinematic, { -2, 0.5f });
        AddPolygon(world, pusher, BoxOutline(0.5f, 0.5f));
        world.SetLinearVelocity(pusher, { 1, 0 });
        const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 0.5f });
        AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
        Run(world, 3.0f);
        Check(Near(world.GetPosition(pusher).x, 1.0f, 1.0e-3f), "the kinematic body moves at its own speed");
        Check(world.GetPosition(box).x > world.GetPosition(pusher).x + 0.9f, "and shoves the box ahead of it");
        Check(Near(world.GetMassData(pusher).mass, 0.0f, 0.0f), "a kinematic body has no mass");
    }

    void TestHandlesAndMassUpdates()
    {
        World world;
        const BodyId body = AddBody(world, BodyType::Dynamic, { 1, 2 });
        Check(world.GetMassData(body).inertia == 0.0f, "a body with no shapes does not turn");
        const ShapeId shape = AddPolygon(world, body, BoxOutline(1.0f, 0.5f));
        Check(world.GetChildCount(shape) == 1, "a box is one child");
        Check(world.GetMassData(body).inertia > 0.0f, "adding a shape gives it inertia");
        Check(Near(world.GetPosition(body).x, 1.0f, 0.0f) && Near(world.GetPosition(body).y, 2.0f, 0.0f),
            "and does not move it");

        // 트리거는 무게가 없다. 옆에 붙인 감지 칸이 물체의 중심을 끌어가면 그 물체는 기울어 넘어진다.
        const JBro::Physics2D::MassData before = world.GetMassData(body);
        ShapeDef sensor;
        sensor.isTrigger = true;
        JBro::Physics2D::Circle zone;
        zone.center = { 5, 0 };
        zone.radius = 1.0f;
        const ShapeId trigger = world.CreateCircleShape(body, zone, sensor);
        Check(world.IsValid(trigger), "a trigger circle attaches");
        const JBro::Physics2D::MassData after = world.GetMassData(body);
        Check(after.center.x == before.center.x && after.inertia == before.inertia,
            "and does not shift the center of mass or the inertia");
        world.DestroyShape(trigger);

        ShapeId bad;
        const Array<Vec2> bowTie = { { 0, 0 }, { 2, 2 }, { 2, 0 }, { 0, 2 } };
        Check(world.CreatePolygonShape(body, bowTie.View(), {}, bad) == JBro::Physics2D::PolygonError::SelfIntersecting,
            "a self-intersecting outline is refused");
        Check(false == world.IsValid(bad), "and makes no shape");

        world.DestroyShape(shape);
        Check(false == world.IsValid(shape), "a destroyed shape is gone");
        world.DestroyBody(body);
        const BodyId reused = AddBody(world, BodyType::Dynamic, { 0, 0 });
        Check(reused.index == body.index && reused.generation != body.generation,
            "a reused slot gets a new generation");
        Check(false == world.IsValid(body), "so the old handle stays dead");
    }

    // **캡슐은 누우면 반지름만큼 떠서 서고, 세우면 둥근 끝으로 선다.** 원으로 줄어든 캡슐은 원이다.
    void TestCapsulesRestOnTheGround()
    {
        World world;
        AddGround(world);
        const BodyId lying = AddBody(world, BodyType::Dynamic, { -5, 2 });
        const ShapeId lyingShape = world.CreateCapsuleShape(lying, { -1, 0 }, { 1, 0 }, 0.5f, {});
        Check(world.IsValid(lyingShape) && world.GetChildCount(lyingShape) == 1, "a capsule is one piece");
        const BodyId round = AddBody(world, BodyType::Dynamic, { 5, 2 });
        const ShapeId roundShape = world.CreateCapsuleShape(round, { 0, 0 }, { 0, 0.001f }, 0.5f, {});
        Check(world.GetPolygonChild(roundShape, 0) == nullptr, "two points closer than the slop make a circle");
        Run(world, 3.0f);

        Check(Near(world.GetPosition(lying).y, 0.5f, 2.0f * JBro::Physics2D::LinearSlop), "the lying capsule rests a radius up");
        Check(Near(world.GetAngle(lying), 0.0f, 1.0e-3f) && Length(world.GetLinearVelocity(lying)) < 0.01f,
            "flat and still");
        Check(Near(world.GetPosition(round).y, 0.5f, 2.0f * JBro::Physics2D::LinearSlop), "and the round one like a ball");

        const JBro::Physics2D::MassData mass = world.GetMassData(lying);
        const JBro::Physics2D::MassData unit = JBro::Physics2D::ComputeCapsuleMass({ -1, 0 }, { 1, 0 }, 0.5f, 1.0f);
        Check(Near(mass.mass, 1.0f, 0.0f) && Near(mass.inertia, unit.inertia / unit.mass, 1.0e-5f),
            "the requested mass spreads over the capsule's shape");
    }

    // **모양을 제자리에서 바꾸면 닿아 있던 쌍이 이어진다(physics-plan §4 의 4 (1)).** 크기를 움직이는 상자가 바닥에 서 있는
    // 동안 끝·시작 이벤트가 나지 않고, 틀린 외곽선은 모양을 그대로 두고, 레이어를 바꿔 거르면 그제야 끝난다.
    void TestReshapingKeepsTheContact()
    {
        World world;
        // 바닥은 레이어 1 만 받는다. 뒤에서 상자의 레이어만 바꿔 거른다.
        ShapeDef groundDef;
        groundDef.mask = 0x1u;
        AddGround(world, groundDef);
        const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 0.5f });
        const ShapeId shape = AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
        Run(world, 0.5f);
        Check(world.GetBeginEvents().IsEmpty() && world.GetEndEvents().IsEmpty(), "the box has settled");
        const float squareInertia = world.GetMassData(box).inertia;

        int begins = 0;
        int ends = 0;
        for (int i = 0; i < 60; ++i)
        {
            const float half = 0.5f + 0.02f * static_cast<float>(i % 5);
            Check(world.SetPolygonGeometry(shape, BoxOutline(half, 0.5f).View()) == JBro::Physics2D::PolygonError::None,
                "a wider box is accepted");
            world.Step(Frame);
            begins += static_cast<int>(world.GetBeginEvents().Size());
            ends += static_cast<int>(world.GetEndEvents().Size());
        }
        Check(world.IsValid(shape) && begins == 0 && ends == 0, "growing and shrinking it in place never ends the contact");
        Check(world.SetPolygonGeometry(shape, BoxOutline(1.0f, 0.5f).View()) == JBro::Physics2D::PolygonError::None
            && Near(world.GetMassData(box).inertia, squareInertia * (4.0f + 1.0f) / (1.0f + 1.0f), 1.0e-4f),
            "a reshaped box turns with the inertia of its new shape: m(w^2 + h^2)/12");
        Check(Near(world.GetPosition(box).y, 0.5f, 2.0f * JBro::Physics2D::LinearSlop), "and it stays on the ground");

        const Array<Vec2> bowTie = { { 0, 0 }, { 2, 2 }, { 2, 0 }, { 0, 2 } };
        Check(world.SetPolygonGeometry(shape, bowTie.View()) == JBro::Physics2D::PolygonError::SelfIntersecting
            && world.GetChildCount(shape) == 1, "a wrong outline is refused and the old box stays");

        Check(world.SetCapsuleGeometry(shape, { -0.5f, 0 }, { 0.5f, 0 }, 0.5f) && world.GetPolygonChild(shape, 0)->count == 2,
            "the same shape can turn into a capsule");
        Check(world.SetCapsuleGeometry(shape, { 0, 0 }, { 0, 0.001f }, 0.5f) && world.GetPolygonChild(shape, 0) == nullptr,
            "or, too short, a circle");
        Run(world, 0.5f);

        ShapeDef apart;
        apart.layer = 0x2u;
        world.SetSurface(shape, apart);
        world.Step(Frame);
        Check(world.GetEndEvents().Size() == 1, "filtering it out by layer ends the contact");
    }

    // **스텝은 힙을 건드리지 않는다(ProjectRule §9).** 쌓인 상자·U 에 든 조약돌이 서 있는 스텝과, 모양을 스텝마다 바꾸는 스텝을 잰다.
    // 스크래치 배열은 처음 몇 스텝에 용량이 차고 그 뒤로는 자라지 않아야 한다.
    void TestSteppingDoesNotAllocate()
    {
        World world;
        AddGround(world);
        for (int i = 0; i < 5; ++i)
        {
            const BodyId box = AddBody(world, BodyType::Dynamic, { -4.0f, 0.5f + static_cast<float>(i) });
            AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
        }
        const BodyId cup = AddBody(world, BodyType::Static, { 4, 0 });
        AddPolygon(world, cup, UOutline());
        const BodyId pebble = AddBody(world, BodyType::Dynamic, { 5.5f, 2 });
        JBro::Physics2D::Circle round;
        round.radius = 0.3f;
        world.CreateCircleShape(pebble, round, {});
        const BodyId pill = AddBody(world, BodyType::Dynamic, { 0, 1 });
        world.CreateCapsuleShape(pill, { -0.5f, 0 }, { 0.5f, 0 }, 0.4f, {});
        const BodyId growing = AddBody(world, BodyType::Dynamic, { 8, 0.5f });
        const ShapeId growingShape = AddPolygon(world, growing, BoxOutline(0.5f, 0.5f));
        Array<Array<Vec2>> outlines;
        for (int i = 0; i < 5; ++i)
        {
            outlines.Add(BoxOutline(0.5f + 0.02f * static_cast<float>(i), 0.5f));
        }
        const auto step = [&](int i)
        {
            world.SetPolygonGeometry(growingShape, outlines[static_cast<std::size_t>(i % 5)].View());
            world.Step(Frame);
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
        std::cout << "  CRT allocations during 120 physics steps (one shape reshaped each step): " << g_allocations << '\n';
        Check(g_allocations == 0, "a physics step, reshaping included, does not touch the heap");
#endif
    }

    // 워커 테스트의 장면: 10 층 상자 피라미드(55), 원 20, 캡슐 5, U 에 든 조약돌. 쉬는 동안에도 후보가 64 를 넘는다.
    Array<BodyId> BuildPile(World& world)
    {
        Array<BodyId> moving;
        AddGround(world);
        for (int row = 0; row < 10; ++row)
        {
            for (int column = 0; column < 10 - row; ++column)
            {
                const float x = -5.0f + static_cast<float>(column) + 0.5f * static_cast<float>(row);
                const BodyId box = AddBody(world, BodyType::Dynamic, { x, 0.5f + static_cast<float>(row) });
                AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
                moving.Add(box);
            }
        }
        JBro::Physics2D::Circle ball;
        ball.radius = 0.25f;
        for (int i = 0; i < 20; ++i)
        {
            const BodyId body = AddBody(world, BodyType::Dynamic, { 8.0f + 0.3f * static_cast<float>(i % 5), 1.0f + 0.6f * static_cast<float>(i / 5) });
            world.CreateCircleShape(body, ball, {});
            moving.Add(body);
        }
        for (int i = 0; i < 5; ++i)
        {
            const BodyId body = AddBody(world, BodyType::Dynamic, { -12.0f, 0.5f + static_cast<float>(i) });
            world.CreateCapsuleShape(body, { -0.5f, 0 }, { 0.5f, 0 }, 0.45f, {});
            moving.Add(body);
        }
        const BodyId cup = AddBody(world, BodyType::Static, { 14, 0 });
        AddPolygon(world, cup, UOutline());
        for (int i = 0; i < 3; ++i)
        {
            const BodyId pebble = AddBody(world, BodyType::Dynamic, { 15.5f, 2.0f + static_cast<float>(i) });
            JBro::Physics2D::Circle small;
            small.radius = 0.2f;
            world.CreateCircleShape(pebble, small, {});
            moving.Add(pebble);
        }
        return moving;
    }

    // **좁은 판정을 워커와 나눠도 결과는 같다(D-223).** 워커 0 과 3 의 두 월드를 3 초 돌려 모든 몸의 자세를 비트까지 맞댄다.
    void TestWorkersGiveTheSameResult()
    {
        World serial;
        World parallel;
        const Array<BodyId> serialBodies = BuildPile(serial);
        const Array<BodyId> parallelBodies = BuildPile(parallel);
        Check(serialBodies.Size() == 83 && parallelBodies.Size() == 83, "both piles hold 83 moving bodies");
        parallel.SetWorkerCount(3);
#if !defined(__EMSCRIPTEN__)
        Check(parallel.GetWorkerCount() == 3, "the parallel world starts three physics workers");
#endif
        for (int i = 0; i < 180; ++i)
        {
            serial.Step(Frame);
            parallel.Step(Frame);
        }
        const JBro::Physics2D::StepStats serialStats = serial.GetLastStepStats();
        const JBro::Physics2D::StepStats parallelStats = parallel.GetLastStepStats();
        Check(serialStats.candidates >= 64 && serialStats.candidates == parallelStats.candidates,
            "the pile keeps enough candidates to split, the same in both");
        Check(serialStats.parallelSubSteps == 0 && parallelStats.parallelSubSteps == parallel.Settings().subSteps,
            "only the world with workers splits its narrow phase, every substep");
        for (std::size_t i = 0; i < serialBodies.Size(); ++i)
        {
            const Vec2 a = serial.GetPosition(serialBodies[i]);
            const Vec2 b = parallel.GetPosition(parallelBodies[i]);
            const float angleA = serial.GetAngle(serialBodies[i]);
            const float angleB = parallel.GetAngle(parallelBodies[i]);
            Check(std::memcmp(&a, &b, sizeof(a)) == 0 && std::memcmp(&angleA, &angleB, sizeof(angleA)) == 0,
                "every body ends bit for bit where the single-thread world put it");
        }

        parallel.SetWorkerCount(1000);
#if !defined(__EMSCRIPTEN__)
        Check(parallel.GetWorkerCount() == JBro::Physics2D::MaxWorkerCount, "an asked-for count is capped");
#endif
        parallel.SetWorkerCount(0);
        parallel.Step(Frame);
        Check(parallel.GetWorkerCount() == 0 && parallel.GetLastStepStats().parallelSubSteps == 0,
            "and zero goes back to the main thread alone");
    }

    // **추천 워커 수(D-223).** 1024 조각 아래는 메인 한 스레드, 1024 마다 하나, 코어 수 - 1 과 4 가 상한이다.
    void TestRecommendedWorkerCounts()
    {
        using JBro::Physics2D::RecommendWorkerCount;
        Check(RecommendWorkerCount(0, 8) == 0 && RecommendWorkerCount(1023, 8) == 0, "a scene under 1024 pieces stays on the main thread");
        Check(RecommendWorkerCount(1024, 8) == 1 && RecommendWorkerCount(3000, 8) == 2, "a bigger one gets a worker per 1024 pieces");
        Check(RecommendWorkerCount(50000, 8) == 4 && RecommendWorkerCount(50000, 3) == 2,
            "but never more than four, nor more than the other cores");
        Check(RecommendWorkerCount(50000, 1) == 0 && RecommendWorkerCount(2048, 2) == 1, "a single core never gets workers");
    }

    // **워커와 나눠 도는 스텝도 힙을 건드리지 않는다.** 워커는 시작할 때만 세운다.
    void TestParallelSteppingDoesNotAllocate()
    {
        World world;
        BuildPile(world);
        world.SetWorkerCount(3);
        for (int i = 0; i < 60; ++i)
        {
            world.Step(Frame);
        }
#if defined(_MSC_VER) && defined(_DEBUG)
        g_allocations = 0;
        const _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(&CountAllocations);
        for (int i = 0; i < 60; ++i)
        {
            world.Step(Frame);
        }
        _CrtSetAllocHook(previous);
        std::cout << "  CRT allocations during 60 physics steps on three workers: " << g_allocations << '\n';
        Check(g_allocations == 0 && world.GetLastStepStats().parallelSubSteps > 0,
            "a physics step split across workers does not touch the heap");
#endif
    }

    // **힘·충격량·토크(D-227).** 무중력에서 질량 2 인 1x1 상자(관성 m(w²+h²)/12 = 1/3). 힘은 Step 한 번만 가해지고 비워진다.
    void TestForcesAndImpulses()
    {
        World world;
        world.Settings().gravity = { 0, 0 };
        BodyDef def;
        def.mass = 2.0f;
        const BodyId box = world.CreateBody(def);
        AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
        world.ApplyForceToCenter(box, { 4, 0 });
        world.Step(Frame);
        Check(Near(world.GetLinearVelocity(box).x, 2.0f * Frame, 1.0e-6f), "a force of 4 on a mass of 2 adds 2 m/s² for one step");
        world.Step(Frame);
        Check(Near(world.GetLinearVelocity(box).x, 2.0f * Frame, 1.0e-6f), "and is gone the step after");
        world.ApplyLinearImpulseToCenter(box, { 0, 2 });
        Check(Near(world.GetLinearVelocity(box).y, 1.0f, 1.0e-6f), "an impulse of 2 adds 1 m/s at once");
        world.ApplyTorque(box, 1.0f);
        world.Step(Frame);
        Check(Near(world.GetAngularVelocity(box), 3.0f * Frame, 1.0e-5f), "a torque of 1 on an inertia of 1/3 spins it up by 3 rad/s²");
        world.SetAngularVelocity(box, 0.0f);
        const Vec2 center = world.GetWorldCenter(box);
        world.ApplyLinearImpulse(box, { 1, 0 }, { center.x, center.y + 0.5f });
        Check(Near(world.GetAngularVelocity(box), -1.5f, 1.0e-5f), "an impulse half a unit above the center turns it by -0.5 / (1/3)");

        const BodyId ground = AddBody(world, BodyType::Static, { 0, -5 });
        world.ApplyLinearImpulseToCenter(ground, { 5, 5 });
        Check(world.GetLinearVelocity(ground).x == 0.0f, "a static body takes no impulse");
    }

    // **축 고정(D-227).** Y 를 고정한 몸은 떨어지지 않고 옆으로는 밀리며, X 를 고정한 몸은 비탈에서 미끄러지지 않고 선다.
    void TestAxisLocks()
    {
        World world;
        const BodyId floating = AddBody(world, BodyType::Dynamic, { 0, 5 });
        AddPolygon(world, floating, BoxOutline(0.5f, 0.5f));
        BodyDef lockY;
        lockY.freezePositionY = true;
        world.SetBodyProperties(floating, lockY);
        world.ApplyLinearImpulseToCenter(floating, { 1, 1 });
        Run(world, 1.0f);
        Check(Near(world.GetPosition(floating).y, 5.0f, 1.0e-6f), "a body locked in y neither falls nor rises");
        Check(Near(world.GetPosition(floating).x, 1.0f, 1.0e-3f), "but an impulse still moves it in x");

        // 45° 비탈(돌린 상자) 위에 떨어뜨린다. 풀린 몸은 옆으로 미끄러지고, x 를 고정한 몸은 그 자리에 선다.
        const BodyId slope = AddBody(world, BodyType::Static, { 10, 0 }, 0.78539816f);
        AddPolygon(world, slope, BoxOutline(3.0f, 3.0f));
        ShapeDef slippery;
        slippery.friction = 0.0f;
        const BodyId free = AddBody(world, BodyType::Dynamic, { 9.0f, 4.0f });
        AddPolygon(world, free, BoxOutline(0.25f, 0.25f), slippery);
        const BodyId pinned = AddBody(world, BodyType::Dynamic, { 11.0f, 4.0f });
        AddPolygon(world, pinned, BoxOutline(0.25f, 0.25f), slippery);
        BodyDef lockX;
        lockX.freezePositionX = true;
        world.SetBodyProperties(pinned, lockX);
        Run(world, 2.0f);
        Check(std::fabs(world.GetPosition(free).x - 9.0f) > 0.5f, "a free body slides off the frictionless slope");
        Check(Near(world.GetPosition(pinned).x, 11.0f, 1.0e-5f) && world.GetPosition(pinned).y < 4.0f,
            "a body locked in x drops onto it and stays in its column");
    }

    // **성질을 제자리에서 바꾸면 닿아 있던 쌍이 이어진다(D-227).** 서 있는 상자의 질량·감쇠를 바꿔도 끝·시작 이벤트가 없다.
    void TestBodyPropertiesChangeInPlace()
    {
        World world;
        AddGround(world);
        const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 0.5f });
        AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
        Run(world, 0.5f);
        BodyDef heavier;
        heavier.mass = 5.0f;
        heavier.angularDamping = 2.0f;
        world.SetBodyProperties(box, heavier);
        world.Step(Frame);
        Check(world.GetBeginEvents().IsEmpty() && world.GetEndEvents().IsEmpty(), "changing the mass keeps the contact");
        Check(Near(world.GetMassData(box).mass, 5.0f, 0.0f), "and the new mass is used");
    }

    // **고정한 축은 유효 질량에서도 빠진다(D-227).** x 를 고정한 몸이 반발 1·마찰 0 인 45° 비탈에 떨어지면 세로로만 움직일 수 있으므로
    // 떨어진 속력 그대로 튀어 오른다. 반발은 한 번만 풀어서, 유효 질량을 축 고정 없이 재면 절반 속력으로만 튄다.
    void TestALockedBodyBouncesWithItsRealMass()
    {
        World world;
        const BodyId slope = AddBody(world, BodyType::Static, { 0, 0 }, 0.78539816f);
        ShapeDef bouncy;
        bouncy.friction = 0.0f;
        bouncy.restitution = 1.0f;
        AddPolygon(world, slope, BoxOutline(3.0f, 3.0f), bouncy);
        BodyDef def;
        def.position = { 1.0f, 6.0f };
        def.freezePositionX = true;
        def.fixedRotation = true;
        const BodyId ball = world.CreateBody(def);
        JBro::Physics2D::Circle round;
        round.radius = 0.25f;
        world.CreateCircleShape(ball, round, bouncy);
        float fallSpeed = 0.0f;
        float riseSpeed = 0.0f;
        for (int i = 0; i < 120; ++i)
        {
            world.Step(Frame);
            const float vy = world.GetLinearVelocity(ball).y;
            fallSpeed = std::fmin(fallSpeed, vy);
            if (fallSpeed < -1.0f)
            {
                riseSpeed = std::fmax(riseSpeed, vy);
            }
        }
        Check(fallSpeed < -3.0f && riseSpeed > 0.9f * -fallSpeed,
            "a ball locked in x bounces off a 45 degree slope as fast as it fell");
    }

    // **체인 바닥에서는 미끄러지는 상자가 걸리지 않는다(D-229).** 1 유닛 선분 40 개를 이은 마찰 없는 바닥에서 5 m/s 로 민 상자가 2 초 뒤에도
    // 같은 속력이고 튀지 않는다.
    void TestABoxSlidesAcrossAChainWithoutSnagging()
    {
        World world;
        world.Settings().enableSleep = false;
        const BodyId ground = AddBody(world, BodyType::Static, { 0, 0 });
        Array<Vec2> points;
        for (int i = 0; i <= 40; ++i)
        {
            points.Add({ -20.0f + static_cast<float>(i), 0.0f });
        }
        ShapeDef slippery;
        slippery.friction = 0.0f;
        const ShapeId chain = world.CreateChainShape(ground, points.View(), false, slippery);
        Check(world.IsValid(chain) && world.GetChildCount(chain) == 40, "the chain is forty segments");
        const BodyId box = AddBody(world, BodyType::Dynamic, { -15, 0.5f });
        AddPolygon(world, box, BoxOutline(0.5f, 0.5f), slippery);
        Run(world, 0.3f);
        world.SetLinearVelocity(box, { 5, 0 });
        float lowest = 10.0f;
        float highest = -10.0f;
        for (int i = 0; i < 120; ++i)
        {
            world.Step(Frame);
            lowest = std::fmin(lowest, world.GetLinearVelocity(box).x);
            highest = std::fmax(highest, std::fabs(world.GetLinearVelocity(box).y));
        }
        Check(lowest > 4.95f, "the box keeps its speed across every seam");
        Check(highest < 0.05f, "and never hops");
        const Array<Vec2> tooFew = { { 0, 0 } };
        Check(false == world.IsValid(world.CreateChainShape(ground, tooFew.View(), false, {})), "one point is no chain");
    }

    // **수면(D-229).** 서 있는 상자는 0.5 초쯤 뒤 잠들고, 충격량·다른 몸의 충돌·바닥을 옮기기로 깬다. 잠들 수 없는 몸은 깨어 있다.
    void TestBodiesFallAsleepAndWake()
    {
        World world;
        const BodyId ground = AddGround(world);
        const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 0.5f });
        AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
        BodyDef restless;
        restless.canSleep = false;
        const BodyId awake = world.CreateBody(restless);
        world.SetTransform(awake, { 5, 0.5f }, 0.0f);
        AddPolygon(world, awake, BoxOutline(0.5f, 0.5f));
        Run(world, 0.2f);
        Check(world.IsAwake(box), "a box that just landed is awake");
        Run(world, 1.5f);
        Check(false == world.IsAwake(box) && world.IsAwake(awake), "after resting it sleeps, but not one that cannot");
        Check(world.GetLastStepStats().sleepingBodies == 1 && world.GetLastStepStats().awakeBodies == 1, "the stats count them");
        const Vec2 asleep = world.GetPosition(box);
        Run(world, 1.0f);
        Check(world.GetPosition(box).x == asleep.x && world.GetPosition(box).y == asleep.y, "a sleeping body does not move at all");

        world.ApplyLinearImpulseToCenter(box, { 0, 3 });
        Check(world.IsAwake(box), "an impulse wakes it");
        Run(world, 2.5f);
        Check(false == world.IsAwake(box), "and it sleeps again once it has landed");

        // 위에서 떨어진 상자가 잠든 상자를 깨운다.
        const BodyId dropped = AddBody(world, BodyType::Dynamic, { 0, 4 });
        AddPolygon(world, dropped, BoxOutline(0.5f, 0.5f));
        bool wokeByTouch = false;
        for (int i = 0; i < 90; ++i)
        {
            world.Step(Frame);
            wokeByTouch = wokeByTouch || world.IsAwake(box);
        }
        Check(wokeByTouch, "a box landing on it wakes it");
        Run(world, 2.0f);
        Check(false == world.IsAwake(box) && false == world.IsAwake(dropped), "and the two sleep together once still");

        // 바닥을 내리면 잠든 몸이 깨어 떨어진다.
        world.SetTransform(ground, { 0, -3.5f }, 0.0f);
        Run(world, 1.5f);
        Check(world.GetPosition(box).y < -2.0f, "moving the floor away wakes them and they fall");
    }

    // **잠든 더미는 풀지 않는다(D-229).** 10 층 상자 더미가 잠들면 스텝마다 풀 접촉이 없고 자리를 지킨다.
    void TestAStackSleeps()
    {
        World world;
        AddGround(world);
        Array<BodyId> boxes;
        for (int i = 0; i < 10; ++i)
        {
            const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 0.5f + static_cast<float>(i) });
            AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
            boxes.Add(box);
        }
        Run(world, 5.0f);
        Check(world.GetLastStepStats().sleepingBodies == 10, "the whole stack falls asleep");
        const float top = world.GetPosition(boxes[9]).y;
        Run(world, 2.0f);
        Check(world.GetPosition(boxes[9]).y == top, "and stays exactly where it slept");
    }

    // **무엇이 잠든 몸을 깨우는가(D-229).** 잠든 두 층 더미에서: 밑 상자를 쳐올리면 같은 스텝에 위 상자도 깨어 함께 오르고(풀기 전에 깨운다),
    // 중력을 뒤집으면 깨어 오르며, 잠든 상자를 순간 이동하면 그 자리에서 떨어지고, 잠든 상자에 겹쳐 새 벽을 세우면 깨어 밀려난다.
    void TestWhatWakesASleepingBody()
    {
        {
            World world;
            AddGround(world);
            const BodyId bottom = AddBody(world, BodyType::Dynamic, { 0, 0.5f });
            AddPolygon(world, bottom, BoxOutline(0.5f, 0.5f));
            const BodyId top = AddBody(world, BodyType::Dynamic, { 0, 1.5f });
            AddPolygon(world, top, BoxOutline(0.5f, 0.5f));
            Run(world, 2.0f);
            Check(false == world.IsAwake(bottom) && false == world.IsAwake(top), "the two-box stack sleeps");
            const float topBefore = world.GetPosition(top).y;
            world.ApplyLinearImpulseToCenter(bottom, { 0, 5 });
            world.Step(Frame);
            Check(world.IsAwake(top) && world.GetPosition(top).y > topBefore + 0.01f,
                "knocking the bottom box up lifts the top one in the same step");
        }
        {
            World world;
            AddGround(world);
            const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 0.5f });
            AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
            Run(world, 2.0f);
            Check(false == world.IsAwake(box), "the box sleeps");
            world.Settings().gravity = { 0, -5.0f };
            world.Step(Frame);
            Check(world.IsAwake(box), "changing gravity wakes it");
            Run(world, 2.0f);
            Check(false == world.IsAwake(box), "and it sleeps again once gravity stays put");
            world.Settings().gravity = { 0, 9.81f };
            Run(world, 0.5f);
            Check(world.GetPosition(box).y > 1.0f, "turning gravity over wakes it and it rises");
        }
        {
            World world;
            AddGround(world);
            const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 0.5f });
            AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
            Run(world, 2.0f);
            world.SetTransform(box, { 0, 0.5f }, 0.0f);
            Check(world.IsAwake(box), "moving a sleeping body wakes it, even in place");
            Run(world, 2.0f);
            world.SetTransform(box, { 0, 5 }, 0.0f);
            Run(world, 0.3f);
            Check(world.GetPosition(box).y < 4.9f, "a sleeping box moved into the air falls from there");
        }
        {
            World world;
            AddGround(world);
            const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 0.5f });
            AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
            Run(world, 2.0f);
            Check(false == world.IsAwake(box), "the box sleeps again");
            const BodyId wall = AddBody(world, BodyType::Static, { 0.9f, 0.5f });
            AddPolygon(world, wall, BoxOutline(0.5f, 0.5f));
            Run(world, 0.5f);
            Check(world.GetPosition(box).x < -0.05f, "a new wall overlapping it wakes it and pushes it out");
        }
    }

    // **한 방향 발판(D-230).** 위에서 떨어진 상자는 얹히고, 밑에서 뛰어오른 상자는 뚫고 올라가 위에 얹히며, 옆에서 미끄러져
    // 들어온 상자는 지나간다. 뒤집은 발판(180°)은 위가 아래라 위에서 떨어진 상자가 지나간다. 흘려보내는 동안에는 닿은 것이
    // 아니라 시작 이벤트가 없다.
    void TestOneWayPlatforms()
    {
        ShapeDef oneWay;
        oneWay.oneWay = true;
        {
            World world;
            const BodyId platform = AddBody(world, BodyType::Static, { 0, 0 });
            AddPolygon(world, platform, BoxOutline(3.0f, 0.25f), oneWay);
            const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 2 });
            AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
            Run(world, 1.5f);
            Check(Near(world.GetPosition(box).y, 0.75f, 0.02f), "a box dropped from above rests on the platform");
        }
        {
            World world;
            const BodyId platform = AddBody(world, BodyType::Static, { 0, 0 });
            AddPolygon(world, platform, BoxOutline(3.0f, 0.25f), oneWay);
            const BodyId box = AddBody(world, BodyType::Dynamic, { 0, -1.5f });
            AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
            world.SetLinearVelocity(box, { 0, 8 });
            bool beganWhileBelow = false;
            for (int i = 0; i < 90; ++i)
            {
                world.Step(Frame);
                if (world.GetPosition(box).y < 0.7f && false == world.GetBeginEvents().IsEmpty())
                {
                    beganWhileBelow = true;
                }
            }
            Check(false == beganWhileBelow, "passing up through it is not a contact");
            Check(Near(world.GetPosition(box).y, 0.75f, 0.02f), "a box jumping from below goes through and lands on top");
        }
        {
            World world;
            world.Settings().gravity = { 0, 0 };
            const BodyId platform = AddBody(world, BodyType::Static, { 0, 0 });
            AddPolygon(world, platform, BoxOutline(1.0f, 0.25f), oneWay);
            const BodyId box = AddBody(world, BodyType::Dynamic, { -3, 0 });
            AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
            world.SetLinearVelocity(box, { 4, 0 });
            Run(world, 1.5f);
            Check(world.GetPosition(box).x > 2.5f, "a box sliding in from the side passes through");
        }
        {
            World world;
            const BodyId platform = AddBody(world, BodyType::Static, { 0, 0 }, 3.14159265f);
            AddPolygon(world, platform, BoxOutline(3.0f, 0.25f), oneWay);
            const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 2 });
            AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
            Run(world, 1.0f);
            Check(world.GetPosition(box).y < -1.0f, "an upside-down platform lets a box from above fall through");
        }
    }

    // **이어지는 접촉은 Stay 로 온다(D-230).** 시작한 스텝은 시작만, 그 뒤는 스텝마다 이어짐이다. 몸이 잠들면 이어짐이 멈춘다.
    void TestStayEventsFollowTouchingPairs()
    {
        World world;
        AddGround(world);
        const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 0.52f });
        AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
        int begins = 0;
        int beganAt = -1;
        int staysBefore = 0;
        int staysAfter = 0;
        for (int i = 0; i < 20; ++i)
        {
            world.Step(Frame);
            begins += static_cast<int>(world.GetBeginEvents().Size());
            if (beganAt < 0 && false == world.GetBeginEvents().IsEmpty())
            {
                beganAt = i;
            }
            const int stays = static_cast<int>(world.GetStayEvents().Size());
            if (beganAt < 0 || beganAt == i)
            {
                staysBefore += stays;
            }
            else
            {
                staysAfter += stays;
            }
        }
        Check(begins == 1 && beganAt >= 0 && staysBefore == 0, "no stay comes before or with the step that begins the contact");
        Check(staysAfter == 19 - beganAt, "every later step reports the pair as staying once");
        Run(world, 2.0f);
        Check(false == world.IsAwake(box), "the box sleeps");
        world.Step(Frame);
        Check(world.GetStayEvents().IsEmpty(), "and a sleeping pair reports nothing");
    }

    // **레이어 충돌 표(D-230).** 레이어 1 과 2 를 떼어 두면 둘은 서로 지나가고, 둘 다 여전히 바닥(레이어 0)에는 선다.
    // 표의 한 행만 채워도 된다 - 두 쪽 비트 쌍 가운데 떼지 않은 것이 있는지를 본다.
    void TestTheLayerTableSeparatesLayers()
    {
        World world;
        world.Settings().ignoredLayers[1] = 1u << 2;
        world.Settings().ignoredLayers[2] = 1u << 1;
        AddGround(world);
        ShapeDef first;
        first.layer = 1u << 1;
        ShapeDef second;
        second.layer = 1u << 2;
        const BodyId lower = AddBody(world, BodyType::Dynamic, { 0, 0.5f });
        AddPolygon(world, lower, BoxOutline(0.5f, 0.5f), first);
        const BodyId upper = AddBody(world, BodyType::Dynamic, { 0, 3 });
        AddPolygon(world, upper, BoxOutline(0.5f, 0.5f), second);
        Run(world, 1.5f);
        Check(Near(world.GetPosition(lower).y, 0.5f, 0.02f) && Near(world.GetPosition(upper).y, 0.5f, 0.02f),
            "boxes on separated layers fall into each other and both stand on the ground");

        World same;
        AddGround(same);
        const BodyId bottom = AddBody(same, BodyType::Dynamic, { 0, 0.5f });
        AddPolygon(same, bottom, BoxOutline(0.5f, 0.5f), first);
        const BodyId top = AddBody(same, BodyType::Dynamic, { 0, 3 });
        AddPolygon(same, top, BoxOutline(0.5f, 0.5f), second);
        same.Settings().ignoredLayers[1] = 1u << 2;
        Run(same, 1.5f);
        Check(Near(same.GetPosition(top).y, 0.5f, 0.02f), "one row of the table is enough to separate them");
    }

    JBro::Physics2D::Circle MakeBall(float radius)
    {
        JBro::Physics2D::Circle ball;
        ball.radius = radius;
        return ball;
    }

    float DistanceBetween(Vec2 a, Vec2 b)
    {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    // **거리 조인트(D-230).** 단단하면 월드의 점에서 늘 같은 거리로 흔들리고, 밧줄은 그 거리 안에서는 자유롭게 떨어지다가
    // 거기서 멈추며, 용수철은 중력에 늘어난 채 선다. 밧줄·용수철로 바꾸면 쌓인 임펄스를 비운다.
    void TestDistanceJoints()
    {
        using JBro::Physics2D::DistanceJointDef;
        using JBro::Physics2D::JointId;
        {
            World world;
            const BodyId ball = AddBody(world, BodyType::Dynamic, { 2, 0 });
            world.CreateCircleShape(ball, MakeBall(0.1f), {});
            DistanceJointDef def;
            def.bodyA = ball;
            def.localAnchorB = { 0, 0 };
            def.length = 2.0f;
            const JointId joint = world.CreateDistanceJoint(def);
            Check(world.IsValid(joint) && world.GetJointCount() == 1, "a distance joint to the world is made");
            float worst = 0.0f;
            for (int i = 0; i < 120; ++i)
            {
                world.Step(Frame);
                worst = std::fmax(worst, std::fabs(DistanceBetween(world.GetPosition(ball), { 0, 0 }) - 2.0f));
            }
            Check(worst < 0.02f, "a rigid distance joint keeps the ball two metres from the pin");
            Check(world.GetPosition(ball).y < -0.5f, "and the ball swings down");
        }
        {
            World world;
            const BodyId ball = AddBody(world, BodyType::Dynamic, { 0, -1 });
            world.CreateCircleShape(ball, MakeBall(0.1f), {});
            DistanceJointDef rope;
            rope.bodyA = ball;
            rope.length = 2.0f;
            rope.maxLengthOnly = true;
            world.CreateDistanceJoint(rope);
            Run(world, 0.2f);
            Check(world.GetPosition(ball).y < -1.1f, "inside its length a rope lets the ball fall freely");
            Run(world, 1.5f);
            const float hanging = DistanceBetween(world.GetPosition(ball), { 0, 0 });
            Check(hanging > 1.95f && hanging < 2.02f, "and it stops the ball at its length");
        }
        {
            World world;
            const BodyId ball = AddBody(world, BodyType::Dynamic, { 0, -1 });
            world.CreateCircleShape(ball, MakeBall(0.1f), {});
            DistanceJointDef spring;
            spring.bodyA = ball;
            spring.length = 1.0f;
            spring.hertz = 1.0f;
            spring.dampingRatio = 0.5f;
            world.CreateDistanceJoint(spring);
            Run(world, 4.0f);
            // 고유 진동수 1 Hz 인 질량-용수철은 g / ω² ≈ 0.248 m 늘어나 선다.
            const float stretched = DistanceBetween(world.GetPosition(ball), { 0, 0 });
            Check(stretched > 1.2f && stretched < 1.3f, "a spring stretches under gravity by about g over omega squared");
        }
    }

    // **경첩(D-230).** 월드에 건 막대는 핀 둘레로만 돌고, 한계를 주면 그 각을 넘지 않으며, 모터는 목표 속도로 돌린다.
    // 이은 두 몸은 collideConnected 가 거짓이면 겹쳐도 밀지 않고, 참이면 떨어진다. 몸을 지우면 조인트도 사라진다.
    void TestHingeJoints()
    {
        using JBro::Physics2D::HingeJointDef;
        using JBro::Physics2D::JointId;
        const float degree = 3.14159265f / 180.0f;
        {
            World world;
            const BodyId rod = AddBody(world, BodyType::Dynamic, { 1, 0 });
            AddPolygon(world, rod, BoxOutline(1.0f, 0.1f));
            HingeJointDef def;
            def.bodyA = rod;
            def.localAnchorA = { -1, 0 };
            def.localAnchorB = { 0, 0 };
            def.enableLimit = true;
            def.lowerAngle = -30.0f * degree;
            def.upperAngle = 30.0f * degree;
            const JointId hinge = world.CreateHingeJoint(def);
            float highest = 0.0f;
            float pinDrift = 0.0f;
            for (int i = 0; i < 120; ++i)
            {
                world.Step(Frame);
                highest = std::fmax(highest, world.GetHingeAngle(hinge));
                const Vec2 end = world.GetPosition(rod);
                const float angle = world.GetAngle(rod);
                const Vec2 pin{ end.x - std::cos(angle), end.y - std::sin(angle) };
                pinDrift = std::fmax(pinDrift, DistanceBetween(pin, { 0, 0 }));
            }
            Check(pinDrift < 0.02f, "a hinged rod keeps its end on the pin");
            // A 가 막대이고 B 가 월드라 막대가 아래로 돌면 상대 각(B - A)이 커진다 - 위 한계가 막는다.
            Check(highest < 31.5f * degree && highest > 28.0f * degree, "and the upper limit stops it at thirty degrees");
            // 한계를 끄면 막대가 아래로 늘어진다. 같은 조인트를 제자리에서 바꾼다.
            def.enableLimit = false;
            Check(world.SetHingeJoint(hinge, def), "the hinge changes in place");
            Run(world, 3.0f);
            Check(world.GetHingeAngle(hinge) > 60.0f * degree, "without the limit the rod hangs down");
        }
        {
            World world;
            world.Settings().gravity = { 0, 0 };
            const BodyId wheel = AddBody(world, BodyType::Dynamic, { 0, 0 });
            world.CreateCircleShape(wheel, MakeBall(0.5f), {});
            HingeJointDef def;
            def.bodyA = wheel;
            def.enableMotor = true;
            def.motorSpeed = 2.0f;
            def.maxMotorTorque = 100.0f;
            const JointId motor = world.CreateHingeJoint(def);
            Run(world, 0.5f);
            // A 가 바퀴이고 B 가 월드라 상대 각속도(B - A)가 목표다: 바퀴는 거꾸로 돈다.
            Check(Near(world.GetAngularVelocity(wheel), -2.0f, 0.01f), "a motor spins the wheel at its speed");
            def.maxMotorTorque = 0.0f;
            world.SetHingeJoint(motor, def);
            Run(world, 0.5f);
            Check(Near(world.GetAngularVelocity(wheel), -2.0f, 0.05f), "with no torque the motor stops driving and the wheel coasts");
        }
        {
            World world;
            world.Settings().gravity = { 0, 0 };
            const BodyId first = AddBody(world, BodyType::Dynamic, { 0, 0 });
            AddPolygon(world, first, BoxOutline(0.5f, 0.5f));
            const BodyId second = AddBody(world, BodyType::Dynamic, { 0.5f, 0 });
            AddPolygon(world, second, BoxOutline(0.5f, 0.5f));
            HingeJointDef def;
            def.bodyA = first;
            def.bodyB = second;
            def.localAnchorA = { 0.25f, 0 };
            def.localAnchorB = { -0.25f, 0 };
            const JointId hinge = world.CreateHingeJoint(def);
            Run(world, 0.5f);
            Check(Near(world.GetPosition(second).x - world.GetPosition(first).x, 0.5f, 0.01f),
                "hinged boxes that overlap do not push each other apart");
            def.collideConnected = true;
            world.SetHingeJoint(hinge, def);
            Run(world, 0.5f);
            Check(world.GetPosition(second).x - world.GetPosition(first).x > 0.52f
                    || std::fabs(world.GetAngle(second) - world.GetAngle(first)) > 0.2f,
                "with collideConnected they do");
            world.DestroyBody(first);
            Check(false == world.IsValid(hinge) && world.GetJointCount() == 0, "destroying a body removes its joints");
            HingeJointDef stray = def;
            Check(false == world.IsValid(world.CreateHingeJoint(stray)), "a joint to a destroyed body is not made");
        }
    }

    // **조인트로 이은 몸은 한 섬이다(D-230).** 잠든 상자에 매달린 공을 깨우면 상자도 깬다.
    void TestJointedBodiesSleepTogether()
    {
        using JBro::Physics2D::DistanceJointDef;
        World world;
        AddGround(world);
        const BodyId box = AddBody(world, BodyType::Dynamic, { 0, 0.5f });
        AddPolygon(world, box, BoxOutline(0.5f, 0.5f));
        const BodyId ball = AddBody(world, BodyType::Dynamic, { 3, 0.25f });
        world.CreateCircleShape(ball, MakeBall(0.25f), {});
        DistanceJointDef def;
        def.bodyA = box;
        def.bodyB = ball;
        def.localAnchorB = { 0, 0 };
        def.length = 3.0f;
        world.CreateDistanceJoint(def);
        Run(world, 2.0f);
        Check(false == world.IsAwake(box) && false == world.IsAwake(ball), "a box and the ball tied to it sleep together");
        world.ApplyLinearImpulseToCenter(ball, { 0, 3 });
        world.Step(Frame);
        Check(world.IsAwake(box), "waking the ball wakes the box it is tied to");
    }

    // **이어지는 판정(CCD, D-231).** 서브스텝마다 0.8 m 를 가는 반지름 5 cm 공과 상자는 두께 10 cm 벽도, 체인 선분도 뚫지 않는다.
    // 바닥에 얹혀 빠르게 미끄러지는 상자는 바닥(출발부터 닿아 있다) 때문에 서지 않고, 한 방향 발판은 밑에서 오면 지나간다.
    void TestFastBodiesDoNotTunnel()
    {
        using JBro::Physics2D::StepStats;
        const auto fire = [](World& world, bool round) {
            world.Settings().gravity = { 0, 0 };
            const BodyId bullet = AddBody(world, BodyType::Dynamic, { 0, 0 });
            if (round)
            {
                world.CreateCircleShape(bullet, MakeBall(0.05f), {});
            }
            else
            {
                AddPolygon(world, bullet, BoxOutline(0.05f, 0.05f));
            }
            world.SetLinearVelocity(bullet, { 200, 0 });
            std::uint32_t hits = 0;
            for (int i = 0; i < 30; ++i)
            {
                world.Step(Frame);
                hits += world.GetLastStepStats().continuousHits;
            }
            return std::make_pair(bullet, hits);
        };
        {
            World world;
            const BodyId wall = AddBody(world, BodyType::Static, { 5, 0 });
            AddPolygon(world, wall, BoxOutline(0.05f, 3.0f));
            const auto [ball, hits] = fire(world, true);
            Check(world.GetPosition(ball).x < 5.0f && hits > 0, "a fast ball stops at a thin wall instead of passing it");
        }
        {
            World world;
            const BodyId wall = AddBody(world, BodyType::Static, { 5, 0 });
            AddPolygon(world, wall, BoxOutline(0.05f, 3.0f));
            const auto [box, hits] = fire(world, false);
            Check(world.GetPosition(box).x < 5.0f && hits > 0, "and so does a fast box");
        }
        {
            World world;
            const BodyId line = AddBody(world, BodyType::Static, { 5, 0 });
            const Array<Vec2> points{ { 0, -3 }, { 0, 3 } };
            world.CreateChainShape(line, points.View(), false, {});
            const auto [ball, hits] = fire(world, true);
            Check(world.GetPosition(ball).x < 5.0f && hits > 0, "a chain segment stops it too");
        }
        {
            World world;
            ShapeDef slippery;
            slippery.friction = 0.0f;
            AddGround(world, slippery);
            const BodyId box = AddBody(world, BodyType::Dynamic, { -15, 0.5f });
            AddPolygon(world, box, BoxOutline(0.5f, 0.5f), slippery);
            Run(world, 0.5f);
            world.SetLinearVelocity(box, { 60, 0 });
            Run(world, 0.25f);
            Check(world.GetPosition(box).x > -2.0f, "a box sliding fast on the ground is not held back by the ground it touches");
        }
        {
            World world;
            world.Settings().gravity = { 0, 0 };
            ShapeDef oneWay;
            oneWay.oneWay = true;
            const BodyId ledge = AddBody(world, BodyType::Static, { 0, 5 });
            AddPolygon(world, ledge, BoxOutline(3.0f, 0.05f), oneWay);
            const BodyId ball = AddBody(world, BodyType::Dynamic, { 0, 0 });
            world.CreateCircleShape(ball, MakeBall(0.05f), {});
            world.SetLinearVelocity(ball, { 0, 200 });
            Run(world, 0.1f);
            Check(world.GetPosition(ball).y > 6.0f, "a fast ball from below goes through a one-way ledge");
        }
    }
}

int RunPhysics2DWorldTests()
{
    TestABoxFallsAndRestsOnTheGround();
    TestABoxInTheNotchOfAUStaysInside();
    TestAnLShapedBodyTurnsAboutItsCenterOfMass();
    TestAnOverlappingBodyIsPushedOutWithoutBouncing();
    TestAStackOfTenBoxesSettles();
    TestLinearDampingSlowsABody();
    TestTheSameSceneRunsTheSameTwice();
    TestFrictionHoldsOrLetsGoOnASlope();
    TestRestitutionBounces();
    TestContactEventsArePerShapePair();
    TestTriggersReportButDoNotPush();
    TestLayersFilterContacts();
    TestAKinematicBodyPushesADynamicOne();
    TestHandlesAndMassUpdates();
    TestCapsulesRestOnTheGround();
    TestReshapingKeepsTheContact();
    TestSteppingDoesNotAllocate();
    TestWorkersGiveTheSameResult();
    TestRecommendedWorkerCounts();
    TestParallelSteppingDoesNotAllocate();
    TestForcesAndImpulses();
    TestAxisLocks();
    TestBodyPropertiesChangeInPlace();
    TestALockedBodyBouncesWithItsRealMass();
    TestABoxSlidesAcrossAChainWithoutSnagging();
    TestBodiesFallAsleepAndWake();
    TestAStackSleeps();
    TestWhatWakesASleepingBody();
    TestOneWayPlatforms();
    TestTheLayerTableSeparatesLayers();
    TestDistanceJoints();
    TestHingeJoints();
    TestJointedBodiesSleepTogether();
    TestFastBodiesDoNotTunnel();
    TestStayEventsFollowTouchingPairs();
    std::cout << "Physics2D world tests passed.\n";
    return 0;
}
