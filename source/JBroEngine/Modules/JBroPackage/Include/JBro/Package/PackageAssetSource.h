#pragma once

#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetSource.h>
#include <JBro/Package/PackageReader.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro::Package
{
    // 패키지에서 에셋 바이트를 주는 곳이다(D-232). 에셋 시스템은 느슨한 파일과 같은 모양으로 받는다.
    // 흘려 읽는 이름은 `jpak:<32 자리 아이디>` 이고, `OpenStream` 은 그 블롭의 창 스트림을 연다(어느 스레드에서든).
    class PackageAssetSource final : public IAssetSource
    {
    public:
        // 열린 패키지를 가리킨다. 패키지는 이것보다 오래 살아야 한다.
        explicit PackageAssetSource(const PackageReader& package);

        Bool Read(const AssetRecord& record, AssetBlob blob, Array<std::byte>& out) const override;
        Bool Has(const AssetRecord& record, AssetBlob blob) const override;
        String MakeStreamPath(const AssetRecord& record) const override;
        OwnerPtr<IFileStream> OpenStream(const char* streamPath) const override;

    private:
        const PackageReader& m_package;
    };

    // 패키지의 색인으로 레지스트리를 채운다(에셋마다 하나, 경로·타입·주인). 앞의 내용은 비운다. 채운 에셋 수다.
    UInt32 FillRegistry(const PackageReader& package, AssetRegistry& registry);
}
