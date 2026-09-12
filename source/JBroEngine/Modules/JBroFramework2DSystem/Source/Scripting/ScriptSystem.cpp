#include <JBro/Framework2DSystem/Scripting/ScriptSystem.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Runtime/GameObject.h>

#include <algorithm>

namespace JBro::System
{
    int ScriptSystem::GetExecutionOrder() const
    {
        // 변환이 선 뒤, 렌더 추출 전이다. 스크립트가 그 프레임의 위치를 보고 고칠 수 있어야 한다.
        return 200;
    }

    std::size_t ScriptSystem::GetLastUpdateCount() const
    {
        return m_lastUpdateCount;
    }

    std::size_t ScriptSystem::GetStartedCount() const
    {
        return m_started.Size();
    }

    void ScriptSystem::OnInitialize(Canvas& canvas)
    {
        (void)canvas;
        m_collected.Clear();
        m_ordered.Clear();
        m_started.Clear();
        m_lastUpdateCount = 0;
    }

    std::uint32_t ScriptSystem::MeasureDepth(const GameObject& object)
    {
        std::uint32_t depth = 0;
        const GameObject* walker = object.GetParent();
        // 계층은 순환할 수 없다(SetParent 가 막는다). 그래도 상한을 두어 무한 루프를 만들지 않는다.
        constexpr std::uint32_t MaxDepth = 1u << 16;
        while (walker != nullptr && depth < MaxDepth)
        {
            ++depth;
            walker = walker->GetParent();
        }
        return depth;
    }

    void ScriptSystem::Rebuild(Canvas& canvas)
    {
        canvas.CollectScripts(m_collected);

        // 죽은 스크립트의 id 를 시작 목록에 남겨 두면 목록이 세션 내내 자란다.
        // 비활성이라 이번 순서에서 빠진 것은 살아 있으므로 지우지 않는다 —
        // 다시 켰을 때 두 번 시작해서는 안 되기 때문이다.
        m_started.RemoveAll([this](InstanceId started)
        {
            for (const GameScriptBase* script : m_collected)
            {
                if (script != nullptr && script->GetInstanceId() == started)
                {
                    return false;
                }
            }
            return true;
        });

        m_ordered.Clear();
        for (GameScriptBase* script : m_collected)
        {
            if (script == nullptr || false == script->IsActiveComponent())
            {
                continue;
            }
            GameObject* owner = Internal::CanvasAccess::GetOwner(*script);
            if (owner == nullptr)
            {
                continue;
            }

            ScriptEntry entry;
            entry.script = script;
            entry.layerOrder = owner->GetLayer() != nullptr
                ? owner->GetLayer()->GetOrder()
                : 0;
            entry.depth = MeasureDepth(*owner);
            entry.instanceId = script->GetInstanceId();
            entry.started = m_started.Contains(entry.instanceId);
            m_ordered.Add(entry);
        }

        // 레이어 → 계층 깊이(부모 먼저) → InstanceId(부착 시각) 순이다(D-45).
        std::sort(m_ordered.begin(), m_ordered.end(),
            [](const ScriptEntry& left, const ScriptEntry& right)
        {
            if (left.layerOrder != right.layerOrder)
            {
                return left.layerOrder < right.layerOrder;
            }
            if (left.depth != right.depth)
            {
                return left.depth < right.depth;
            }
            return left.instanceId < right.instanceId;
        });
    }

    void ScriptSystem::OnUpdate(Canvas& canvas, float deltaTime)
    {
        Rebuild(canvas);
        m_lastUpdateCount = 0;

        // 아직 시작하지 않은 것부터 OnCreate·OnStart 를 받는다. 같은 프레임 안에서
        // 그 뒤에 OnUpdate 가 온다 — 시작 훅이 한 프레임 늦게 보이면 안 된다.
        for (ScriptEntry& entry : m_ordered)
        {
            if (entry.started)
            {
                continue;
            }
            entry.script->OnCreate();
            entry.script->OnStart();
            m_started.Add(entry.instanceId);
            entry.started = true;
        }

        for (const ScriptEntry& entry : m_ordered)
        {
            entry.script->OnUpdate(deltaTime);
            ++m_lastUpdateCount;
        }
    }

    void ScriptSystem::OnFixedUpdate(Canvas& canvas, float fixedDeltaTime)
    {
        // 고정 스텝은 이미 세워 둔 순서를 그대로 쓴다. 여기서 목록을 다시 세우면
        // 한 프레임 안의 스텝마다 순서가 흔들린다.
        (void)canvas;
        for (const ScriptEntry& entry : m_ordered)
        {
            if (entry.started)
            {
                entry.script->OnFixedUpdate(fixedDeltaTime);
            }
        }
    }

    void ScriptSystem::OnShutdown(Canvas& canvas)
    {
        // 살아 있는 동안 시작된 것만 OnDestroy 를 받는다. 이미 파괴된 것은 목록에 없다.
        Rebuild(canvas);
        for (const ScriptEntry& entry : m_ordered)
        {
            if (entry.started)
            {
                entry.script->OnDestroy();
            }
        }
        m_collected.Clear();
        m_ordered.Clear();
        m_started.Clear();
        m_lastUpdateCount = 0;
    }
}
