#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/ObjectPool.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Framework2D/Canvas/Layer.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Table.h>

#include <cstddef>

namespace JBro
{
    class ComponentBase;

    // §8. 씬의 최상위 단위. 오브젝트 풀과 타입별 컴포넌트 풀, 레이어를 직접 소유한다.
    // CWorld 를 감싸지 않는다 — 캔버스가 곧 캔버스이다.
    class Canvas final
    {
    public:
        explicit Canvas(JAllocator allocator);
        ~Canvas();

        Canvas(const Canvas&)            = delete;
        Canvas& operator=(const Canvas&) = delete;

        // 오브젝트
        GameObject* CreateObject(const char* name = nullptr);
        bool        DestroyObject(GameObject* object);
        std::size_t GetObjectCount() const;

        template <typename Fn>
        void ForEachObject(Fn&& function);

        // 레이어
        Layer&      CreateLayer(const char* name = nullptr);
        bool        DestroyLayer(LayerIndex layer);
        bool        MoveLayer(LayerIndex layer, std::size_t newIndex);
        Layer*      FindLayer(LayerIndex layer);
        std::size_t GetLayerCount() const;
        Layer*      GetLayerAt(std::size_t index);
        LayerIndex  GetDefaultLayer() const;

        // 타입별 컴포넌트 풀 접근. 인터페이스만 두고 구현은 F1~G4 에서 채운다.
        template <typename T>
        T*   AttachComponent(GameObject* owner);
        template <typename T>
        bool DetachComponent(GameObject* owner, T* component);
        template <typename T>
        T*   GetComponent(GameObject* owner);
        template <typename T, typename Fn>
        void ForEach(Fn&& function);

    private:
        struct IComponentBucket
        {
            virtual ~IComponentBucket() = default;
            virtual void DestroyAllOnObject(GameObject* owner) = 0;
        };

        JAllocator                                     m_allocator;
        OwnerPtr<TObjectPool<GameObject>>              m_objects;
        Array<OwnerPtr<Layer>>                         m_layers;
        LayerIndex                                     m_defaultLayer = InvalidLayerIndex;
        LayerIndex                                     m_nextLayer    = 0;
        Table<ComponentTypeId, OwnerPtr<IComponentBucket>> m_componentBuckets;
    };
}
