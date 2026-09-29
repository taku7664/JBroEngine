#include <JBro/Framework2DSystem/System/Button2DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2DSystem/Rendering/CameraView2D.h>
#include <JBro/Framework2DSystem/Scripting/ScriptSystem.h>
#include <JBro/Framework2DSystem/System/Camera2DSystem.h>
#include <JBro/InputTypes/InputView.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>

#include <algorithm>
#include <cmath>

namespace JBro::System
{
    namespace
    {
        // 같은 레이어의 스크립트보다 먼저 받는다(`AddSystemInputHandler` 는 같은 순서면 시스템이 먼저다).
        constexpr std::int32_t ButtonInputOrder = 0;

        bool Apply(const Matrix3x2& m, float x, float y, float& outX, float& outY)
        {
            outX = x * m.m11 + y * m.m21 + m.m31;
            outY = x * m.m12 + y * m.m22 + m.m32;
            return std::isfinite(outX) && std::isfinite(outY);
        }

        bool Invert(const Matrix3x2& m, Matrix3x2& inverse)
        {
            const double determinant = static_cast<double>(m.m11) * m.m22 - static_cast<double>(m.m12) * m.m21;
            if (determinant == 0.0 || false == std::isfinite(determinant))
            {
                return false;
            }
            inverse.m11 = static_cast<float>(m.m22 / determinant);
            inverse.m12 = static_cast<float>(-m.m12 / determinant);
            inverse.m21 = static_cast<float>(-m.m21 / determinant);
            inverse.m22 = static_cast<float>(m.m11 / determinant);
            inverse.m31 = static_cast<float>((static_cast<double>(m.m21) * m.m32 - static_cast<double>(m.m22) * m.m31) / determinant);
            inverse.m32 = static_cast<float>((static_cast<double>(m.m12) * m.m31 - static_cast<double>(m.m11) * m.m32) / determinant);
            return true;
        }

        bool IsScreenLayer(const Layer& layer)
        {
            return layer.GetSpace() == LayerSpace::Screen;
        }

        const Color& TintFor(const Component::Button2D& button)
        {
            if (false == button.interactable)
            {
                return button.disabledTint;
            }
            if (button.pressed)
            {
                return button.pressedTint;
            }
            if (button.hovered)
            {
                return button.hoverTint;
            }
            return button.normalTint;
        }
    }

    int Button2DSystem::GetExecutionOrder() const
    {
        // 갱신 훅은 쓰지 않는다 - 일은 입력 체인(`OnInput`) 안에서 한다.
        return 150;
    }

    void Button2DSystem::SetScriptSystem(ScriptSystem* scripts)
    {
        if (m_scripts == scripts)
        {
            return;
        }
        if (m_scripts != nullptr)
        {
            m_scripts->RemoveSystemInputHandler(*this);
        }
        m_scripts = scripts;
        if (m_scripts != nullptr)
        {
            m_scripts->AddSystemInputHandler(*this, "UI", ButtonInputOrder);
        }
    }

    void Button2DSystem::SetScreenSpace(const ScreenSpaceFrame& frame)
    {
        m_screen = frame;
    }

    void Button2DSystem::OnInitialize(Canvas& canvas)
    {
        m_canvas = &canvas;
    }

    void Button2DSystem::OnShutdown(Canvas& canvas)
    {
        (void)canvas;
        SetScriptSystem(nullptr);
        m_canvas = nullptr;
        m_hovered = InvalidInstanceId;
        m_pressed = InvalidInstanceId;
        m_pointerOver = false;
        m_collectedScripts.Clear();
        m_scriptKeys.Clear();
        m_hookTargets.Clear();
    }

    bool Button2DSystem::PixelToLayer(const Layer& layer, float pixelX, float pixelY, Vector2& point) const
    {
        if (IsScreenLayer(layer))
        {
            return ScreenPixelToLayer(layer.GetScaleMode(), m_screen, pixelX, pixelY, point.x, point.y);
        }
        float nx = 0.0f;
        float ny = 0.0f;
        RenderCamera2D camera;
        CameraView2D view;
        if (m_canvas == nullptr || false == ScreenPixelToNormalized(m_screen, pixelX, pixelY, nx, ny)
            || false == Camera2DSystem::SelectCamera(*m_canvas, camera) || false == ComputeCameraView2D(camera, m_screen, view))
        {
            return false;
        }
        // 뷰 좌표 = -1..1 x 카메라의 반폭·반높이(그리기와 같은 `ComputeCameraView2D`, D-239). 월드 = 카메라 트랜스폼 x 뷰 좌표.
        Matrix3x2 cameraWorld;
        if (false == Invert(view.view, cameraWorld))
        {
            return false;
        }
        return Apply(cameraWorld, nx * view.halfWidth, ny * view.halfHeight, point.x, point.y);
    }

