#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace JBro::Engine
{
    using Entity = std::uint32_t;
    inline constexpr Entity InvalidEntity = std::numeric_limits<Entity>::max();

    struct JStringView
    {
        const char* data = nullptr;
        std::uint32_t size = 0;
    };

    template <typename T>
    struct JArrayView
    {
        const T* data = nullptr;
        std::uint32_t size = 0;
    };

    struct JAllocator
    {
        void* userData = nullptr;
        void* (*allocate)(void* userData, std::size_t size, std::size_t alignment) = nullptr;
        void (*free)(void* userData, void* memory) = nullptr;
        void* (*reallocate)(void* userData, void* memory, std::size_t newSize, std::size_t alignment) = nullptr;
    };

    struct JMemoryContext
    {
        JAllocator persistent;
        JAllocator frame;
        JAllocator scratch;
    };

    JAllocator CreateDefaultAllocator();

    template <typename T>
    using OwnerPtr = std::unique_ptr<T>;

    template <typename T, typename... Args>
    OwnerPtr<T> MakeOwnerPtr(Args&&... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    class IModule
    {
    public:
        virtual ~IModule() = default;

        virtual bool Initialize(const JMemoryContext& memory) = 0;
        virtual void Shutdown() = 0;
    };

    struct ComponentTypeId
    {
        std::uint64_t value = 0;

        bool IsValid() const;
        bool operator==(const ComponentTypeId& other) const;
    };

    using TypeId = std::uint64_t;

    class CComponentRegistry;
    using ComponentRegistry = CComponentRegistry;

}
