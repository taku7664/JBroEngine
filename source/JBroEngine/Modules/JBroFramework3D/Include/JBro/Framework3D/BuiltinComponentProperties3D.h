#pragma once

#include <JBro/AudioTypes/BuiltinAudioComponents.h>
#include <JBro/Framework3D/Component/AudioListener3D.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Physics3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Reflection/PropertyRegistry.h>

namespace JBro::Component
{
    // 3D 빌트인 컴포넌트의 프로퍼티 표를 빌트인 보관함에 넣는다(D-56 계획서 §8.4).
    //
    // 2D 쪽은 같은 함수가 `.cpp` 에 있는데 여기는 헤더에 있다. `JBroFramework3D` 는
    // 소스가 없어서 `Utility` 로 두었고(`58522de`), 이 함수 하나 때문에 다시
    // 라이브러리로 되돌리는 것은 얻는 것보다 잃는 것이 많다. inline 함수의 정적 지역
    // 변수는 번역 단위마다 따로 생기지 않으므로 "한 번만 등록" 계약은 그대로다.
    //
    // 두 번 불러도 된다. 프레임 루프에서 부르는 함수가 아니다.
    inline bool RegisterBuiltinComponentProperties3D()
    {
        static const bool registered = []
        {
            // && 로 엮지 않는다. 하나가 실패하면 뒤의 것이 아예 등록되지 않고,
            // 그러면 첫 실패 하나가 인스펙터에서 여러 컴포넌트를 통째로 지운다.
            bool all = true;
            all = RegisterBuiltinProperties<Transform3D>()    && all;
            all = RegisterBuiltinProperties<Camera3D>()       && all;
            all = RegisterBuiltinProperties<MeshRenderer3D>() && all;
            all = RegisterBuiltinProperties<Rigidbody3D>()    && all;
            all = RegisterBuiltinProperties<Collider3D>()     && all;
            all = RegisterBuiltinProperties<AudioListener3D>() && all;
            // 소스는 차원과 무관한 모듈의 것이다(D-197). 두 프레임워크가 함께 부르고 한 번만 등록된다.
            all = RegisterBuiltinAudioComponentProperties()   && all;
            return all;
        }();
        return registered;
    }
}
