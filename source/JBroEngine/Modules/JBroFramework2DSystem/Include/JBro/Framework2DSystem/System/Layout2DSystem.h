#pragma once

#include <JBro/Canvas/GameSystem.h>

#include <JBro/Types/Array.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro
{
    class GameScript2D;
    class SpriteLibrary;
}

namespace JBro::System
{
    // **자식을 칸에 놓는다**(D-352 6 단계). 위젯 시스템(50) 뒤, 트랜스폼 전파(100) 앞에 돌아 같은 프레임에 보인다.
    // 자식의 `position` 을 매 프레임 쓴다 - 손으로 옮긴 자리는 남지 않는다.
    // `fitChildren` 이면 칸을 자식의 보이는 크기로 재고(D-355), 레이아웃 뒤에 `ContentSizeFitter2D` 가 같은 오브젝트의 내용 크기로 스프라이트·마스크·위젯 사각형을
    // 맞춘다. 글자의 크기는 지난 프레임의 레이아웃이다(글자는 추출 때 레이아웃한다) - 한 프레임 늦다.
    // 가상화 목록(`ListView2D`, D-357)도 여기서 돈다 - 레이아웃보다 먼저, 뷰포트에 걸치는 항목만 풀의 오브젝트에 묶어 놓는다.
    class Layout2DSystem final : public GameSystem
    {
    public:
        static constexpr Int32 ExecutionOrder = 60;

        Int32 GetExecutionOrder() const override;
        // 에셋 크기(`FromSprite`)의 스프라이트를 재는 데 쓴다. 없으면 그 스프라이트는 `cellSize` 다.
        void SetSpriteLibrary(SpriteLibrary* library);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;

    private:
        // 목록이 항목을 묶을 때 부를 스크립트를 먼저 모으는 자리다. 멤버로 두어 부를 때마다 할당하지 않는다.
        Array<GameScript2D*> m_hookTargets;
        SpriteLibrary* m_spriteLibrary = nullptr;
    };
}
