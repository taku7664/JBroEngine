#include <JBro/Framework2D/Service/Text2DService.h>

#include <JBro/Framework2D/Internal/SystemContext.h>

namespace JBro::Service
{
    System::ITextSystem* Text2DService::GetTextSystem()
    {
        return GetFramework2DSystems().Text2D;
    }
}
