#include <JBro/Framework2DSystem/System/Button2DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Runtime/ScriptCallGuard.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/Slider2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Toggle2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2DSystem/Rendering/CameraView2D.h>
#include <JBro/Framework2DSystem/Scripting/ScriptSystem.h>
#include <JBro/Framework2DSystem/System/Camera2DSystem.h>
#include <JBro/InputTypes/InputView.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>

#include <algorithm>
#include <cmath>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace JBro::System
{
    namespace
    {
        // 같은 레이어의 스크립트보다 먼저 받는다(`AddSystemInputHandler` 는 같은 순서면 시스템이 먼저다).
        constexpr Int32 ButtonInputOrder = 0;

        Bool Apply(const Matrix3x2& m, Float x, Float y, Float& outX, Float& outY)
        {
            outX = x * m.m11 + y * m.m21 + m.m31;
            outY = x * m.m12 + y * m.m22 + m.m32;
            return std::isfinite(outX) && std::isfinite(outY);
        }

        Bool Invert(const Matrix3x2& m, Matrix3x2& inverse)
        {
            const double determinant = static_cast<double>(m.m11) * m.m22 - static_cast<double>(m.m12) * m.m21;
            if (determinant == 0.0 || false == std::isfinite(determinant))
            {
                return false;
            }
            inverse.m11 = static_cast<JBro::Float>(m.m22 / determinant);
            inverse.m12 = static_cast<JBro::Float>(-m.m12 / determinant);
            inverse.m21 = static_cast<JBro::Float>(-m.m21 / determinant);
            inverse.m22 = static_cast<JBro::Float>(m.m11 / determinant);
            inverse.m31 = static_cast<JBro::Float>((static_cast<double>(m.m21) * m.m32 - static_cast<double>(m.m22) * m.m31) / determinant);
            inverse.m32 = static_cast<JBro::Float>((static_cast<double>(m.m12) * m.m31 - static_cast<double>(m.m11) * m.m32) / determinant);
            return true;
        }

        Bool IsScreenLayer(const Layer& layer)
        {
            return layer.GetSpace() == LayerSpace::Screen;
        }

        enum class ButtonState : std::uint8_t { Normal, Hover, Pressed, Disabled };

        ButtonState StateOf(const Component::Button2D& button)
        {
            if (false == button.interactable)
            {
                return ButtonState::Disabled;
            }
            if (button.pressed)
            {
                return ButtonState::Pressed;
            }
            if (button.hovered)
            {
                return ButtonState::Hover;
            }
            return ButtonState::Normal;
        }

        const Color& TintFor(const Component::Button2D& button, ButtonState state)
        {
            switch (state)
            {
            case ButtonState::Disabled:
                return button.disabledTint;
            case ButtonState::Pressed:
                return button.pressedTint;
            case ButtonState::Hover:
                return button.hoverTint;
            case ButtonState::Normal:
            default:
                return button.normalTint;
            }
        }

        // 상태 칸이 비었으면(해석되지 않았으면) 보통 그림 그대로다.
        AssetHandle SpriteFor(const Component::Button2D& button, ButtonState state)
        {
            switch (state)
            {
            case ButtonState::Disabled:
                return button.disabledSprite;
            case ButtonState::Pressed:
                return button.pressedSprite;
            case ButtonState::Hover:
                return button.hoverSprite;
            case ButtonState::Normal:
            default:
                return {};
            }
        }

        // 전이를 거는 그림이다(D-351). `targetGraphic` 이 비면 같은 오브젝트의 스프라이트다.
        Component::SpriteRenderer2D* TargetGraphic(Canvas& canvas, Component::Button2D& button, Object::GameObject* owner)
        {
            if (button.targetGraphic.IsValid())
            {
                return Internal::ComponentHandleAccess::ResolveAs<Component::SpriteRenderer2D>(button.targetGraphic);
            }
            return owner != nullptr ? canvas.FindComponentRaw<Component::SpriteRenderer2D>(owner) : nullptr;
        }

        // 보통 상태로 돌아오면 떠 둔 스프라이트를 되돌린다. 전이가 `SpriteSwap` 이 아니게 되어도 같다.
        void RestoreSwap(Component::Button2D& button, Component::SpriteRenderer2D* graphic)
        {
            if (button.swapped && graphic != nullptr)
            {
                graphic->sprite = button.normalSprite;
            }
            button.swapped = false;
            button.normalSprite = {};
        }

        void ApplyTransition(Canvas& canvas, Component::Button2D& button, Object::GameObject* owner)
        {
            Component::SpriteRenderer2D* graphic = TargetGraphic(canvas, button, owner);
            const ButtonState state = StateOf(button);
            switch (button.transition)
            {
            case Component::ButtonTransition::ColorTint:
                RestoreSwap(button, graphic);
                if (graphic != nullptr)
                {
                    graphic->tint = TintFor(button, state);
                }
                break;
            case Component::ButtonTransition::SpriteSwap:
            {
                const AssetHandle wanted = SpriteFor(button, state);
                if (graphic == nullptr || wanted.generation == 0)
                {
                    RestoreSwap(button, graphic);
                    break;
                }
                if (false == button.swapped)
                {
                    button.normalSprite = graphic->sprite;
                    button.swapped = true;
                }
                graphic->sprite = wanted;
                break;
            }
            case Component::ButtonTransition::None:
            default:
                RestoreSwap(button, graphic);
                break;
            }
        }

        // **슬라이더의 셈**(D-351). 값은 사각형의 그 축에서 0 인 변에서 1 인 변까지의 비율이다.
        Float SliderValueAt(const Component::Slider2D& slider, Vector2 local)
        {
            const Float width = std::fabs(slider.size.x);
            const Float height = std::fabs(slider.size.y);
            Float value = 0.0f;
            switch (slider.direction)
            {
            case Component::SliderDirection::RightToLeft:
                value = Float(width > 0.0f ? (((slider.offset.x + width * 0.5f) - local.x) / width).Get() : 0.0f);
                break;
            case Component::SliderDirection::BottomToTop:
                value = Float(height > 0.0f ? ((local.y - (slider.offset.y - height * 0.5f)) / height).Get() : 0.0f);
                break;
            case Component::SliderDirection::TopToBottom:
                value = Float(height > 0.0f ? (((slider.offset.y + height * 0.5f) - local.y) / height).Get() : 0.0f);
                break;
            case Component::SliderDirection::LeftToRight:
            default:
                value = Float(width > 0.0f ? ((local.x - (slider.offset.x - width * 0.5f)) / width).Get() : 0.0f);
                break;
            }
            return Float(std::isfinite(value.Get()) ? Float::Clamp(value, 0.0f, 1.0f) : 0.0f);
        }

        Vector2 SliderHandleAt(const Component::Slider2D& slider)
        {
            const Float width = std::fabs(slider.size.x);
            const Float height = std::fabs(slider.size.y);
            const Float value = Float(std::isfinite(slider.value.Get()) ? Float::Clamp(slider.value, 0.0f, 1.0f) : 0.0f);
            switch (slider.direction)
            {
            case Component::SliderDirection::RightToLeft:
                return { slider.offset.x + width * 0.5f - value * width, slider.offset.y };
            case Component::SliderDirection::BottomToTop:
                return { slider.offset.x, slider.offset.y - height * 0.5f + value * height };
            case Component::SliderDirection::TopToBottom:
                return { slider.offset.x, slider.offset.y + height * 0.5f - value * height };
            case Component::SliderDirection::LeftToRight:
            default:
                return { slider.offset.x - width * 0.5f + value * width, slider.offset.y };
            }
        }
    }

    Int32 Button2DSystem::GetExecutionOrder() const
    {
        // 갱신 훅은 값에서 그림을 놓는다 - 트랜스폼 전파(100)보다 먼저여야 같은 프레임에 보인다. 포인터 일은 입력 체인(`OnInput`) 안에서 한다.
        return 50;
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
        m_hoveredKind = WidgetKind::None;
        m_pressed = InvalidInstanceId;
        m_pressedKind = WidgetKind::None;
        m_pointerOver = false;
        m_hookTargets.Clear();
    }

    Bool Button2DSystem::PixelToLayer(const Layer& layer, Float pixelX, Float pixelY, Vector2& point) const
    {
        if (IsScreenLayer(layer))
        {
            return ScreenPixelToLayer(layer.GetScaleMode(), m_screen, pixelX, pixelY, point.x, point.y);
        }
        Float nx = 0.0f;
        Float ny = 0.0f;
        RenderCamera2D camera;
        CameraView2D view;
        if (m_canvas == nullptr || false == ScreenPixelToNormalized(m_screen, pixelX, pixelY, nx, ny)
            || false == Camera2DSystem::SelectCamera(*m_canvas, camera) || false == ComputeCameraView2D(camera, m_screen, view))
        {
            return false;
        }
        // 뷰 좌표 = -1..1 x 카메라의 반폭·반높이(그리기와 같은 `ComputeCameraView2D`, D-239). 월드 = 카메라 트랜스폼 x 뷰 좌표.
        Matrix3x2 cameraWorld;
        if (false == Invert(view.view, cameraWorld)
            || false == Apply(cameraWorld, nx * view.halfWidth, ny * view.halfHeight, point.x, point.y))
        {
            return false;
        }
        // **패럴랙스 레이어는 그려진 만큼 옮겨진 자리에서 누른다**(D-286). 그리기와 같은 함수로 잰 양을 빼서 오브젝트의 월드로 돌린다.
        Float offsetX = 0.0f;
        Float offsetY = 0.0f;
        if (layer.GetParallax() != 1.0f && ComputeParallaxOffset2D(view.view, layer.GetParallax(), offsetX, offsetY))
        {
            point.x -= offsetX;
            point.y -= offsetY;
        }
        return true;
    }

    Bool Button2DSystem::LayerToPixel(const Layer& layer, Vector2 point, Float& pixelX, Float& pixelY) const
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
        Float vx = 0.0f;
        Float vy = 0.0f;
        Float offsetX = 0.0f;
        Float offsetY = 0.0f;
        if (layer.GetParallax() != 1.0f && ComputeParallaxOffset2D(view.view, layer.GetParallax(), offsetX, offsetY))
        {
            point.x += offsetX;
            point.y += offsetY;
        }
        if (false == Apply(view.view, point.x, point.y, vx, vy))
        {
            return false;
        }
        return NormalizedToScreenPixel(m_screen, vx / view.halfWidth, vy / view.halfHeight, pixelX, pixelY);
    }

    Bool Button2DSystem::ScreenToLayer(Vector2 pixel, Handle::GameObject object, Vector2& point) const
    {
        const Object::GameObject* found = Internal::GameObjectHandleAccess::Resolve(object);
        const Layer* layer = found != nullptr ? found->GetLayer() : nullptr;
        return layer != nullptr && PixelToLayer(*layer, pixel.x, pixel.y, point);
    }

    Bool Button2DSystem::LayerToScreen(Vector2 point, Handle::GameObject object, Vector2& pixel) const
    {
        const Object::GameObject* found = Internal::GameObjectHandleAccess::Resolve(object);
        const Layer* layer = found != nullptr ? found->GetLayer() : nullptr;
        return layer != nullptr && LayerToPixel(*layer, point, pixel.x, pixel.y);
    }

    Bool Button2DSystem::IsPointerOverButton() const
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

    Bool Button2DSystem::PointerToLocal(Object::GameObject& owner, const Layer& layer, const Pointer& pointer, Vector2& local) const
    {
        const auto* transform = m_canvas->FindComponentRaw<Component::Transform2D>(&owner);
        if (transform == nullptr || false == transform->worldValid)
        {
            return false;
        }
        Vector2 point;
        Matrix3x2 inverse;
        return PixelToLayer(layer, pointer.x, pointer.y, point) && Invert(transform->world, inverse)
            && Apply(inverse, point.x, point.y, local.x, local.y);
    }

    Bool Button2DSystem::HitRect(Object::GameObject& owner, const Layer& layer, Vector2 size, Vector2 offset, const Pointer& pointer, Vector2& local) const
    {
        if (false == PointerToLocal(owner, layer, pointer, local))
        {
            return false;
        }
        const Float halfWidth = std::fabs(size.x) * 0.5f;
        const Float halfHeight = std::fabs(size.y) * 0.5f;
        return std::fabs(local.x - offset.x) <= halfWidth && std::fabs(local.y - offset.y) <= halfHeight;
    }

    void Button2DSystem::CallHook(Object::GameObject* object, Hook hook)
    {
        if (object == nullptr || false == object->IsActiveInHierarchy())
        {
            return;
        }
        // 먼저 모은 뒤 부른다. 훅이 스크립트를 붙이거나 떼면 목록이 흔들린다.
        m_hookTargets.Clear();
        for (const ScriptSlot& slot : object->GetScripts())
        {
            GameScriptBase* script = slot.reference.TryGet();
            if (script != nullptr && script->IsActiveScript())
            {
                // 2D 프로젝트의 스크립트는 모두 GameScript2D 다(D-207).
                m_hookTargets.Add(static_cast<GameScript2D*>(script));
            }
        }
        for (GameScript2D* script : m_hookTargets)
        {
            // 스크립트 훅은 `ScriptCallGuard` 를 거친다(D-330).
            switch (hook)
            {
            case Hook::Enter:
                ScriptCallGuard::Call("OnPointerEnter", [script] { script->OnPointerEnter(); });
                break;
            case Hook::Exit:
                ScriptCallGuard::Call("OnPointerExit", [script] { script->OnPointerExit(); });
                break;
            case Hook::Down:
                ScriptCallGuard::Call("OnPointerDown", [script] { script->OnPointerDown(); });
                break;
            case Hook::Up:
                ScriptCallGuard::Call("OnPointerUp", [script] { script->OnPointerUp(); });
                break;
            case Hook::ValueChanged:
                ScriptCallGuard::Call("OnValueChanged", [script] { script->OnValueChanged(); });
                break;
            case Hook::Click:
            default:
                ScriptCallGuard::Call("OnClick", [script] { script->OnClick(); });
                break;
            }
        }
    }

    void Button2DSystem::PlaceWidgets(Canvas& canvas)
    {
        // 슬라이더: 손잡이(자식 오브젝트)를 값의 자리에, 채우기 스프라이트의 비율을 값으로.
        canvas.ForEach<Component::Slider2D>([&](Component::Slider2D& slider)
        {
            if (false == slider.IsActiveComponent())
            {
                return;
            }
            if (Object::GameObject* handle = Internal::GameObjectHandleAccess::Resolve(slider.handle))
            {
                if (auto* place = canvas.FindComponentRaw<Component::Transform2D>(handle))
                {
                    place->position = SliderHandleAt(slider);
                }
            }
            if (auto* fill = Internal::ComponentHandleAccess::ResolveAs<Component::SpriteRenderer2D>(slider.fill))
            {
                fill->fillAmount = Float(std::isfinite(slider.value.Get()) ? Float::Clamp(slider.value, 0.0f, 1.0f) : 0.0f);
            }
        });
        // 토글: 표시 스프라이트는 켜졌을 때만 보인다.
        canvas.ForEach<Component::Toggle2D>([&](Component::Toggle2D& toggle)
        {
            if (false == toggle.IsActiveComponent())
            {
                return;
            }
            if (auto* mark = Internal::ComponentHandleAccess::ResolveAs<Component::SpriteRenderer2D>(toggle.checkmark))
            {
                mark->visible = toggle.isOn;
            }
        });
    }

    void Button2DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        (void)deltaTime;
        PlaceWidgets(canvas);
    }

    InputResult Button2DSystem::OnInput(InputView& input)
    {
        m_pointerOver = false;
        if (m_canvas == nullptr)
        {
            return InputResult::Pass;
        }
        const Pointer pointer = ReadPointer(input);

        // 1. 포인터 아래의 가장 위 위젯, 그리고 지난 프레임의 호버·누름 오브젝트를 찾는다. 이번 프레임의 `clicked`·`changed` 는 여기서 지운다.
        struct Top
        {
            Object::GameObject* owner = nullptr;
            WidgetKind kind = WidgetKind::None;
            Bool interactable = true;
            Bool screen = false;
            LayerOrder layer = 0;
            Int32 order = 0;
            Int32 sequence = 0;
            Vector2 local;
        };
        Top top;
        Object::GameObject* hoveredBefore = nullptr;
        Object::GameObject* pressedBefore = nullptr;
        const auto consider = [&](Object::GameObject* owner, WidgetKind kind, Bool interactable, Vector2 size, Vector2 offset, Bool active) {
            if (owner == nullptr)
            {
                return;
            }
            const InstanceId id = owner->GetInstanceId();
            if (id == m_hovered && kind == m_hoveredKind)
            {
                hoveredBefore = owner;
            }
            if (id == m_pressed && kind == m_pressedKind)
            {
                pressedBefore = owner;
            }
            const Layer* layer = owner->GetLayer();
            // 꺼진 위젯·감춘 레이어는 없는 것이다. 꺼진 위젯(`interactable` 거짓)은 보이므로 포인터를 받는다.
            Vector2 local;
            if (false == pointer.present || false == active || false == owner->IsActiveInHierarchy() || layer == nullptr
                || false == layer->IsVisible() || false == HitRect(*owner, *layer, size, offset, pointer, local))
            {
                return;
            }
            const Bool screen = IsScreenLayer(*layer);
            const auto* sprite = m_canvas->FindComponentRaw<Component::SpriteRenderer2D>(owner);
            const Int32 order = sprite != nullptr ? sprite->renderOrder : Int32(0);
            const Int32 sequence = sprite != nullptr ? sprite->drawSequence : Int32(0);
            const Bool above = top.owner == nullptr || (screen != top.screen ? screen
                : layer->GetOrder() != top.layer ? Bool(layer->GetOrder() > top.layer)
                : order != top.order ? Bool(order > top.order)
                : Bool(sequence >= top.sequence));
            if (above)
            {
                top.owner = owner;
                top.kind = kind;
                top.interactable = interactable;
                top.screen = screen;
                top.layer = layer->GetOrder();
                top.order = order;
                top.sequence = sequence;
                top.local = local;
            }
        };
        m_canvas->ForEach<Component::Button2D>([&](Component::Button2D& button)
        {
            button.clicked = false;
            consider(Internal::CanvasAccess::GetOwner(button), WidgetKind::Button, button.interactable, button.size, button.offset, button.IsActiveComponent());
        });
        m_canvas->ForEach<Component::Toggle2D>([&](Component::Toggle2D& toggle)
        {
            toggle.changed = false;
            consider(Internal::CanvasAccess::GetOwner(toggle), WidgetKind::Toggle, toggle.interactable, toggle.size, toggle.offset, toggle.IsActiveComponent());
        });
        m_canvas->ForEach<Component::Slider2D>([&](Component::Slider2D& slider)
        {
            slider.changed = false;
            consider(Internal::CanvasAccess::GetOwner(slider), WidgetKind::Slider, slider.interactable, slider.size, slider.offset, slider.IsActiveComponent());
        });

        // 2. 호버가 바뀌면 떠난 쪽이 먼저다. 들어옴·나감 훅은 버튼의 것이다.
        if (hoveredBefore != top.owner || m_hoveredKind != top.kind)
        {
            if (m_hoveredKind == WidgetKind::Button)
            {
                CallHook(hoveredBefore, Hook::Exit);
            }
            if (top.kind == WidgetKind::Button)
            {
                CallHook(top.owner, Hook::Enter);
            }
        }
        m_hovered = top.owner != nullptr ? top.owner->GetInstanceId() : InvalidInstanceId;
        m_hoveredKind = top.kind;

        // 3. 누르기·끌기·떼기. 누른 위젯은 포인터가 벗어나도 뗄 때까지 쥔다 - 떼는 자리가 그 위젯 위여야 누름이다.
        Object::GameObject* pressed = pressedBefore;
        WidgetKind pressedKind = pressedBefore != nullptr ? m_pressedKind : WidgetKind::None;
        if (pointer.pressed && top.owner != nullptr && top.interactable)
        {
            pressed = top.owner;
            pressedKind = top.kind;
            if (top.kind == WidgetKind::Button)
            {
                CallHook(pressed, Hook::Down);
            }
        }
        // 슬라이더는 쥔 동안 포인터의 자리가 곧 값이다 - 사각형을 벗어나도 그 축의 끝에 붙는다.
        if (pressed != nullptr && pressedKind == WidgetKind::Slider && pointer.present && pointer.down)
        {
            auto* slider = m_canvas->FindComponentRaw<Component::Slider2D>(pressed);
            const Layer* layer = pressed->GetLayer();
            Vector2 local;
            if (slider != nullptr && layer != nullptr && PointerToLocal(*pressed, *layer, pointer, local))
            {
                const Float value = SliderValueAt(*slider, local);
                if (value != slider->value)
                {
                    slider->value = value;
                    slider->changed = true;
                    CallHook(pressed, Hook::ValueChanged);
                }
            }
        }
        if (pressed != nullptr && (pointer.released || false == pointer.present || false == pointer.down))
        {
            Object::GameObject* released = pressed;
            const WidgetKind releasedKind = pressedKind;
            pressed = nullptr;
            pressedKind = WidgetKind::None;
            if (releasedKind == WidgetKind::Button)
            {
                CallHook(released, Hook::Up);
            }
            const Bool onTop = pointer.released && released == top.owner && releasedKind == top.kind && top.interactable;
            if (onTop && releasedKind == WidgetKind::Button)
            {
                if (auto* button = m_canvas->FindComponentRaw<Component::Button2D>(released))
                {
                    button->clicked = true;
                }
                CallHook(released, Hook::Click);
            }
            else if (onTop && releasedKind == WidgetKind::Toggle)
            {
                if (auto* toggle = m_canvas->FindComponentRaw<Component::Toggle2D>(released))
                {
                    toggle->isOn = false == toggle->isOn;
                    toggle->changed = true;
                    CallHook(released, Hook::ValueChanged);
                }
            }
        }
        m_pressed = pressed != nullptr ? pressed->GetInstanceId() : InvalidInstanceId;
        m_pressedKind = pressedKind;

        // 4. 상태 필드와 전이. 훅이 위젯을 지웠을 수 있으니 다시 돈다.
        m_canvas->ForEach<Component::Button2D>([&](Component::Button2D& button)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(button);
            const InstanceId id = owner != nullptr ? owner->GetInstanceId() : InvalidInstanceId;
            button.hovered = id != InvalidInstanceId && id == m_hovered && m_hoveredKind == WidgetKind::Button;
            button.pressed = id != InvalidInstanceId && id == m_pressed && m_pressedKind == WidgetKind::Button;
            if (button.IsActiveComponent())
            {
                ApplyTransition(*m_canvas, button, owner);
            }
            else
            {
                RestoreSwap(button, TargetGraphic(*m_canvas, button, owner));
            }
        });
        m_canvas->ForEach<Component::Toggle2D>([&](Component::Toggle2D& toggle)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(toggle);
            const InstanceId id = owner != nullptr ? owner->GetInstanceId() : InvalidInstanceId;
            toggle.hovered = id != InvalidInstanceId && id == m_hovered && m_hoveredKind == WidgetKind::Toggle;
            toggle.pressed = id != InvalidInstanceId && id == m_pressed && m_pressedKind == WidgetKind::Toggle;
        });
        m_canvas->ForEach<Component::Slider2D>([&](Component::Slider2D& slider)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(slider);
            const InstanceId id = owner != nullptr ? owner->GetInstanceId() : InvalidInstanceId;
            slider.hovered = id != InvalidInstanceId && id == m_hovered && m_hoveredKind == WidgetKind::Slider;
            slider.pressed = id != InvalidInstanceId && id == m_pressed && m_pressedKind == WidgetKind::Slider;
        });

        // 5. 위젯 위거나 쥐고 있으면 그 장치를 아래에서 지운다. 입력은 막지 않는다 - 키보드·게임패드는 지나간다.
        m_pointerOver = top.owner != nullptr || m_pressed != InvalidInstanceId;
        // 둘 다 지운다 - 터치를 마우스로도 흉내 내는 플랫폼에서 같은 손가락이 마우스로 새어 나가지 않게.
        if (m_pointerOver)
        {
            input.Consume(InputDevice::Mouse);
            input.Consume(InputDevice::Touch);
        }
        return InputResult::Pass;
    }
}
