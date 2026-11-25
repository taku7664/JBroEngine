#include <JBro/Asset/AssetSource.h>

#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Platform/Platform.h>

#include <cstring>
#include <type_traits>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        constexpr char CookedTextureMagic[4] = { 'J', 'T', 'E', 'X' };
        constexpr char CookedMeshMagic[4] = { 'J', 'M', 'S', 'H' };
        constexpr char CookedSkeletonMagic[4] = { 'J', 'S', 'K', 'L' };
        constexpr char CookedAnimationMagic[4] = { 'J', 'A', 'N', 'M' };
        constexpr std::uint32_t CookedMeshUvs = 1u;
        constexpr std::uint32_t CookedMeshTangents = 2u;
        constexpr std::uint32_t CookedMeshSkin = 4u;

        // 뼈대·클립 블롭을 앞에서부터 쓰고 읽는다. 값은 이 기계의 바이트 순서 그대로다(쿡된 메시와 같다 - 패키지는 같은 엔진이 읽는다).
        class ByteWriter
        {
        public:
            explicit ByteWriter(Array<std::byte>& out)
                : m_out(out)
            {
                m_out.Clear();
            }

            void Raw(const void* data, std::size_t size)
            {
                const std::size_t at = m_out.Size();
                m_out.Resize(at + size);
                if (size > 0)
                {
                    std::memcpy(m_out.Data() + at, data, size);
                }
            }

            template <typename T>
            void Value(const T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>, "only plain values are written as bytes");
                Raw(&value, sizeof(T));
            }

            void Text(const String& text)
            {
                Value(static_cast<std::uint32_t>(text.size()));
                Raw(text.data(), text.size());
            }

            void Floats(const Array<Float>& values)
            {
                Raw(values.Data(), values.Size() * sizeof(Float));
            }

        private:
            Array<std::byte>& m_out;
        };

        class ByteReader
        {
        public:
            explicit ByteReader(const Array<std::byte>& bytes)
                : m_bytes(bytes)
            {
            }

            Bool Raw(void* data, std::size_t size)
            {
                if (m_failed || m_bytes.Size() - m_at < size)
                {
                    m_failed = true;
                    return false;
                }
                if (size > 0)
                {
                    std::memcpy(data, m_bytes.Data() + m_at, size);
                }
                m_at += size;
                return true;
            }

            template <typename T>
            Bool Value(T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>, "only plain values are read as bytes");
                return Raw(&value, sizeof(T));
            }

            // 길이가 남은 바이트보다 길면 거절한다 - 망가진 길이로 큰 할당을 하지 않는다.
            Bool Text(String& text)
            {
                std::uint32_t size = 0;
                if (false == Value(size) || m_bytes.Size() - m_at < size)
                {
                    m_failed = true;
                    return false;
                }
                text.assign(reinterpret_cast<const char*>(m_bytes.Data() + m_at), size);
                m_at += size;
                return true;
            }

            Bool Floats(Array<Float>& values, std::size_t count)
            {
                if (m_failed || (m_bytes.Size() - m_at) / sizeof(Float) < count)
                {
                    m_failed = true;
                    return false;
                }
                values.Resize(count);
                return Raw(values.Data(), count * sizeof(Float));
            }

            // 남은 바이트로 `count` 개의 `itemSize` 가 들어갈 수 있는가다 - 망가진 개수로 큰 배열을 잡지 않는다.
            Bool CanHold(std::size_t count, std::size_t itemSize) const
            {
                return false == m_failed && itemSize > 0 && (m_bytes.Size() - m_at) / itemSize >= count;
            }

            Bool AtEnd() const
            {
                return false == m_failed && m_at == m_bytes.Size();
            }

        private:
            const Array<std::byte>& m_bytes;
            std::size_t m_at = 0;
            Bool m_failed = false;
        };
    }

    void WriteCookedMesh(const CookedMesh& mesh, Array<std::byte>& out)
    {
        const std::uint32_t vertexCount = static_cast<std::uint32_t>(mesh.vertices.Size());
        const std::uint32_t indexCount = static_cast<std::uint32_t>(mesh.indices.Size());
        // 스킨은 정점마다 하나일 때만 싣는다 - 수가 다르면 스킨 없는 메시로 굽는다(읽는 쪽이 어긋난 스킨을 받지 않는다).
        const Bool skinned = false == mesh.skin.IsEmpty() && mesh.skin.Size() == mesh.vertices.Size();
        const std::uint32_t flags = (mesh.hasUvs ? CookedMeshUvs : 0u) | (mesh.hasTangents ? CookedMeshTangents : 0u) | (skinned ? CookedMeshSkin : 0u);
        const std::size_t vertexBytes = static_cast<std::size_t>(vertexCount) * sizeof(ModelVertex);
        const std::size_t indexBytes = static_cast<std::size_t>(indexCount) * sizeof(std::uint32_t);
        const std::size_t skinBytes = skinned ? sizeof(AssetId) + static_cast<std::size_t>(vertexCount) * sizeof(ModelSkinWeights) : 0;
        out.Resize(CookedMeshHeaderSize + vertexBytes + indexBytes + skinBytes);
        std::memcpy(out.Data(), CookedMeshMagic, sizeof(CookedMeshMagic));
        std::memcpy(out.Data() + 4, &vertexCount, sizeof(vertexCount));
        std::memcpy(out.Data() + 8, &indexCount, sizeof(indexCount));
        std::memcpy(out.Data() + 12, &flags, sizeof(flags));
        std::memcpy(out.Data() + 16, &mesh.material, sizeof(mesh.material));
        if (vertexBytes > 0)
        {
            std::memcpy(out.Data() + CookedMeshHeaderSize, mesh.vertices.Data(), vertexBytes);
        }
        if (indexBytes > 0)
        {
            std::memcpy(out.Data() + CookedMeshHeaderSize + vertexBytes, mesh.indices.Data(), indexBytes);
        }
        if (skinned)
        {
            std::byte* skin = out.Data() + CookedMeshHeaderSize + vertexBytes + indexBytes;
            std::memcpy(skin, &mesh.skeleton, sizeof(mesh.skeleton));
            std::memcpy(skin + sizeof(AssetId), mesh.skin.Data(), static_cast<std::size_t>(vertexCount) * sizeof(ModelSkinWeights));
        }
    }

    Bool ReadCookedMesh(const Array<std::byte>& bytes, CookedMesh& mesh)
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
        const Bool skinned = (flags & CookedMeshSkin) != 0;
        const std::size_t skinBytes = skinned ? sizeof(AssetId) + static_cast<std::size_t>(vertexCount) * sizeof(ModelSkinWeights) : 0;
        if (vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0 || (flags & ~(CookedMeshUvs | CookedMeshTangents | CookedMeshSkin)) != 0
            || bytes.Size() != CookedMeshHeaderSize + vertexBytes + indexBytes + skinBytes)
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
        Array<ModelSkinWeights> readSkin;
        AssetId readSkeleton;
        if (skinned)
        {
            const std::byte* skin = bytes.Data() + CookedMeshHeaderSize + vertexBytes + indexBytes;
            std::memcpy(&readSkeleton, skin, sizeof(readSkeleton));
            readSkin.Resize(vertexCount);
            std::memcpy(readSkin.Data(), skin + sizeof(AssetId), static_cast<std::size_t>(vertexCount) * sizeof(ModelSkinWeights));
        }
        mesh.vertices = std::move(readVertices);
        mesh.indices = std::move(readIndices);
        mesh.hasUvs = (flags & CookedMeshUvs) != 0;
        mesh.hasTangents = (flags & CookedMeshTangents) != 0;
        std::memcpy(&mesh.material, bytes.Data() + 16, sizeof(mesh.material));
        mesh.skin = std::move(readSkin);
        mesh.skeleton = readSkeleton;
        return true;
    }

    void WriteCookedSkeleton(const ModelSkeleton& skeleton, Array<std::byte>& out)
    {
        ByteWriter writer(out);
        writer.Raw(CookedSkeletonMagic, sizeof(CookedSkeletonMagic));
        writer.Value(static_cast<std::uint32_t>(skeleton.bones.Size()));
        writer.Text(skeleton.name);
        writer.Value(skeleton.rootParent);
        for (const ModelBone& bone : skeleton.bones)
        {
            writer.Text(bone.name);
            writer.Value(bone.parent);
            writer.Value(bone.position);
            writer.Value(bone.rotation);
            writer.Value(bone.scale);
            writer.Value(bone.inverseBind);
        }
    }

    Bool ReadCookedSkeleton(const Array<std::byte>& bytes, ModelSkeleton& skeleton)
    {
        ByteReader reader(bytes);
        char magic[4] = {};
        std::uint32_t count = 0;
        ModelSkeleton read;
        if (false == reader.Raw(magic, sizeof(magic)) || std::memcmp(magic, CookedSkeletonMagic, sizeof(magic)) != 0
            || false == reader.Value(count) || false == reader.Text(read.name) || false == reader.Value(read.rootParent))
        {
            return false;
        }
        // 뼈 하나는 적어도 이름 길이와 고정 칸이다 - 개수가 망가졌으면 잡기 전에 거절한다.
        constexpr std::size_t FixedBoneBytes = sizeof(std::uint32_t) + sizeof(Int32) + sizeof(Vector3) * 2 + sizeof(Quaternion) + sizeof(Matrix4x4);
        if (false == reader.CanHold(count, FixedBoneBytes))
        {
            return false;
        }
        read.bones.Resize(count);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            ModelBone& bone = read.bones[index];
            if (false == reader.Text(bone.name) || false == reader.Value(bone.parent) || false == reader.Value(bone.position)
                || false == reader.Value(bone.rotation) || false == reader.Value(bone.scale) || false == reader.Value(bone.inverseBind))
            {
                return false;
            }
            // 부모는 늘 앞의 뼈다(디코더가 그렇게 늘어놓는다) - 자세 셈이 앞에서부터 한 번에 돈다.
            if (bone.parent < -1 || bone.parent >= static_cast<Int32>(index))
            {
                return false;
            }
        }
        if (false == reader.AtEnd())
        {
            return false;
        }
        skeleton = std::move(read);
        return true;
    }

    void WriteCookedAnimation(const ModelAnimation& animation, Array<std::byte>& out)
    {
        ByteWriter writer(out);
        writer.Raw(CookedAnimationMagic, sizeof(CookedAnimationMagic));
        writer.Text(animation.name);
        writer.Value(animation.duration);
        writer.Value(static_cast<std::uint32_t>(animation.tracks.Size()));
        for (const ModelAnimationTrack& track : animation.tracks)
        {
            writer.Text(track.bone);
            writer.Value(static_cast<std::uint8_t>(track.path));
            writer.Value(static_cast<std::uint8_t>(track.interpolation));
            writer.Value(static_cast<std::uint32_t>(track.times.Size()));
            writer.Floats(track.times);
            writer.Floats(track.values);
        }
    }

    Bool ReadCookedAnimation(const Array<std::byte>& bytes, ModelAnimation& animation)
    {
        ByteReader reader(bytes);
        char magic[4] = {};
        std::uint32_t trackCount = 0;
        ModelAnimation read;
        if (false == reader.Raw(magic, sizeof(magic)) || std::memcmp(magic, CookedAnimationMagic, sizeof(magic)) != 0
            || false == reader.Text(read.name) || false == reader.Value(read.duration) || false == reader.Value(trackCount))
        {
            return false;
        }
        constexpr std::size_t FixedTrackBytes = sizeof(std::uint32_t) * 2 + sizeof(std::uint8_t) * 2;
        if (false == reader.CanHold(trackCount, FixedTrackBytes))
        {
            return false;
        }
        read.tracks.Resize(trackCount);
        for (std::uint32_t index = 0; index < trackCount; ++index)
        {
            ModelAnimationTrack& track = read.tracks[index];
            std::uint8_t path = 0;
            std::uint8_t interpolation = 0;
            std::uint32_t keys = 0;
            if (false == reader.Text(track.bone) || false == reader.Value(path) || false == reader.Value(interpolation) || false == reader.Value(keys)
                || path > static_cast<std::uint8_t>(ModelTrackPath::Scale) || interpolation > static_cast<std::uint8_t>(ModelInterpolation::Step))
            {
                return false;
            }
            track.path = static_cast<ModelTrackPath>(path);
            track.interpolation = static_cast<ModelInterpolation>(interpolation);
            const std::size_t components = track.path == ModelTrackPath::Rotation ? 4 : 3;
            if (false == reader.Floats(track.times, keys) || false == reader.CanHold(keys, components * sizeof(Float))
                || false == reader.Floats(track.values, static_cast<std::size_t>(keys) * components))
            {
                return false;
            }
        }
        if (false == reader.AtEnd())
        {
            return false;
        }
        animation = std::move(read);
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
