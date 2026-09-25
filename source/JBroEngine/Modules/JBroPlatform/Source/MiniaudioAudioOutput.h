#pragma once

#include <JBro/Platform/Platform.h>

namespace JBro::Internal
{
    // miniaudio 의 `ma_device` 로 연 출력 장치다(D-197). Windows 는 WASAPI, 웹은 Web Audio 를 miniaudio 가 고른다.
    // 장치가 없으면 null 이다. 플랫폼 구현 둘이 같이 쓰므로 공개 헤더가 아니라 이 모듈 안에 둔다.
    OwnerPtr<IAudioOutput> CreateMiniaudioOutput(const AudioOutputDesc& desc);
}
