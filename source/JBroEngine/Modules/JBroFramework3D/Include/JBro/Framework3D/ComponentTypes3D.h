#pragma once

// 3D 빌트인 컴포넌트의 열거형이다. 컴포넌트 데이터 헤더와 스크립트가 함께 본다(D-302 - 데이터 헤더는 스크립트 대상에서 막혀 있다).

#include <JBro/Reflection/EnumDescriptor.h>

#include <cstdint>

namespace JBro::Component
{
    enum class CameraProjection3D { Perspective, Orthographic };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::CameraProjection3D, "Component::CameraProjection3D",
        { Component::CameraProjection3D::Perspective,  "Perspective" },
        { Component::CameraProjection3D::Orthographic, "Orthographic" });
}

namespace JBro::Component
{
    // 글자 판이 어디를 보나(D-222). Transform 은 오브젝트의 회전을 따르는 판(간판·벽의 글씨), Billboard 는 늘 카메라를 보는 판(이름표)이다.
    // Billboard 는 오브젝트의 위치와 크기만 쓰고 회전은 뷰마다 카메라의 것으로 바꾼다 - 시뮬레이션 뷰와 편집 뷰가 각자 제 카메라를 본다.
    enum class TextFacing3D : std::uint8_t { Transform, Billboard };
}

namespace JBro
{
    JBRO_DEFINE_ENUM_TYPE(Component::TextFacing3D, "Component::TextFacing3D",
        { Component::TextFacing3D::Transform, "Transform" },
        { Component::TextFacing3D::Billboard, "Billboard" });
}
