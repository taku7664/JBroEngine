#include <JBro/Framework2DSystem/System/Layout2DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/ContentSizeFitter2D.h>
#include <JBro/Framework2D/Component/Dropdown2D.h>
#include <JBro/Framework2D/Component/InputField2D.h>
#include <JBro/Framework2D/Component/Layout2D.h>
#include <JBro/Framework2D/Component/Mask2D.h>
#include <JBro/Framework2D/Component/Slider2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2D/Component/Toggle2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Rendering/SpriteLibrary.h>
#include <JBro/Framework2DSystem/System/Text2DSystem.h>
#include <JBro/Runtime/GameObject.h>

#include <JBro/Types/ValueMath.h>

#include <cmath>

namespace JBro::System
{
    namespace
    {
        Bool Finite(Vector2 value)
        {
            return std::isfinite(value.x.Get()) && std::isfinite(value.y.Get());
        }

        // 같은 오브젝트의 위젯 사각형이다(버튼·토글·슬라이더·글자 칸·드롭다운·마스크 가운데 처음 것). 없으면 거짓이다.
        Bool WidgetRect(Canvas& canvas, Object::GameObject& object, Vector2*& size)
        {
            if (auto* button = canvas.FindComponentRaw<Component::Button2D>(&object))
            {
                size = &button->size;
                return true;
            }
            if (auto* toggle = canvas.FindComponentRaw<Component::Toggle2D>(&object))
            {
                size = &toggle->size;
                return true;
            }
            if (auto* slider = canvas.FindComponentRaw<Component::Slider2D>(&object))
            {
                size = &slider->size;
                return true;
            }
            if (auto* field = canvas.FindComponentRaw<Component::InputField2D>(&object))
            {
                size = &field->size;
                return true;
            }
            if (auto* dropdown = canvas.FindComponentRaw<Component::Dropdown2D>(&object))
            {
                size = &dropdown->size;
                return true;
            }
            return false;
        }

        // **오브젝트의 보이는 크기**(D-355). 레이아웃 블록 → 글자(지난 프레임의 레이아웃) → 스프라이트(`Custom` 크기나 에셋의 칸 크기) → 위젯 사각형 → 마스크 차례다.
        // 없거나 유한하지 않으면 거짓이다.
        Bool VisibleExtent(Canvas& canvas, SpriteLibrary* sprites, const Text2DSystem* texts, Object::GameObject& object, Vector2& extent)
        {
            if (const auto* layout = canvas.FindComponentRaw<Component::Layout2D>(&object))
            {
                extent = { layout->contentWidth, layout->contentHeight };
                return Finite(extent);
            }
            if (const auto* text = canvas.FindComponentRaw<Component::Text2D>(&object))
            {
                Float minX = 0.0f;
                Float minY = 0.0f;
                Float maxX = 0.0f;
                Float maxY = 0.0f;
                if (texts != nullptr && texts->GetLocalBounds(text->GetInstanceId(), minX, minY, maxX, maxY))
                {
                    extent = { maxX - minX, maxY - minY };
                    return Finite(extent);
                }
            }
            if (const auto* sprite = canvas.FindComponentRaw<Component::SpriteRenderer2D>(&object))
            {
                if (sprite->sizeMode == Component::SpriteSizeMode::Custom)
                {
                    extent = { std::fabs(sprite->size.x), std::fabs(sprite->size.y) };
                    return Finite(extent);
                }
                SpriteFrameView frame;
                AssetHandle texture;
                Float uv[4] = {};
                if (sprites != nullptr && sprite->sprite.generation != 0 && sprites->Resolve(sprite->sprite, sprite->frameIndex, texture, uv, &frame))
                {
                    const Layer* layer = object.GetLayer();
                    const Bool screen = layer != nullptr && layer->GetSpace() == LayerSpace::Screen;
                    extent = screen ? Vector2{ frame.widthPixels, frame.heightPixels } : Vector2{ frame.widthUnits, frame.heightUnits };
                    return Finite(extent);
                }
            }
            Vector2* rect = nullptr;
            if (WidgetRect(canvas, object, rect))
            {
                extent = { std::fabs(rect->x), std::fabs(rect->y) };
                return Finite(extent);
            }
            if (const auto* mask = canvas.FindComponentRaw<Component::Mask2D>(&object))
            {
                extent = { std::fabs(mask->size.x), std::fabs(mask->size.y) };
                return Finite(extent);
            }
            return false;
        }
    }

    Int32 Layout2DSystem::GetExecutionOrder() const
    {
        return ExecutionOrder;
    }

