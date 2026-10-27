#pragma once

#include <JBro/Graphics/PostProcessMixer.h>
#include <JBro/Runtime/IPostProcessSystem.h>

namespace JBro
{
    // **스크립트의 후처리 덮어쓰기를 받는 자리**(D-346). 프레임워크가 하나를 들고 그 섞기(`PostProcessMixer`)를 건넨다 - 덮어쓴 값은 섞기가 들고
    // 섞기의 맨 끝(`ApplyOverrides`)에 얹는다. 스크립트 컨텍스트를 묶을 때 `SystemContext::PostProcess` 에 꽂힌다.
    class PostProcessSystem final : public System::IPostProcessSystem
    {
    public:
        void Bind(PostProcessMixer* mixer)
        {
            m_mixer = mixer;
        }

        UInt32 FindField(const char* effect, const char* field) const override;
        Bool SetFloat(UInt32 field, Float value) override;
        Bool SetInt(UInt32 field, Int32 value) override;
        Bool SetBool(UInt32 field, Bool value) override;
        Bool SetColor(UInt32 field, Color value) override;
        Bool SetVector2(UInt32 field, Vector2 value) override;
        void Clear(UInt32 field) override;
        void ClearAll() override;

    private:
        PostProcessMixer* m_mixer = nullptr;
    };
}
