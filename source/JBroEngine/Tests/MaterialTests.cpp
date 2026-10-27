#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/MaterialFile.h>
#include <JBro/Asset/MaterialShader.h>
#include <JBro/Graphics/MaterialLibrary.h>
#include <JBro/Package/PackageAssetSource.h>
#include <JBro/Package/PackageCollect.h>
#include <JBro/Package/PackageCook.h>
#include <JBro/Package/PackageReader.h>
#include <JBro/Package/PackageWriter.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/ShaderCompiler/ShaderCompiler.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <process.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **머티리얼**(D-347, postprocess-plan §3.14): `.jmat` 본문, 구운 셰이더 묶음, 그 자리에서 굽는 컴파일러와 반사, 파라미터 블록, 게임 패키지의 구운 셰이더.
// 렌더러로 그리는 것은 `PostProcessPixel` 의 머티리얼 시험이 세 백엔드로 잰다.
namespace
{
    namespace fs = std::filesystem;
    using namespace JBro;

    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    String Utf8(const fs::path& path)
    {
        const std::u8string text = path.generic_u8string();
        return String(reinterpret_cast<const char*>(text.data()), text.size());
    }

    void WriteText(const fs::path& path, const char* text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file.write(text, static_cast<std::streamsize>(std::strlen(text)));
        Check(file.good(), "the test writes its own file");
    }

    JBro::Bool Near(JBro::Float a, JBro::Float b)
    {
        return std::fabs(a - b) < 1e-5f;
    }

