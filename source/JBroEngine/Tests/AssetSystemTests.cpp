#include "TestCases.h"
#include "TestClock.h"
#include "TestModels.h"
#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetSource.h>
#include <JBro/Asset/ImageDecoder.h>
#include <JBro/Asset/MaterialFile.h>
#include <JBro/Asset/SpriteFrames.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework2D/BuiltinComponentProperties2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Host/AssetLoad.h>
#include <JBro/Host/IFramework.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Task/TaskManager.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

namespace
{
    namespace fs = std::filesystem;

    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    // 2x2 RGBA PNG. 왼쪽 위 빨강, 오른쪽 위 초록, 왼쪽 아래 파랑, 오른쪽 아래 반투명 흰색.
    constexpr unsigned char TinyPng[] =
    {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d,
        0x24, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x1f, 0x0c, 0x81, 0x34, 0x08, 0x34, 0x00, 0x00, 0x49, 0x49, 0x09, 0x78, 0x9c, 0x51, 0x17, 0x92,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
    };

    JBro::JArrayView<std::byte> PngView()
    {
        JBro::JArrayView<std::byte> view;
        view.data = reinterpret_cast<const std::byte*>(TinyPng);
        view.size = static_cast<JBro::UInt32>(sizeof(TinyPng));
        return view;
    }

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

    JBro::Bool PixelIs(const JBro::TextureData& texture, JBro::UInt32 x, JBro::UInt32 y,
        unsigned r, unsigned g, unsigned b, unsigned a)
    {
        const std::size_t offset = (static_cast<std::size_t>(y) * texture.width + x) * 4;
        const unsigned char* p = reinterpret_cast<const unsigned char*>(texture.pixels.Data()) + offset;
        return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
    }

    // **디코더.** 바이트에서 RGBA8 을 얻고, 쓰레기는 거절한다.
    void TestTheDecoderReadsPngAndRefusesGarbage()
    {
        JBro::DecodedImage image;
        Check(JBro::DecodeImage(PngView(), image), "a png decodes");
        Check(image.width == 2 && image.height == 2 && image.pixels.Size() == 16, "to 2x2 RGBA8");
        const unsigned char* p = reinterpret_cast<const unsigned char*>(image.pixels.Data());
        Check(p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255, "top-left is red");
        Check(p[4] == 0 && p[5] == 255 && p[6] == 0 && p[7] == 255, "top-right is green");
        Check(p[8] == 0 && p[9] == 0 && p[10] == 255 && p[11] == 255, "bottom-left is blue");
        Check(p[12] == 255 && p[13] == 255 && p[14] == 255 && p[15] == 128, "bottom-right is half white");

        const char garbage[] = "this is not an image";
        JBro::JArrayView<std::byte> view;
        view.data = reinterpret_cast<const std::byte*>(garbage);
        view.size = static_cast<JBro::UInt32>(sizeof(garbage));
        JBro::DecodedImage untouched;
        untouched.width = 7;
        Check(false == JBro::DecodeImage(view, untouched), "garbage is refused");
        Check(untouched.width == 7, "and the result is left alone");
        Check(false == JBro::DecodeImage({}, untouched), "empty is refused");
    }

    // **프레임 나누기.** 순수 함수라 표로 본다.
    void TestSpriteFramesFollowTheImportOptions()
    {
        JBro::SpriteImportOptions options;
        JBro::Array<JBro::SpriteFrame> frames;
        Check(JBro::BuildSpriteFrames(64, 32, options, frames) && frames.Size() == 1, "None is one frame");
        Check(frames[0].x == 0 && frames[0].y == 0 && frames[0].width == 64 && frames[0].height == 32, "covering the image");
        Check(frames[0].pivotX == 0.5f && frames[0].pivotY == 0.5f, "with the default pivot");

        options.sliceType = JBro::SpriteSliceType::CellCount;
        options.rowCount = 2;
        options.columnCount = 4;
        options.pivotY = 0.0f;
        Check(JBro::BuildSpriteFrames(64, 32, options, frames) && frames.Size() == 8, "2x4 cells are eight frames");
        Check(frames[0].width == 16 && frames[0].height == 16, "each a quarter by a half");
        Check(frames[1].x == 16 && frames[1].y == 0, "row-major: the second is to the right");
        Check(frames[4].x == 0 && frames[4].y == 16, "the fifth starts the second row");
        Check(frames[7].pivotY == 0.0f, "the pivot is carried into every frame");

        options.sliceType = JBro::SpriteSliceType::CellSize;
        options.cellWidth = 10;
        options.cellHeight = 10;
        options.marginX = 2;
        options.marginY = 1;
        options.gapX = 1;
        options.gapY = 0;
        // 폭 64: 여백 2 씩 빼면 60, 셀 10 + 간격 1 → 1 + (60-10)/11 = 5 칸. 높이 32: 여백 1 씩 빼면 30 → 3 칸.
        Check(JBro::BuildSpriteFrames(64, 32, options, frames) && frames.Size() == 15, "cell size with margin and gap");
        Check(frames[0].x == 2 && frames[0].y == 1, "the first cell starts after the margin");
        Check(frames[1].x == 13, "the next is a cell and a gap over");
        Check(frames[5].y == 11, "rows step by the cell height plus the gap");

        JBro::Array<JBro::SpriteFrame> untouched;
        untouched.Resize(3);
        options.cellWidth = 100;
        Check(false == JBro::BuildSpriteFrames(64, 32, options, untouched), "a cell wider than the image is refused");
        Check(untouched.Size() == 3, "and the output is left alone");
        options.cellWidth = 10;
        options.marginX = 40;
        Check(false == JBro::BuildSpriteFrames(64, 32, options, untouched), "margins eating the image are refused");
        options.marginX = 0;
        options.sliceType = JBro::SpriteSliceType::CellCount;
        options.columnCount = 0;
        Check(false == JBro::BuildSpriteFrames(64, 32, options, untouched), "zero columns is refused");
        Check(false == JBro::BuildSpriteFrames(0, 32, JBro::SpriteImportOptions{}, untouched), "an empty image is refused");
    }

    struct Fixture
    {
        fs::path root;
        JBro::WindowsPlatform platform;
        JBro::JMemoryContext memory;
        JBro::AssetRegistry registry;
        JBro::AssetSystem assets;
        JBro::AssetId textureId;
        JBro::AssetId spriteId;
        JBro::AssetId canvasId;