    void Layout2DSystem::SetSpriteLibrary(SpriteLibrary* library)
    {
        m_spriteLibrary = library;
    }

    void Layout2DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        (void)deltaTime;
        const Text2DSystem* texts = canvas.GetSystems().FindSystem<Text2DSystem>();
        canvas.ForEach<Component::Layout2D>([&](Component::Layout2D& layout)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(layout);
            if (owner == nullptr || false == layout.IsActiveComponent())
            {
                return;
            }
            // 유한하지 않은 값이 하나라도 있으면 이번 프레임은 놓지 않는다 - 자식의 자리를 NaN 으로 만들지 않는다.
            const Float fields[] = { layout.cellSize.x, layout.cellSize.y, layout.spacing.x, layout.spacing.y,
                layout.padding.x, layout.padding.y, layout.pivot.x, layout.pivot.y };
            for (const Float field : fields)
            {
                if (false == std::isfinite(field.Get()))
                {
                    return;
                }
            }
            const Vector2 cell{ std::fabs(layout.cellSize.x), std::fabs(layout.cellSize.y) };
            // 자식마다 칸의 크기다. 고정이면 `cellSize`, `fitChildren` 이면 보이는 크기(없으면 `cellSize`)다.
            const auto extentOf = [&](Object::GameObject& child)
            {
                Vector2 extent = cell;
                if (layout.fitChildren && false == VisibleExtent(canvas, m_spriteLibrary, texts, child, extent))
                {
                    extent = cell;
                }
                return extent;
            };
            // 1. 놓을 자식을 센다 - 살아 있고 트랜스폼이 있는 것만, 계층 차례대로. 크기의 합과 최대도 여기서 잰다.
            UInt32 count = 0;
            Float maxWidth = 0.0f;
            Float maxHeight = 0.0f;
            Float sumWidth = 0.0f;
            Float sumHeight = 0.0f;
            for (const SafePtr<Object::GameObject>& child : owner->GetChildren())
            {
                Object::GameObject* object = child.TryGet();
                if (object == nullptr || false == object->IsActiveInHierarchy() || canvas.FindComponentRaw<Component::Transform2D>(object) == nullptr)
                {
                    continue;
                }
                const Vector2 extent = extentOf(*object);
                maxWidth = extent.x > maxWidth ? extent.x : maxWidth;
                maxHeight = extent.y > maxHeight ? extent.y : maxHeight;
                sumWidth += extent.x;
                sumHeight += extent.y;
                ++count;
            }
            const UInt32 columns = static_cast<JBro::UInt32>(Max(Int32(1), layout.columns).Get());
            // 격자의 칸은 하나다 - 맞출 때는 가장 큰 것. 세로·가로의 다른 축도 가장 큰 것이다.
            const Float cellWidth = layout.fitChildren ? maxWidth : cell.x;
            const Float cellHeight = layout.fitChildren ? maxHeight : cell.y;
            // 자식이 없으면 블록은 여백뿐이다.
            UInt32 across = 0;
            UInt32 down = 0;
            Float width = 0.0f;
            Float height = 0.0f;
            switch (layout.mode)
            {
            case Component::LayoutMode::Horizontal:
                across = count;
                down = count > 0 ? UInt32(1) : UInt32(0);
                width = (layout.fitChildren ? sumWidth : static_cast<JBro::Float>(across) * cellWidth)
                    + (across > 0 ? static_cast<JBro::Float>(across - 1) * layout.spacing.x : Float(0.0f));
                height = static_cast<JBro::Float>(down) * cellHeight;
                break;
            case Component::LayoutMode::Grid:
                across = Min(count, columns);
                down = (count + columns - 1) / columns;
                width = static_cast<JBro::Float>(across) * cellWidth + (across > 0 ? static_cast<JBro::Float>(across - 1) * layout.spacing.x : Float(0.0f));
                height = static_cast<JBro::Float>(down) * cellHeight + (down > 0 ? static_cast<JBro::Float>(down - 1) * layout.spacing.y : Float(0.0f));
                break;
            case Component::LayoutMode::Vertical:
            default:
                across = count > 0 ? UInt32(1) : UInt32(0);
                down = count;
                width = static_cast<JBro::Float>(across) * cellWidth;
                height = (layout.fitChildren ? sumHeight : static_cast<JBro::Float>(down) * cellHeight)
                    + (down > 0 ? static_cast<JBro::Float>(down - 1) * layout.spacing.y : Float(0.0f));
                break;
            }
            // 2. 블록의 크기와 원점. 블록의 `pivot` 이 이 오브젝트의 원점에 온다 - (0, 1) 이면 왼쪽 위다.
            width += layout.padding.x * 2.0f;
            height += layout.padding.y * 2.0f;
            layout.contentWidth = width;
            layout.contentHeight = height;
            const Float left = -layout.pivot.x * width;
            const Float top = (1.0f - layout.pivot.y) * height;
            // 3. 자식을 칸의 가운데에 놓는다. 맞출 때 세로·가로는 제 크기만큼 자리를 차지한다.
            UInt32 index = 0;
            Float cursorX = left + layout.padding.x;
            Float cursorY = top - layout.padding.y;
            for (const SafePtr<Object::GameObject>& child : owner->GetChildren())
            {
                Object::GameObject* object = child.TryGet();
                auto* place = object != nullptr && object->IsActiveInHierarchy() ? canvas.FindComponentRaw<Component::Transform2D>(object) : nullptr;
                if (place == nullptr)
                {
                    continue;
                }
                const Vector2 extent = layout.fitChildren ? extentOf(*object) : cell;
                switch (layout.mode)
                {
                case Component::LayoutMode::Horizontal:
                    place->position.x = cursorX + extent.x * 0.5f;
                    place->position.y = top - layout.padding.y - cellHeight * 0.5f;
                    cursorX += extent.x + layout.spacing.x;
                    break;
                case Component::LayoutMode::Grid:
                {
                    const UInt32 column = index % columns;
                    const UInt32 row = index / columns;
                    place->position.x = left + layout.padding.x + static_cast<JBro::Float>(column) * (cellWidth + layout.spacing.x) + cellWidth * 0.5f;
                    place->position.y = top - layout.padding.y - static_cast<JBro::Float>(row) * (cellHeight + layout.spacing.y) - cellHeight * 0.5f;
                    break;
                }
                case Component::LayoutMode::Vertical:
                default:
                    place->position.x = left + layout.padding.x + cellWidth * 0.5f;
                    place->position.y = cursorY - extent.y * 0.5f;
                    cursorY -= extent.y + layout.spacing.y;
                    break;
                }
                ++index;
            }
        });
        // 내용 크기 맞추기(D-355): 같은 오브젝트의 레이아웃 블록(없으면 글자 사각형)에 여백을 둘러 스프라이트·마스크·위젯 사각형에 쓴다.
        canvas.ForEach<Component::ContentSizeFitter2D>([&](Component::ContentSizeFitter2D& fitter)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(fitter);
            if (owner == nullptr || false == fitter.IsActiveComponent() || false == Finite(fitter.padding) || false == Finite(fitter.minSize))
            {
                return;
            }
            Vector2 content;
            Bool have = false;
            if (const auto* layout = canvas.FindComponentRaw<Component::Layout2D>(owner))
            {
                content = { layout->contentWidth, layout->contentHeight };
                have = Finite(content);
            }
            else if (const auto* text = canvas.FindComponentRaw<Component::Text2D>(owner))
            {
                Float minX = 0.0f;
                Float minY = 0.0f;
                Float maxX = 0.0f;
                Float maxY = 0.0f;
                if (texts != nullptr && texts->GetLocalBounds(text->GetInstanceId(), minX, minY, maxX, maxY))
                {
                    content = { maxX - minX, maxY - minY };
                    have = Finite(content);
                }
            }
            if (false == have)
            {
                return;
            }
            Vector2 size{ content.x + std::fabs(fitter.padding.x) * 2.0f, content.y + std::fabs(fitter.padding.y) * 2.0f };
            size.x = size.x < std::fabs(fitter.minSize.x) ? Float(std::fabs(fitter.minSize.x)) : size.x;
            size.y = size.y < std::fabs(fitter.minSize.y) ? Float(std::fabs(fitter.minSize.y)) : size.y;
            fitter.contentWidth = size.x;
            fitter.contentHeight = size.y;
            const auto apply = [&](Vector2& target)
            {
                if (fitter.fitWidth)
                {
                    target.x = size.x;
                }
                if (fitter.fitHeight)
                {
                    target.y = size.y;
                }
            };
            if (auto* sprite = canvas.FindComponentRaw<Component::SpriteRenderer2D>(owner))
            {
                sprite->sizeMode = Component::SpriteSizeMode::Custom;
                apply(sprite->size);
            }
            if (auto* mask = canvas.FindComponentRaw<Component::Mask2D>(owner))
            {
                apply(mask->size);
            }
            Vector2* rect = nullptr;
            if (WidgetRect(canvas, *owner, rect))
            {
                apply(*rect);
            }
        });
    }
}
