#include "TestCases.h"
#include <JBro/Physics3D/World.h>

#include <cmath>
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
        ground.layer = layer;
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
        skips.mask = 0xFFFFFFFDu;
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
}

JBro::Int32 RunPhysics3DWorldTests()
{
    JBRO_CASE(TestABoxFallsAndRestsOnTheGround());
    JBRO_CASE(TestASphereRollsDownASlope());
    JBRO_CASE(TestLayersAndTriggersLetBodiesThrough());
    JBRO_CASE(TestMassGravityAndFrozenAxes());
    JBRO_CASE(TestShapesIdsAndLifetime());
    JBRO_CASE(TestSteppingDoesNotAllocate());
    std::cout << "Physics3D world tests passed.\n";
    return 0;
}