    bool Button2DSystem::LayerToPixel(const Layer& layer, Vector2 point, float& pixelX, float& pixelY) const
    {
        if (IsScreenLayer(layer))
        {
            return LayerToScreenPixel(layer.GetScaleMode(), m_screen, point.x, point.y, pixelX, pixelY);
        }
        RenderCamera2D camera;
        CameraView2D view;
        if (m_canvas == nullptr || false == Camera2DSystem::SelectCamera(*m_canvas, camera)
            || false == ComputeCameraView2D(camera, m_screen, view))
        {
            return false;
        }
        float vx = 0.0f;
        float vy = 0.0f;
        if (false == Apply(view.view, point.x, point.y, vx, vy))
        {
            return false;
        }
        return NormalizedToScreenPixel(m_screen, vx / view.halfWidth, vy / view.halfHeight, pixelX, pixelY);
    }

    bool Button2DSystem::ScreenToLayer(Vector2 pixel, Handle::GameObject object, Vector2& point) const
    {
        const Object::GameObject* found = Internal::GameObjectHandleAccess::Resolve(object);
        const Layer* layer = found != nullptr ? found->GetLayer() : nullptr;
        return layer != nullptr && PixelToLayer(*layer, pixel.x, pixel.y, point);
    }

    bool Button2DSystem::LayerToScreen(Vector2 point, Handle::GameObject object, Vector2& pixel) const
    {
        const Object::GameObject* found = Internal::GameObjectHandleAccess::Resolve(object);
        const Layer* layer = found != nullptr ? found->GetLayer() : nullptr;
        return layer != nullptr && LayerToPixel(*layer, point, pixel.x, pixel.y);
    }

    bool Button2DSystem::IsPointerOverButton() const
    {
        return m_pointerOver;
    }

    Button2DSystem::Pointer Button2DSystem::ReadPointer(InputView& input)
    {
        Pointer pointer;
        // 손가락이 있으면 첫 손가락이다. 뗀 프레임에도 한 번 남아 있어 떼기를 본다.
        const TouchState& touch = input.Touch();
        if (touch.count > 0)
        {
            const TouchPoint& first = touch.Get(0);
            pointer.present = true;
            pointer.touch = true;
            pointer.x = first.x;
            pointer.y = first.y;
            pointer.down = first.IsActive();
            pointer.pressed = first.phase == TouchPhase::Began;
            pointer.released = first.phase == TouchPhase::Ended || first.phase == TouchPhase::Cancelled;
            return pointer;
        }
        const MouseState& mouse = input.Mouse();
        if (mouse.hasPosition)
        {
            pointer.present = true;
            pointer.x = mouse.x;
            pointer.y = mouse.y;
            pointer.down = mouse.IsDown(MouseButton::Left);
            pointer.pressed = mouse.IsPressed(MouseButton::Left);
            pointer.released = mouse.IsReleased(MouseButton::Left);
        }
        return pointer;
    }

    bool Button2DSystem::HitTest(const Component::Button2D& button, Object::GameObject& owner, const Layer& layer, const Pointer& pointer) const
    {
        const auto* transform = m_canvas->FindComponentRaw<Component::Transform2D>(&owner);
        if (transform == nullptr || false == transform->worldValid)
        {
            return false;
        }
        Vector2 point;
        Matrix3x2 inverse;
        float localX = 0.0f;
        float localY = 0.0f;
        if (false == PixelToLayer(layer, pointer.x, pointer.y, point) || false == Invert(transform->world, inverse)
            || false == Apply(inverse, point.x, point.y, localX, localY))
        {
            return false;
        }
        const float halfWidth = std::fabs(button.size.x) * 0.5f;
        const float halfHeight = std::fabs(button.size.y) * 0.5f;
        return std::fabs(localX - button.offset.x) <= halfWidth && std::fabs(localY - button.offset.y) <= halfHeight;
    }

    void Button2DSystem::RefreshScriptKeys()
    {
        const std::uint64_t revision = m_canvas->GetScriptOrderRevision();
        if (revision == m_scriptRevision)
        {
            return;
        }
        m_canvas->CollectScripts(m_collectedScripts);
        m_scriptKeys.Clear();
        for (GameScriptBase* script : m_collectedScripts)
        {
            if (script != nullptr)
            {
                m_scriptKeys.Add(static_cast<const ComponentBase*>(script));
            }
        }
        std::sort(m_scriptKeys.begin(), m_scriptKeys.end());
        m_scriptRevision = revision;
    }

    void Button2DSystem::CallHook(Object::GameObject* object, Hook hook)
    {
        if (object == nullptr || false == object->IsActiveInHierarchy())
        {
            return;
        }
        RefreshScriptKeys();
        // 먼저 모은 뒤 부른다. 훅이 컴포넌트를 붙이거나 떼면 슬롯 배열이 흔들린다.
        m_hookTargets.Clear();
        for (const ComponentSlot& slot : object->GetComponents())
        {
            ComponentBase* component = slot.reference.TryGet();
            if (component != nullptr && component->IsActiveComponent()
                && std::binary_search(m_scriptKeys.begin(), m_scriptKeys.end(), static_cast<const ComponentBase*>(component)))
            {
                // 2D 프로젝트의 스크립트는 모두 GameScript2D 다(D-207).
                m_hookTargets.Add(static_cast<GameScript2D*>(static_cast<GameScriptBase*>(component)));
            }
        }
        for (GameScript2D* script : m_hookTargets)
        {
            switch (hook)
            {
            case Hook::Enter:
                script->OnPointerEnter();
                break;
            case Hook::Exit:
                script->OnPointerExit();
                break;
            case Hook::Down:
                script->OnPointerDown();
                break;
            case Hook::Up:
                script->OnPointerUp();
                break;
            case Hook::Click:
            default:
                script->OnClick();
                break;
            }
        }
    }

