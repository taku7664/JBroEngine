#pragma once

namespace JBro::Component
{
    // 2D 빌트인 컴포넌트의 프로퍼티 표를 빌트인 보관함에 넣는다(D-56 계획서 §8.4).
    //
    // 표 자체는 각 컴포넌트가 `JBRO_FIELD` 로 이미 만들어 두었다. 여기서 하는 일은
    // 그것을 **이름으로 찾을 수 있게** 하는 것뿐이다 — 인스펙터는 손에 든 것이 무슨
    // 타입인지만 알고 정의는 못 보기 때문이다.
    //
    // 두 번 불러도 된다. 처음 한 번만 실제로 등록하고 그 뒤로는 곧바로 true 다.
    // 프레임 루프에서 부르는 함수가 아니다.
    bool RegisterBuiltinComponentProperties2D();
}
