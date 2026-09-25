#include <JBro/Physics2D/World.h>

#include <cmath>
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
    std::cout << "Physics2D world tests passed.\n";
    return 0;
}
