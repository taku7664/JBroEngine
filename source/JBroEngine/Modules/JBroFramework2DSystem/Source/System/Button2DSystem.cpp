#include <JBro/Framework2DSystem/System/Button2DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Runtime/ScriptCallGuard.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/InputField2D.h>
#include <JBro/Framework2D/Component/Mask2D.h>
#include <JBro/Framework2D/Component/ScrollView2D.h>
#include <JBro/Framework2D/Component/Slider2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2D/Component/Toggle2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2DSystem/Rendering/CameraView2D.h>
#include <JBro/Framework2DSystem/Rendering/ClipRect2D.h>
#include <JBro/Framework2DSystem/Scripting/ScriptSystem.h>
#include <JBro/Framework2DSystem/System/Camera2DSystem.h>
#include <JBro/Framework2DSystem/System/Text2DSystem.h>
#include <JBro/InputTypes/InputView.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Runtime/TextStore.h>

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

        // 게임패드·키보드로 받은 포커스가 있는 버튼은 호버와 같이 보인다(D-352) - 고른 것을 눈으로 알아야 한다.
        ButtonState StateOf(const Component::Button2D& button, Bool focusVisible)
        {
            if (false == button.interactable)
            {
                return ButtonState::Disabled;
            }
            if (button.pressed)
            {
                return ButtonState::Pressed;
            }
            if (button.hovered || (button.focused && focusVisible))
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

        void ApplyTransition(Canvas& canvas, Component::Button2D& button, Object::GameObject* owner, Bool focusVisible)
        {
            Component::SpriteRenderer2D* graphic = TargetGraphic(canvas, button, owner);
            const ButtonState state = StateOf(button, focusVisible);
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

        // 한 축을 뷰포트가 늘 내용 안에 들게 가둔다. 내용이 뷰포트보다 작은 축은 건드리지 않는다.
        Bool ClampAxis(Float& position, Float center, Float viewport, Float content)
        {
            const Float half = std::fabs(viewport) * 0.5f;
            const Float contentHalf = std::fabs(content) * 0.5f;
            if (contentHalf <= half)
            {
                return false;
            }
            const Float low = center + half - contentHalf;
            const Float high = center - half + contentHalf;
            if (position < low)
            {
                position = low;
                return true;
            }
            if (position > high)
            {
                position = high;
                return true;
            }
            return false;
        }

        // **UTF-8**(D-352). 코드포인트 하나를 붙이고, 마지막 코드포인트를 뗀다. 글자 수는 코드포인트 수다.
        void AppendCodePoint(String& text, UInt32 codePoint)
        {
            const std::uint32_t value = codePoint.Get();
            if (value < 0x80)
            {
                text.push_back(static_cast<char>(value));
            }
            else if (value < 0x800)
            {
                text.push_back(static_cast<char>(0xC0 | (value >> 6)));
                text.push_back(static_cast<char>(0x80 | (value & 0x3F)));
            }
            else if (value < 0x10000)
            {
                text.push_back(static_cast<char>(0xE0 | (value >> 12)));
                text.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
                text.push_back(static_cast<char>(0x80 | (value & 0x3F)));
            }
            else if (value < 0x110000)
            {
                text.push_back(static_cast<char>(0xF0 | (value >> 18)));
                text.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
                text.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
                text.push_back(static_cast<char>(0x80 | (value & 0x3F)));
            }
        }

        void RemoveLastCodePoint(String& text)
        {
            while (false == text.empty())
            {
                const unsigned char last = static_cast<unsigned char>(text.back());
                text.pop_back();
                // 이어지는 바이트(10xxxxxx)가 아니면 글자의 첫 바이트다.
                if ((last & 0xC0) != 0x80)
                {
                    break;
                }
            }
        }

        UInt32 CountCodePoints(const String& text)
        {
            UInt32 count = 0;
            for (const char character : text)
            {
                if ((static_cast<unsigned char>(character) & 0xC0) != 0x80)
                {
                    ++count;
                }
            }
            return count;
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
        m_focused = InvalidInstanceId;
        m_focusedKind = WidgetKind::None;
        m_padPressed = false;
        m_stickX = 0.0f;
        m_stickY = 0.0f;
        m_scrollCandidate = InvalidInstanceId;
        m_scrollDragging = false;
        m_dragDelta = {};
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

    Bool Button2DSystem::PointerToLocal(Object::GameObject& owner, const Layer& layer, const Pointer& pointer, Vector2& local, Vector2* layerPoint) const
    {
        const auto* transform = m_canvas->FindComponentRaw<Component::Transform2D>(&owner);
        if (transform == nullptr || false == transform->worldValid)
        {
            return false;
        }
        Vector2 point;
        Matrix3x2 inverse;
        if (false == PixelToLayer(layer, pointer.x, pointer.y, point) || false == Invert(transform->world, inverse)
            || false == Apply(inverse, point.x, point.y, local.x, local.y))
        {
            return false;
        }
        if (layerPoint != nullptr)
        {
            *layerPoint = point;
        }
        return true;
    }

    Bool Button2DSystem::HitRect(Object::GameObject& owner, const Layer& layer, Vector2 size, Vector2 offset, const Pointer& pointer, Vector2& local) const
    {
        Vector2 point;
        if (false == PointerToLocal(owner, layer, pointer, local, &point))
        {
            return false;
        }
        const Float halfWidth = std::fabs(size.x) * 0.5f;
        const Float halfHeight = std::fabs(size.y) * 0.5f;
        if (std::fabs(local.x - offset.x) > halfWidth || std::fabs(local.y - offset.y) > halfHeight)
        {
            return false;
        }
        // **마스크 밖은 없는 것이다**(D-351 3 단계). 그리는 쪽과 같은 사각형이다.
        Float clip[4];
        return false == ComputeClipRect2D(*m_canvas, &owner, clip) || ClipRectContains(clip, point);
    }

    Bool Button2DSystem::WidgetPixel(Object::GameObject& owner, Float& pixelX, Float& pixelY) const
    {
        const auto* transform = m_canvas->FindComponentRaw<Component::Transform2D>(&owner);
        const Layer* layer = owner.GetLayer();
        return transform != nullptr && transform->worldValid && layer != nullptr && LayerToPixel(*layer, transform->worldPosition, pixelX, pixelY);
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
            case Hook::Submit:
                ScriptCallGuard::Call("OnSubmit", [script] { script->OnSubmit(); });
                break;
            case Hook::Click:
            default:
                ScriptCallGuard::Call("OnClick", [script] { script->OnClick(); });
                break;
            }
        }
    }

    Object::GameObject* Button2DSystem::FindScrollAncestor(Object::GameObject* object) const
    {
        for (Object::GameObject* at = object; at != nullptr; at = at->GetParent())
        {
            const auto* scroll = m_canvas->FindComponentRaw<Component::ScrollView2D>(at);
            if (scroll != nullptr && scroll->IsActiveComponent() && m_canvas->FindComponentRaw<Component::Mask2D>(at) != nullptr)
            {
                return at;
            }
        }
        return nullptr;
    }

    void Button2DSystem::ScrollBy(Object::GameObject& owner, Component::ScrollView2D& scroll, Vector2 delta, Bool& clampedX, Bool& clampedY)
    {
        clampedX = false;
        clampedY = false;
        const auto* mask = m_canvas->FindComponentRaw<Component::Mask2D>(&owner);
        Object::GameObject* content = Internal::GameObjectHandleAccess::Resolve(scroll.content);
        auto* place = content != nullptr ? m_canvas->FindComponentRaw<Component::Transform2D>(content) : nullptr;
        if (mask == nullptr || place == nullptr)
        {
            return;
        }
        // 내용은 스크롤 뷰의 자식이다 - 자리는 뷰포트와 같은 로컬 좌표다.
        if (scroll.horizontal)
        {
            place->position.x += delta.x;
            clampedX = ClampAxis(place->position.x, mask->offset.x, mask->size.x, scroll.contentSize.x);
        }
        if (scroll.vertical)
        {
            place->position.y += delta.y;
            clampedY = ClampAxis(place->position.y, mask->offset.y, mask->size.y, scroll.contentSize.y);
        }
    }

    void Button2DSystem::SetFocus(Object::GameObject* object, WidgetKind kind, Bool visible)
    {
        m_focused = object != nullptr ? object->GetInstanceId() : InvalidInstanceId;
        m_focusedKind = object != nullptr ? kind : WidgetKind::None;
        m_focusVisible = object != nullptr && visible;
        m_padPressed = false;
    }

    void Button2DSystem::NavigateFocus(Int32 directionX, Int32 directionY)
    {
        // 지금 포커스의 자리. 없으면 왼쪽 위의 위젯부터다.
        Float fromX = 0.0f;
        Float fromY = 0.0f;
        Bool hasFrom = false;
        Object::GameObject* best = nullptr;
        WidgetKind bestKind = WidgetKind::None;
        Float bestScore = 0.0f;
        const auto consider = [&](Object::GameObject* owner, WidgetKind kind, Bool interactable, Bool active) {
            if (owner == nullptr || false == active || false == interactable || false == owner->IsActiveInHierarchy()
                || owner->GetLayer() == nullptr || false == owner->GetLayer()->IsVisible())
            {
                return;
            }
            if (owner->GetInstanceId() == m_focused && kind == m_focusedKind)
            {
                hasFrom = WidgetPixel(*owner, fromX, fromY);
                return;
            }
            Float x = 0.0f;
            Float y = 0.0f;
            if (false == WidgetPixel(*owner, x, y))
            {
                return;
            }
            Float score = 0.0f;
            if (m_focused == InvalidInstanceId)
            {
                // 왼쪽 위가 먼저다 - 줄(y)이 먼저고 같은 줄에서는 왼쪽이다.
                score = y * 4096.0f + x;
            }
            else
            {
                // 그 방향으로 간 거리에 옆으로 벗어난 거리를 두 배로 더한다. 뒤는 안 된다. 자리는 아래에서 채운다.
                score = -1.0f;
                (void)score;
            }
            struct Candidate { Object::GameObject* owner; WidgetKind kind; Float x; Float y; Float score; };
            Candidate candidate{ owner, kind, x, y, score };
            if (m_focused == InvalidInstanceId)
            {
                if (best == nullptr || candidate.score < bestScore)
                {
                    best = owner;
                    bestKind = kind;
                    bestScore = candidate.score;
                }
                return;
            }
            // 포커스가 있으면 두 번째 걸음에서 센다 - 지금 자리를 먼저 알아야 한다. 후보는 다시 돌며 본다.
        };
        const auto considerAll = [&]() {
            m_canvas->ForEach<Component::Button2D>([&](Component::Button2D& button) {
                consider(Internal::CanvasAccess::GetOwner(button), WidgetKind::Button, button.interactable, button.IsActiveComponent());
            });
            m_canvas->ForEach<Component::Toggle2D>([&](Component::Toggle2D& toggle) {
                consider(Internal::CanvasAccess::GetOwner(toggle), WidgetKind::Toggle, toggle.interactable, toggle.IsActiveComponent());
            });
            m_canvas->ForEach<Component::Slider2D>([&](Component::Slider2D& slider) {
                consider(Internal::CanvasAccess::GetOwner(slider), WidgetKind::Slider, slider.interactable, slider.IsActiveComponent());
            });
            m_canvas->ForEach<Component::InputField2D>([&](Component::InputField2D& field) {
                consider(Internal::CanvasAccess::GetOwner(field), WidgetKind::Field, field.interactable, field.IsActiveComponent());
            });
        };
        considerAll();
        if (m_focused == InvalidInstanceId)
        {
            SetFocus(best, bestKind, true);
            return;
        }
        if (false == hasFrom)
        {
            // 포커스 위젯이 사라졌거나 자리를 모른다 - 비우고 다음에 왼쪽 위부터다.
            SetFocus(nullptr, WidgetKind::None);
            return;
        }
        // 두 번째 걸음: 방향의 가장 가까운 것.
        best = nullptr;
        const Float dirX = static_cast<JBro::Float>(directionX.Get());
        const Float dirY = static_cast<JBro::Float>(directionY.Get());
        const auto pick = [&](Object::GameObject* owner, WidgetKind kind, Bool interactable, Bool active) {
            if (owner == nullptr || false == active || false == interactable || false == owner->IsActiveInHierarchy()
                || owner->GetLayer() == nullptr || false == owner->GetLayer()->IsVisible()
                || (owner->GetInstanceId() == m_focused && kind == m_focusedKind))
            {
                return;
            }
            Float x = 0.0f;
            Float y = 0.0f;
            if (false == WidgetPixel(*owner, x, y))
            {
                return;
            }
            const Float along = (x - fromX) * dirX + (y - fromY) * dirY;
            if (along <= 0.5f)
            {
                return;
            }
            const Float across = std::fabs((x - fromX) * dirY - (y - fromY) * dirX);
            const Float score = along + across * 2.0f;
            if (best == nullptr || score < bestScore)
            {
                best = owner;
                bestKind = kind;
                bestScore = score;
            }
        };
        m_canvas->ForEach<Component::Button2D>([&](Component::Button2D& button) {
            pick(Internal::CanvasAccess::GetOwner(button), WidgetKind::Button, button.interactable, button.IsActiveComponent());
        });
        m_canvas->ForEach<Component::Toggle2D>([&](Component::Toggle2D& toggle) {
            pick(Internal::CanvasAccess::GetOwner(toggle), WidgetKind::Toggle, toggle.interactable, toggle.IsActiveComponent());
        });
        m_canvas->ForEach<Component::Slider2D>([&](Component::Slider2D& slider) {
            pick(Internal::CanvasAccess::GetOwner(slider), WidgetKind::Slider, slider.interactable, slider.IsActiveComponent());
        });
        m_canvas->ForEach<Component::InputField2D>([&](Component::InputField2D& field) {
            pick(Internal::CanvasAccess::GetOwner(field), WidgetKind::Field, field.interactable, field.IsActiveComponent());
        });
        if (best != nullptr)
        {
            SetFocus(best, bestKind, true);
        }
    }

    void Button2DSystem::TypeInto(Object::GameObject& owner, Component::InputField2D& field, InputView& input)
    {
        const KeyboardState& keyboard = input.Keyboard();
        TextStore& store = TextStore::Get();
        const ArrayView<const char> current = store.GetText(field.text);
        m_textScratch.assign(current.Data() != nullptr ? current.Data() : "", current.Size());
        Bool changed = false;
        UInt32 count = CountCodePoints(m_textScratch);
        for (UInt32 index = 0; index < keyboard.GetTextLength(); ++index)
        {
            const UInt32 codePoint = keyboard.GetText(index);
            // 제어 글자(개행·탭 포함)는 글자가 아니다 - Enter 는 키로 본다.
            if (codePoint < 0x20 || codePoint == 0x7F)
            {
                continue;
            }
            if (field.maxLength > 0 && count >= static_cast<JBro::UInt32>(field.maxLength.Get()))
            {
                break;
            }
            AppendCodePoint(m_textScratch, codePoint);
            ++count;
            changed = true;
        }
        if (keyboard.IsPressed(Key::Backspace) && false == m_textScratch.empty())
        {
            RemoveLastCodePoint(m_textScratch);
            changed = true;
        }
        if (changed)
        {
            store.Assign(field.text, m_textScratch.data(), m_textScratch.size());
            field.changed = true;
            CallHook(&owner, Hook::ValueChanged);
        }
        if (keyboard.IsPressed(Key::Enter))
        {
            field.submitted = true;
            CallHook(&owner, Hook::Submit);
        }
        if (keyboard.IsPressed(Key::Escape))
        {
            SetFocus(nullptr, WidgetKind::None);
        }
    }

    void Button2DSystem::PlaceWidgets(Canvas& canvas, Float deltaTime)
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
        // 글자 칸: 글자가 바뀌었으면 보이는 텍스트에 옮기고, 커서는 글자 끝에서 깜빡인다(D-352).
        System::Text2DSystem* texts = canvas.GetSystems().FindSystem<System::Text2DSystem>();
        canvas.ForEach<Component::InputField2D>([&](Component::InputField2D& field)
        {
            if (false == field.IsActiveComponent())
            {
                return;
            }
            TextStore& store = TextStore::Get();
            const UInt32 revision = store.GetRevision(field.text);
            auto* display = Internal::ComponentHandleAccess::ResolveAs<Component::Text2D>(field.display);
            if (display != nullptr && revision != field.mirroredRevision)
            {
                const ArrayView<const char> text = store.GetText(field.text);
                store.Assign(display->text, text.Data() != nullptr ? text.Data() : "", text.Size());
                field.mirroredRevision = revision;
            }
            auto* caret = Internal::ComponentHandleAccess::ResolveAs<Component::SpriteRenderer2D>(field.caret);
            if (caret == nullptr)
            {
                return;
            }
            if (false == field.focused)
            {
                caret->visible = false;
                field.blinkTime = 0.0f;
                return;
            }
            field.blinkTime += deltaTime;
            const Float rate = field.blinkRate;
            caret->visible = rate <= 0.0f || std::fmod(field.blinkTime.Get(), rate.Get() * 2.0f) < rate.Get();
            // 커서는 보이는 텍스트의 오른쪽 끝, 세로 가운데다(그 오브젝트의 로컬 - 커서는 그 자식이어야 한다).
            Float minX = 0.0f;
            Float minY = 0.0f;
            Float maxX = 0.0f;
            Float maxY = 0.0f;
            Object::GameObject* caretObject = Internal::CanvasAccess::GetOwner(*caret);
            auto* place = caretObject != nullptr ? canvas.FindComponentRaw<Component::Transform2D>(caretObject) : nullptr;
            if (place != nullptr)
            {
                if (display != nullptr && texts != nullptr && texts->GetLocalBounds(display->GetInstanceId(), minX, minY, maxX, maxY))
                {
                    place->position = { maxX, (minY + maxY) * 0.5f };
                }
                else
                {
                    place->position = {};
                }
            }
        });
    }

    void Button2DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        PlaceWidgets(canvas, deltaTime);
        // 스크롤: 끄는 중이면 이번 프레임에 끈 양이 속도이고, 놓았으면 속도로 미끄러지다 멈춘다. 끝에 닿은 축은 속도가 0 이다.
        canvas.ForEach<Component::ScrollView2D>([&](Component::ScrollView2D& scroll)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(scroll);
            if (owner == nullptr || false == scroll.IsActiveComponent())
            {
                return;
            }
            const Bool dragging = m_scrollDragging && owner->GetInstanceId() == m_pressed && m_pressedKind == WidgetKind::Scroll;
            scroll.dragging = dragging;
            if (dragging)
            {
                if (deltaTime > 0.0f)
                {
                    scroll.velocity = { m_dragDelta.x / deltaTime, m_dragDelta.y / deltaTime };
                }
                m_dragDelta = {};
                return;
            }
            if ((scroll.velocity.x == 0.0f && scroll.velocity.y == 0.0f) || deltaTime <= 0.0f)
            {
                return;
            }
            Bool clampedX = false;
            Bool clampedY = false;
            ScrollBy(*owner, scroll, { scroll.velocity.x * deltaTime, scroll.velocity.y * deltaTime }, clampedX, clampedY);
            const Float rate = Float::Clamp(scroll.decelerationRate, 0.0f, 1.0f);
            const Float keep = rate > 0.0f ? Float(std::pow(rate.Get(), deltaTime.Get())) : Float(0.0f);
            scroll.velocity.x = clampedX || false == scroll.horizontal ? Float(0.0f) : scroll.velocity.x * keep;
            scroll.velocity.y = clampedY || false == scroll.vertical ? Float(0.0f) : scroll.velocity.y * keep;
            // 느려지면 멈춘다 - 영원히 꼬물거리지 않는다.
            if (std::fabs(scroll.velocity.x) < 1.0f)
            {
                scroll.velocity.x = 0.0f;
            }
            if (std::fabs(scroll.velocity.y) < 1.0f)
            {
                scroll.velocity.y = 0.0f;
            }
        });
    }

    InputResult Button2DSystem::OnInput(InputView& input)
    {
        m_pointerOver = false;
        if (m_canvas == nullptr)
        {
            return InputResult::Pass;
        }
        const Pointer pointer = ReadPointer(input);

        // 0. 스크립트가 글자 칸에 포커스를 줬으면 받는다(`focused` 를 참으로 쓴 것).
        m_canvas->ForEach<Component::InputField2D>([&](Component::InputField2D& field)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(field);
            if (owner != nullptr && field.focused && field.IsActiveComponent()
                && (owner->GetInstanceId() != m_focused || m_focusedKind != WidgetKind::Field))
            {
                SetFocus(owner, WidgetKind::Field);
            }
        });

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
        Object::GameObject* focusedObject = nullptr;
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
            if (id == m_focused && kind == m_focusedKind && active && owner->IsActiveInHierarchy())
            {
                focusedObject = owner;
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
            // 차례가 같으면 뒤에 온 것이 위다. 다만 스크롤 뷰는 제 안의 위젯 밑이다 - 빈 곳이어야 스크롤 뷰다.
            const Bool above = top.owner == nullptr || (screen != top.screen ? screen
                : layer->GetOrder() != top.layer ? Bool(layer->GetOrder() > top.layer)
                : order != top.order ? Bool(order > top.order)
                : sequence != top.sequence ? Bool(sequence > top.sequence)
                : Bool(kind != WidgetKind::Scroll || top.kind == WidgetKind::Scroll));
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
        m_canvas->ForEach<Component::InputField2D>([&](Component::InputField2D& field)
        {
            field.changed = false;
            field.submitted = false;
            consider(Internal::CanvasAccess::GetOwner(field), WidgetKind::Field, field.interactable, field.size, field.offset, field.IsActiveComponent());
        });
        // 스크롤 뷰의 사각형은 같은 오브젝트의 마스크다. 안의 위젯이 위에 있으면 그쪽이 먼저고, 빈 곳이면 스크롤 뷰다.
        m_canvas->ForEach<Component::ScrollView2D>([&](Component::ScrollView2D& scroll)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(scroll);
            const auto* mask = owner != nullptr ? m_canvas->FindComponentRaw<Component::Mask2D>(owner) : nullptr;
            if (mask != nullptr)
            {
                consider(owner, WidgetKind::Scroll, true, mask->size, mask->offset, scroll.IsActiveComponent() && mask->IsActiveComponent());
            }
        });
        if (m_focused != InvalidInstanceId && focusedObject == nullptr)
        {
            // 포커스 위젯이 사라졌다.
            SetFocus(nullptr, WidgetKind::None);
        }

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
        if (pointer.pressed)
        {
            // 누른 위젯이 포커스를 받는다. 빈 곳이나 스크롤 뷰의 빈 자리를 누르면 포커스가 없다(글자 칸은 키보드를 놓는다).
            if (top.owner != nullptr && top.interactable && top.kind != WidgetKind::Scroll)
            {
                SetFocus(top.owner, top.kind);
            }
            else
            {
                SetFocus(nullptr, WidgetKind::None);
            }
        }
        if (pointer.pressed && top.owner != nullptr && top.interactable)
        {
            pressed = top.owner;
            pressedKind = top.kind;
            m_pressPixelX = pointer.x;
            m_pressPixelY = pointer.y;
            m_lastPixelX = pointer.x;
            m_lastPixelY = pointer.y;
            m_scrollDragging = false;
            m_dragDelta = {};
            // 누른 것이 스크롤 뷰거나 그 안의 버튼·토글·글자 칸이면 끌기가 스크롤이 된다. 슬라이더는 제 끌기가 먼저다.
            Object::GameObject* scrollOwner = top.kind == WidgetKind::Scroll ? top.owner
                : top.kind == WidgetKind::Slider ? nullptr : FindScrollAncestor(top.owner);
            m_scrollCandidate = scrollOwner != nullptr ? scrollOwner->GetInstanceId() : InvalidInstanceId;
            if (top.kind == WidgetKind::Button)
            {
                CallHook(pressed, Hook::Down);
            }
        }
        // 끌기 문턱을 넘으면 스크롤이 쥔다 - 누르고 있던 버튼은 뗌만 받고 누름은 아니다.
        if (pressed != nullptr && false == m_scrollDragging && m_scrollCandidate != InvalidInstanceId && pointer.present && pointer.down)
        {
            const Float dx = pointer.x - m_pressPixelX;
            const Float dy = pointer.y - m_pressPixelY;
            if (dx * dx + dy * dy > ScrollDragThreshold * ScrollDragThreshold)
            {
                Object::GameObject* scrollOwner = nullptr;
                m_canvas->ForEach<Component::ScrollView2D>([&](Component::ScrollView2D& scroll)
                {
                    Object::GameObject* owner = Internal::CanvasAccess::GetOwner(scroll);
                    if (owner != nullptr && owner->GetInstanceId() == m_scrollCandidate)
                    {
                        scrollOwner = owner;
                    }
                });
                if (scrollOwner != nullptr)
                {
                    if (pressedKind == WidgetKind::Button)
                    {
                        CallHook(pressed, Hook::Up);
                    }
                    pressed = scrollOwner;
                    pressedKind = WidgetKind::Scroll;
                    m_scrollDragging = true;
                    m_lastPixelX = m_pressPixelX;
                    m_lastPixelY = m_pressPixelY;
                }
                else
                {
                    m_scrollCandidate = InvalidInstanceId;
                }
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
        // 스크롤은 쥔 동안 포인터가 간 만큼 내용이 따라온다(레이어 단위).
        if (pressed != nullptr && pressedKind == WidgetKind::Scroll && m_scrollDragging && pointer.present && pointer.down)
        {
            auto* scroll = m_canvas->FindComponentRaw<Component::ScrollView2D>(pressed);
            const Layer* layer = pressed->GetLayer();
            Vector2 from;
            Vector2 to;
            if (scroll != nullptr && layer != nullptr && PixelToLayer(*layer, m_lastPixelX, m_lastPixelY, from) && PixelToLayer(*layer, pointer.x, pointer.y, to))
            {
                const Vector2 delta{ to.x - from.x, to.y - from.y };
                Bool clampedX = false;
                Bool clampedY = false;
                ScrollBy(*pressed, *scroll, delta, clampedX, clampedY);
                m_dragDelta.x += clampedX ? Float(0.0f) : delta.x;
                m_dragDelta.y += clampedY ? Float(0.0f) : delta.y;
            }
            m_lastPixelX = pointer.x;
            m_lastPixelY = pointer.y;
        }
        if (pressed != nullptr && (pointer.released || false == pointer.present || false == pointer.down))
        {
            Object::GameObject* released = pressed;
            const WidgetKind releasedKind = pressedKind;
            pressed = nullptr;
            pressedKind = WidgetKind::None;
            m_scrollDragging = false;
            m_scrollCandidate = InvalidInstanceId;
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

        // 휠: 포인터 아래의 위젯이 스크롤 뷰거나 그 안의 것이면 그 스크롤 뷰가 받는다.
        const MouseState& mouse = input.Mouse();
        if (top.owner != nullptr && false == pointer.touch && (mouse.wheelY != 0.0f || mouse.wheelX != 0.0f))
        {
            if (Object::GameObject* scrollOwner = top.kind == WidgetKind::Scroll ? top.owner : FindScrollAncestor(top.owner))
            {
                if (auto* scroll = m_canvas->FindComponentRaw<Component::ScrollView2D>(scrollOwner))
                {
                    Bool clampedX = false;
                    Bool clampedY = false;
                    // 휠을 위로 굴리면 위의 내용이 보인다 - 내용은 아래로 간다.
                    ScrollBy(*scrollOwner, *scroll, { mouse.wheelX * scroll->scrollSensitivity, -mouse.wheelY * scroll->scrollSensitivity }, clampedX, clampedY);
                    scroll->velocity = {};
                }
            }
        }

        // 4. 게임패드(D-352): 십자키·왼쪽 스틱이 포커스를 옮기고 `South` 가 누름이다. 슬라이더에 포커스가 있으면 좌우가 값이다.
        // 어느 패드든 하나만 본다 - 연결된 것 모두의 합이다.
        Bool usedGamepad = false;
        {
            Int32 directionX = 0;
            Int32 directionY = 0;
            Bool confirmPressed = false;
            Bool confirmReleased = false;
            Float stickX = 0.0f;
            Float stickY = 0.0f;
            for (UInt32 index = 0; index < MaxGamepads; ++index)
            {
                const GamepadState& pad = input.Gamepad(index);
                if (false == pad.connected)
                {
                    continue;
                }
                if (pad.IsPressed(GamepadButton::DPadLeft)) { directionX = -1; }
                if (pad.IsPressed(GamepadButton::DPadRight)) { directionX = 1; }
                if (pad.IsPressed(GamepadButton::DPadUp)) { directionY = -1; }
                if (pad.IsPressed(GamepadButton::DPadDown)) { directionY = 1; }
                confirmPressed = confirmPressed || pad.IsPressed(GamepadButton::South);
                confirmReleased = confirmReleased || pad.IsReleased(GamepadButton::South);
                const Float x = pad.GetAxis(GamepadAxis::LeftX);
                const Float y = pad.GetAxis(GamepadAxis::LeftY);
                stickX = std::fabs(x) > std::fabs(stickX) ? x : stickX;
                stickY = std::fabs(y) > std::fabs(stickY) ? y : stickY;
            }
            // 스틱은 문턱을 넘는 순간이 한 번이다(위가 +y 라 화면의 아래 방향과 반대다).
            if (stickX >= StickThreshold && m_stickX < StickThreshold) { directionX = 1; }
            if (stickX <= -StickThreshold && m_stickX > -StickThreshold) { directionX = -1; }
            if (stickY >= StickThreshold && m_stickY < StickThreshold) { directionY = -1; }
            if (stickY <= -StickThreshold && m_stickY > -StickThreshold) { directionY = 1; }
            m_stickX = stickX;
            m_stickY = stickY;

            if (directionX != 0 || directionY != 0)
            {
                usedGamepad = true;
                auto* slider = focusedObject != nullptr && m_focusedKind == WidgetKind::Slider
                    ? m_canvas->FindComponentRaw<Component::Slider2D>(focusedObject) : nullptr;
                if (slider != nullptr && directionX != 0)
                {
                    const Float value = Float::Clamp(slider->value + SliderStep * static_cast<JBro::Float>(directionX.Get()), 0.0f, 1.0f);
                    if (value != slider->value)
                    {
                        slider->value = value;
                        slider->changed = true;
                        CallHook(focusedObject, Hook::ValueChanged);
                    }
                }
                else
                {
                    NavigateFocus(directionX, directionY);
                }
            }
            if (confirmPressed && focusedObject != nullptr)
            {
                usedGamepad = true;
                m_padPressed = true;
                if (m_focusedKind == WidgetKind::Button)
                {
                    CallHook(focusedObject, Hook::Down);
                }
            }
            if (confirmReleased && m_padPressed && focusedObject != nullptr)
            {
                usedGamepad = true;
                m_padPressed = false;
                if (m_focusedKind == WidgetKind::Button)
                {
                    CallHook(focusedObject, Hook::Up);
                    if (auto* button = m_canvas->FindComponentRaw<Component::Button2D>(focusedObject))
                    {
                        button->clicked = true;
                    }
                    CallHook(focusedObject, Hook::Click);
                }
                else if (m_focusedKind == WidgetKind::Toggle)
                {
                    if (auto* toggle = m_canvas->FindComponentRaw<Component::Toggle2D>(focusedObject))
                    {
                        toggle->isOn = false == toggle->isOn;
                        toggle->changed = true;
                        CallHook(focusedObject, Hook::ValueChanged);
                    }
                }
            }
            // 포커스가 바뀌었을 수 있다 - 뒤의 상태 칸은 번호로 본다.
        }

        // 5. 키보드(D-352): 글자 칸에 포커스가 있으면 글자를 받고 키보드를 소비한다.
        Bool usedKeyboard = false;
        if (m_focused != InvalidInstanceId && m_focusedKind == WidgetKind::Field)
        {
            m_canvas->ForEach<Component::InputField2D>([&](Component::InputField2D& field)
            {
                Object::GameObject* owner = Internal::CanvasAccess::GetOwner(field);
                if (owner != nullptr && owner->GetInstanceId() == m_focused && field.IsActiveComponent() && field.interactable)
                {
                    TypeInto(*owner, field, input);
                    usedKeyboard = true;
                }
            });
        }

        // 6. 상태 필드와 전이. 훅이 위젯을 지웠을 수 있으니 다시 돈다.
        m_canvas->ForEach<Component::Button2D>([&](Component::Button2D& button)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(button);
            const InstanceId id = owner != nullptr ? owner->GetInstanceId() : InvalidInstanceId;
            button.hovered = id != InvalidInstanceId && id == m_hovered && m_hoveredKind == WidgetKind::Button;
            button.pressed = id != InvalidInstanceId && ((id == m_pressed && m_pressedKind == WidgetKind::Button)
                || (m_padPressed && id == m_focused && m_focusedKind == WidgetKind::Button));
            button.focused = id != InvalidInstanceId && id == m_focused && m_focusedKind == WidgetKind::Button;
            if (button.IsActiveComponent())
            {
                ApplyTransition(*m_canvas, button, owner, m_focusVisible);
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
            toggle.focused = id != InvalidInstanceId && id == m_focused && m_focusedKind == WidgetKind::Toggle;
        });
        m_canvas->ForEach<Component::Slider2D>([&](Component::Slider2D& slider)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(slider);
            const InstanceId id = owner != nullptr ? owner->GetInstanceId() : InvalidInstanceId;
            slider.hovered = id != InvalidInstanceId && id == m_hovered && m_hoveredKind == WidgetKind::Slider;
            slider.pressed = id != InvalidInstanceId && id == m_pressed && m_pressedKind == WidgetKind::Slider;
            slider.focused = id != InvalidInstanceId && id == m_focused && m_focusedKind == WidgetKind::Slider;
        });
        m_canvas->ForEach<Component::InputField2D>([&](Component::InputField2D& field)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(field);
            const InstanceId id = owner != nullptr ? owner->GetInstanceId() : InvalidInstanceId;
            field.focused = id != InvalidInstanceId && id == m_focused && m_focusedKind == WidgetKind::Field;
        });

        // 7. 위젯 위거나 쥐고 있으면 그 장치를 아래에서 지운다. 입력은 막지 않는다 - UI 가 쓴 게임패드·키보드만 지운다.
        m_pointerOver = top.owner != nullptr || m_pressed != InvalidInstanceId;
        // 둘 다 지운다 - 터치를 마우스로도 흉내 내는 플랫폼에서 같은 손가락이 마우스로 새어 나가지 않게.
        if (m_pointerOver)
        {
            input.Consume(InputDevice::Mouse);
            input.Consume(InputDevice::Touch);
        }
        if (usedGamepad)
        {
            input.Consume(InputDevice::Gamepad);
        }
        if (usedKeyboard)
        {
            input.Consume(InputDevice::Keyboard);
        }
        return InputResult::Pass;
    }
}
