#include <JBro/Framework3DSystem/Rendering/PostProcessVolume3DWeight.h>

#include <JBro/Framework3D/Component/PostProcessVolume3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>

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

        double Over(double value, double half)
        {
            const double over = std::fabs(value) - half;
            return over > 0.0 ? over : 0.0;
        }
    }

    Float ComputePostProcessVolumeWeight3D(
        const Component::PostProcessVolume3D& volume, const Component::Transform3D* transform, const Vector3& camera)
    {
        const Float weight = Clamp01(volume.weight);
        if (volume.shape == Component::PostProcessVolumeShape3D::Global)
        {
            return weight;
        }
        if (transform == nullptr)
        {
            return 0.0f;
        }
        // 카메라를 볼륨의 회전 축으로 옮긴다(회전의 켤레로 돌린다). 거리는 월드 단위다.
        const Vector3 offset = {camera.x - transform->worldPosition.x, camera.y - transform->worldPosition.y,
            camera.z - transform->worldPosition.z};
        const Vector3 local = Rotate(Conjugate(transform->worldRotation), offset);
        const double scaleX = std::fabs(static_cast<double>(transform->worldScale.x.Get()));
        const double scaleY = std::fabs(static_cast<double>(transform->worldScale.y.Get()));
        const double scaleZ = std::fabs(static_cast<double>(transform->worldScale.z.Get()));
        double outside = 0.0;
        if (volume.shape == Component::PostProcessVolumeShape3D::Box)
        {
            const double overX = Over(local.x.Get(), std::fabs(static_cast<double>(volume.size.x.Get())) * scaleX * 0.5);
            const double overY = Over(local.y.Get(), std::fabs(static_cast<double>(volume.size.y.Get())) * scaleY * 0.5);
            const double overZ = Over(local.z.Get(), std::fabs(static_cast<double>(volume.size.z.Get())) * scaleZ * 0.5);
            outside = std::sqrt(overX * overX + overY * overY + overZ * overZ);
        }
        else
        {
            const double largest = scaleX > scaleY ? (scaleX > scaleZ ? scaleX : scaleZ) : (scaleY > scaleZ ? scaleY : scaleZ);
            const double radius = std::fabs(static_cast<double>(volume.radius.Get())) * largest;
            const double x = local.x.Get();
            const double y = local.y.Get();
            const double z = local.z.Get();
            const double distance = std::sqrt(x * x + y * y + z * z);
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
