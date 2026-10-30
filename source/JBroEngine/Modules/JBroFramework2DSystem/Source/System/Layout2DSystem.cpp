#include <JBro/Framework2DSystem/System/Layout2DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Framework2D/Component/Button2D.h>
#include <JBro/Framework2D/Component/ContentSizeFitter2D.h>
#include <JBro/Framework2D/Component/Dropdown2D.h>
#include <JBro/Framework2D/Component/InputField2D.h>
#include <JBro/Framework2D/Component/Layout2D.h>
#include <JBro/Framework2D/Component/ListView2D.h>
#include <JBro/Framework2D/Component/Mask2D.h>
#include <JBro/Framework2D/Component/ScrollView2D.h>
#include <JBro/Framework2D/Component/Slider2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2D/Component/Toggle2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2DSystem/Rendering/SpriteLibrary.h>
#include <JBro/Framework2DSystem/System/Text2DSystem.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Runtime/ScriptCallGuard.h>

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
        // 가상화 목록(D-357): 뷰포트에 걸치는 항목만 풀에 묶어 놓는다. 풀 = 스크롤 내용의 자식(계층 차례).
        canvas.ForEach<Component::ListView2D>([&](Component::ListView2D& list)
        {
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(list);
            if (owner == nullptr || false == list.IsActiveComponent())
            {
                return;
            }
            auto* scroll = canvas.FindComponentRaw<Component::ScrollView2D>(owner);
            const auto* mask = canvas.FindComponentRaw<Component::Mask2D>(owner);
            Object::GameObject* content = scroll != nullptr ? Internal::GameObjectHandleAccess::Resolve(scroll->content) : nullptr;
            auto* place = content != nullptr && content->GetParent() == owner ? canvas.FindComponentRaw<Component::Transform2D>(content) : nullptr;
            const Float itemAlong = std::fabs(list.vertical ? list.itemSize.y : list.itemSize.x);
            const Float itemAcross = std::fabs(list.vertical ? list.itemSize.x : list.itemSize.y);
            const Float spacing = std::fabs(list.spacing);
            const Int32 count = list.count < 0 ? Int32(0) : list.count;
            if (scroll == nullptr || mask == nullptr || place == nullptr || false == std::isfinite(itemAlong.Get()) || false == std::isfinite(itemAcross.Get())
                || false == std::isfinite(spacing.Get()) || itemAlong + spacing <= 0.0f)
            {
                return;
            }
            const Float pitch = itemAlong + spacing;
            const Float total = count > 0 ? Float(static_cast<JBro::Float>(count.Get()) * pitch - spacing) : Float(0.0f);
            scroll->contentSize = list.vertical ? Vector2{ itemAcross, total } : Vector2{ total, itemAcross };
            // 뷰포트의 양 끝을 내용 좌표로 옮겨 걸치는 번호 범위를 센다. 세로는 위에서 아래로, 가로는 왼쪽에서 오른쪽으로 번호가 는다.
            const Float position = list.vertical ? place->position.y : place->position.x;
            const Float center = list.vertical ? mask->offset.y : mask->offset.x;
            const Float half = std::fabs(list.vertical ? mask->size.y : mask->size.x) * 0.5f;
            Int32 firstIndex = -1;
            Int32 lastIndex = -2;
            if (count > 0 && std::isfinite(position.Get()) && std::isfinite(center.Get()) && std::isfinite(half.Get()))
            {
                // 항목 i 의 내용 좌표 구간: 세로는 [total/2 - i·pitch - item, total/2 - i·pitch], 가로는 [-total/2 + i·pitch, -total/2 + i·pitch + item].
                const Float nearEdge = list.vertical ? Float(center + half - position) : Float(center - half - position);
                const Float farEdge = list.vertical ? Float(center - half - position) : Float(center + half - position);
                const Float firstRaw = list.vertical ? Float((total * 0.5f - itemAlong - nearEdge) / pitch) : Float((nearEdge - total * 0.5f - itemAlong) / pitch);
                const Float lastRaw = list.vertical ? Float((total * 0.5f - farEdge) / pitch) : Float((farEdge + total * 0.5f) / pitch);
                const Int32 first = Int32(static_cast<std::int32_t>(std::ceil(firstRaw.Get())));
                const Int32 last = Int32(static_cast<std::int32_t>(std::floor(lastRaw.Get())));
                firstIndex = first < 0 ? Int32(0) : first;
                lastIndex = last >= count ? Int32(count - 1) : last;
            }
            const Int32 visible = lastIndex >= firstIndex ? Int32(lastIndex - firstIndex + 1) : Int32(0);
            list.firstVisible = visible > 0 ? firstIndex : Int32(-1);
            list.visibleCount = visible;
            // 풀: 내용의 자식들. 항목 i 는 i % 풀 크기 번째다 - 걸치는 것이 풀보다 많으면 앞의 것만 보인다. 전에 묶였던 번호는 지난 범위로 되센다.
            const Int32 pool = Int32(static_cast<std::int32_t>(content->GetChildren().Size()));
            const Int32 first = visible > 0 ? firstIndex : Int32(-1);
            Int32 slot = 0;
            for (const SafePtr<Object::GameObject>& child : content->GetChildren())
            {
                Object::GameObject* item = child.TryGet();
                if (item == nullptr)
                {
                    ++slot;
                    continue;
                }
                const Int32 index = Component::ListView2D::SlotIndex(first, visible, pool, slot);
                const Int32 before = Component::ListView2D::SlotIndex(list.shownFirst, list.shownVisible, list.shownPool, slot);
                if (index < 0)
                {
                    if (item->IsActiveSelf())
                    {
                        item->SetActive(false);
                    }
                    ++slot;
                    continue;
                }
                if (auto* itemPlace = canvas.FindComponentRaw<Component::Transform2D>(item))
                {
                    const Float along = static_cast<JBro::Float>(index.Get()) * pitch + itemAlong * 0.5f;
                    itemPlace->position = list.vertical ? Vector2{ 0.0f, total * 0.5f - along } : Vector2{ along - total * 0.5f, 0.0f };
                }
                if (false == item->IsActiveSelf())
                {
                    item->SetActive(true);
                }
                if (before != index)
                {
                    list.boundItem = item->GetScriptHandle();
                    list.boundIndex = index;
                    // 먼저 모은 뒤 부른다. 훅이 스크립트를 붙이거나 떼면 목록이 흔들린다.
                    m_hookTargets.Clear();
                    for (const ScriptSlot& scriptSlot : owner->GetScripts())
                    {
                        GameScriptBase* script = scriptSlot.reference.TryGet();
                        if (script != nullptr && script->IsActiveScript())
                        {
                            m_hookTargets.Add(static_cast<GameScript2D*>(script));
                        }
                    }
                    for (GameScript2D* script : m_hookTargets)
                    {
                        ScriptCallGuard::Call("OnListItem", [script] { script->OnListItem(); });
                    }
                }
                ++slot;
            }
            list.boundItem = {};
            list.boundIndex = -1;
            list.shownFirst = first;
            list.shownVisible = visible;
            list.shownPool = pool;
        });
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
