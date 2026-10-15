#pragma once
#include <JBro/Types/Bool.h>
#include <JBro/Types/String.h>
#include <string_view>

namespace JBro
{
    // Bounded asynchronous stdin/stdout/stderr queues. Destruction owns process-tree shutdown.
    class IProcessChannel
    {
    public:
        virtual ~IProcessChannel() = default;
        virtual Bool Write(std::string_view bytes) = 0;
        virtual Bool Read(String& bytes, Bool standardError = false) = 0;
        virtual Bool IsRunning() const = 0;
    };
}
