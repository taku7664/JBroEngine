#include "TestClock.h"
#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ScreenSpace.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Framework2DSystem/Rendering/RenderWorld2D.h>
#include <JBro/Framework2DSystem/Rendering/SpritePieces.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <process.h>
#include <stdexcept>
#include <string>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 스프라이트 펴 그리기(D-351 2 단계): 9-슬라이스·타일·채우기. 조각 셈은 숫자로, 그림은 D3D12 로 그려 되읽는다.
namespace
{
    using namespace JBro;
    namespace fs = std::filesystem;

    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    JBro::Bool Near(JBro::Float got, JBro::Float want)
    {
        return std::fabs(got - want) < 0.001f;
    }

    JBro::Bool PieceIs(const SpritePiece& piece, JBro::Float gu0, JBro::Float gu1, JBro::Float gv0, JBro::Float gv1,
        JBro::Float tu0, JBro::Float tu1, JBro::Float tv0, JBro::Float tv1)
    {
        return Near(piece.gu0, gu0) && Near(piece.gu1, gu1) && Near(piece.gv0, gv0) && Near(piece.gv1, gv1)
            && Near(piece.tu0, tu0) && Near(piece.tu1, tu1) && Near(piece.tv0, tv0) && Near(piece.tv1, tv1);
    }

    SpriteRenderItem Item(Component::SpriteDrawMode mode, Vector2 size, JBro::Float tileWidth, JBro::Float tileHeight)
    {
        SpriteRenderItem item;
        item.drawMode = mode;
        item.size = size;
        item.tileWidth = tileWidth;
        item.tileHeight = tileHeight;
        return item;
    }

    // **9-슬라이스는 모서리를 그대로 두고 변·가운데만 늘린다.** 4 x 4 칸에 테두리 1 을 16 x 16 으로 펴면 모서리 조각의 그려지는 비율과
    // 텍스처 비율이 1/16 과 1/4 로 다르다 - 그 차이가 모서리를 지키는 것이다.
    void TestSlicedPiecesKeepTheCorners()
    {
        SpriteRenderItem item = Item(Component::SpriteDrawMode::Sliced, {16.0f, 16.0f}, 4.0f, 4.0f);
        item.border[0] = 1.0f;
        item.border[1] = 1.0f;
        item.border[2] = 1.0f;
        item.border[3] = 1.0f;
        SpritePiece pieces[MaxSpritePieces];
        Check(BuildSpritePieces(item, pieces, MaxSpritePieces) == 9, "a sliced sprite with four borders is nine pieces");
        Check(PieceIs(pieces[0], 0.0f, 1.0f / 16.0f, 0.0f, 1.0f / 16.0f, 0.0f, 0.25f, 0.0f, 0.25f), "the bottom-left corner is one pixel of the rectangle and one of the texture");
        Check(PieceIs(pieces[4], 1.0f / 16.0f, 15.0f / 16.0f, 1.0f / 16.0f, 15.0f / 16.0f, 0.25f, 0.75f, 0.25f, 0.75f), "the middle stretches the inner half of the texture over fourteen pixels");
        Check(PieceIs(pieces[8], 15.0f / 16.0f, 1.0f, 15.0f / 16.0f, 1.0f, 0.75f, 1.0f, 0.75f, 1.0f), "the top-right corner is the texture's top-right pixel");
        Check(PieceIs(pieces[7], 1.0f / 16.0f, 15.0f / 16.0f, 15.0f / 16.0f, 1.0f, 0.25f, 0.75f, 0.75f, 1.0f), "the top edge stretches sideways only");

        // 테두리 합이 크기를 넘으면 비례로 줄어들고, 너비 0 인 조각은 나오지 않는다.
        item.size = {2.0f, 16.0f};
        Check(BuildSpritePieces(item, pieces, MaxSpritePieces) == 6, "a rectangle too narrow for its borders drops the middle column");
        Check(PieceIs(pieces[0], 0.0f, 0.5f, 0.0f, 1.0f / 16.0f, 0.0f, 0.25f, 0.0f, 0.25f), "and shares the width between the two border columns");

        // 풀리지 않은 스프라이트(칸 크기 0)는 온 조각 하나다.
        item.size = {16.0f, 16.0f};
        item.tileWidth = 0.0f;
        Check(BuildSpritePieces(item, pieces, MaxSpritePieces) == 1 && PieceIs(pieces[0], 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f),
            "an unresolved sprite falls back to one whole piece");
        // 용량이 모자라면 거기서 끊긴다.
        item.tileWidth = 4.0f;
        Check(BuildSpritePieces(item, pieces, 4) == 4, "the piece count never exceeds the capacity");
    }

