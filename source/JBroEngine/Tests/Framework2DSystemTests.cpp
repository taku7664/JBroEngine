#include <JBro/Core/Core.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Runtime/ComponentLookupStats.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
#include <JBro/Framework2D/System/IPhysics2DSystem.h>
#include <JBro/Framework2DSystem/System/Physics2DSystem.h>
#include <JBro/Framework2DSystem/System/Camera2DSystem.h>
#include <JBro/Framework2DSystem/System/SpriteRender2DSystem.h>
#include <JBro/Framework2DSystem/System/Transform2DSystem.h>
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
        JBro::Component::Transform2D* childLocal =
            canvas.AttachComponent<JBro::Component::Transform2D>(child);
        Check(parentLocal != nullptr && childLocal != nullptr,
            "transform components must attach");

        parentLocal->position = {2.0f, 1.0f};
        parentLocal->rotation = 1.57079632679f;
        childLocal->position = {1.0f, 0.0f};
        child->SetParent(parent);

        JBro::System::Transform2DSystem system;
        system.Initialize(canvas);
        system.Update(canvas, 1.0f / 60.0f);

        Check(NearlyEqual(parentLocal->worldPosition.x, 2.0f), "parent world x must match local x");
        Check(NearlyEqual(parentLocal->worldPosition.y, 1.0f), "parent world y must match local y");
        Check(NearlyEqual(childLocal->worldPosition.x, 2.0f), "parent rotation must affect child world x");
        Check(NearlyEqual(childLocal->worldPosition.y, 2.0f), "parent rotation must affect child world y");
        Check(NearlyEqual(childLocal->worldRotation, parentLocal->rotation), "world rotation must be decomposed");
        Check(childLocal->worldValid, "updated world transform must be valid");

        childLocal->SetEnabled(false);
        childLocal->worldPosition = {99.0f, 99.0f};
        system.Update(canvas, 1.0f / 60.0f);
        Check(
            NearlyEqual(childLocal->worldPosition.x, 99.0f)
                && NearlyEqual(childLocal->worldPosition.y, 99.0f),
            "disabled transform must pass through the shared active gate");
        Check(false == childLocal->worldValid,
            "a transform that stopped being updated must not stay marked valid");
        system.Shutdown(canvas);
    }

    // ── 꺼진 부모 아래의 서브트리 (A4) ───────────────────────────────────
    //
    // 부모 오브젝트는 켜 둔 채 부모의 `Transform2D` 만 끄면, 예전에는 자식이 "루트" 로
    // 뽑혀 **단위행렬에서** 전파됐다. 자식 서브트리가 조용히 원점으로 튀었다는 뜻이다.
    // 이제는 서브트리를 통째로 건너뛰고 캐시를 무효로 둔다 - 그리기와 카메라가
    // `worldValid` 를 보므로 아무것도 엉뚱한 자리에 나타나지 않는다.
    void TestDisablingAParentTransformSkipsTheSubtreeInsteadOfMovingIt()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* parent = canvas.CreateObject("parent");
        JBro::GameObject* child = canvas.CreateObject("child");
        JBro::GameObject* grandChild = canvas.CreateObject("grandchild");
        child->SetParent(parent);
        grandChild->SetParent(child);

        auto* parentLocal = canvas.AttachComponent<JBro::Component::Transform2D>(parent);
        auto* childLocal = canvas.AttachComponent<JBro::Component::Transform2D>(child);
        auto* grandLocal = canvas.AttachComponent<JBro::Component::Transform2D>(grandChild);
        parentLocal->position = {10.0f, 0.0f};
        childLocal->position = {1.0f, 0.0f};
        grandLocal->position = {1.0f, 0.0f};

        JBro::System::Transform2DSystem system;
        system.Initialize(canvas);
        system.Update(canvas, 1.0f / 60.0f);
        Check(NearlyEqual(childLocal->worldPosition.x, 11.0f), "the child must start under its parent");
        Check(NearlyEqual(grandLocal->worldPosition.x, 12.0f), "the grandchild must start under the chain");

        // 부모 오브젝트는 켜 둔 채 그 Transform 만 끈다.
        parentLocal->SetEnabled(false);
        system.Update(canvas, 1.0f / 60.0f);

        Check(parent->IsActiveInHierarchy() && child->IsActiveInHierarchy(),
            "only the component was disabled, the objects stay active");
        Check(false == childLocal->worldValid && false == grandLocal->worldValid,
            "a subtree under a disabled transform must not be marked valid");
        Check(NearlyEqual(childLocal->worldPosition.x, 11.0f)
                && NearlyEqual(grandLocal->worldPosition.x, 12.0f),
            "the subtree must not be moved to the origin");

        // 다시 켜면 그대로 돌아온다.
        parentLocal->SetEnabled(true);
        system.Update(canvas, 1.0f / 60.0f);
        Check(childLocal->worldValid && grandLocal->worldValid,
            "re-enabling the parent transform must bring the subtree back");
        Check(NearlyEqual(childLocal->worldPosition.x, 11.0f)
                && NearlyEqual(grandLocal->worldPosition.x, 12.0f),
            "the subtree must return to where it was");

        system.Shutdown(canvas);
    }

    // 부모에 `Transform2D` 가 **아예 없는** 것은 꺼진 것과 다르다. 물려받을 자리가 없으므로
    // 그 아래는 자기 로컬이 곧 월드다. 이쪽 동작은 바뀌지 않았다.
    void TestAParentWithNoTransformLeavesTheChildAtItsOwnLocal()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* parent = canvas.CreateObject("no transform");
        JBro::GameObject* child = canvas.CreateObject("child");
        child->SetParent(parent);

        auto* childLocal = canvas.AttachComponent<JBro::Component::Transform2D>(child);
        childLocal->position = {3.0f, 4.0f};

        JBro::System::Transform2DSystem system;
        system.Initialize(canvas);
        system.Update(canvas, 1.0f / 60.0f);

        Check(childLocal->worldValid, "a child under a transformless parent must still be updated");
        Check(NearlyEqual(childLocal->worldPosition.x, 3.0f)
                && NearlyEqual(childLocal->worldPosition.y, 4.0f),
            "with nothing to inherit, the local value is the world value");

        system.Shutdown(canvas);
    }

    // D-46: 레이어 합성 순서가 정렬 키의 최상위이고, 비가시 레이어는 추출에서 빠진다.
    // 이 두 성질이 없으면 Layer 는 렌더에 아무 영향이 없는 이름표일 뿐이다.
    void TestLayerOrderDrivesSpriteSorting()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::Layer& background = *canvas.GetLayerAt(0);
        JBro::Layer& foreground = canvas.CreateLayer("foreground");

        JBro::GameObject* front = canvas.CreateObject("front");
        JBro::GameObject* back = canvas.CreateObject("back");
        Check(canvas.SetObjectLayer(front, foreground.GetId())
            && canvas.SetObjectLayer(back, background.GetId()),
            "objects must take their layer");

        for (JBro::GameObject* object : {front, back})
        {
            auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
            auto* sprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(object);
            Check(transform != nullptr && sprite != nullptr, "sprite fixture must attach");
            // 앞 레이어의 것이 renderOrder 로는 뒤로 가게 두어, 레이어가 이겨야 함을 드러낸다.
            sprite->renderOrder = object == front ? -100 : 100;
        }

        JBro::RenderWorld2D world;
        Check(world.ReserveSprites(8), "render world must reserve");
        JBro::System::Transform2DSystem transforms;
        JBro::System::SpriteRender2DSystem sprites;
        sprites.SetRenderWorld(&world);

        world.BeginFrame();
        transforms.Initialize(canvas);
        transforms.Update(canvas, 0.0f);
        sprites.ExtractRenderWorld(canvas);
        world.EndFrame();

        Check(world.GetSpriteCount() == 2, "both sprites must extract");
        Check(world.GetSprite(0).owner == back && world.GetSprite(1).owner == front,
            "layer order must outrank renderOrder when sorting");

        foreground.SetVisible(false);
        world.BeginFrame();
        transforms.Update(canvas, 0.0f);
        sprites.ExtractRenderWorld(canvas);
        world.EndFrame();
        Check(world.GetSpriteCount() == 1 && world.GetSprite(0).owner == back,
            "an invisible layer must drop out of extraction");
        transforms.Shutdown(canvas);
    }

    void TestPhysicsGravityIntegration()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        JBro::GameObject* object = canvas.CreateObject("falling body");
        Check(object != nullptr, "physics object must be created");

        JBro::Component::Transform2D* transform =
            canvas.AttachComponent<JBro::Component::Transform2D>(object);
        JBro::Component::Rigidbody2D* body =
            canvas.AttachComponent<JBro::Component::Rigidbody2D>(object);
        Check(
            transform != nullptr && body != nullptr,
            "physics components must attach");

        JBro::System::Physics2DSystem system;
        system.Initialize(canvas);
        system.FixedUpdate(canvas, 0.5f);

        Check(NearlyEqual(body->linearVelocity.y, -4.905f), "gravity must update velocity");
        // 커널은 고정 스텝을 넷으로 나눠 적분한다(D-199). 서브스텝 h 마다 속도를 먼저 올리고 옮기므로
        // 위치는 -g·h²·(1 + 2 + 3 + 4) 다. 한 번에 옮기던 옛 값(-2.4525)보다 참값 -g·t²/2 = -1.226 에 가깝다.
        const float subStep = 0.5f / 4.0f;
        Check(NearlyEqual(transform->position.y, -9.81f * subStep * subStep * 10.0f), "velocity must update position");
        Check(false == transform->worldValid, "physics movement must invalidate the world transform");

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
        auto* cameraLocal = canvas.AttachComponent<JBro::Component::Transform2D>(cameraObject);        auto* camera = canvas.AttachComponent<JBro::Component::Camera2D>(cameraObject);
        auto* spriteLocal = canvas.AttachComponent<JBro::Component::Transform2D>(spriteObject);        auto* sprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(spriteObject);
        auto* secondSprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(spriteObject);
        Check(cameraLocal && camera && spriteLocal && sprite && secondSprite,
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
            const auto identity = JBro::MultiplyMatrix3x2(cameraLocal->world, extractedCamera->view);
            Check(NearlyEqual(identity.m11, 1.0f) && NearlyEqual(identity.m22, 1.0f)
                && NearlyEqual(identity.m12, 0.0f) && NearlyEqual(identity.m21, 0.0f)
                && NearlyEqual(identity.m31, 0.0f) && NearlyEqual(identity.m32, 0.0f),
                "camera view must invert translation, rotation and nonuniform scale");
            Check(NearlyEqual(extractedCamera->orthographicSize, 7.0f), "camera size must survive extraction");
            Check(extractedCamera->projection == camera->projection
                && NearlyEqual(extractedCamera->nearPlane, -7.0f) && NearlyEqual(extractedCamera->farPlane, 20.0f)
                && NearlyEqual(extractedCamera->clearColor.R, 0.2f), "camera projection settings must survive extraction");
            Check(world.GetSpriteCount() == 1, "hidden sprites must not extract");
            const auto& item = world.GetSprite(0);
            Check(item.owner == spriteObject && item.sourceId == sprite->GetInstanceId(), "sprite identity must survive extraction");
            Check(NearlyEqual(item.world.m31, 8.0f) && NearlyEqual(item.world.m32, -2.0f), "sprite must use world transform");
            Check(NearlyEqual(item.size.x, -2.0f) && NearlyEqual(item.size.y, -3.0f), "both flips must affect signed size");
            // 라이브러리가 없어 스프라이트가 풀리지 않았다. `FromSprite`(기본)여도 저작 값이다(D-117).
            Check(sprite->sizeMode == JBro::Component::SpriteSizeMode::FromSprite
                && NearlyEqual(item.pivot.x, 0.1f) && NearlyEqual(item.pivot.y, 0.8f),
                "an unresolved sprite keeps its authored size and pivot");
            Check(item.renderOrder == -4 && world.GetSpriteCapacity() == capacity, "extraction must preserve order and reuse storage");
            // 라이브러리가 없으면 스프라이트 에셋은 풀리지 않는다 - 텍스처는 비고(흰색) UV 는 전체다(D-113).
            Check(item.texture.generation == 0
                && item.uvRect[0] == 0.0f && item.uvRect[1] == 0.0f && item.uvRect[2] == 1.0f && item.uvRect[3] == 1.0f
                && item.material.index == 5 && item.material.generation == 9
                && NearlyEqual(item.pivot.x, 0.1f) && NearlyEqual(item.pivot.y, 0.8f)
                && NearlyEqual(item.tint.A, 0.8f), "sprite assets and appearance must survive extraction");
        }
        world.BeginFrame();
        camera->primary = false;
        sprite->SetEnabled(false);
        cameras.Update(canvas, 0.0f);
        sprites.Update(canvas, 0.0f);
        // **`primary` 가 아니어도 살아 있으면 그린다**(D-187). 그전에는 아무것도 그리지
        // 않았는데, 카메라를 붙이고 재생을 누른 사람에게는 검은 화면만 남았다.
        Check(world.GetCamera() != nullptr,
            "an active camera draws even when nothing is marked primary");
        Check(world.GetSpriteCount() == 0, "while a disabled sprite still skips");
        // **프레임을 새로 연다.** 추출은 카메라를 넣기만 하므로, 지우지 않으면 앞 프레임에
        // 넣은 것이 남아 "아무것도 고르지 못했다" 를 잴 수 없다.
        world.BeginFrame();
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
        cameraLocal->worldValid = false;
        spriteLocal->worldValid = false;
        cameras.Update(canvas, 0.0f);
        sprites.Update(canvas, 0.0f);
        Check(world.GetCamera() == nullptr && world.GetSpriteCount() == 0, "dirty world caches must not extract");
        transforms.Update(canvas, 0.0f);
        cameraLocal->SetEnabled(false);
        spriteLocal->SetEnabled(false);
        cameras.Update(canvas, 0.0f);
        sprites.Update(canvas, 0.0f);
        Check(world.GetCamera() == nullptr && world.GetSpriteCount() == 0, "disabled world caches must not extract");
        cameraLocal->SetEnabled(true);
        spriteLocal->SetEnabled(true);
        secondSprite->visible = true;
        sprite->flip = JBro::Component::SpriteFlip::Horizontal;
        secondSprite->flip = JBro::Component::SpriteFlip::Vertical;
        sprites.Update(canvas, 0.0f);
        Check(world.GetSpriteCount() == 2, "multiple sprite components on one object must extract independently");
        Check(NearlyEqual(world.GetSprite(0).size.x, -2.0f) && NearlyEqual(world.GetSprite(0).size.y, 3.0f)
            && NearlyEqual(world.GetSprite(1).size.x, 1.0f) && NearlyEqual(world.GetSprite(1).size.y, -1.0f),
            "horizontal and vertical flips must affect only their corresponding axes");
        Check(world.GetSprite(0).sourceId != world.GetSprite(1).sourceId,
            "multiple sprites must have distinct sort identities");
        world.BeginFrame();
        auto* laterCamera = canvas.AttachComponent<JBro::Component::Camera2D>(spriteObject);
        laterCamera->primary = true;
        cameras.Update(canvas, 0.0f);
        Check(world.GetCamera()->owner == cameraObject, "first valid primary camera must win");
        // **`primary` 는 차례를 이긴다**(D-187). 지정한 카메라가 뒤에 있다고 해서 앞의
        // 지정하지 않은 것으로 그리면, 지정이라는 것이 아무 뜻도 갖지 못한다.
        world.BeginFrame();
        camera->primary = false;
        cameras.Update(canvas, 0.0f);
        Check(world.GetCamera() != nullptr && world.GetCamera()->owner == spriteObject,
            "a primary camera wins over an earlier one that is not primary");
        // 아무것도 지정하지 않으면 첫 활성 카메라다. 차례가 같으면 늘 같은 것이어야 한다.
        world.BeginFrame();
        laterCamera->primary = false;
        cameras.Update(canvas, 0.0f);
        Check(world.GetCamera() != nullptr && world.GetCamera()->owner == cameraObject,
            "with none primary the first active camera draws");
        camera->primary = true;
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

    // §3.4 의 비용을 숫자로 남긴다. 계층이 깊고 Transform 이 마지막에 붙은,
    // 타입 조회에 가장 불리한 배치다.
    void TestDeepHierarchyLookupBudget()
    {
        JBro::Canvas canvas(JBro::CreateDefaultAllocator());
        constexpr int Depth = 8;
        constexpr int Chains = 25;

        for (int chain = 0; chain < Chains; ++chain)
        {
            JBro::GameObject* parent = nullptr;
            for (int level = 0; level < Depth; ++level)
            {
                JBro::GameObject* object = canvas.CreateObject("node");
                // Transform 을 마지막에 붙여 스캔이 가장 멀리 가게 만든다.
                canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(object);
                canvas.AttachComponent<JBro::Component::Collider2D>(object);
                canvas.AttachComponent<JBro::Component::Rigidbody2D>(object);
                canvas.AttachComponent<JBro::Component::Transform2D>(object);
                if (parent != nullptr)
                {
                    object->SetParent(parent);
                }
                parent = object;
            }
        }

        JBro::System::Transform2DSystem transforms;
        transforms.Initialize(canvas);
        JBro::Diagnostics::ComponentLookupCounters::Reset();
        transforms.Update(canvas, 0.0f);
        const std::size_t lookups = JBro::Diagnostics::ComponentLookupCounters::Get().lookups;
        const std::size_t dereferences = JBro::Diagnostics::ComponentLookupCounters::Get().dereferences;
        std::cout << "  [measure] " << (Depth * Chains) << "-node hierarchy, transform attached last: lookups="
            << lookups << " dereferences=" << dereferences << std::endl;

        // 노드마다 조회는 둘까지다 — 루트인지 보는 부모 조회 하나, 부모의 내림에서
        // 자식 Transform 을 찾는 하나. 루트는 부모가 없어 앞의 것이 일어나지 않는다.
        Check(lookups <= static_cast<std::size_t>(2 * Depth * Chains),
            "a hierarchy pass must not need more than two type lookups per node");
        // 이 비율이 조회 비용의 실체다. 찾는 컴포넌트가 목록의 몇 번째든
        // 제어 블록은 한 번만 따라가야 한다.
        Check(dereferences <= lookups + static_cast<std::size_t>(Depth * Chains),
            "a type lookup must dereference the component it wants, not the ones before it");
        transforms.Shutdown(canvas);
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
            renderWorld.GetSprite(0).owner == second,
            "render-world sprites must sort by render order");
        // P-5: 정렬은 아이템을 옮기지 않는다. 바뀌는 것은 순열뿐이다.
        Check(renderWorld.GetSubmittedSprites()[0].owner == first
            && renderWorld.GetSubmittedSprites()[1].owner == second,
            "sorting must leave submitted items in place and permute only the draw order");

        // 부호 있는 renderOrder 가 0 을 건너도 순서가 유지되어야 한다(키 패킹 검증).
        renderWorld.BeginFrame();
        const std::int32_t orders[] = {2147483647, 0, -2147483647 - 1, -1};
        for (std::int32_t order : orders)
        {
            JBro::SpriteRenderItem item;
            item.owner = first;
            item.renderOrder = order;
            Check(renderWorld.SubmitSprite(item), "key packing probe must submit");
        }
        renderWorld.EndFrame();
        Check(renderWorld.GetSpriteCount() == 4, "key packing probe must retain every sprite");
        for (std::size_t index = 1; index < renderWorld.GetSpriteCount(); ++index)
        {
            Check(renderWorld.GetSprite(index - 1).renderOrder <= renderWorld.GetSprite(index).renderOrder,
                "a packed sort key must keep signed render order ascending across zero");
        }

        // 레이어도 순서도 같으면 sourceId 가 결정한다. std::sort 는 안정하지 않으므로
        // 이 타이브레이크가 없으면 제출 순서(내림차순)가 그대로 남는다.
        renderWorld.BeginFrame();
        const JBro::InstanceId tiedIds[] = {40, 30, 20, 10};
        for (JBro::InstanceId id : tiedIds)
        {
            JBro::SpriteRenderItem item;
            item.owner = first;
            item.sourceId = id;
            item.layerOrder = 3;
            item.renderOrder = 7;
            Check(renderWorld.SubmitSprite(item), "tie-break probe must submit");
        }
        renderWorld.EndFrame();
        Check(renderWorld.GetSpriteCount() == 4, "tie-break probe must retain every sprite");
        for (std::size_t index = 1; index < renderWorld.GetSpriteCount(); ++index)
        {
            Check(renderWorld.GetSprite(index - 1).sourceId < renderWorld.GetSprite(index).sourceId,
                "sprites with an identical key must order by sourceId");
        }

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
    TestDisablingAParentTransformSkipsTheSubtreeInsteadOfMovingIt();
    TestAParentWithNoTransformLeavesTheChildAtItsOwnLocal();
    TestLayerOrderDrivesSpriteSorting();
    TestPhysicsGravityIntegration();
    TestPhysicsQueries();
    TestDeepHierarchyLookupBudget();
    TestRenderWorldCollection();
    TestRenderExtraction();
    TestRenderWorldCapacityLimit();
    std::cout << "Framework2D system tests passed.\n";
    return 0;
}