    InputResult Button2DSystem::OnInput(InputView& input)
    {
        m_pointerOver = false;
        if (m_canvas == nullptr)
        {
            return InputResult::Pass;
        }
        const Pointer pointer = ReadPointer(input);

        // 1. 포인터 아래의 가장 위 버튼, 그리고 지난 프레임의 호버·누름 오브젝트를 찾는다. 이번 프레임의 `clicked` 는 여기서 지운다.
        Object::GameObject* top = nullptr;
        Component::Button2D* topButton = nullptr;
        bool topScreen = false;
        LayerOrder topLayer = 0;
        std::int32_t topOrder = 0;
        Object::GameObject* hoveredBefore = nullptr;
        Object::GameObject* pressedBefore = nullptr;
        m_canvas->ForEach<Component::Button2D>([&](Component::Button2D& button)
        {
            button.clicked = false;
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(button);
            if (owner == nullptr)
            {
                return;
            }
            const InstanceId id = owner->GetInstanceId();
            if (id == m_hovered)
            {
                hoveredBefore = owner;
            }
            if (id == m_pressed)
            {
                pressedBefore = owner;
            }
            const Layer* layer = owner->GetLayer();
            // 꺼진 버튼·감춘 레이어는 없는 것이다. 꺼진 버튼(`interactable` 거짓)은 보이므로 포인터를 받는다.
            if (false == pointer.present || false == button.IsActiveComponent() || false == owner->IsActiveInHierarchy() || layer == nullptr
                || false == layer->IsVisible() || false == HitTest(button, *owner, *layer, pointer))
            {
                return;
            }
            const bool screen = IsScreenLayer(*layer);
            const auto* sprite = m_canvas->FindComponentRaw<Component::SpriteRenderer2D>(owner);
            const std::int32_t order = sprite != nullptr ? sprite->renderOrder : 0;
            const bool above = top == nullptr || (screen != topScreen ? screen
                : layer->GetOrder() != topLayer ? layer->GetOrder() > topLayer
                : order >= topOrder);
            if (above)
            {
                top = owner;
                topButton = &button;
                topScreen = screen;
                topLayer = layer->GetOrder();
                topOrder = order;
            }
        });

        // 2. 호버가 바뀌면 떠난 쪽이 먼저다.
        if (hoveredBefore != top)
        {
            CallHook(hoveredBefore, Hook::Exit);
            CallHook(top, Hook::Enter);
        }
        m_hovered = top != nullptr ? top->GetInstanceId() : InvalidInstanceId;

        // 3. 누르기와 떼기. 누른 버튼은 포인터가 벗어나도 뗄 때까지 쥔다 - 떼는 자리가 그 버튼 위여야 누름이다.
        Object::GameObject* pressed = pressedBefore;
        if (pointer.pressed && topButton != nullptr && topButton->interactable)
        {
            pressed = top;
            CallHook(pressed, Hook::Down);
        }
        if (pressed != nullptr && (pointer.released || false == pointer.present || false == pointer.down))
        {
            Object::GameObject* released = pressed;
            pressed = nullptr;
            CallHook(released, Hook::Up);
            if (pointer.released && released == top && topButton != nullptr && topButton->interactable)
            {
                topButton->clicked = true;
                CallHook(released, Hook::Click);
            }
        }
        m_pressed = pressed != nullptr ? pressed->GetInstanceId() : InvalidInstanceId;

        // 4. 상태 필드와 색. 훅이 버튼을 지웠을 수 있으니 다시 돈다.
        m_canvas->ForEach<Component::Button2D>([&](Component::Button2D& button)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(button);
            const InstanceId id = owner != nullptr ? owner->GetInstanceId() : InvalidInstanceId;
            button.hovered = id != InvalidInstanceId && id == m_hovered;
            button.pressed = id != InvalidInstanceId && id == m_pressed;
            if (button.tintSprite && owner != nullptr && button.IsActiveComponent())
            {
                if (auto* sprite = m_canvas->FindComponentRaw<Component::SpriteRenderer2D>(owner))
                {
                    sprite->tint = TintFor(button);
                }
            }
        });

        // 5. 버튼 위거나 쥐고 있으면 그 장치를 아래에서 지운다. 입력은 막지 않는다 - 키보드·게임패드는 지나간다.
        m_pointerOver = top != nullptr || m_pressed != InvalidInstanceId;
        // 둘 다 지운다 - 터치를 마우스로도 흉내 내는 플랫폼에서 같은 손가락이 마우스로 새어 나가지 않게.
        if (m_pointerOver)
        {
            input.Consume(InputDevice::Mouse);
            input.Consume(InputDevice::Touch);
        }
        return InputResult::Pass;
    }
}
