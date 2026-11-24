#include <JBro/Framework3D/Handle/BoneAttachment3D.h>

#include <JBro/Framework3D/Component/BoneAttachment3D.h>

#include <cstring>

namespace JBro::Handle
{
    Component::BoneAttachment3D* BoneAttachment3D::Resolve(const char* operation) const
    {
        return static_cast<Component::BoneAttachment3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    void BoneAttachment3D::SetSocket(const char* name)
    {
        Component::BoneAttachment3D* component = Resolve("SetSocket");
        if (component == nullptr)
        {
            return;
        }
        // 스크립트 DLL 에도 호스트의 저장소가 묶여 있다(Text3D 와 같다).
        TextStore::Get().Assign(component->socket, name != nullptr ? name : "", name != nullptr ? std::strlen(name) : 0);
    }
}
