#include <JBro/Runtime/Runtime.h>

namespace JBro
{
    bool RuntimeModule::Initialize(const JMemoryContext&) { return true; }
    void RuntimeModule::Shutdown() {}
}