    // **타일은 칸 크기로 되풀이하고 마지막 타일은 같은 비율로 잘린다.** 4 x 4 칸을 10 x 10 에 깔면 한 줄에 세 장이고 셋째는 2 픽셀이다.
    void TestTiledPiecesRepeatTheCell()
    {
        const SpriteRenderItem item = Item(Component::SpriteDrawMode::Tiled, {10.0f, 10.0f}, 4.0f, 4.0f);
        SpritePiece pieces[MaxSpritePieces];
        Check(BuildSpritePieces(item, pieces, MaxSpritePieces) == 9, "ten pixels take three tiles of four per axis");
        Check(PieceIs(pieces[0], 0.0f, 0.4f, 0.0f, 0.4f, 0.0f, 1.0f, 0.0f, 1.0f), "the first tile is a whole cell");
        Check(PieceIs(pieces[2], 0.8f, 1.0f, 0.0f, 0.4f, 0.0f, 0.5f, 0.0f, 1.0f), "the last tile in a row shows half the cell");
        Check(PieceIs(pieces[8], 0.8f, 1.0f, 0.8f, 1.0f, 0.0f, 0.5f, 0.0f, 0.5f), "and the top-right tile is cut on both axes");
        // 많이 깔면 상한에서 끊긴다.
        const SpriteRenderItem huge = Item(Component::SpriteDrawMode::Tiled, {4000.0f, 4000.0f}, 4.0f, 4.0f);
        Check(BuildSpritePieces(huge, pieces, MaxSpritePieces) == MaxSpritePieces, "a huge area stops at the piece limit");
        // 칸 크기가 사각형보다 크면 한 장이 잘린 채 나온다.
        const SpriteRenderItem small = Item(Component::SpriteDrawMode::Tiled, {2.0f, 4.0f}, 4.0f, 4.0f);
        Check(BuildSpritePieces(small, pieces, MaxSpritePieces) == 1 && PieceIs(pieces[0], 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.0f, 1.0f),
            "a rectangle narrower than the cell shows part of one tile");
    }

    // **채우기는 한 조각이고 시작 변에서부터 비율만큼이다.**
    void TestFilledPiecesStartAtTheOrigin()
    {
        SpriteRenderItem item = Item(Component::SpriteDrawMode::Filled, {16.0f, 8.0f}, 16.0f, 8.0f);
        item.fillAmount = 0.25f;
        SpritePiece pieces[MaxSpritePieces];
        Check(BuildSpritePieces(item, pieces, MaxSpritePieces) == 1 && PieceIs(pieces[0], 0.0f, 0.25f, 0.0f, 1.0f, 0.0f, 0.25f, 0.0f, 1.0f),
            "a quarter from the left is the left quarter of the rectangle and the texture");
        item.fillOrigin = Component::SpriteFillOrigin::Right;
        Check(BuildSpritePieces(item, pieces, MaxSpritePieces) == 1 && PieceIs(pieces[0], 0.75f, 1.0f, 0.0f, 1.0f, 0.75f, 1.0f, 0.0f, 1.0f),
            "from the right it is the right quarter");
        item.fillOrigin = Component::SpriteFillOrigin::Bottom;
        Check(BuildSpritePieces(item, pieces, MaxSpritePieces) == 1 && PieceIs(pieces[0], 0.0f, 1.0f, 0.0f, 0.25f, 0.0f, 1.0f, 0.0f, 0.25f),
            "from the bottom it is the bottom quarter");
        item.fillOrigin = Component::SpriteFillOrigin::Top;
        Check(BuildSpritePieces(item, pieces, MaxSpritePieces) == 1 && PieceIs(pieces[0], 0.0f, 1.0f, 0.75f, 1.0f, 0.0f, 1.0f, 0.75f, 1.0f),
            "from the top it is the top quarter");
        item.fillAmount = 0.0f;
        Check(BuildSpritePieces(item, pieces, MaxSpritePieces) == 0, "an empty fill draws nothing");
        item.fillAmount = 7.0f;
        Check(BuildSpritePieces(item, pieces, MaxSpritePieces) == 1 && PieceIs(pieces[0], 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f),
            "an overfull fill is the whole sprite");
        const SpriteRenderItem simple = Item(Component::SpriteDrawMode::Simple, {16.0f, 8.0f}, 0.0f, 0.0f);
        Check(BuildSpritePieces(simple, pieces, MaxSpritePieces) == 1 && PieceIs(pieces[0], 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f),
            "a simple sprite is one whole piece");
    }

