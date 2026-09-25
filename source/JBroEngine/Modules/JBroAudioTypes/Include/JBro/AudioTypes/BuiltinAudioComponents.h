#pragma once

namespace JBro::Component
{
    // 차원과 무관한 오디오 컴포넌트(`AudioSource`)의 프로퍼티를 이름으로 내놓는다. 여러 번 불러도 한 번만 등록한다.
    // 두 프레임워크가 각자 자기 내장 컴포넌트와 함께 부른다.
    bool RegisterBuiltinAudioComponentProperties();
}
