#include <JBro/Framework2D/Service/Text2DService.h>

#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2D/Internal/SystemContext.h>

namespace JBro::Service
{
    System::ITextSystem* Text2DService::GetTextSystem()
    {
        return GetFramework2DSystems().Text2D;
    }

    TextId* Text2DService::FindTextField(const Handle::Text2D& text, Bool key)
    {
        Component::Text2D* component = Internal::ComponentHandleAccess::ResolveAs<Component::Text2D>(text);
        if (component == nullptr)
        {
            return nullptr;
        }
        return key ? &component->textKey : &component->text;
    }
}