    // 파라미터 넷과 텍스처 하나를 쓰는 세 자리 겸용 셰이더다. 쓰지 않는 텍스처는 fxc 가 지우므로 자리마다 읽는다.
    constexpr const char* TintShader = R"hlsl(#include "JBro/Material.hlsli"
cbuffer Material : register(b1)
{
    float4 tintColor = float4(1.0f, 0.5f, 0.25f, 1.0f);
    float strength = 0.75f;
    int steps = 3;
    bool invert;
};
Texture2D noise : register(t2);
#if defined(JBRO_MATERIAL_SPRITE)
float4 PSMain(JBroSpriteInput input) : SV_TARGET
{
    const float4 color = JBroSampleSprite(input) * input.tint * noise.Sample(JBroLinearRepeat, input.uv);
    return lerp(color, color * tintColor, strength) + (invert ? 1.0f : 0.0f) * steps;
}
#elif defined(JBRO_MATERIAL_MESH)
float4 PSMain(JBroMeshInput input) : SV_TARGET
{
    const float4 color = input.tint * JBroMeshLight(input.normal) * noise.Sample(JBroLinearRepeat, input.uv);
    return lerp(color, color * tintColor, strength) + (invert ? 1.0f : 0.0f) * steps;
}
#else
float4 PSMain(JBroPostProcessInput input) : SV_TARGET
{
    const float4 color = JBroLoadSource(input) * noise.SampleLevel(JBroLinearClamp, JBroUV(input), 0.0f);
    return lerp(color, color * tintColor, strength) + (invert ? 1.0f : 0.0f) * steps;
}
#endif
)hlsl";

    void TestTheMaterialFileRoundTrips()
    {
        MaterialDocument document;
        document.shader = Uuid::FromName("material test shader");
        document.domain = MaterialDomain::Sprite;
        const Float tint[4] = {1.0f, 0.5f, 0.0f, 1.0f};
        const Float strength = 0.25f;
        document.SetParameter("tintColor", tint, 4);
        document.SetParameter("strength", &strength, 1);
        document.SetTexture("noise", Uuid::FromName("material test texture"));
        const String text = WriteMaterial(document);
        Check(text.Contains("Domain: Sprite") && text.Contains("strength: 0.25") && false == text.Contains("["),
            "the material is written with block sequences only (the engine YAML does not read flow sequences)");

        MaterialDocument read;
        String error;
        Check(ParseMaterial(text.data(), text.size(), read, &error), "the written material reads back");
        Check(read.shader == document.shader && read.domain == MaterialDomain::Sprite, "with its shader and domain");
        const MaterialParameterValue* readTint = read.FindParameter("tintColor");
        Check(readTint != nullptr && readTint->count == 4 && Near(readTint->values[1], 0.5f) && Near(readTint->values[3], 1.0f),
            "a vector keeps its four components");
        Check(read.FindParameter("strength") != nullptr && read.FindParameter("strength")->count == 1, "a scalar stays a scalar");
        Check(read.FindTexture("noise") != nullptr && read.FindTexture("noise")->texture == Uuid::FromName("material test texture"),
            "the texture keeps its id");
        Check(WriteMaterial(read) == text, "writing what was read gives the same text");

        read.RemoveParameter("strength");
        read.SetTexture("noise", AssetId{});
        Check(read.FindParameter("strength") == nullptr && read.textures.IsEmpty(), "a removed parameter and an emptied texture are gone");

        MaterialDocument empty;
        Check(ParseMaterial("", 0, empty) && empty.shader.IsNull() && empty.domain == MaterialDomain::PostProcess,
            "an empty file is a material without a shader");
        const char* mesh = "Domain: Mesh\n";
        Check(ParseMaterial(mesh, std::strlen(mesh), empty, &error) && empty.domain == MaterialDomain::Mesh && WriteMaterial(empty).Contains("Domain: Mesh"),
            "the mesh domain reads and writes back (D-348)");
        const char* unknownDomain = "Domain: Volume\n";
        Check(false == ParseMaterial(unknownDomain, std::strlen(unknownDomain), empty, &error) && error.Contains("Volume"),
            "an unknown domain is refused with its name");
        const char* broken = "Shader: \"unclosed\n";
        Check(false == ParseMaterial(broken, std::strlen(broken), empty, &error), "broken YAML is refused");
        const char* odd = "Parameters:\n  strength: high\n  tint:\n    - 1\n    - 2\n    - 3\n    - 4\n    - 5\nTextures:\n  noise: 12\n";
        Array<String> warnings;
        Check(ParseMaterial(odd, std::strlen(odd), empty, &error, &warnings) && empty.parameters.IsEmpty() && empty.textures.IsEmpty()
                && warnings.Size() == 3,
            "a word, five components and a bad id are dropped with a warning each");
    }

    void TestACompiledSetRoundTripsAndRefusesDamage()
    {
        CompiledShaderSet set;
        CompiledMaterialShader sprite;
        sprite.domain = MaterialDomain::Sprite;
        for (UInt32 format = 0; format < ShaderCodeFormatCount; ++format)
        {
            sprite.pixel[format.Get()].Resize(8 + format.Get());
            std::memset(sprite.pixel[format.Get()].Data(), static_cast<int>(format.Get() + 1), sprite.pixel[format.Get()].Size());
        }
        sprite.parameterBytes = 32;
        MaterialParameterInfo tint;
        tint.name = "tintColor";
        tint.type = MaterialParameterType::Float4;
        tint.offset = 0;
        tint.defaults[1] = 0.5f;
        tint.color = true;
        sprite.parameters.Add(tint);
        MaterialParameterInfo steps;
        steps.name = "steps";
        steps.type = MaterialParameterType::Int;
        steps.offset = 16;
        sprite.parameters.Add(steps);
        MaterialTextureInfo noise;
        noise.name = "noise";
        noise.slot = 2;
        sprite.textures.Add(noise);
        set.variants.Add(sprite);

        Array<std::byte> blob;
        WriteCompiledShaderSet(set, blob);
        CompiledShaderSet read;
        Check(ReadCompiledShaderSet(blob.Data(), blob.Size(), read) && read.variants.Size() == 1, "the compiled set reads back");
        const CompiledMaterialShader* back = read.Find(MaterialDomain::Sprite);
        Check(back != nullptr && read.Find(MaterialDomain::PostProcess) == nullptr, "only the domain it holds is there");
        Check(back->pixel[2].Size() == 10 && back->pixel[2][9] == std::byte{3}, "the bytecode of each backend comes back");
        Check(back->parameterBytes == 32 && back->parameters.Size() == 2 && back->parameters[0].color && Near(back->parameters[0].defaults[1], 0.5f)
                && back->parameters[1].type == MaterialParameterType::Int && back->parameters[1].offset == 16,
            "the parameters come back with their shape, place and defaults");
        Check(back->textures.Size() == 1 && back->textures[0].slot == 2 && back->textures[0].name == "noise", "the texture comes back");

        Array<std::byte> cut = blob;
        cut.Resize(cut.Size() - 1);
        Check(false == ReadCompiledShaderSet(cut.Data(), cut.Size(), read), "a cut blob is refused");
        Array<std::byte> wrongMagic = blob;
        wrongMagic[0] = std::byte{0};
        Check(false == ReadCompiledShaderSet(wrongMagic.Data(), wrongMagic.Size(), read), "a blob with the wrong magic is refused");
        // 파라미터가 블록 밖을 가리키게 고친다(steps 의 자리 16 → 32).
        CompiledShaderSet outside = set;
        outside.variants[0].parameters[1].offset = 32;
        Array<std::byte> outsideBlob;
        WriteCompiledShaderSet(outside, outsideBlob);
        Check(false == ReadCompiledShaderSet(outsideBlob.Data(), outsideBlob.Size(), read), "a parameter outside its block is refused");
    }

    void TestTheCompilerReflectsTheShader(ShaderCompiler& compiler)
    {
        // 스프라이트의 입구가 읽는 라이트맵(t7)은 엔진의 것이라 텍스처에 나오지 않는다(D-348) - 아래의 "텍스처 하나" 가 그것을 본다.
        for (const MaterialDomain domain : {MaterialDomain::PostProcess, MaterialDomain::Sprite, MaterialDomain::Mesh})
        {
            CompiledMaterialShader shader;
            String error;
            if (false == compiler.Compile(TintShader, "Tint.hlsl", domain, shader, error))
            {
                std::cout << "  compiler said: " << error.c_str() << '\n';
            }
            Check(error.empty() && shader.domain == domain && false == shader.pixel[0].IsEmpty(), "the tint shader compiles for every domain");
            Check(shader.domain == domain, "the result knows its domain");
            Check(false == shader.pixel[static_cast<std::size_t>(ShaderCodeFormat::Dxbc)].IsEmpty(), "with D3D11 code");
            Check(compiler.HasFormat(ShaderCodeFormat::Dxil) == (false == shader.pixel[static_cast<std::size_t>(ShaderCodeFormat::Dxil)].IsEmpty()),
                "D3D12 code when dxcompiler.dll opened");
            Check(compiler.HasFormat(ShaderCodeFormat::SpirV) == (false == shader.pixel[static_cast<std::size_t>(ShaderCodeFormat::SpirV)].IsEmpty()),
                "Vulkan code when the compiler writes SPIR-V");
            const MaterialParameterInfo* tint = shader.FindParameter("tintColor");
            const MaterialParameterInfo* strength = shader.FindParameter("strength");
            const MaterialParameterInfo* steps = shader.FindParameter("steps");
            const MaterialParameterInfo* invert = shader.FindParameter("invert");
            Check(shader.parameters.Size() == 4 && tint != nullptr && strength != nullptr && steps != nullptr && invert != nullptr,
                "the four cbuffer variables are the parameters");
            Check(tint->type == MaterialParameterType::Float4 && tint->offset == 0 && tint->color && Near(tint->defaults[2], 0.25f),
                "a float4 named ...Color is a colour with its HLSL initial value");
            Check(strength->type == MaterialParameterType::Float && strength->offset == 16 && false == strength->color
                    && Near(strength->defaults[0], 0.75f),
                "a float follows in the next register");
            Check(steps->type == MaterialParameterType::Int && Near(steps->defaults[0], 3.0f), "an int keeps its initial value");
            Check(invert->type == MaterialParameterType::Bool && Near(invert->defaults[0], 0.0f), "a bool without one starts false");
            Check(shader.parameterBytes == 32, "the block is two registers");
            Check(shader.textures.Size() == 1 && shader.textures[0].name == "noise" && shader.textures[0].slot == 2,
                "the texture is found by name at its register");
        }
        // 틀은 모든 자리에서 구워진다(에디터의 `새 셰이더`).
        for (const MaterialDomain domain : {MaterialDomain::PostProcess, MaterialDomain::Sprite, MaterialDomain::Mesh})
        {
            CompiledMaterialShader shader;
            String error;
            Check(compiler.Compile(GetMaterialShaderTemplate(), "NewShader.hlsl", domain, shader, error) && shader.parameters.Size() == 2,
                "the new shader template compiles for every domain");
        }
        // 사용자 파일의 줄 번호는 엔진이 뒤에 붙이는 입구에 밀리지 않는다(스프라이트의 맺음말은 제 `#line` 이다).
        CompiledMaterialShader broken;
        String error;
        Check(false == compiler.Compile("#include \"JBro/Material.hlsli\"\n\nfloat4 PSMain(JBroSpriteInput input) : SV_TARGET { return nothing; }\n",
                  "Broken.hlsl", MaterialDomain::Sprite, broken, error)
                && error.Contains("Broken.hlsl(3"),
            "a sprite shader's error keeps the user's line");
    }

    void TestTheCompilerRefusesWhatAMaterialCannotHold(ShaderCompiler& compiler)
    {
        const auto refuses = [&](const char* source, const char* expected, const char* message) {
            CompiledMaterialShader shader;
            String error;
            const JBro::Bool compiled = compiler.Compile(source, "Bad.hlsl", MaterialDomain::PostProcess, shader, error);
            if (compiled || false == error.Contains(expected))
            {
                std::cout << "  compiler said: " << error.c_str() << '\n';
            }
            Check(false == compiled && error.Contains(expected), message);
        };
        refuses("#include \"JBro/Material.hlsli\"\nfloat4 PSMain(JBroPostProcessInput input) : SV_TARGET { return undefinedThing; }\n",
            "Bad.hlsl(2", "a compile error names the file and line");
        refuses("#include \"JBro/Material.hlsli\"\ncbuffer Extra : register(b2) { float4 more; };\n"
                "float4 PSMain(JBroPostProcessInput input) : SV_TARGET { return more; }\n",
            "cbuffer Extra", "a cbuffer other than Material is refused");
        refuses("#include \"JBro/Material.hlsli\"\ncbuffer Material : register(b1) { float4x4 turn; };\n"
                "float4 PSMain(JBroPostProcessInput input) : SV_TARGET { return mul(turn, JBroLoadSource(input)); }\n",
            "turn", "a matrix parameter is refused");
        refuses("#include \"JBro/Material.hlsli\"\nTexture2D layers[2] : register(t1);\n"
                "float4 PSMain(JBroPostProcessInput input) : SV_TARGET { return layers[0].Load(int3(0, 0, 0)) + layers[1].Load(int3(0, 0, 0)); }\n",
            "layers", "a texture array is refused");
        refuses("#include \"JBro/Missing.hlsli\"\nfloat4 PSMain(float4 position : SV_POSITION) : SV_TARGET { return 0; }\n",
            "Missing", "an include other than the prelude is refused");
        // t7 은 엔진의 라이트맵 자리다 - 머티리얼 텍스처는 t6 까지다(D-348).
        refuses("#include \"JBro/Material.hlsli\"\nTexture2D last : register(t7);\n"
                "float4 PSMain(JBroPostProcessInput input) : SV_TARGET { return last.Load(int3(0, 0, 0)); }\n",
            "t1..t6", "a texture at t7 is refused");
        // 메시에는 t0 의 입력이 없다.
        CompiledMaterialShader mesh;
        String error;
        Check(false == compiler.Compile("#include \"JBro/Material.hlsli\"\nTexture2D base : register(t0);\n"
                                         "float4 PSMain(JBroMeshInput input) : SV_TARGET { return base.Sample(JBroLinearRepeat, input.uv); }\n",
                  "Bad.hlsl", MaterialDomain::Mesh, mesh, error)
                && error.Contains("no input at t0"),
            "a mesh shader that reads t0 is refused");
    }

    void TestTheParameterBlockFollowsTheDocument(ShaderCompiler& compiler)
    {
        CompiledMaterialShader shader;
        String error;
        Check(compiler.Compile(TintShader, "Tint.hlsl", MaterialDomain::Sprite, shader, error), "the tint shader compiles");
        MaterialDocument document;
        Array<std::byte> block;
        MaterialLibrary::BuildParameterBlock(shader, document, block);
        Check(block.Size() == 32, "the block is as large as the cbuffer");
        float values[8] = {};
        std::memcpy(values, block.Data(), 32);
        std::int32_t steps = 0;
        std::memcpy(&steps, block.Data() + 20, 4);
        Check(values[0] == 1.0f && values[1] == 0.5f && values[2] == 0.25f && values[4] == 0.75f && steps == 3,
            "unwritten parameters take their HLSL initial values");
        const Float strength = 0.125f;
        const Float three[3] = {0.0f, 1.0f, 0.0f};
        const Float many = 6.6f;
        const Float yes = 1.0f;
        document.SetParameter("strength", &strength, 1);
        document.SetParameter("tintColor", three, 3);
        document.SetParameter("steps", &many, 1);
        document.SetParameter("invert", &yes, 1);
        document.SetParameter("notInTheShader", &yes, 1);
        MaterialLibrary::BuildParameterBlock(shader, document, block);
        std::memcpy(values, block.Data(), 32);
        std::memcpy(&steps, block.Data() + 20, 4);
        std::uint32_t invert = 0;
        std::memcpy(&invert, block.Data() + 24, 4);
        Check(values[0] == 0.0f && values[1] == 1.0f && values[2] == 0.0f && values[3] == 0.0f,
            "a written vector takes its components and a missing one is 0");
        Check(values[4] == 0.125f && steps == 7 && invert == 1u, "a float is written as is, an int rounds and a bool is 1");
    }

    // **게임 패키지는 구운 셰이더만 싣는다.** 머티리얼에서 따라가면 셰이더와 텍스처가 함께 오고, 셰이더는 원본 대신 두 자리를 구운 묶음이다.
    // 패키지로 연 에셋 시스템은 그 묶음을 들고, 컴파일러 없는 머티리얼 라이브러리가 그것을 쓴다. 컴파일러 없이는 굽지 않는다.
    void TestAPackageShipsCompiledShaders(ShaderCompiler& compiler)
    {
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform initializes");
        const fs::path root = fs::temp_directory_path() / ("JBroMaterialPackage" + std::to_string(_getpid()));
        std::error_code ignored;
        fs::remove_all(root, ignored);
        const fs::path assets = root / "Assets";
        WriteText(assets / "Shaders" / "Tint.hlsl", TintShader);
        AssetRegistry loose;
        AssetScanOptions options;
        options.createMissingMeta = true;
        AssetScanReport report;
        const String assetRoot = Utf8(assets);
        Check(loose.Scan(platform, assetRoot.c_str(), options, report), "the shader folder scans");
        const AssetRecord* shaderRecord = loose.FindByPath("Shaders/Tint.hlsl");
        Check(shaderRecord != nullptr && shaderRecord->type == AssetType::Shader, "a .hlsl file is a shader asset");
        const AssetId shaderId = shaderRecord->id;
        MaterialDocument document;
        document.shader = shaderId;
        document.domain = MaterialDomain::Sprite;
        const String material = WriteMaterial(document);
        WriteText(assets / "Materials" / "Tint.jmat", material.c_str());
        Check(loose.Scan(platform, assetRoot.c_str(), options, report), "the material scans");
        const AssetRecord* materialRecord = loose.FindByPath("Materials/Tint.jmat");
        Check(materialRecord != nullptr && materialRecord->type == AssetType::Material, "a .jmat file is a material asset");
        const AssetId materialId = materialRecord->id;

        // 느슨한 폴더에서는 셰이더가 원본 글자를 든다.
        {
            AssetSystem looseAssets;
            Check(looseAssets.Initialize(memory), "the loose asset system initializes");
            looseAssets.Bind(platform, loose, assetRoot.c_str());
            const ShaderAssetData* shader = looseAssets.GetShader(looseAssets.Load(shaderId));
            const MaterialAssetData* loaded = looseAssets.GetMaterial(looseAssets.Load(materialId));
            Check(shader != nullptr && false == shader->hasCompiled && shader->source.Contains("tintColor"), "a loose shader holds its text");
            Check(loaded != nullptr && loaded->text == material, "a loose material holds its text");
            looseAssets.Shutdown();
        }

        Array<AssetId> seeds;
        seeds.Add(materialId);
        Array<AssetId> collected;
        Package::CollectReport collectReport;
        Package::CollectAssets(platform, loose, assetRoot.c_str(), ArrayView<const AssetId>(seeds.Data(), seeds.Size()), collected, collectReport);
        Check(collected.Size() == 2 && collected[1] == shaderId, "a material takes its shader along");

        Package::PackageWriter refusedWriter(1);
        Package::CookReport refused;
        Check(false == Package::CookAssets(platform, loose, assetRoot.c_str(), ArrayView<const AssetId>(collected.Data(), collected.Size()),
                           refusedWriter, refused)
                && refused.failures.Size() == 1,
            "without a compiler the shader cannot be cooked");
        if (false == compiler.HasFormat(ShaderCodeFormat::SpirV))
        {
            std::cout << "  [skip] this dxcompiler.dll writes no SPIR-V; the packaged shader is not verified" << std::endl;
            platform.Shutdown();
            return;
        }
        Package::PackageWriter writer(0x5EEDull);
        Package::CookReport cooked;
        const JBro::Bool cookedAll = Package::CookAssets(platform, loose, assetRoot.c_str(),
            ArrayView<const AssetId>(collected.Data(), collected.Size()), writer, cooked, &compiler);
        for (const String& failure : cooked.failures)
        {
            std::cout << "  cook said: " << failure.c_str() << '\n';
        }
        Check(cookedAll, "with a compiler the material and its shader cook");
        Check(cooked.compiledShaders == 1, "the shader is counted as compiled");
        const String packagePath = Utf8(root / "game.jpak");
        String error;
        Check(writer.Save(platform, packagePath.c_str(), error), "the package is written");
        fs::remove_all(assets, ignored);

        Package::PackageReader package;
        Check(package.Open(platform, packagePath.c_str(), error), "the package opens");
        Check(package.Find(shaderId, Package::BlobKind::CompiledShader) != nullptr && package.Find(shaderId, Package::BlobKind::Source) == nullptr,
            "the shader ships compiled, not as its text");
        AssetRegistry packed;
        Check(Package::FillRegistry(package, packed) == 2, "the registry is filled from the index");
        Package::PackageAssetSource source(package);
        AssetSystem packagedAssets;
        Check(packagedAssets.Initialize(memory), "the packaged asset system initializes");
        packagedAssets.Bind(platform, packed, source);
        const AssetHandle shaderHandle = packagedAssets.Load(shaderId);
        const ShaderAssetData* shader = packagedAssets.GetShader(shaderHandle);
        Check(shader != nullptr && shader->hasCompiled && shader->source.empty(), "the packaged shader holds the compiled set");
        Check(shader->compiled.Find(MaterialDomain::Sprite) != nullptr && shader->compiled.Find(MaterialDomain::PostProcess) != nullptr
                && shader->compiled.Find(MaterialDomain::Mesh) != nullptr,
            "both domains were compiled");
        MaterialLibrary library;
        library.Initialize(&packagedAssets, nullptr, nullptr);
        const CompiledMaterialShader* reflected = library.GetCompiledShader(shaderHandle, MaterialDomain::Sprite);
        Check(reflected != nullptr && reflected->FindParameter("strength") != nullptr && library.GetCompileCount() == 0,
            "a library without a compiler uses the packaged shader");
        library.Shutdown();
        packagedAssets.Shutdown();
        package.Close();
        fs::remove_all(root, ignored);
        platform.Shutdown();
    }
}

JBro::Int32 RunMaterialTests()
{
    TestTheMaterialFileRoundTrips();
    TestACompiledSetRoundTripsAndRefusesDamage();
    WindowsPlatform platform;
    JMemoryContext memory;
    Check(platform.Initialize(memory), "the platform initializes");
    ShaderCompiler compiler;
    Check(compiler.Initialize(platform.GetExecutableFolder().View()), "the shader compiler opens d3dcompiler_47.dll");
    Check(compiler.HasFormat(ShaderCodeFormat::Dxil), "the build put dxcompiler.dll next to the tests");
    TestTheCompilerReflectsTheShader(compiler);
    TestTheCompilerRefusesWhatAMaterialCannotHold(compiler);
    TestTheParameterBlockFollowsTheDocument(compiler);
    TestAPackageShipsCompiledShaders(compiler);
    compiler.Shutdown();
    platform.Shutdown();
    std::cout << "Material tests passed.\n";
    return 0;
}
