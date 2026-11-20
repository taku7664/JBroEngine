#include "TestClock.h"
#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Asset/RenderTextureFile.h>
#include <JBro/Framework2DSystem/System/Camera2DSystem.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/ScreenSpace.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Light2D.h>
#include <JBro/Framework2D/Component/Mask2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Graphics/PostProcess.h>
#include <JBro/Graphics/PostProcessProfile.h>
#include <JBro/ScreenUi/Rendering/RenderWorld2D.h>
#include <JBro/ScreenUi/Rendering/SpritePieces.h>
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

            // **마스크**(D-351 3 단계): 가운데 16 x 8 창 안의 32 x 32 흰 사각형은 창만큼만 보인다 - x 24..39, y 28..35.
            sprite->visible = false;
            Object::GameObject* window = canvas->CreateObject("window");
            Check(canvas->SetObjectLayer(window, ui.GetId()), "the window goes on the screen layer");
            canvas->AttachComponent<Component::Transform2D>(window);
            auto* mask = canvas->AttachComponent<Component::Mask2D>(window);
            mask->size = {16.0f, 8.0f};
            Object::GameObject* paper = canvas->CreateObject("paper");
            Check(canvas->SetObjectLayer(paper, ui.GetId()), "the paper goes on the screen layer");
            paper->SetParent(window);
            canvas->AttachComponent<Component::Transform2D>(paper);
            auto* white = canvas->AttachComponent<Component::SpriteRenderer2D>(paper);
            white->sizeMode = Component::SpriteSizeMode::Custom;
            white->size = {32.0f, 32.0f};
            gpu.Paint(framework);
            gpu.Paint(framework);
            Check(gpu.Is(31, 31, 255, 255, 255) && gpu.Is(24, 28, 255, 255, 255) && gpu.Is(39, 35, 255, 255, 255), "inside the mask the paper is drawn");
            Check(gpu.Is(31, 27, 0, 0, 0) && gpu.Is(31, 36, 0, 0, 0) && gpu.Is(23, 31, 0, 0, 0) && gpu.Is(40, 31, 0, 0, 0),
                "outside the mask nothing of the paper is drawn");
            // 마스크를 옮기면 잘리는 자리도 따라간다.
            canvas->FindComponentRaw<Component::Transform2D>(window)->position = {8.0f, 0.0f};
            gpu.Paint(framework);
            gpu.Paint(framework);
            Check(gpu.Is(32, 31, 255, 255, 255) && gpu.Is(47, 31, 255, 255, 255) && gpu.Is(31, 31, 0, 0, 0) && gpu.Is(48, 31, 0, 0, 0),
                "moving the mask moves the clip");
            framework.Shutdown();
        }
        gpu.Close();
        project.Close();
    }

    // **분할 화면과 조명 뒤의 화면 레이어**(D-359, ui-plan §7.3). 64 x 64 대상에 왼쪽 절반의 카메라 A(월드 (-10, 0))와 오른쪽 절반의 카메라 B((10, 0)). A 앞에 빨강, B 앞에 파랑
    // 1 x 1 스프라이트가 있고 월드는 0.2 의 환경광뿐이라 빛을 받는 스프라이트는 어둡다. 화면 레이어의 흰 8 x 8 은 오른쪽 위에서 빛과 무관하게 희다(두 번째 프레임부터 광원 맵).
    void TestSplitScreenCamerasAndTheScreenLayerStaysUnlit()
    {
        SliceProject project;
        project.Open();
        Gpu gpu(project.platform, project.memory);
        if (false == gpu.ready)
        {
            std::cout << "  [skip] no D3D12 device; split screen not verified" << std::endl;
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
            // **카메라는 눈이고, 화면의 절반은 캔버스의 뷰포트다**(D-438).
            Array<CanvasViewport> halves;
            const auto makeCamera = [&](const char* name, JBro::Float x, JBro::Float originX) {
                Object::GameObject* object = canvas->CreateObject(name);
                canvas->AttachComponent<Component::Transform2D>(object)->position = {x, 0.0f};
                auto* camera = canvas->AttachComponent<Component::Camera2D>(object);
                camera->orthographicSize = 1.0f;
                camera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
                CanvasViewport half;
                half.name = name;
                half.camera = object->GetInstanceId();
                half.origin = {originX, 0.0f};
                half.size = {0.5f, 1.0f};
                halves.Add(half);
                return camera;
            };
            makeCamera("left", -10.0f, 0.0f);
            auto* rightCamera = makeCamera("right", 10.0f, 0.5f);
            canvas->SetViewports(halves);
            // 오른쪽 카메라만 노출 -2.2 의 프로필(D-361). 파일을 쓰고 다시 스캔해 번호를 받는다.
            {
                PostProcessProfileDocument document;
                PostProcessProfileEntry exposure = MakePostProcessProfileEntry(PostProcessEffects::Exposure);
                reinterpret_cast<ExposureEffect*>(exposure.block.Data())->exposure = -2.2f;
                exposure.overrides = JBro::UInt64(1);
                document.entries.Add(exposure);
                const String yaml = WritePostProcessProfile(document);
                WriteBytes(project.root / "dark.jpost", yaml.data(), yaml.size());
                AssetScanOptions options;
                // 메타가 없는 파일은 등록하지 않는다 - 시험이라 메타를 만들게 한다(에디터와 같다).
                options.createMissingMeta = true;
                AssetScanReport report;
                Check(project.registry.Scan(project.platform, Utf8(project.root).c_str(), options, report), "the profile scans in");
                AssetId profileId;
                for (std::size_t index = 0; index < project.registry.GetCount(); ++index)
                {
                    if (project.registry.GetRecord(index).type == AssetType::PostProcessProfile)
                    {
                        profileId = project.registry.GetRecord(index).id;
                    }
                }
                Check(false == profileId.IsNull(), "the profile has an id");
                rightCamera->postProcessProfileId = profileId;
            }
            const auto makeSquare = [&](const char* name, JBro::Float x, Color tint) {
                Object::GameObject* object = canvas->CreateObject(name);
                canvas->AttachComponent<Component::Transform2D>(object)->position = {x, 0.0f};
                auto* sprite = canvas->AttachComponent<Component::SpriteRenderer2D>(object);
                sprite->sizeMode = Component::SpriteSizeMode::Custom;
                sprite->size = {1.0f, 1.0f};
                sprite->tint = tint;
                return sprite;
            };
            makeSquare("red", -10.0f, {1.0f, 0.0f, 0.0f, 1.0f});
            makeSquare("blue", 10.0f, {0.0f, 0.0f, 1.0f, 1.0f});
            Object::GameObject* lightObject = canvas->CreateObject("ambient");
            canvas->AttachComponent<Component::Transform2D>(lightObject);
            auto* light = canvas->AttachComponent<Component::Light2D>(lightObject);
            light->type = Component::Light2DType::Global;
            light->color = {0.2f, 0.2f, 0.2f, 1.0f};
            Layer& ui = canvas->CreateLayer("UI");
            ui.SetSpace(LayerSpace::Screen);
            Object::GameObject* panel = canvas->CreateObject("panel");
            Check(canvas->SetObjectLayer(panel, ui.GetId()), "the panel goes on the screen layer");
            auto* panelPlace = canvas->AttachComponent<Component::Transform2D>(panel);
            panelPlace->anchor = {1.0f, 1.0f};
            panelPlace->position = {-4.0f, -4.0f};
            auto* panelSprite = canvas->AttachComponent<Component::SpriteRenderer2D>(panel);
            panelSprite->sizeMode = Component::SpriteSizeMode::Custom;
            panelSprite->size = {8.0f, 8.0f};
            framework.BindCanvasAssets();
            gpu.Paint(framework);
            gpu.Paint(framework);
            // 왼쪽 절반은 카메라 A: 가운데(16, 32)에 빨강(어둡게), 오른쪽 절반의 가운데(48, 32)에 파랑(어둡게). 서로의 스프라이트는 보이지 않는다.
            Check(gpu.Is(16, 32, 51, 0, 0), "the left camera shows the red square, darkened by the ambient light");
            // 오른쪽은 제 프로필의 노출(-2.2 EV → 표시값 x 0.5)을 받는다 - 51 → 약 26. 후처리된 뷰는 하나다(D-361).
            Check(gpu.Is(48, 32, 0, 0, 26), "the right camera shows the blue square through its own exposure profile");
            Check(gpu.renderer.GetLastFrameStats().postProcessedViewCount == 1, "only the right camera's view is post-processed");
            // 둘째 뷰의 지우기(D-362): 오른쪽 카메라의 지우기 색을 초록으로 하면 오른쪽 절반의 빈 곳이 초록(노출로 약 128)이고 왼쪽은 그대로 검다. 끄면 앞 뷰의 검정 위에 그린다.
            rightCamera->clearColor = {0.0f, 1.0f, 0.0f, 1.0f};
            gpu.Paint(framework);
            Check(gpu.Is(48, 60, 0, 128, 0) && gpu.Is(40, 4, 0, 128, 0), "the second camera paints its viewport with its own clear colour, through its exposure");
            Check(gpu.Is(16, 4, 0, 0, 0) && gpu.Is(16, 32, 51, 0, 0), "the first camera's half keeps its own clear colour and square");
            Check(gpu.Is(48, 32, 0, 0, 26), "the square still draws over the fill and the fill takes no light");
            Check(gpu.Is(60, 4, 255, 255, 255), "the screen layer stays on top");
            canvas->GetViewportAt(1)->clearViewport = false;
            gpu.Paint(framework);
            Check(gpu.Is(48, 60, 0, 0, 0), "with clearViewport off the second camera draws over the first view's clear");
            Check(gpu.Is(16, 4, 0, 0, 0) && gpu.Is(48, 60, 0, 0, 0), "away from the squares both halves are clear colour");
            // 화면 레이어는 대상 전체이고 빛을 받지 않는다 - 오른쪽 위 8 x 8 이 희다.
            Check(gpu.Is(60, 4, 255, 255, 255) && gpu.Is(57, 7, 255, 255, 255), "the screen-layer square (x 56..63, y 0..7) stays white over the lit world");
            Check(gpu.Is(53, 11, 0, 0, 0) && gpu.Is(50, 14, 0, 0, 0), "just outside it the clear colour shows");
            // **뷰포트의 레이어 필터**(D-438). 왼쪽 뷰포트가 빈 월드 레이어만 그리면 빨강 사각형(기본 레이어)이 빠진다. 화면 레이어는 그대로다.
            Layer& empty = canvas->CreateLayer("Empty");
            canvas->GetViewportAt(0)->layers.Add(empty.GetId());
            gpu.Paint(framework);
            Check(gpu.Is(16, 32, 0, 0, 0), "a viewport that draws only another layer leaves the default layer's square out");
            Check(gpu.Is(48, 32, 0, 0, 26), "the other viewport still draws every layer");
            Check(gpu.Is(60, 4, 255, 255, 255), "and the screen layer is drawn whatever the filter");
            canvas->GetViewportAt(0)->layers.Clear();
            canvas->GetViewportAt(0)->layers.Add(canvas->GetDefaultLayer());
            gpu.Paint(framework);
            Check(gpu.Is(16, 32, 51, 0, 0), "naming the default layer brings the square back");
            framework.Shutdown();
        }
        gpu.Close();
        project.Close();
    }
}

