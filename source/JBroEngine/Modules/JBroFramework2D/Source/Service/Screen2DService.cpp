#include <JBro/Framework2D/Service/Screen2DService.h>

#include <JBro/Framework2D/Internal/SystemContext.h>
#include <JBro/Types/Bool.h>

namespace JBro::Service
{
    Bool Screen2DService::ScreenToLayer(Vector2 pixel, GameObjectHandle object, Vector2& point) const
    {
        const System::IScreen2DSystem* system = GetFramework2DSystems().Screen2D;
        return system != nullptr && system->ScreenToLayer(pixel, object, point);
    }

    Bool Screen2DService::LayerToScreen(Vector2 point, GameObjectHandle object, Vector2& pixel) const
    {
        const System::IScreen2DSystem* system = GetFramework2DSystems().Screen2D;
        return system != nullptr && system->LayerToScreen(point, object, pixel);
    }

    Bool Screen2DService::IsPointerOverButton() const
    {
        const System::IScreen2DSystem* system = GetFramework2DSystems().Screen2D;
        return system != nullptr && system->IsPointerOverButton();
    }
}
