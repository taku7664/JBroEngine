#pragma once

#include <cstdint>

namespace JBro
{
    // 블렌드와 불투명도는 여기 없다 - 캔버스의 `Layer` 가 든다(D-279). 렌더가 읽지 않던 둘째 자리였다.
    class Layer2D final
    {
    public:
        float GetParallaxFactor() const;
        void SetParallaxFactor(float factor);

        bool ForcesOwnTexture() const;
        void SetForceOwnTexture(bool enabled);

    private:
        float     m_parallaxFactor = 1.0f;
        bool      m_forceOwnTexture = false;
    };
}
