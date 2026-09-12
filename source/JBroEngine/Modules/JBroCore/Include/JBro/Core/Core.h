#pragma once

#include <cstddef>
#include <cstdint>

#include <JBro/Types/Allocator.h>
#include <JBro/Types/SafePtr.h>

namespace JBro
{
    // 문자열·배열 뷰. 인터페이스 경계에서만 쓴다.
    struct JStringView
    {
        const char*   data = nullptr;
        std::uint32_t size = 0;
    };

    template <typename T>
    struct JArrayView
    {
        const T*      data = nullptr;
        std::uint32_t size = 0;
    };

    // JAllocator 는 Types/Allocator.h 에 있다. Array/Table 의 할당기 정책과
    // 같은 헤더에 두어야 정책이 그것을 참조할 수 있다(D-52).

    struct JMemoryContext
    {
        JAllocator persistent;
        JAllocator frame;
        JAllocator scratch;
    };

    JAllocator CreateDefaultAllocator();

    class IModule
    {
    public:
        virtual ~IModule() = default;

        virtual bool Initialize(const JMemoryContext& memory) = 0;
        virtual void Shutdown() = 0;
    };

    // 영속 식별자. 세션이 바뀌어도 같은 값이 같은 오브젝트를 가리킨다.
    // 생성기는 상태를 가지므로 별도 헤더(InstanceIdGenerator.h)에 있다.
    using InstanceId = std::uint64_t;
    inline constexpr InstanceId InvalidInstanceId = 0;
}
