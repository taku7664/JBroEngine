#include <JBro/ScreenUi/Rendering/Parallax2D.h>

#include <cmath>

namespace JBro
{
    Bool ComputeParallaxOffset2D(const Matrix3x2& view, Float factor, Float& dx, Float& dy)
    {
        // 카메라의 월드 자리는 뷰가 원점으로 보내는 점이다: x·m11 + y·m21 + m31 = 0, x·m12 + y·m22 + m32 = 0.
        const Float det = view.m11 * view.m22 - view.m21 * view.m12;
        if (false == std::isfinite(det) || std::fabs(det) < 1e-12f)
        {
            return false;
        }
        const Float cameraX = (view.m21 * view.m32 - view.m31 * view.m22) / det;
        const Float cameraY = (view.m31 * view.m12 - view.m11 * view.m32) / det;
        dx = cameraX * (1.0f - factor);
        dy = cameraY * (1.0f - factor);
        return std::isfinite(dx) && std::isfinite(dy);
    }
}
