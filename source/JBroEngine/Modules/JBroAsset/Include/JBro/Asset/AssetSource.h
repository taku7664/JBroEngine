#pragma once

#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    class IPlatform;
    class IFileStream;

    // 에셋 하나가 가진 바이트 덩어리다(D-232). 번호는 패키지의 블롭 종류(`Package::BlobKind`)와 같다.
    enum class AssetBlob : std::uint8_t
    {
        Meta = 1,          // `.jmeta` 원문
        Source = 2,        // 원본 파일
        CookedTexture = 3, // 빌드가 디코드해 둔 RGBA8 - 있으면 디코드를 건너뛴다
        FontAtlas = 4,     // 빌드가 미리 떠 둔 글리프 아틀라스
    };

    // `CookedTexture` 블롭의 모양이다: 16 바이트 머리(`JTEX`·폭·높이·0) 뒤에 `폭 * 높이 * 4` 바이트의 RGBA8(왼쪽 위 원점).
    inline constexpr std::size_t CookedTextureHeaderSize = 16;

    struct CookedTextureInfo
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    void WriteCookedTexture(std::uint32_t width, std::uint32_t height, const std::byte* rgba, Array<std::byte>& out);
    // 머리와 크기가 맞으면 참이다. 픽셀은 `bytes` 의 `CookedTextureHeaderSize` 뒤에 있다.
    bool ReadCookedTexture(const Array<std::byte>& bytes, CookedTextureInfo& info);

    // 에셋 시스템이 바이트를 받는 곳이다(D-232). 에셋 폴더의 파일(`LooseAssetSource`)이든 패키지(`Package::PackageAssetSource`)든 같은 모양이라
    // 에셋 시스템은 어느 쪽인지 모른다. `Read`·`Has`·`MakeStreamPath` 는 메인 스레드이고, `OpenStream` 은 어느 스레드에서 불러도 된다
    // (디스크 스트리밍 오디오의 스트리머가 부른다).
    class IAssetSource
    {
    public:
        virtual ~IAssetSource() = default;

        virtual bool Read(const AssetRecord& record, AssetBlob blob, Array<std::byte>& out) const = 0;
        virtual bool Has(const AssetRecord& record, AssetBlob blob) const = 0;
        // 원본을 흘려 읽을 때 쓰는 이름이다. 느슨한 파일은 절대경로, 패키지는 `jpak:<아이디>` 다. 믹서가 이 글자를 들고 있다가 `OpenStream` 에 준다.
        virtual String MakeStreamPath(const AssetRecord& record) const = 0;
        virtual OwnerPtr<IFileStream> OpenStream(const char* streamPath) const = 0;
    };

    // 에셋 폴더의 파일이다. 에디터와 원본 프로젝트로 여는 게임이 쓴다. 파일은 플랫폼이 연다(D-112).
    class LooseAssetSource final : public IAssetSource
    {
    public:
        void Bind(IPlatform* platform, const char* assetRoot);

        bool Read(const AssetRecord& record, AssetBlob blob, Array<std::byte>& out) const override;
        bool Has(const AssetRecord& record, AssetBlob blob) const override;
        String MakeStreamPath(const AssetRecord& record) const override;
        OwnerPtr<IFileStream> OpenStream(const char* streamPath) const override;

        String SourcePathOf(const AssetRecord& record) const;

    private:
        IPlatform* m_platform = nullptr;
        String m_assetRoot;
    };
}
