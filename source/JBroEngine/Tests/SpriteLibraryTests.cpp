#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Framework2DSystem/Rendering/SpriteLibrary.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    namespace fs = std::filesystem;

    void Check(bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    constexpr unsigned char TinyPng[] =
    {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d,
        0x24, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x1f, 0x0c, 0x81, 0x34, 0x08, 0x34, 0x00, 0x00, 0x49, 0x49, 0x09, 0x78, 0x9c, 0x51, 0x17, 0x92,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
    };

    JBro::String Utf8(const fs::path& path)
    {
        const std::u8string text = path.generic_u8string();
        return JBro::String(reinterpret_cast<const char*>(text.data()), text.size());
    }

    void WriteBytes(const fs::path& path, const void* data, std::size_t size)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    bool SameHandle(JBro::AssetHandle a, JBro::AssetHandle b)
    {
        return a.index == b.index && a.generation == b.generation;
    }

    // **스프라이트 에셋이 GPU 텍스처가 된다(D-113).** 같은 텍스처는 한 번만 올라가고, 칸은 UV 로 풀리며, in-place 재로드는
    // 같은 렌더러 핸들에 다시 올라간다. 렌더러가 필요해 D3D12 가 없으면 건너뛴다.
    void TestTheLibraryUploadsOnceAndFollowsFrames()
    {
        JBro::WindowsPlatform platform;
        JBro::D3D12RHIModule rhi;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no D3D12 device; sprite library not verified" << std::endl;
            platform.Shutdown();
            return;
        }
        JBro::WindowDesc windowDesc;
        constexpr char title[] = "JBro sprite library probe";
        windowDesc.title = {title, sizeof(title) - 1};
        windowDesc.width = 64;
        windowDesc.height = 64;
        windowDesc.visible = false;
        const JBro::WindowHandle window = platform.OpenPlatformWindow(windowDesc);
        JBro::Renderer renderer;
        JBro::RendererConfig config;
        config.api = rhi.GetApi();
        config.surface = platform.CreateSurface(window);
        config.surfaceExtent = {64, 64};
        config.maxSpriteSubmissions = 8;
        config.presentMode = JBro::PresentMode::Immediate;
        Check(renderer.Initialize(rhi, config), "the renderer must initialize");

        const fs::path root = fs::temp_directory_path() / L"JBroSpriteLibraryProbe";
        fs::remove_all(root);
        WriteBytes(root / "a.png", TinyPng, sizeof(TinyPng));
        WriteBytes(root / "b.png", TinyPng, sizeof(TinyPng));
        JBro::AssetRegistry registry;
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(registry.Scan(platform, Utf8(root).c_str(), options, report) && report.registered == 4, "two images scan");
        JBro::AssetId spriteA;
        JBro::AssetId spriteB;
        const JBro::AssetId textureA = registry.FindByPath("a.png")->id;
        for (std::size_t index = 0; index < registry.GetCount(); ++index)
        {
            const JBro::AssetRecord& record = registry.GetRecord(index);
            if (record.type == JBro::AssetType::Sprite)
            {
                (record.owner == textureA ? spriteA : spriteB) = record.id;
            }
        }
        JBro::AssetSystem assets;
        Check(assets.Initialize(memory), "the asset system initializes");
        assets.Bind(platform, registry, Utf8(root).c_str());

        JBro::SpriteLibrary library;
        library.Initialize(&assets, &renderer);
        JBro::AssetHandle texture;
        float uv[4] = {9.0f, 9.0f, 9.0f, 9.0f};
        Check(false == library.Resolve(JBro::AssetHandle{}, 0, texture, uv), "an empty handle does not resolve");
        Check(false == library.Resolve(JBro::AssetHandle{5, 5}, 0, texture, uv) && uv[0] == 9.0f,
            "a handle that is not loaded does not resolve and leaves the outputs alone");

        const JBro::AssetHandle handleA = assets.Load(spriteA);
        Check(library.Resolve(handleA, 0, texture, uv), "a loaded sprite resolves");
        Check(texture.generation != 0 && renderer.GetTextureCount() == 1 && library.GetUploadedTextureCount() == 1,
            "its texture went up once");
        Check(uv[0] == 0.0f && uv[1] == 0.0f && uv[2] == 1.0f && uv[3] == 1.0f, "an unsliced sprite is the whole texture");
        JBro::AssetHandle again;
        Check(library.Resolve(handleA, 7, again, uv) && SameHandle(again, texture) && renderer.GetTextureCount() == 1,
            "resolving again reuses the upload, and a frame index past the end is the last frame");

        const JBro::AssetHandle handleB = assets.Load(spriteB);
        JBro::AssetHandle textureB;
        Check(library.Resolve(handleB, 0, textureB, uv) && false == SameHandle(textureB, texture)
                && renderer.GetTextureCount() == 2,
            "a second image is a second texture");

        // 시트 옵션을 적어 넣고 in-place 재로드하면 칸이 풀린다. 텍스처는 그대로다.
        JBro::AssetMetaFile meta;
        JBro::AssetMetaError error;
        const JBro::String metaPath = Utf8(root / "a.png.jmeta");
        Check(JBro::LoadAssetMetaFile(platform, metaPath.c_str(), meta, error), "the meta loads");
        JBro::String text = JBro::FormatAssetMetaFile(meta);
        text.append("  ImportOptions:\n    sliceType: CellCount\n    rowCount: 2\n    columnCount: 2\n");
        JBro::JArrayView<std::byte> bytes;
        bytes.data = reinterpret_cast<const std::byte*>(text.data());
        bytes.size = static_cast<std::uint32_t>(text.size());
        Check(platform.WriteWholeFile(metaPath.c_str(), bytes), "the meta with a sheet saves");
        Check(assets.ReloadInPlace(spriteA), "the sprite reloads in place");
        Check(library.Resolve(handleA, 3, again, uv) && SameHandle(again, texture), "the same texture serves the sheet");
        Check(uv[0] == 0.5f && uv[1] == 0.5f && uv[2] == 0.5f && uv[3] == 0.5f, "frame three is the bottom-right cell");
        Check(library.Resolve(handleA, 1, again, uv) && uv[0] == 0.5f && uv[1] == 0.0f, "frame one is the top-right cell");

        // 텍스처의 in-place 재로드는 같은 렌더러 핸들에 다시 올린다.
        Check(assets.ReloadInPlace(textureA), "the texture reloads in place");
        Check(library.Resolve(handleA, 0, again, uv) && SameHandle(again, texture) && renderer.GetTextureCount() == 2,
            "a new pixel generation is uploaded into the same texture");

        // 스프라이트를 내리고 다시 올리면 에셋 핸들의 세대가 바뀌고, 라이브러리는 그 슬롯을 새로 올린다.
        assets.Release(handleA);
        Check(assets.CollectUnused() == 2, "the sprite and its texture go");
        const JBro::AssetHandle reloaded = assets.Load(spriteA);
        Check(false == SameHandle(reloaded, handleA), "the reloaded sprite is a new generation");
        JBro::AssetHandle fresh;
        Check(library.Resolve(reloaded, 0, fresh, uv) && false == SameHandle(fresh, texture) && renderer.GetTextureCount() == 2,
            "the slot's old upload is replaced, not leaked");

        library.Shutdown();
        Check(renderer.GetTextureCount() == 0, "shutting the library down returns every texture");
        assets.Shutdown();
        renderer.Shutdown();
        rhi.Shutdown();
        platform.ClosePlatformWindow(window);
        platform.PumpEvents();
        platform.Shutdown();
        fs::remove_all(root);
    }
}

int RunSpriteLibraryTests()
{
    TestTheLibraryUploadsOnceAndFollowsFrames();
    std::cout << "Sprite library tests passed.\n";
    return 0;
}
