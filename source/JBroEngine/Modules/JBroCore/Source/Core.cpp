#include <JBro/Core/Core.h>

#include <algorithm>
#include <cstdlib>
#include <cstdint>

namespace JBro
{
    namespace
    {
        void* DefaultAllocate(void*, std::size_t size, std::size_t alignment)
        {
            if (size == 0) return nullptr;

            alignment = std::max(alignment, alignof(void*));
            const std::size_t totalSize = size + alignment - 1 + sizeof(void*);
            void* raw = std::malloc(totalSize);
            if (raw == nullptr) return nullptr;

            const std::uintptr_t begin   = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void*);
            const std::uintptr_t aligned = (begin + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment) - 1);
            void* result = reinterpret_cast<void*>(aligned);
            reinterpret_cast<void**>(result)[-1] = raw;
            return result;
        }

        void DefaultFree(void*, void* memory)
        {
            if (memory != nullptr) std::free(reinterpret_cast<void**>(memory)[-1]);
        }
    }

    JAllocator CreateDefaultAllocator()
    {
        return JAllocator{ nullptr, &DefaultAllocate, &DefaultFree, nullptr };
    }
}
