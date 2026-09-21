#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>

#include <cstdint>

namespace JBro
{
    class AssetSystem;

    // 스프라이트의 **실제 모양**을 선분으로 낸다(D-149).
    //
    // 기존 엔진의 `CCanvasViewContour` 자리다. 선택 표시를 사각형으로만 두르면, 그림이
    // 칸의 한 귀퉁이에만 있을 때 무엇을 골랐는지 화면에서 알기 어렵다 - 빈자리까지 테두리가
    // 둘러쳐지기 때문이다.
    //
    // **폴리곤을 잇지 않고 선분을 모은다.** 불투명한 픽셀의 네 변 중 이웃이 투명한 쪽만
    // 남기면 그것이 곧 경계다. 선분을 이어 하나의 고리로 만드는 일은, 구멍이 있거나 그림이
    // 여러 덩어리로 갈린 경우를 위해 따로 다뤄야 하는데 - 화면에 그리는 데에는 이을 필요가 없다.
    //
    // 좌표는 **칸 안의 비율**이다(왼쪽 위 0,0 에서 오른쪽 아래 1,1). 크기·피벗·회전은 그리는
    // 쪽이 얹는다 - 같은 그림을 쓰는 오브젝트가 여럿이면 저마다 다른 크기로 서기 때문이다.
    class EditorSpriteContours
    {
    public:
        // 경계를 재는 해상도의 한계다. 넘으면 정수 배로 건너뛰며 줄여 잰다 - 1024 짜리
        // 그림의 픽셀 경계를 그대로 그리면 선분이 수천 개가 되고, 화면에서는 굵은 띠로 뭉친다.
        static constexpr std::uint32_t MaxSide = 96;
        // 이 값보다 옅은 픽셀은 없는 것으로 본다. 0 으로 두면 눈에 보이지 않는 가장자리의
        // 잔여 알파까지 경계가 되어, 윤곽이 그림보다 한 겹 크게 나온다.
        static constexpr std::uint8_t AlphaThreshold = 8;
        // 한 프레임에 새로 재는 개수다. 고른 것이 많아도 그 프레임이 늘어지지 않게 막는다.
        static constexpr std::uint32_t MaxNewPerFrame = 2;

        struct Segment
        {
            float x0 = 0.0f;
            float y0 = 0.0f;
            float x1 = 0.0f;
            float y1 = 0.0f;
        };

        void Initialize(AssetSystem& assets);
        void Shutdown();
        void BeginFrame();

        // 이 텍스처의 이 칸이 그리는 모양이다. 아직 재지 못했으면 nullptr 다(다음 프레임에
        // 다시 물으면 있다). 칸이 통째로 불투명하면 빈 배열이 아니라 **사각형 네 변**이다 -
        // 부르는 쪽이 그 경우를 따로 다루지 않아도 되게.
        // **핸들로 가린다.** 아이디가 아니라 로드된 핸들인 것은, 부르는 쪽이 이미 그
        // 핸들을 들고 있기 때문이다(스프라이트가 자기 텍스처를 참조 수로 잡고 있다) -
        // 아이디로 받으면 여기서 다시 로드해 참조 수를 올렸다 내려야 한다.
        const Array<Segment>* Get(AssetHandle texture, const SpriteFrame& frame);
        void Clear();

    private:
        struct Entry
        {
            AssetHandle texture;
            // 칸의 자리와 크기다. 같은 텍스처라도 칸마다 모양이 다르다.
            std::uint32_t x = 0;
            std::uint32_t y = 0;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint32_t pixelGeneration = 0;
            Array<Segment> segments;
            bool failed = false;
        };

        Entry* Find(AssetHandle texture, const SpriteFrame& frame);
        bool Build(AssetHandle texture, const SpriteFrame& frame, Entry& entry);

        AssetSystem* m_assets = nullptr;
        Array<Entry> m_entries;
        std::uint32_t m_budget = 0;
    };
}
