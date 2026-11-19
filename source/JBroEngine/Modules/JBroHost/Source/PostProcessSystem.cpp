#include <JBro/Host/PostProcessSystem.h>

#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/Math2DReflection.h>

namespace JBro
{
    UInt32 PostProcessSystem::FindField(const char* effect, const char* field) const
    {
        return PostProcessMixer::FindField(effect, field);
    }

    Bool PostProcessSystem::SetFloat(UInt32 field, Float value)
    {
        return m_mixer != nullptr && m_mixer->SetOverride(field, TypeDescriptorOf<Float>::Get(), &value);
    }

    Bool PostProcessSystem::SetInt(UInt32 field, Int32 value)
    {
        return m_mixer != nullptr && m_mixer->SetOverride(field, TypeDescriptorOf<Int32>::Get(), &value);
    }

    Bool PostProcessSystem::SetBool(UInt32 field, Bool value)
    {
        return m_mixer != nullptr && m_mixer->SetOverride(field, TypeDescriptorOf<Bool>::Get(), &value);
    }

    Bool PostProcessSystem::SetColor(UInt32 field, Color value)
    {
        return m_mixer != nullptr && m_mixer->SetOverride(field, TypeDescriptorOf<Color>::Get(), &value);
    }

    Bool PostProcessSystem::SetVector2(UInt32 field, Vector2 value)
    {
        return m_mixer != nullptr && m_mixer->SetOverride(field, TypeDescriptorOf<Vector2>::Get(), &value);
    }

    void PostProcessSystem::Clear(UInt32 field)
    {
        if (m_mixer != nullptr)
        {
            m_mixer->ClearOverride(field);
        }
    }

    void PostProcessSystem::ClearAll()
    {
        if (m_mixer != nullptr)
        {
            m_mixer->ClearOverrides();
        }
    }
}
