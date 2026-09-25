#pragma once

#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Core/Core.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>
#include <JBro/Types/Table.h>

#include <cstddef>

namespace JBro
{
    class IPlatform;

    // 로드된 텍스처다. **CPU 자료만이다** - GPU 텍스처는 프레임워크 시스템의 라이브러리가 든다(asset-plan §2.5).
    // 픽셀은 RGBA8, 왼쪽 위가 원점이다. `pixelGeneration` 은 in-place 재로드마다 오른다 - GPU 쪽이 이것을 보고
    // 다시 올릴지 정한다.
    struct TextureData
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        Array<std::byte> pixels;
        std::uint32_t pixelGeneration = 1;
        // 메타에 적힌 그대로다.
        TextureImportOptions options;
        // 프로젝트 기본을 적용한 값이다(D-117). `Default` 는 여기 오지 않는다 - 그리는 쪽은 이것만 본다.
        TextureFilter filter = TextureFilter::Nearest;
    };

    // 로드된 스프라이트다. 자기 텍스처를 참조 수로 잡고 있다.
    struct SpriteData
    {
        AssetHandle texture;
        SpriteImportOptions options;
        Array<SpriteFrame> frames;
    };

    // 로드된 오디오다(D-197). **CPU 자료만이다** - 재생은 엔진의 믹서가 한다. `Decompressed` 면 `pcm` 이 f32 인터리브
    // 전체이고, `Streaming` 이면 `encoded` 가 파일 바이트 그대로다(믹서가 보이스마다 풀어 재생한다). 형식은 파일 그대로다.
    // `dataGeneration` 은 in-place 재로드마다 오른다.
    struct AudioData
    {
        AudioImportOptions options;
        std::uint32_t sampleRate = 0;
        std::uint32_t channels = 0;
        std::uint64_t frameCount = 0;
        Array<float> pcm;
        Array<std::byte> encoded;
        std::uint32_t dataGeneration = 1;
    };

    // 오디오 자료가 풀리거나 바뀌기 **직전에** 불린다. 믹서가 그 자료를 빌려 재생하고 있을 수 있으므로, 받는 쪽은 여기서
    // 그 클립의 보이스를 멈추고 등록을 내린다. 그 뒤에 자료가 풀린다 - 오디오 스레드가 풀린 메모리를 읽지 않게 하는 순서다.
    using AudioReleaseCallback = void (*)(void* user, AssetHandle handle);

    // 프로젝트 수명 동안 에셋 로드와 캐시를 소유한다(D-50·D-111). 사용자 호출 표면은 값형 Service::AssetService 다.
    //
    // **타입별 풀과 index+generation 핸들이다.** `IAsset` 가상 기반이 없다. 핸들의 `index` 상위 4 비트가 타입이고
    // 나머지가 풀의 자리다. 타입이 다른 핸들로 물으면 `nullptr` 다. 로드는 동기이고 메인 스레드다. **프레임 경로에서
    // 부르지 않는다** - 해석 패스(`BindComponentAssets`)가 캔버스 로드 뒤와 편집 뒤에만 돈다(asset-plan §2.6).
    // 참조 수가 0 이 되어도 곧 내려가지 않는다. `CollectUnused` 가 프로젝트 닫기·캔버스 전환 뒤에 내린다.
    class AssetSystem final : public IModule
    {
    public:
        bool Initialize(const JMemoryContext& memory) override;
        void Shutdown() override;

        // 프로젝트를 열 때 레지스트리·플랫폼·에셋 폴더(UTF-8 절대경로)를 잇는다. 닫을 때 `Unbind` 가 전부 내린다.
        void Bind(IPlatform& platform, const AssetRegistry& registry, const char* assetRoot);
        void Unbind();
        bool IsBound() const;

        // 프로젝트의 `TextureFilter` 다(D-117). 임포트 옵션이 `Default` 인 텍스처가 이것을 받는다. 로드·재로드 때
        // 적용되므로 프로젝트를 열 때(`Bind` 전에) 정한다. `Default` 를 주면 `Nearest` 로 본다.
        void SetDefaultTextureFilter(TextureFilter filter);
        TextureFilter GetDefaultTextureFilter() const;

        // 잇긴 에셋 폴더(UTF-8 절대경로)와 레지스트리의 레코드가 가리키는 메타 경로다. 에디터가 메타를 고쳐 쓸 때 쓴다(D-120).
        const String& GetAssetRoot() const;
        String GetMetaPath(const AssetRecord& record) const;

        // 로드돼 있으면 참조 수만 올리고 같은 핸들을 준다. 레지스트리에 없거나 이 판이 아직 싣지 못하는 타입
        // (Mesh·Material·Shader·Canvas·...)이거나 읽기·디코드가 실패하면 빈 핸들이다.
        AssetHandle Load(AssetId id);
        // 참조 수를 내린다. 0 이 되어도 자료는 `CollectUnused` 까지 산다.
        void Release(AssetHandle handle);
        // 로드돼 있으면 그 핸들, 아니면 빈 핸들. 참조 수를 건드리지 않는다.
        AssetHandle Find(AssetId id) const;
        bool IsLoaded(AssetHandle handle) const;
        std::uint32_t GetReferenceCount(AssetHandle handle) const;

        const TextureData* GetTexture(AssetHandle handle) const;
        const SpriteData* GetSprite(AssetHandle handle) const;
        const AudioData* GetAudio(AssetHandle handle) const;
        // 오디오 자료를 풀기 전에 부를 곳이다(하나). 오디오 시스템이 프로젝트를 열 때 걸고 닫을 때 null 로 푼다.
        void SetAudioReleaseListener(AudioReleaseCallback callback, void* user);

        // 디스크의 최신 상태로 자료만 바꾼다. 핸들과 세대는 그대로다(asset-plan §2.7). 로드돼 있지 않으면 false.
        bool ReloadInPlace(AssetId id);
        // 로드된 것 전부를 in-place 재로드한다. 감시가 넘쳐 무엇이 바뀌었는지 모를 때 쓴다. 성공한 개수다.
        std::uint32_t ReloadAllInPlace();
        // 참조 수 0 인 것을 내린다. 내린 개수다. 스프라이트가 먼저 내려가고 그것이 놓은 텍스처가 따라 내려간다.
        std::uint32_t CollectUnused();
        std::uint32_t GetLoadedCount() const;

        // **해석 패스.** 컴포넌트의 `xxxId`(`AssetId`) 필드마다 로드해 짝 `xxx`(`AssetHandle`) 필드를 채운다.
        // 빈 아이디와 실패는 핸들을 비운다. 얻은 핸들은 `acquired` 에 쌓인다 - 캔버스를 닫을 때 `ReleaseAll` 로 놓는다.
        // 채운 핸들 수를 돌려준다.
        std::uint32_t BindComponentAssets(const PropertyTable& table, void* component, Array<AssetHandle>& acquired);
        void ReleaseAll(Array<AssetHandle>& acquired);

        static AssetType GetHandleType(AssetHandle handle);

    private:
        template <typename TData>
        struct Slot
        {
            TData data;
            AssetId id;
            std::uint32_t generation = 1;
            std::uint32_t referenceCount = 0;
            bool occupied = false;
        };

        template <typename TData>
        struct Pool
        {
            Array<Slot<TData>> slots;
            Array<std::uint32_t> freeList;
        };

        static constexpr std::uint32_t TypeShift = 28;
        static constexpr std::uint32_t SlotMask = (1u << TypeShift) - 1;

        template <typename TData>
        Slot<TData>* FindSlot(Pool<TData>& pool, AssetHandle handle, AssetType type);
        template <typename TData>
        const Slot<TData>* FindSlot(const Pool<TData>& pool, AssetHandle handle, AssetType type) const;
        template <typename TData>
        AssetHandle Occupy(Pool<TData>& pool, AssetType type, AssetId id, TData&& data);
        template <typename TData>
        void Vacate(Pool<TData>& pool, std::uint32_t slotIndex);

        bool ReadTexture(const AssetRecord& record, TextureData& data);
        bool ReadSpriteOptions(const AssetRecord& record, SpriteImportOptions& options);
        bool ReadTextureOptions(const AssetRecord& record, TextureImportOptions& options);
        bool ReadAudio(const AssetRecord& record, AudioData& data);
        void NotifyAudioRelease(std::uint32_t slotIndex);
        // 메타를 한 번만 파싱한다. 이미지의 Texture 와 Sprite 는 같은 파일이라 주인(Texture) 아이디로 캐시한다.
        // `ReloadInPlace` 가 그 자리를 비워 다음 읽기가 디스크를 본다 - 로드되지 않은 에셋의 옵션을 고쳐도 다음 로드가
        // 새 옵션으로 시작한다.
        bool ReadMeta(const AssetRecord& record, AssetMetaFile& meta);
        void ForgetMeta(AssetId id);
        Table<AssetId, AssetMetaFile> m_metaCache;
        bool BuildSprite(const AssetRecord& record, SpriteData& data);
        String MetaPathOf(const AssetRecord& record) const;
        String SourcePathOf(const AssetRecord& record) const;

        IPlatform* m_platform = nullptr;
        TextureFilter m_defaultTextureFilter = TextureFilter::Nearest;
        const AssetRegistry* m_registry = nullptr;
        String m_assetRoot;
        Pool<TextureData> m_textures;
        Pool<SpriteData> m_sprites;
        Pool<AudioData> m_audio;
        AudioReleaseCallback m_audioRelease = nullptr;
        void* m_audioReleaseUser = nullptr;
        Table<AssetId, AssetHandle> m_loaded;
    };
}
