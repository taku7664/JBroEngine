#include <JBro/Framework3DSystem/System/BoneAttachment3DSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Core/Log.h>
#include <JBro/Framework3D/Component/Animator3D.h>
#include <JBro/Framework3D/Component/BoneAttachment3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3DSystem/Math3DMatrix.h>
#include <JBro/Framework3DSystem/System/Transform3DSystem.h>
#include <JBro/Runtime/GameObject.h>

#include <string_view>
#include <utility>

namespace JBro::System
{
    Int32 BoneAttachment3DSystem::GetExecutionOrder() const
    {
        return ExecutionOrder;
    }

    void BoneAttachment3DSystem::SetAnimation(Animation3DSystem* animation)
    {
        m_animation = animation;
    }

    const BoneAttachment3DSystem::State* BoneAttachment3DSystem::FindState(InstanceId attachment) const
    {
        const UInt32* index = m_stateOf.Find(attachment);
        return index != nullptr ? &m_states[index->Get()] : nullptr;
    }

    void BoneAttachment3DSystem::OnUpdate(Canvas& canvas, Float deltaTime)
    {
        (void)deltaTime;
        if (m_animation == nullptr)
        {
            return;
        }
        ++m_stamp;
        canvas.ForEach<Component::BoneAttachment3D>([&](Component::BoneAttachment3D& attachment)
        {
            if (false == attachment.IsActiveComponent())
            {
                return;
            }
            Object::GameObject* owner = Internal::CanvasAccess::GetOwner(attachment);
            Component::Transform3D* transform = owner != nullptr ? canvas.FindComponentRaw<Component::Transform3D>(owner) : nullptr;
            if (transform == nullptr || false == transform->IsActiveComponent() || false == transform->worldValid)
            {
                return;
            }
            const InstanceId instance = attachment.GetInstanceId();
            State* state = nullptr;
            if (const UInt32* index = m_stateOf.Find(instance))
            {
                state = &m_states[index->Get()];
            }
            else
            {
                m_stateOf.TryAdd(instance, static_cast<UInt32>(m_states.Size()));
                m_states.Add(State{});
                state = &m_states[m_states.Size() - 1];
                state->instance = instance;
            }
            state->stamp = m_stamp;

            // 가장 가까운 조상의 애니메이터다(자기 것은 아니다 - 제 뼈에 붙으면 제 자리가 돌고 돈다).
            const Component::Animator3D* animator = nullptr;
            for (Object::GameObject* above = owner->GetParent(); above != nullptr && animator == nullptr; above = above->GetParent())
            {
                const auto* found = canvas.FindComponentRaw<Component::Animator3D>(above);
                animator = found != nullptr && found->IsActiveComponent() ? found : nullptr;
            }
            if (animator == nullptr)
            {
                state->animator = InvalidInstanceId;
                state->found = false;
                return;
            }
            // 이름이나 소켓 출처가 바뀌었을 때만 푼다.
            const UInt32 nameRevision = TextStore::Get().GetRevision(attachment.socket);
            const UInt32 sourceRevision = m_animation->GetSocketSourceRevision(canvas, *animator);
            if (sourceRevision == 0)
            {
                return;
            }
            if (state->animator != animator->GetInstanceId() || state->name.index != attachment.socket.index
                || state->name.generation != attachment.socket.generation || state->nameRevision != nameRevision
                || state->sourceRevision != sourceRevision)
            {
                state->animator = animator->GetInstanceId();
                state->name = attachment.socket;
                state->nameRevision = nameRevision;
                state->sourceRevision = sourceRevision;
                const ArrayView<const char> text = TextStore::Get().GetText(attachment.socket);
                const std::string_view name(text.Data() != nullptr ? text.Data() : "", text.Size());
                state->found = m_animation->ResolveSocket(canvas, *animator, name, state->socket);
                if (false == state->found && false == name.empty())
                {
                    // 이름이 바뀌었을 때만 오는 자리라 글자를 떠도 된다.
                    const String shown(name.data(), name.size());
                    Log::Write(LogLevel::Warning, "animation", "%s: no socket or bone named %s under its animator", owner->GetName(), shown.c_str());
                }
            }
            if (false == state->found)
            {
                return;
            }
            Vector3 socketPosition;
            Quaternion socketRotation;
            Vector3 socketScale;
            if (false == m_animation->ComputeSocketWorld(canvas, *animator, state->socket, socketPosition, socketRotation, socketScale))
            {
                return;
            }
            // 제 로컬 값은 소켓에서의 덧 오프셋이다.
            ComposeTransform3D(socketPosition, socketRotation, socketScale, transform->position, transform->rotation, transform->scale,
                transform->worldPosition, transform->worldRotation, transform->worldScale);
            Transform3DSystem::PropagateChildren(canvas, *owner, *transform);
        });
        // 사라진 붙이기의 상태를 치운다.
        for (std::size_t index = m_states.Size(); index > 0; --index)
        {
            if (m_states[index - 1].stamp == m_stamp)
            {
                continue;
            }
            const std::size_t last = m_states.Size() - 1;
            m_stateOf.Remove(m_states[index - 1].instance);
            if (index - 1 != last)
            {
                m_states[index - 1] = std::move(m_states[last]);
                if (UInt32* moved = m_stateOf.Find(m_states[index - 1].instance))
                {
                    *moved = static_cast<UInt32>(index - 1);
                }
            }
            m_states.RemoveAt(last);
        }
    }

    void BoneAttachment3DSystem::OnShutdown(Canvas& canvas)
    {
        (void)canvas;
        m_states.Clear();
        m_stateOf.Clear();
    }
}
