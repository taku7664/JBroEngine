#pragma once

#include <cstdint>

namespace JBro
{
    // 블렌드·불투명도(D-279)와 패럴랙스(D-285)는 여기 없다 - 캔버스의 `Layer` 가 든다. 렌더가 읽지 않던 둘째 자리였다.
    class Layer2D final
    {
    public:
        bool ForcesOwnTexture() const;
        void SetForceOwnTexture(bool enabled);

    private:
        bool      m_forceOwnTexture = false;
    };
}
