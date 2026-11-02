#include <JBro/Asset/AssetSource.h>

#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Platform/Platform.h>

#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        constexpr char CookedTextureMagic[4] = { 'J', 'T', 'E', 'X' };
    }

    void WriteCookedTexture(UInt32 width, UInt32 height, const std::byte* rgba, Array<std::byte>& out)
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

    Bool ReadCookedTexture(const Array<std::byte>& bytes, CookedTextureInfo& info)
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

    Bool LooseAssetSource::Read(const AssetRecord& record, AssetBlob blob, Array<std::byte>& out) const
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

    Bool LooseAssetSource::ReadSibling(const AssetRecord& record, const char* relative, Array<std::byte>& out) const
    {
        out.Clear();
        if (m_platform == nullptr || relative == nullptr || relative[0] == '\0')
        {
            return false;
        }
        // 에셋 폴더 밖을 읽지 않는다: 절대 경로·드라이브·`..` 는 거절한다. URI 의 %XX 는 푼다.
        String name;
        for (const char* at = relative; *at != '\0'; ++at)
        {
            if (*at == '%' && at[1] != '\0' && at[2] != '\0')
            {
                const auto digit = [](char c) -> int {
                    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                };
                const int high = digit(at[1]);
                const int low = digit(at[2]);
                if (high < 0 || low < 0)
                {
                    return false;
                }
                name.push_back(static_cast<char>(high * 16 + low));
                at += 2;
                continue;
            }
            name.push_back(*at);
        }
        if (name[0] == '/' || name[0] == '\\' || name.find(':') != String::npos || name.find("..") != String::npos)
        {
            return false;
        }
        String path = SourcePathOf(record);
        const std::size_t slash = path.find_last_of("/\\");
        path.resize(slash == String::npos ? 0 : slash + 1);
        path.append(name);
        return m_platform->ReadWholeFile(path.c_str(), out);
    }

    Bool LooseAssetSource::Has(const AssetRecord& record, AssetBlob blob) const
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
