#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Canvas/Canvas.h>
#include <JBro/Framework2D/Framework2D.h>
#include <JBro/Runtime/World.h>

#include <iostream>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    struct Position
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Velocity
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    constexpr JBro::Engine::ComponentTypeId PositionType{ 1 };
    constexpr JBro::Engine::ComponentTypeId VelocityType{ 2 };

    void TestWorldOwnsEntityAndComponentLifetime()
    {
        JBro::Engine::CWorld world(JBro::Engine::CreateDefaultAllocator());
        Check(world.RegisterComponent<Position>(PositionType), "position type must register");
        Check(world.RegisterComponent<Velocity>(VelocityType), "velocity type must register");
        Check(false == world.RegisterComponent<Velocity>(PositionType),
            "one component type id must not bind to a different C++ storage type");

        const JBro::Engine::Entity entity = world.CreateEntity();
        Position* position = world.AddComponent<Position>(entity, PositionType, Position{ 1.0f, 2.0f });
        Velocity* velocity = world.AddComponent<Velocity>(entity, VelocityType, Velocity{ 3.0f, 4.0f });
        Check(position != nullptr && velocity != nullptr, "registered components must attach to a live entity");

        std::size_t queryCount = 0;
        world.Query<Position, Velocity>(PositionType, VelocityType,
            [&](JBro::Engine::Entity found, Position& foundPosition, Velocity& foundVelocity)
            {
                Check(found == entity, "query must report the owning entity");
                foundPosition.x += foundVelocity.x;
                ++queryCount;
            });
        Check(queryCount == 1 && position->x == 4.0f, "query must join component storages");

        Check(world.DestroyEntity(entity), "destroy request must accept a live entity");
        Check(world.IsAlive(entity), "entity destruction must be deferred during iteration-safe phase");
        world.FlushCommands();
        Check(false == world.IsAlive(entity), "flush must destroy the entity");
        Check(world.GetComponent<Position>(entity, PositionType) == nullptr,
            "entity destruction must remove every attached component");
    }

    void TestComponentAddressesStayStable()
    {
        JBro::Engine::CWorld world(JBro::Engine::CreateDefaultAllocator());
        Check(world.RegisterComponent<Position>(PositionType), "position type must register");

        const JBro::Engine::Entity first = world.CreateEntity();
        Position* firstPosition = world.AddComponent<Position>(first, PositionType, Position{ 99.0f, 0.0f });
        for (int index = 0; index < 200; ++index)
        {
            const JBro::Engine::Entity entity = world.CreateEntity();
            Check(world.AddComponent<Position>(entity, PositionType, Position{ static_cast<float>(index), 0.0f }) != nullptr,
                "component growth must succeed");
        }
        Check(firstPosition == world.GetComponent<Position>(first, PositionType) && firstPosition->x == 99.0f,
            "growing storage must not move existing components");
    }

    void TestCanvasIsCompositionOnly()
    {
        JBro::Engine::CCanvas canvas(JBro::Engine::CreateDefaultAllocator());
        JBro::Engine::CWorld& canvasWorld = canvas.GetWorld();
        const JBro::Engine::Entity canvasEntity = canvasWorld.CreateEntity();

        JBro::Engine::CLayer& background = canvas.CreateLayer("Background");
        JBro::Engine::CLayer& foreground = canvas.CreateLayer("Foreground");
        foreground.SetOpacity(0.5f);
        foreground.SetBlendMode(JBro::Engine::ELayerBlendMode::Additive);

        Check(canvas.AssignEntity(canvasEntity, foreground.GetId()), "live entity must be assignable to a layer");
        Check(canvas.MoveLayer(foreground.GetId(), 0), "layer order must be mutable");
        Check(canvas.GetLayerAt(0)->GetId() == foreground.GetId(), "canvas order must be composition order");

        foreground.SetVisible(false);
        Check(canvasWorld.IsAlive(canvasEntity), "layer visibility must not affect simulation lifetime");
        Check(canvas.DestroyLayer(foreground.GetId()), "non-default layer must be destroyable");
        Check(canvas.GetEntityLayer(canvasEntity) == background.GetId(),
            "destroyed layer members must fall back to the default layer");
    }

    void TestHierarchyAndFrameworkOwnership()
    {
        JBro::Engine::CCanvas canvas(JBro::Engine::CreateDefaultAllocator());
        JBro::Engine::CWorld& world = canvas.GetWorld();
        const JBro::Engine::Entity parent = world.CreateEntity("Parent");
        const JBro::Engine::Entity child = world.CreateEntity("Child");
        Check(world.SetParent(child, parent), "valid hierarchy assignment must succeed");
        Check(false == world.SetParent(parent, child), "hierarchy cycle must be rejected");
        world.SetActive(parent, false);
        Check(false == world.IsActiveInHierarchy(child), "parent activity must affect descendants");

        JBro::Engine::Framework2D framework;
        JBro::Engine::FrameworkContext context;
        Check(framework.Initialize(context), "2D framework must create a canvas-owned world");
        JBro::Engine::CWorld* frameworkWorld = framework.GetWorld();
        Check(frameworkWorld != nullptr, "framework canvas must own a world");
        Check(framework.GetCanvas() != nullptr && framework.GetCanvas()->GetLayerCount() == 1,
            "framework must own one composition canvas with a default layer");

        const JBro::Engine::Entity frameworkParent = frameworkWorld->CreateEntity("Parent");
        const JBro::Engine::Entity frameworkChild = frameworkWorld->CreateEntity("Child");
        frameworkWorld->SetParent(frameworkChild, frameworkParent);
        Check(frameworkWorld->DestroyEntity(frameworkParent), "parent destroy must queue hierarchy destruction");
        framework.Update(0.0f);
        Check(false == frameworkWorld->IsAlive(frameworkParent) && false == frameworkWorld->IsAlive(frameworkChild),
            "flushing the parent must destroy its hierarchy");
        framework.Shutdown();
    }
}

int RunWorldCanvasFoundationTests()
{
    TestWorldOwnsEntityAndComponentLifetime();
    TestComponentAddressesStayStable();
    TestCanvasIsCompositionOnly();
    TestHierarchyAndFrameworkOwnership();
    std::cout << "World/canvas foundation tests passed.\n";
    return 0;
}
