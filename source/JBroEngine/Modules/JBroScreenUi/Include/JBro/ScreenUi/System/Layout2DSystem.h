#pragma once

#include <JBro/Canvas/GameSystem.h>
#include <JBro/ScreenUi/UiHost.h>

#include <JBro/Core/Core.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro
{
    class SpriteLibrary;
    class GameScriptBase;
    namespace Object
    {
        class GameObject;
    }
}

namespace JBro::System
{
    // **자식을 칸에 놓는다**(D-352 6 단계). 위젯 시스템(50) 뒤, 트랜스폼 전파(100) 앞에 돌아 같은 프레임에 보인다.
    // 자식의 `position` 을 매 프레임 쓴다 - 손으로 옮긴 자리는 남지 않는다.
    // `fitChildren` 이면 칸을 자식의 보이는 크기로 재고(D-355), 레이아웃 뒤에 `ContentSizeFitter2D` 가 같은 오브젝트의 내용 크기로 스프라이트·마스크·위젯 사각형을
    // 맞춘다. 글자의 크기는 지난 프레임의 레이아웃이다(글자는 추출 때 레이아웃한다) - 한 프레임 늦다.
    // 가상화 목록(`ListView2D`, D-357)도 여기서 돈다 - 레이아웃보다 먼저, 뷰포트에 걸치는 항목만 풀의 오브젝트에 묶어 놓는다.
    // 가변 크기 목록(D-360)의 누적 자리(항목마다 끝 오프셋)는 목록마다 여기 든다 - 잴 때만 할당하고, 사라진 목록의 것은 다음 갱신에 버린다.
    class Layout2DSystem final : public GameSystem
    {
    public:
        static constexpr Int32 ExecutionOrder = 60;

        Int32 GetExecutionOrder() const override;
        // 에셋 크기(`FromSprite`)의 스프라이트를 재는 데 쓴다. 없으면 그 스프라이트는 `cellSize` 다.
        void SetSpriteLibrary(SpriteLibrary* library);
        // 목록 훅(`OnListItem`·`OnListMeasure`)을 부르는 자리다(D-362). 없으면 훅이 없다 - 가변 목록은 `itemSize` 로 잰다.
        void SetHookDispatcher(IUiHookDispatcher* hooks);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;

    private:
        // 가변 크기 목록 하나의 누적 자리다(D-360). `ends[i]` 는 항목 i 의 끝 오프셋(진행 축, 시작에서)이고 시작은 `ends[i - 1] + spacing` 이다.
        struct ListCache
        {
            InstanceId   owner = InvalidInstanceId;
            Array<Float> ends;
            Bool         seen = false;
        };
        // 이 목록의 자리 저장소다. 없으면 만든다.
        ListCache& CacheFor(InstanceId owner);
        // 목록의 스크립트를 모은다(`m_hookTargets`).
        void CollectHooks(Object::GameObject& owner);
        // 목록이 항목을 묶을 때 부를 스크립트를 먼저 모으는 자리다. 멤버로 두어 부를 때마다 할당하지 않는다.
        Array<GameScriptBase*> m_hookTargets;
        IUiHookDispatcher* m_hooks = nullptr;
        Array<ListCache>     m_listCaches;
        SpriteLibrary* m_spriteLibrary = nullptr;
    };
}
