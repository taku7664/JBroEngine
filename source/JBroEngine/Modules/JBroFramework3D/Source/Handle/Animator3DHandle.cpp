#include <JBro/Framework3D/Handle/Animator3D.h>

#include <JBro/Framework3D/Component/Animator3D.h>

#include <cstring>

namespace JBro::Handle
{
    Component::Animator3D* Animator3D::Resolve(const char* operation) const
    {
        return static_cast<Component::Animator3D*>(ResolveComponent(StaticTypeName(), operation));
    }

    Bool Animator3D::Play(UInt32 layer, Int32 clip)
    {
        Component::Animator3D* component = Resolve("Play");
        return component != nullptr && component->Play(layer, clip);
    }

    Bool Animator3D::CrossFade(UInt32 layer, Int32 clip, Float seconds)
    {
        Component::Animator3D* component = Resolve("CrossFade");
        return component != nullptr && component->CrossFade(layer, clip, seconds);
    }

    Bool Animator3D::Stop(UInt32 layer)
    {
        Component::Animator3D* component = Resolve("Stop");
        return component != nullptr && component->Stop(layer);
    }

    Bool Animator3D::SetLayerWeight(UInt32 layer, Float weight)
    {
        Component::Animator3D* component = Resolve("SetLayerWeight");
        return component != nullptr && component->SetLayerWeight(layer, weight);
    }

    Bool Animator3D::SetLayerAdditive(UInt32 layer, Bool additive)
    {
        Component::Animator3D* component = Resolve("SetLayerAdditive");
        return component != nullptr && component->SetLayerAdditive(layer, additive);
    }

    Bool Animator3D::SetLayerMask(UInt32 layer, const char* rootBone, UInt32 blendDepth)
    {
        Component::Animator3D* component = Resolve("SetLayerMask");
        const std::string_view name = rootBone != nullptr ? std::string_view(rootBone, std::strlen(rootBone)) : std::string_view();
        return component != nullptr && component->SetLayerMask(layer, name, blendDepth);
    }

    Float Animator3D::GetLayerTime(UInt32 layer) const
    {
        const Component::Animator3D* component = Resolve("GetLayerTime");
        return component != nullptr ? component->GetLayerTime(layer) : Float(0.0f);
    }

    Bool Animator3D::IsPlaying(UInt32 layer) const
    {
        const Component::Animator3D* component = Resolve("IsPlaying");
        return component != nullptr && component->IsPlaying(layer);
    }

    Int32 Animator3D::GetLayerClip(UInt32 layer) const
    {
        const Component::Animator3D* component = Resolve("GetLayerClip");
        return component != nullptr ? component->GetLayerClip(layer) : Int32(-1);
    }

    Float Animator3D::GetSpeed() const
    {
        const Component::Animator3D* component = Resolve("GetSpeed");
        return component != nullptr ? component->speed : Float(0.0f);
    }

    void Animator3D::SetSpeed(Float value)
    {
        Component::Animator3D* component = Resolve("SetSpeed");
        if (component == nullptr)
        {
            return;
        }
        component->speed = value;
    }
}
