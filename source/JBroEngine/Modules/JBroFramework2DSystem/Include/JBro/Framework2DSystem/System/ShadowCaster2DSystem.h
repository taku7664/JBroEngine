#pragma once

#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
#include <JBro/Framework2DSystem/Rendering/SpriteLibrary.h>
#include <JBro/Canvas/GameSystem.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>

namespace JBro::System
{
    // **그림자 변을 렌더 월드에 담는다**(D-291 3 단계). `ShadowCaster2D` 의 모양(콜라이더나 스프라이트 사각형)을 월드로 펴 시계 반대 방향으로 감은 변으로
    // 낸다. 감긴 방향은 넓이의 부호로 재고 뒤집힌 크기(거울)도 바로잡는다. 꼭짓점을 차례로 셈해 내므로 프레임마다 힙을 잡지 않는다.
    class ShadowCaster2DSystem final : public GameSystem
    {
    public:
        // 스프라이트 크기가 풀린 뒤다(스프라이트 추출 400).
        Int32 GetExecutionOrder() const override;

        void SetRenderWorld(RenderWorld2D* renderWorld);
        // `Sprite` 모양이 에셋의 크기와 피벗을 푸는 곳이다. 없으면 컴포넌트의 `size`·`pivot` 이다.
        void SetSpriteLibrary(SpriteLibrary* library);
        void ExtractRenderWorld(Canvas& canvas);

    protected:
        void OnUpdate(Canvas& canvas, Float deltaTime) override;

    private:
        RenderWorld2D* m_renderWorld = nullptr;
        SpriteLibrary* m_spriteLibrary = nullptr;
    };
}
