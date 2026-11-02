#pragma once

#include <JBro/Asset/MaterialFile.h>
#include <JBro/Asset/MaterialShader.h>
#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>
#include <JBro/Core/Core.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/NameTable.h>
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
        // **그 오브젝트의 렌더러 머티리얼이다**(D-367). `owner`(컴포넌트의 인스턴스 아이디)가 이 머티리얼의 파라미터를 덮어썼으면 그 인스턴스, 아니면 `Resolve` 와 같다.
        // 덮어쓴 오브젝트가 없으면 찾기가 늘지 않는다.
        AssetHandle Resolve(AssetHandle materialAsset, InstanceId owner);
        // 바뀐 머티리얼을 다시 짓는다. 프레임워크의 Update 가 프레임마다 한 번 부른다.
        void Update();
        // 마지막으로 지을 때의 오류다(읽히지 않는 본문, 셰이더 없음, 컴파일 오류). 문제가 없었거나 처음 보는 머티리얼이면 nullptr 이다.
        const String* GetError(AssetHandle materialAsset) const;

        // **셰이더 에셋을 그 자리로 구운 것**이다(반사 포함). 에디터의 머티리얼 인스펙터가 파라미터·텍스처 이름을 이것으로 안다. 실패하면 nullptr 이고
        // `error` 에 까닭을 준다. 셰이더 본문이 바뀌면 다시 굽는다.
        const CompiledMaterialShader* GetCompiledShader(AssetHandle shaderAsset, MaterialDomain domain, const String** error = nullptr);

        // **스크립트의 파라미터 덮어쓰기**(D-348, `MaterialService`). 머티리얼 아이디와 셰이더의 변수 이름으로 자리를 잡고 번호를 준다 - 같은 둘이면
        // 같은 번호다. 아직 실리지 않은 머티리얼도 잡을 수 있다(실리면 얹힌다). 자리가 차면 `InvalidParameter` 다. 덮어쓴 값은 그 머티리얼의 파일 값 위에
        // 얹혀 다음 `Update` 에서 렌더러에 올라간다. 셰이더의 형과 칸 수가 맞지 않으면 얹지 않는다(`float3` 은 넷을 받아 넷째를 버린다).
        static constexpr UInt32 MaxParameterOverrides = 64;
        static constexpr UInt32 InvalidParameter = 0xFFFFFFFFu;
        UInt32 FindParameter(AssetId material, const char* parameter);
        // **오브젝트 하나의 파라미터**(D-367). 같은 머티리얼을 쓰는 다른 오브젝트는 바뀌지 않는다. 값을 얹는 자리는 에셋의 덮어쓰기 위다. 그 오브젝트의 값을 모두
        // 지우면 인스턴스도 사라져 다시 한 배치로 그려진다. 오브젝트마다 배치가 끊긴다.
        UInt32 FindParameter(AssetId material, const char* parameter, InstanceId owner);
        Bool SetParameter(UInt32 parameter, const Float* values, UInt32 count, Bool integer);
        void ClearParameter(UInt32 parameter);
        void ClearParameters();
        // 머티리얼의 파라미터가 바뀔 때마다(다시 짓기·덮어쓰기) 오르는 수다. 후처리 프로필 라이브러리가 이것으로 볼륨 섞기의 값을 다시 뜬다.
        UInt32 GetParameterStamp() const noexcept
        {
            return m_parameterStamp;
        }
        // **볼륨 섞기에 쓸 지금의 파라미터**(D-348). 지은 머티리얼의 셰이더 아이디·지금의 파라미터 블록(덮어쓰기 포함)·4 바이트 칸마다의 종류
        // (`MaterialCellKind`)다. 짓지 못한 머티리얼이면 거짓이다. 가리키는 곳은 다음 `Update` 까지 산다.
        Bool DescribeParameters(AssetHandle materialAsset, AssetId& shader, JArrayView<std::byte>& block, JArrayView<std::uint8_t>& kinds) const;

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
            // 지은 셰이더(주소는 `m_compiled` 칸이라 살아 있다)와 그때의 블록이다. `baseBlock` 은 파일의 값, `liveBlock` 은 덮어쓰기를 얹은 것이다.
            // 덮어쓰기가 바뀌면 다시 짓지 않고 `liveBlock` 만 다시 떠 올린다 - 프레임 안에서 할당하지 않는다.
            const Compiled* compiled = nullptr;
            AssetId shaderId;
            Array<std::byte> baseBlock;
            Array<std::byte> liveBlock;
            Array<std::uint8_t> cellKinds;
            Array<NameId> parameterNames;
            AssetHandle rendererTextures[MaxMaterialTextures];
            UInt32 appliedOverrideStamp = 0;
        };
        struct ParameterOverride
        {
            AssetId material;
            NameId name = InvalidNameId;
            // 0 이면 에셋 전체이고, 아니면 그 오브젝트(컴포넌트 인스턴스 아이디)만이다(D-367).
            InstanceId owner = InvalidInstanceId;
            Float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            UInt32 count = 0;
            Bool integer = false;
            Bool set = false;
        };

        Bool IsStale(const Material& material) const;
        void Build(Material& material);
        void ReleaseAssets(Material& material);
        AssetHandle EnsureTexture(AssetHandle textureAsset);
        Compiled* Compile(AssetHandle shaderAsset, MaterialDomain domain);
        // 오브젝트 하나가 덮어쓴 머티리얼이다(D-367).
        struct Instance
        {
            InstanceId owner = InvalidInstanceId;
            AssetHandle asset;
            AssetHandle renderer;
            // 지을 때의 원본 렌더러 핸들이다. 다르면 원본이 다시 올라간 것이라 인스턴스도 다시 올린다.
            AssetHandle base;
            Array<std::byte> block;
        };
        // `block` 위에 그 주인의 덮어쓰기를 얹는다.
        void LayOverrides(const Material& material, InstanceId owner, Array<std::byte>& block) const;
        // 덮어쓰기와 머티리얼에 맞춰 인스턴스를 짓고 고치고 지운다.
        void SyncInstances();
        // `baseBlock` 위에 이 머티리얼의 덮어쓰기를 얹어 `liveBlock` 을 뜬다.
        void ApplyOverrides(Material& material);
        void Refresh(Material& material);

        AssetSystem* m_assets = nullptr;
        Renderer* m_renderer = nullptr;
        IShaderCompiler* m_compiler = nullptr;
        Array<Material> m_materials;
        // 주소가 머티리얼의 `builtShader` 로 잡히므로 칸을 지우지 않고 다시 쓴다(셰이더 하나·자리 하나에 한 칸).
        Array<OwnerPtr<Compiled>> m_compiled;
        Array<TextureSlot> m_textures;
        UInt32 m_compileCount = 0;
        UInt32 m_buildCount = 0;
        ParameterOverride m_overrides[MaxParameterOverrides];
        UInt32 m_overrideCount = 0;
        // 덮어쓰기를 고칠 때마다 오른다. 머티리얼은 얹은 때의 값을 들고 다르면 다시 얹는다.
        UInt32 m_overrideStamp = 1;
        UInt32 m_parameterStamp = 1;
        Array<Instance> m_instances;
        // 인스턴스를 맞춘 때의 두 판이다. 같으면 할 일이 없다.
        UInt32 m_syncedOverrideStamp = 0;
        UInt32 m_syncedParameterStamp = 0;
    };

    // 볼륨 섞기가 파라미터 블록의 4 바이트 칸을 무엇으로 읽는가다(D-348). `Unused` 는 파라미터가 아닌 채움 칸이다.
    enum class MaterialCellKind : std::uint8_t
    {
        Float,
        Int,
        UInt,
        Bool,
        Unused = 0xFF,
    };
}
