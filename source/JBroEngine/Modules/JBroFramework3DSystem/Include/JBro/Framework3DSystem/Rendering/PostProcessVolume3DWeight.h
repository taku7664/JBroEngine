#pragma once

#include <JBro/Types/Float.h>
#include <JBro/Types/Math3D.h>

namespace JBro::Component
{
    class PostProcessVolume3D;
    class Transform3D;
}

namespace JBro
{
    // **카메라 자리에서 3D 볼륨의 무게**(D-346). 2D 의 `ComputePostProcessVolumeWeight2D` 와 같은 셈이고 모양이 상자·구다.
    Float ComputePostProcessVolumeWeight3D(
        const Component::PostProcessVolume3D& volume, const Component::Transform3D* transform, const Vector3& camera);
}
