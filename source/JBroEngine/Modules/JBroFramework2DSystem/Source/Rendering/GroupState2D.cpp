#include <JBro/Framework2DSystem/Rendering/GroupState2D.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/Component/CanvasGroup2D.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>

namespace JBro
{
    Float ComputeGroupAlpha2D(Canvas& canvas, Object::GameObject* object)
    {
        Float alpha = 1.0f;
        for (Object::GameObject* at = object; at != nullptr; at = at->GetParent())
        {
            const auto* group = canvas.FindComponentRaw<Component::CanvasGroup2D>(at);
            if (group != nullptr && group->IsActiveComponent() && std::isfinite(group->alpha.Get()))
            {
                alpha = alpha * Float::Clamp(group->alpha, 0.0f, 1.0f);
            }
        }
        return alpha;
    }

    Bool IsGroupBlockingRaycasts2D(Canvas& canvas, Object::GameObject* object)
    {
        for (Object::GameObject* at = object; at != nullptr; at = at->GetParent())
        {
            const auto* group = canvas.FindComponentRaw<Component::CanvasGroup2D>(at);
            if (group != nullptr && group->IsActiveComponent() && false == group->blocksRaycasts)
            {
                return false;
            }
        }
        return true;
    }

    Bool IsGroupInteractable2D(Canvas& canvas, Object::GameObject* object)
    {
        for (Object::GameObject* at = object; at != nullptr; at = at->GetParent())
        {
            const auto* group = canvas.FindComponentRaw<Component::CanvasGroup2D>(at);
            if (group != nullptr && group->IsActiveComponent() && false == group->interactable)
            {
                return false;
            }
        }
        return true;
    }
}
