#include <JBro/Framework3D/Framework3D.h>

namespace JBro
{
    bool Framework3D::Initialize(const FrameworkContext&) { return true; }
    void Framework3D::Update(float) {}
    void Framework3D::Shutdown() {}

    IFramework* CreateFramework3D()                          { return new Framework3D(); }
    void        DestroyFramework3D(IFramework* framework)    { delete framework; }
}
