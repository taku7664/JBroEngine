#include <JBro/Framework3D/Service/Animation3DService.h>

#include <JBro/Framework3D/Component/Animator3D.h>
#include <JBro/Framework3D/Internal/SystemContext.h>
#include <JBro/Framework3D/System/IAnimation3DSystem.h>

namespace JBro::Service
{
    Int32 Animation3DService::FindSocket(Handle::Animator3D animator, const char* name) const
    {
        System::IAnimation3DSystem* animation = GetFramework3DSystems().Animation3D;
        const Component::Animator3D* component = Internal::ComponentHandleAccess::ResolveAs<Component::Animator3D>(animator);
        if (animation == nullptr || component == nullptr || name == nullptr)
        {
            return -1;
        }
        return animation->FindSocket(*component, name);
    }

    Bool Animation3DService::GetSocketTransform(Handle::Animator3D animator, Int32 socket, Vector3& position, Quaternion& rotation, Vector3& scale) const
    {
        System::IAnimation3DSystem* animation = GetFramework3DSystems().Animation3D;
        const Component::Animator3D* component = Internal::ComponentHandleAccess::ResolveAs<Component::Animator3D>(animator);
        if (animation == nullptr || component == nullptr)
        {
            return false;
        }
        return animation->GetSocketTransform(*component, socket, position, rotation, scale);
    }
}
