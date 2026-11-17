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
        constexpr char CookedMeshMagic[4] = { 'J', 'M', 'S', 'H' };
    }

    void WriteCookedMesh(const Array<ModelVertex>& vertices, const Array<UInt32>& indices, Bool hasUvs, AssetId material, Array<std::byte>& out)
    {
        const std::uint32_t vertexCount = static_cast<std::uint32_t>(vertices.Size());
        const std::uint32_t indexCount = static_cast<std::uint32_t>(indices.Size());
        const std::uint32_t flags = hasUvs ? 1u : 0u;
        const std::size_t vertexBytes = static_cast<std::size_t>(vertexCount) * sizeof(ModelVertex);
        const std::size_t indexBytes = static_cast<std::size_t>(indexCount) * sizeof(std::uint32_t);
        out.Resize(CookedMeshHeaderSize + vertexBytes + indexBytes);
        std::memcpy(out.Data(), CookedMeshMagic, sizeof(CookedMeshMagic));
        std::memcpy(out.Data() + 4, &vertexCount, sizeof(vertexCount));
        std::memcpy(out.Data() + 8, &indexCount, sizeof(indexCount));
        std::memcpy(out.Data() + 12, &flags, sizeof(flags));
        std::memcpy(out.Data() + 16, &material, sizeof(material));
        if (vertexBytes > 0)
        {
            std::memcpy(out.Data() + CookedMeshHeaderSize, vertices.Data(), vertexBytes);
        }
        if (indexBytes > 0)
        {
            std::memcpy(out.Data() + CookedMeshHeaderSize + vertexBytes, indices.Data(), indexBytes);
        }
    }

    Bool ReadCookedMesh(const Array<std::byte>& bytes, Array<ModelVertex>& vertices, Array<UInt32>& indices, Bool& hasUvs, AssetId& material)
    {
        if (bytes.Size() < CookedMeshHeaderSize || std::memcmp(bytes.Data(), CookedMeshMagic, sizeof(CookedMeshMagic)) != 0)
        {
            return false;
        }
        std::uint32_t vertexCount = 0;
        std::uint32_t indexCount = 0;
        std::uint32_t flags = 0;
        std::memcpy(&vertexCount, bytes.Data() + 4, sizeof(vertexCount));
        std::memcpy(&indexCount, bytes.Data() + 8, sizeof(indexCount));
        std::memcpy(&flags, bytes.Data() + 12, sizeof(flags));
        const std::size_t vertexBytes = static_cast<std::size_t>(vertexCount) * sizeof(ModelVertex);
        const std::size_t indexBytes = static_cast<std::size_t>(indexCount) * sizeof(std::uint32_t);
        if (vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0 || (flags & ~1u) != 0
            || bytes.Size() != CookedMeshHeaderSize + vertexBytes + indexBytes)
        {
            return false;
        }
        Array<UInt32> readIndices;
        readIndices.Resize(indexCount);
        std::memcpy(readIndices.Data(), bytes.Data() + CookedMeshHeaderSize + vertexBytes, indexBytes);
        for (std::size_t index = 0; index < readIndices.Size(); ++index)
        {
            if (readIndices[index] >= vertexCount)
            {
                return false;
            }
        }
        Array<ModelVertex> readVertices;
        readVertices.Resize(vertexCount);
        std::memcpy(readVertices.Data(), bytes.Data() + CookedMeshHeaderSize, vertexBytes);
        vertices = std::move(readVertices);
        indices = std::move(readIndices);
        hasUvs = (flags & 1u) != 0;
        std::memcpy(&material, bytes.Data() + 16, sizeof(material));
        return true;
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
