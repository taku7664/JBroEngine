#pragma once

#include <JBro/Asset/MaterialShader.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>

#include <string_view>

namespace JBro
{
    // **머티리얼 셰이더를 그 자리에서 굽는다**(D-347, postprocess-plan §2.4 의 에디터 런타임 컴파일). 에디터·시험·게임 빌드가 든다 - 게임은
    // 들지 않는다(패키지의 구운 묶음만 싣는다).
    //
    // 컴파일러는 DLL 을 실행 중에 연다. DXBC(D3D11)와 반사는 `d3dcompiler_47.dll`(Windows 에 늘 있다), DXIL(D3D12)과 SPIR-V(Vulkan)는
    // `dxcompiler.dll` 이다. `dxcompiler.dll` 은 실행 파일 폴더의 것을 먼저 본다 - 빌드가 Vulkan SDK 의 것(SPIR-V 를 굽고 DXIL 을 스스로
    // 서명한다)을 옮겨 둔다. 그것이 SPIR-V 를 굽지 못하면 SPIR-V 칸은 비고 `HasFormat` 이 거짓이다.
    //
    // 반사는 DXBC 에서 한다: `cbuffer Material : register(b1)` 의 변수(스칼라·벡터만)와 t1.. 의 `Texture2D`. 쓰지 않는 텍스처는 fxc 가 지워
    // 반사에 나오지 않는다. 메인 스레드 전용이다.
    class ShaderCompiler final : public IShaderCompiler
    {
    public:
        ShaderCompiler() = default;
        ShaderCompiler(const ShaderCompiler&) = delete;
        ShaderCompiler& operator=(const ShaderCompiler&) = delete;
        ~ShaderCompiler() override;

        // `executableFolder`(UTF-8) 에서 `dxcompiler.dll` 을 먼저 찾는다. 비어 있으면 시스템 검색 경로만 본다. DXBC 컴파일러가 없으면 거짓이다.
        Bool Initialize(std::string_view executableFolder);
        void Shutdown();

        Bool Compile(std::string_view source, const char* sourceName, MaterialDomain domain, CompiledMaterialShader& out,
            String& error) override;
        Bool HasFormat(ShaderCodeFormat format) const override;

    private:
        struct Impl;
        OwnerPtr<Impl> m_impl;
    };
}
