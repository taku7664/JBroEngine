#include "TestCases.h"
#include <JBro/Physics3D/World.h>

#include <chrono>
#include <cmath>
#include <thread>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 3D 물리 커널의 시험이다(D-414, physics3d-plan §3 의 1 단계). 캔버스 없이 커널만 돌린다 - 풀이는 Jolt 가 하므로
// Jolt 를 시험하는 것이 아니라 커널이 넘기는 값(도형·질량·층·축 고정·트리거)과 수명·할당이 맞는지를 장면으로 본다.
namespace
{
    using JBro::Vector3;
    using JBro::Quaternion;
    using JBro::Physics3D::BodyDef;
    using JBro::Physics3D::BodyId;
    using JBro::Physics3D::BodyType;
    using JBro::Physics3D::ShapeType;
    using JBro::Physics3D::World;

    constexpr JBro::Float Frame = 1.0f / 60.0f;

    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    JBro::Bool Near(JBro::Float value, JBro::Float expected, JBro::Float tolerance)
    {
        return std::fabs(value - expected) <= tolerance;
    }

    // 바닥(윗면 y=0)에 선 반 크기 0.5 상자의 높이인가. Jolt 는 쌓인 것이 떨지 않게 접촉을 겹침 여유(`PhysicsSettings::mPenetrationSlop`, 0.02)만큼
    // 겹친 채 둔다 - 그래서 0.5 가 아니라 0.48 언저리에 선다.
    JBro::Bool RestsOnGround(JBro::Float y)
    {
        return y > 0.47f && y < 0.505f;
    }

    void Run(World& world, JBro::Float seconds)
    {
        const JBro::Int32 steps = static_cast<JBro::Int32>(std::lround(seconds / Frame));
        for (JBro::Int32 step = 0; step < steps; ++step)
        {
            world.Step(Frame);
        }
    }

    // 윗면이 y=0 인 넓은 바닥이다.
    BodyId MakeGround(World& world, JBro::UInt32 layer = 0x00000001u, JBro::Bool trigger = false)
    {
        BodyDef ground;
        ground.type = BodyType::Static;
        ground.shape.type = ShapeType::Box;
        ground.shape.halfExtents = {20.0f, 0.5f, 20.0f};
        ground.position = {0.0f, -0.5f, 0.0f};
        ground.shape.layer = layer;
        ground.isTrigger = trigger;
        const BodyId id = world.CreateBody(ground);
        Check(world.IsValid(id), "the ground must be made");
        return id;
    }

    BodyDef BoxAt(Vector3 position)
    {
        BodyDef box;
        box.shape.type = ShapeType::Box;
        box.shape.halfExtents = {0.5f, 0.5f, 0.5f};
        box.position = position;
        return box;
    }

    Quaternion AboutZ(JBro::Float radians)
    {
        return {0.0f, 0.0f, std::sin(radians * 0.5f), std::cos(radians * 0.5f)};
    }

    void TestABoxFallsAndRestsOnTheGround()
    {
        World world;
        MakeGround(world);
        const BodyId box = world.CreateBody(BoxAt({0.0f, 3.0f, 0.0f}));
        Check(world.IsValid(box), "the box must be made");
        Check(world.GetBodyCount() == 2, "two bodies are in the world");
        world.Step(Frame);
        Check(world.GetPosition(box).y < 3.0f && world.GetLinearVelocity(box).y < 0.0f, "gravity pulls the box down");
        Run(world, 3.0f);
        const Vector3 at = world.GetPosition(box);
        std::cout << "  box rests at y=" << at.y << '\n';
        Check(RestsOnGround(at.y) && Near(at.x, 0.0f, 0.01f) && Near(at.z, 0.0f, 0.01f), "the box stands on the ground, its half size above it");
        Check(std::fabs(world.GetLinearVelocity(box).y) < 0.01f, "and has stopped");
        Check(false == world.IsAwake(box), "a box at rest falls asleep");
    }

    void TestASphereRollsDownASlope()
    {
        World world;
        // 20 도 기운 판이다. Z 축 둘레로 -20 도 돌리면 윗면이 +x 쪽으로 내려간다.
        BodyDef slope;
        slope.type = BodyType::Static;
        slope.shape.halfExtents = {10.0f, 0.25f, 3.0f};
        slope.rotation = AboutZ(-0.349066f);
        world.CreateBody(slope);
        BodyDef ball;
        ball.shape.type = ShapeType::Sphere;
        ball.shape.radius = 0.5f;
        ball.position = {-3.0f, 2.0f, 0.0f};
        const BodyId sphere = world.CreateBody(ball);
        Check(world.IsValid(sphere), "the sphere must be made");
        Run(world, 1.5f);
        const Vector3 at = world.GetPosition(sphere);
        const Vector3 spin = world.GetAngularVelocity(sphere);
        std::cout << "  sphere at x=" << at.x << " spinning " << spin.z << '\n';
        Check(at.x > -1.0f, "the sphere goes down the slope towards +x");
        Check(spin.z < -1.0f, "and rolls - it turns about -z as it moves to +x, it does not slide");
    }

    void TestLayersAndTriggersLetBodiesThrough()
    {
        World world;
        MakeGround(world, 0x00000002u);
        BodyDef hits = BoxAt({-2.0f, 2.0f, 0.0f});
        const BodyId stands = world.CreateBody(hits);
        BodyDef skips = BoxAt({2.0f, 2.0f, 0.0f});
        // 바닥의 층(비트 1)을 마스크에서 뺐다.
        skips.shape.mask = 0xFFFFFFFDu;
        const BodyId falls = world.CreateBody(skips);
        Run(world, 2.0f);
        Check(RestsOnGround(world.GetPosition(stands).y), "a box whose mask has the ground's layer rests on it");
        Check(world.GetPosition(falls).y < -5.0f, "a box whose mask leaves the ground's layer out falls through");

        World triggered;
        MakeGround(triggered, 0x00000001u, true);
        const BodyId through = triggered.CreateBody(BoxAt({0.0f, 2.0f, 0.0f}));
        Run(triggered, 2.0f);
        Check(triggered.GetPosition(through).y < -5.0f, "a trigger ground does not hold anything up");
    }

