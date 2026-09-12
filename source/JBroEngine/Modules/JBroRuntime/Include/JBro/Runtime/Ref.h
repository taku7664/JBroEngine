#pragma once

#include <JBro/Core/Core.h>

#include <cassert>
#include <cstdint>
#include <type_traits>

namespace JBro
{
    // GameObject는 전용 GameObjectHandle을 사용하고, Ref<T>는 그 외 영속 참조에 사용한다.
    class GameObject;

    struct InstanceHandle                    // 8B. 이번 실행에서의 위치
    {
        std::uint32_t Slot = 0;
        std::uint32_t Gen  = 0;

        constexpr bool IsSet() const
        {
            return Slot != 0 && Gen != 0;
        }
    };

    struct InstanceRef                       // 24B · POD · DLL 경계 통과
    {
        InstanceId     ObjectId    = InvalidInstanceId;
        InstanceId     ComponentId = InvalidInstanceId;
        mutable InstanceHandle Cached;
    };

    // 에셋은 AssetHandle 로 다루고 Canvas 는 스크립트에 노출하지 않으므로 카테고리를 두지 않는다.
    enum class RefCategory : std::uint8_t
    {
        Object,
        Component,
        Script,
    };

    namespace Internal
    {
        struct RefLayoutProbe final
        {
        };

        struct ResolvedInstance
        {
            void* Pointer = nullptr;
            InstanceHandle Handle;
        };

        void* ResolveInstanceByHandle(
            InstanceHandle handle,
            RefCategory category);
        ResolvedInstance ResolveInstanceById(
            InstanceId objectId,
            InstanceId componentId,
            RefCategory category);
        bool PatchInstanceRefCache(
            InstanceRef& reference,
            RefCategory category);
    }

    // T 로부터 카테고리를 뽑는 트레이트. 실제 카테고리 값은 실 객체가 생기는 Stage B~G 에서
    // 각 타입별로 특수화한다. B0 단계에서는 기본값(Object) 로 둔다.
    template<typename T>
    struct RefCategoryOf
    {
        static constexpr RefCategory value = RefCategory::Object;
    };

    template<typename T>
    class Ref : public InstanceRef
    {
    public:
        static_assert(false == std::is_same_v<std::remove_cv_t<T>, GameObject>,
            "GameObject references must use GameObjectHandle, not Ref<GameObject>");

        static constexpr RefCategory Category = RefCategoryOf<T>::value;

        T*   Get() const;                    // 무효면 nullptr
        T*   operator->() const;             // Get() 과 같음 + Debug assert
        T&   operator*()  const;
        bool IsValid() const;
        void Clear();
        explicit operator bool() const;      // "설정됨" 만. 해석하지 않음
        bool operator==(const Ref& rhs) const;
    };

    // Ref<T> 는 InstanceRef 저장부에 어떤 필드도 얹지 않고 virtual 도 갖지 않는다.
    // 리플렉션·직렬화·인스펙터가 T 를 모른 채 저장부 필드에 접근할 수 있어야 하기 때문이다.
    static_assert(sizeof(Ref<Internal::RefLayoutProbe>) == sizeof(InstanceRef),
        "Ref<T> must not add data members beyond InstanceRef");
    static_assert(std::is_standard_layout_v<Ref<Internal::RefLayoutProbe>>,
        "Ref<T> must be standard layout for DLL boundary safety");
    static_assert(std::is_trivially_copyable_v<Ref<Internal::RefLayoutProbe>>,
        "Ref<T> must be trivially copyable for POD boundary crossing");

    template<typename T>
    T* Ref<T>::Get() const
    {
        if (Cached.IsSet())
        {
            void* cached = Internal::ResolveInstanceByHandle(Cached, Category);
            if (cached != nullptr)
            {
                return static_cast<T*>(cached);
            }
        }

        const Internal::ResolvedInstance resolved = Internal::ResolveInstanceById(
            ObjectId,
            ComponentId,
            Category);
        if (resolved.Pointer != nullptr)
        {
            Cached = resolved.Handle;
        }
        return static_cast<T*>(resolved.Pointer);
    }

    template<typename T>
    T* Ref<T>::operator->() const
    {
        T* pointer = Get();
        assert(pointer != nullptr);
        return pointer;
    }

    template<typename T>
    T& Ref<T>::operator*() const
    {
        T* pointer = Get();
        assert(pointer != nullptr);
        return *pointer;
    }

    template<typename T>
    bool Ref<T>::IsValid() const
    {
        return Get() != nullptr;
    }

    template<typename T>
    void Ref<T>::Clear()
    {
        ObjectId = InvalidInstanceId;
        ComponentId = InvalidInstanceId;
        Cached = {};
    }

    template<typename T>
    Ref<T>::operator bool() const
    {
        return ObjectId != InvalidInstanceId;
    }

    template<typename T>
    bool Ref<T>::operator==(const Ref& rhs) const
    {
        return ObjectId == rhs.ObjectId && ComponentId == rhs.ComponentId;
    }
}
