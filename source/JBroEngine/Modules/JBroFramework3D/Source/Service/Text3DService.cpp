#include <JBro/Framework3D/Service/Text3DService.h>

#include <JBro/Framework3D/Component/Text3D.h>
#include <JBro/Framework3D/Internal/SystemContext.h>

namespace JBro::Service
{
    System::ITextSystem* Text3DService::GetTextSystem()
    {
        return GetFramework3DSystems().Text3D;
    }

    TextId* Text3DService::FindTextField(const Handle::Text3D& text, Bool key)
    {
        Component::Text3D* component = Internal::ComponentHandleAccess::ResolveAs<Component::Text3D>(text);
        if (component == nullptr)
        {
            return nullptr;
        }
        return key ? &component->textKey : &component->text;
    }
}
