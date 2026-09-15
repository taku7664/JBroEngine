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
        m_scriptKeys.Clear();
        m_roots.Clear();
        m_walkStack.Clear();

        m_ordered.Clear();
        m_started.Clear();
        m_lastUpdateCount = 0;
        m_rebuildCount = 0;
        m_builtRevision = 0;
    }

    std::size_t ScriptSystem::GetRebuildCount() const
    {
        return m_rebuildCount;
    }

    bool ScriptSystem::IsScript(const ComponentBase* component) const
    {
        if (component == nullptr || m_scriptKeys.IsEmpty())
        {
            return false;
        }
        std::size_t low = 0;
        std::size_t high = m_scriptKeys.Size();
        while (low < high)
        {
            const std::size_t middle = low + (high - low) / 2;
            if (m_scriptKeys[middle] < component)
            {
                low = middle + 1;
            }
            else
            {
                high = middle;
            }
        }
        return low < m_scriptKeys.Size() && m_scriptKeys[low] == component;
    }

    void ScriptSystem::AppendScripts(GameObject& object)
    {
        // **오브젝트 안의 차례는 컴포넌트 배열 자리 그대로다**(D-45). 여기서 다시
        // 정렬하면 떼었다 되돌린 컴포넌트가 제 자리를 잃는다.
        for (const ComponentSlot& slot : object.GetComponents())
        {
            ComponentBase* component = slot.reference.TryGet();
            if (false == IsScript(component))
            {
                continue;
            }
            ScriptEntry entry;
            entry.script     = static_cast<GameScriptBase*>(component);
            entry.instanceId = component->GetInstanceId();
            entry.started    = m_started.Contains(entry.instanceId);
            m_ordered.Add(entry);
        }
    }

    void ScriptSystem::EnsureOrder(Canvas& canvas)
    {
        const std::uint64_t revision = canvas.GetScriptOrderRevision();
        if (revision == m_builtRevision)
        {
            return;
        }
        Rebuild(canvas);
        m_builtRevision = revision;
    }

    void ScriptSystem::Rebuild(Canvas& canvas)
    {
        ++m_rebuildCount;
        canvas.CollectScripts(m_collected);

        // **스크립트가 하나도 없으면 트리를 걷지 않는다.** 걸을 것이 없는데 루트를 모으면
        // 스크립트를 쓰지 않는 캔버스도 오브젝트 수만큼 배열을 채우게 되고, 그 첫 채움이
        // 정상 프레임의 힙 할당이 된다(§9). 여기서 멈추는 것이 구 엔진과도 같다.
        if (m_collected.IsEmpty())
        {
            m_scriptKeys.Clear();
            m_ordered.Clear();
            m_started.Clear();
            return;
        }

        // 죽은 스크립트의 id 를 시작 목록에 남겨 두면 목록이 세션 내내 자란다.
        // 비활성이라 이번에 안 도는 것은 살아 있으므로 지우지 않는다 - 다시 켰을 때
        // 두 번 시작해서는 안 되기 때문이다. 훑는 비용은 재구축 때만 든다.
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

        m_scriptKeys.Clear();
        for (GameScriptBase* script : m_collected)
        {
            if (script != nullptr)
            {
                m_scriptKeys.Add(static_cast<const ComponentBase*>(script));
            }
        }
        std::sort(m_scriptKeys.begin(), m_scriptKeys.end());

        // 루트를 (레이어 합성 순서, 생성 순서) 로 줄 세운다. 구 엔진과 같은 키다.
        m_roots.Clear();
        canvas.ForEachObject([this](GameObject& object)
        {
            if (object.GetParent() == nullptr)
            {
                m_roots.Add(&object);
            }
        });
        std::sort(m_roots.begin(), m_roots.end(),
            [](const GameObject* left, const GameObject* right)
        {
            const Layer* leftLayer  = left->GetLayer();
            const Layer* rightLayer = right->GetLayer();
            const LayerOrder leftOrder  = leftLayer  != nullptr ? leftLayer->GetOrder()  : 0;
            const LayerOrder rightOrder = rightLayer != nullptr ? rightLayer->GetOrder() : 0;
            if (leftOrder != rightOrder)
            {
                return leftOrder < rightOrder;
            }
            return left->GetInstanceId() < right->GetInstanceId();
        });

        // 루트마다 서브트리를 통째로 내려간다. 형제 서브트리는 섞이지 않는다.
        //
        // 재귀가 아니라 스택으로 도는 이유는 깊은 계층에서 호출 스택을 쓰지 않기 위해서다.
        // 꺼내는 차례가 곧 방문 차례이므로 자식은 **거꾸로** 넣는다.
        m_ordered.Clear();
        m_walkStack.Clear();
        for (std::size_t at = m_roots.Size(); at > 0; --at)
        {
            if (m_roots[at - 1] != nullptr)
            {
                m_walkStack.Add(m_roots[at - 1]);
            }
        }

        while (false == m_walkStack.IsEmpty())
        {
            GameObject* object = m_walkStack.Last();
            m_walkStack.Resize(m_walkStack.Size() - 1);
            AppendScripts(*object);

            // **자식은 배열 자리 그대로 내려간다.** 구 엔진은 여기서 `GetCreationOrder` 로
            // 정렬했는데, 그 엔진에는 형제 자리를 바꾸는 길이 없어 배열 자리가 곧 생성
            // 순서였다. 이 엔진에는 `SetChildIndex` 가 있고 계층에서 끌어 옮기면 그것이
            // 움직인다(D-84) - 정렬해 버리면 사용자가 옮긴 자리를 실행 순서가 무시한다.
            // 아무도 옮기지 않았으면 배열 자리가 생성 순서이므로 구 엔진과 같은 결과다.
            const Array<SafePtr<GameObject>>& children = object->GetChildren();
            for (std::size_t at = children.Size(); at > 0; --at)
            {
                if (GameObject* child = children[at - 1].TryGet())
                {
                    m_walkStack.Add(child);
                }
            }
        }
    }

    void ScriptSystem::OnUpdate(Canvas& canvas, float deltaTime)
    {
        EnsureOrder(canvas);
        m_lastUpdateCount = 0;

        // **훅을 부르는 동안 순회를 잠근다**(ProjectRule §8, D-45). 스크립트가 훅 안에서
        // 오브젝트를 지우면 그 파괴는 큐로 가고, `Framework2D::Update` 가 시스템 갱신을
        // 마친 뒤 안전 지점에서 흘린다. 잠그지 않으면 `Canvas::DestroyObject` 가 그 자리에서
        // 파괴하고, `m_ordered` 에 남은 포인터가 파괴자가 지나간 객체를 가리킨다 -
        // 풀 슬롯이 남아 있어 크래시 없이 조용히 돈다.
        Canvas::IterationGuard guard(canvas);

        // 아직 시작하지 않은 것부터 OnCreate·OnStart 를 받는다. 같은 프레임 안에서
        // 그 뒤에 OnUpdate 가 온다 — 시작 훅이 한 프레임 늦게 보이면 안 된다.
        //
        // **활성 판정은 여기서 한다.** 목록을 세울 때 걸러 내면 스크립트를 껐다 켜는 것이
        // 목록을 다시 세워야 하는 일이 되고, 그러면 더티 플래그의 트리거가 D-45 가 댄
        // 이름들보다 넓어진다. 목록에는 살아 있는 것이 전부 들어 있고 도는 것만 고른다.
        for (ScriptEntry& entry : m_ordered)
        {
            if (entry.started || false == entry.script->IsActiveComponent())
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
            if (false == entry.script->IsActiveComponent())
            {
                continue;
            }
            entry.script->OnUpdate(deltaTime);
            ++m_lastUpdateCount;
        }
    }

    void ScriptSystem::OnFixedUpdate(Canvas& canvas, float fixedDeltaTime)
    {
        // 고정 스텝은 세워 둔 순서를 그대로 쓴다. 다만 **헌 목록은 쓰지 않는다** -
        // Framework 가 스텝마다 파괴를 흘리므로(D-45), 앞 스텝에서 지운 스크립트를
        // 다음 스텝이 죽은 포인터로 들고 있게 된다. 바뀐 것이 없으면 비교 한 번으로 끝난다.
        EnsureOrder(canvas);
        Canvas::IterationGuard guard(canvas);
        for (const ScriptEntry& entry : m_ordered)
        {
            if (entry.started && entry.script->IsActiveComponent())
            {
                entry.script->OnFixedUpdate(fixedDeltaTime);
            }
        }
    }

    void ScriptSystem::OnShutdown(Canvas& canvas)
    {
        // 살아 있는 동안 시작된 것만 OnDestroy 를 받는다. 이미 파괴된 것은 목록에 없다.
        EnsureOrder(canvas);
        {
            Canvas::IterationGuard guard(canvas);
            for (const ScriptEntry& entry : m_ordered)
            {
                if (entry.started)
                {
                    entry.script->OnDestroy();
                }
            }
        }
        // 가드가 풀린 뒤 한 번 흘린다. `OnDestroy` 안에서 무언가를 더 지웠다면 그 요청이
        // 큐에 남아 있고, 여기서 비우지 않으면 아무도 비우지 않는다.
        canvas.FlushPendingDestroy();
        m_collected.Clear();
        m_scriptKeys.Clear();
        m_roots.Clear();
        m_walkStack.Clear();

        m_ordered.Clear();
        m_started.Clear();
        m_lastUpdateCount = 0;
        m_builtRevision = 0;
    }
}
