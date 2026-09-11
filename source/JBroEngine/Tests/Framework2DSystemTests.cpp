#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Rendering/RenderWorld2D.h>
#include <JBro/Framework2D/System/IPhysics2DSystem.h>
#include <JBro/Framework2D/System/Physics2DSystem.h>
#include <JBro/Framework2D/System/Camera2DSystem.h>
#include <JBro/Framework2D/System/SpriteRender2DSystem.h>
#include <JBro/Framework2D/System/Transform2DSystem.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    bool NearlyEqual(float left, float right)
    {
        return std::fabs(left - right) <= 0.0001f;
    }

    void TestTransformHierarchyPropagation()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* parent = canvas.CreateObject("parent");
        JBro::GameObject* child = canvas.CreateObject("child");
        Check(parent != nullptr && child != nullptr, "transform objects must be created");

        JBro::Component::Transform2D* parentLocal =
            canvas.AttachComponent<JBro::Component::Transform2D>(parent);
        JBro::Component::WorldTransform2D* parentWorld =
            canvas.AttachComponent<JBro::Component::WorldTransform2D>(parent);
        JBro::Component::Transform2D* childLocal =
            canvas.AttachComponent<JBro::Component::Transform2D>(child);
        JBro::Component::WorldTransform2D* childWorld =
            canvas.AttachComponent<JBro::Component::WorldTransform2D>(child);
        Check(
            parentLocal != nullptr
                && parentWorld != nullptr
                && childLocal != nullptr
                && childWorld != nullptr,
            "transform components must attach");

        parentLocal->position = {2.0f, 1.0f};
        parentLocal->rotation = 1.57079632679f;
        childLocal->position = {1.0f, 0.0f};
        child->SetParent(parent);

        JBro::System::Transform2DSystem system;
        system.Initialize(canvas);
        system.Update(canvas, 1.0f / 60.0f);

        Check(NearlyEqual(parentWorld->position.x, 2.0f), "parent world x must match local x");
        Check(NearlyEqual(parentWorld->position.y, 1.0f), "parent world y must match local y");
        Check(NearlyEqual(childWorld->position.x, 2.0f), "parent rotation must affect child world x");
        Check(NearlyEqual(childWorld->position.y, 2.0f), "parent rotation must affect child world y");
        Check(NearlyEqual(childWorld->rotation, parentLocal->rotation), "world rotation must be decomposed");
        Check(false == childWorld->dirty, "updated world transform must be clean");

        childLocal->SetEnabled(false);
        childWorld->position = {99.0f, 99.0f};
        system.Update(canvas, 1.0f / 60.0f);
        Check(
            NearlyEqual(childWorld->position.x, 99.0f)
                && NearlyEqual(childWorld->position.y, 99.0f),
            "disabled transform must pass through the shared active gate");
        system.Shutdown(canvas);
    }

    void TestPhysicsGravityIntegration()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("falling body");
        Check(object != nullptr, "physics object must be created");

        JBro::Component::Transform2D* transform =
            canvas.AttachComponent<JBro::Component::Transform2D>(object);
        JBro::Component::WorldTransform2D* world =
            canvas.AttachComponent<JBro::Component::WorldTransform2D>(object);
        JBro::Component::Rigidbody2D* body =
            canvas.AttachComponent<JBro::Component::Rigidbody2D>(object);
        Check(
            transform != nullptr && world != nullptr && body != nullptr,
            "physics components must attach");

        JBro::System::Physics2DSystem system;
        system.Initialize(canvas);
        system.FixedUpdate(canvas, 0.5f);

        Check(NearlyEqual(body->linearVelocity.y, -4.905f), "gravity must update velocity");
        Check(NearlyEqual(transform->position.y, -2.4525f), "velocity must update position");
        Check(world->dirty, "physics movement must invalidate the world transform");

        body->SetEnabled(false);
        const float disabledPosition = transform->position.y;
        system.FixedUpdate(canvas, 0.5f);
        Check(
            NearlyEqual(transform->position.y, disabledPosition),
            "disabled rigidbody must pass through the shared active gate");
        system.Shutdown(canvas);
    }

    void TestPhysicsQueries()
    {
        static_assert(std::is_same_v<
            decltype(JBro::Collision2D::other),
            JBro::GameObjectHandle>);
        static_assert(std::is_standard_layout_v<JBro::Collision2D>);
        static_assert(std::is_trivially_copyable_v<JBro::Collision2D>);

        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* boxObject = canvas.CreateObject("box");
        JBro::GameObject* circleObject = canvas.CreateObject("circle");
        Check(boxObject != nullptr && circleObject != nullptr, "query objects must be created");

        JBro::Component::Transform2D* boxTransform =
            canvas.AttachComponent<JBro::Component::Transform2D>(boxObject);
        JBro::Component::Collider2D* boxCollider =
            canvas.AttachComponent<JBro::Component::Collider2D>(boxObject);
        JBro::Component::Collider2D* secondBoxCollider =
            canvas.AttachComponent<JBro::Component::Collider2D>(boxObject);
        JBro::Component::Transform2D* circleTransform =
            canvas.AttachComponent<JBro::Component::Transform2D>(circleObject);
        JBro::Component::Collider2D* circleCollider =
            canvas.AttachComponent<JBro::Component::Collider2D>(circleObject);
        Check(
            boxTransform != nullptr
                && boxCollider != nullptr
                && secondBoxCollider != nullptr
                && circleTransform != nullptr
                && circleCollider != nullptr,
            "query components must attach");

        boxTransform->position = {2.0f, 0.0f};
        boxCollider->shape = JBro::Component::ColliderShape2D::Box;
        boxCollider->size = {2.0f, 2.0f};
        secondBoxCollider->shape = JBro::Component::ColliderShape2D::Box;
        secondBoxCollider->offset = {0.25f, 0.0f};
        secondBoxCollider->size = {0.5f, 0.5f};
        circleTransform->position = {5.0f, 0.0f};
        circleCollider->shape = JBro::Component::ColliderShape2D::Circle;
        circleCollider->radius = 1.0f;

        JBro::System::Physics2DSystem system;
        static_assert(std::is_base_of_v<
            JBro::System::IPhysics2DSystem,
            JBro::System::Physics2DSystem>);
        JBro::System::IPhysics2DSystem& queries = system;
        JBro::Collision2D hit;
        Check(false == queries.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 10.0f, hit),
            "uninitialized queries must not access a canvas");
        system.Initialize(canvas);
        Check(
            queries.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 10.0f, hit),
            "ray must hit the nearest collider");
        Check(hit.other.GetInstanceId() == boxObject->GetInstanceId(),
            "ray must select the nearest object through a safe script handle");
        Check(NearlyEqual(hit.point.x, 1.0f), "box hit point must be on its near face");
        Check(NearlyEqual(hit.normal.x, -1.0f), "box hit normal must face the ray");

        JBro::Array<JBro::GameObjectHandle> overlaps;
        queries.OverlapBox({{1.5f, -0.5f}, {5.5f, 0.5f}}, overlaps);
        Check(overlaps.Size() == 2, "overlap box must include box and circle");

        boxCollider->SetEnabled(false);
        secondBoxCollider->SetEnabled(false);
        Check(
            queries.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 10.0f, hit),
            "ray must continue past a disabled collider");
        Check(hit.other.GetInstanceId() == circleObject->GetInstanceId(),
            "disabled collider must not participate in queries");
        Check(NearlyEqual(hit.point.x, 4.0f), "circle hit point must be on its near edge");

        const JBro::Collision2D retainedHit = hit;
        Check(canvas.DestroyObject(circleObject), "query target must be destroyed");
        Check(false == retainedHit.other.IsValid(), "retained collision handle must invalidate after destruction");
        std::size_t validOverlapCount = 0;
        for (const auto& overlap : overlaps)
        {
            if (overlap.IsValid())
            {
                ++validOverlapCount;
            }
        }
        Check(validOverlapCount == 1, "only the surviving overlap target must remain valid");

        system.Shutdown(canvas);
        Check(false == queries.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 10.0f, hit),
            "physics queries must stop resolving the canvas after system shutdown");
        Check(hit.other.GetInstanceId() == JBro::InvalidInstanceId,
            "unavailable queries must clear the previous hit");
        queries.OverlapBox({{1.5f, -0.5f}, {5.5f, 0.5f}}, overlaps);
        Check(overlaps.Size() == 0, "shutdown overlap queries must clear previous results");

        boxCollider->SetEnabled(true);
        system.Initialize(canvas);
        Check(queries.Raycast({0.0f, 0.0f}, {1.0f, 0.0f}, 10.0f, hit)
            && hit.other.GetInstanceId() == boxObject->GetInstanceId(),
            "reinitialized queries must use the newly bound canvas");
        system.Shutdown(canvas);
    }

    void TestRenderExtraction()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* cameraObject = canvas.CreateObject("camera");
        JBro::GameObject* spriteObject = canvas.CreateObject("sprite");
        auto* cameraLocal = canvas.AttachComponent<JBro::Component::Transform2D>(cameraObject);
        auto* cameraWorld = canvas.AttachComponent<JBro::Component::WorldTransform2D>(cameraObject);
        auto* camera = canvas.AttachComponent<JBro::Component::Camera2D>(cameraObject);
        auto* spriteLocal = canvas.AttachComponent<JBro::Component::Transform2D>(spriteObject);
        auto* spriteWorld = canvas.AttachComponent<JBro::Component::WorldTransform2D>(spriteObject);
        auto* sprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(spriteObject);
        auto* secondSprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(spriteObject);
        Check(cameraLocal && cameraWorld && camera && spriteLocal && spriteWorld && sprite && secondSprite,
            "render components must attach");
        cameraLocal->position = {3.0f, 4.0f};
        cameraLocal->rotation = 0.7f;
        cameraLocal->scale = {2.0f, 0.5f};
        camera->primary = true;
        camera->orthographicSize = 7.0f;
        camera->projection = JBro::Component::CameraProjection2D::PixelPerfect;
        camera->nearPlane = -7.0f;
        camera->farPlane = 20.0f;
        camera->clearColor = {0.2f, 0.3f, 0.4f, 1.0f};
        spriteLocal->position = {8.0f, -2.0f};
        sprite->size = {2.0f, 3.0f};
        sprite->flip = JBro::Component::SpriteFlip::Both;
        sprite->renderOrder = -4;
        sprite->sprite = {3, 7};
        sprite->material = {5, 9};
        sprite->pivot = {0.1f, 0.8f};
        sprite->tint = {0.5f, 0.6f, 0.7f, 0.8f};
        secondSprite->visible = false;

        JBro::RenderWorld2D world;
        Check(world.ReserveSprites(2), "extraction storage must reserve");
        const auto capacity = world.GetSpriteCapacity();
        JBro::System::Transform2DSystem transforms;
        JBro::System::Camera2DSystem cameras;
        JBro::System::SpriteRender2DSystem sprites;
        Check(transforms.GetExecutionOrder() < cameras.GetExecutionOrder()
            && cameras.GetExecutionOrder() < sprites.GetExecutionOrder(), "extraction must follow transforms");
        cameras.Update(canvas, 0.0f);
        sprites.Update(canvas, 0.0f);
        cameras.SetRenderWorld(&world);
        sprites.SetRenderWorld(&world);
        for (int frame = 0; frame < 3; ++frame)
        {
            world.BeginFrame();
            transforms.Update(canvas, 0.0f);
            cameras.Update(canvas, 0.0f);
            sprites.Update(canvas, 0.0f);
            world.EndFrame();
            const auto* extractedCamera = world.GetCamera();
            Check(extractedCamera && extractedCamera->owner == cameraObject, "active primary camera must extract");
            const auto identity = JBro::MultiplyMatrix3x2(cameraWorld->matrix, extractedCamera->view);
            Check(NearlyEqual(identity.m11, 1.0f) && NearlyEqual(identity.m22, 1.0f)
                && NearlyEqual(identity.m12, 0.0f) && NearlyEqual(identity.m21, 0.0f)
                && NearlyEqual(identity.m31, 0.0f) && NearlyEqual(identity.m32, 0.0f),
                "camera view must invert translation, rotation and nonuniform scale");
            Check(NearlyEqual(extractedCamera->orthographicSize, 7.0f), "camera size must survive extraction");
            Check(extractedCamera->projection == camera->projection
                && NearlyEqual(extractedCamera->nearPlane, -7.0f) && NearlyEqual(extractedCamera->farPlane, 20.0f)
                && NearlyEqual(extractedCamera->clearColor.R, 0.2f), "camera projection settings must survive extraction");
            Check(world.GetSpriteCount() == 1, "hidden sprites must not extract");
            const auto& item = world.GetSprites()[0];
            Check(item.owner == spriteObject && item.sourceId == sprite->GetInstanceId(), "sprite identity must survive extraction");
            Check(NearlyEqual(item.world.m31, 8.0f) && NearlyEqual(item.world.m32, -2.0f), "sprite must use world transform");
            Check(NearlyEqual(item.size.x, -2.0f) && NearlyEqual(item.size.y, -3.0f), "both flips must affect signed size");
            Check(item.renderOrder == -4 && world.GetSpriteCapacity() == capacity, "extraction must preserve order and reuse storage");
            Check(item.sprite.index == 3 && item.sprite.generation == 7
                && item.material.index == 5 && item.material.generation == 9
                && NearlyEqual(item.pivot.x, 0.1f) && NearlyEqual(item.pivot.y, 0.8f)
                && NearlyEqual(item.tint.A, 0.8f), "sprite assets and appearance must survive extraction");
        }
        world.BeginFrame();
        camera->primary = false;
        sprite->SetEnabled(false);
        cameras.Update(canvas, 0.0f);
        sprites.Update(canvas, 0.0f);
        Check(world.GetCamera() == nullptr && world.GetSpriteCount() == 0, "non-primary camera and disabled sprite must skip");
        camera->primary = true;
        cameraLocal->scale.x = 0.0f;
        transforms.Update(canvas, 0.0f);
        cameras.Update(canvas, 0.0f);
        Check(world.GetCamera() == nullptr, "singular camera must not produce an invalid view");
        cameraLocal->scale.x = 1.0f;
        transforms.Update(canvas, 0.0f);
        cameraObject->SetActive(false);
        spriteObject->SetActive(false);
        sprite->SetEnabled(true);
        cameras.Update(canvas, 0.0f);
        sprites.Update(canvas, 0.0f);
        Check(world.GetCamera() == nullptr && world.GetSpriteCount() == 0, "inactive objects must not extract");
        cameraObject->SetActive(true);
        spriteObject->SetActive(true);
        cameraWorld->dirty = true;
        spriteWorld->dirty = true;
        cameras.Update(canvas, 0.0f);
        sprites.Update(canvas, 0.0f);
        Check(world.GetCamera() == nullptr && world.GetSpriteCount() == 0, "dirty world caches must not extract");
        transforms.Update(canvas, 0.0f);
        cameraWorld->SetEnabled(false);
        spriteWorld->SetEnabled(false);
        cameras.Update(canvas, 0.0f);
        sprites.Update(canvas, 0.0f);
        Check(world.GetCamera() == nullptr && world.GetSpriteCount() == 0, "disabled world caches must not extract");
        cameraWorld->SetEnabled(true);
        spriteWorld->SetEnabled(true);
        secondSprite->visible = true;
        sprite->flip = JBro::Component::SpriteFlip::Horizontal;
        secondSprite->flip = JBro::Component::SpriteFlip::Vertical;
        sprites.Update(canvas, 0.0f);
        Check(world.GetSpriteCount() == 2, "multiple sprite components on one object must extract independently");
        Check(NearlyEqual(world.GetSprites()[0].size.x, -2.0f) && NearlyEqual(world.GetSprites()[0].size.y, 3.0f)
            && NearlyEqual(world.GetSprites()[1].size.x, 1.0f) && NearlyEqual(world.GetSprites()[1].size.y, -1.0f),
            "horizontal and vertical flips must affect only their corresponding axes");
        Check(world.GetSprites()[0].sourceId != world.GetSprites()[1].sourceId,
            "multiple sprites must have distinct sort identities");
        world.BeginFrame();
        auto* laterCamera = canvas.AttachComponent<JBro::Component::Camera2D>(spriteObject);
        laterCamera->primary = true;
        cameras.Update(canvas, 0.0f);
        Check(world.GetCamera()->owner == cameraObject, "first valid primary camera must win");
        world.BeginFrame();
        cameraLocal->scale.x = 0.0f;
        transforms.Update(canvas, 0.0f);
        cameras.Update(canvas, 0.0f);
        Check(world.GetCamera() && world.GetCamera()->owner == spriteObject, "invalid primary camera must allow a valid fallback");
    }

    void TestRenderWorldCapacityLimit()
    {
        JBro::RenderWorld2D world;
        world.BeginFrame();
        Check(false == world.SubmitSprite({}), "unreserved collection must reject without allocating");
        Check(world.GetDroppedSpriteCount() == 1, "overflow must be observable");
        Check(world.ReserveSprites(2), "collection must reserve outside the frame");
        world.BeginFrame();
        const auto capacity = world.GetSpriteCapacity();
        for (std::size_t index = 0; index < capacity; ++index)
        {
            Check(world.SubmitSprite({}), "reserved slots must accept submissions");
        }
        Check(false == world.SubmitSprite({}), "full collection must reject without growing");
        Check(world.GetSpriteCapacity() == capacity && world.GetSpriteCount() == capacity,
            "overflow must preserve storage and accepted submissions");
        Check(world.GetDroppedSpriteCount() == 1, "frame must count dropped submissions");
        world.BeginFrame();
        Check(world.GetDroppedSpriteCount() == 0, "new frame must clear overflow statistics");
        world.EndFrame();
    }

    void TestRenderWorldCollection()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* first = canvas.CreateObject("first sprite");
        JBro::GameObject* second = canvas.CreateObject("second sprite");
        Check(first != nullptr && second != nullptr, "render objects must be created");

        JBro::RenderWorld2D renderWorld;
        Check(renderWorld.ReserveSprites(4), "render-world storage must reserve before frames");
        const std::size_t reservedCapacity = renderWorld.GetSpriteCapacity();
        Check(reservedCapacity >= 4, "render-world capacity must satisfy the reservation");

        renderWorld.BeginFrame();
        JBro::RenderCamera2D camera;
        camera.owner = first;
        renderWorld.SetCamera(camera);

        JBro::SpriteRenderItem later;
        later.owner = first;
        later.renderOrder = 5;
        JBro::SpriteRenderItem earlier;
        earlier.owner = second;
        earlier.renderOrder = -2;
        renderWorld.SubmitSprite(later);
        renderWorld.SubmitSprite(earlier);
        renderWorld.EndFrame();

        Check(renderWorld.GetCamera() != nullptr, "render-world camera must survive collection");
        Check(renderWorld.GetSpriteCount() == 2, "render-world must retain submitted sprites");
        Check(
            renderWorld.GetSprites()[0].owner == second,
            "render-world sprites must sort by render order");

        renderWorld.BeginFrame();
        Check(renderWorld.GetCamera() == nullptr, "new render frame must clear its camera");
        Check(renderWorld.GetSpriteCount() == 0, "new render frame must clear sprite count");
        Check(
            renderWorld.GetSpriteCapacity() == reservedCapacity,
            "new render frame must reuse reserved storage");
    }
}

int RunFramework2DSystemTests()
{
    TestTransformHierarchyPropagation();
    TestPhysicsGravityIntegration();
    TestPhysicsQueries();
    TestRenderWorldCollection();
    TestRenderExtraction();
    TestRenderWorldCapacityLimit();
    std::cout << "Framework2D system tests passed.\n";
    return 0;
}
