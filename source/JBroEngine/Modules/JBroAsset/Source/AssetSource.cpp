#include <JBro/Asset/AssetSource.h>

#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Platform/Platform.h>

#include <cstring>

namespace JBro
{
    namespace
    {
        constexpr char CookedTextureMagic[4] = { 'J', 'T', 'E', 'X' };
    }

    void WriteCookedTexture(std::uint32_t width, std::uint32_t height, const std::byte* rgba, Array<std::byte>& out)
    {
        const std::size_t pixels = static_cast<std::size_t>(width) * height * 4;
        out.Resize(CookedTextureHeaderSize + pixels);
        std::memset(out.Data(), 0, CookedTextureHeaderSize);
        std::memcpy(out.Data(), CookedTextureMagic, sizeof(CookedTextureMagic));
        std::memcpy(out.Data() + 4, &width, sizeof(width));
        std::memcpy(out.Data() + 8, &height, sizeof(height));
        if (pixels > 0)
        {
            std::memcpy(out.Data() + CookedTextureHeaderSize, rgba, pixels);
        }
    }

    bool ReadCookedTexture(const Array<std::byte>& bytes, CookedTextureInfo& info)
    {
        if (bytes.Size() < CookedTextureHeaderSize || std::memcmp(bytes.Data(), CookedTextureMagic, sizeof(CookedTextureMagic)) != 0)
        {
            return false;
        }
        CookedTextureInfo read;
        std::memcpy(&read.width, bytes.Data() + 4, sizeof(read.width));
        std::memcpy(&read.height, bytes.Data() + 8, sizeof(read.height));
        if (read.width == 0 || read.height == 0 || read.width > 16384 || read.height > 16384
            || bytes.Size() != CookedTextureHeaderSize + static_cast<std::size_t>(read.width) * read.height * 4)
        {
            return false;
        }
        info = read;
        return true;
    }

    void LooseAssetSource::Bind(IPlatform* platform, const char* assetRoot)
    {
        m_platform = platform;
        m_assetRoot = assetRoot != nullptr ? assetRoot : "";
    }

    String LooseAssetSource::SourcePathOf(const AssetRecord& record) const
    {
        String path = m_assetRoot;
        if (false == path.empty() && path.back() != '/' && path.back() != '\\')
        {
            path.push_back('/');
        }
        path.append(record.relativePath);
        return path;
    }

    bool LooseAssetSource::Read(const AssetRecord& record, AssetBlob blob, Array<std::byte>& out) const
    {
        out.Clear();
        if (m_platform == nullptr)
        {
            return false;
        }
        // 느슨한 파일에는 원본과 메타뿐이다. 쿡된 것은 빌드만 만든다.
        if (blob == AssetBlob::Source)
        {
            return m_platform->ReadWholeFile(SourcePathOf(record).c_str(), out);
        }
        if (blob == AssetBlob::Meta)
        {
            return m_platform->ReadWholeFile(AssetTypeRules::MakeMetaPath(SourcePathOf(record)).c_str(), out);
        }
        return false;
    }

    bool LooseAssetSource::Has(const AssetRecord& record, AssetBlob blob) const
    {
        (void)record;
        return blob == AssetBlob::Source || blob == AssetBlob::Meta;
    }

    String LooseAssetSource::MakeStreamPath(const AssetRecord& record) const
    {
        return SourcePathOf(record);
    }

    OwnerPtr<IFileStream> LooseAssetSource::OpenStream(const char* streamPath) const
    {
        if (m_platform == nullptr || streamPath == nullptr)
        {
            return {};
        }
        return m_platform->OpenFileStream(streamPath);
    }
}
