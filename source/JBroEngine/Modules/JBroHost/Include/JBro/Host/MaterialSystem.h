#pragma once

#include <JBro/Graphics/MaterialLibrary.h>
#include <JBro/Runtime/IMaterialSystem.h>

namespace JBro
{
    // **스크립트의 머티리얼 파라미터 덮어쓰기를 받는 자리**(D-348). 프레임워크가 하나를 들고 그 머티리얼 라이브러리를 건넨다 - 덮어쓴 값은 라이브러리가
    // 들고 다음 `Update` 에서 렌더러의 머티리얼에 얹는다. 스크립트 컨텍스트를 묶을 때 `SystemContext::Material` 에 꽂힌다.
    class MaterialSystem final : public System::IMaterialSystem
    {
    public:
        void Bind(MaterialLibrary* library)
        {
            m_library = library;
        }

        UInt32 FindParameter(Uuid material, const char* parameter) override
        {
            return m_library != nullptr ? m_library->FindParameter(material, parameter) : InvalidParameter;
        }
        Bool SetValues(UInt32 parameter, const Float* values, UInt32 count, Bool integer) override
        {
            return m_library != nullptr && m_library->SetParameter(parameter, values, count, integer);
        }
        void Clear(UInt32 parameter) override
        {
            if (m_library != nullptr)
            {
                m_library->ClearParameter(parameter);
            }
        }
        void ClearAll() override
        {
            if (m_library != nullptr)
            {
                m_library->ClearParameters();
            }
        }

    private:
        MaterialLibrary* m_library = nullptr;
    };
}
