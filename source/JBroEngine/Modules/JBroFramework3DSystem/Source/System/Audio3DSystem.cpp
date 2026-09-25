#include <JBro/Framework3DSystem/System/Audio3DSystem.h>

#include <JBro/Audio/AudioSystem.h>
#include <JBro/AudioTypes/Component/AudioSource.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Core/Log.h>
#include <JBro/Framework3D/Component/AudioListener3D.h>
#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3D/Math3D.h>

#include <cmath>

namespace JBro::System
{
    namespace
    {
        const Component::Transform3D* WorldOf(Canvas& canvas, const ComponentBase& component)
        {
            GameObject* owner = Internal::CanvasAccess::GetOwner(component);
            const auto* transform = canvas.FindComponentRaw<Component::Transform3D>(owner);
            if (transform == nullptr || false == transform->IsActiveComponent() || false == transform->worldValid)
            {
                return nullptr;
            }
            return transform;
        }

        void Store(const Vec3& value, float out[3])
        {
            out[0] = value.x;
            out[1] = value.y;
            out[2] = value.z;
        }
    }

    Audio3DSystem::Audio3DSystem(AudioSystem& audio)
        : m_audio(audio)
    {
    }

    int Audio3DSystem::GetExecutionOrder() const
    {
        return ExecutionOrder;
    }

    void Audio3DSystem::ReleaseAllSources(Canvas& canvas)
    {
        canvas.ForEach<Component::AudioSource>([&](Component::AudioSource& source)
        {
            m_audio.ReleaseSource(source);
        });
    }

    void Audio3DSystem::OnUpdate(Canvas& canvas, float deltaTime)
    {
        // 듣는 자리: 첫 활성 리스너, 없으면 게임 카메라(`primary` 먼저), 그것도 없으면 원점에서 -Z 를 본다.
        const Component::Transform3D* listener = nullptr;
        std::uint32_t listeners = 0;
        canvas.ForEach<Component::AudioListener3D>([&](Component::AudioListener3D& candidate)
        {
            if (false == candidate.IsActiveComponent())
            {
                return;
            }
            ++listeners;
            if (listener == nullptr)
            {
                listener = WorldOf(canvas, candidate);
            }
        });
        if (listeners > 1 && false == m_warnedListeners)
        {
            m_warnedListeners = true;
            Log::Write(LogLevel::Warning, "audio", "%u audio listeners are active - the first one is used", listeners);
        }
        if (listener == nullptr)
        {
            bool primaryFound = false;
            canvas.ForEach<Component::Camera3D>([&](Component::Camera3D& camera)
            {
                if (primaryFound || false == camera.IsActiveComponent())
                {
                    return;
                }
                if (const Component::Transform3D* world = WorldOf(canvas, camera))
                {
                    if (listener == nullptr || camera.primary)
                    {
                        listener = world;
                        primaryFound = camera.primary;
                    }
                }
            });
        }
        float position[3] = {0.0f, 0.0f, 0.0f};
        float forward[3] = {0.0f, 0.0f, -1.0f};
        float up[3] = {0.0f, 1.0f, 0.0f};
        if (listener != nullptr)
        {
            Store(listener->worldPosition, position);
            Store(Rotate(listener->worldRotation, Vec3{0.0f, 0.0f, -1.0f}), forward);
            Store(Rotate(listener->worldRotation, Vec3{0.0f, 1.0f, 0.0f}), up);
        }
        m_audio.SetListener(position, forward, up, 0.0f, deltaTime);

        canvas.ForEach<Component::AudioSource>([&](Component::AudioSource& source)
        {
            float sourcePosition[3] = {position[0], position[1], position[2]};
            // 변환이 없는 소스(배경음)는 듣는 자리에 있는 것으로 본다.
            if (const Component::Transform3D* world = WorldOf(canvas, source))
            {
                Store(world->worldPosition, sourcePosition);
            }
            m_audio.UpdateSource(source, source.IsActiveComponent(), sourcePosition, deltaTime);
        });
    }
}