namespace
{
    using namespace JBro;

    // **렌더 텍스처**(D-362). 규칙과 파일: `.jrt` 는 RenderTexture 이고 `renderTextureId` 가 그것을 가리킨다. 크기는 1..4096 으로 갇히고 필터는 Nearest·Linear 다.
    // 그림(D3D12, 64 x 64): 카메라 C(월드 (100, 0), 반높이 1)가 32 x 32 렌더 텍스처에 초록 바탕의 빨강 1 x 1 사각형을 그리고, 주 카메라(원점, 반높이 1 → 1 유닛 32 픽셀)
    // 앞의 1 x 1 스프라이트가 그 텍스처를 보인다 - 화면 가운데 32 x 32 가 그 그림이고(가운데 빨강, 가장자리 초록) 밖은 주 카메라의 검정이다. C 를 `primary` 로 해도 화면의 주
    // 카메라가 되지 않는다. 파일의 크기를 바꿔 다시 읽으면 타깃을 다시 만든다.
    void TestRenderTexturesShowWhatTheirCameraDrew()
    {
        Check(AssetTypeRules::DetectTypeFromPath("minimap.jrt") == AssetType::RenderTexture, "a .jrt file is a render texture");
        Check(AssetTypeRules::TypeOfIdFieldName("renderTextureId") == AssetType::RenderTexture, "renderTextureId picks render textures");
        RenderTextureSettings read;
        Check(ParseRenderTexture("", 0, read) && read.width == 256 && read.height == 256 && read.filter == TextureFilter::Linear, "an empty file is 256 x 256 linear");
        const char* odd = "Width: 0\nHeight: 99999\nFilter: Nearest\n";
        Check(ParseRenderTexture(odd, std::strlen(odd), read) && read.width == 1 && read.height == RenderTextureSettings::MaxSide && read.filter == TextureFilter::Nearest,
            "sizes clamp to 1..4096 and the filter reads");
        RenderTextureSettings written;
        written.width = 32;
        written.height = 16;
        written.filter = TextureFilter::Nearest;
        const String text = WriteRenderTexture(written);
        Check(ParseRenderTexture(text.data(), text.size(), read) && read.width == 32 && read.height == 16 && read.filter == TextureFilter::Nearest, "writing and reading round-trips");
        const char* broken = "Filter: \"unclosed\n";
        String error;
        Check(false == ParseRenderTexture(broken, std::strlen(broken), read, &error) && false == error.empty(), "broken YAML is refused with a reason");

        SliceProject project;
        project.Open();
        Gpu gpu(project.platform, project.memory);
        if (false == gpu.ready)
        {
            std::cout << "  [skip] no D3D12 device; render textures not verified" << std::endl;
            gpu.Close();
            project.Close();
            return;
        }
        {
            RenderTextureSettings settings;
            settings.width = 32;
            settings.height = 32;
            settings.filter = TextureFilter::Nearest;
            const String body = WriteRenderTexture(settings);
            WriteBytes(project.root / "minimap.jrt", body.data(), body.size());
            AssetScanOptions options;
            options.createMissingMeta = true;
            AssetScanReport report;
            Check(project.registry.Scan(project.platform, Utf8(project.root).c_str(), options, report), "the render texture scans in");
            AssetId targetId;
            for (std::size_t index = 0; index < project.registry.GetCount(); ++index)
            {
                if (project.registry.GetRecord(index).type == AssetType::RenderTexture)
                {
                    targetId = project.registry.GetRecord(index).id;
                }
            }
            Check(false == targetId.IsNull(), "the render texture has an id");

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
            Object::GameObject* mainObject = canvas->CreateObject("main");
            canvas->AttachComponent<Component::Transform2D>(mainObject);
            auto* mainCamera = canvas->AttachComponent<Component::Camera2D>(mainObject);
            mainCamera->orthographicSize = 1.0f;
            mainCamera->clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
            Object::GameObject* minimapObject = canvas->CreateObject("minimap camera");
            canvas->AttachComponent<Component::Transform2D>(minimapObject)->position = {100.0f, 0.0f};
            auto* minimapCamera = canvas->AttachComponent<Component::Camera2D>(minimapObject);
            minimapCamera->orthographicSize = 1.0f;
            minimapCamera->clearColor = {0.0f, 1.0f, 0.0f, 1.0f};
            // 미니맵은 렌더 텍스처에 그리는 뷰포트다(D-438). 화면의 뷰포트는 주 카메라를 정한다.
            {
                Array<CanvasViewport> viewports;
                CanvasViewport screenView;
                screenView.name = "Main";
                screenView.camera = mainObject->GetInstanceId();
                CanvasViewport minimapView;
                minimapView.name = "Minimap";
                minimapView.camera = minimapObject->GetInstanceId();
                minimapView.renderTextureId = targetId;
                viewports.Add(minimapView);
                viewports.Add(screenView);
                canvas->SetViewports(viewports);
            }
            Object::GameObject* square = canvas->CreateObject("square");
            canvas->AttachComponent<Component::Transform2D>(square)->position = {100.0f, 0.0f};
            auto* squareSprite = canvas->AttachComponent<Component::SpriteRenderer2D>(square);
            squareSprite->sizeMode = Component::SpriteSizeMode::Custom;
            squareSprite->size = {1.0f, 1.0f};
            squareSprite->tint = {1.0f, 0.0f, 0.0f, 1.0f};
            Object::GameObject* screenObject = canvas->CreateObject("screen");
            canvas->AttachComponent<Component::Transform2D>(screenObject);
            auto* screenSprite = canvas->AttachComponent<Component::SpriteRenderer2D>(screenObject);
            screenSprite->sizeMode = Component::SpriteSizeMode::Custom;
            screenSprite->size = {1.0f, 1.0f};
            screenSprite->renderTextureId = targetId;
            framework.BindCanvasAssets();
            Check(screenSprite->renderTexture.generation != 0, "the sprite field resolves to the loaded render texture");
            gpu.Paint(framework);
            gpu.Paint(framework);
            // 트랜스폼은 첫 갱신에 계산된다 - 그 뒤에 묻는다.
            RenderCamera2D chosen;
            Check(System::Camera2DSystem::SelectCamera(*canvas, chosen) && chosen.owner == mainObject, "a camera drawing into a texture is never the screen's primary camera");
            Check(gpu.Is(32, 32, 255, 0, 0), "the middle of the screen shows the red square the minimap camera drew");
            Check(gpu.Is(19, 19, 0, 255, 0) && gpu.Is(44, 44, 0, 255, 0), "around it the minimap camera's green clear shows through the sprite");
            Check(gpu.Is(5, 5, 0, 0, 0) && gpu.Is(58, 32, 0, 0, 0), "outside the sprite is the main camera's black - the minimap camera did not draw on the screen");
            // 크기를 바꿔 다시 읽으면 타깃을 다시 만든다: 64 x 64 에서도 같은 그림이다.
            settings.width = 64;
            settings.height = 64;
            const String bigger = WriteRenderTexture(settings);
            WriteBytes(project.root / "minimap.jrt", bigger.data(), bigger.size());
            Check(project.assets.ReloadInPlace(targetId), "the render texture reloads in place");
            gpu.Paint(framework);
            gpu.Paint(framework);
            Check(gpu.Is(32, 32, 255, 0, 0) && gpu.Is(19, 19, 0, 255, 0) && gpu.Is(5, 5, 0, 0, 0), "after the resize the picture is the same");
            // 렌더 텍스처를 떼면 그 뷰포트는 화면에 그리고, 목록의 맨 앞이라 주 카메라가 된다.
            canvas->GetViewportAt(0)->renderTextureId = {};
            framework.BindCanvasAssets();
            gpu.Paint(framework);
            Check(System::Camera2DSystem::SelectCamera(*canvas, chosen) && chosen.owner == minimapObject, "back on the screen the first viewport is the main one");
            // 가운데 스프라이트는 렌더 텍스처를 보이는데, 그 텍스처는 이제 아무도 그리지 않을 뿐 앞의 그림이 남아 있다 - 가장자리만 본다.
            Check(gpu.Is(5, 5, 0, 0, 0) && gpu.Is(58, 32, 0, 0, 0), "and the main viewport after it paints the whole screen with its own black");
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
    TestSplitScreenCamerasAndTheScreenLayerStaysUnlit();
    TestRenderTexturesShowWhatTheirCameraDrew();
    std::cout << "Sprite draw mode tests passed." << std::endl;
    return 0;
}
