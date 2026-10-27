#include <JBro/Runtime/PostProcessService.h>

#include <JBro/Runtime/IPostProcessSystem.h>
#include <JBro/Runtime/SystemContext.h>

namespace JBro::Service
{
    namespace
    {
        System::IPostProcessSystem* PostProcess()
        {
            return GetSystemContext().PostProcess;
        }
    }

    PostProcessField PostProcessService::FindField(const char* effect, const char* field) const
    {
        System::IPostProcessSystem* system = PostProcess();
        if (system == nullptr || effect == nullptr || field == nullptr)
        {
            return {};
        }
        return {system->FindField(effect, field)};
    }

    Bool PostProcessService::SetFloat(PostProcessField field, Float value) const
    {
        System::IPostProcessSystem* system = PostProcess();
        return system != nullptr && field.IsValid() && system->SetFloat(field.value, value);
    }

    Bool PostProcessService::SetInt(PostProcessField field, Int32 value) const
    {
        System::IPostProcessSystem* system = PostProcess();
        return system != nullptr && field.IsValid() && system->SetInt(field.value, value);
    }

    Bool PostProcessService::SetBool(PostProcessField field, Bool value) const
    {
        System::IPostProcessSystem* system = PostProcess();
        return system != nullptr && field.IsValid() && system->SetBool(field.value, value);
    }

    Bool PostProcessService::SetColor(PostProcessField field, Color value) const
    {
        System::IPostProcessSystem* system = PostProcess();
        return system != nullptr && field.IsValid() && system->SetColor(field.value, value);
    }

    Bool PostProcessService::SetVector2(PostProcessField field, Vector2 value) const
    {
        System::IPostProcessSystem* system = PostProcess();
        return system != nullptr && field.IsValid() && system->SetVector2(field.value, value);
    }

    void PostProcessService::Clear(PostProcessField field) const
    {
        System::IPostProcessSystem* system = PostProcess();
        if (system != nullptr && field.IsValid())
        {
            system->Clear(field.value);
        }
    }

    void PostProcessService::ClearAll() const
    {
        if (System::IPostProcessSystem* system = PostProcess())
        {
            system->ClearAll();
        }
    }
}