    void TestMassGravityAndFrozenAxes()
    {
        World world;
        BodyDef floating = BoxAt({0.0f, 5.0f, 0.0f});
        floating.gravityScale = 0.0f;
        const BodyId still = world.CreateBody(floating);
        BodyDef rail = BoxAt({0.0f, 5.0f, 3.0f});
        rail.gravityScale = 0.0f;
        rail.linearVelocity = {4.0f, 0.0f, 2.0f};
        rail.freezePositionX = true;
        const BodyId onRail = world.CreateBody(rail);
        Run(world, 1.0f);
        Check(Near(world.GetPosition(still).y, 5.0f, 1.0e-4f), "a body with no gravity scale does not fall");
        Check(Near(world.GetPosition(onRail).x, 0.0f, 1.0e-4f) && world.GetPosition(onRail).z > 4.5f,
            "a body frozen on x keeps its x and still moves on z");

        // 무거운 상자가 가벼운 상자를 민다. 질량이 바디까지 갔는지는 부딪힌 뒤 속도로 본다.
        World pushing;
        pushing.SetGravity({0.0f, 0.0f, 0.0f});
        Check(Near(pushing.GetGravity().y, 0.0f, 1.0e-6f), "the world takes a new gravity");
        BodyDef heavy = BoxAt({-2.0f, 0.0f, 0.0f});
        heavy.mass = 100.0f;
        heavy.linearVelocity = {3.0f, 0.0f, 0.0f};
        BodyDef light = BoxAt({0.0f, 0.0f, 0.0f});
        light.mass = 1.0f;
        const BodyId heavyId = pushing.CreateBody(heavy);
        const BodyId lightId = pushing.CreateBody(light);
        Run(pushing, 1.0f);
        std::cout << "  after the push heavy=" << pushing.GetLinearVelocity(heavyId).x << " light=" << pushing.GetLinearVelocity(lightId).x << '\n';
        Check(pushing.GetLinearVelocity(heavyId).x > 2.5f, "the heavy box barely slows down");
        Check(pushing.GetLinearVelocity(lightId).x > 2.9f, "and carries the light one along");

        // 모든 축을 막은 동적 바디는 Jolt 가 받지 않는다 - 커널이 움직이지 않는 바디로 받는다.
        World frozen;
        BodyDef locked = BoxAt({0.0f, 2.0f, 0.0f});
        locked.freezePositionX = locked.freezePositionY = locked.freezePositionZ = true;
        locked.freezeRotationX = locked.freezeRotationY = locked.freezeRotationZ = true;
        const BodyId lockedId = frozen.CreateBody(locked);
        Check(frozen.IsValid(lockedId), "a fully frozen body is still made");
        Run(frozen, 0.5f);
        Check(Near(frozen.GetPosition(lockedId).y, 2.0f, 1.0e-4f), "and does not move");
    }

    void TestShapesIdsAndLifetime()
    {
        World world;
        BodyDef flat = BoxAt({});
        flat.shape.halfExtents = {1.0f, 0.0f, 1.0f};
        Check(false == world.IsValid(world.CreateBody(flat)), "a box with no height is refused");
        BodyDef noBall;
        noBall.shape.type = ShapeType::Sphere;
        noBall.shape.radius = 0.0f;
        Check(false == world.IsValid(world.CreateBody(noBall)), "a sphere with no radius is refused");
        Check(false == world.IsValid(BodyId{}), "the empty id is not a body");

        // 얇은 판(반 높이 0.01)은 둥근 모서리보다 얇아도 받는다.
        BodyDef thin = BoxAt({});
        thin.shape.halfExtents = {1.0f, 0.01f, 1.0f};
        Check(world.IsValid(world.CreateBody(thin)), "a thin plate is made");

        BodyDef capsule;
        capsule.shape.type = ShapeType::Capsule;
        capsule.shape.radius = 0.3f;
        capsule.shape.halfHeight = 0.6f;
        capsule.shape.offset = {0.0f, 1.0f, 0.0f};
        capsule.userData = 0xABCDEF0123ull;
        const BodyId first = world.CreateBody(capsule);
        Check(world.IsValid(first) && world.GetUserData(first) == 0xABCDEF0123ull, "a capsule keeps its user data");
        world.DestroyBody(first);
        Check(false == world.IsValid(first), "a destroyed body's id is no longer valid");
        const BodyId second = world.CreateBody(capsule);
        Check(world.IsValid(second) && false == world.IsValid(first), "a new body in the same slot does not revive the old id");
        Check(world.GetPosition(first).y == 0.0f, "reading a dead id gives zero instead of the new body's state");

        world.SetTransform(second, {5.0f, 6.0f, 7.0f}, AboutZ(0.5f));
        Check(Near(world.GetPosition(second).x, 5.0f, 1.0e-4f) && Near(world.GetRotation(second).z, std::sin(0.25f), 1.0e-4f),
            "a body can be moved and turned in place");
        world.SetLinearVelocity(second, {1.0f, 2.0f, 3.0f});
        world.SetAngularVelocity(second, {0.0f, 1.0f, 0.0f});
        Check(Near(world.GetLinearVelocity(second).z, 3.0f, 1.0e-4f) && Near(world.GetAngularVelocity(second).y, 1.0f, 1.0e-4f),
            "and given a velocity");

        // 도형이 원점에서 비켜 앉으면 바닥에 닿는 것은 도형이다 - 원점은 그만큼 아래에 선다.
        {
            World offsetWorld;
            MakeGround(offsetWorld);
            BodyDef raised = BoxAt({0.0f, 3.0f, 0.0f});
            raised.shape.offset = {0.0f, 1.0f, 0.0f};
            raised.freezeRotationX = raised.freezeRotationY = raised.freezeRotationZ = true;
            const BodyId body = offsetWorld.CreateBody(raised);
            Run(offsetWorld, 3.0f);
            Check(RestsOnGround(offsetWorld.GetPosition(body).y + 1.0f), "a shape set one unit above its origin rests with the origin one unit lower");
        }

        // 월드를 내리고 다시 세워도 Jolt 의 전역 준비가 다시 선다.
        {
            World again;
            MakeGround(again);
            const BodyId box = again.CreateBody(BoxAt({0.0f, 1.0f, 0.0f}));
            Run(again, 1.0f);
            Check(RestsOnGround(again.GetPosition(box).y), "a second world runs while the first is alive");
        }
    }

    void TestSteppingDoesNotAllocate()
    {
        World world;
        MakeGround(world);
        for (JBro::Int32 index = 0; index < 20; ++index)
        {
            const JBro::Float x = static_cast<JBro::Float>(index % 5) * 1.2f - 2.4f;
            const JBro::Float y = 1.0f + static_cast<JBro::Float>(index / 5) * 1.1f;
            BodyDef box = BoxAt({x, y, 0.0f});
            // 잠들면 Jolt 가 아무것도 하지 않아 0 이 저절로 나온다. 깨어 있는 채로 접촉을 풀게 한다.
            box.canSleep = false;
            world.CreateBody(box);
        }
        // 상자들이 떨어져 자리를 잡을 때까지 둔다.
        Run(world, 3.0f);
        const JBro::Physics3D::AllocationStats before = JBro::Physics3D::GetAllocationStats();
        Check(before.allocations > 0, "the kernel hands Jolt its allocation hooks");
        Run(world, 2.0f);
        const JBro::Physics3D::AllocationStats after = JBro::Physics3D::GetAllocationStats();
        std::cout << "  during 120 steps: " << (after.allocations - before.allocations) << " allocations through the hooks, "
                  << (after.systemAllocations - before.systemAllocations) << " from the heap" << '\n';
        // Jolt 는 깨어 있는 몸이 움직이면 브로드페이즈 트리를 다시 지으며 스텝마다 받았다 놓는다 - 그것을 풀이 받아 준다.
        Check(after.allocations > before.allocations, "a pile of awake boxes still makes Jolt ask for memory every step");
        Check(after.systemAllocations == before.systemAllocations, "but the pool hands back what it freed, so stepping takes nothing from the heap");
    }