        void Open()
        {
            root = fs::temp_directory_path() / L"JBroAssetSystemProbe·에셋";
            fs::remove_all(root);
            fs::create_directories(root / "Art");
            WriteBytes(root / "Art" / "tiny.png", TinyPng, sizeof(TinyPng));
            const char canvas[] = "Version: 1\n";
            WriteBytes(root / "level.jcanvas", canvas, sizeof(canvas) - 1);
            Check(platform.Initialize(memory), "the platform must initialize");
            JBro::AssetScanOptions options;
            options.createMissingMeta = true;
            JBro::AssetScanReport report;
            Check(registry.Scan(platform, Utf8(root).c_str(), options, report), "the folder scans");
            Check(report.registered == 3, "a texture, a sprite and a canvas");
            const JBro::AssetRecord* texture = registry.FindByPath("Art/tiny.png");
            textureId = texture->id;
            for (std::size_t index = 0; index < registry.GetCount(); ++index)
            {
                if (registry.GetRecord(index).type == JBro::AssetType::Sprite)
                {
                    spriteId = registry.GetRecord(index).id;
                }
            }
            canvasId = registry.FindByPath("level.jcanvas")->id;
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

    // **로드·참조 수·핸들 세대.** 스프라이트가 텍스처를 잡고, 0 이 된 것만 `CollectUnused` 가 내린다.
    void TestLoadReleaseAndCollect()
    {
        Fixture fixture;
        fixture.Open();
        JBro::AssetSystem& assets = fixture.assets;

        Check(assets.Load(JBro::Uuid::FromName("nobody")).generation == 0, "an unknown id is empty");
        Check(assets.Load(fixture.canvasId).generation == 0, "a type this stage cannot load is empty");
        Check(assets.Load(JBro::AssetId{}).generation == 0, "the null id is empty");
        Check(assets.GetLoadedCount() == 0, "and nothing was loaded by those");

        const JBro::AssetHandle texture = assets.Load(fixture.textureId);
        Check(texture.generation != 0, "the texture loads");
        Check(JBro::AssetSystem::GetHandleType(texture) == JBro::AssetType::Texture, "as a texture handle");
        const JBro::TextureData* pixels = assets.GetTexture(texture);
        Check(pixels != nullptr && pixels->width == 2 && pixels->height == 2, "with its size");
        Check(PixelIs(*pixels, 0, 0, 255, 0, 0, 255) && PixelIs(*pixels, 1, 1, 255, 255, 255, 128), "and its pixels");
        Check(assets.GetSprite(texture) == nullptr, "a texture handle is not a sprite");
        Check(assets.GetReferenceCount(texture) == 1, "one reference");
        const JBro::AssetHandle again = assets.Load(fixture.textureId);
        Check(again.index == texture.index && again.generation == texture.generation, "loading again is the same handle");
        Check(assets.GetReferenceCount(texture) == 2, "with two references");
        Check(assets.Find(fixture.textureId).generation == texture.generation, "Find sees it");
        Check(assets.GetReferenceCount(texture) == 2, "without touching the count");

        const JBro::AssetHandle sprite = assets.Load(fixture.spriteId);
        Check(sprite.generation != 0, "the sprite loads");
        const JBro::SpriteData* data = assets.GetSprite(sprite);
        Check(data != nullptr && data->frames.Size() == 1, "with one frame by default");
        Check(data->frames[0].width == 2 && data->frames[0].height == 2, "covering the texture");
        Check(data->texture.generation == texture.generation && data->texture.index == texture.index,
            "and it points at the loaded texture");
        Check(assets.GetReferenceCount(texture) == 3, "which it holds a reference to");
        Check(assets.GetLoadedCount() == 2, "two assets are loaded");

        assets.Release(texture);
        assets.Release(texture);
        Check(assets.GetReferenceCount(texture) == 1, "the sprite's reference remains");
        Check(assets.CollectUnused() == 0, "so nothing is collected");
        assets.Release(sprite);
        Check(assets.IsLoaded(sprite), "a released asset stays until collection");
        Check(assets.CollectUnused() == 2, "collecting frees the sprite and then its texture");
        Check(false == assets.IsLoaded(sprite) && false == assets.IsLoaded(texture), "both handles are dead");
        Check(assets.GetTexture(texture) == nullptr && assets.GetSprite(sprite) == nullptr, "and answer nothing");
        Check(assets.GetLoadedCount() == 0, "the table is empty");

        const JBro::AssetHandle reloaded = assets.Load(fixture.textureId);
        Check(reloaded.index == texture.index && reloaded.generation != texture.generation,
            "the slot is reused with a new generation, so the old handle stays dead");
        Check(assets.GetTexture(texture) == nullptr && assets.GetTexture(reloaded) != nullptr, "old no, new yes");
        assets.Release(reloaded);
        assets.Release(reloaded);
        Check(assets.GetReferenceCount(reloaded) == 0, "releasing below zero stops at zero");
        fixture.Close();
    }

    // **in-place 재로드와 임포트 옵션.** 핸들은 같고 자료만 바뀐다. 옵션은 메타의 `Sprite.ImportOptions` 에서 온다.
    void TestReloadInPlaceKeepsHandles()
    {
        Fixture fixture;
        fixture.Open();
        JBro::AssetSystem& assets = fixture.assets;

        const JBro::AssetHandle sprite = assets.Load(fixture.spriteId);
        const JBro::AssetHandle texture = assets.GetSprite(sprite)->texture;
        Check(assets.GetTexture(texture)->pixelGeneration == 1, "a fresh texture is generation one");

        // 메타에 옵션을 적어 넣고 다시 읽는다 - 옵션은 리플렉션 표의 필드 이름이 키다.
        JBro::AssetMetaFile meta;
        JBro::AssetMetaError error;
        const JBro::String metaPath = Utf8(fixture.root / "Art" / "tiny.png.jmeta");
        Check(JBro::LoadAssetMetaFile(fixture.platform, metaPath.c_str(), meta, error), "the meta loads");
        JBro::String text = JBro::FormatAssetMetaFile(meta);
        text.append("  ImportOptions:\n    sliceType: CellCount\n    rowCount: 2\n    columnCount: 2\n    pivotX: 0\n");
        JBro::JArrayView<std::byte> view;
        view.data = reinterpret_cast<const std::byte*>(text.data());
        view.size = static_cast<JBro::UInt32>(text.size());
        Check(fixture.platform.WriteWholeFile(metaPath.c_str(), view), "the meta with options saves");

        Check(assets.ReloadInPlace(fixture.spriteId), "the sprite reloads in place");
        const JBro::SpriteData* data = assets.GetSprite(sprite);
        Check(data != nullptr, "under the same handle");
        Check(data->frames.Size() == 4 && data->frames[3].x == 1 && data->frames[3].y == 1, "now four one-pixel frames");
        Check(data->options.sliceType == JBro::SpriteSliceType::CellCount && data->frames[0].pivotX == 0.0f,
            "with the options the meta gave");

        Check(assets.ReloadInPlace(fixture.textureId), "the texture reloads in place");
        Check(assets.GetTexture(texture) != nullptr && assets.GetTexture(texture)->pixelGeneration == 2,
            "the same handle, one generation later");

        // **텍스처의 샘플러**(D-117). 메타의 `Texture.ImportOptions.filter` 가 프로젝트 기본을 덮어쓰고, `Default`
        // 는 프로젝트 기본을 받는다. 프로젝트 기본은 로드·재로드 때 적용된다.
        Check(assets.GetTexture(texture)->filter == JBro::TextureFilter::Nearest
                && assets.GetTexture(texture)->options.filter == JBro::TextureFilter::Default,
            "a meta with no texture block is Default, which is the project's Nearest");
        JBro::String textureText = JBro::FormatAssetMetaFile(meta);
        textureText.append("Texture:\n  ImportOptions:\n    filter: Linear\n");
        view.data = reinterpret_cast<const std::byte*>(textureText.data());
        view.size = static_cast<JBro::UInt32>(textureText.size());
        Check(fixture.platform.WriteWholeFile(metaPath.c_str(), view), "the meta with a texture filter saves");
        Check(assets.ReloadInPlace(fixture.textureId) && assets.GetTexture(texture)->filter == JBro::TextureFilter::Linear,
            "the texture's own filter wins");
        assets.SetDefaultTextureFilter(JBro::TextureFilter::Linear);
        textureText = JBro::FormatAssetMetaFile(meta);
        textureText.append("Texture:\n  ImportOptions:\n    filter: Default\n");
        view.data = reinterpret_cast<const std::byte*>(textureText.data());
        view.size = static_cast<JBro::UInt32>(textureText.size());
        Check(fixture.platform.WriteWholeFile(metaPath.c_str(), view), "the meta with Default saves");
        Check(assets.ReloadInPlace(fixture.textureId) && assets.GetTexture(texture)->filter == JBro::TextureFilter::Linear
                && assets.GetTexture(texture)->options.filter == JBro::TextureFilter::Default,
            "Default takes the project's filter, which is now Linear");
        assets.SetDefaultTextureFilter(JBro::TextureFilter::Default);
        Check(assets.GetDefaultTextureFilter() == JBro::TextureFilter::Nearest, "a project default of Default is Nearest");
        Check(assets.ReloadInPlace(fixture.textureId) && assets.GetTexture(texture)->filter == JBro::TextureFilter::Nearest,
            "and the reload follows it");
        textureText = JBro::FormatAssetMetaFile(meta);
        textureText.append("Texture:\n  ImportOptions:\n    filter: Blurry\n");
        view.data = reinterpret_cast<const std::byte*>(textureText.data());
        view.size = static_cast<JBro::UInt32>(textureText.size());
        Check(fixture.platform.WriteWholeFile(metaPath.c_str(), view), "the meta with a bad filter saves");
        Check(false == assets.ReloadInPlace(fixture.textureId), "a filter name nobody knows is refused");
        Check(assets.GetTexture(texture)->filter == JBro::TextureFilter::Nearest, "leaving the previous data");
        Check(false == assets.ReloadInPlace(fixture.canvasId), "an asset that is not loaded is false");

        // 옵션이 읽히지 않으면 실패고 옛 자료가 남는다.
        text.append("    nonsense: 1\n");
        view.data = reinterpret_cast<const std::byte*>(text.data());
        view.size = static_cast<JBro::UInt32>(text.size());
        Check(fixture.platform.WriteWholeFile(metaPath.c_str(), view), "a meta with an unknown option key saves");
        Check(false == assets.ReloadInPlace(fixture.spriteId), "and is refused on reload");

        Check(assets.GetSprite(sprite)->frames.Size() == 4, "leaving the previous frames in place");
        Check(assets.Load(fixture.spriteId).generation == sprite.generation, "and the sprite is still the same one");

        // 0 이하의 PPU 는 읽히긴 하되 기본값으로 바로잡힌다(D-119) - 프레임 경로가 0 으로 나누지 않게.
        JBro::String badPpu = JBro::FormatAssetMetaFile(meta);
        badPpu.append("  ImportOptions:\n    pixelsPerUnit: -5\n");
        view.data = reinterpret_cast<const std::byte*>(badPpu.data());
        view.size = static_cast<JBro::UInt32>(badPpu.size());
        Check(fixture.platform.WriteWholeFile(metaPath.c_str(), view), "a meta with a negative PPU saves");
        Check(assets.ReloadInPlace(fixture.spriteId)
                && assets.GetSprite(sprite)->options.pixelsPerUnit == JBro::DefaultPixelsPerUnit,
            "a non-positive pixels per unit is repaired to the default on load");
        view.data = reinterpret_cast<const std::byte*>(text.data());
        view.size = static_cast<JBro::UInt32>(text.size());
        Check(fixture.platform.WriteWholeFile(metaPath.c_str(), view), "the meta with the unknown key is put back");

        // 메타는 이미지마다 한 번 파싱돼 캐시된다. 내려간 뒤의 로드는 디스크를 다시 본다 - 손으로 고친 메타가 반영되게.
        {
            JBro::String rows = JBro::FormatAssetMetaFile(meta);
            rows.append("  ImportOptions:\n    sliceType: CellCount\n    rowCount: 2\n    columnCount: 1\n");
            view.data = reinterpret_cast<const std::byte*>(rows.data());
            view.size = static_cast<JBro::UInt32>(rows.size());
            const std::size_t framesBefore = assets.GetSprite(sprite)->frames.Size();
            Check(fixture.platform.WriteWholeFile(metaPath.c_str(), view), "a meta edited by hand saves");
            Check(assets.GetSprite(sprite)->frames.Size() == framesBefore, "the loaded sprite does not see it yet");
            assets.Release(sprite);
            assets.Release(sprite);
            Check(assets.CollectUnused() >= 1, "the sprite goes when nobody holds it");
            const JBro::AssetHandle again = assets.Load(fixture.spriteId);
            Check(again.generation != 0 && assets.GetSprite(again)->frames.Size() == 2,
                "a fresh load after unloading reads the meta from disk again");
            assets.Release(again);
            assets.CollectUnused();
            view.data = reinterpret_cast<const std::byte*>(text.data());
            view.size = static_cast<JBro::UInt32>(text.size());
            Check(fixture.platform.WriteWholeFile(metaPath.c_str(), view), "the meta with the unknown key is put back again");
        }

        // 새 로드는 그 옵션으로 시작한다 - 다시 열면 거절돼야 하므로 먼저 전부 내린다.
        assets.Release(sprite);
        assets.Release(sprite);
        assets.CollectUnused();
        Check(assets.Load(fixture.spriteId).generation == 0, "a sprite whose options do not read does not load");
        fixture.Close();
    }

    // **해석 패스.** `spriteId` 가 `sprite` 를 채우고, 빈 아이디는 핸들을 비운다.
    void TestBindComponentAssetsFillsHandlesFromIds()
    {
        Fixture fixture;
        fixture.Open();
        JBro::AssetSystem& assets = fixture.assets;
        JBro::Component::RegisterBuiltinComponentProperties2D();
        const JBro::PropertyTable* table = JBro::PropertyRegistry::Lookup(
            JBro::NameTable::Get().Intern("Component::SpriteRenderer2D"));
        Check(table != nullptr, "the sprite renderer has a property table");

        JBro::Component::SpriteRenderer2D renderer;
        renderer.spriteId = fixture.spriteId;
        renderer.sprite = JBro::AssetHandle{99, 99};
        renderer.materialId = JBro::AssetId{};
        renderer.material = JBro::AssetHandle{5, 5};
        JBro::Array<JBro::AssetHandle> acquired;
        Check(assets.BindComponentAssets(*table, &renderer, acquired) == 1, "one field is bound");
        Check(renderer.sprite.generation != 0 && assets.GetSprite(renderer.sprite) != nullptr, "sprite got its handle");
        Check(renderer.material.generation == 0 && renderer.material.index == 0, "an empty id clears the stale handle");
        Check(acquired.Size() == 1 && acquired[0].generation == renderer.sprite.generation, "the handle is remembered");
        Check(assets.GetReferenceCount(renderer.sprite) == 1, "held once");

        renderer.spriteId = JBro::Uuid::FromName("missing");
        Check(assets.BindComponentAssets(*table, &renderer, acquired) == 0, "an unknown id binds nothing");
        Check(renderer.sprite.generation == 0, "and clears the handle");

        assets.ReleaseAll(acquired);
        Check(acquired.IsEmpty(), "the list empties");
        Check(assets.CollectUnused() == 2, "and the sprite and its texture can go");
        fixture.Close();
    }
}

namespace
{
    // 프로퍼티 표를 등록하지 않은 컴포넌트다. 해석 패스는 이것을 건너뛰어야 한다 - 표 없이 걸으면 null 을 읽는다.
    class Unlisted final : public JBro::ComponentBase
    {
    public:
        static constexpr const char* StaticTypeName()
        {
            return "Component::TestUnlistedForAssets";
        }

