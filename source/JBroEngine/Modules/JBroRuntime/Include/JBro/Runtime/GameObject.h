#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Types/Array.h>

#include <cstdint>

namespace JBro
{
    class ComponentBase;
    class Canvas;

    // §8. 실 객체. TObjectPool 이 소유하며 한 번 발급된 포인터는 이동하지 않는다.
    // 컴포넌트 논리 소유, 부모·자식 계층, 레이어 소속을 멤버로 갖는다.
    class GameObject
    {
    public:
        GameObject();
        ~GameObject();

        GameObject(const GameObject&)            = delete;
        GameObject& operator=(const GameObject&) = delete;

        InstanceId     GetInstanceId() const;
        InstanceHandle GetHandle()     const;

        Canvas* GetCanvas() const;
        void    SetCanvas(Canvas* canvas);

        // 계층
        GameObject*                GetParent() const;
        void                       SetParent(GameObject* parent);
        const Array<GameObject*>&  GetChildren() const;

        // 레이어 소속. O(1).
        std::uint32_t GetLayerIndex() const;
        void          SetLayerIndex(std::uint32_t layerIndex);

        // 활성 상태
        bool IsActiveSelf()        const;
        bool IsActiveInHierarchy() const;
        void SetActive(bool active);

        // 태그·플래그(B10)
        const char*   GetTag()   const;
        void          SetTag(const char* tag);
        std::uint32_t GetFlags() const;
        void          SetFlags(std::uint32_t flags);

        // 컴포넌트 접근. 실체 저장은 Canvas 의 타입별 풀에 있고 여기서는 논리 소유만 기록한다.
        const Array<ComponentBase*>& GetComponents() const;
        void  AttachComponent(ComponentBase* component);
        bool  DetachComponent(ComponentBase* component);

    private:
        InstanceId              m_instanceId  = InvalidInstanceId;
        InstanceHandle          m_handle;
        Canvas*                 m_canvas      = nullptr;
        GameObject*             m_parent      = nullptr;
        Array<GameObject*>      m_children;
        Array<ComponentBase*>   m_components;
        std::uint32_t           m_layerIndex  = 0;
        std::uint32_t           m_flags       = 0;
        bool                    m_active      = true;
        const char*             m_tag         = nullptr;
    };
}
