#include <JBro/AudioTypes/Component/AudioSource.h>

#include <JBro/AudioTypes/BuiltinAudioComponents.h>
#include <JBro/AudioTypes/Internal/SystemContext.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Types/Bool.h>

namespace JBro::Component
{
    void AudioSource::OnDetached()
    {
        // 컴포넌트가 떼이면(오브젝트 파괴·캔버스 내리기 포함) 제 보이스도 멈춘다. 시스템이 없으면 보이스도 없다.
        if (System::IAudioSystem* audio = GetAudioSystems().Audio)
        {
            audio->ReleaseSource(*this);
        }
        ComponentBase::OnDetached();
    }

    Bool RegisterBuiltinAudioComponentProperties()
    {
        static const Bool registered = RegisterBuiltinProperties<AudioSource>();
        return registered;
    }
}
