#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Graphics/PostProcess.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

#include <cstddef>

namespace JBro
{
    class AssetSystem;
    class Renderer;

    // **후처리 프로필 에셋을 뷰의 효과 목록으로 푼다**(D-344, postprocess-plan §2.5). `SpriteLibrary` 와 같은 자리다 - 에셋은 본문 글자만 들고,
    // 풀린 블록과 LUT 의 GPU 텍스처는 여기가 든다. 2D·3D 프레임워크가 하나씩 든다.
    //
    // **프레임 경로에 할당이 없다.** 프로필은 처음 볼 때와 본문이 바뀔 때(`dataGeneration`)만 다시 풀고, 그 밖의 프레임은 찾아서 견주기만 한다.
    // 프로필 안의 에셋 참조(`colorLutId`)는 해석 패스(`AssetSystem::BindComponentAssets`)로 싣고, `.cube` 는 렌더러의 3D 텍스처로 올린다.
    // **렌더러 프레임 밖에서만 부른다** - 텍스처를 올리고 지운다. 프레임워크의 Update(렌더 추출)가 그 자리다.
    class PostProcessLibrary final
    {
    public:
        PostProcessLibrary() = default;
        PostProcessLibrary(const PostProcessLibrary&) = delete;
        PostProcessLibrary& operator=(const PostProcessLibrary&) = delete;
        ~PostProcessLibrary();

        void Initialize(AssetSystem* assets, Renderer* renderer);
        void Shutdown();

        // 프로필 에셋의 효과 목록이다. 가리키는 곳은 다음 `Resolve`·`Shutdown` 까지 산다. 빈 핸들·로드되지 않은 에셋·읽히지 않는 본문은 빈 목록이다.
        PostProcessStack Resolve(AssetHandle profile);

        // 시험이 본다.
        UInt32 GetResolveCount() const noexcept
        {
            return m_resolveCount;
        }
        UInt32 GetLutTextureCount() const noexcept;

    private:
        struct Profile
        {
            AssetHandle asset;
            UInt32 dataGeneration = 0;
            Array<PostProcessEntry> entries;
            Array<std::byte> blocks;
            // 블록 안의 에셋 참조로 잡은 핸들이다. 다시 풀 때 놓는다.
            Array<AssetHandle> acquired;
            // 이 프로필이 쓰는 LUT 와 그 세대다. 세대가 오르면 다시 푼다.
            Array<AssetHandle> luts;
            Array<UInt32> lutGenerations;
            Bool used = false;
        };
        struct LutTexture
        {
            AssetHandle asset;
            UInt32 dataGeneration = 0;
            AssetHandle texture;
        };

        Bool IsStale(const Profile& profile) const;
        void Build(Profile& profile);
        AssetHandle EnsureLut(AssetHandle lutAsset);
        void ReleaseProfile(Profile& profile);

        AssetSystem* m_assets = nullptr;
        Renderer* m_renderer = nullptr;
        Array<Profile> m_profiles;
        Array<LutTexture> m_luts;
        UInt32 m_resolveCount = 0;
    };
}
