#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/Framework2D/System/IText2DSystem.h>
#include <JBro/Framework2DSystem/Rendering/TextLibrary.h>
#include <JBro/Text/TextLayout.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Table.h>

#include <cstdint>

namespace JBro
{
    class AssetSystem;
    class Renderer;
    class RenderWorld2D;
}

namespace JBro::System
{
    // `Text2D` 를 글자마다 스프라이트 아이템으로 푼다(D-200 (4), text-plan §4.1·§4.3).
    //
    // 한 프레임은 세 걸음이다. (1) 글자·폰트·옵션이 바뀐 텍스트만 다시 레이아웃하고 새 글리프를 아틀라스에 넣는다.
    // (2) 더러운 아틀라스 페이지만 올린다(렌더러 프레임 밖이다). (3) 캐시된 글리프마다 아이템을 낸다.
    // 바뀌지 않은 텍스트는 표 조회 하나와 아이템 제출뿐이고 할당이 없다 - 변경은 저장소 판번호와 정수 비교로 안다.
    //
    // 레이아웃과 경계는 이 시스템의 캐시가 든다(키는 컴포넌트의 InstanceId). **컴포넌트에 되쓰지 않는다**(text-plan §1.2 의 6 번).
    // 이 시스템이 `TextLibrary` 를 소유하므로 페이지 텍스처는 캔버스의 시스템이 내려갈 때(렌더러보다 먼저) 풀린다.
    class Text2DSystem final : public GameSystem, public IText2DSystem
    {
    public:
        static constexpr int ExecutionOrder = 410; // 스프라이트(400) 뒤, 오디오(450) 앞

        ~Text2DSystem() override;
        int GetExecutionOrder() const override;

        void SetRenderWorld(RenderWorld2D* renderWorld);
        // 폰트를 읽을 에셋 시스템과 페이지를 올릴 렌더러다. 둘 중 하나가 없으면 텍스트를 그리지 않는다.
        void SetResources(AssetSystem* assets, Renderer* renderer);

        // IText2DSystem - 스크립트 서비스가 부른다. 호스트의 저장소에 쓴다.
        void SetText(Component::Text2D& text, const char* utf8, std::uint32_t length) override;
        std::uint32_t GetTextLength(const Component::Text2D& text) const override;
        std::uint32_t CopyText(const Component::Text2D& text, char* buffer, std::uint32_t capacity) const override;

        // 마지막으로 레이아웃한 블록 사각형이다(유닛, 오브젝트 로컬). 에디터의 선택과 외곽선이 쓴다. 아직 없으면 거짓이다.
        bool GetLocalBounds(InstanceId text, float& minX, float& minY, float& maxX, float& maxY) const;

        const TextLibrary& GetLibrary() const;
        // 지금까지 다시 레이아웃한 횟수다. 테스트가 "바뀌지 않은 텍스트는 다시 레이아웃하지 않는다" 를 잰다.
        std::uint64_t GetRelayoutCount() const;
        // 캐시에 들어 있는 텍스트 수다.
        std::uint32_t GetCachedTextCount() const;

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;
        void OnShutdown(Canvas& canvas) override;

    private:
        struct GlyphQuad
        {
            // 로컬 픽셀(블록 기준점 원점, y 위쪽)의 왼쪽 위 모서리와 크기다.
            float         left = 0.0f;
            float         top = 0.0f;
            float         width = 0.0f;
            float         height = 0.0f;
            float         uvRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
            std::uint16_t page = 0;
        };

        struct Entry
        {
            // 이 캐시를 만든 입력이다. 하나라도 다르면 다시 레이아웃한다.
            TextId               text;
            std::uint32_t        textRevision = 0;
            AssetHandle          font;
            std::uint32_t        fontGeneration = 0;
            std::uint64_t        optionsKey = 0;
            Text::TextLayout     layout;
            Array<GlyphQuad>     quads;
            float                pixelsPerUnit = DefaultPixelsPerUnit;
            TextureFilter        filter = TextureFilter::Nearest;
            float                bounds[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // 유닛
            bool                 hasBounds = false;
            bool                 warnedMissingFont = false;
            std::uint64_t        lastSeenFrame = 0;
        };

        static std::uint64_t MakeOptionsKey(const Component::Text2D& text);
        void Relayout(const Component::Text2D& text, Entry& entry, const FontView& font);
        void Submit(Canvas& canvas, const Component::Text2D& text, const Entry& entry);
        void DropUnseen();

        RenderWorld2D*               m_renderWorld = nullptr;
        TextLibrary                  m_library;
        Table<InstanceId, Entry>     m_entries;
        Array<InstanceId>            m_scratchUnseen;
        std::uint64_t                m_frame = 0;
        std::uint64_t                m_relayouts = 0;
    };
}
