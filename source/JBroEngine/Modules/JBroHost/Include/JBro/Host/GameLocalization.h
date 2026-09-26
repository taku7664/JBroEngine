#pragma once

#include <JBro/Asset/Asset.h>
#include <JBro/LocalizationTypes/Internal/SystemContext.h>
#include <JBro/LocalizationTypes/ServiceContext.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <cstdint>
#include <string_view>

namespace JBro
{
    // 게임 문자열 표의 호스트 구현이다(D-226). 엔진이 소유하고 두 차원이 같은 것을 쓴다.
    //
    // 프로젝트의 `.jstrings` 에셋을 **로케일과 무관하게 모두** 든다. 표는 작고, 그래야 로케일을 바꿀 때 에셋을 싣지 않는다 -
    // 스크립트가 프레임 안에서 `SetLocale` 을 불러도 로드가 프레임 경로에 들지 않는다(asset-plan §2.6). 찾을 때 지금 로케일의 표를
    // 먼저, 폴백 로케일의 표를 다음에 본다. 같은 로케일의 표가 여럿이면 에셋 레지스트리의 차례로 먼저 찾는 것이 이긴다.
    //
    // `Refresh` 는 프레임 밖에서 부른다: 레지스트리가 바뀌었으면 표를 다시 모으고, 들고 있는 표가 재로드됐으면 판번호를 올린다.
    // 메인 스레드 전용이다.
    class GameLocalization final : public System::ILocalization
    {
    public:
        GameLocalization();
        GameLocalization(const GameLocalization&) = delete;
        GameLocalization& operator=(const GameLocalization&) = delete;
        ~GameLocalization();

        // 프로젝트를 열 때 잇는다. null 을 주면 들고 있던 표를 놓는다. 표는 다음 `Refresh` 에서 모은다.
        void Attach(AssetSystem* assets, const AssetRegistry* registry);
        void SetFallbackLocale(const char* locale);
        const String& GetFallbackLocale() const;
        void Refresh();
        std::uint32_t GetTableCount() const;

        // 이 구현을 가리키는 블록이다. 주소가 바뀌지 않으므로 엔진이 한 번 묶는다.
        const LocalizationSystemContext& GetSystemContext() const;
        const LocalizationServiceContext& GetServiceContext() const;

        std::size_t GetLocale(char* buffer, std::size_t capacity) const noexcept override;
        bool SetLocale(const char* locale) noexcept override;
        bool Find(const char* key, std::size_t keyLength, const char*& text, std::size_t& textLength) const noexcept override;
        std::uint32_t GetRevision() const noexcept override;

        // 호스트 쪽 편의다. `GetLocale` 과 같되 버퍼가 없다.
        const String& GetLocaleName() const;

    private:
        struct HeldTable
        {
            AssetHandle handle;
            std::uint32_t dataGeneration = 0;
        };

        void ReleaseTables();
        void Gather();
        bool FindIn(const String& locale, std::string_view key, const char*& text, std::size_t& textLength) const;

        AssetSystem* m_assets = nullptr;
        const AssetRegistry* m_registry = nullptr;
        std::uint64_t m_registryRevision = 0;
        bool m_gathered = false;
        Array<HeldTable> m_tables;
        String m_locale;
        String m_fallback;
        std::uint32_t m_revision = 1;
        LocalizationSystemContext m_systemContext;
        LocalizationServiceContext m_serviceContext;
    };
}
