#include <JBro/Editor/Widget/ComponentDrag.h>

namespace JBro::Widget
{
    void SetComponentDragPayload(const ComponentDragPayload& value)
    {
        ComponentDragPayload original;
        if (ReadDropValue(PeekDrag(DragKind::InspectorComponent), original))
        {
            SetDragValue(DragKind::InspectorComponent, original);
        }
        else
        {
            SetDragValue(DragKind::InspectorComponent, value);
        }
    }
}
