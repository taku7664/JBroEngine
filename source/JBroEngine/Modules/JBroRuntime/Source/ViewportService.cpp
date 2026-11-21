#include <JBro/Runtime/ViewportService.h>

#include <JBro/AssetTypes/AssetTypes.h>

#include <JBro/Runtime/IViewportSystem.h>
#include <JBro/Runtime/SystemContext.h>

namespace JBro::Service
{
    namespace
    {
        System::IViewportSystem* Viewports()
        {
            return GetSystemContext().Viewports;
        }
    }

    UInt32 ViewportService::GetCount() const
    {
        System::IViewportSystem* system = Viewports();
        return system != nullptr ? system->GetCount() : UInt32(0);
    }

    Int32 ViewportService::Find(const char* name) const
    {
        System::IViewportSystem* system = Viewports();
        return system != nullptr && name != nullptr ? system->Find(name) : Int32(-1);
    }

    Bool ViewportService::IsActive(UInt32 index) const
    {
        System::IViewportSystem* system = Viewports();
        return system != nullptr && system->IsActive(index);
    }

    void ViewportService::SetActive(UInt32 index, Bool active) const
    {
        if (System::IViewportSystem* system = Viewports())
        {
            system->SetActive(index, active);
        }
    }

    Vector2 ViewportService::GetOrigin(UInt32 index) const
    {
        System::IViewportSystem* system = Viewports();
        return system != nullptr ? system->GetOrigin(index) : Vector2{};
    }

    void ViewportService::SetOrigin(UInt32 index, Vector2 origin) const
    {
        if (System::IViewportSystem* system = Viewports())
        {
            system->SetOrigin(index, origin);
        }
    }

    Vector2 ViewportService::GetSize(UInt32 index) const
    {
        System::IViewportSystem* system = Viewports();
        return system != nullptr ? system->GetSize(index) : Vector2{};
    }

    void ViewportService::SetSize(UInt32 index, Vector2 size) const
    {
        if (System::IViewportSystem* system = Viewports())
        {
            system->SetSize(index, size);
        }
    }

    Bool ViewportService::GetClearViewport(UInt32 index) const
    {
        System::IViewportSystem* system = Viewports();
        return system != nullptr && system->GetClearViewport(index);
    }

    void ViewportService::SetClearViewport(UInt32 index, Bool clear) const
    {
        if (System::IViewportSystem* system = Viewports())
        {
            system->SetClearViewport(index, clear);
        }
    }

    Handle::GameObject ViewportService::GetCamera(UInt32 index) const
    {
        System::IViewportSystem* system = Viewports();
        return system != nullptr ? system->GetCamera(index) : Handle::GameObject{};
    }

    Bool ViewportService::SetCamera(UInt32 index, Handle::GameObject camera) const
    {
        System::IViewportSystem* system = Viewports();
        return system != nullptr && system->SetCamera(index, camera);
    }

    void ViewportService::SetAutoCamera(UInt32 index) const
    {
        if (System::IViewportSystem* system = Viewports())
        {
            system->SetAutoCamera(index);
        }
    }

    Vector2 ViewportService::GetOriginPixels(UInt32 index) const
    {
        System::IViewportSystem* system = Viewports();
        return system != nullptr ? system->GetOriginPixels(index) : Vector2{};
    }

    void ViewportService::SetOriginPixels(UInt32 index, Vector2 pixels) const
    {
        if (System::IViewportSystem* system = Viewports())
        {
            system->SetOriginPixels(index, pixels);
        }
    }

    Vector2 ViewportService::GetSizePixels(UInt32 index) const
    {
        System::IViewportSystem* system = Viewports();
        return system != nullptr ? system->GetSizePixels(index) : Vector2{};
    }

    void ViewportService::SetSizePixels(UInt32 index, Vector2 pixels) const
    {
        if (System::IViewportSystem* system = Viewports())
        {
            system->SetSizePixels(index, pixels);
        }
    }

    AssetHandle ViewportService::GetRenderTexture(UInt32 index) const
    {
        System::IViewportSystem* system = Viewports();
        return system != nullptr ? system->GetRenderTexture(index) : AssetHandle{};
    }

    void ViewportService::SetRenderTexture(UInt32 index, AssetHandle texture) const
    {
        if (System::IViewportSystem* system = Viewports())
        {
            system->SetRenderTexture(index, texture);
        }
    }

    Bool ViewportService::DrawsLayer(UInt32 index, Handle::Layer layer) const
    {
        System::IViewportSystem* system = Viewports();
        return system != nullptr && layer.IsValid() && system->DrawsLayer(index, layer.GetId());
    }

    void ViewportService::SetLayerDrawn(UInt32 index, Handle::Layer layer, Bool drawn) const
    {
        System::IViewportSystem* system = Viewports();
        if (system != nullptr && layer.IsValid())
        {
            system->SetLayerDrawn(index, layer.GetId(), drawn);
        }
    }

    void ViewportService::ResetLayers(UInt32 index) const
    {
        if (System::IViewportSystem* system = Viewports())
        {
            system->ResetLayers(index);
        }
    }
}