        JBro::ComponentTypeId GetTypeId() const override
        {
            return JBro::MakeStableTypeId(StaticTypeName());
        }
    };

    // **프레임워크가 캔버스의 에셋을 푼다(D-115).** 에디터와 게임 호스트가 같은 것을 부른다. 다시 풀면 앞 것을 놓고,
    // 종료하면 전부 놓아 수집할 수 있다.
    void TestTheFrameworkBindsItsCanvasAssets()
    {
        Fixture fixture;
        fixture.Open();
        JBro::Framework2D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.memory = fixture.memory;
        context.assets = &fixture.assets;
        Check(framework.Initialize(context), "a framework without a renderer initializes");
        JBro::Canvas* canvas = framework.GetCanvas();
        JBro::Object::GameObject* object = canvas->CreateObject("hero");
        auto* sprite = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(object);
        sprite->spriteId = fixture.spriteId;
        JBro::Object::GameObject* other = canvas->CreateObject("ghost");
        auto* missing = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(other);
        missing->spriteId = JBro::Uuid::FromName("nobody");
        missing->sprite = JBro::AssetHandle{3, 3};
        Check(canvas->AttachComponent<Unlisted>(object) != nullptr, "a component without a property table attaches");

        framework.BindCanvasAssets();
        Check(sprite->sprite.generation != 0 && fixture.assets.GetSprite(sprite->sprite) != nullptr, "the sprite is resolved");
        Check(fixture.assets.GetReferenceCount(sprite->sprite) == 1, "and held once");
        Check(missing->sprite.generation == 0, "an id nobody knows clears the stale handle");
        Check(fixture.assets.GetLoadedCount() == 2, "the sprite and its texture are loaded");

        const JBro::AssetHandle first = sprite->sprite;
        framework.BindCanvasAssets();
        Check(sprite->sprite.generation == first.generation && fixture.assets.GetReferenceCount(first) == 1,
            "binding again releases the previous hold, so the count stays one");

        JBro::IFramework& abstract = framework;
        abstract.BindCanvasAssets();
        Check(fixture.assets.GetReferenceCount(first) == 1, "the same through the host's interface");

        framework.Shutdown();
        Check(fixture.assets.GetReferenceCount(first) == 0, "shutting down releases the hold");
        Check(fixture.assets.CollectUnused() == 2, "so the sprite and its texture can be collected");
        fixture.Close();
    }

