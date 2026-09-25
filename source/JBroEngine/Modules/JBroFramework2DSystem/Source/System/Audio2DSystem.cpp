#include <JBro/Framework2DSystem/System/Audio2DSystem.h>

#include <JBro/Audio/AudioSystem.h>
#include <JBro/AudioTypes/Component/AudioSource.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Core/Log.h>
#include <JBro/Framework2D/Component/AudioListener2D.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>

#include <cmath>

namespace JBro::System
{
    namespace
    {
        // 월드 위치다. 변환이 없거나 꺼져 있으면 거짓이다.
        bool WorldPositionOf(Canvas& canvas, const ComponentBase& component, float out[3])
        {
            GameObject* owner = Internal::CanvasAccess::GetOwner(component);
            const auto* transform = canvas.FindComponentRaw<Component::Transform2D>(owner);
            if (transform == nullptr || false == transform->IsActiveComponent() || false == transform->worldValid)
            {
                return false;
            }
            out[0] = transform->worldPosition.x;
            out[1] = transform->worldPosition.y;
            out[2] = 0.0f;
            return std::isfinite(out[0]) && std::isfinite(out[1]);
        }
    }

    Audio2DSystem::Audio2DSystem(AudioSystem& audio)
        : m_audio(audio)
    {
    }

    int Audio2DSystem::GetExecutionOrder() const
    {
        return ExecutionOrder;
    }

    void Audio2DSystem::ReleaseAllSources(Canvas& canvas)
    {
        canvas.ForEach<Component::AudioSource>([&](Component::AudioSource& source)
        {
            m_audio.ReleaseSource(source);
        });
    }

    void Audio2DSystem::OnUpdate(Canvas& canvas, float deltaTime)
    {
        // 듣는 자리: 첫 활성 리스너, 없으면 게임 카메라(`primary` 먼저), 그것도 없으면 원점이다.
        float listener[3] = {0.0f, 0.0f, 0.0f};
        float panDistance = 5.0f;
        bool found = false;
        std::uint32_t listeners = 0;
        canvas.ForEach<Component::AudioListener2D>([&](Component::AudioListener2D& candidate)
        {
            if (false == candidate.IsActiveComponent())
            {
                return;
            }
            ++listeners;
            float position[3];
            if (false == found && WorldPositionOf(canvas, candidate, position))
            {
                listener[0] = position[0];
                listener[1] = position[1];
                panDistance = candidate.panDistance;
                found = true;
            }
        });
        if (listeners > 1 && false == m_warnedListeners)
        {
            m_warnedListeners = true;
            Log::Write(LogLevel::Warning, "audio", "%u audio listeners are active - the first one is used", listeners);
        }
        if (false == found)
        {
            bool primaryFound = false;
            canvas.ForEach<Component::Camera2D>([&](Component::Camera2D& camera)
            {
                if (primaryFound || false == camera.IsActiveComponent())
                {
                    return;
                }
                float position[3];
                if (WorldPositionOf(canvas, camera, position) && (false == found || camera.primary))
                {
                    listener[0] = position[0];
                    listener[1] = position[1];
                    found = true;
                    primaryFound = camera.primary;
                }
            });
        }
        // 2D 는 화면을 보는 쪽(-Z)이 앞이고 +Y 가 위다. 소스는 `panDistance` 깊이 앞에 놓인다(`AudioSystem::SetListener`).
        const float forward[3] = {0.0f, 0.0f, -1.0f};
        const float up[3] = {0.0f, 1.0f, 0.0f};
        m_audio.SetListener(listener, forward, up, std::isfinite(panDistance) && panDistance > 0.0f ? panDistance : 5.0f,
            deltaTime);

        canvas.ForEach<Component::AudioSource>([&](Component::AudioSource& source)
        {
            float position[3] = {listener[0], listener[1], 0.0f};
            const bool placed = WorldPositionOf(canvas, source, position);
            // 변환이 없는 소스도 운다 - 배경음은 대개 변환 없이 붙는다. 그때는 듣는 자리에 있는 것으로 본다.
            if (false == placed)
            {
                position[0] = listener[0];
                position[1] = listener[1];
            }
            m_audio.UpdateSource(source, source.IsActiveComponent(), position, deltaTime);
        });
    }
}
