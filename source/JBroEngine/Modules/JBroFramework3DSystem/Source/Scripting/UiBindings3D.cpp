#include <JBro/Framework3DSystem/Scripting/UiBindings3D.h>

#include <JBro/Framework3D/Scripting/GameScript.h>

namespace JBro::System
{
    void UiHookDispatcher3D::Call(GameScriptBase& script, UiHook hook)
    {
        GameScript3D& script3D = static_cast<GameScript3D&>(script);
        switch (hook)
        {
        case UiHook::PointerEnter:
            script3D.OnPointerEnter();
            break;
        case UiHook::PointerExit:
            script3D.OnPointerExit();
            break;
        case UiHook::PointerDown:
            script3D.OnPointerDown();
            break;
        case UiHook::PointerUp:
            script3D.OnPointerUp();
            break;
        case UiHook::ValueChanged:
            script3D.OnValueChanged();
            break;
        case UiHook::Submit:
            script3D.OnSubmit();
            break;
        case UiHook::DragBegin:
            script3D.OnDragBegin();
            break;
        case UiHook::DragEnd:
            script3D.OnDragEnd();
            break;
        case UiHook::Drop:
            script3D.OnDrop();
            break;
        case UiHook::ListItem:
            script3D.OnListItem();
            break;
        case UiHook::ListMeasure:
            script3D.OnListMeasure();
            break;
        case UiHook::Click:
        default:
            script3D.OnClick();
            break;
        }
    }
}
