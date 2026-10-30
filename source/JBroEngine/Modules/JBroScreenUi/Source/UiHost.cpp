#include <JBro/ScreenUi/UiHost.h>

namespace JBro::System
{
    const char* GetUiHookName(UiHook hook)
    {
        switch (hook)
        {
        case UiHook::PointerEnter:
            return "OnPointerEnter";
        case UiHook::PointerExit:
            return "OnPointerExit";
        case UiHook::PointerDown:
            return "OnPointerDown";
        case UiHook::PointerUp:
            return "OnPointerUp";
        case UiHook::ValueChanged:
            return "OnValueChanged";
        case UiHook::Submit:
            return "OnSubmit";
        case UiHook::DragBegin:
            return "OnDragBegin";
        case UiHook::DragEnd:
            return "OnDragEnd";
        case UiHook::Drop:
            return "OnDrop";
        case UiHook::ListItem:
            return "OnListItem";
        case UiHook::ListMeasure:
            return "OnListMeasure";
        case UiHook::Click:
        default:
            return "OnClick";
        }
    }
}
