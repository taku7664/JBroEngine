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

    // **풀린 프로필 하나와 항목마다 덮어쓴 필드**(D-346). 볼륨이 섞을 때 덮어쓴 필드만 섞는다. `overrides` 는 `stack.entries` 와 같은 차례다.
    struct PostProcessProfileView
    {
        PostProcessStack stack;
        JArrayView<UInt64> overrides;
    };

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

        // 프로필 에셋의 효과 목록이다. 가리키는 곳은 그 프로필을 다시 풀 때(본문이 바뀐 뒤의 `Resolve`)와 `Shutdown` 까지 산다 - 다른 프로필을
        // 풀어도 그대로다. 빈 핸들·로드되지 않은 에셋·읽히지 않는 본문은 빈 목록이다.
        PostProcessStack Resolve(AssetHandle profile);
        // 위와 같고 항목마다 덮어쓴 필드도 준다(D-346).
        PostProcessProfileView ResolveProfile(AssetHandle profile);
        // **아이디로 프로필을 싣는다**(D-345, 레이어의 프로필). 처음 보는 아이디만 싣고(`AssetSystem::Load`) 그 핸들을 `Shutdown` 까지 든다 -
        // 다음 프레임부터는 찾기만 한다. 빈 아이디·싣지 못한 아이디는 빈 핸들이다(못 실은 것도 기억해 매 프레임 다시 싣지 않는다).
        AssetHandle FindOrLoad(AssetId profile);

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
            // 항목마다 덮어쓴 필드다(`PostProcessProfileEntry::overrides`).
            Array<UInt64> overrides;
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
        // `FindOrLoad` 가 실은 프로필이다. 핸들은 `Shutdown` 이 놓는다.
        Array<AssetId> m_loadedIds;
        Array<AssetHandle> m_loadedHandles;
        UInt32 m_resolveCount = 0;
    };
}