    JBro::TaskManagerDesc LoadWorkers()
    {
        JBro::TaskManagerDesc desc;
        desc.workerCount = 2;
        return desc;
    }

    JBro::TaskManagerDesc LoadOnMainThread()
    {
        JBro::TaskManagerDesc desc;
        desc.useWorkers = false;
        return desc;
    }

    // **캔버스를 열 때의 워커 로드**(D-236). 아이디를 모으고, 워커가 주인 텍스처를 디코드하고, 메인이 풀에 넣은 뒤
    // 바인딩은 실린 것을 찾아 참조만 올린다. 모으기는 겹친 것과 빈 아이디를 뺀다.
    void TestTheCanvasAssetsLoadOnWorkersThenBind()
    {
        Fixture fixture;
        fixture.Open();
        JBro::Framework2D framework;
        JBro::FrameworkContext context;
        JBro::Testing::AttachClock(context);
        context.memory = fixture.memory;
        context.assets = &fixture.assets;
        Check(framework.Initialize(context), "a framework without a renderer initializes");
        JBro::Canvas* canvas = framework.GetCanvas();
        auto* a = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(canvas->CreateObject("a"));
        auto* b = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(canvas->CreateObject("b"));
        auto* none = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(canvas->CreateObject("none"));
        auto* ghost = canvas->AttachComponent<JBro::Component::SpriteRenderer2D>(canvas->CreateObject("ghost"));
        a->spriteId = fixture.spriteId;
        b->spriteId = fixture.spriteId;
        none->spriteId = JBro::AssetId{};
        ghost->spriteId = JBro::Uuid::FromName("nobody");

        JBro::Array<JBro::AssetId> ids;
        framework.CollectCanvasAssetIds(ids);
        Check(ids.Size() == 2 && ids.Contains(fixture.spriteId) && ids.Contains(ghost->spriteId),
            "collecting keeps each id once and leaves out empty ids");
        Check(a->sprite.generation == 0 && fixture.assets.GetLoadedCount() == 0, "collecting neither loads nor binds");

        JBro::TaskManager tasks;
        Check(tasks.Initialize(LoadWorkers()), "the task manager starts");
        // 프로젝트 기본을 기본값(Nearest)과 다르게 둔다. 풀에 넣을 때 적용되는지가 드러난다.
        fixture.assets.SetDefaultTextureFilter(JBro::TextureFilter::Linear);
        JBro::AssetLoadResult result;
        const JBro::TaskGroupId id = JBro::SubmitAssetLoad(tasks, fixture.assets,
            JBro::ArrayView<const JBro::AssetId>(ids.Data(), ids.Size()), "editor.task.load_canvas", result);
        Check(id != JBro::InvalidTaskGroupId, "the load is submitted");
        Check(fixture.assets.GetLoadedCount() == 0, "submitting loads nothing on the main thread");
        const JBro::TaskGroup* group = tasks.FindGroup(id);
        Check(group != nullptr && group->GetTaskCount() == 1, "the sprite sends its owner texture, and the unknown id sends nothing");
        Check(group->GetTaskAt(0).GetName() == "tiny.png", "a load task is named after its file");
        Check(group->GetName() == "editor.task.load_canvas", "the group carries its localization key");
        Check(tasks.Wait(id), "waiting on the load works");
        Check(result.finished && false == result.canceled && result.failed == 0, "the load finished without failures");
        Check(result.held.Size() == 1, "the load holds the texture it put in the pool");
        const JBro::AssetHandle texture = result.held[0];
        Check(fixture.assets.Find(fixture.textureId).generation == texture.generation, "the texture is in the pool");
        const JBro::TextureData* data = fixture.assets.GetTexture(texture);
        Check(data != nullptr && data->width == 2 && data->height == 2 && PixelIs(*data, 0, 0, 255, 0, 0, 255),
            "the worker decoded the pixels");
        Check(data->filter == JBro::TextureFilter::Linear, "the project default filter is applied when adopted");

        framework.BindCanvasAssets();
        Check(a->sprite.generation != 0 && b->sprite.generation == a->sprite.generation, "binding resolves both sprites");
        Check(fixture.assets.GetReferenceCount(texture) == 2, "the texture is held by the load and by the sprite");
        fixture.assets.ReleaseAll(result.held);
        Check(fixture.assets.GetReferenceCount(texture) == 1, "releasing the load's hold leaves the sprite's");
        Check(fixture.assets.GetLoadedCount() == 2 && fixture.assets.CollectUnused() == 0, "nothing is collected while bound");

        // 다시 보내면 이미 실린 것이라 보낼 것이 없다. 빈 묶음도 끝난다.
        JBro::AssetLoadResult again;
        const JBro::TaskGroupId second = JBro::SubmitAssetLoad(tasks, fixture.assets,
            JBro::ArrayView<const JBro::AssetId>(ids.Data(), ids.Size()), "editor.task.load_canvas", again);
        Check(tasks.FindGroup(second)->GetTaskCount() == 0, "what is already loaded is not sent again");
        tasks.Update();
        Check(again.finished && again.held.IsEmpty(), "an empty load finishes on the next Update");
        tasks.Shutdown();
        framework.Shutdown();
        fixture.Close();
    }