    // 한 바디에 도형 여럿(한 오브젝트의 콜라이더 여럿)과 도형마다의 마찰(D-416).
    void TestCompoundShapesAndSurfaces()
    {
        // 아령: 반지름 0.5 의 구 둘을 x 로 ±1 에 둔 바디 하나. 바닥에 누우면 원점이 구의 반지름 높이에 선다.
        World world;
        MakeGround(world);
        JBro::Physics3D::ShapeDesc balls[2];
        balls[0].type = ShapeType::Sphere;
        balls[0].offset = {-1.0f, 0.0f, 0.0f};
        balls[1].type = ShapeType::Sphere;
        balls[1].offset = {1.0f, 0.0f, 0.0f};
        BodyDef dumbbell;
        dumbbell.position = {0.0f, 2.0f, 0.0f};
        dumbbell.shapes = JBro::ArrayView<const JBro::Physics3D::ShapeDesc>(balls);
        const BodyId bell = world.CreateBody(dumbbell);
        Check(world.IsValid(bell), "a body made of two shapes is made");
        Run(world, 3.0f);
        const Vector3 at = world.GetPosition(bell);
        std::cout << "  the dumbbell rests at y=" << at.y << '\n';
        Check(at.y > 0.45f && at.y < 0.51f, "both balls hold it up - its origin rests one radius above the ground");

        // 25 도 경사 위의 두 상자: 마찰 0 인 쪽은 미끄러지고 마찰 1 인 쪽은 선다. 바디의 마찰이 아니라 도형의 마찰이 접촉에 간다.
        World slope;
        BodyDef plane;
        plane.type = BodyType::Static;
        plane.shape.halfExtents = {10.0f, 0.25f, 4.0f};
        plane.shape.friction = 1.0f;
        plane.rotation = AboutZ(-0.436332f);
        slope.CreateBody(plane);
        BodyDef icy = BoxAt({-2.0f, 1.5f, -2.0f});
        icy.rotation = AboutZ(-0.436332f);
        icy.shape.friction = 0.0f;
        BodyDef grippy = BoxAt({-2.0f, 1.5f, 2.0f});
        grippy.rotation = AboutZ(-0.436332f);
        grippy.shape.friction = 1.0f;
        const BodyId slides = slope.CreateBody(icy);
        const BodyId holds = slope.CreateBody(grippy);
        Run(slope, 1.5f);
        std::cout << "  on the slope: icy x=" << slope.GetPosition(slides).x << " grippy x=" << slope.GetPosition(holds).x << '\n';
        Check(slope.GetPosition(slides).x > 0.5f, "a box with no friction slides down");
        Check(slope.GetPosition(holds).x < -1.5f, "a box with full friction stays where it landed");

        // 바디의 마찰은 첫 도형의 것이지만 접촉은 **닿은 도형**의 마찰로 섞는다. 첫 도형(마찰 0)은 위로 떨어져 닿지 않는 작은 구이고,
        // 경사에 닿는 것은 둘째 도형(마찰 1)인 상자다 - 바디의 값으로 섞으면 미끄러진다.
        JBro::Physics3D::ShapeDesc parts[2];
        parts[0].type = ShapeType::Sphere;
        parts[0].radius = 0.1f;
        parts[0].offset = {0.0f, 2.0f, 0.0f};
        parts[0].friction = 0.0f;
        parts[1].halfExtents = {0.5f, 0.5f, 0.5f};
        parts[1].friction = 1.0f;
        BodyDef mixed = BoxAt({-2.0f, 1.5f, 0.0f});
        mixed.rotation = AboutZ(-0.436332f);
        mixed.shapes = JBro::ArrayView<const JBro::Physics3D::ShapeDesc>(parts);
        mixed.freezeRotationX = mixed.freezeRotationY = mixed.freezeRotationZ = true;
        const BodyId grips = slope.CreateBody(mixed);
        Run(slope, 1.5f);
        Check(slope.GetPosition(grips).x < -1.5f, "the touching shape's friction holds, not the body's first shape");
    }

    void TestShapesAndPropertiesChangeInPlace()
    {
        World world;
        MakeGround(world);
        const BodyId box = world.CreateBody(BoxAt({0.0f, 1.0f, 0.0f}));
        Run(world, 2.0f);
        Check(RestsOnGround(world.GetPosition(box).y), "the box rests first");
        // 크기를 키운다: 같은 바디가 큰 상자로 선다.
        JBro::Physics3D::ShapeDesc bigger;
        bigger.halfExtents = {1.0f, 1.0f, 1.0f};
        Check(world.SetShapes(box, JBro::ArrayView<const JBro::Physics3D::ShapeDesc>(&bigger, 1)), "the box takes a new shape");
        Run(world, 2.0f);
        Check(world.IsValid(box) && RestsOnGround(world.GetPosition(box).y - 0.5f), "and stands on it - its origin rises to the new half size");
        JBro::Physics3D::ShapeDesc nothing;
        nothing.halfExtents = {0.0f, 1.0f, 1.0f};
        Check(false == world.SetShapes(box, JBro::ArrayView<const JBro::Physics3D::ShapeDesc>(&nothing, 1)), "a bad shape is refused and the old one stays");

        // 성질을 제자리에서: 중력 배율 0 이면 떠 있는 채로, 마스크에서 바닥을 빼면 떨어진다.
        World props;
        MakeGround(props);
        BodyDef def = BoxAt({0.0f, 3.0f, 0.0f});
        const BodyId body = props.CreateBody(def);
        def.gravityScale = 0.0f;
        props.SetBodyProperties(body, def);
        Run(props, 1.0f);
        Check(Near(props.GetPosition(body).y, 3.0f, 1.0e-3f), "a body whose gravity scale became 0 stays up");
        def.gravityScale = 1.0f;
        props.SetBodyProperties(body, def);
        Run(props, 2.0f);
        Check(RestsOnGround(props.GetPosition(body).y), "gravity back on, it falls and rests");
        // 레이어는 도형에 있다(D-421) - 마스크를 바꾸려면 도형을 바꾼다.
        def.shape.mask = 0xFFFFFFFEu;
        Check(props.SetShapes(body, JBro::ArrayView<const JBro::Physics3D::ShapeDesc>(&def.shape, 1)), "the body takes its shape with a new mask");
        props.SetTransform(body, {0.0f, 1.0f, 0.0f}, {});
        Run(props, 2.0f);
        Check(props.GetPosition(body).y < -5.0f, "leaving the ground's layer out of its mask lets it fall through");

        // 질량을 바꾸면 미는 힘이 달라진다: 가벼워진 상자는 같은 속도의 상자에 더 밀린다.
        World mass;
        mass.SetGravity({0.0f, 0.0f, 0.0f});
        BodyDef mover = BoxAt({-2.0f, 0.0f, 0.0f});
        mover.linearVelocity = {3.0f, 0.0f, 0.0f};
        BodyDef target = BoxAt({0.0f, 0.0f, 0.0f});
        const BodyId moving = mass.CreateBody(mover);
        const BodyId hit = mass.CreateBody(target);
        target.mass = 100.0f;
        mass.SetBodyProperties(hit, target);
        Run(mass, 1.0f);
        Check(mass.GetLinearVelocity(hit).x < 0.2f && mass.GetLinearVelocity(moving).x < 0.5f,
            "a target made a hundred times heavier in place barely moves and stops the mover");
    }

