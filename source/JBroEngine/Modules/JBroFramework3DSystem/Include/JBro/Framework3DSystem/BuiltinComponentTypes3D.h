#pragma once

namespace JBro::Component
{
    // 3D 빌트인 컴포넌트를 이름으로 붙일 수 있게 `ComponentRegistry` 에 넣는다.
    //
    // 프로퍼티 등록(`BuiltinComponentProperties3D.h`)과 따로인 이유는 **보는 모듈이
    // 다르기 때문이다.** 프로퍼티는 Core 만 있으면 되어 `JBroFramework3D` 안에서 하지만,
    // 붙이는 함수는 `Canvas` 를 보아야 하고 그것은 Tier E 다(D-42). 2D 쪽과 같은 갈림이다.
    //
    // 두 번 불러도 된다. 프레임 루프에서 부르는 함수가 아니다.
    bool RegisterBuiltinComponentTypes3D();
}