    // 4 x 4 RGBA PNG. 모서리는 빨강(왼쪽 위)·초록(오른쪽 위)·파랑(왼쪽 아래)·노랑(오른쪽 아래), 위 변은 하늘색, 아래 변은 자홍, 왼쪽 변은 주황,
    // 오른쪽 변은 보라, 가운데는 흰색이다. 테두리 1 로 9 조각을 펴면 아홉 색이 자기 자리에 있어야 한다.
    constexpr unsigned char SlicePng[] =
    {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04,
        0x08, 0x06, 0x00, 0x00, 0x00, 0xa9, 0xf1, 0x9e, 0x7e, 0x00, 0x00, 0x00, 0x20, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x9f, 0xe1, 0xff, 0x7f, 0x28, 0x06, 0xc1, 0x06, 0x10, 0x13, 0x02, 0x40, 0x4c, 0x4c, 0x01, 0x06, 0x84, 0x7a, 0xb0, 0x26, 0x00, 0x10, 0xce, 0x2e,
        0xd4, 0x52, 0xf3, 0xbe, 0xa6, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
    };

    void WriteBytes(const fs::path& path, const void* data, std::size_t size)
    {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    String Utf8(const fs::path& path)
    {
        const std::u8string text = path.generic_u8string();
        return String(reinterpret_cast<const char*>(text.data()), text.size());
    }

    struct SliceProject
    {
        WindowsPlatform platform;
        JMemoryContext memory;
        AssetRegistry registry;
        AssetSystem assets;
        fs::path root;
        AssetId spriteId;

        void Open()
        {
            root = fs::temp_directory_path() / (L"JBroSliceProbe·조각-" + std::to_wstring(_getpid()));
            fs::remove_all(root);
            WriteBytes(root / "Art" / "slice.png", SlicePng, sizeof(SlicePng));
            Check(platform.Initialize(memory), "the platform initializes");
            AssetScanOptions options;
            options.createMissingMeta = true;
            AssetScanReport report;
            Check(registry.Scan(platform, Utf8(root).c_str(), options, report), "the art folder scans");
            for (std::size_t index = 0; index < registry.GetCount(); ++index)
            {
                if (registry.GetRecord(index).type == AssetType::Sprite)
                {
                    spriteId = registry.GetRecord(index).id;
                }
            }
            Check(false == spriteId.IsNull(), "the png registers a sprite");
            // 테두리 1 픽셀, 픽셀 샘플링. 옵션은 `.jmeta` 로 간다 - 에디터가 쓰는 길과 같다.
            const String metaPath = Utf8(root / "Art" / "slice.png.jmeta");
            AssetMetaFile meta;
            AssetMetaError error;
            Check(LoadAssetMetaFile(platform, metaPath.c_str(), meta, error), "the png meta reads");
            meta.hasTextureOptions = true;
            meta.textureOptions.filter = TextureFilter::Nearest;
            meta.hasSpriteOptions = true;
            meta.spriteOptions.borderLeft = 1;
            meta.spriteOptions.borderTop = 1;
            meta.spriteOptions.borderRight = 1;
            meta.spriteOptions.borderBottom = 1;
            Check(SaveAssetMetaFile(platform, metaPath.c_str(), meta), "the png meta saves");
            AssetMetaFile reread;
            Check(LoadAssetMetaFile(platform, metaPath.c_str(), reread, error) && reread.hasSpriteOptions && reread.spriteOptions.borderRight == 1,
                "the border round-trips through the meta file");
            Check(assets.Initialize(memory), "the asset system initializes");
            assets.Bind(platform, registry, Utf8(root).c_str());
        }

        void Close()
        {
            assets.Shutdown();
            platform.Shutdown();
            fs::remove_all(root);
        }
    };

    struct Gpu
    {
        WindowsPlatform& platform;
        D3D12RHIModule rhi;
        Renderer renderer;
        WindowHandle window;
        Array<std::byte> image;
        TextureReadback readback;
        JBro::Bool ready = false;

        explicit Gpu(WindowsPlatform& owner, JMemoryContext memory)
            : platform(owner)
        {
            if (false == rhi.Initialize(memory))
            {
                return;
            }
            WindowDesc windowDesc;
            constexpr char title[] = "JBro slice probe";
            windowDesc.title = {title, static_cast<JBro::UInt32>(sizeof(title) - 1)};
            windowDesc.width = 64;
            windowDesc.height = 64;
            windowDesc.visible = false;
            window = platform.OpenPlatformWindow(windowDesc);
            RendererConfig config;
            config.api = rhi.GetApi();
            config.surface = platform.CreateSurface(window);
            config.surfaceExtent = {64, 64};
            config.maxSpriteSubmissions = 64;
            config.presentMode = PresentMode::Immediate;
            Check(renderer.Initialize(rhi, config), "the renderer initializes");
            image.Resize(64 * 64 * 4);
            ready = true;
        }

        void Close()
        {
            if (false == ready)
            {
                rhi.Shutdown();
                return;
            }
            renderer.Shutdown();
            rhi.Shutdown();
            platform.ClosePlatformWindow(window);
            platform.PumpEvents();
        }

        void Paint(Framework2D& framework)
        {
            JBro::Testing::Tick(framework, 1.0f / 60.0f);
            Check(renderer.BeginFrame() == FrameStatus::Ready, "the frame begins");
            Check(framework.Render() == RenderResult::Submitted, "the framework submits");
            Check(renderer.EndFrame() == FrameStatus::Ready, "the frame presents");
            Check(renderer.ReadBackBuffer(image.Data(), image.Size(), readback), "the frame reads back");
        }

        // BGRA 바이트를 0..255 로 읽는다.
        JBro::Bool Is(JBro::UInt32 x, JBro::UInt32 y, JBro::Int32 r, JBro::Int32 g, JBro::Int32 b) const
        {
            const auto* pixel = reinterpret_cast<const unsigned char*>(
                image.Data() + static_cast<std::size_t>(y) * readback.rowPitch + static_cast<std::size_t>(x) * 4);
            const auto close = [](JBro::Int32 got, JBro::Int32 want) { return std::abs((got - want).Get()) <= 8; };
            const JBro::Bool ok = close(pixel[2], r) && close(pixel[1], g) && close(pixel[0], b);
            if (false == ok)
            {
                std::cout << "  read " << static_cast<int>(pixel[2]) << ", " << static_cast<int>(pixel[1]) << ", " << static_cast<int>(pixel[0])
                          << " wanted " << r << ", " << g << ", " << b << " at " << x << ", " << y << '\n';
            }
            return ok;
        }
    };

    // **그려 본다**: 화면 레이어(64 x 64 = 기준이라 1 유닛 = 1 픽셀)에 4 x 4 스프라이트를 세 방식으로 편다.
    void TestDrawModesReachTheScreen()
    {
        SliceProject project;
        project.Open();
        Gpu gpu(project.platform, project.memory);
        if (false == gpu.ready)
        {
            std::cout << "  [skip] no D3D12 device; draw modes not verified" << std::endl;
            gpu.Close();
            project.Close();
            return;
        }
        {
            Framework2D framework;
            FrameworkContext context;
            JBro::Testing::AttachClock(context);
            context.memory = project.memory;
            context.assets = &project.assets;
            context.renderer = &gpu.renderer;
            Check(framework.Initialize(context), "the framework initializes");
            ScreenSpaceFrame screen;
            screen.referenceWidth = 64.0f;
            screen.referenceHeight = 64.0f;
            screen.targetWidth = 64.0f;
            screen.targetHeight = 64.0f;
            framework.SetScreenSpace(screen);
            Canvas* canvas = framework.GetCanvas();
            Object::GameObject* cameraObject = canvas->CreateObject("camera");
            canvas->AttachComponent<Component::Transform2D>(cameraObject);
            auto* camera = canvas->AttachComponent<Component::Camera2D>(cameraObject);
            camera->primary = true;
            camera->orthographicSize = 1.0f;
            camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
            Layer& ui = canvas->CreateLayer("UI");
            ui.SetSpace(LayerSpace::Screen);
            Object::GameObject* panel = canvas->CreateObject("panel");
            Check(canvas->SetObjectLayer(panel, ui.GetId()), "the panel goes on the screen layer");
            canvas->AttachComponent<Component::Transform2D>(panel);
            auto* sprite = canvas->AttachComponent<Component::SpriteRenderer2D>(panel);
            sprite->spriteId = project.spriteId;
            sprite->sizeMode = Component::SpriteSizeMode::Custom;
            sprite->size = {16.0f, 16.0f};
            sprite->drawMode = Component::SpriteDrawMode::Sliced;
            framework.BindCanvasAssets();
            Check(sprite->sprite.generation != 0, "the sprite resolves");

            // 9-슬라이스: 가운데 (32, 32) 의 16 x 16 은 x·y 24..39 다. 모서리는 한 픽셀, 변과 가운데는 늘어난다.
            gpu.Paint(framework);
            Check(gpu.Is(24, 24, 255, 0, 0) && gpu.Is(39, 24, 0, 255, 0) && gpu.Is(24, 39, 0, 0, 255) && gpu.Is(39, 39, 255, 255, 0),
                "the four corners keep their one-pixel colors");
            Check(gpu.Is(31, 24, 0, 255, 255) && gpu.Is(31, 39, 255, 0, 255) && gpu.Is(24, 31, 255, 128, 0) && gpu.Is(39, 31, 128, 0, 255),
                "the edges stretch along their side");
            Check(gpu.Is(31, 31, 255, 255, 255) && gpu.Is(25, 25, 255, 255, 255) && gpu.Is(38, 38, 255, 255, 255), "the middle is the white center");
            Check(gpu.Is(23, 31, 0, 0, 0) && gpu.Is(40, 31, 0, 0, 0), "nothing is drawn outside the rectangle");

            // 타일: 10 x 10 은 x·y 27..36 이다. 타일은 왼쪽 아래부터 깔리므로 아래 줄(y 33..36)이 온전한 칸이고 맨 위 두 줄은 칸의 아래 반이다.
            sprite->drawMode = Component::SpriteDrawMode::Tiled;
            sprite->size = {10.0f, 10.0f};
            gpu.Paint(framework);
            Check(gpu.Is(27, 33, 255, 0, 0) && gpu.Is(28, 33, 0, 255, 255) && gpu.Is(30, 33, 0, 255, 0), "the first tile shows the whole cell's top row");
            Check(gpu.Is(31, 33, 255, 0, 0) && gpu.Is(35, 33, 255, 0, 0) && gpu.Is(36, 33, 0, 255, 255), "the next tiles repeat it and the last is cut to two pixels");
            Check(gpu.Is(27, 36, 0, 0, 255) && gpu.Is(27, 27, 255, 128, 0) && gpu.Is(27, 28, 0, 0, 255), "a cut tile on top shows the cell's bottom half");

            // 채우기: 왼쪽에서 절반이면 x 24..31 만 그려진다.
            sprite->drawMode = Component::SpriteDrawMode::Filled;
            sprite->size = {16.0f, 16.0f};
            sprite->fillAmount = 0.5f;
            gpu.Paint(framework);
            Check(gpu.Is(24, 24, 255, 0, 0) && gpu.Is(31, 24, 0, 255, 255) && gpu.Is(32, 24, 0, 0, 0) && gpu.Is(39, 24, 0, 0, 0),
                "half a fill from the left draws the left half of the texture and nothing on the right");
            sprite->fillOrigin = Component::SpriteFillOrigin::Top;
            gpu.Paint(framework);
            Check(gpu.Is(24, 24, 255, 0, 0) && gpu.Is(24, 31, 255, 128, 0) && gpu.Is(24, 32, 0, 0, 0), "from the top it draws the top half");

            // 단순: 그대로 4 x 4 가 16 x 16 으로 늘어난다 - 모서리 색이 4 픽셀씩이다.
            sprite->drawMode = Component::SpriteDrawMode::Simple;
            sprite->fillAmount = 1.0f;
            gpu.Paint(framework);
            Check(gpu.Is(24, 24, 255, 0, 0) && gpu.Is(27, 27, 255, 0, 0) && gpu.Is(28, 28, 255, 255, 255), "a simple sprite stretches every pixel alike");
            framework.Shutdown();
        }
        gpu.Close();
        project.Close();
    }
}

JBro::Int32 RunSpriteDrawModeTests()
{
    TestSlicedPiecesKeepTheCorners();
    TestTiledPiecesRepeatTheCell();
    TestFilledPiecesStartAtTheOrigin();
    TestDrawModesReachTheScreen();
    std::cout << "Sprite draw mode tests passed." << std::endl;
    return 0;
}