    // 접촉 사건(D-417): 바디 쌍마다 시작 한 번, 닿아 있는 동안 스텝마다 유지, 떨어지면 끝. 트리거도 같은 길이고, 지운 바디의 접촉도 끝으로 나온다.
    void TestContactEventsFollowTouchingPairs()
    {
        World world;
        const BodyId ground = MakeGround(world);
        BodyDef def = BoxAt({0.0f, 1.0f, 0.0f});
        def.userData = 77;
        const BodyId box = world.CreateBody(def);
        JBro::Int32 begins = 0;
        JBro::Int32 stays = 0;
        JBro::Bool firstSeen = false;
        for (JBro::Int32 step = 0; step < 90; ++step)
        {
            world.Step(Frame);
            for (const JBro::Physics3D::ContactEvent& event : world.GetBeginEvents())
            {
                ++begins;
                const JBro::Bool boxIsA = event.bodyA.value == box.value;
                const JBro::UInt64 boxData = boxIsA ? event.userDataA : event.userDataB;
                // 법선은 A 에서 B 로 간다. 상자가 A 면 아래(바닥 쪽)를 가리킨다.
                const JBro::Float down = boxIsA ? event.normal.y.Get() : -event.normal.y.Get();
                Check(boxData == 77 && down < -0.9f && false == event.isTrigger, "the begin carries the box, a normal from it to the ground and no trigger");
                Check(std::fabs(event.point.y) < 0.05f, "and a contact point on the ground's top face");
                firstSeen = true;
            }
            for (const JBro::Physics3D::ContactEvent& event : world.GetStayEvents())
            {
                ++stays;
                // 유지에서도 법선은 사건의 A 에서 B 로 간다 - Jolt 가 콜백에서 두 바디를 어느 차례로 주든.
                const JBro::Float down = event.bodyA.value == box.value ? event.normal.y.Get() : -event.normal.y.Get();
                Check(down < -0.9f, "a stay's normal also points from A to B");
            }
            Check(world.GetEndEvents().Size() == 0, "nothing ends while the box lies on the ground");
        }
        std::cout << "  begins " << begins << ", stays " << stays << '\n';
        Check(firstSeen && begins == 1, "a box landing on the ground begins one contact, not one per contact point");
        // 잠든 뒤로는 유지가 나오지 않는다(2D 와 같다 - 잠든 더미가 스텝마다 알리지 않는다). Jolt 는 잠들 때 접촉을 "제거" 로 알리지만 끝이 아니다.
        Check(stays > 20 && false == world.IsAwake(box), "it keeps the contact while it settles, then falls asleep without ending it");
        (void)ground;

        // 잠든 상자를 들어 올리면(깨워 옮긴다) 끝난다.
        world.SetTransform(box, {0.0f, 5.0f, 0.0f}, {});
        world.Step(Frame);
        Check(world.GetEndEvents().Size() == 1, "lifting the box away ends the contact");

        // 다시 떨어뜨려 닿게 한 뒤 지우면, 다음 스텝에 끝이 나온다 - userData 가 실려 있다.
        Run(world, 2.0f);
        world.DestroyBody(box);
        world.Step(Frame);
        Check(world.GetEndEvents().Size() == 1 && (world.GetEndEvents()[0].userDataA == 77 || world.GetEndEvents()[0].userDataB == 77),
            "destroying a body that touches ends its contact with its user data");

        // 트리거를 지나가는 상자: 시작과 끝이 트리거로 나오고, 상자는 지나간다.
        World triggers;
        BodyDef net;
        net.type = BodyType::Static;
        net.shape.halfExtents = {2.0f, 0.1f, 2.0f};
        net.isTrigger = true;
        triggers.CreateBody(net);
        const BodyId diver = triggers.CreateBody(BoxAt({0.0f, 2.0f, 0.0f}));
        JBro::Int32 triggerBegins = 0;
        JBro::Int32 triggerEnds = 0;
        for (JBro::Int32 step = 0; step < 90; ++step)
        {
            triggers.Step(Frame);
            for (const JBro::Physics3D::ContactEvent& event : triggers.GetBeginEvents())
            {
                triggerBegins += event.isTrigger ? 1 : 0;
            }
            for (const JBro::Physics3D::ContactEvent& event : triggers.GetEndEvents())
            {
                triggerEnds += event.isTrigger ? 1 : 0;
            }
        }
        Check(triggerBegins == 1 && triggerEnds == 1 && triggers.GetPosition(diver).y < -1.0f,
            "a box falling through a trigger begins and ends one trigger contact and keeps falling");

        // 키네마틱 트리거와 정적 바닥: 움직이는 오브젝트의 트리거(`triggerSeesStatic`)만 바닥과의 겹침을 알린다.
        const auto touchesGround = [](JBro::Bool seesStatic) {
            World still;
            MakeGround(still);
            BodyDef sensor = BoxAt({0.0f, 0.4f, 0.0f});
            sensor.type = BodyType::Kinematic;
            sensor.isTrigger = true;
            sensor.triggerSeesStatic = seesStatic;
            still.CreateBody(sensor);
            JBro::Int32 begun = 0;
            for (JBro::Int32 step = 0; step < 5; ++step)
            {
                still.Step(Frame);
                begun += static_cast<JBro::Int32>(still.GetBeginEvents().Size());
            }
            return begun;
        };
        Check(touchesGround(true) == 1, "a moving object's trigger hears the static ground it overlaps");
        Check(touchesGround(false) == 0, "a still trigger does not - two things that never move do not meet, as in 2D");
    }

