#pragma once

#include <JBro/Runtime/IObjectSystem.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

namespace JBro
{
    class Canvas;

    namespace Object
    {
        class GameObject;
    }

    // 스크립트의 오브젝트 서비스가 캔버스에 닿는 구현이다(D-301 (7)). 캔버스를 가진 프레임워크가 하나 들고, 스크립트 컨텍스트를 묶을 때
    // `SystemContext::Objects` 에 꽂는다. 차원은 모른다 - 만들 때 붙일 기본 컴포넌트의 타입 이름만 받는다(2D 는 `Transform2D`).
    //
    // 찾는 차례는 레이어 차례 → 그 레이어의 뿌리 차례 → 자식 차례(깊이 우선)다. 계층 패널에 보이는 차례와 같다.
    // 파괴를 요청한 오브젝트는 찾지 않는다.
    class CanvasObjectSystem final : public System::IObjectSystem
    {
    public:
        CanvasObjectSystem() = default;
        CanvasObjectSystem(const CanvasObjectSystem&) = delete;
        CanvasObjectSystem& operator=(const CanvasObjectSystem&) = delete;

        // `canvas` 가 null 이면 모든 질의가 빈 값이다. `defaultComponent` 는 `ComponentRegistry` 의 타입 이름이고 비워도 된다.
        void Bind(Canvas* canvas, const char* defaultComponent);

        Handle::GameObject Create(const char* name, const Handle::GameObject& parent, UInt32 layer) override;
        Handle::GameObject Find(NameId name, Bool byTag) override;
        void FindAll(NameId name, Bool byTag, Array<Handle::GameObject>& results) override;
        Handle::GameObject FindRoot(UInt32 layer, NameId name) override;
        UInt32 FindLayer(NameId name) override;
        Bool IsLayerAlive(UInt32 layer) override;
        const char* GetLayerName(UInt32 layer) override;
        InstanceRef AddComponent(const Handle::GameObject& object, ComponentTypeId type) override;
        InstanceRef AddScript(const Handle::GameObject& object, const char* typeName) override;

    private:
        // 차례대로 돌며 `visit` 가 참을 돌려주면 멈춘다.
        template<typename Visit>
        void Walk(Visit&& visit);

        Canvas* m_canvas = nullptr;
        String m_defaultComponent;
        Array<Object::GameObject*> m_roots;
    };
}
