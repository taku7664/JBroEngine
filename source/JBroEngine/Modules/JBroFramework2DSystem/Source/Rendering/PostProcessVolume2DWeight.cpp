#include <JBro/Framework2DSystem/Rendering/PostProcessVolume2DWeight.h>

#include <JBro/Framework2D/Component/PostProcessVolume2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>

#include <cmath>

namespace JBro
{
    namespace
    {
        Float Clamp01(Float value)
        {
            // 유한하지 않은 값(NaN)은 0 이다 - 비교가 둘 다 거짓이라 처음 값이 남는다.
            Float clamped = 0.0f;
            if (value >= 1.0f)
            {
                clamped = 1.0f;
            }
            else if (value > 0.0f)
            {
                clamped = value;
            }
            return clamped;
        }
    }

    Float ComputePostProcessVolumeWeight2D(
        const Component::PostProcessVolume2D& volume, const Component::Transform2D* transform, Vector2 camera)
    {
        const Float weight = Clamp01(volume.weight);
        if (volume.shape == Component::PostProcessVolumeShape2D::Global)
        {
            return weight;
        }
        if (transform == nullptr)
        {
            return 0.0f;
        }
        // 카메라를 볼륨의 회전 축으로 옮긴다. 거리는 월드 단위다 - 크기는 모양에 곱하고 축은 회전만 본다.
        const double dx = static_cast<double>(camera.x.Get()) - transform->worldPosition.x.Get();
        const double dy = static_cast<double>(camera.y.Get()) - transform->worldPosition.y.Get();
        const double angle = static_cast<double>(transform->worldRotation.Get());
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        const double localX = dx * cosine + dy * sine;
        const double localY = -dx * sine + dy * cosine;
        const double scaleX = std::fabs(static_cast<double>(transform->worldScale.x.Get()));
        const double scaleY = std::fabs(static_cast<double>(transform->worldScale.y.Get()));
        double outside = 0.0;
        if (volume.shape == Component::PostProcessVolumeShape2D::Box)
        {
            const double halfX = std::fabs(static_cast<double>(volume.size.x.Get())) * scaleX * 0.5;
            const double halfY = std::fabs(static_cast<double>(volume.size.y.Get())) * scaleY * 0.5;
            const double overX = std::fabs(localX) - halfX > 0.0 ? std::fabs(localX) - halfX : 0.0;
            const double overY = std::fabs(localY) - halfY > 0.0 ? std::fabs(localY) - halfY : 0.0;
            outside = std::sqrt(overX * overX + overY * overY);
        }
        else
        {
            const double radius = std::fabs(static_cast<double>(volume.radius.Get())) * (scaleX > scaleY ? scaleX : scaleY);
            const double distance = std::sqrt(localX * localX + localY * localY);
            outside = distance - radius > 0.0 ? distance - radius : 0.0;
        }
        if (outside <= 0.0)
        {
            return weight;
        }
        if (false == (volume.blendDistance > 0.0f))
        {
            return 0.0f;
        }
        return weight * Clamp01(static_cast<JBro::Float>(1.0 - outside / static_cast<double>(volume.blendDistance.Get())));
    }
}
