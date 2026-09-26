#include <JBro/Framework3D/Service/Text3DService.h>

#include <JBro/Framework3D/Internal/SystemContext.h>

namespace JBro::Service
{
    System::ITextSystem* Text3DService::GetTextSystem()
    {
        return GetFramework3DSystems().Text3D;
    }
}
