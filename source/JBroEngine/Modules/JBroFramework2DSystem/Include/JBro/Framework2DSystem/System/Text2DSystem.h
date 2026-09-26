#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Component/Text2D.h>
#include <JBro/TextRendering/GlyphMesh.h>
#include <JBro/TextRendering/TextBlock.h>
#include <JBro/TextRendering/TextLibrary.h>
#include <JBro/TextRendering/TextSystemBase.h>
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
    class Text2DSystem final : public GameSystem, public TextSystemBase
    {
    public:
        static constexpr int ExecutionOrder = 410; // 스프라이트(400) 뒤, 오디오(450) 앞

        ~Text2DSystem() override;
        int GetExecutionOrder() const override;

        void SetRenderWorld(RenderWorld2D* renderWorld);
        // 폰트를 읽을 에셋 시스템과 페이지를 올릴 렌더러다. 둘 중 하나가 없으면 텍스트를 그리지 않는다.
        // tasks 가 있으면 폰트의 미리 뜨기가 워커에서 돈다(없어도 된다).
        void SetResources(AssetSystem* assets, Renderer* renderer, TaskManager* tasks = nullptr);

        // 스크립트 서비스(`Text2DService`)가 부르는 글자 읽기·쓰기는 공용 `TextSystemBase` 가 한다(D-224).

        // 마지막으로 레이아웃한 블록 사각형이다(유닛, 오브젝트 로컬). 에디터의 선택과 외곽선이 쓴다. 아직 없으면 거짓이다.
        bool GetLocalBounds(InstanceId text, float& minX, float& minY, float& maxX, float& maxY) const;
        // 쓸 수 있는 폰트가 없어 그리지 못하는 텍스트인가. 에디터 인스펙터가 경고로 보인다 - 그리는 쪽과 같은 판단을
        // 따로 흉내 내지 않고 여기서 묻는다. 아직 한 번도 돌지 않은 텍스트는 거짓이다.
        bool IsMissingFont(InstanceId text) const;
        // 마지막으로 레이아웃한 글자 크기(em 픽셀)다. 자동 크기면 찾은 크기다. 레이아웃이 없으면 0 이다.
        float GetLaidOutFontSize(InstanceId text) const;

        const TextLibrary& GetLibrary() const;
        // 퇴출 한도(폰트 하나의 아틀라스 페이지 수)다. 테스트가 작게 줄여 퇴출을 부른다.
        void SetAtlasPageLimit(std::uint32_t pages);
        // 지난 프레임에 스프라이트 제출 상한을 넘어 그리지 못한 글자 수다.
        std::uint32_t GetDroppedGlyphCount() const;
        // 지금까지 다시 레이아웃한 횟수다. 테스트가 "바뀌지 않은 텍스트는 다시 레이아웃하지 않는다" 를 잰다.
        std::uint64_t GetRelayoutCount() const;
        // 캐시에 들어 있는 텍스트 수다.
        std::uint32_t GetCachedTextCount() const;

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;
        void OnShutdown(Canvas& canvas) override;

    private:
        struct Entry
        {
            // 레이아웃과 쿼드는 공용 캐시가 든다(D-222). 여기는 이 시스템의 몫만이다.
            TextBlock            block;
            bool                 warnedMissingFont = false;
            std::uint64_t        lastSeenFrame = 0;
        };

        static TextBlockSettings SettingsOf(const Component::Text2D& text);
        void Submit(Canvas& canvas, const Component::Text2D& text, const Entry& entry);
        void DropUnseen();

        RenderWorld2D*               m_renderWorld = nullptr;
        TextLibrary                  m_library;
        Table<InstanceId, Entry>     m_entries;
        Array<InstanceId>            m_scratchUnseen;
        std::uint64_t                m_frame = 0;
        std::uint64_t                m_relayouts = 0;
        std::uint32_t                m_droppedGlyphsThisFrame = 0;
        std::uint32_t                m_droppedGlyphs = 0;
        bool                         m_warnedDroppedGlyphs = false;
    };
}
