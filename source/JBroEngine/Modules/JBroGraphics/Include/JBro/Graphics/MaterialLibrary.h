#pragma once

#include <JBro/Asset/MaterialFile.h>
#include <JBro/Asset/MaterialShader.h>
#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/SafePtr.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class AssetSystem;
    class Renderer;

    // **머티리얼 에셋을 렌더러 머티리얼로 푼다**(D-347, postprocess-plan §3.14). `PostProcessLibrary` 와 같은 자리다 - 에셋은 본문 글자만 들고,
    // 구운 셰이더와 렌더러의 머티리얼·텍스처는 여기가 든다. 2D·3D 프레임워크가 하나씩 든다.
    //
    // 셰이더는 머티리얼의 자리(`Domain`)로 굽는다. 에디터는 넘겨받은 컴파일러(`IShaderCompiler`)로 그 자리에서 굽고, 게임은 패키지의 구운 묶음을
    // 쓴다(컴파일러가 없다). 같은 셰이더·같은 자리는 한 번만 굽는다.
    //
    // **프레임 경로에 할당이 없다.** `Resolve` 는 처음 보는 머티리얼만 짓고 그 밖에는 찾기만 한다. 바뀐 것(머티리얼·셰이더 본문, 텍스처 픽셀)은
    // 프레임마다 한 번 `Update` 가 세대를 견주어 다시 짓는다 - 렌더러 핸들은 그대로 두고 파라미터·텍스처만 갈아 끼우며, 셰이더가 바뀌면 새로 올린다.
    // **렌더러 프레임 밖에서만 부른다.**
    class MaterialLibrary final
    {
    public:
        MaterialLibrary() = default;
        MaterialLibrary(const MaterialLibrary&) = delete;
        MaterialLibrary& operator=(const MaterialLibrary&) = delete;
        ~MaterialLibrary();

        void Initialize(AssetSystem* assets, Renderer* renderer, IShaderCompiler* compiler);
        void Shutdown();

        // 머티리얼 에셋(로드된 핸들)의 렌더러 머티리얼이다. 빈 핸들·머티리얼이 아닌 에셋·짓지 못한 머티리얼은 빈 핸들이다(그 까닭은 `GetError`).
        // 렌더러 핸들은 머티리얼의 셰이더가 바뀌기 전까지 그대로다.
        AssetHandle Resolve(AssetHandle materialAsset);
        // 바뀐 머티리얼을 다시 짓는다. 프레임워크의 Update 가 프레임마다 한 번 부른다.
        void Update();
        // 마지막으로 지을 때의 오류다(읽히지 않는 본문, 셰이더 없음, 컴파일 오류). 문제가 없었거나 처음 보는 머티리얼이면 nullptr 이다.
        const String* GetError(AssetHandle materialAsset) const;

        // **셰이더 에셋을 그 자리로 구운 것**이다(반사 포함). 에디터의 머티리얼 인스펙터가 파라미터·텍스처 이름을 이것으로 안다. 실패하면 nullptr 이고
        // `error` 에 까닭을 준다. 셰이더 본문이 바뀌면 다시 굽는다.
        const CompiledMaterialShader* GetCompiledShader(AssetHandle shaderAsset, MaterialDomain domain, const String** error = nullptr);

        // **머티리얼 문서를 셰이더의 파라미터 블록으로 옮긴다.** 적히지 않은 파라미터는 HLSL 초기값이다. 칸 수가 모자라면 0, 넘치면 버린다.
        // `blocks` 는 `shader.parameterBytes` 바이트로 맞춘다. 에디터·시험도 쓴다.
        static void BuildParameterBlock(const CompiledMaterialShader& shader, const MaterialDocument& document, Array<std::byte>& block);

        // 시험이 본다.
        UInt32 GetMaterialCount() const noexcept;
        UInt32 GetCompileCount() const noexcept
        {
            return m_compileCount;
        }
        UInt32 GetBuildCount() const noexcept
        {
            return m_buildCount;
        }

    private:
        struct Compiled
        {
            AssetHandle shader;
            UInt32 dataGeneration = 0;
            MaterialDomain domain = MaterialDomain::PostProcess;
            CompiledMaterialShader result;
            Bool ok = false;
            String error;
        };
        struct TextureSlot
        {
            AssetHandle asset;
            UInt32 pixelGeneration = 0;
            AssetHandle texture;
        };
        struct Material
        {
            AssetHandle asset;
            UInt32 dataGeneration = 0;
            // 머티리얼이 잡은 셰이더·텍스처 에셋이다. 다시 지을 때 놓는다.
            AssetHandle shader;
            UInt32 shaderGeneration = 0;
            MaterialDomain domain = MaterialDomain::PostProcess;
            Array<AssetHandle> textures;
            Array<UInt32> textureGenerations;
            // 이 머티리얼이 렌더러에 올린 것과 그때의 셰이더(에셋·세대·자리)다. 셰이더가 같으면 파라미터·텍스처만 갈아 끼운다.
            AssetHandle renderer;
            AssetHandle registeredShader;
            UInt32 registeredGeneration = 0;
            MaterialDomain registeredDomain = MaterialDomain::PostProcess;
            String error;
            Bool hasError = false;
        };

        Bool IsStale(const Material& material) const;
        void Build(Material& material);
        void ReleaseAssets(Material& material);
        AssetHandle EnsureTexture(AssetHandle textureAsset);
        Compiled* Compile(AssetHandle shaderAsset, MaterialDomain domain);

        AssetSystem* m_assets = nullptr;
        Renderer* m_renderer = nullptr;
        IShaderCompiler* m_compiler = nullptr;
        Array<Material> m_materials;
        // 주소가 머티리얼의 `builtShader` 로 잡히므로 칸을 지우지 않고 다시 쓴다(셰이더 하나·자리 하나에 한 칸).
        Array<OwnerPtr<Compiled>> m_compiled;
        Array<TextureSlot> m_textures;
        UInt32 m_compileCount = 0;
        UInt32 m_buildCount = 0;
    };
}
