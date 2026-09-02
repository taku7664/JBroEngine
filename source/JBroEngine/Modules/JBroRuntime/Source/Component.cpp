#include <JBro/Runtime/Component.h>

namespace JBro
{
    GameObject* ComponentBase::GetOwner() const  { return m_owner; }
    void        ComponentBase::SetOwner(GameObject* owner) { m_owner = owner; }
    bool        ComponentBase::IsActiveComponent() const   { return m_enabled; }
    void        ComponentBase::SetEnabled(bool enabled)    { m_enabled = enabled; }
}
