#include <JBro/LocalizationTypes/Service/LocalizationService.h>

#include <JBro/LocalizationTypes/Internal/SystemContext.h>

#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    Bool LocalizationService::IsReady() const
    {
        return GetLocalizationSystems().Localization != nullptr;
    }

    Bool LocalizationService::SetLocale(const char* locale) const
    {
        System::ILocalization* localization = GetLocalizationSystems().Localization;
        return localization != nullptr && localization->SetLocale(locale);
    }

    String LocalizationService::GetLocale() const
    {
        const System::ILocalization* localization = GetLocalizationSystems().Localization;
        if (localization == nullptr)
        {
            return {};
        }
        // 로케일 이름은 짧다. 잘렸으면 길이만큼 이 사본이 키워 다시 받는다.
        char small[32] = {};
        const std::size_t length = localization->GetLocale(small, sizeof(small));
        if (length < sizeof(small))
        {
            return String(small, length);
        }
        String out;
        out.resize(length + 1);
        const std::size_t again = localization->GetLocale(out.data(), out.size());
        out.resize(again < out.size() ? again : out.size() - 1);
        return out;
    }

    Bool LocalizationService::TryGetText(const char* key, String& out) const
    {
        out.clear();
        const System::ILocalization* localization = GetLocalizationSystems().Localization;
        if (localization == nullptr || key == nullptr)
        {
            return false;
        }
        const char* text = nullptr;
        std::size_t length = 0;
        if (false == localization->Find(key, std::strlen(key), text, length))
        {
            return false;
        }
        // 호스트 메모리의 글자를 이 사본의 힙으로 옮긴다.
        out.assign(text, length);
        return true;
    }

    String LocalizationService::GetText(const char* key) const
    {
        String out;
        if (TryGetText(key, out))
        {
            return out;
        }
        return key != nullptr ? String(key) : String();
    }

    UInt32 LocalizationService::GetRevision() const
    {
        const System::ILocalization* localization = GetLocalizationSystems().Localization;
        return localization != nullptr ? localization->GetRevision() : UInt32(0);
    }
}
