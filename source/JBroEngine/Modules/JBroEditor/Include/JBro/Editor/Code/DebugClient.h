#pragma once
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>

namespace JBro { class IPlatform; }
namespace JBro::Code
{
    enum class DebugState { Idle, Starting, Configuring, Ready, Stopped, Stopping, Failed };
    struct Breakpoint
    {
        // DAP source lines are one based. Unverified locations remain pending.
        UInt32 requestedLine = 0;
        UInt32 actualLine = 0;
        Bool verified = false;
        String message;
    };
    class DebugClient final
    {
    public:
        explicit DebugClient(IPlatform& platform);
        ~DebugClient();
        Bool Start(const String& executable, UInt32 processId);
        // Poll until Idle (or Failed) so the adapter can clear engine breakpoints.
        void Stop();
        void Poll(Float elapsed);
        void SetBreakpoints(const String& path, const Array<UInt32>& lines);
        // Ready follows configurationDone, after all resolved locations were acknowledged.
        void Configure();
        void Continue();
        DebugState GetState() const;
        const Array<Breakpoint>* GetBreakpoints(const String& path) const;
        const String& GetError() const;
        UInt64 GetRevision() const;
    private:
        struct State;
        OwnerPtr<State> m_state;
    };
}
