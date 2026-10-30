#include <JBro/Framework2DSystem/System/Button2DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Runtime/ScriptCallGuard.h>
#include <JBro/AudioTypes/ServiceContext.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/Draggable2D.h>
#include <JBro/Framework2D/Component/DropTarget2D.h>
#include <JBro/Framework2D/Component/Dropdown2D.h>
#include <JBro/Framework2D/Component/InputField2D.h>
#include <JBro/Framework2D/Component/Mask2D.h>
#include <JBro/Framework2D/Component/Modal2D.h>
#include <JBro/Framework2D/Component/Navigation2D.h>
#include <JBro/Framework2D/Component/ScrollView2D.h>
#include <JBro/Framework2D/Component/Slider2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2D/Component/TabGroup2D.h>
#include <JBro/Framework2D/Component/Toggle2D.h>
#include <JBro/Framework2D/Component/ToggleGroup2D.h>
#include <JBro/Framework2D/Component/Tooltip2D.h>
#include <JBro/Framework2D/Component/VirtualKeyboard2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2DSystem/Rendering/CameraView2D.h>
#include <JBro/Framework2DSystem/Rendering/ClipRect2D.h>
#include <JBro/Framework2DSystem/Rendering/GroupState2D.h>
#include <JBro/Framework2DSystem/Scripting/ScriptSystem.h>
#include <JBro/Framework2DSystem/System/Camera2DSystem.h>
#include <JBro/Framework2DSystem/System/Text2DSystem.h>
#include <JBro/Host/IFramework.h>
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
        // 전이 셈은 위젯 다섯이 같다(D-355) - 필드 이름(`interactable`·`pressed`·`hovered`·`focused`·`transition`·`targetGraphic`·틴트·상태 스프라이트·래치)이 같다.
        template<typename TWidget>
        ButtonState StateOf(const TWidget& button, Bool focusVisible)
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

        template<typename TWidget>
        const Color& TintFor(const TWidget& button, ButtonState state)
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
        template<typename TWidget>
        AssetHandle SpriteFor(const TWidget& button, ButtonState state)
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
        template<typename TWidget>
        Component::SpriteRenderer2D* TargetGraphic(Canvas& canvas, TWidget& button, Object::GameObject* owner)
        {
            if (button.targetGraphic.IsValid())
            {
                return Internal::ComponentHandleAccess::ResolveAs<Component::SpriteRenderer2D>(button.targetGraphic);
            }
            return owner != nullptr ? canvas.FindComponentRaw<Component::SpriteRenderer2D>(owner) : nullptr;
        }

        // 보통 상태로 돌아오면 떠 둔 스프라이트를 되돌린다. 전이가 `SpriteSwap` 이 아니게 되어도 같다.
        template<typename TWidget>
        void RestoreSwap(TWidget& button, Component::SpriteRenderer2D* graphic)
        {
            if (button.swapped && graphic != nullptr)
            {
                graphic->sprite = button.normalSprite;
            }
            button.swapped = false;
            button.normalSprite = {};
        }

        template<typename TWidget>
        void ApplyTransition(Canvas& canvas, TWidget& button, Object::GameObject* owner, Bool focusVisible)
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

        // 오브젝트와 자손의 토글을 계층 차례로 돈다(D-355, 토글 그룹·드롭다운 항목). 켜짐과 무관하게 모두 센다 - 번호가 흔들리면 안 된다.
        template<typename Visit>
        void VisitToggles(Canvas& canvas, Object::GameObject& object, Visit& visit)
        {
            if (auto* toggle = canvas.FindComponentRaw<Component::Toggle2D>(&object))
            {
                visit(object, *toggle);
            }
            for (const SafePtr<Object::GameObject>& child : object.GetChildren())
            {
                if (Object::GameObject* next = child.TryGet())
                {
                    VisitToggles(canvas, *next, visit);
                }
            }
        }

        Bool IsSelfOrDescendantOf(const Object::GameObject* object, const Object::GameObject* ancestor)
        {
            for (const Object::GameObject* at = object; at != nullptr; at = at->GetParent())
            {
                if (at == ancestor)
                {
                    return true;
                }
            }
            return false;
        }

        // **UTF-8**(D-352·D-354). 코드포인트 하나를 바이트 자리 `at` 에 끼운다. 글자 수는 코드포인트 수이고, 글자 번호는 바이트 자리로 옮겨 쓴다.
        void InsertCodePoint(String& text, std::size_t at, UInt32 codePoint)
        {
            const std::uint32_t value = codePoint.Get();
            // 짝 없는 서러게이트(플랫폼이 UTF-16 한 단위를 그대로 보낸 것)는 UTF-8 에 없는 값이다 - 버린다.
            if (value >= 0xD800 && value <= 0xDFFF)
            {
                return;
            }
            char bytes[4] = {};
            std::size_t length = 0;
            if (value < 0x80)
            {
                bytes[length++] = static_cast<char>(value);
            }
            else if (value < 0x800)
            {
                bytes[length++] = static_cast<char>(0xC0 | (value >> 6));
                bytes[length++] = static_cast<char>(0x80 | (value & 0x3F));
            }
            else if (value < 0x10000)
            {
                bytes[length++] = static_cast<char>(0xE0 | (value >> 12));
                bytes[length++] = static_cast<char>(0x80 | ((value >> 6) & 0x3F));
                bytes[length++] = static_cast<char>(0x80 | (value & 0x3F));
            }
            else if (value < 0x110000)
            {
                bytes[length++] = static_cast<char>(0xF0 | (value >> 18));
                bytes[length++] = static_cast<char>(0x80 | ((value >> 12) & 0x3F));
                bytes[length++] = static_cast<char>(0x80 | ((value >> 6) & 0x3F));
                bytes[length++] = static_cast<char>(0x80 | (value & 0x3F));
            }
            if (length > 0)
            {
                text.insert(at <= text.size() ? at : text.size(), bytes, length);
            }
        }

        UInt32 CountCodePoints(const char* data, std::size_t size)
        {
            UInt32 count = 0;
            for (std::size_t at = 0; at < size; ++at)
            {
                // 이어지는 바이트(10xxxxxx)가 아니면 글자의 첫 바이트다.
                if ((static_cast<unsigned char>(data[at]) & 0xC0) != 0x80)
                {
                    ++count;
                }
            }
            return count;
        }

        UInt32 CountCodePoints(const String& text)
        {
            return CountCodePoints(text.data(), text.size());
        }

        // 코드포인트 번호 `index` 의 글자가 시작하는 바이트 자리다. 글자 수 이상이면 끝이다.
        std::size_t ByteOffsetOf(const char* data, std::size_t size, UInt32 index)
        {
            UInt32 seen = 0;
            for (std::size_t at = 0; at < size; ++at)
            {
                if ((static_cast<unsigned char>(data[at]) & 0xC0) != 0x80)
                {
                    if (seen == index)
                    {
                        return at;
                    }
                    ++seen;
                }
            }
            return size;
        }

        std::size_t ByteOffsetOf(const String& text, UInt32 index)
        {
            return ByteOffsetOf(text.data(), text.size(), index);
        }

        // 글자 칸의 커서 번호를 글자 수 안으로 가둔 값이다.
        // UTF-8 한 글자를 푼다(D-355, 붙여넣기). 깨진 바이트는 1 바이트를 건너뛰고 거짓이다.
        Bool DecodeCodePoint(const char* data, std::size_t size, std::size_t at, UInt32& codePoint, std::size_t& length)
        {
            const unsigned char lead = static_cast<unsigned char>(data[at]);
            length = 1;
            if (lead < 0x80)
            {
                codePoint = lead;
                return true;
            }
            const std::size_t needed = (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 0;
            if (needed == 0 || at + needed > size)
            {
                return false;
            }
            UInt32 value = lead & (needed == 2 ? 0x1Fu : needed == 3 ? 0x0Fu : 0x07u);
            for (std::size_t index = 1; index < needed; ++index)
            {
                const unsigned char trail = static_cast<unsigned char>(data[at + index]);
                if ((trail & 0xC0) != 0x80)
                {
                    return false;
                }
                value = (value << 6) | (trail & 0x3Fu);
            }
            length = needed;
            codePoint = value;
            return true;
        }

        UInt32 ClampCaret(const Component::InputField2D& field, UInt32 count)
        {
            const std::int32_t raw = field.caretIndex.Get();
            if (raw <= 0)
            {
                return 0;
            }
            const std::uint32_t wanted = static_cast<std::uint32_t>(raw);
            return wanted < count.Get() ? UInt32(wanted) : count;
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

    void Button2DSystem::SetTextInputHost(ITextInputHost* host)
    {
        m_textInput = host;
    }

    void Button2DSystem::SetScreenSpace(const ScreenSpaceFrame& frame)
    {
        m_screen = frame;
    }

    void Button2DSystem::OnInitialize(Canvas& canvas)
    {
        m_canvas = &canvas;
        m_lifted.Reserve(64);
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
        // 그 픽셀이 든 뷰포트의 카메라로 그 뷰포트 안에서 역투영한다(D-359). 사각형은 그 카메라의 것으로 다시 건다.
        ScreenSpaceFrame frame = m_screen;
        frame.areaX = 0.0f;
        frame.areaY = 0.0f;
        frame.areaWidth = 0.0f;
        frame.areaHeight = 0.0f;
        if (m_canvas == nullptr || false == Camera2DSystem::SelectCameraAt(*m_canvas, frame, pixelX, pixelY, camera))
        {
            return false;
        }
        ApplyCameraViewport(camera, frame);
        if (false == ScreenPixelToNormalized(frame, pixelX, pixelY, nx, ny) || false == ComputeCameraView2D(camera, frame, view))
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
        // 월드 → 픽셀은 주 카메라의 뷰포트다(D-359).
        ScreenSpaceFrame frame = m_screen;
        frame.areaX = 0.0f;
        frame.areaY = 0.0f;
        frame.areaWidth = 0.0f;
        frame.areaHeight = 0.0f;
        if (m_canvas == nullptr || false == Camera2DSystem::SelectCamera(*m_canvas, camera))
        {
            return false;
        }
        ApplyCameraViewport(camera, frame);
        if (false == ComputeCameraView2D(camera, frame, view))
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
        return NormalizedToScreenPixel(frame, vx / view.halfWidth, vy / view.halfHeight, pixelX, pixelY);
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
        const Float dx = std::fabs(local.x - offset.x);
        const Float dy = std::fabs(local.y - offset.y);
        // 안이라는 쪽을 묻는다 - NaN 크기·자리는 어느 비교도 참이 아니라 밖이다.
        if (false == (dx <= halfWidth && dy <= halfHeight))
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
            case Hook::DragBegin:
                ScriptCallGuard::Call("OnDragBegin", [script] { script->OnDragBegin(); });
                break;
            case Hook::DragEnd:
                ScriptCallGuard::Call("OnDragEnd", [script] { script->OnDragEnd(); });
                break;
            case Hook::Drop:
                ScriptCallGuard::Call("OnDrop", [script] { script->OnDrop(); });
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

    Object::GameObject* Button2DSystem::FindToggleGroup(Object::GameObject* object) const
    {
        for (Object::GameObject* at = object; at != nullptr; at = at->GetParent())
        {
            const auto* group = m_canvas->FindComponentRaw<Component::ToggleGroup2D>(at);
            if (group != nullptr && group->IsActiveComponent())
            {
                return at;
            }
        }
        return nullptr;
    }

    Object::GameObject* Button2DSystem::FindDropdownOfOption(Object::GameObject* option, UInt32& index) const
    {
        for (Object::GameObject* at = option != nullptr ? option->GetParent() : nullptr; at != nullptr; at = at->GetParent())
        {
            Object::GameObject* found = nullptr;
            UInt32 position = 0;
            m_canvas->ForEach<Component::Dropdown2D>([&](Component::Dropdown2D& dropdown)
            {
                if (found != nullptr || false == dropdown.IsActiveComponent() || Internal::GameObjectHandleAccess::Resolve(dropdown.list) != at)
                {
                    return;
                }
                UInt32 counted = 0;
                Bool hit = false;
                auto count = [&](Object::GameObject& object, Component::Toggle2D&)
                {
                    if (&object == option)
                    {
                        position = counted;
                        hit = true;
                    }
                    ++counted;
                };
                VisitToggles(*m_canvas, *at, count);
                if (hit)
                {
                    found = Internal::CanvasAccess::GetOwner(dropdown);
                }
            });
            if (found != nullptr)
            {
                index = position;
                return found;
            }
        }
        return nullptr;
    }

    Bool Button2DSystem::FlipToggle(Object::GameObject& owner, Component::Toggle2D& toggle)
    {
        // 드롭다운의 항목: 뒤집지 않고 고른다. 열려 있을 때만이다.
        UInt32 index = 0;
        if (Object::GameObject* dropdownOwner = FindDropdownOfOption(&owner, index))
        {
            auto* dropdown = m_canvas->FindComponentRaw<Component::Dropdown2D>(dropdownOwner);
            if (dropdown == nullptr || false == dropdown->open)
            {
                return false;
            }
            const Int32 chosen = Int32(static_cast<std::int32_t>(index.Get()));
            dropdown->open = false;
            if (dropdown->value != chosen)
            {
                dropdown->value = chosen;
                dropdown->changed = true;
                CallHook(dropdownOwner, Hook::ValueChanged);
            }
            return true;
        }
        Object::GameObject* groupOwner = FindToggleGroup(&owner);
        const auto* group = groupOwner != nullptr ? m_canvas->FindComponentRaw<Component::ToggleGroup2D>(groupOwner) : nullptr;
        if (toggle.isOn && group != nullptr && false == group->allowOff)
        {
            return false;
        }
        toggle.isOn = false == toggle.isOn;
        toggle.changed = true;
        if (toggle.isOn && groupOwner != nullptr)
        {
            auto turnOffOthers = [&](Object::GameObject&, Component::Toggle2D& other)
            {
                if (&other != &toggle)
                {
                    other.isOn = false;
                }
            };
            VisitToggles(*m_canvas, *groupOwner, turnOffOthers);
        }
        CallHook(&owner, Hook::ValueChanged);
        return true;
    }

    void Button2DSystem::CloseDropdownsOutside(Object::GameObject* pressed)
    {
        m_canvas->ForEach<Component::Dropdown2D>([&](Component::Dropdown2D& dropdown)
        {
            if (false == dropdown.open)
            {
                return;
            }
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(dropdown);
            Object::GameObject* list = Internal::GameObjectHandleAccess::Resolve(dropdown.list);
            const Bool inside = pressed != nullptr && (pressed == owner || (list != nullptr && IsSelfOrDescendantOf(pressed, list)));
            if (false == inside)
            {
                dropdown.open = false;
            }
        });
    }

    Int32 Button2DSystem::CountOptions(const Component::Dropdown2D& dropdown) const
    {
        Object::GameObject* list = Internal::GameObjectHandleAccess::Resolve(dropdown.list);
        if (list == nullptr)
        {
            return 0;
        }
        Int32 count = 0;
        auto tally = [&](Object::GameObject&, Component::Toggle2D&)
        {
            ++count;
        };
        VisitToggles(*m_canvas, *list, tally);
        return count;
    }

    void Button2DSystem::OpenDropdown(Component::Dropdown2D& dropdown)
    {
        const Int32 count = CountOptions(dropdown);
        dropdown.open = true;
        dropdown.highlighted = count <= 0 ? Int32(-1) : (dropdown.value >= 0 && dropdown.value < count ? dropdown.value : Int32(0));
    }

    void Button2DSystem::MoveHighlight(Component::Dropdown2D& dropdown, Int32 delta)
    {
        const Int32 count = CountOptions(dropdown);
        if (count <= 0)
        {
            dropdown.highlighted = -1;
            return;
        }
        Int32 next = dropdown.highlighted < 0 ? (delta > 0 ? Int32(0) : Int32(count - 1)) : Int32(dropdown.highlighted + delta);
        if (next < 0)
        {
            next = 0;
        }
        if (next >= count)
        {
            next = count - 1;
        }
        dropdown.highlighted = next;
    }

    void Button2DSystem::PickHighlight(Object::GameObject& owner, Component::Dropdown2D& dropdown)
    {
        const Int32 count = CountOptions(dropdown);
        const Int32 chosen = dropdown.highlighted;
        dropdown.open = false;
        dropdown.highlighted = -1;
        if (chosen < 0 || chosen >= count)
        {
            return;
        }
        if (dropdown.value != chosen)
        {
            dropdown.value = chosen;
            dropdown.changed = true;
            CallHook(&owner, Hook::ValueChanged);
        }
    }

    Bool Button2DSystem::InsideModal(Object::GameObject* object) const
    {
        if (false == m_modalActive)
        {
            return true;
        }
        Bool inside = false;
        m_canvas->ForEach<Component::Modal2D>([&](Component::Modal2D& modal)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(modal);
            if (false == inside && owner != nullptr && modal.IsActiveComponent() && owner->IsActiveInHierarchy() && IsSelfOrDescendantOf(object, owner))
            {
                inside = true;
            }
        });
        return inside;
    }

    void Button2DSystem::LiftSubtree(Object::GameObject& root, Int32 delta)
    {
        Bool lifted = false;
        if (auto* sprite = m_canvas->FindComponentRaw<Component::SpriteRenderer2D>(&root))
        {
            sprite->renderOrder = sprite->renderOrder + delta;
            lifted = true;
        }
        if (auto* text = m_canvas->FindComponentRaw<Component::Text2D>(&root))
        {
            text->renderOrder = text->renderOrder + delta;
            lifted = true;
        }
        if (lifted)
        {
            m_lifted.Add(root.GetScriptHandle());
        }
        for (const SafePtr<Object::GameObject>& child : root.GetChildren())
        {
            if (Object::GameObject* next = child.TryGet())
            {
                LiftSubtree(*next, delta);
            }
        }
    }

    void Button2DSystem::LowerLifted(Int32 delta)
    {
        for (const Handle::GameObject& handle : m_lifted)
        {
            Object::GameObject* object = Internal::GameObjectHandleAccess::Resolve(handle);
            if (object == nullptr)
            {
                continue;
            }
            if (auto* sprite = m_canvas->FindComponentRaw<Component::SpriteRenderer2D>(object))
            {
                sprite->renderOrder = sprite->renderOrder + delta;
            }
            if (auto* text = m_canvas->FindComponentRaw<Component::Text2D>(object))
            {
                text->renderOrder = text->renderOrder + delta;
            }
        }
        m_lifted.Clear();
    }

    void Button2DSystem::DropDraggable(Object::GameObject& released, const Pointer& pointer)
    {
        auto* draggable = m_canvas->FindComponentRaw<Component::Draggable2D>(&released);
        if (draggable == nullptr)
        {
            return;
        }
        // 올렸던 것만 올렸던 만큼 내린다(D-360·D-361).
        if (draggable->lifted != 0)
        {
            LowerLifted(Int32(-draggable->lifted));
            draggable->lifted = 0;
        }
        // 포인터 아래의 놓을 자리 - 먼저 찾은 것이다(한 자리에 여럿을 겹쳐 두지 않는다). 모달 밖은 없는 것이다.
        Object::GameObject* targetOwner = nullptr;
        Component::DropTarget2D* target = nullptr;
        m_canvas->ForEach<Component::DropTarget2D>([&](Component::DropTarget2D& candidate)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(candidate);
            const Layer* layer = owner != nullptr ? owner->GetLayer() : nullptr;
            Vector2 local;
            if (target == nullptr && owner != nullptr && layer != nullptr && candidate.IsActiveComponent() && owner->IsActiveInHierarchy() && layer->IsVisible()
                && pointer.present && InsideModal(owner) && HitRect(*owner, *layer, candidate.size, candidate.offset, pointer, local))
            {
                targetOwner = owner;
                target = &candidate;
            }
        });
        draggable->dragging = false;
        if (target != nullptr)
        {
            target->dropped = released.GetScriptHandle();
            target->hovering = false;
            draggable->droppedOn = targetOwner->GetScriptHandle();
        }
        else if (draggable->snapBack && draggable->startValid)
        {
            if (auto* place = m_canvas->FindComponentRaw<Component::Transform2D>(&released))
            {
                place->position = draggable->startPosition;
            }
        }
        draggable->startValid = false;
        if (targetOwner != nullptr)
        {
            CallHook(targetOwner, Hook::Drop);
        }
        CallHook(&released, Hook::DragEnd);
    }

    Bool Button2DSystem::ScrollbarPage(Object::GameObject& barOwner, Float& step, Float& knobHalf) const
    {
        Bool found = false;
        m_canvas->ForEach<Component::ScrollView2D>([&](Component::ScrollView2D& scroll)
        {
            if (found || false == scroll.IsActiveComponent())
            {
                return;
            }
            const Bool vertical = Internal::GameObjectHandleAccess::Resolve(scroll.verticalBar) == &barOwner;
            const Bool horizontal = Internal::GameObjectHandleAccess::Resolve(scroll.horizontalBar) == &barOwner;
            if (false == vertical && false == horizontal)
            {
                return;
            }
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(scroll);
            const auto* mask = owner != nullptr ? m_canvas->FindComponentRaw<Component::Mask2D>(owner) : nullptr;
            if (mask == nullptr || (vertical && false == scroll.vertical) || (horizontal && false == scroll.horizontal))
            {
                return;
            }
            const Float viewport = std::fabs(vertical ? mask->size.y : mask->size.x);
            const Float content = std::fabs(vertical ? scroll.contentSize.y : scroll.contentSize.x);
            if (false == std::isfinite(viewport.Get()) || false == std::isfinite(content.Get()) || content <= viewport || viewport <= 0.0f)
            {
                return;
            }
            const Float ratio = viewport / content;
            step = viewport / (content - viewport);
            knobHalf = (ratio < 0.1f ? Float(0.1f) : ratio) * 0.5f;
            found = std::isfinite(step.Get()) && step > 0.0f;
        });
        return found;
    }

    void Button2DSystem::PageScrollbar(Object::GameObject& barOwner, Component::Slider2D& slider, Float pointerValue, Bool forward)
    {
        Float step = 0.0f;
        Float knobHalf = 0.0f;
        if (false == ScrollbarPage(barOwner, step, knobHalf) || false == std::isfinite(pointerValue.Get()))
        {
            return;
        }
        const Float value = Float(std::isfinite(slider.value.Get()) ? Float::Clamp(slider.value, 0.0f, 1.0f) : 0.0f);
        // 손잡이가 포인터에 닿았거나 포인터가 처음 방향의 반대쪽이면 그대로다 - 쥔 채 포인터를 옮기면 다시 간다.
        if (std::fabs(pointerValue - value) <= knobHalf || (pointerValue > value) != forward)
        {
            return;
        }
        const Float next = Float::Clamp(forward ? Float(value + step) : Float(value - step), 0.0f, 1.0f);
        if (next != slider.value)
        {
            slider.value = next;
            slider.changed = true;
            CallHook(&barOwner, Hook::ValueChanged);
        }
    }

    Object::GameObject* Button2DSystem::FindKeyboardAncestor(Object::GameObject* object) const
    {
        for (Object::GameObject* at = object; at != nullptr; at = at->GetParent())
        {
            const auto* keyboard = m_canvas->FindComponentRaw<Component::VirtualKeyboard2D>(at);
            if (keyboard != nullptr && keyboard->IsActiveComponent())
            {
                return at;
            }
        }
        return nullptr;
    }

    Button2DSystem::WidgetKind Button2DSystem::WidgetKindOf(Object::GameObject* object) const
    {
        if (object == nullptr || false == object->IsActiveInHierarchy())
        {
            return WidgetKind::None;
        }
        if (const auto* button = m_canvas->FindComponentRaw<Component::Button2D>(object); button != nullptr && button->IsActiveComponent() && button->interactable)
        {
            return WidgetKind::Button;
        }
        if (const auto* toggle = m_canvas->FindComponentRaw<Component::Toggle2D>(object); toggle != nullptr && toggle->IsActiveComponent() && toggle->interactable)
        {
            return WidgetKind::Toggle;
        }
        if (const auto* slider = m_canvas->FindComponentRaw<Component::Slider2D>(object); slider != nullptr && slider->IsActiveComponent() && slider->interactable)
        {
            return WidgetKind::Slider;
        }
        if (const auto* field = m_canvas->FindComponentRaw<Component::InputField2D>(object); field != nullptr && field->IsActiveComponent() && field->interactable)
        {
            return WidgetKind::Field;
        }
        if (const auto* dropdown = m_canvas->FindComponentRaw<Component::Dropdown2D>(object); dropdown != nullptr && dropdown->IsActiveComponent() && dropdown->interactable)
        {
            return WidgetKind::Dropdown;
        }
        return WidgetKind::None;
    }

    void Button2DSystem::PlayUiSound(AssetHandle sound)
    {
        if (sound.generation != 0)
        {
            GetAudioServices().Audio.PlayOneShot(sound);
        }
    }

    void Button2DSystem::EditField(Object::GameObject& owner, Component::InputField2D& field, const char* utf8, std::size_t size, Bool backspace)
    {
        TextStore& store = TextStore::Get();
        const ArrayView<const char> current = store.GetText(field.text);
        m_textScratch.assign(current.Data() != nullptr ? current.Data() : "", current.Size());
        UInt32 count = CountCodePoints(m_textScratch);
        UInt32 caret = ClampCaret(field, count);
        const UInt32 caretBefore = caret;
        Bool changed = false;
        // 선택이 있으면 먼저 지운다 - 글자는 그 자리에, 지우기는 그것으로 끝이다.
        const Int32 anchor = field.selectionAnchor;
        if (anchor >= 0 && UInt32(static_cast<std::uint32_t>(anchor.Get())) != caret)
        {
            const UInt32 anchorIndex = UInt32(static_cast<std::uint32_t>(anchor.Get())) < count ? UInt32(static_cast<std::uint32_t>(anchor.Get())) : count;
            const UInt32 begin = anchorIndex < caret ? anchorIndex : caret;
            const UInt32 end = anchorIndex < caret ? caret : anchorIndex;
            const std::size_t from = ByteOffsetOf(m_textScratch, begin);
            const std::size_t to = ByteOffsetOf(m_textScratch, end);
            m_textScratch.erase(from, to - from);
            count -= end - begin;
            caret = begin;
            changed = true;
            backspace = false;
        }
        if (backspace && caret > 0)
        {
            const std::size_t begin = ByteOffsetOf(m_textScratch, caret - 1);
            const std::size_t end = ByteOffsetOf(m_textScratch, caret);
            m_textScratch.erase(begin, end - begin);
            --caret;
            --count;
            changed = true;
        }
        std::size_t at = 0;
        while (utf8 != nullptr && at < size)
        {
            UInt32 codePoint = 0;
            std::size_t length = 1;
            const Bool decoded = DecodeCodePoint(utf8, size, at, codePoint, length);
            at += length;
            if (false == decoded || codePoint < 0x20 || codePoint == 0x7F)
            {
                continue;
            }
            if (field.maxLength > 0 && count >= static_cast<JBro::UInt32>(field.maxLength.Get()))
            {
                break;
            }
            const std::size_t before = m_textScratch.size();
            InsertCodePoint(m_textScratch, ByteOffsetOf(m_textScratch, caret), codePoint);
            if (m_textScratch.size() != before)
            {
                ++count;
                ++caret;
                changed = true;
            }
        }
        field.caretIndex = Int32(static_cast<std::int32_t>(caret.Get()));
        field.selectionAnchor = -1;
        field.blinkTime = 0.0f;
        if (changed)
        {
            // 고치기 전 글자를 떠 둔다(D-361) - Ctrl+Z 한 단계.
            m_undoScratch.assign(current.Data() != nullptr ? current.Data() : "", current.Size());
            KeepUndo(field, m_undoScratch.data(), m_undoScratch.size(), caretBefore);
            store.Assign(field.text, m_textScratch.data(), m_textScratch.size());
            field.changed = true;
            CallHook(&owner, Hook::ValueChanged);
        }
    }

    void Button2DSystem::PressVirtualKey(Object::GameObject& keyboardOwner, Object::GameObject* key, Bool fromGamepad)
    {
        const auto* keyboard = m_canvas->FindComponentRaw<Component::VirtualKeyboard2D>(&keyboardOwner);
        if (keyboard == nullptr)
        {
            return;
        }
        // 대상 칸: 적힌 것, 없으면 포커스 칸, 게임패드면(포커스가 키에 있다) 마지막 포커스 칸까지(D-360).
        Object::GameObject* fieldOwner = Internal::GameObjectHandleAccess::Resolve(keyboard->target);
        const InstanceId wanted = m_focusedKind == WidgetKind::Field ? m_focused : (fromGamepad ? m_lastField : InvalidInstanceId);
        if (fieldOwner == nullptr && wanted != InvalidInstanceId)
        {
            m_canvas->ForEach<Component::InputField2D>([&](Component::InputField2D& field)
            {
                Object::GameObject* owner = Internal::CanvasAccess::GetOwner(field);
                if (owner != nullptr && owner->GetInstanceId() == wanted)
                {
                    fieldOwner = owner;
                }
            });
        }
        auto* field = fieldOwner != nullptr ? m_canvas->FindComponentRaw<Component::InputField2D>(fieldOwner) : nullptr;
        if (field == nullptr || false == field->IsActiveComponent() || false == field->interactable)
        {
            return;
        }
        if (key == nullptr || Internal::GameObjectHandleAccess::Resolve(keyboard->backspace) == key)
        {
            EditField(*fieldOwner, *field, nullptr, 0, true);
            return;
        }
        if (Internal::GameObjectHandleAccess::Resolve(keyboard->enter) == key)
        {
            field->submitted = true;
            CallHook(fieldOwner, Hook::Submit);
            return;
        }
        if (Internal::GameObjectHandleAccess::Resolve(keyboard->space) == key)
        {
            EditField(*fieldOwner, *field, " ", 1, false);
            return;
        }
        // 나머지 버튼은 제 오브젝트의 글자다.
        const auto* label = m_canvas->FindComponentRaw<Component::Text2D>(key);
        if (label == nullptr)
        {
            return;
        }
        const ArrayView<const char> text = TextStore::Get().GetText(label->text);
        if (text.Data() == nullptr || text.Size() == 0)
        {
            return;
        }
        // 글자를 넣는 동안 저장소가 움직일 수 있다 - 라벨을 먼저 떠 둔다.
        m_clipScratch.assign(text.Data(), text.Size());
        EditField(*fieldOwner, *field, m_clipScratch.data(), m_clipScratch.size(), false);
    }

    void Button2DSystem::ScrollBy(Object::GameObject& owner, Component::ScrollView2D& scroll, Vector2 delta, Bool& clampedX, Bool& clampedY)
    {
        clampedX = false;
        clampedY = false;
        const auto* mask = m_canvas->FindComponentRaw<Component::Mask2D>(&owner);
        Object::GameObject* content = Internal::GameObjectHandleAccess::Resolve(scroll.content);
        // 내용은 스크롤 뷰의 **자식**이어야 한다 - 자리는 뷰포트와 같은 로컬 좌표다. 자기 자신이나 남을 가리키면 움직이지 않는다
        // (스크롤 뷰 자체가 매 프레임 끌려가는 것을 막는다).
        auto* place = content != nullptr && content->GetParent() == &owner ? m_canvas->FindComponentRaw<Component::Transform2D>(content) : nullptr;
        if (mask == nullptr || place == nullptr || false == std::isfinite(delta.x.Get()) || false == std::isfinite(delta.y.Get()))
        {
            return;
        }
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
        if (object != nullptr && kind == WidgetKind::Field)
        {
            m_lastField = m_focused;
        }
        // 글자 칸이 포커스를 받으면 커서는 글자 끝이고 바로 보인다(D-354).
        if (object != nullptr && kind == WidgetKind::Field && m_canvas != nullptr)
        {
            if (auto* field = m_canvas->FindComponentRaw<Component::InputField2D>(object))
            {
                const ArrayView<const char> text = TextStore::Get().GetText(field->text);
                const UInt32 count = CountCodePoints(text.Data(), text.Size());
                field->caretIndex = Int32(static_cast<std::int32_t>(count.Get()));
                field->selectionAnchor = -1;
                field->blinkTime = 0.0f;
            }
        }
    }

    void Button2DSystem::PlaceCaretAt(Component::InputField2D& field, const Pointer& pointer, Bool extend)
    {
        auto* display = Internal::ComponentHandleAccess::ResolveAs<Component::Text2D>(field.display);
        Object::GameObject* displayObject = display != nullptr ? Internal::CanvasAccess::GetOwner(*display) : nullptr;
        const Layer* layer = displayObject != nullptr ? displayObject->GetLayer() : nullptr;
        const System::Text2DSystem* texts = m_canvas->GetSystems().FindSystem<System::Text2DSystem>();
        const ArrayView<const char> text = TextStore::Get().GetText(field.text);
        const UInt32 count = CountCodePoints(text.Data(), text.Size());
        UInt32 caret = count;
        Vector2 local;
        UInt32 byteOffset = 0;
        if (display != nullptr && layer != nullptr && texts != nullptr && PointerToLocal(*displayObject, *layer, pointer, local)
            && texts->GetLocalCaretAt(display->GetInstanceId(), local.x, local.y, byteOffset))
        {
            const std::size_t clamped = static_cast<std::size_t>(byteOffset.Get()) < text.Size() ? static_cast<std::size_t>(byteOffset.Get()) : text.Size();
            caret = CountCodePoints(text.Data(), clamped);
        }
        const Int32 index = Int32(static_cast<std::int32_t>(caret.Get()));
        if (extend)
        {
            field.selectionAnchor = m_fieldPressCaret != index ? m_fieldPressCaret : Int32(-1);
        }
        else
        {
            field.selectionAnchor = -1;
            m_fieldPressCaret = index;
        }
        field.caretIndex = index;
        field.blinkTime = 0.0f;
    }

    void Button2DSystem::SelectWordAtCaret(Component::InputField2D& field)
    {
        const ArrayView<const char> text = TextStore::Get().GetText(field.text);
        const char* data = text.Data();
        const std::size_t size = text.Size();
        if (data == nullptr || size == 0)
        {
            return;
        }
        const UInt32 count = CountCodePoints(data, size);
        const UInt32 caret = ClampCaret(field, count);
        const auto isSpace = [](UInt32 codePoint) { return codePoint == 0x20 || codePoint == 0x09 || codePoint == 0x0A || codePoint == 0x0D || codePoint == 0x3000; };
        const auto codePointAt = [&](UInt32 index, UInt32& codePoint) -> Bool
        {
            if (index >= count)
            {
                return false;
            }
            std::size_t length = 0;
            return DecodeCodePoint(data, size, ByteOffsetOf(data, size, index), codePoint, length);
        };
        // 커서 양쪽을 본다 - 왼쪽 글자가 단어면 거기서, 아니면 오른쪽 글자가 단어여야 한다.
        UInt32 probe = 0;
        UInt32 begin = caret;
        UInt32 end = caret;
        while (begin > 0 && codePointAt(begin - 1, probe) && false == isSpace(probe))
        {
            --begin;
        }
        while (end < count && codePointAt(end, probe) && false == isSpace(probe))
        {
            ++end;
        }
        if (end == begin)
        {
            return;
        }
        field.selectionAnchor = Int32(static_cast<std::int32_t>(begin.Get()));
        field.caretIndex = Int32(static_cast<std::int32_t>(end.Get()));
        m_fieldPressCaret = field.selectionAnchor;
        field.blinkTime = 0.0f;
    }

    void Button2DSystem::KeepUndo(Component::InputField2D& field, const char* utf8, std::size_t size, UInt32 caret)
    {
        TextStore::Get().Assign(field.undoText, utf8 != nullptr ? utf8 : "", size);
        field.undoCaret = Int32(static_cast<std::int32_t>(caret.Get()));
        field.undoValid = true;
    }

    Bool Button2DSystem::Repeats(Bool down, Bool pressed, Float& held, Float interval)
    {
        if (pressed)
        {
            held = 0.0f;
            return true;
        }
        if (false == down)
        {
            held = 0.0f;
            return false;
        }
        held = held + m_frameDelta;
        if (held >= RepeatDelay)
        {
            held = held - interval;
            return true;
        }
        return false;
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
        Object::GameObject* fromOwner = nullptr;
        const auto consider = [&](Object::GameObject* owner, WidgetKind kind, Bool interactable, Bool active) {
            if (owner == nullptr || false == active || false == interactable || false == owner->IsActiveInHierarchy()
                || owner->GetLayer() == nullptr || false == owner->GetLayer()->IsVisible() || false == InsideModal(owner)
                || false == IsGroupInteractable2D(*m_canvas, owner))
            {
                return;
            }
            if (owner->GetInstanceId() == m_focused && kind == m_focusedKind)
            {
                hasFrom = WidgetPixel(*owner, fromX, fromY);
                fromOwner = owner;
                return;
            }
            Float x = 0.0f;
            Float y = 0.0f;
            if (false == WidgetPixel(*owner, x, y))
            {
                return;
            }
            // 포커스가 없으면 왼쪽 위가 먼저다 - 줄(y)이 먼저고 같은 줄에서는 왼쪽이다. 포커스가 있으면 이 걸음은 지금 자리만 찾고,
            // 방향의 후보는 두 번째 걸음(`pick`)이 센다.
            if (m_focused == InvalidInstanceId)
            {
                const Float score = y * 4096.0f + x;
                if (best == nullptr || score < bestScore)
                {
                    best = owner;
                    bestKind = kind;
                    bestScore = score;
                }
            }
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
            m_canvas->ForEach<Component::Dropdown2D>([&](Component::Dropdown2D& dropdown) {
                consider(Internal::CanvasAccess::GetOwner(dropdown), WidgetKind::Dropdown, dropdown.interactable, dropdown.IsActiveComponent());
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
        // 명시 연결(D-358): 포커스 오브젝트의 `Navigation2D` 가 그 방향을 가리키면 그리로 간다. 가리킨 곳에 켜진 위젯이 없거나 모달·그룹 밖이면 그 방향은 없는 것이다.
        if (const auto* links = fromOwner != nullptr ? m_canvas->FindComponentRaw<Component::Navigation2D>(fromOwner) : nullptr; links != nullptr && links->IsActiveComponent())
        {
            const Handle::GameObject& link = directionY < 0 ? links->up : directionY > 0 ? links->down : directionX < 0 ? links->left : links->right;
            if (link.IsValid())
            {
                Object::GameObject* target = Internal::GameObjectHandleAccess::Resolve(link);
                const WidgetKind kind = WidgetKindOf(target);
                if (kind != WidgetKind::None && target != fromOwner && InsideModal(target) && IsGroupInteractable2D(*m_canvas, target)
                    && target->GetLayer() != nullptr && target->GetLayer()->IsVisible())
                {
                    SetFocus(target, kind, true);
                }
                return;
            }
        }
        // 두 번째 걸음: 방향의 가장 가까운 것.
        best = nullptr;
        const Float dirX = static_cast<JBro::Float>(directionX.Get());
        const Float dirY = static_cast<JBro::Float>(directionY.Get());
        const auto pick = [&](Object::GameObject* owner, WidgetKind kind, Bool interactable, Bool active) {
            if (owner == nullptr || false == active || false == interactable || false == owner->IsActiveInHierarchy()
                || owner->GetLayer() == nullptr || false == owner->GetLayer()->IsVisible() || false == InsideModal(owner)
                || false == IsGroupInteractable2D(*m_canvas, owner)
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
        m_canvas->ForEach<Component::Dropdown2D>([&](Component::Dropdown2D& dropdown) {
            pick(Internal::CanvasAccess::GetOwner(dropdown), WidgetKind::Dropdown, dropdown.interactable, dropdown.IsActiveComponent());
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
        // 커서는 글자 번호다(D-354). 스크립트가 글자를 통째로 바꿨을 수 있으니 글자 수 안으로 가둔다.
        UInt32 caret = ClampCaret(field, count);
        const UInt32 caretBefore = caret;
        // 선택의 다른 끝이다(D-355). 커서와 다를 때만 선택이다. 스크립트가 썼을 수 있으니 글자 수 안으로 가둔다.
        Int32 anchor = field.selectionAnchor < 0 ? Int32(-1) : field.selectionAnchor;
        if (anchor >= 0 && UInt32(static_cast<std::uint32_t>(anchor.Get())) > count)
        {
            anchor = Int32(static_cast<std::int32_t>(count.Get()));
        }
        const auto anchorIndex = [&]() { return UInt32(static_cast<std::uint32_t>(anchor.Get())); };
        const auto hasSelection = [&]() { return anchor >= 0 && anchorIndex() != caret; };
        const auto selectionBegin = [&]() { return anchorIndex() < caret ? anchorIndex() : caret; };
        const auto selectionEnd = [&]() { return anchorIndex() < caret ? caret : anchorIndex(); };
        const auto eraseSelection = [&]()
        {
            const UInt32 begin = selectionBegin();
            const UInt32 end = selectionEnd();
            const std::size_t from = ByteOffsetOf(m_textScratch, begin);
            const std::size_t to = ByteOffsetOf(m_textScratch, end);
            m_textScratch.erase(from, to - from);
            count -= end - begin;
            caret = begin;
            anchor = -1;
            changed = true;
        };
        // 글자 하나를 커서 자리에 끼운다. 제어 글자는 글자가 아니고, 상한에 닿으면 거짓이다.
        const auto insert = [&](UInt32 codePoint)
        {
            if (codePoint < 0x20 || codePoint == 0x7F)
            {
                return true;
            }
            if (field.maxLength > 0 && count >= static_cast<JBro::UInt32>(field.maxLength.Get()))
            {
                return false;
            }
            const std::size_t before = m_textScratch.size();
            InsertCodePoint(m_textScratch, ByteOffsetOf(m_textScratch, caret), codePoint);
            if (m_textScratch.size() != before)
            {
                ++count;
                ++caret;
                changed = true;
            }
            return true;
        };
        const Bool shift = (keyboard.modifiers & KeyModifierShift) != 0;
        // Alt 가 같이 눌린 Ctrl 은 AltGr 이라 글자다 - 단축키가 아니다.
        const Bool control = (keyboard.modifiers & KeyModifierControl) != 0 && (keyboard.modifiers & KeyModifierAlt) == 0;
        // Ctrl 단축키(D-355): A 전부, C 복사, X 잘라내기, V 붙여넣기. 클립보드는 호스트의 것이고 없으면 비어 있다. Z 는 되돌리기 한 단계다(D-361) - 떠 둔 글자와 지금 것을 바꾼다.
        if (control && keyboard.IsPressed(Key::A))
        {
            anchor = 0;
            caret = count;
        }
        if (control && keyboard.IsPressed(Key::Z) && field.undoValid)
        {
            const ArrayView<const char> undo = store.GetText(field.undoText);
            m_textScratch.assign(undo.Data() != nullptr ? undo.Data() : "", undo.Size());
            count = CountCodePoints(m_textScratch);
            const Int32 wanted = field.undoCaret < 0 ? Int32(0) : field.undoCaret;
            caret = UInt32(static_cast<std::uint32_t>(wanted.Get())) < count ? UInt32(static_cast<std::uint32_t>(wanted.Get())) : count;
            anchor = -1;
            changed = true;
        }
        if (control && (keyboard.IsPressed(Key::C) || keyboard.IsPressed(Key::X)) && hasSelection())
        {
            const std::size_t from = ByteOffsetOf(m_textScratch, selectionBegin());
            const std::size_t to = ByteOffsetOf(m_textScratch, selectionEnd());
            if (m_textInput != nullptr)
            {
                m_textInput->WriteClipboardText(m_textScratch.data() + from, to - from);
            }
            if (keyboard.IsPressed(Key::X))
            {
                eraseSelection();
            }
        }
        if (control && keyboard.IsPressed(Key::V) && m_textInput != nullptr)
        {
            m_clipScratch.clear();
            if (m_textInput->ReadClipboardText(m_clipScratch) && false == m_clipScratch.empty())
            {
                if (hasSelection())
                {
                    eraseSelection();
                }
                // 클립보드의 UTF-8 을 글자마다 끼운다 - 제어 글자는 버리고 상한에서 멈춘다.
                std::size_t at = 0;
                while (at < m_clipScratch.size())
                {
                    UInt32 codePoint = 0;
                    std::size_t length = 1;
                    const Bool decoded = DecodeCodePoint(m_clipScratch.data(), m_clipScratch.size(), at, codePoint, length);
                    at += length;
                    if (decoded && false == insert(codePoint))
                    {
                        break;
                    }
                }
            }
        }
        // 글자(D-352). Ctrl 이 눌린 동안의 글자는 단축키의 것이라 끼우지 않는다. 선택이 있으면 그 자리를 대신한다.
        if (false == control)
        {
            for (UInt32 index = 0; index < keyboard.GetTextLength(); ++index)
            {
                const UInt32 codePoint = keyboard.GetText(index);
                if (codePoint < 0x20 || codePoint == 0x7F)
                {
                    continue;
                }
                if (hasSelection())
                {
                    eraseSelection();
                }
                if (false == insert(codePoint))
                {
                    break;
                }
            }
        }
        // 쥔 Backspace·Delete·←·→ 는 지연 뒤 간격마다 되풀이한다(D-354).
        const Bool backspace = Repeats(keyboard.IsDown(Key::Backspace), keyboard.IsPressed(Key::Backspace), m_keyHeld[0], KeyRepeatInterval);
        const Bool erase = Repeats(keyboard.IsDown(Key::Delete), keyboard.IsPressed(Key::Delete), m_keyHeld[1], KeyRepeatInterval);
        const Bool left = Repeats(keyboard.IsDown(Key::Left), keyboard.IsPressed(Key::Left), m_keyHeld[2], KeyRepeatInterval);
        const Bool right = Repeats(keyboard.IsDown(Key::Right), keyboard.IsPressed(Key::Right), m_keyHeld[3], KeyRepeatInterval);
        if (backspace)
        {
            if (hasSelection())
            {
                eraseSelection();
            }
            else if (caret > 0)
            {
                const std::size_t begin = ByteOffsetOf(m_textScratch, caret - 1);
                const std::size_t end = ByteOffsetOf(m_textScratch, caret);
                m_textScratch.erase(begin, end - begin);
                --caret;
                --count;
                changed = true;
            }
        }
        if (erase)
        {
            if (hasSelection())
            {
                eraseSelection();
            }
            else if (caret < count)
            {
                const std::size_t begin = ByteOffsetOf(m_textScratch, caret);
                const std::size_t end = ByteOffsetOf(m_textScratch, caret + 1);
                m_textScratch.erase(begin, end - begin);
                --count;
                changed = true;
            }
        }
        // 옮기기(D-354·D-355). Shift 면 선택을 늘리고(기준이 없으면 지금 커서가 기준), 아니면 선택을 접는다 - ←·→ 는 선택의 그쪽 끝으로.
        const auto moveTo = [&](UInt32 target)
        {
            if (shift)
            {
                if (anchor < 0)
                {
                    anchor = Int32(static_cast<std::int32_t>(caret.Get()));
                }
            }
            else
            {
                anchor = -1;
            }
            caret = target;
        };
        if (left)
        {
            if (false == shift && hasSelection())
            {
                caret = selectionBegin();
                anchor = -1;
            }
            else if (caret > 0)
            {
                moveTo(caret - 1);
            }
        }
        if (right)
        {
            if (false == shift && hasSelection())
            {
                caret = selectionEnd();
                anchor = -1;
            }
            else if (caret < count)
            {
                moveTo(caret + 1);
            }
        }
        if (keyboard.IsPressed(Key::Home))
        {
            moveTo(0);
        }
        if (keyboard.IsPressed(Key::End))
        {
            moveTo(count);
        }
        field.caretIndex = Int32(static_cast<std::int32_t>(caret.Get()));
        field.selectionAnchor = hasSelection() ? anchor : Int32(-1);
        if (changed || caret != caretBefore)
        {
            // 고치거나 옮긴 커서는 바로 보인다.
            field.blinkTime = 0.0f;
        }
        if (changed)
        {
            // 고치기 전 글자를 떠 둔다(D-361) - Ctrl+Z 한 단계.
            m_undoScratch.assign(current.Data() != nullptr ? current.Data() : "", current.Size());
            KeepUndo(field, m_undoScratch.data(), m_undoScratch.size(), caretBefore);
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
        // 스크롤바(D-355): 막대(슬라이더)와 내용 자리를 맞춘다. 막대가 이번 프레임에 끌렸으면 내용이, 아니면 막대가 따라온다. 손잡이 길이는 뷰포트/내용 비율이다.
        // 슬라이더 자리 놓기보다 먼저다 - 같은 프레임에 손잡이가 따라온다.
        canvas.ForEach<Component::ScrollView2D>([&](Component::ScrollView2D& scroll)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(scroll);
            if (owner == nullptr || false == scroll.IsActiveComponent())
            {
                return;
            }
            const auto* mask = canvas.FindComponentRaw<Component::Mask2D>(owner);
            Object::GameObject* content = Internal::GameObjectHandleAccess::Resolve(scroll.content);
            auto* place = content != nullptr && content->GetParent() == owner ? canvas.FindComponentRaw<Component::Transform2D>(content) : nullptr;
            if (mask == nullptr || place == nullptr)
            {
                return;
            }
            // 값 0 은 위·왼쪽이다. 세로는 내용 자리의 낮은 끝(`low`)이 위이고, 가로는 높은 끝(`high`)이 왼쪽이다(내용이 오른쪽으로 밀려 왼쪽 끝이 보인다).
            const auto sync = [&](const Handle::GameObject& barHandle, Bool enabled, Float& position, Float center, Float viewport, Float contentSize, Bool startIsHigh)
            {
                Object::GameObject* barObject = Internal::GameObjectHandleAccess::Resolve(barHandle);
                auto* bar = barObject != nullptr ? canvas.FindComponentRaw<Component::Slider2D>(barObject) : nullptr;
                if (bar == nullptr)
                {
                    return;
                }
                const Float half = std::fabs(viewport) * 0.5f;
                const Float contentHalf = std::fabs(contentSize) * 0.5f;
                Float ratio = Float(contentHalf > 0.0f && std::isfinite(contentHalf.Get()) ? Float::Clamp(half / contentHalf, 0.0f, 1.0f) : 1.0f);
                if (false == enabled || contentHalf <= half || false == std::isfinite(position.Get()) || false == std::isfinite(half.Get()))
                {
                    bar->value = 0.0f;
                    ratio = 1.0f;
                }
                else
                {
                    const Float low = center + half - contentHalf;
                    const Float high = center - half + contentHalf;
                    const Float start = startIsHigh ? high : low;
                    const Float end = startIsHigh ? low : high;
                    const Float span = end - start;
                    if (bar->changed)
                    {
                        position = start + span * Float::Clamp(Float(std::isfinite(bar->value.Get()) ? bar->value.Get() : 0.0f), 0.0f, 1.0f);
                        scroll.velocity = {};
                    }
                    else
                    {
                        bar->value = Float(span != 0.0f ? Float::Clamp((position - start) / span, 0.0f, 1.0f) : 0.0f);
                    }
                }
                // 손잡이(막대의 자식)의 `Custom` 크기 스프라이트는 그 축 길이가 비율이다. 트랙의 1/10 보다 짧아지지 않는다.
                Object::GameObject* knobObject = Internal::GameObjectHandleAccess::Resolve(bar->handle);
                auto* knob = knobObject != nullptr && knobObject->GetParent() == barObject ? canvas.FindComponentRaw<Component::SpriteRenderer2D>(knobObject) : nullptr;
                if (knob != nullptr && knob->sizeMode == Component::SpriteSizeMode::Custom)
                {
                    const Bool along = bar->direction == Component::SliderDirection::LeftToRight || bar->direction == Component::SliderDirection::RightToLeft;
                    const Float track = std::fabs(along ? bar->size.x : bar->size.y);
                    const Float length = track * (ratio < 0.1f ? Float(0.1f) : ratio);
                    if (along)
                    {
                        knob->size.x = length;
                    }
                    else
                    {
                        knob->size.y = length;
                    }
                }
            };
            sync(scroll.horizontalBar, scroll.horizontal, place->position.x, mask->offset.x, mask->size.x, scroll.contentSize.x, true);
            sync(scroll.verticalBar, scroll.vertical, place->position.y, mask->offset.y, mask->size.y, scroll.contentSize.y, false);
        });
        // 슬라이더: 손잡이(자식 오브젝트)를 값의 자리에, 채우기 스프라이트의 비율을 값으로.
        canvas.ForEach<Component::Slider2D>([&](Component::Slider2D& slider)
        {
            if (false == slider.IsActiveComponent())
            {
                return;
            }
            // 손잡이는 슬라이더의 **자식**이어야 한다 - 자기 자신이나 남을 가리키면 놓지 않는다.
            Object::GameObject* handle = Internal::GameObjectHandleAccess::Resolve(slider.handle);
            if (handle != nullptr && handle->GetParent() == Internal::CanvasAccess::GetOwner(slider))
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
        // 토글 그룹(D-355): 하나만 켜진다. 둘 이상이면 전에 켜져 있지 않던 쪽(번호가 `value` 와 다른 첫 것)을 남긴다. 스크립트가 켠 것도 여기서 정리된다.
        canvas.ForEach<Component::ToggleGroup2D>([&](Component::ToggleGroup2D& group)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(group);
            if (owner == nullptr || false == group.IsActiveComponent())
            {
                return;
            }
            Int32 index = 0;
            Int32 firstOn = -1;
            Int32 newOn = -1;
            Int32 onCount = 0;
            auto survey = [&](Object::GameObject&, Component::Toggle2D& toggle)
            {
                if (toggle.isOn)
                {
                    ++onCount;
                    if (firstOn < 0)
                    {
                        firstOn = index;
                    }
                    if (newOn < 0 && index != group.value)
                    {
                        newOn = index;
                    }
                }
                ++index;
            };
            VisitToggles(canvas, *owner, survey);
            const Int32 keep = onCount > 1 && newOn >= 0 ? newOn : firstOn;
            if (onCount > 1)
            {
                index = 0;
                auto settle = [&](Object::GameObject&, Component::Toggle2D& toggle)
                {
                    toggle.isOn = index == keep;
                    ++index;
                };
                VisitToggles(canvas, *owner, settle);
            }
            group.value = keep;
        });
        // 탭(D-357): 같은 오브젝트의 토글 그룹이 고른 번호의 페이지(`content` 의 자식)만 켠다.
        canvas.ForEach<Component::TabGroup2D>([&](Component::TabGroup2D& tabs)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(tabs);
            if (owner == nullptr || false == tabs.IsActiveComponent())
            {
                return;
            }
            const auto* group = canvas.FindComponentRaw<Component::ToggleGroup2D>(owner);
            Object::GameObject* content = Internal::GameObjectHandleAccess::Resolve(tabs.content);
            const Int32 page = group != nullptr && group->IsActiveComponent() ? group->value : Int32(-1);
            tabs.page = page;
            if (content == nullptr || content == owner)
            {
                return;
            }
            Int32 index = 0;
            for (const SafePtr<Object::GameObject>& child : content->GetChildren())
            {
                Object::GameObject* object = child.TryGet();
                if (object != nullptr && object->IsActiveSelf() != (index == page))
                {
                    object->SetActive(index == page);
                }
                ++index;
            }
            if (page >= index)
            {
                tabs.page = -1;
            }
        });
        // 툴팁(D-357): 머문 시간이 찼으면 대상을 켠다. 대상이 자기 자신이면 손대지 않는다.
        canvas.ForEach<Component::Tooltip2D>([&](Component::Tooltip2D& tooltip)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(tooltip);
            Object::GameObject* target = Internal::GameObjectHandleAccess::Resolve(tooltip.target);
            if (owner == nullptr || target == nullptr || target == owner || false == tooltip.IsActiveComponent())
            {
                return;
            }
            if (target->IsActiveSelf() != tooltip.shown)
            {
                target->SetActive(tooltip.shown);
            }
            // 따라가기(D-360): 대상을 포인터 자리 + 오프셋에 - 대상 부모의 좌표로.
            if (tooltip.follow && tooltip.shown && m_pointer.present)
            {
                auto* place = canvas.FindComponentRaw<Component::Transform2D>(target);
                Object::GameObject* parent = target->GetParent();
                const Layer* layer = target->GetLayer();
                Vector2 at;
                const Bool mapped = place != nullptr && layer != nullptr
                    && (parent != nullptr ? PointerToLocal(*parent, *layer, m_pointer, at) : PixelToLayer(*layer, m_pointer.x, m_pointer.y, at));
                if (mapped && std::isfinite(tooltip.followOffset.x.Get()) && std::isfinite(tooltip.followOffset.y.Get()))
                {
                    place->position = { at.x + tooltip.followOffset.x, at.y + tooltip.followOffset.y };
                }
            }
        });
        // 드롭다운(D-355): 목록(자식)의 켜짐은 `open`, 항목 토글의 켜짐은 `value`, 캡션은 고른 항목의 글자다.
        canvas.ForEach<Component::Dropdown2D>([&](Component::Dropdown2D& dropdown)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(dropdown);
            if (owner == nullptr || false == dropdown.IsActiveComponent())
            {
                return;
            }
            Object::GameObject* list = Internal::GameObjectHandleAccess::Resolve(dropdown.list);
            // 목록은 드롭다운의 **자식**이어야 켜고 끈다 - 자기 자신이나 남을 가리키면 손대지 않는다.
            if (list != nullptr && list->GetParent() == owner && list->IsActiveSelf() != dropdown.open)
            {
                list->SetActive(dropdown.open);
            }
            Int32 index = 0;
            Component::Text2D* chosenLabel = nullptr;
            if (list != nullptr && list != owner)
            {
                auto mark = [&](Object::GameObject& option, Component::Toggle2D& toggle)
                {
                    const Bool on = index == dropdown.value;
                    toggle.isOn = on;
                    if (on)
                    {
                        chosenLabel = canvas.FindComponentRaw<Component::Text2D>(&option);
                    }
                    ++index;
                };
                VisitToggles(canvas, *list, mark);
            }
            auto* caption = Internal::ComponentHandleAccess::ResolveAs<Component::Text2D>(dropdown.caption);
            if (caption == nullptr || caption == chosenLabel)
            {
                return;
            }
            TextStore& store = TextStore::Get();
            const UInt32 revision = chosenLabel != nullptr ? store.GetRevision(chosenLabel->text) : UInt32(0);
            if (dropdown.value != dropdown.shownValue || revision != dropdown.shownRevision)
            {
                const ArrayView<const char> label = chosenLabel != nullptr ? store.GetText(chosenLabel->text) : ArrayView<const char>{};
                store.Assign(caption->text, label.Data() != nullptr ? label.Data() : "", label.Size());
                dropdown.shownValue = dropdown.value;
                dropdown.shownRevision = revision;
            }
        });
        // 글자 칸: 글자가 바뀌었으면 보이는 텍스트에 옮기고(조합 중이면 조합 글자를 커서 자리에 끼워서), 커서는 커서 자리에서 깜빡이며, 선택 구간을
        // 두 커서 사이로 늘린다(D-352·D-354·D-355). 포커스 칸의 커서 자리는 호스트에 알려 IME 조합 창이 따라오게 한다.
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
            const ArrayView<const char> text = store.GetText(field.text);
            const UInt32 count = CountCodePoints(text.Data(), text.Size());
            const UInt32 caretIndex = ClampCaret(field, count);
            const std::size_t caretByte = ByteOffsetOf(text.Data(), text.Size(), caretIndex);
            // 조합 중인 글자는 저장 글자에 넣지 않고 보이는 텍스트에만 끼운다. 조합이 끝나면 저장 글자를 다시 옮긴다.
            const Bool composing = field.focused && field.composing && m_compositionLength > 0;
            std::size_t compositionBytes = 0;
            if (display != nullptr && composing)
            {
                m_textScratch.assign(text.Data() != nullptr ? text.Data() : "", text.Size());
                std::size_t at = caretByte;
                for (UInt32 index = 0; index < m_compositionLength; ++index)
                {
                    const std::size_t before = m_textScratch.size();
                    InsertCodePoint(m_textScratch, at, m_composition[index]);
                    at += m_textScratch.size() - before;
                }
                compositionBytes = at - caretByte;
                store.Assign(display->text, m_textScratch.data(), m_textScratch.size());
                field.mirroredRevision = revision;
                field.mirroredComposition = true;
            }
            else if (display != nullptr && (revision != field.mirroredRevision || field.mirroredComposition))
            {
                store.Assign(display->text, text.Data() != nullptr ? text.Data() : "", text.Size());
                field.mirroredRevision = revision;
                field.mirroredComposition = false;
            }
            Object::GameObject* displayObject = display != nullptr ? Internal::CanvasAccess::GetOwner(*display) : nullptr;
            // 보이는 텍스트의 커서 자리(그 오브젝트의 로컬)다. 레이아웃이 아직 없으면 거짓이다.
            const auto caretAt = [&](std::size_t byteOffset, Float& x, Float& y, Float& height)
            {
                return display != nullptr && texts != nullptr
                    && texts->GetLocalCaret(display->GetInstanceId(), UInt32(static_cast<std::uint32_t>(byteOffset)), x, y, height);
            };
            // `display` 의 **자식**일 때만 놓는다 - 아니면 놓지 않는다.
            const auto childPlace = [&](Object::GameObject* object) -> Component::Transform2D*
            {
                return object != nullptr && displayObject != nullptr && object->GetParent() == displayObject
                    ? canvas.FindComponentRaw<Component::Transform2D>(object) : nullptr;
            };
            // 선택 구간(D-355): 두 커서 자리 사이의 사각형이다. 여러 줄에 걸치면 둘러싼 사각형이다. 조합 중에는 선택이 없다.
            // `selection` 스프라이트가 없으면 보이는 텍스트의 강조 범위(D-362)로 줄마다 칠한다.
            auto* selectionSprite = Internal::ComponentHandleAccess::ResolveAs<Component::SpriteRenderer2D>(field.selection);
            if (selectionSprite == nullptr && display != nullptr)
            {
                const Bool selectedRange = field.focused && false == composing && field.selectionAnchor >= 0
                    && UInt32(static_cast<std::uint32_t>(field.selectionAnchor.Get())) != caretIndex;
                if (selectedRange)
                {
                    const Int32 anchorIndex = UInt32(static_cast<std::uint32_t>(field.selectionAnchor.Get())) < count
                        ? field.selectionAnchor : Int32(static_cast<std::int32_t>(count.Get()));
                    const Int32 caretAt = Int32(static_cast<std::int32_t>(caretIndex.Get()));
                    display->highlightBegin = anchorIndex < caretAt ? anchorIndex : caretAt;
                    display->highlightEnd = anchorIndex < caretAt ? caretAt : anchorIndex;
                }
                else
                {
                    display->highlightBegin = 0;
                    display->highlightEnd = 0;
                }
            }
            if (auto* selection = selectionSprite)
            {
                Bool shown = false;
                const Bool selected = field.focused && false == composing && field.selectionAnchor >= 0
                    && UInt32(static_cast<std::uint32_t>(field.selectionAnchor.Get())) != caretIndex;
                Component::Transform2D* place = childPlace(Internal::CanvasAccess::GetOwner(*selection));
                if (selected && place != nullptr)
                {
                    const UInt32 anchorIndex = UInt32(static_cast<std::uint32_t>(field.selectionAnchor.Get())) < count
                        ? UInt32(static_cast<std::uint32_t>(field.selectionAnchor.Get())) : count;
                    Float x1 = 0.0f;
                    Float y1 = 0.0f;
                    Float h1 = 0.0f;
                    Float x2 = 0.0f;
                    Float y2 = 0.0f;
                    Float h2 = 0.0f;
                    if (caretAt(ByteOffsetOf(text.Data(), text.Size(), anchorIndex), x1, y1, h1) && caretAt(caretByte, x2, y2, h2))
                    {
                        const Float left = x1 < x2 ? x1 : x2;
                        const Float right = x1 < x2 ? x2 : x1;
                        const Float top1 = y1 + h1 * 0.5f;
                        const Float top2 = y2 + h2 * 0.5f;
                        const Float bottom1 = y1 - h1 * 0.5f;
                        const Float bottom2 = y2 - h2 * 0.5f;
                        const Float top = top1 > top2 ? top1 : top2;
                        const Float bottom = bottom1 < bottom2 ? bottom1 : bottom2;
                        place->position = { (left + right) * 0.5f, (top + bottom) * 0.5f };
                        selection->sizeMode = Component::SpriteSizeMode::Custom;
                        selection->pivotMode = Component::SpritePivotMode::Custom;
                        selection->pivot = { 0.5f, 0.5f };
                        selection->size = { right - left, top - bottom };
                        shown = true;
                    }
                }
                selection->visible = shown;
            }
            // 조합 밑줄(D-361): 조합 글자 구간 아래의 선이다. 조합이 없거나 레이아웃이 없으면 숨긴다.
            if (auto* underline = Internal::ComponentHandleAccess::ResolveAs<Component::SpriteRenderer2D>(field.composition))
            {
                Bool shown = false;
                Component::Transform2D* place = childPlace(Internal::CanvasAccess::GetOwner(*underline));
                Float x1 = 0.0f;
                Float y1 = 0.0f;
                Float h1 = 0.0f;
                Float x2 = 0.0f;
                Float y2 = 0.0f;
                Float h2 = 0.0f;
                if (composing && compositionBytes > 0 && place != nullptr && caretAt(caretByte, x1, y1, h1) && caretAt(caretByte + compositionBytes, x2, y2, h2))
                {
                    const Float left = x1 < x2 ? x1 : x2;
                    const Float right = x1 < x2 ? x2 : x1;
                    const Float bottom1 = y1 - h1 * 0.5f;
                    const Float bottom2 = y2 - h2 * 0.5f;
                    const Float bottom = bottom1 < bottom2 ? bottom1 : bottom2;
                    const Float thickness = (h1 > h2 ? h1 : h2) * 0.1f;
                    if (right > left && thickness > 0.0f)
                    {
                        place->position = { (left + right) * 0.5f, bottom + thickness * 0.5f };
                        underline->sizeMode = Component::SpriteSizeMode::Custom;
                        underline->pivotMode = Component::SpritePivotMode::Custom;
                        underline->pivot = { 0.5f, 0.5f };
                        underline->size = { right - left, thickness };
                        shown = true;
                    }
                }
                underline->visible = shown;
            }
            // 커서: 포커스가 있을 때만 보이고 `blinkRate` 초마다 깜빡인다. 커서 자리는 조합 글자 뒤다. 레이아웃이 아직 없으면 오른쪽 끝이다.
            if (auto* caret = Internal::ComponentHandleAccess::ResolveAs<Component::SpriteRenderer2D>(field.caret))
            {
                if (false == field.focused)
                {
                    caret->visible = false;
                    field.blinkTime = 0.0f;
                }
                else
                {
                    const Float rate = Float(std::isfinite(field.blinkRate.Get()) && field.blinkRate > 0.0f ? field.blinkRate.Get() : 0.0f);
                    // 시계는 한 주기(켜짐 + 꺼짐) 안에 둔다 - 몇 시간을 켜 둬도 정밀도가 깎이지 않는다.
                    field.blinkTime = rate > 0.0f ? Float(std::fmod((field.blinkTime + deltaTime).Get(), rate.Get() * 2.0f)) : Float(0.0f);
                    caret->visible = rate <= 0.0f || field.blinkTime < rate;
                    if (Component::Transform2D* place = childPlace(Internal::CanvasAccess::GetOwner(*caret)))
                    {
                        Float caretX = 0.0f;
                        Float caretY = 0.0f;
                        Float lineHeight = 0.0f;
                        Float minX = 0.0f;
                        Float minY = 0.0f;
                        Float maxX = 0.0f;
                        Float maxY = 0.0f;
                        if (caretAt(caretByte + compositionBytes, caretX, caretY, lineHeight))
                        {
                            place->position = { caretX, caretY };
                        }
                        else if (display != nullptr && texts != nullptr && texts->GetLocalBounds(display->GetInstanceId(), minX, minY, maxX, maxY))
                        {
                            place->position = { maxX, (minY + maxY) * 0.5f };
                        }
                        else
                        {
                            place->position = {};
                        }
                    }
                }
            }
            // IME 조합 창(D-355): 포커스 칸의 커서 자리를 화면 픽셀로 호스트에 알린다 - 호스트가 플랫폼의 조합 창을 옮기고 인라인 조합을 켠다.
            if (field.focused && m_textInput != nullptr && displayObject != nullptr)
            {
                const auto* transform = canvas.FindComponentRaw<Component::Transform2D>(displayObject);
                const Layer* layer = displayObject->GetLayer();
                Float caretX = 0.0f;
                Float caretY = 0.0f;
                Float lineHeight = 0.0f;
                if (transform != nullptr && transform->worldValid && layer != nullptr)
                {
                    Float topX = 0.0f;
                    Float topY = 0.0f;
                    Float bottomX = 0.0f;
                    Float bottomY = 0.0f;
                    Float topPixelX = 0.0f;
                    Float topPixelY = 0.0f;
                    Float bottomPixelX = 0.0f;
                    Float bottomPixelY = 0.0f;
                    if (caretAt(caretByte + compositionBytes, caretX, caretY, lineHeight)
                        && Apply(transform->world, caretX, caretY + lineHeight * 0.5f, topX, topY)
                        && Apply(transform->world, caretX, caretY - lineHeight * 0.5f, bottomX, bottomY)
                        && LayerToPixel(*layer, { topX, topY }, topPixelX, topPixelY)
                        && LayerToPixel(*layer, { bottomX, bottomY }, bottomPixelX, bottomPixelY))
                    {
                        m_textInput->SetTextInputArea(topPixelX, (topPixelY + bottomPixelY) * 0.5f, Float(std::fabs((bottomPixelY - topPixelY).Get())));
                    }
                    else if (LayerToPixel(*layer, transform->worldPosition, topPixelX, topPixelY))
                    {
                        m_textInput->SetTextInputArea(topPixelX, topPixelY, 0.0f);
                    }
                }
            }
        });
    }

    void Button2DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        // 다음 프레임의 자동 반복이 쓰는 시간이다(입력 체인은 갱신 훅보다 앞이다).
        m_frameDelta = 0.0f;
        if (std::isfinite(deltaTime.Get()) && deltaTime > 0.0f)
        {
            m_frameDelta = deltaTime;
        }
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
            const Float rate = Float(std::isfinite(scroll.decelerationRate.Get()) ? Float::Clamp(scroll.decelerationRate, 0.0f, 1.0f) : 0.0f);
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
        m_pointer = pointer;
        m_clock = m_clock + m_frameDelta;

        // 모달(D-357): 켜진 것이 있으면 그 안의 위젯만 후보이고, 게임까지 막는 것이 있으면 이번 프레임의 장치를 모두 지운다.
        m_modalActive = false;
        m_modalBlocks = false;
        m_canvas->ForEach<Component::Modal2D>([&](Component::Modal2D& modal)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(modal);
            if (owner != nullptr && modal.IsActiveComponent() && owner->IsActiveInHierarchy())
            {
                m_modalActive = true;
                m_modalBlocks = m_modalBlocks || modal.blockGame;
            }
        });

        // 0. 스크립트가 글자 칸의 `focused` 를 썼으면 받는다 - 참이면 그 칸이 포커스를 받고(여럿이면 첫 것만), 지금 포커스인 칸이 거짓이면 놓는다.
        {
            Object::GameObject* wanted = nullptr;
            Bool dropped = false;
            m_canvas->ForEach<Component::InputField2D>([&](Component::InputField2D& field)
            {
                Object::GameObject* owner = Internal::CanvasAccess::GetOwner(field);
                if (owner == nullptr || false == field.IsActiveComponent())
                {
                    return;
                }
                const Bool current = owner->GetInstanceId() == m_focused && m_focusedKind == WidgetKind::Field;
                if (field.focused && false == current && wanted == nullptr)
                {
                    wanted = owner;
                }
                else if (false == field.focused && current)
                {
                    dropped = true;
                }
            });
            if (wanted != nullptr)
            {
                SetFocus(wanted, WidgetKind::Field);
            }
            else if (dropped)
            {
                SetFocus(nullptr, WidgetKind::None);
            }
        }

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
            if (owner == nullptr || false == InsideModal(owner) || false == IsGroupInteractable2D(*m_canvas, owner))
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
            // 꺼진 위젯·감춘 레이어는 없는 것이다. 꺼진 위젯(`interactable` 거짓)은 보이므로 포인터를 받는다. 그룹이 포인터를 지나가게 하면(`blocksRaycasts` 거짓)
            // 후보가 아니다(D-361) - 포커스는 위에서 이미 지켰다.
            Vector2 local;
            if (false == pointer.present || false == active || false == owner->IsActiveInHierarchy() || layer == nullptr
                || false == layer->IsVisible() || false == IsGroupBlockingRaycasts2D(*m_canvas, owner) || false == HitRect(*owner, *layer, size, offset, pointer, local))
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
        m_canvas->ForEach<Component::Dropdown2D>([&](Component::Dropdown2D& dropdown)
        {
            dropdown.changed = false;
            consider(Internal::CanvasAccess::GetOwner(dropdown), WidgetKind::Dropdown, dropdown.interactable, dropdown.size, dropdown.offset, dropdown.IsActiveComponent());
        });
        m_canvas->ForEach<Component::Draggable2D>([&](Component::Draggable2D& draggable)
        {
            draggable.droppedOn = {};
            consider(Internal::CanvasAccess::GetOwner(draggable), WidgetKind::Draggable, draggable.interactable, draggable.size, draggable.offset, draggable.IsActiveComponent());
        });
        m_canvas->ForEach<Component::DropTarget2D>([&](Component::DropTarget2D& target)
        {
            target.dropped = {};
            target.hovering = false;
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
        if (m_pressed != InvalidInstanceId && pressedBefore == nullptr)
        {
            // 쥔 위젯이 사라졌다 - 끌기도 끝이다.
            m_scrollDragging = false;
            m_scrollCandidate = InvalidInstanceId;
            m_dragDelta = {};
        }
        // 누름·탐색이 포커스를 옮기면 그 뒤의 훅은 새 포커스에 간다 - 번호로 다시 푼다.
        const auto resolveFocused = [&]() {
            focusedObject = nullptr;
            if (m_focused == InvalidInstanceId)
            {
                return;
            }
            const auto take = [&](Object::GameObject* owner, Bool active) {
                if (owner != nullptr && owner->GetInstanceId() == m_focused && active && owner->IsActiveInHierarchy())
                {
                    focusedObject = owner;
                }
            };
            switch (m_focusedKind)
            {
            case WidgetKind::Button:
                m_canvas->ForEach<Component::Button2D>([&](Component::Button2D& button) {
                    take(Internal::CanvasAccess::GetOwner(button), button.IsActiveComponent());
                });
                break;
            case WidgetKind::Toggle:
                m_canvas->ForEach<Component::Toggle2D>([&](Component::Toggle2D& toggle) {
                    take(Internal::CanvasAccess::GetOwner(toggle), toggle.IsActiveComponent());
                });
                break;
            case WidgetKind::Slider:
                m_canvas->ForEach<Component::Slider2D>([&](Component::Slider2D& slider) {
                    take(Internal::CanvasAccess::GetOwner(slider), slider.IsActiveComponent());
                });
                break;
            case WidgetKind::Field:
                m_canvas->ForEach<Component::InputField2D>([&](Component::InputField2D& field) {
                    take(Internal::CanvasAccess::GetOwner(field), field.IsActiveComponent());
                });
                break;
            case WidgetKind::Dropdown:
                m_canvas->ForEach<Component::Dropdown2D>([&](Component::Dropdown2D& dropdown) {
                    take(Internal::CanvasAccess::GetOwner(dropdown), dropdown.IsActiveComponent());
                });
                break;
            case WidgetKind::Draggable:
                m_canvas->ForEach<Component::Draggable2D>([&](Component::Draggable2D& draggable) {
                    take(Internal::CanvasAccess::GetOwner(draggable), draggable.IsActiveComponent());
                });
                break;
            default:
                break;
            }
        };

        // 2. 호버가 바뀌면 떠난 쪽이 먼저다. 들어옴·나감 훅은 버튼의 것이다.
        if (hoveredBefore != top.owner || m_hoveredKind != top.kind)
        {
            if (m_hoveredKind == WidgetKind::Button)
            {
                CallHook(hoveredBefore, Hook::Exit);
            }
            if (top.kind == WidgetKind::Button)
            {
                if (const auto* button = m_canvas->FindComponentRaw<Component::Button2D>(top.owner))
                {
                    PlayUiSound(button->hoverSound);
                }
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
            // 가상 키보드의 버튼은 포커스를 가져가지 않는다(D-358) - 칸이 포커스를 지켜야 글자가 들어간다.
            const Bool onKeyboard = top.owner != nullptr && top.kind == WidgetKind::Button && FindKeyboardAncestor(top.owner) != nullptr;
            if (onKeyboard)
            {
                // 그대로 둔다.
            }
            else if (top.owner != nullptr && top.interactable && top.kind != WidgetKind::Scroll)
            {
                SetFocus(top.owner, top.kind);
            }
            else
            {
                SetFocus(nullptr, WidgetKind::None);
            }
            resolveFocused();
            // 글자 칸을 누르면 누른 자리에 커서가 선다(D-355). 같은 칸을 바로 다시 누르면(더블 클릭) 그 자리의 단어를 고른다(D-361).
            if (top.owner != nullptr && top.kind == WidgetKind::Field && top.interactable)
            {
                if (auto* field = m_canvas->FindComponentRaw<Component::InputField2D>(top.owner))
                {
                    PlaceCaretAt(*field, pointer, false);
                    const InstanceId id = top.owner->GetInstanceId();
                    const Bool again = id == m_lastFieldPress && m_clock - m_lastFieldPressTime <= DoubleClickTime
                        && std::fabs(pointer.x - m_lastFieldPressX) <= DoubleClickDistance && std::fabs(pointer.y - m_lastFieldPressY) <= DoubleClickDistance;
                    if (again)
                    {
                        SelectWordAtCaret(*field);
                        m_lastFieldPress = InvalidInstanceId;
                    }
                    else
                    {
                        m_lastFieldPress = id;
                        m_lastFieldPressTime = m_clock;
                        m_lastFieldPressX = pointer.x;
                        m_lastFieldPressY = pointer.y;
                    }
                }
            }
            // 열린 드롭다운은 제 사각형·목록 밖을 누르면 접힌다(D-355).
            CloseDropdownsOutside(top.owner);
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
            m_sliderPaging = false;
            m_pageHeld = 0.0f;
            // 스크롤바의 손잡이 밖을 눌렀으면 한 페이지씩이다(D-360) - 포인터를 따라가지 않는다.
            if (top.kind == WidgetKind::Slider)
            {
                if (auto* slider = m_canvas->FindComponentRaw<Component::Slider2D>(pressed))
                {
                    Float step = 0.0f;
                    Float knobHalf = 0.0f;
                    const Float pointerValue = SliderValueAt(*slider, top.local);
                    if (ScrollbarPage(*pressed, step, knobHalf) && std::fabs(pointerValue - Float::Clamp(slider->value, 0.0f, 1.0f)) > knobHalf)
                    {
                        m_sliderPaging = true;
                        m_pageForward = pointerValue > slider->value;
                        PageScrollbar(*pressed, *slider, pointerValue, m_pageForward);
                    }
                }
            }
            // 누른 것이 스크롤 뷰거나 그 안의 버튼·토글·글자 칸이면 끌기가 스크롤이 된다. 슬라이더는 제 끌기가 먼저다.
            Object::GameObject* scrollOwner = top.kind == WidgetKind::Scroll ? top.owner
                : (top.kind == WidgetKind::Slider || top.kind == WidgetKind::Draggable) ? nullptr : FindScrollAncestor(top.owner);
            m_scrollCandidate = scrollOwner != nullptr ? scrollOwner->GetInstanceId() : InvalidInstanceId;
            if (top.kind == WidgetKind::Button)
            {
                CallHook(pressed, Hook::Down);
            }
            // 끌기 시작(D-357): 제자리를 떠 두고 알린다.
            if (top.kind == WidgetKind::Draggable)
            {
                if (auto* draggable = m_canvas->FindComponentRaw<Component::Draggable2D>(pressed))
                {
                    if (auto* place = m_canvas->FindComponentRaw<Component::Transform2D>(pressed))
                    {
                        draggable->startPosition = place->position;
                        draggable->startValid = true;
                    }
                    draggable->dragging = true;
                    // 끌리는 동안 맨 위에(D-360).
                    draggable->lifted = draggable->liftOrder;
                    m_lifted.Clear();
                    if (draggable->lifted != 0)
                    {
                        LiftSubtree(*pressed, draggable->lifted);
                    }
                    CallHook(pressed, Hook::DragBegin);
                }
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
        // 슬라이더는 쥔 동안 포인터의 자리가 곧 값이다 - 사각형을 벗어나도 그 축의 끝에 붙는다. 트랙 누름은 쥔 동안 되풀이해 한 페이지씩이다(D-360).
        if (pressed != nullptr && pressedKind == WidgetKind::Slider && pointer.present && pointer.down)
        {
            auto* slider = m_canvas->FindComponentRaw<Component::Slider2D>(pressed);
            const Layer* layer = pressed->GetLayer();
            Vector2 local;
            if (m_sliderPaging)
            {
                // 쥔 동안 되풀이한다 - 손잡이가 닿으면 `PageScrollbar` 가 멈추고, 포인터가 더 가면 다시 간다. 놓기 전에는 끌기로 바뀌지 않는다.
                if (slider != nullptr && layer != nullptr && false == pointer.pressed && PointerToLocal(*pressed, *layer, pointer, local)
                    && Repeats(true, false, m_pageHeld, NavigateRepeatInterval))
                {
                    PageScrollbar(*pressed, *slider, SliderValueAt(*slider, local), m_pageForward);
                }
            }
            else if (slider != nullptr && layer != nullptr && PointerToLocal(*pressed, *layer, pointer, local))
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
        // 끌기(D-357): 쥔 동안 포인터가 간 만큼 오브젝트가 따라온다(레이어 단위). 놓을 자리의 `hovering` 은 끌리는 동안 포인터가 그 사각형 안인가다.
        if (pressed != nullptr && pressedKind == WidgetKind::Draggable && pointer.present && pointer.down)
        {
            const Layer* layer = pressed->GetLayer();
            auto* place = m_canvas->FindComponentRaw<Component::Transform2D>(pressed);
            Vector2 from;
            Vector2 to;
            if (false == pointer.pressed && place != nullptr && layer != nullptr
                && PixelToLayer(*layer, m_lastPixelX, m_lastPixelY, from) && PixelToLayer(*layer, pointer.x, pointer.y, to))
            {
                place->position.x += to.x - from.x;
                place->position.y += to.y - from.y;
            }
            m_lastPixelX = pointer.x;
            m_lastPixelY = pointer.y;
            m_canvas->ForEach<Component::DropTarget2D>([&](Component::DropTarget2D& target)
            {
                Object::GameObject* owner = Internal::CanvasAccess::GetOwner(target);
                const Layer* targetLayer = owner != nullptr ? owner->GetLayer() : nullptr;
                Vector2 local;
                target.hovering = owner != nullptr && targetLayer != nullptr && target.IsActiveComponent() && owner->IsActiveInHierarchy() && InsideModal(owner)
                    && HitRect(*owner, *targetLayer, target.size, target.offset, pointer, local);
            });
        }
        // 글자 칸은 쥔 동안 포인터가 가리키는 글자까지 선택이다(D-355). 스크롤 뷰 안의 글자 칸은 끌기가 스크롤이라 선택하지 않는다.
        if (pressed != nullptr && pressedKind == WidgetKind::Field && pointer.present && pointer.down && false == pointer.pressed
            && m_scrollCandidate == InvalidInstanceId)
        {
            if (auto* field = m_canvas->FindComponentRaw<Component::InputField2D>(pressed))
            {
                PlaceCaretAt(*field, pointer, true);
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
            m_sliderPaging = false;
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
                    PlayUiSound(button->clickSound);
                }
                CallHook(released, Hook::Click);
                // 가상 키보드의 버튼이면 글자 칸에 넣는다(D-358). 훅이 버튼을 지웠을 수 있으니 번호로 다시 찾는다.
                if (Object::GameObject* keyboardOwner = FindKeyboardAncestor(released))
                {
                    PressVirtualKey(*keyboardOwner, released, false);
                }
            }
            else if (onTop && releasedKind == WidgetKind::Toggle)
            {
                if (auto* toggle = m_canvas->FindComponentRaw<Component::Toggle2D>(released))
                {
                    FlipToggle(*released, *toggle);
                }
            }
            else if (onTop && releasedKind == WidgetKind::Dropdown)
            {
                if (auto* dropdown = m_canvas->FindComponentRaw<Component::Dropdown2D>(released))
                {
                    if (dropdown->open)
                    {
                        dropdown->open = false;
                    }
                    else
                    {
                        OpenDropdown(*dropdown);
                    }
                }
            }
            else if (releasedKind == WidgetKind::Draggable)
            {
                DropDraggable(*released, pointer);
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
                    const Float sensitivity = Float(std::isfinite(scroll->scrollSensitivity.Get()) ? scroll->scrollSensitivity.Get() : 0.0f);
                    ScrollBy(*scrollOwner, *scroll, { mouse.wheelX * sensitivity, -mouse.wheelY * sensitivity }, clampedX, clampedY);
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
            Bool erasePressed = false;
            Float stickX = 0.0f;
            Float stickY = 0.0f;
            // 쥐고 있는 방향이다(자동 반복, D-354). 십자키가 스틱보다 먼저다.
            Int32 heldX = 0;
            Int32 heldY = 0;
            for (UInt32 index = 0; index < MaxGamepads; ++index)
            {
                const GamepadState& pad = input.Gamepad(index);
                if (false == pad.connected)
                {
                    continue;
                }
                if (pad.IsPressed(GamepadButton::DPadLeft))
                {
                    directionX = -1;
                }
                if (pad.IsPressed(GamepadButton::DPadRight))
                {
                    directionX = 1;
                }
                if (pad.IsPressed(GamepadButton::DPadUp))
                {
                    directionY = -1;
                }
                if (pad.IsPressed(GamepadButton::DPadDown))
                {
                    directionY = 1;
                }
                if (pad.IsDown(GamepadButton::DPadLeft))
                {
                    heldX = -1;
                }
                if (pad.IsDown(GamepadButton::DPadRight))
                {
                    heldX = 1;
                }
                if (pad.IsDown(GamepadButton::DPadUp))
                {
                    heldY = -1;
                }
                if (pad.IsDown(GamepadButton::DPadDown))
                {
                    heldY = 1;
                }
                confirmPressed = confirmPressed || pad.IsPressed(GamepadButton::South);
                confirmReleased = confirmReleased || pad.IsReleased(GamepadButton::South);
                erasePressed = erasePressed || pad.IsPressed(GamepadButton::East);
                const Float x = pad.GetAxis(GamepadAxis::LeftX);
                const Float y = pad.GetAxis(GamepadAxis::LeftY);
                stickX = std::fabs(x) > std::fabs(stickX) ? x : stickX;
                stickY = std::fabs(y) > std::fabs(stickY) ? y : stickY;
            }
            // 스틱은 문턱을 넘는 순간이 한 번이다(위가 +y 라 화면의 아래 방향과 반대다).
            if (stickX >= StickThreshold && m_stickX < StickThreshold)
            {
                directionX = 1;
            }
            if (stickX <= -StickThreshold && m_stickX > -StickThreshold)
            {
                directionX = -1;
            }
            if (stickY >= StickThreshold && m_stickY < StickThreshold)
            {
                directionY = -1;
            }
            if (stickY <= -StickThreshold && m_stickY > -StickThreshold)
            {
                directionY = 1;
            }
            m_stickX = stickX;
            m_stickY = stickY;
            if (heldX == 0 && stickX >= StickThreshold)
            {
                heldX = 1;
            }
            if (heldX == 0 && stickX <= -StickThreshold)
            {
                heldX = -1;
            }
            if (heldY == 0 && stickY >= StickThreshold)
            {
                heldY = -1;
            }
            if (heldY == 0 && stickY <= -StickThreshold)
            {
                heldY = 1;
            }
            // 자동 반복(D-354): 같은 방향을 쥐고 있으면 `RepeatDelay` 뒤 `NavigateRepeatInterval` 마다 다시 옮긴다. 새로 누르거나 방향이 바뀌면 처음부터 센다.
            if (heldX == 0 && heldY == 0)
            {
                m_navigateHeld = 0.0f;
                m_navigateHeldX = 0;
                m_navigateHeldY = 0;
            }
            else if (directionX != 0 || directionY != 0 || heldX != m_navigateHeldX || heldY != m_navigateHeldY)
            {
                m_navigateHeld = 0.0f;
                m_navigateHeldX = heldX;
                m_navigateHeldY = heldY;
            }
            else
            {
                m_navigateHeld = m_navigateHeld + m_frameDelta;
                if (m_navigateHeld >= RepeatDelay)
                {
                    m_navigateHeld = m_navigateHeld - NavigateRepeatInterval;
                    directionX = heldX;
                    directionY = heldY;
                }
            }

            if (directionX != 0 || directionY != 0)
            {
                usedGamepad = true;
                auto* slider = focusedObject != nullptr && m_focusedKind == WidgetKind::Slider
                    ? m_canvas->FindComponentRaw<Component::Slider2D>(focusedObject) : nullptr;
                auto* openDropdown = focusedObject != nullptr && m_focusedKind == WidgetKind::Dropdown
                    ? m_canvas->FindComponentRaw<Component::Dropdown2D>(focusedObject) : nullptr;
                if (openDropdown != nullptr && false == openDropdown->open)
                {
                    openDropdown = nullptr;
                }
                if (openDropdown != nullptr)
                {
                    // 펴진 드롭다운(D-360): 위아래가 돋움이고 좌우는 아무것도 아니다 - 포커스는 목록에 머문다.
                    if (directionY != 0)
                    {
                        MoveHighlight(*openDropdown, directionY);
                    }
                }
                else if (slider != nullptr && directionX != 0)
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
                    resolveFocused();
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
            if (m_padPressed)
            {
                // 쥐고 있는 중간 프레임도 UI 의 것이다 - 게임이 같은 누름을 보지 않는다.
                usedGamepad = true;
            }
            if (confirmReleased && m_padPressed && focusedObject != nullptr)
            {
                m_padPressed = false;
                if (m_focusedKind == WidgetKind::Button)
                {
                    CallHook(focusedObject, Hook::Up);
                    if (auto* button = m_canvas->FindComponentRaw<Component::Button2D>(focusedObject))
                    {
                        button->clicked = true;
                        PlayUiSound(button->clickSound);
                    }
                    CallHook(focusedObject, Hook::Click);
                    // 가상 키보드의 키였으면 글자를 넣는다(D-360). 훅이 키를 지웠을 수 있으니 번호로 다시 찾는다.
                    resolveFocused();
                    if (Object::GameObject* keyboardOwner = focusedObject != nullptr ? FindKeyboardAncestor(focusedObject) : nullptr)
                    {
                        PressVirtualKey(*keyboardOwner, focusedObject, true);
                    }
                }
                else if (m_focusedKind == WidgetKind::Toggle)
                {
                    if (auto* toggle = m_canvas->FindComponentRaw<Component::Toggle2D>(focusedObject))
                    {
                        FlipToggle(*focusedObject, *toggle);
                    }
                }
                else if (m_focusedKind == WidgetKind::Dropdown)
                {
                    if (auto* dropdown = m_canvas->FindComponentRaw<Component::Dropdown2D>(focusedObject))
                    {
                        if (dropdown->open)
                        {
                            PickHighlight(*focusedObject, *dropdown);
                        }
                        else
                        {
                            OpenDropdown(*dropdown);
                        }
                    }
                }
            }
            // 키보드 안에 포커스가 있으면 `East` 가 지우기다(D-360).
            if (erasePressed && focusedObject != nullptr)
            {
                if (Object::GameObject* keyboardOwner = FindKeyboardAncestor(focusedObject))
                {
                    usedGamepad = true;
                    PressVirtualKey(*keyboardOwner, nullptr, true);
                }
            }
            // 포커스가 바뀌었을 수 있다 - 뒤의 상태 칸은 번호로 본다.
        }

        // 5. 키보드(D-352): 글자 칸에 포커스가 있으면 글자를 받고 키보드를 소비한다.
        // 훅(`OnValueChanged`·`OnSubmit`)은 풀 순회 밖에서 부른다 - 훅이 글자 칸을 붙이거나 떼도 순회가 흔들리지 않는다.
        Bool usedKeyboard = false;
        if (m_focusedKind == WidgetKind::Field)
        {
            resolveFocused();
            auto* field = focusedObject != nullptr ? m_canvas->FindComponentRaw<Component::InputField2D>(focusedObject) : nullptr;
            if (field != nullptr && field->interactable)
            {
                // 조합 중인 글자를 떠 둔다(D-355) - 갱신 훅이 보이는 텍스트의 커서 자리에 끼운다.
                const KeyboardState& keyboard = input.Keyboard();
                m_compositionLength = keyboard.GetCompositionLength() < MaxTextPerFrame ? keyboard.GetCompositionLength() : MaxTextPerFrame;
                for (UInt32 index = 0; index < m_compositionLength; ++index)
                {
                    m_composition[index] = keyboard.GetComposition(index);
                }
                field->composing = m_compositionLength > 0;
                TypeInto(*focusedObject, *field, input);
                usedKeyboard = true;
            }
        }
        // 포커스 드롭다운(D-360): Enter·Space 가 펴고, 펴진 동안 ↑·↓ 가 돋움을 옮기며(쥐면 되풀이) Enter 가 고르고 Esc 가 접는다. 펴진 동안만 키보드를 소비한다.
        if (m_focusedKind == WidgetKind::Dropdown)
        {
            resolveFocused();
            auto* dropdown = focusedObject != nullptr ? m_canvas->FindComponentRaw<Component::Dropdown2D>(focusedObject) : nullptr;
            const KeyboardState& keyboard = input.Keyboard();
            if (dropdown != nullptr && dropdown->interactable && dropdown->IsActiveComponent())
            {
                const Bool confirm = keyboard.IsPressed(Key::Enter) || keyboard.IsPressed(Key::Space);
                if (dropdown->open)
                {
                    usedKeyboard = true;
                    const Bool up = Repeats(keyboard.IsDown(Key::Up), keyboard.IsPressed(Key::Up), m_dropdownHeld[0], NavigateRepeatInterval);
                    const Bool down = Repeats(keyboard.IsDown(Key::Down), keyboard.IsPressed(Key::Down), m_dropdownHeld[1], NavigateRepeatInterval);
                    if (up)
                    {
                        MoveHighlight(*dropdown, -1);
                    }
                    if (down)
                    {
                        MoveHighlight(*dropdown, 1);
                    }
                    if (confirm)
                    {
                        PickHighlight(*focusedObject, *dropdown);
                    }
                    else if (keyboard.IsPressed(Key::Escape))
                    {
                        dropdown->open = false;
                    }
                }
                else if (confirm)
                {
                    usedKeyboard = true;
                    OpenDropdown(*dropdown);
                }
            }
        }
        if (m_focusedKind != WidgetKind::Dropdown)
        {
            m_dropdownHeld[0] = 0.0f;
            m_dropdownHeld[1] = 0.0f;
        }
        if (false == usedKeyboard)
        {
            for (Float& held : m_keyHeld)
            {
                held = 0.0f;
            }
            m_compositionLength = 0;
            // Esc 는 열린 드롭다운을 접는다(D-355). 소비하지는 않는다.
            if (input.Keyboard().IsPressed(Key::Escape))
            {
                CloseDropdownsOutside(nullptr);
            }
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
        // 펴진 드롭다운의 돋운 항목은 호버처럼 보인다(D-360). 접힌 드롭다운의 돋움은 지운다.
        InstanceId highlightedOption = InvalidInstanceId;
        m_canvas->ForEach<Component::Dropdown2D>([&](Component::Dropdown2D& dropdown)
        {
            if (false == dropdown.open)
            {
                dropdown.highlighted = -1;
                return;
            }
            Object::GameObject* list = Internal::GameObjectHandleAccess::Resolve(dropdown.list);
            if (list == nullptr || dropdown.highlighted < 0 || highlightedOption != InvalidInstanceId)
            {
                return;
            }
            Int32 index = 0;
            auto find = [&](Object::GameObject& option, Component::Toggle2D&)
            {
                if (index == dropdown.highlighted)
                {
                    highlightedOption = option.GetInstanceId();
                }
                ++index;
            };
            VisitToggles(*m_canvas, *list, find);
        });
        m_canvas->ForEach<Component::Toggle2D>([&](Component::Toggle2D& toggle)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(toggle);
            const InstanceId id = owner != nullptr ? owner->GetInstanceId() : InvalidInstanceId;
            toggle.hovered = id != InvalidInstanceId && ((id == m_hovered && m_hoveredKind == WidgetKind::Toggle) || id == highlightedOption);
            toggle.pressed = id != InvalidInstanceId && id == m_pressed && m_pressedKind == WidgetKind::Toggle;
            toggle.focused = id != InvalidInstanceId && id == m_focused && m_focusedKind == WidgetKind::Toggle;
            if (toggle.IsActiveComponent())
            {
                ApplyTransition(*m_canvas, toggle, owner, m_focusVisible);
            }
            else
            {
                RestoreSwap(toggle, TargetGraphic(*m_canvas, toggle, owner));
            }
        });
        m_canvas->ForEach<Component::Slider2D>([&](Component::Slider2D& slider)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(slider);
            const InstanceId id = owner != nullptr ? owner->GetInstanceId() : InvalidInstanceId;
            slider.hovered = id != InvalidInstanceId && id == m_hovered && m_hoveredKind == WidgetKind::Slider;
            slider.pressed = id != InvalidInstanceId && id == m_pressed && m_pressedKind == WidgetKind::Slider;
            slider.focused = id != InvalidInstanceId && id == m_focused && m_focusedKind == WidgetKind::Slider;
            if (slider.IsActiveComponent())
            {
                ApplyTransition(*m_canvas, slider, owner, m_focusVisible);
            }
            else
            {
                RestoreSwap(slider, TargetGraphic(*m_canvas, slider, owner));
            }
        });
        m_canvas->ForEach<Component::InputField2D>([&](Component::InputField2D& field)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(field);
            const InstanceId id = owner != nullptr ? owner->GetInstanceId() : InvalidInstanceId;
            field.hovered = id != InvalidInstanceId && id == m_hovered && m_hoveredKind == WidgetKind::Field;
            field.pressed = id != InvalidInstanceId && id == m_pressed && m_pressedKind == WidgetKind::Field;
            field.focused = id != InvalidInstanceId && id == m_focused && m_focusedKind == WidgetKind::Field;
            if (false == field.focused)
            {
                // 포커스를 잃으면 선택도 조합도 없다.
                field.composing = false;
                field.selectionAnchor = -1;
            }
            if (field.IsActiveComponent())
            {
                ApplyTransition(*m_canvas, field, owner, m_focusVisible);
            }
            else
            {
                RestoreSwap(field, TargetGraphic(*m_canvas, field, owner));
            }
        });
        m_canvas->ForEach<Component::Draggable2D>([&](Component::Draggable2D& draggable)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(draggable);
            const InstanceId id = owner != nullptr ? owner->GetInstanceId() : InvalidInstanceId;
            draggable.dragging = id != InvalidInstanceId && id == m_pressed && m_pressedKind == WidgetKind::Draggable;
        });
        // 툴팁(D-357): 포인터를 잡지 않는다 - 누르지 않은 포인터가 사각형 위에 머문 시간을 센다(지난 갱신 훅의 델타). `delay` 를 넘기면 보인다.
        m_canvas->ForEach<Component::Tooltip2D>([&](Component::Tooltip2D& tooltip)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(tooltip);
            const Layer* layer = owner != nullptr ? owner->GetLayer() : nullptr;
            Vector2 local;
            const Bool hovered = owner != nullptr && layer != nullptr && tooltip.IsActiveComponent() && owner->IsActiveInHierarchy() && layer->IsVisible()
                && IsGroupBlockingRaycasts2D(*m_canvas, owner)
                && pointer.present && false == pointer.down && InsideModal(owner) && HitRect(*owner, *layer, tooltip.size, tooltip.offset, pointer, local);
            tooltip.hoverTime = hovered ? Float(tooltip.hoverTime + m_frameDelta) : Float(0.0f);
            const Float delay = Float(std::isfinite(tooltip.delay.Get()) && tooltip.delay > 0.0f ? tooltip.delay.Get() : 0.0f);
            tooltip.shown = hovered && tooltip.hoverTime >= delay;
        });
        m_canvas->ForEach<Component::Dropdown2D>([&](Component::Dropdown2D& dropdown)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(dropdown);
            const InstanceId id = owner != nullptr ? owner->GetInstanceId() : InvalidInstanceId;
            dropdown.hovered = id != InvalidInstanceId && id == m_hovered && m_hoveredKind == WidgetKind::Dropdown;
            dropdown.pressed = id != InvalidInstanceId && ((id == m_pressed && m_pressedKind == WidgetKind::Dropdown)
                || (m_padPressed && id == m_focused && m_focusedKind == WidgetKind::Dropdown));
            dropdown.focused = id != InvalidInstanceId && id == m_focused && m_focusedKind == WidgetKind::Dropdown;
            if (dropdown.IsActiveComponent())
            {
                ApplyTransition(*m_canvas, dropdown, owner, m_focusVisible);
            }
            else
            {
                RestoreSwap(dropdown, TargetGraphic(*m_canvas, dropdown, owner));
            }
        });

        // 7. 위젯 위거나 쥐고 있으면 그 장치를 아래에서 지운다. 입력은 막지 않는다 - UI 가 쓴 게임패드·키보드만 지운다.
        m_pointerOver = top.owner != nullptr || m_pressed != InvalidInstanceId || m_modalBlocks;
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
        if (usedKeyboard || m_modalBlocks)
        {
            input.Consume(InputDevice::Keyboard);
        }
        if (m_modalBlocks)
        {
            input.Consume(InputDevice::Gamepad);
        }
        return InputResult::Pass;
    }
}
