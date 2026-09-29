#include <JBro/Framework2D/Service/Screen2DService.h>

#include <JBro/Framework2D/Internal/SystemContext.h>

namespace JBro::Service
{
    bool Screen2DService::ScreenToLayer(Vector2 pixel, Handle::GameObject object, Vector2& point) const
    {
        const System::IScreen2DSystem* system = GetFramework2DSystems().Screen2D;
        return system != nullptr && system->ScreenToLayer(pixel, object, point);
    }

    bool Screen2DService::LayerToScreen(Vector2 point, Handle::GameObject object, Vector2& pixel) const
    {
        const System::IScreen2DSystem* system = GetFramework2DSystems().Screen2D;
        return system != nullptr && system->LayerToScreen(point, object, pixel);
    }

    bool Screen2DService::IsPointerOverButton() const
    {
        const System::IScreen2DSystem* system = GetFramework2DSystems().Screen2D;
        return system != nullptr && system->IsPointerOverButton();
    }
}
