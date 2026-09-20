#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Core/Core.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
#include <JBro/Framework2DSystem/System/SpriteRender2DSystem.h>
#include <JBro/Framework2DSystem/System/Transform2DSystem.h>
#include <JBro/Runtime/GameObject.h>
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
        WriteBytes(root / "c.png", TinyPng, sizeof(TinyPng));
        JBro::AssetRegistry registry;
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(registry.Scan(platform, Utf8(root).c_str(), options, report) && report.registered == 6, "three images scan");
        JBro::AssetId spriteA;
        JBro::AssetId spriteB;
        JBro::AssetId spriteC;
        const JBro::AssetId textureA = registry.FindByPath("a.png")->id;
        const JBro::AssetId textureC = registry.FindByPath("c.png")->id;
        for (std::size_t index = 0; index < registry.GetCount(); ++index)
        {
            const JBro::AssetRecord& record = registry.GetRecord(index);
            if (record.type == JBro::AssetType::Sprite)
            {
                (record.owner == textureA ? spriteA : record.owner == textureC ? spriteC : spriteB) = record.id;
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
        // 크기는 칸 픽셀 / 에셋 PPU 다(D-117). 새 메타의 PPU 는 100 이라 2x2 는 0.02 유닛이다.
        JBro::SpriteFrameView view;
        Check(library.Resolve(handleA, 0, texture, uv, &view)
                && view.widthUnits == 0.02f && view.heightUnits == 0.02f && view.pivotX == 0.5f && view.pivotY == 0.5f,
            "a 2x2 image at the default 100 pixels per unit is 0.02 units wide with the centre pivot");
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
        // 칸이 정사각형이 아니어도 너비와 높이가 따로 간다: 2x2 를 두 줄 한 칸으로 자르면 2x1 픽셀, PPU 2 로 1 x 0.5 유닛.
        {
            const JBro::String metaB = Utf8(root / "b.png.jmeta");
            JBro::AssetMetaFile metaFileB;
            JBro::AssetMetaError rowError;
            Check(JBro::LoadAssetMetaFile(platform, metaB.c_str(), metaFileB, rowError), "b's meta loads");
            JBro::String rows = JBro::FormatAssetMetaFile(metaFileB);
            rows.append("  ImportOptions:\n    sliceType: CellCount\n    rowCount: 2\n    columnCount: 1\n    pixelsPerUnit: 2\n");
            JBro::JArrayView<std::byte> rowBytes;
            rowBytes.data = reinterpret_cast<const std::byte*>(rows.data());
            rowBytes.size = static_cast<std::uint32_t>(rows.size());
            Check(platform.WriteWholeFile(metaB.c_str(), rowBytes), "b's meta with two rows saves");
            Check(assets.ReloadInPlace(spriteB), "b reloads in place");
            JBro::SpriteFrameView wide;
            Check(library.Resolve(handleB, 1, textureB, uv, &wide) && wide.widthUnits == 1.0f && wide.heightUnits == 0.5f,
                "a 2x1 cell at 2 pixels per unit is one unit wide and half a unit tall");
        }

        // 시트 옵션을 적어 넣고 in-place 재로드하면 칸이 풀린다. 텍스처는 그대로다.
        JBro::AssetMetaFile meta;
        JBro::AssetMetaError error;
        const JBro::String metaPath = Utf8(root / "a.png.jmeta");
        Check(JBro::LoadAssetMetaFile(platform, metaPath.c_str(), meta, error), "the meta loads");
        JBro::String text = JBro::FormatAssetMetaFile(meta);
        text.append("  ImportOptions:\n    sliceType: CellCount\n    rowCount: 2\n    columnCount: 2\n    pixelsPerUnit: 2\n    pivotX: 0\n");
        JBro::JArrayView<std::byte> bytes;
        bytes.data = reinterpret_cast<const std::byte*>(text.data());
        bytes.size = static_cast<std::uint32_t>(text.size());
        Check(platform.WriteWholeFile(metaPath.c_str(), bytes), "the meta with a sheet saves");
        Check(assets.ReloadInPlace(spriteA), "the sprite reloads in place");
        Check(library.Resolve(handleA, 3, again, uv) && SameHandle(again, texture), "the same texture serves the sheet");
        Check(uv[0] == 0.5f && uv[1] == 0.5f && uv[2] == 0.5f && uv[3] == 0.5f, "frame three is the bottom-right cell");
        Check(library.Resolve(handleA, 1, again, uv) && uv[0] == 0.5f && uv[1] == 0.0f, "frame one is the top-right cell");
        Check(library.Resolve(handleA, 1, again, uv, &view) && view.widthUnits == 0.5f && view.heightUnits == 0.5f
                && view.pivotX == 0.0f && view.pivotY == 0.5f,
            "a 1x1 cell at 2 pixels per unit is half a unit, with the sheet's pivot");

        // **시스템은 그 크기를 쓴다.** 캔버스의 스프라이트가 `FromSprite`(기본)면 저작 `size` 가 아니라 칸 크기와 칸 피벗이
        // 렌더 월드에 들어가고, `Custom` 이면 저작 값이다(D-117).
        {
            JBro::Canvas canvas(JBro::CreateDefaultAllocator());
            JBro::GameObject* object = canvas.CreateObject("hero");
            auto* transform = canvas.AttachComponent<JBro::Component::Transform2D>(object);
            auto* sprite = canvas.AttachComponent<JBro::Component::SpriteRenderer2D>(object);
            Check(transform != nullptr && sprite != nullptr, "the probe object must have its components");
            sprite->sprite = handleA;
            sprite->frameIndex = 1;
            sprite->size = {7.0f, 9.0f};
            sprite->pivot = {0.25f, 0.75f};
            JBro::RenderWorld2D world;
            Check(world.ReserveSprites(2), "the render world must reserve");
            JBro::System::Transform2DSystem transforms;
            JBro::System::SpriteRender2DSystem sprites;
            sprites.SetRenderWorld(&world);
            sprites.SetSpriteLibrary(&library);
            const auto extract = [&]() {
                world.BeginFrame();
                transforms.Update(canvas, 0.0f);
                sprites.Update(canvas, 0.0f);
                world.EndFrame();
                Check(world.GetSpriteCount() == 1, "one sprite must be extracted");
                return world.GetSprite(0);
            };
            const JBro::SpriteRenderItem fromSprite = extract();
            Check(fromSprite.size.x == 0.5f && fromSprite.size.y == 0.5f
                    && fromSprite.pivot.x == 0.0f && fromSprite.pivot.y == 0.5f,
                "FromSprite takes the cell's size in units and the cell's pivot");
            sprite->sizeMode = JBro::Component::SpriteSizeMode::Custom;
            sprite->pivotMode = JBro::Component::SpritePivotMode::Custom;
            const JBro::SpriteRenderItem custom = extract();
            Check(custom.size.x == 7.0f && custom.size.y == 9.0f && custom.pivot.x == 0.25f && custom.pivot.y == 0.75f,
                "Custom takes the authored size and pivot");
            // 크기는 에셋, 피벗은 저작 값 - D-117 이 말한 "피벗 덮어쓰기" 다.
            sprite->sizeMode = JBro::Component::SpriteSizeMode::FromSprite;
            const JBro::SpriteRenderItem mixed = extract();
            Check(mixed.size.x == 0.5f && mixed.size.y == 0.5f && mixed.pivot.x == 0.25f && mixed.pivot.y == 0.75f,
                "the asset's size with an authored pivot is reachable");
            sprite->pivotMode = JBro::Component::SpritePivotMode::FromSprite;
            sprite->sprite = {};
            const JBro::SpriteRenderItem unresolved = extract();
            Check(unresolved.size.x == 7.0f && unresolved.pivot.x == 0.25f,
                "an unresolved sprite falls back to the authored values even in FromSprite");
            Check(fromSprite.filter == JBro::TextureFilter::Nearest && unresolved.filter == JBro::TextureFilter::Nearest,
                "a texture that says nothing samples nearest, and so does an unresolved sprite");
        }

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

        // **메타의 샘플러가 화면까지 간다**(D-117). 프레임워크 전체를 세워 c.png(PPU 100 → 0.02 유닛이라 메타로 PPU 2 를
        // 준다)를 1x1 유닛으로 그린다. 카메라 반높이 1 → 64 픽셀이 2 유닛, 스프라이트는 (16..48) 픽셀이고 텍셀 경계가
        // x = 32 다. Nearest 는 경계 오른쪽이 순수한 초록이고, Linear 는 빨강과 섞인다.
        {
            const JBro::String metaC = Utf8(root / "c.png.jmeta");
            JBro::AssetMetaFile metaFile;
            Check(JBro::LoadAssetMetaFile(platform, metaC.c_str(), metaFile, error), "c's meta loads");
            JBro::String sheet = JBro::FormatAssetMetaFile(metaFile);
            sheet.append("  ImportOptions:\n    pixelsPerUnit: 2\n");
            bytes.data = reinterpret_cast<const std::byte*>(sheet.data());
            bytes.size = static_cast<std::uint32_t>(sheet.size());
            Check(platform.WriteWholeFile(metaC.c_str(), bytes), "c's meta with a PPU saves");

            JBro::Framework2D framework;
            JBro::FrameworkContext context;
            context.memory = memory;
            context.assets = &assets;
            context.renderer = &renderer;
            Check(framework.Initialize(context), "the framework initializes with assets and a renderer");
            JBro::Canvas* canvas = framework.GetCanvas();
            JBro::GameObject* cameraObject = canvas->CreateObject("camera");
            canvas->AttachComponent<JBro::Component::Transform2D>(cameraObject);
            auto* camera = canvas->AttachComponent<JBro::Component::Camera2D>(cameraObject);
            camera->primary = true;
            camera->orthographicSize = 1.0f;
            camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
            JBro::GameObject* heroObject = canvas->CreateObject("hero");
            canvas->AttachComponent<JBro::Component::Transform2D>(heroObject);
            auto* hero = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(heroObject);
            hero->spriteId = spriteC;
            framework.BindCanvasAssets();
            Check(hero->sprite.generation != 0, "the hero's sprite resolves");

            JBro::Array<std::byte> image;
            image.Resize(64 * 64 * 4);
            JBro::TextureReadback readback;
            const auto paint = [&](std::uint32_t x, std::uint32_t y, float& r, float& g, float& b) {
                framework.Update(1.0f / 60.0f);
                Check(renderer.BeginFrame() == JBro::FrameStatus::Ready, "the framework frame must begin");
                Check(framework.Render() == JBro::RenderResult::Submitted, "the framework must submit the hero");
                Check(renderer.EndFrame() == JBro::FrameStatus::Ready, "the framework frame must present");
                Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback), "the framework frame reads back");
                const auto* px = reinterpret_cast<const unsigned char*>(
                    image.Data() + static_cast<std::size_t>(y) * readback.rowPitch + static_cast<std::size_t>(x) * 4);
                b = px[0] / 255.0f;
                g = px[1] / 255.0f;
                r = px[2] / 255.0f;
            };
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            paint(32, 24, r, g, b);
            Check(r < 0.05f && g > 0.95f, "with the project's Nearest the seam pixel is pure green");
            paint(8, 8, r, g, b);
            Check(r < 0.05f && g < 0.05f && b < 0.05f, "and outside the one-unit sprite is the clear colour");

            JBro::String smooth = JBro::FormatAssetMetaFile(metaFile);
            smooth.append("  ImportOptions:\n    pixelsPerUnit: 2\nTexture:\n  ImportOptions:\n    filter: Linear\n");
            bytes.data = reinterpret_cast<const std::byte*>(smooth.data());
            bytes.size = static_cast<std::uint32_t>(smooth.size());
            Check(platform.WriteWholeFile(metaC.c_str(), bytes), "c's meta with Linear saves");
            Check(assets.ReloadInPlace(textureC), "c's texture reloads in place");
            paint(32, 24, r, g, b);
            Check(r > 0.3f && r < 0.7f && g > 0.3f && g < 0.7f, "with the texture's Linear the seam pixel blends red and green");
            framework.Shutdown();
        }

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
