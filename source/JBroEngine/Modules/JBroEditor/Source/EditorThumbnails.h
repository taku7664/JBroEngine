#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/RHI/RHI.h>
#include <JBro/Types/Array.h>

#include <cstdint>

namespace JBro
{
    class AssetSystem;

    // 에셋의 작은 그림을 만들어 든다(D-147).
    //
    // **에셋은 CPU 픽셀만 든다**(asset-plan §2.5). 화면에 붙이려면 GPU 텍스처가 있어야 하고,
    // 스프라이트를 그리는 쪽(`SpriteLibrary`)이 드는 것은 렌더러의 핸들이지 ImGui 가 받는
    // 것이 아니다. 그래서 에디터가 자기 것을 따로 만든다.
    //
    // **줄여서 든다.** 4096 짜리 그림 스무 장을 원본 크기로 들면 목록을 여는 것만으로
    // 수백 MB 가 GPU 에 올라간다. 긴 변이 `MaxSide` 를 넘으면 정수 배로 건너뛰며 줄인다 -
    // 가중 평균이 아니라 건너뛰기인 것은, 픽셀 아트의 또렷한 가장자리를 지키기 위해서다.
    //
    // 만드는 일은 **RHI 프레임 밖**에서만 된다. 에디터는 UI 를 프레임 밖에서 만들므로
    // (EditorApplication::Tick) 패널이 그리는 자리에서 바로 불러도 된다. 다만 한 프레임에
    // 여럿을 만들면 그 프레임이 늘어져, 프레임마다 만드는 개수를 막아 둔다.
    class EditorThumbnails
    {
    public:
        // 긴 변의 최대 픽셀이다.
        static constexpr std::uint32_t MaxSide = 128;
        // 한 프레임에 새로 만드는 개수다. 나머지는 다음 프레임에 만들어진다 -
        // 폴더를 열자마자 백 장을 올리면 그 프레임이 눈에 띄게 멈춘다.
        static constexpr std::uint32_t MaxNewPerFrame = 4;

        void Initialize(IRHIDevice& device, AssetSystem& assets);
        void Shutdown();
        // 프레임이 시작될 때 부른다. 이번 프레임에 만들 수 있는 개수를 되돌린다.
        void BeginFrame();

        // 이 에셋의 그림이다. 아직 없으면 만들어 본다(이번 프레임의 몫이 남아 있을 때).
        // 텍스처가 아닌 에셋이거나 읽지 못하면 빈 핸들이다.
        TextureHandle Get(AssetId asset);
        // 다시 읽힌 에셋의 그림을 버린다. 다음에 물으면 새로 만든다.
        void Invalidate(AssetId asset);
        void Clear();

        std::uint32_t GetCount() const { return static_cast<std::uint32_t>(m_entries.Size()); }

    private:
        struct Entry
        {
            AssetId asset;
            TextureHandle texture;
            // 만들 때 본 픽셀 세대다. 에셋이 제자리에서 다시 읽히면 이 값이 달라진다.
            std::uint32_t pixelGeneration = 0;
            // 만들려다 실패했는가. 실패한 것을 프레임마다 다시 만들려 들면 목록이 멈춘다.
            bool failed = false;
        };

        Entry* Find(AssetId asset);
        bool Build(AssetId asset, Entry& entry);

        IRHIDevice* m_device = nullptr;
        AssetSystem* m_assets = nullptr;
        Array<Entry> m_entries;
        std::uint32_t m_budget = 0;
    };
}