    // 질의(D-417): 반직선·모든 반직선·구와 상자 겹침·구와 상자 밀기. 레이어 마스크로 거르고, 결과는 맞은 바디의 userData 다.
    void TestQueriesFindBodies()
    {
        World world;
        BodyDef floor;
        floor.type = BodyType::Static;
        floor.shape.halfExtents = {20.0f, 0.5f, 20.0f};
        floor.position = {0.0f, -0.5f, 0.0f};
        floor.userData = 1;
        world.CreateBody(floor);
        // 윗면이 y=1 인 정적 상자. 레이어는 비트 2.
        BodyDef block;
        block.type = BodyType::Static;
        block.shape.halfExtents = {0.5f, 0.5f, 0.5f};
        block.position = {3.0f, 0.5f, 0.0f};
        block.shape.layer = 0x4u;
        block.userData = 2;
        world.CreateBody(block);
        const JBro::UInt32 all = 0xFFFFFFFFu;

        JBro::Physics3D::QueryHit hit;
        Check(world.Raycast({3.0f, 5.0f, 0.0f}, {0.0f, -2.0f, 0.0f}, 10.0f, all, hit), "a ray down onto the block hits");
        Check(hit.userData == 2 && Near(hit.distance, 4.0f, 1.0e-3f) && Near(hit.point.y, 1.0f, 1.0e-3f) && Near(hit.normal.y, 1.0f, 1.0e-3f),
            "the block, four units down, on its top face facing up - the direction need not be normalized");
        Check(world.Raycast({3.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f, ~0x4u, hit) && hit.userData == 1 && Near(hit.distance, 5.0f, 1.0e-3f),
            "masking the block's layer out lets the ray reach the floor");
        Check(false == world.Raycast({3.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 3.0f, all, hit) && hit.userData == 0,
            "a ray too short misses and clears the hit");
        Check(false == world.Raycast({3.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 10.0f, all, hit), "a ray with no direction misses");

        JBro::Array<JBro::Physics3D::QueryHit> hits;
        world.RaycastAll({3.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 10.0f, all, hits);
        Check(hits.Size() == 2 && hits[0].userData == 2 && hits[1].userData == 1, "a ray through everything finds the block, then the floor");

        JBro::Array<JBro::UInt64> found;
        world.OverlapSphere({3.0f, 0.5f, 0.0f}, 0.2f, all, found);
        Check(found.Size() == 1 && found[0] == 2, "a small sphere inside the block overlaps only the block");
        world.OverlapBox({2.0f, 0.5f, 0.0f}, {2.0f, 1.0f, 1.0f}, {}, all, found);
        Check(found.Size() == 2, "a box over both overlaps the floor and the block, once each");
        world.OverlapBox({2.0f, 0.5f, 0.0f}, {2.0f, 1.0f, 1.0f}, {}, 0x4u, found);
        Check(found.Size() == 1 && found[0] == 2, "and only the block when the mask asks for its layer");

        Check(world.SphereCast({3.0f, 5.0f, 0.0f}, 0.5f, {0.0f, -1.0f, 0.0f}, 10.0f, all, hit), "a sphere pushed down hits");
        Check(hit.userData == 2 && Near(hit.distance, 3.5f, 1.0e-2f) && Near(hit.normal.y, 1.0f, 1.0e-2f) && Near(hit.point.y, 1.0f, 1.0e-2f),
            "it stops one radius above the block's top, touching it there");
        Check(world.BoxCast({3.0f, 5.0f, 0.0f}, {0.25f, 0.25f, 0.25f}, {}, {0.0f, -1.0f, 0.0f}, 10.0f, all, hit),
            "a box pushed down hits");
        Check(hit.userData == 2 && Near(hit.distance, 3.75f, 1.0e-2f), "it stops its half size above the block's top");
    }
}

namespace
{
    // 메시와 볼록 껍질(D-418): 두 삼각형짜리 바닥 메시 위에 껍질 상자가 선다. 메시는 움직이는 바디가 받지 않는다.
    void TestMeshesAndHulls()
    {
        World world;
        const Vector3 corners[4] = {{-10.0f, 0.0f, -10.0f}, {10.0f, 0.0f, -10.0f}, {10.0f, 0.0f, 10.0f}, {-10.0f, 0.0f, 10.0f}};
        // 위(+y)를 보는 면이 되게 감는다.
        const JBro::UInt32 triangles[6] = {0, 2, 1, 0, 3, 2};
        BodyDef floor;
        floor.type = BodyType::Static;
        floor.shape.type = ShapeType::Mesh;
        floor.shape.points = JBro::ArrayView<const Vector3>(corners);
        floor.shape.indices = JBro::ArrayView<const JBro::UInt32>(triangles);
        Check(world.IsValid(world.CreateBody(floor)), "a static body takes a triangle mesh");

        Vector3 cube[8];
        for (JBro::Int32 index = 0; index < 8; ++index)
        {
            cube[index] = {(index & 1) != 0 ? 0.5f : -0.5f, (index & 2) != 0 ? 0.5f : -0.5f, (index & 4) != 0 ? 0.5f : -0.5f};
        }
        BodyDef hull = BoxAt({0.0f, 2.0f, 0.0f});
        hull.shape.type = ShapeType::ConvexHull;
        hull.shape.points = JBro::ArrayView<const Vector3>(cube);
        const BodyId box = world.CreateBody(hull);
        Check(world.IsValid(box), "a moving body takes a convex hull");
        Run(world, 3.0f);
        std::cout << "  hull box on the mesh floor rests at y=" << world.GetPosition(box).y << '\n';
        Check(RestsOnGround(world.GetPosition(box).y), "the hull of a unit cube rests on the mesh floor like a box");

        BodyDef movingMesh = BoxAt({0.0f, 5.0f, 0.0f});
        movingMesh.shape = floor.shape;
        Check(false == world.IsValid(world.CreateBody(movingMesh)), "a moving body does not take a triangle mesh");
        JBro::Physics3D::ShapeDesc meshDesc = floor.shape;
        Check(false == world.SetShapes(box, JBro::ArrayView<const JBro::Physics3D::ShapeDesc>(&meshDesc, 1)), "nor can a moving body be given one later");
        JBro::Physics3D::ShapeDesc few;
        few.type = ShapeType::ConvexHull;
        few.points = JBro::ArrayView<const Vector3>(corners, 3);
        Check(false == world.SetShapes(box, JBro::ArrayView<const JBro::Physics3D::ShapeDesc>(&few, 1)), "a hull of fewer than four points is refused");
    }
}

namespace
{
    // **워커**(D-419, physics3d-plan 5 단계). 상자 1000 개가 쌓여 부딪히는 장면을 워커 0 과 넷으로 돌려, 워커가 있으면 스텝이 빨라지는지 잰다.
    // 같은 장면이 같은 결과를 내는지도 본다(Jolt 는 워커 수와 무관하게 같은 차례로 푼다).
    JBro::Float TimePile(JBro::UInt32 workers, JBro::Float& settledHeight)
    {
        World world;
        world.SetWorkerCount(workers);
        Check(world.GetWorkerCount() == workers, "the world takes the worker count");
        MakeGround(world);
        BodyId top;
        for (JBro::Int32 index = 0; index < 1000; ++index)
        {
            const JBro::Float x = static_cast<JBro::Float>(index % 10) * 1.05f - 4.7f;
            const JBro::Float z = static_cast<JBro::Float>((index / 10) % 10) * 1.05f - 4.7f;
            const JBro::Float y = 0.5f + static_cast<JBro::Float>(index / 100) * 1.05f;
            BodyDef box = BoxAt({x, y, z});
            box.canSleep = false;
            top = world.CreateBody(box);
        }
        // 처음 몇 스텝은 워커가 일어나고 접촉 표가 자리를 잡는다.
        Run(world, 0.25f);
        const auto start = std::chrono::steady_clock::now();
        Run(world, 1.0f);
        const auto end = std::chrono::steady_clock::now();
        settledHeight = world.GetPosition(top).y;
        return static_cast<JBro::Float>(std::chrono::duration<double, std::milli>(end - start).count());
    }

    void TestWorkersSpeedUpALargePile()
    {
        Check(JBro::Physics3D::RecommendWorkerCount(100, 16) == 0, "a small canvas runs on the main thread alone");
        Check(JBro::Physics3D::RecommendWorkerCount(1000, 16) == 3, "a thousand colliders get three workers");
        Check(JBro::Physics3D::RecommendWorkerCount(100000, 16) == 15 && JBro::Physics3D::RecommendWorkerCount(100000, 64) == 16,
            "never more than the spare cores or the cap");
        Check(JBro::Physics3D::RecommendWorkerCount(100000, 1) == 0, "a single core has no spare");
        const JBro::UInt32 cores = std::thread::hardware_concurrency();
        if (cores < 4)
        {
            std::cout << "  [skip] fewer than four cores; the worker speed-up not measured" << '\n';
            return;
        }
        JBro::Float singleTop = 0.0f;
        JBro::Float pooledTop = 0.0f;
        const JBro::Float single = TimePile(0, singleTop);
        const JBro::Float pooled = TimePile(4, pooledTop);
        std::cout << "  1000 boxes, 60 steps: main thread " << single << " ms, four workers " << pooled << " ms" << '\n';
        Check(pooled < single * 0.8f, "four workers step a thousand touching boxes noticeably faster than the main thread alone");
        Check(std::fabs(singleTop - pooledTop) < 0.05f, "and the pile ends up the same height");
    }
}

namespace
{
    // **힘과 충격량**(D-420). 중력 없는 월드에서: 힘은 한 스텝 동안만(F/m·dt), 충격량은 바로(J/m), 각 충격량과 토크는 돌린다. 정적인 몸은 받지 않는다.
    void TestForcesAndImpulses()
    {
        World world;
        world.SetGravity({0.0f, 0.0f, 0.0f});
        BodyDef def = BoxAt({0.0f, 0.0f, 0.0f});
        def.mass = 2.0f;
        const BodyId pushed = world.CreateBody(def);
        world.ApplyForce(pushed, {6.0f, 0.0f, 0.0f});
        world.Step(Frame);
        Check(Near(world.GetLinearVelocity(pushed).x, 6.0f / 2.0f * Frame, 1.0e-4f), "a force acts for one step: F/m times the step");
        world.Step(Frame);
        Check(Near(world.GetLinearVelocity(pushed).x, 6.0f / 2.0f * Frame, 1.0e-4f), "and is gone the next step");
        world.ApplyImpulse(pushed, {0.0f, 4.0f, 0.0f});
        Check(Near(world.GetLinearVelocity(pushed).y, 2.0f, 1.0e-4f), "an impulse changes the velocity at once: J/m");
        world.ApplyAngularImpulse(pushed, {0.0f, 1.0f, 0.0f});
        Check(world.GetAngularVelocity(pushed).y > 0.5f, "an angular impulse turns the body about its axis");
        const BodyId twisted = world.CreateBody(BoxAt({5.0f, 0.0f, 0.0f}));
        world.ApplyTorque(twisted, {0.0f, 0.0f, 3.0f});
        world.Step(Frame);
        Check(world.GetAngularVelocity(twisted).z > 0.0f, "a torque spins it up for a step");

        BodyDef wall;
        wall.type = BodyType::Static;
        const BodyId still = world.CreateBody(wall);
        world.ApplyImpulse(still, {10.0f, 0.0f, 0.0f});
        world.Step(Frame);
        Check(Near(world.GetPosition(still).x, 0.0f, 1.0e-6f), "a static body ignores forces");

        // 질량 중심은 도형이 비켜 앉은 쪽이다.
        BodyDef offset = BoxAt({0.0f, 10.0f, 0.0f});
        offset.shape.offset = {2.0f, 0.0f, 0.0f};
        const BodyId lopsided = world.CreateBody(offset);
        Check(Near(world.GetCenterOfMass(lopsided).x, 2.0f, 1.0e-4f), "the centre of mass sits on the offset shape");

        world.SetAwake(pushed, false);
        Check(false == world.IsAwake(pushed), "a body can be put to sleep");
        world.SetAwake(pushed, true);
        Check(world.IsAwake(pushed), "and woken");
    }
}

namespace
{
    // **도형마다의 레이어**(D-421). 한 바디의 두 도형이 저마다 거른다: 아래 상자는 바닥의 레이어를 마스크에서 뺐고 위 상자는 바닥과 만난다 -
    // 바디는 위 상자로 바닥에 선다(아래 상자는 바닥을 지난다). 첫 도형의 값만 쓰면 바디 전체가 바닥을 지난다.
    // 레이어 충돌 표가 떼어 둔 두 레이어도 지나간다. 질의는 맞은 도형의 레이어로 거른다.
    void TestEachShapeHasItsOwnLayer()
    {
        World world;
        MakeGround(world, 0x2u);
        JBro::Physics3D::ShapeDesc parts[2];
        parts[0].halfExtents = {0.5f, 0.5f, 0.5f};
        parts[0].offset = {0.0f, -1.0f, 0.0f};
        parts[0].mask = ~JBro::UInt32(0x2u);
        parts[0].layer = 0x8u;
        parts[1].halfExtents = {0.5f, 0.5f, 0.5f};
        parts[1].layer = 0x1u;
        BodyDef stack = BoxAt({0.0f, 3.0f, 0.0f});
        stack.shapes = JBro::ArrayView<const JBro::Physics3D::ShapeDesc>(parts);
        stack.freezeRotationX = stack.freezeRotationY = stack.freezeRotationZ = true;
        const BodyId body = world.CreateBody(stack);
        Run(world, 3.0f);
        std::cout << "  two-layer body rests at y=" << world.GetPosition(body).y << '\n';
        Check(RestsOnGround(world.GetPosition(body).y), "the upper shape stands on the ground while the lower one, masked off, sinks through it");

        // 질의: 위 도형의 레이어(비트 0)만 묻는 반직선은 아래 도형을 지나 위 도형에 맞는다.
        JBro::Physics3D::QueryHit hit;
        Check(world.Raycast({3.0f, world.GetPosition(body).y, 0.0f}, {-1.0f, 0.0f, 0.0f}, 10.0f, 0x1u, hit) && Near(hit.point.x, 0.5f, 1.0e-2f),
            "a ray asking for the upper shape's layer hits the upper shape");
        Check(false == world.Raycast({3.0f, world.GetPosition(body).y - 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, 10.0f, 0x1u, hit),
            "and passes the lower shape, which is on another layer of the same body");
        Check(world.Raycast({3.0f, world.GetPosition(body).y - 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, 10.0f, 0x8u, hit),
            "asking for the lower shape's layer finds it");

        // 레이어 충돌 표: 레이어 3 과 4 를 떼어 두면 레이어 3 상자가 레이어 4 바닥을 지난다.
        World table;
        MakeGround(table, 0x10u);
        BodyDef box = BoxAt({0.0f, 2.0f, 0.0f});
        box.shape.layer = 0x8u;
        const BodyId falls = table.CreateBody(box);
        JBro::UInt32 rows[32] = {};
        rows[3] = 0x10u;
        rows[4] = 0x8u;
        table.SetIgnoredLayers(rows);
        Run(table, 2.0f);
        Check(table.GetPosition(falls).y < -5.0f, "the layer table lets layer 3 pass through layer 4");
        const JBro::UInt32 none[32] = {};
        table.SetIgnoredLayers(none);
        table.SetTransform(falls, {0.0f, 2.0f, 0.0f}, {});
        table.SetLinearVelocity(falls, {});
        Run(table, 2.0f);
        Check(RestsOnGround(table.GetPosition(falls).y), "and with the table cleared it rests again");
    }
}

namespace
{
    JBro::Int32 CountTriggerEvents(JBro::ArrayView<const JBro::Physics3D::ContactEvent> events)
    {
        JBro::Int32 count = 0;
        for (const JBro::Physics3D::ContactEvent& event : events)
        {
            count += event.isTrigger ? 1 : 0;
        }
        return count;
    }

    // **빠른 트리거**(D-423). 한 스텝에 8 을 가는 총알이 두께 0.2 의 트리거 판을 건너뛰지 않는다 - 그 스텝에 시작, 다음 스텝에 끝.
    // 트리거가 `MoveContinuously` 로 한 스텝에 멀리 옮겨져도 사이의 몸을 알린다. `SetTransform`(순간 이동)은 쓸지 않는다.
    void TestFastTriggersDoNotSkip()
    {
        World world;
        world.SetGravity({0.0f, 0.0f, 0.0f});
        BodyDef slab;
        slab.type = BodyType::Static;
        slab.shape.halfExtents = {0.1f, 2.0f, 2.0f};
        slab.isTrigger = true;
        world.CreateBody(slab);
        BodyDef bullet;
        bullet.shape.type = ShapeType::Sphere;
        bullet.shape.radius = 0.1f;
        bullet.position = {-6.0f, 0.0f, 0.0f};
        bullet.linearVelocity = {480.0f, 0.0f, 0.0f};
        const BodyId shot = world.CreateBody(bullet);
        world.Step(Frame);
        Check(world.GetPosition(shot).x > 0.3f, "the bullet ends the step past the slab");
        Check(CountTriggerEvents(world.GetBeginEvents()) == 1, "a bullet that jumps over a thin trigger in one step still enters it");
        Check(CountTriggerEvents(world.GetEndEvents()) == 0, "and leaves it on the next step, not the same one");
        world.Step(Frame);
        Check(CountTriggerEvents(world.GetEndEvents()) == 1 && CountTriggerEvents(world.GetBeginEvents()) == 0, "the next step ends the pass");

        // 트리거를 옮긴다: 사이에 떠 있는 상자를 지나친다.
        World moved;
        moved.SetGravity({0.0f, 0.0f, 0.0f});
        BodyDef sensor;
        sensor.type = BodyType::Kinematic;
        sensor.shape.halfExtents = {0.1f, 0.5f, 0.5f};
        sensor.isTrigger = true;
        sensor.position = {-5.0f, 0.0f, 0.0f};
        const BodyId gate = moved.CreateBody(sensor);
        moved.CreateBody(BoxAt({0.0f, 0.0f, 0.0f}));
        moved.Step(Frame);
        moved.MoveContinuously(gate, {5.0f, 0.0f, 0.0f}, {});
        moved.Step(Frame);
        Check(CountTriggerEvents(moved.GetBeginEvents()) == 1, "a trigger moved continuously across a box enters it");
        moved.Step(Frame);
        Check(CountTriggerEvents(moved.GetEndEvents()) == 1 && CountTriggerEvents(moved.GetBeginEvents()) == 0,
            "and leaves it on the next step without entering again");
        moved.SetTransform(gate, {-5.0f, 0.0f, 0.0f}, {});
        moved.Step(Frame);
        Check(CountTriggerEvents(moved.GetBeginEvents()) == 0, "a teleport sweeps nothing");
    }
}

namespace
{
    // **빠른 몸이 얇은 벽을 뚫지 않는다**(D-425). 반지름 0.05 의 공이 한 스텝에 3 을 가며 두께 0.1 의 벽으로 간다 - 벽 앞에서 멈춘다.
    // 공끼리도: 빠른 공이 떠 있는 상자를 지나치지 않고 민다.
    void TestFastBodiesDoNotTunnel()
    {
        World world;
        world.SetGravity({0.0f, 0.0f, 0.0f});
        BodyDef wall;
        wall.type = BodyType::Static;
        wall.shape.halfExtents = {0.05f, 2.0f, 2.0f};
        world.CreateBody(wall);
        BodyDef bullet;
        bullet.shape.type = ShapeType::Sphere;
        bullet.shape.radius = 0.05f;
        bullet.position = {-4.0f, 0.0f, 0.0f};
        bullet.linearVelocity = {180.0f, 0.0f, 0.0f};
        const BodyId shot = world.CreateBody(bullet);
        for (JBro::Int32 step = 0; step < 10; ++step)
        {
            world.Step(Frame);
        }
        Check(world.GetPosition(shot).x < 0.0f, "a fast small ball stops at a thin wall instead of tunnelling through it");

        World moving;
        moving.SetGravity({0.0f, 0.0f, 0.0f});
        BodyDef target = BoxAt({0.0f, 0.0f, 0.0f});
        target.shape.halfExtents = {0.05f, 0.5f, 0.5f};
        const BodyId box = moving.CreateBody(target);
        bullet.position = {-4.0f, 0.0f, 0.0f};
        moving.CreateBody(bullet);
        for (JBro::Int32 step = 0; step < 3; ++step)
        {
            moving.Step(Frame);
        }
        Check(moving.GetLinearVelocity(box).x > 0.1f, "a fast ball pushes a thin floating box instead of passing it");
    }
}

namespace
{
    JBro::Float DistanceBetween(const Vector3& a, const Vector3& b)
    {
        return JBro::Length(JBro::Subtract(a, b));
    }

    // **조인트**(D-426). 거리(단단함·밧줄)·경첩(핀·각의 부호·한계·모터)·충돌 끄기·바디와 함께 지워짐·도형이 바뀌어도 핀이 그 자리.
    void TestJoints()
    {
        using JBro::Physics3D::DistanceJointDef;
        using JBro::Physics3D::HingeJointDef;
        using JBro::Physics3D::JointId;
        const Vector3 origin{0.0f, 0.0f, 0.0f};
        // 단단한 거리: 월드 원점에 3 으로 매단 상자는 흔들려 내려와도 원점에서 3 이다.
        {
            World world;
            const BodyId bob = world.CreateBody(BoxAt({3.0f, 0.0f, 0.0f}));
            DistanceJointDef def;
            def.bodyA = bob;
            def.minDistance = 3.0f;
            def.maxDistance = 3.0f;
            const JointId joint = world.CreateDistanceJoint(def);
            Check(world.IsValid(joint) && world.GetJointCount() == 1, "a distance joint to the world is made");
            for (JBro::Int32 step = 0; step < 60; ++step)
            {
                world.Step(Frame);
            }
            Check(world.GetPosition(bob).y < -1.5f, "the bob swings down");
            Check(std::fabs(DistanceBetween(world.GetPosition(bob), origin) - 3.0f) < 0.05f, "and stays three from the pin");
        }
        // 밧줄: 가까이 있으면 그대로 떨어지고, 3 에서 멈춘다.
        {
            World world;
            const BodyId bob = world.CreateBody(BoxAt({1.0f, 0.0f, 0.0f}));
            DistanceJointDef def;
            def.bodyA = bob;
            def.minDistance = 0.0f;
            def.maxDistance = 3.0f;
            world.CreateDistanceJoint(def);
            for (JBro::Int32 step = 0; step < 10; ++step)
            {
                world.Step(Frame);
            }
            Check(std::fabs(world.GetPosition(bob).x - 1.0f) < 0.01f, "a slack rope does not pull");
            for (JBro::Int32 step = 0; step < 110; ++step)
            {
                world.Step(Frame);
            }
            const JBro::Float length = DistanceBetween(world.GetPosition(bob), origin);
            Check(length < 3.05f && length > 2.8f, "a taut rope holds at its length");
        }
        // 경첩: 원점 기준 (-1, 0, 0) 을 월드의 그 자리에 Z 축으로 박은 상자는 핀 둘레로 떨어져 돈다. Z 를 오른손으로 도는 쪽이 양수라 각은 음수다.
        const auto hingedBox = [&](World& world, JBro::Bool limited) {
            const BodyId box = world.CreateBody(BoxAt(origin));
            HingeJointDef def;
            def.bodyA = box;
            def.localAnchorA = {-1.0f, 0.0f, 0.0f};
            def.localAnchorB = {-1.0f, 0.0f, 0.0f};
            def.localAxisA = {0.0f, 0.0f, 1.0f};
            def.localAxisB = {0.0f, 0.0f, 1.0f};
            def.enableLimit = limited;
            def.lowerAngle = -0.3f;
            def.upperAngle = 0.3f;
            const JointId joint = world.CreateHingeJoint(def);
            Check(world.IsValid(joint), "a hinge to the world is made");
            return std::pair<BodyId, JointId>(box, joint);
        };
        const auto pinOf = [&](World& world, BodyId box) {
            return JBro::Add(world.GetPosition(box), JBro::Rotate(world.GetRotation(box), Vector3{-1.0f, 0.0f, 0.0f}));
        };
        {
            World world;
            const auto [box, joint] = hingedBox(world, false);
            JBro::Float lowest = 0.0f;
            for (JBro::Int32 step = 0; step < 60; ++step)
            {
                world.Step(Frame);
                lowest = std::fmin(lowest, world.GetPosition(box).y);
            }
            Check(DistanceBetween(pinOf(world, box), {-1.0f, 0.0f, 0.0f}) < 0.05f, "the hinge keeps its pin");
            Check(world.GetHingeAngle(joint) < -0.5f, "the box swings clockwise about +Z, a negative angle");
            Check(lowest < -0.9f, "and its centre swings down through the bottom, one below the pin");
        }
        {
            World world;
            const auto [box, joint] = hingedBox(world, true);
            for (JBro::Int32 step = 0; step < 120; ++step)
            {
                world.Step(Frame);
            }
            Check(world.GetHingeAngle(joint) > -0.33f, "the limit stops the swing at -0.3");
        }
        // 도형을 바꿔 질량 중심이 옮겨도 핀은 그 자리다 - 조인트를 새 질량 중심으로 다시 세운다.
        {
            World world;
            const auto [box, joint] = hingedBox(world, false);
            world.Step(Frame);
            JBro::Physics3D::ShapeDesc shifted;
            shifted.halfExtents = {0.5f, 0.5f, 0.5f};
            shifted.offset = {0.6f, 0.0f, 0.0f};
            Check(world.SetShapes(box, JBro::ArrayView<const JBro::Physics3D::ShapeDesc>(&shifted, 1)), "the shape changes in place");
            for (JBro::Int32 step = 0; step < 30; ++step)
            {
                world.Step(Frame);
            }
            Check(DistanceBetween(pinOf(world, box), {-1.0f, 0.0f, 0.0f}) < 0.05f, "a moved centre of mass keeps the pin in place");
            Check(world.IsValid(joint), "and the joint keeps its number");
        }
        // 모터: 중력 없이 Y 축 경첩이 상자를 +2 rad/s 로 돌린다 - 각도 양수다.
        {
            World world;
            world.SetGravity({0.0f, 0.0f, 0.0f});
            const BodyId box = world.CreateBody(BoxAt(origin));
            HingeJointDef def;
            def.bodyA = box;
            def.localAxisA = {0.0f, 1.0f, 0.0f};
            def.localNormalA = {1.0f, 0.0f, 0.0f};
            def.localAxisB = {0.0f, 1.0f, 0.0f};
            def.localNormalB = {1.0f, 0.0f, 0.0f};
            def.enableMotor = true;
            def.motorSpeed = 2.0f;
            const JointId joint = world.CreateHingeJoint(def);
            for (JBro::Int32 step = 0; step < 30; ++step)
            {
                world.Step(Frame);
            }
            Check(std::fabs(world.GetAngularVelocity(box).y - 2.0f) < 0.05f, "the motor turns the box at its speed");
            Check(world.GetHingeAngle(joint) > 0.5f, "a positive speed turns it the positive way");
        }
        // 충돌 끄기: 바닥과 그 위의 상자를 긴 밧줄로 이었다. 끄면(기본) 상자가 바닥을 지나 떨어지고, 켜면 선다.
        for (const JBro::Bool collide : {false, true})
        {
            World world;
            const BodyId ground = MakeGround(world);
            const BodyId box = world.CreateBody(BoxAt({0.0f, 1.0f, 0.0f}));
            DistanceJointDef def;
            def.bodyA = box;
            def.bodyB = ground;
            def.minDistance = 0.0f;
            def.maxDistance = 10.0f;
            def.collideConnected = collide;
            world.CreateDistanceJoint(def);
            for (JBro::Int32 step = 0; step < 90; ++step)
            {
                world.Step(Frame);
            }
            if (collide)
            {
                Check(world.GetPosition(box).y > 0.4f, "joined bodies that may collide still rest on each other");
            }
            else
            {
                Check(world.GetPosition(box).y < -1.0f, "joined bodies do not collide by default");
            }
        }
        // 바디를 지우면 그 조인트도 지워진다. 같은 자리를 다시 쓴 조인트와 번호가 갈린다.
        {
            World world;
            const BodyId bob = world.CreateBody(BoxAt({3.0f, 0.0f, 0.0f}));
            DistanceJointDef def;
            def.bodyA = bob;
            const JointId first = world.CreateDistanceJoint(def);
            world.DestroyBody(bob);
            Check(false == world.IsValid(first) && world.GetJointCount() == 0, "destroying a body destroys its joints");
            def.bodyA = world.CreateBody(BoxAt({3.0f, 0.0f, 0.0f}));
            const JointId second = world.CreateDistanceJoint(def);
            Check(world.IsValid(second) && false == world.IsValid(first), "a reused slot does not revive the old number");
            def.bodyB = def.bodyA;
            Check(false == world.IsValid(world.CreateDistanceJoint(def)), "a body is not joined to itself");
        }
    }
}

JBro::Int32 RunPhysics3DWorldTests()
{
    JBRO_CASE(TestABoxFallsAndRestsOnTheGround());
    JBRO_CASE(TestASphereRollsDownASlope());
    JBRO_CASE(TestLayersAndTriggersLetBodiesThrough());
    JBRO_CASE(TestMassGravityAndFrozenAxes());
    JBRO_CASE(TestShapesIdsAndLifetime());
    JBRO_CASE(TestSteppingDoesNotAllocate());
    JBRO_CASE(TestCompoundShapesAndSurfaces());
    JBRO_CASE(TestShapesAndPropertiesChangeInPlace());
    JBRO_CASE(TestContactEventsFollowTouchingPairs());
    JBRO_CASE(TestQueriesFindBodies());
    JBRO_CASE(TestMeshesAndHulls());
    JBRO_CASE(TestWorkersSpeedUpALargePile());
    JBRO_CASE(TestForcesAndImpulses());
    JBRO_CASE(TestEachShapeHasItsOwnLayer());
    JBRO_CASE(TestFastTriggersDoNotSkip());
    JBRO_CASE(TestJoints());
    JBRO_CASE(TestFastBodiesDoNotTunnel());
    std::cout << "Physics3D world tests passed.\n";
    return 0;
}
