#include <JBro/Framework2D/Service/Text2DService.h>

#include <JBro/Framework2D/Internal/SystemContext.h>
#include <JBro/Framework2D/System/IText2DSystem.h>

#include <cstring>

namespace JBro::Service
{
    bool Text2DService::SetText(Ref<Component::Text2D> text, const char* utf8, std::uint32_t length) const
    {
        System::IText2DSystem* system = GetFramework2DSystems().Text2D;
        Component::Text2D* component = text.Get();
        if (system == nullptr || component == nullptr || (utf8 == nullptr && length != 0))
        {
            return false;
        }
        system->SetText(*component, utf8, length);
        return true;
    }

    bool Text2DService::SetText(Ref<Component::Text2D> text, const char* utf8) const
    {
        const std::size_t length = utf8 != nullptr ? std::strlen(utf8) : 0;
        if (length > 0xFFFFFFFFu)
        {
            return false;
        }
        return SetText(text, utf8, static_cast<std::uint32_t>(length));
    }

    std::uint32_t Text2DService::GetTextLength(Ref<Component::Text2D> text) const
    {
        System::IText2DSystem* system = GetFramework2DSystems().Text2D;
        const Component::Text2D* component = text.Get();
        if (system == nullptr || component == nullptr)
        {
            return 0;
        }
        return system->GetTextLength(*component);
    }

    std::uint32_t Text2DService::CopyText(Ref<Component::Text2D> text, char* buffer, std::uint32_t capacity) const
    {
        System::IText2DSystem* system = GetFramework2DSystems().Text2D;
        const Component::Text2D* component = text.Get();
        if (buffer == nullptr || capacity == 0)
        {
            return 0;
        }
        if (system == nullptr || component == nullptr)
        {
            buffer[0] = '\0';
            return 0;
        }
        return system->CopyText(*component, buffer, capacity);
    }
}