    // 스프라이트와 그 주인 텍스처를 함께 보내도 디코드는 한 번이다.
    void TestASpriteAndItsTextureDecodeOnce()
    {
        Fixture fixture;
        fixture.Open();
        JBro::TaskManager tasks;
        Check(tasks.Initialize(LoadOnMainThread()), "the task manager starts");
        const JBro::AssetId ids[] = { fixture.spriteId, fixture.textureId };
        JBro::AssetLoadResult result;
        const JBro::TaskGroupId id = JBro::SubmitAssetLoad(tasks, fixture.assets,
            JBro::ArrayView<const JBro::AssetId>(ids, 2), "editor.task.load_canvas", result);
        Check(tasks.FindGroup(id)->GetTaskCount() == 1, "a sprite and its own texture send one decode");
        Check(tasks.Wait(id) && result.held.Size() == 1, "and hold one texture");
        fixture.assets.ReleaseAll(result.held);
        tasks.Shutdown();
        fixture.Close();
    }

    // 폴더 소스에 넘기되 워커에서 읽을 수 있다고 말하지 않는 소스다(패키지의 모양, D-232).
    class SerialSource final : public JBro::IAssetSource
    {
    public:
        explicit SerialSource(const JBro::LooseAssetSource& inner)
            : m_inner(inner)
        {
        }

        JBro::Bool Read(const JBro::AssetRecord& record, JBro::AssetBlob blob, JBro::Array<std::byte>& out) const override
        {
            return m_inner.Read(record, blob, out);
        }

        JBro::Bool Has(const JBro::AssetRecord& record, JBro::AssetBlob blob) const override
        {
            return m_inner.Has(record, blob);
        }

        JBro::String MakeStreamPath(const JBro::AssetRecord& record) const override
        {
            return m_inner.MakeStreamPath(record);
        }

        JBro::OwnerPtr<JBro::IFileStream> OpenStream(const char* streamPath) const override
        {
            return m_inner.OpenStream(streamPath);
        }

    private:
        const JBro::LooseAssetSource& m_inner;
    };

    // 워커에서 읽을 수 없는 소스면 워커로 보내지 않고, 바인딩이 동기로 싣는다(D-236). 폴더 소스는 워커에서 읽는다.
    void TestASourceThatCannotReadOnWorkersLoadsInPlace()
    {
        Fixture fixture;
        fixture.Open();
        JBro::LooseAssetSource loose;
        loose.Bind(&fixture.platform, Utf8(fixture.root).c_str());
        Check(loose.CanReadOnWorkers(), "the folder source reads on workers");
        SerialSource serial(loose);
        Check(false == serial.CanReadOnWorkers(), "a source says nothing about workers by default, so it does not");
        fixture.assets.Bind(fixture.platform, fixture.registry, serial);
        JBro::TaskManager tasks;
        Check(tasks.Initialize(LoadOnMainThread()), "the task manager starts");
        JBro::AssetLoadResult result;
        const JBro::AssetId ids[] = { fixture.spriteId };
        const JBro::TaskGroupId id = JBro::SubmitAssetLoad(tasks, fixture.assets,
            JBro::ArrayView<const JBro::AssetId>(ids, 1), "editor.task.load_canvas", result);
        Check(tasks.FindGroup(id) != nullptr && tasks.FindGroup(id)->GetTaskCount() == 0,
            "nothing goes to a worker from a source that cannot be read there");
        tasks.Update();
        Check(result.finished && result.held.IsEmpty(), "the empty load finishes and holds nothing");
        const JBro::AssetHandle sprite = fixture.assets.Load(fixture.spriteId);
        Check(sprite.generation != 0 && fixture.assets.GetSprite(sprite) != nullptr, "the sync load still reads it");
        fixture.assets.Release(sprite);
        tasks.Shutdown();
        fixture.Close();
    }

    // 읽지 못한 파일은 실패로 세고 사유를 남기며 풀에 넣지 않는다.
    void TestAFailedDecodeIsCountedAndNotAdopted()
    {
        Fixture fixture;
        fixture.Open();
        const char garbage[] = "not a png";
        WriteBytes(fixture.root / "Art" / "tiny.png", garbage, sizeof(garbage) - 1);
        JBro::TaskManager tasks;
        Check(tasks.Initialize(LoadWorkers()), "the task manager starts");
        JBro::AssetLoadResult result;
        const JBro::AssetId ids[] = { fixture.textureId };
        const JBro::TaskGroupId id = JBro::SubmitAssetLoad(tasks, fixture.assets,
            JBro::ArrayView<const JBro::AssetId>(ids, 1), "editor.task.load_canvas", result);
        Check(tasks.Wait(id), "waiting works");
        Check(result.finished && result.failed == 1 && result.held.IsEmpty(), "the bad file is one failure and nothing is held");
        Check(result.firstFailure.find("tiny.png") != JBro::String::npos, "the reason names the file");
        Check(tasks.FindGroup(id)->GetTaskAt(0).GetState() == JBro::TaskState::Failed, "the task reads Failed");
        Check(fixture.assets.GetLoadedCount() == 0, "a failed decode is not put in the pool");
        tasks.Shutdown();
        fixture.Close();
    }

