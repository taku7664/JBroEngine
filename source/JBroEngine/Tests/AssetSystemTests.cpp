#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/ImageDecoder.h>
#include <JBro/Asset/SpriteFrames.h>
#include <JBro/Framework2D/BuiltinComponentProperties2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Platform/WindowsPlatform.h>
#include <JBro/Reflection/PropertyRegistry.h>

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
        view.size = sizeof(TinyPng);
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

    bool PixelIs(const JBro::TextureData& texture, std::uint32_t x, std::uint32_t y,
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
        view.size = sizeof(garbage);
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
        view.size = static_cast<std::uint32_t>(text.size());
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
        Check(false == assets.ReloadInPlace(fixture.canvasId), "an asset that is not loaded is false");

        // 옵션이 읽히지 않으면 실패고 옛 자료가 남는다.
        text.append("    nonsense: 1\n");
        view.data = reinterpret_cast<const std::byte*>(text.data());
        view.size = static_cast<std::uint32_t>(text.size());
        Check(fixture.platform.WriteWholeFile(metaPath.c_str(), view), "a meta with an unknown option key saves");
        Check(false == assets.ReloadInPlace(fixture.spriteId), "and is refused on reload");
        Check(assets.GetSprite(sprite)->frames.Size() == 4, "leaving the previous frames in place");
        Check(assets.Load(fixture.spriteId).generation == sprite.generation, "and the sprite is still the same one");

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

int RunAssetSystemTests()
{
    TestTheDecoderReadsPngAndRefusesGarbage();
    TestSpriteFramesFollowTheImportOptions();
    TestLoadReleaseAndCollect();
    TestReloadInPlaceKeepsHandles();
    TestBindComponentAssetsFillsHandlesFromIds();
    std::cout << "Asset system tests passed.\n";
    return 0;
}
