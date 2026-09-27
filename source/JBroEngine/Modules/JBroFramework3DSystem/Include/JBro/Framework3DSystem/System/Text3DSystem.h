#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/Core/Core.h>
#include <JBro/Framework3D/Component/Text3D.h>
#include <JBro/TextRendering/TextBlock.h>
#include <JBro/TextRendering/TextLibrary.h>
#include <JBro/TextRendering/TextSystemBase.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/FrameLiveness.h>
#include <JBro/Types/Table.h>

#include <cstdint>

namespace JBro
{
    class AssetSystem;
    class Renderer;
    class RenderWorld3D;
    class TaskManager;
}

namespace JBro::System
{
    // `Text3D` 를 글자마다 월드 텍스트 아이템으로 푼다(D-222). `Text2DSystem` 과 같은 세 걸음이다: 바뀐 텍스트만 다시 레이아웃하고(공용
    // `TextBlock`), 더러운 아틀라스 페이지만 올리고, 캐시된 글리프마다 아이템을 낸다. 아이템은 오브젝트 로컬의 사각형(유닛)과 오브젝트의
    // 월드 자리·회전·크기다 - 빌보드의 회전과 뒤→앞 정렬은 뷰마다 브리지가 한다(게임 카메라와 편집 카메라가 다르다).
    //
    // 폰트 표와 아틀라스(`TextLibrary`)는 이 시스템이 제 것으로 든다. 2D 텍스트 시스템과 나눠 쓰지 않는다 - 캔버스 하나는 한 차원이다.
    // 스크립트 서비스(`Text3DService`)가 부르는 글자 읽기·쓰기는 공용 `TextSystemBase` 가 한다(D-224).
    class Text3DSystem final : public GameSystem, public TextSystemBase
    {
    public:
        static constexpr int ExecutionOrder = 410; // 메시(400) 뒤

        ~Text3DSystem() override;
        int GetExecutionOrder() const override;

        void SetRenderWorld(RenderWorld3D* renderWorld);
        // 폰트를 읽을 에셋 시스템과 페이지를 올릴 렌더러다. 둘 중 하나가 없으면 텍스트를 그리지 않는다.
        void SetResources(AssetSystem* assets, Renderer* renderer, TaskManager* tasks = nullptr);

        // 마지막으로 레이아웃한 블록 사각형이다(유닛, 오브젝트 로컬 XY). 아직 없으면 거짓이다.
        bool GetLocalBounds(InstanceId text, float& minX, float& minY, float& maxX, float& maxY) const;
        // 쓸 수 있는 폰트가 없어 그리지 못하는 텍스트인가.
        bool IsMissingFont(InstanceId text) const;
        const TextLibrary& GetLibrary() const;
        // 지난 프레임에 렌더 월드의 텍스트 용량을 넘어 그리지 못한 글자 수다.
        std::uint32_t GetDroppedGlyphCount() const;
        std::uint64_t GetRelayoutCount() const;
        std::uint32_t GetCachedTextCount() const;

    protected:
        void OnUpdate(Canvas& canvas, float deltaTime) override;
        void OnShutdown(Canvas& canvas) override;

    private:
        struct Entry
        {
            TextBlock     block;
            bool          warnedMissingFont = false;
            std::uint64_t lastSeenFrame = 0;
        };

        static TextBlockSettings SettingsOf(const Component::Text3D& text);
        void Submit(Canvas& canvas, const Component::Text3D& text, const Entry& entry);
        void DropUnseen();

        RenderWorld3D*           m_renderWorld = nullptr;
        TextLibrary              m_library;
        Table<InstanceId, Entry> m_entries;
        Array<InstanceId>        m_scratchUnseen;
        std::uint64_t            m_frame = 0;
        std::uint64_t            m_relayouts = 0;
        std::uint32_t            m_droppedGlyphsThisFrame = 0;
        std::uint32_t            m_droppedGlyphs = 0;
        bool                     m_warnedDroppedGlyphs = false;
    };
}