    // 도는 사이 동기 로드가 먼저 실었으면 두 벌을 만들지 않고 그 핸들에 참조를 더한다. 취소한 로드는 아무것도 싣지 않는다.
    void TestTheLoadYieldsToASyncLoadAndCancels()
    {
        Fixture fixture;
        fixture.Open();
        JBro::TaskManager tasks;
        Check(tasks.Initialize(LoadOnMainThread()), "the task manager starts without workers");
        const JBro::AssetId ids[] = { fixture.textureId };
        JBro::AssetLoadResult raced;
        JBro::SubmitAssetLoad(tasks, fixture.assets,
            JBro::ArrayView<const JBro::AssetId>(ids, 1), "editor.task.load_canvas", raced);
        const JBro::AssetHandle loaded = fixture.assets.Load(fixture.textureId);
        tasks.Update();
        Check(raced.finished && raced.held.Size() == 1, "the load finishes holding one reference");
        Check(raced.held[0].generation == loaded.generation && raced.held[0].index == loaded.index,
            "it holds the handle the sync load made");
        Check(fixture.assets.GetLoadedCount() == 1 && fixture.assets.GetReferenceCount(loaded) == 2,
            "one texture, held twice - not two copies");
        fixture.assets.ReleaseAll(raced.held);
        fixture.assets.Release(loaded);
        fixture.assets.CollectUnused();

        JBro::AssetLoadResult canceled;
        const JBro::TaskGroupId id = JBro::SubmitAssetLoad(tasks, fixture.assets,
            JBro::ArrayView<const JBro::AssetId>(ids, 1), "editor.task.load_canvas", canceled);
        tasks.FindGroup(id)->RequestCancel();
        Check(tasks.Wait(id), "a canceled load still finishes");
        Check(canceled.finished && canceled.canceled && canceled.held.IsEmpty() && canceled.failed == 0,
            "a canceled load reads canceled, holds nothing and counts no failure");
        Check(fixture.assets.GetLoadedCount() == 0, "and loads nothing");
        tasks.Shutdown();

        JBro::TaskManager down;
        JBro::AssetLoadResult refused;
        Check(JBro::SubmitAssetLoad(down, fixture.assets,
            JBro::ArrayView<const JBro::AssetId>(ids, 1), "editor.task.load_canvas", refused) == JBro::InvalidTaskGroupId,
            "a manager that has not started refuses the load");
        Check(refused.finished, "and says it is finished so nobody waits for it");
        fixture.Close();
    }
    // **재질**(material-plan 2 단계). 모델의 하위 Material 은 파일의 값과 그림 칸(내장 그림은 그 하위 Texture, 바깥 그림은 모델 폴더 기준 경로의 Texture)으로 읽히고,
    // 메시는 제 재질을 안다. 사용자의 `.jmat` 은 같은 값을 YAML 로 들고, 고치면 제자리 재로드가 새 값을 읽으며, 모르는 키는 싣지 않는다.
    void TestMaterialsLoadFromModelsAndFiles()
    {
        Fixture fixture;
        fixture.Open();
        const std::string png(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
        const std::string inside = TestModels::SelfContainedQuadGltf("Bricks", png);
        WriteBytes(fixture.root / "Art" / "Props" / "crate.gltf", inside.data(), inside.size());
        // 바깥 그림은 모델 폴더 기준이다 - `../tiny.png` 는 `Art/tiny.png` 다.
        const std::string buffer = TestModels::QuadBuffer();
        const std::string outside = TestModels::QuadGltf("data:application/octet-stream;base64," + TestModels::Base64(buffer), buffer.size(),
            "{\"uri\":\"../tiny.png\"}");
        WriteBytes(fixture.root / "Art" / "Props" / "wall.gltf", outside.data(), outside.size());
        const char obj[] = "mtllib parts.mtl\nv 0 0 0\nv 0 0 1\nv 1 0 1\nv 1 0 0\no Crate\nusemtl Wood\nf 1 2 3\nusemtl Metal\nf 1 3 4\n";
        const char mtl[] = "newmtl Metal\nKd 0.7 0.7 0.7\nnewmtl Wood\nKd 0.6 0.4 0.2\n";
        WriteBytes(fixture.root / "Art" / "Props" / "parts.obj", obj, sizeof(obj) - 1);
        WriteBytes(fixture.root / "Art" / "Props" / "parts.mtl", mtl, sizeof(mtl) - 1);
        const char jmat[] = "Version: 1\nMaterial:\n  shader: Unlit\n  baseColor:\n    - 0.25\n    - 0.5\n    - 1\n    - 1\n  roughness: 0.25\n";
        WriteBytes(fixture.root / "Art" / "glass.jmat", jmat, sizeof(jmat) - 1);
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(fixture.registry.Scan(fixture.platform, Utf8(fixture.root).c_str(), options, report), "the folder scans with models and a material file");

        const auto partOf = [&](const char* path, JBro::AssetType type) {
            const JBro::AssetRecord* model = fixture.registry.FindByPath(path);
            Check(model != nullptr, "the model registers");
            JBro::Array<JBro::AssetId> owned;
            fixture.registry.CollectOwned(model->id, owned);
            for (const JBro::AssetId& id : owned)
            {
                if (fixture.registry.Find(id)->type == type)
                {
                    return id;
                }
            }
            return JBro::AssetId{};
        };
        const JBro::AssetId crateMesh = partOf("Art/Props/crate.gltf", JBro::AssetType::Mesh);
        const JBro::AssetId crateMaterial = partOf("Art/Props/crate.gltf", JBro::AssetType::Material);
        const JBro::AssetId crateImage = partOf("Art/Props/crate.gltf", JBro::AssetType::Texture);
        Check(false == crateMesh.IsNull() && false == crateMaterial.IsNull() && false == crateImage.IsNull(), "the model has a mesh, a material and an image");

        const JBro::AssetHandle meshHandle = fixture.assets.Load(crateMesh);
        const JBro::MeshData* mesh = fixture.assets.GetMesh(meshHandle);
        Check(mesh != nullptr && mesh->material == crateMaterial, "a model's mesh knows the material it uses in the file");
        const JBro::AssetHandle materialHandle = fixture.assets.Load(crateMaterial);
        const JBro::MaterialData* material = fixture.assets.GetMaterial(materialHandle);
        Check(material != nullptr && material->desc.shader == JBro::MaterialShader::Standard, "a model's material loads as a Standard material");
        Check(material->desc.baseColor.R == 1.0f && material->desc.baseColor.G == 0.5f && material->desc.baseColor.B == 0.25f
                && std::fabs(material->desc.metallic.Get() - 0.2f) < 0.0001f && std::fabs(material->desc.roughness.Get() - 0.7f) < 0.0001f,
            "with the file's colour, metallic and roughness");
        Check(material->desc.baseColorTextureId == crateImage && material->desc.normalTextureId.IsNull(),
            "its base colour picture is the model's embedded image, and empty slots stay empty");
        const JBro::MaterialData* wall = fixture.assets.GetMaterial(fixture.assets.Load(partOf("Art/Props/wall.gltf", JBro::AssetType::Material)));
        Check(wall != nullptr && wall->desc.baseColorTextureId == fixture.textureId,
            "a picture outside the model is the Texture of that file, found beside the model");

        // 재질이 둘인 모델: 메시마다 제 재질을 가리키고, 재질은 제 색을 읽는다(번호가 어긋나면 서로 바뀐다).
        {
            const JBro::AssetRecord* parts = fixture.registry.FindByPath("Art/Props/parts.obj");
            Check(parts != nullptr, "the two-material OBJ registers");
            JBro::Array<JBro::AssetId> owned;
            fixture.registry.CollectOwned(parts->id, owned);
            JBro::UInt32 meshes = 0;
            for (const JBro::AssetId& id : owned)
            {
                const JBro::AssetRecord* part = fixture.registry.Find(id);
                if (part->type != JBro::AssetType::Mesh)
                {
                    continue;
                }
                ++meshes;
                const JBro::AssetHandle partHandle = fixture.assets.Load(id);
                const JBro::MeshData* partMesh = fixture.assets.GetMesh(partHandle);
                const JBro::AssetRecord* used = partMesh != nullptr ? fixture.registry.Find(partMesh->material) : nullptr;
                Check(used != nullptr && used->type == JBro::AssetType::Material, "each mesh of the OBJ knows its material");
                // 재질로 나뉜 메시의 이름은 `메시/재질` 이다.
                Check(part->name.size() > used->name.size() && part->name.compare(part->name.size() - used->name.size(), used->name.size(), used->name) == 0,
                    "and it is the material the mesh is named after");
                const JBro::AssetHandle usedHandle = fixture.assets.Load(used->id);
                const JBro::MaterialData* usedMaterial = fixture.assets.GetMaterial(usedHandle);
                const JBro::Float red = used->name == "Wood" ? 0.6f : 0.7f;
                Check(usedMaterial != nullptr && std::fabs((usedMaterial->desc.baseColor.R - red).Get()) < 0.0001f,
                    "whose colour is its own, not the other material's");
                fixture.assets.Release(usedHandle);
                fixture.assets.Release(partHandle);
            }
            Check(meshes == 2, "the OBJ splits into two meshes, one per material");
        }

        // 사용자의 재질 파일.
        const JBro::AssetRecord* glassRecord = fixture.registry.FindByPath("Art/glass.jmat");
        Check(glassRecord != nullptr && glassRecord->type == JBro::AssetType::Material, "a .jmat file is a Material");
        const JBro::AssetId glassId = glassRecord->id;
        const JBro::AssetHandle glassHandle = fixture.assets.Load(glassId);
        const JBro::MaterialData* glass = fixture.assets.GetMaterial(glassHandle);
        Check(glass != nullptr && glass->desc.shader == JBro::MaterialShader::Unlit && glass->desc.baseColor.B == 1.0f
                && glass->desc.roughness == 0.25f && glass->desc.metallic == 0.0f,
            "a material file reads its keys, and the keys it leaves out are defaults");
        const char changed[] = "Version: 1\nMaterial:\n  shader: Standard\n  metallic: 1\n";
        WriteBytes(fixture.root / "Art" / "glass.jmat", changed, sizeof(changed) - 1);
        const JBro::UInt32 before = glass->dataGeneration;
        Check(fixture.assets.ReloadInPlace(glassId), "an edited material reloads in place");
        glass = fixture.assets.GetMaterial(glassHandle);
        Check(glass->desc.shader == JBro::MaterialShader::Standard && glass->desc.metallic == 1.0f && glass->dataGeneration == before + 1,
            "with the new values under a higher generation");

        JBro::MaterialDesc desc;
        desc.shader = JBro::MaterialShader::Unlit;
        desc.alpha = JBro::MaterialAlpha::Cutout;
        desc.alphaCutoff = 0.25f;
        desc.emissive = JBro::Color{0.5f, 0.0f, 0.0f, 1.0f};
        desc.baseColorTextureId = crateImage;
        JBro::String text;
        JBro::MaterialDesc reread;
        JBro::String error;
        Check(JBro::FormatMaterialFile(desc, text) && JBro::ParseMaterialFile(text.c_str(), text.size(), reread, error), "a material file round-trips");
        Check(reread.shader == desc.shader && reread.alpha == desc.alpha && reread.alphaCutoff == desc.alphaCutoff
                && reread.emissive == desc.emissive && reread.baseColorTextureId == desc.baseColorTextureId,
            "with every value");
        // 반투명과 양면(D-443)도 파일의 키다.
        desc.alpha = JBro::MaterialAlpha::Blend;
        desc.doubleSided = true;
        Check(JBro::FormatMaterialFile(desc, text) && JBro::ParseMaterialFile(text.c_str(), text.size(), reread, error)
                && reread.alpha == JBro::MaterialAlpha::Blend && reread.doubleSided,
            "a transparent, double-sided material round-trips");
        const char older[] = "Version: 1\nMaterial:\n  shader: Unlit\n";
        Check(JBro::ParseMaterialFile(older, sizeof(older) - 1, reread, error) && false == reread.doubleSided,
            "a file written before the double-sided key reads as single-sided");
        const char unknown[] = "Version: 1\nMaterial:\n  shininess: 3\n";
        Check(false == JBro::ParseMaterialFile(unknown, sizeof(unknown) - 1, reread, error), "a key the engine does not know is refused");
        Check(false == JBro::ParseMaterialFile("Version: 1\n", 11, reread, error), "a file without a Material block is refused");
        fixture.assets.Release(meshHandle);
        fixture.assets.Release(materialHandle);
        fixture.assets.Release(glassHandle);
        fixture.Close();
    }

    // **같은 그림은 한 번 읽는다**(material-plan 5 단계). 세 모델에 같은 그림이 내장되어 있고 셋째만 밉맵을 껐다. 첫째를 실은 뒤 둘째·셋째 모델 파일의 그림을 깨뜨리면
    // 같은 열쇠의 둘째는 실린 픽셀을 옮겨 받아 모델을 다시 읽지 않고 서지만, 옵션이 달라 열쇠가 다른 셋째는 제 모델을 읽어야 해 서지 못한다.
    void TestTheSameEmbeddedImageIsReadOnce()
    {
        Fixture fixture;
        fixture.Open();
        const std::string png(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
        const std::string gltf = TestModels::SelfContainedQuadGltf("Bricks", png);
        const char* names[] = {"a", "b", "c"};
        for (const char* name : names)
        {
            WriteBytes(fixture.root / "Props" / (std::string(name) + ".gltf"), gltf.data(), gltf.size());
        }
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(fixture.registry.Scan(fixture.platform, Utf8(fixture.root).c_str(), options, report), "the folder scans with three models");
        const auto imageOf = [&](const char* name) {
            const JBro::AssetRecord* model = fixture.registry.FindByPath(std::string("Props/") + name + ".gltf");
            Check(model != nullptr, "each model is registered");
            JBro::Array<JBro::AssetId> owned;
            fixture.registry.CollectOwned(model->id, owned);
            JBro::AssetId image;
            for (const JBro::AssetId& id : owned)
            {
                image = fixture.registry.Find(id)->type == JBro::AssetType::Texture ? id : image;
            }
            Check(false == image.IsNull(), "each model has its embedded image");
            return image;
        };
        // 셋째의 그림만 밉맵을 끈다 - 같은 바이트라도 옵션이 다르면 다른 그림이다.
        {
            const JBro::AssetId third = imageOf("c");
            const JBro::String metaPath = Utf8(fixture.root / "Props" / "c.gltf.jmeta");
            JBro::AssetMetaFile meta;
            JBro::AssetMetaError error;
            Check(JBro::LoadAssetMetaFile(fixture.platform, metaPath.c_str(), meta, error), "the third model's meta reads");
            for (JBro::AssetSubEntry& entry : meta.subAssets)
            {
                if (entry.id == third)
                {
                    entry.hasTextureOptions = true;
                    entry.textureOptions.mipmaps = false;
                }
            }
            Check(JBro::SaveAssetMetaFile(fixture.platform, metaPath.c_str(), meta), "the third image's options are written");
            Check(fixture.registry.Scan(fixture.platform, Utf8(fixture.root).c_str(), options, report), "the folder scans again");
        }
        const JBro::AssetId first = imageOf("a");
        const JBro::AssetId second = imageOf("b");
        const JBro::AssetId third = imageOf("c");
        const JBro::UInt64 key = fixture.registry.Find(first)->sharedImageKey;
        Check(key != 0 && fixture.registry.Find(second)->sharedImageKey == key && fixture.registry.Find(third)->sharedImageKey != key,
            "the same bytes with the same options share a key, other options do not");

        const JBro::AssetHandle firstHandle = fixture.assets.Load(first);
        const JBro::TextureData* firstTexture = fixture.assets.GetTexture(firstHandle);
        Check(firstTexture != nullptr && firstTexture->sharedImageKey == key, "the first image loads and carries its key");
        const std::string broken = TestModels::SelfContainedQuadGltf("Bricks", "not an image at all");
        WriteBytes(fixture.root / "Props" / "b.gltf", broken.data(), broken.size());
        WriteBytes(fixture.root / "Props" / "c.gltf", broken.data(), broken.size());
        const JBro::TextureData* secondTexture = fixture.assets.GetTexture(fixture.assets.Load(second));
        Check(secondTexture != nullptr && secondTexture->width == firstTexture->width && secondTexture->pixels.Size() == firstTexture->pixels.Size()
                && std::memcmp(secondTexture->pixels.Data(), firstTexture->pixels.Data(), firstTexture->pixels.Size()) == 0
                && secondTexture->sharedImageKey == key,
            "the second image takes the loaded pixels and does not read its own model again");
        Check(fixture.assets.Load(third).generation == 0, "an image with other options is not taken for the same one - it reads its own (broken) model");
        fixture.Close();
    }

    // **모델의 내장 그림**(model-import-plan 5 단계). 하위 Texture 는 모델 안의 바이트를 그림 파일과 같은 디코더로 읽고, 옵션은 모델 메타의 제 하위 항목이다.
    // 모델을 읽어야 나오므로 워커로 보내지 않고 동기로 싣는다 - 워커가 모델 파일을 그림으로 풀면 실패한다.
    void TestAModelsEmbeddedImageLoadsAsATexture()
    {
        Fixture fixture;
        fixture.Open();
        const std::string png(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
        const std::string gltf = TestModels::SelfContainedQuadGltf("Bricks", png);
        WriteBytes(fixture.root / "Art" / "crate.gltf", gltf.data(), gltf.size());
        JBro::AssetScanOptions options;
        options.createMissingMeta = true;
        JBro::AssetScanReport report;
        Check(fixture.registry.Scan(fixture.platform, Utf8(fixture.root).c_str(), options, report), "the folder scans with the model");
        const JBro::AssetRecord* model = fixture.registry.FindByPath("Art/crate.gltf");
        Check(model != nullptr && model->type == JBro::AssetType::Model, "the glTF is a model");
        JBro::Array<JBro::AssetId> owned;
        fixture.registry.CollectOwned(model->id, owned);
        JBro::AssetId textureId;
        for (std::size_t index = 0; index < owned.Size(); ++index)
        {
            const JBro::AssetRecord* child = fixture.registry.Find(owned[index]);
            if (child != nullptr && child->type == JBro::AssetType::Texture)
            {
                textureId = child->id;
                Check(child->name == "Bricks", "the embedded image is a Texture named as in the file");
            }
        }
        Check(false == textureId.IsNull(), "the embedded image is a sub-asset of the model");

        // 워커 로드는 그것을 워커로 보내지 않는다(실패로도 세지 않는다).
        {
            JBro::TaskManager tasks;
            Check(tasks.Initialize(LoadWorkers()), "the task manager starts");
            JBro::AssetLoadResult result;
            const JBro::AssetId ids[] = { textureId };
            const JBro::TaskGroupId id = JBro::SubmitAssetLoad(tasks, fixture.assets,
                JBro::ArrayView<const JBro::AssetId>(ids, 1), "editor.task.load_canvas", result);
            Check(tasks.FindGroup(id) != nullptr && tasks.FindGroup(id)->GetTaskCount() == 0,
                "an embedded image is not sent to a worker - it needs its model");
            tasks.Wait(id);
            tasks.Update();
            Check(result.finished && result.failed == 0, "and nothing fails on the way");
            tasks.Shutdown();
        }

        const JBro::AssetHandle handle = fixture.assets.Load(textureId);
        const JBro::TextureData* texture = fixture.assets.GetTexture(handle);
        Check(texture != nullptr && texture->width == 2 && texture->height == 2
                && PixelIs(*texture, 0, 0, 255, 0, 0, 255) && PixelIs(*texture, 1, 0, 0, 255, 0, 255),
            "the embedded PNG decodes to its pixels");
        Check(texture->options.mipmaps, "a model's image keeps mipmaps on by default");

        // 옵션은 하위 항목의 것이다. 고쳐 쓰고 제자리 재로드하면 같은 핸들이 새 옵션으로 읽힌다.
        const JBro::String metaPath = Utf8(fixture.root / "Art" / "crate.gltf.jmeta");
        JBro::AssetMetaFile meta;
        JBro::AssetMetaError error;
        Check(JBro::LoadAssetMetaFile(fixture.platform, metaPath.c_str(), meta, error), "the model's meta reads");
        JBro::Bool found = false;
        for (std::size_t index = 0; index < meta.subAssets.Size(); ++index)
        {
            if (meta.subAssets[index].id == textureId)
            {
                meta.subAssets[index].hasTextureOptions = true;
                meta.subAssets[index].textureOptions.filter = JBro::TextureFilter::Linear;
                meta.subAssets[index].textureOptions.mipmaps = false;
                found = true;
            }
        }
        Check(found && JBro::SaveAssetMetaFile(fixture.platform, metaPath.c_str(), meta), "the image's own options are written");
        Check(fixture.assets.ReloadInPlace(textureId), "the image reloads in place");
        texture = fixture.assets.GetTexture(handle);
        Check(texture != nullptr && texture->options.filter == JBro::TextureFilter::Linear && texture->filter == JBro::TextureFilter::Linear
                && false == texture->options.mipmaps,
            "the reload reads the sub-asset's options, not the model's");
        Check(PixelIs(*texture, 0, 0, 255, 0, 0, 255), "and still the same pixels");
        // 모델 파일이 바뀌면 다시 읽기가 들고 있던 모델을 버리고 파일을 본다 - 그림이 깨졌으면 옛 픽셀로 성공하지 않는다.
        const std::string broken = TestModels::SelfContainedQuadGltf("Bricks", "not an image at all");
        WriteBytes(fixture.root / "Art" / "crate.gltf", broken.data(), broken.size());
        Check(false == fixture.assets.ReloadInPlace(textureId), "a reload after the model file changed reads the file again, not the model it held");
        fixture.assets.Release(handle);
        fixture.Close();
    }

}

JBro::Int32 RunAssetSystemTests()
{
    JBRO_CASE(TestTheFrameworkBindsItsCanvasAssets());
    JBRO_CASE(TestTheDecoderReadsPngAndRefusesGarbage());
    JBRO_CASE(TestSpriteFramesFollowTheImportOptions());
    JBRO_CASE(TestLoadReleaseAndCollect());
    JBRO_CASE(TestReloadInPlaceKeepsHandles());
    JBRO_CASE(TestBindComponentAssetsFillsHandlesFromIds());
    JBRO_CASE(TestTheCanvasAssetsLoadOnWorkersThenBind());
    JBRO_CASE(TestASpriteAndItsTextureDecodeOnce());
    JBRO_CASE(TestAFailedDecodeIsCountedAndNotAdopted());
    JBRO_CASE(TestTheLoadYieldsToASyncLoadAndCancels());
    JBRO_CASE(TestASourceThatCannotReadOnWorkersLoadsInPlace());
    JBRO_CASE(TestAModelsEmbeddedImageLoadsAsATexture());
    JBRO_CASE(TestTheSameEmbeddedImageIsReadOnce());
    JBRO_CASE(TestMaterialsLoadFromModelsAndFiles());
    std::cout << "Asset system tests passed.\n";
    return 0;
}
