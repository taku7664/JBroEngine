#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Platform/WindowsPlatform.h>

#include <cstring>
#include <string_view>
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

    fs::path MakeProbeDirectory()
    {
        // 한글 폴더 이름을 섞는다 - 경로가 UTF-8 로 오가는지 함께 본다.
        const fs::path directory = fs::temp_directory_path() / L"JBroAssetRegistryProbe\u00b7\uc5d0\uc14b";
        fs::remove_all(directory);
        fs::create_directories(directory);
        return directory;
    }

    void WriteFile(const fs::path& path, const char* text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << text;
    }

    JBro::String Utf8(const fs::path& path)
    {
        const std::u8string text = path.u8string();
        return JBro::String(reinterpret_cast<const char*>(text.data()), text.size());
    }

    // **타입 규칙.** 확장자는 부트스트랩이고 이름은 파일에 적히는 것이다.
    void TestTypeRules()
    {
        using namespace JBro::AssetTypeRules;
        Check(DetectTypeFromPath("art/hero.PNG") == JBro::AssetType::Texture, "png is a texture whatever its case");
        Check(DetectTypeFromPath("a.jpeg") == JBro::AssetType::Texture, "jpeg is a texture");
        Check(DetectTypeFromPath("Scenes/level.jcanvas") == JBro::AssetType::Canvas, "jcanvas is a canvas");
        Check(DetectTypeFromPath("x.hlsl") == JBro::AssetType::Shader, "hlsl is a shader");
        Check(DetectTypeFromPath("notes.txt") == JBro::AssetType::Unknown, "txt is nothing this engine knows");
        Check(DetectTypeFromPath("folder.v2/name") == JBro::AssetType::Unknown, "a dot in a folder is not an extension");
        Check(DetectTypeFromPath("noext") == JBro::AssetType::Unknown, "no extension is unknown");
        Check(std::strcmp(GetTypeName(JBro::AssetType::Sprite), "Sprite") == 0, "types are named for the file");
        Check(ParseTypeName("Material") == JBro::AssetType::Material, "and read back by name");
        Check(ParseTypeName("material") == JBro::AssetType::Unknown, "names are exact");
        Check(IsMetaPath("hero.png.jmeta") && false == IsMetaPath("hero.png"), "a meta path ends in .jmeta");
        Check(MakeMetaPath("a/hero.png") == "a/hero.png.jmeta", "the meta sits next to its file under the same name");
    }

    // **메타 파일 왕복.** 이미지는 Sprite 블록을 갖고, 없으면 거절이다.
    void TestMetaFileRoundTrip()
    {
        JBro::AssetMetaFile meta;
        meta.id = JBro::Uuid::FromName("meta/texture");
        meta.type = JBro::AssetType::Texture;
        meta.spriteId = JBro::Uuid::FromName("meta/sprite");
        const JBro::String text = JBro::FormatAssetMetaFile(meta);
        Check(text.find("Type: Texture") != JBro::String::npos, "the type is written by name");
        Check(text.find("Sprite:") != JBro::String::npos, "an image writes its sprite block");

        JBro::AssetMetaFile read;
        JBro::AssetMetaError error;
        Check(JBro::ParseAssetMetaFile(text.c_str(), text.size(), read, error), "the text reads back");
        Check(read.id == meta.id && read.type == meta.type && read.spriteId == meta.spriteId, "whole");

        JBro::AssetMetaFile canvas;
        canvas.id = JBro::Uuid::FromName("meta/canvas");
        canvas.type = JBro::AssetType::Canvas;
        const JBro::String canvasText = JBro::FormatAssetMetaFile(canvas);
        Check(canvasText.find("Sprite:") == JBro::String::npos, "a non-image has no sprite block");
        Check(JBro::ParseAssetMetaFile(canvasText.c_str(), canvasText.size(), read, error) && read.spriteId.IsNull(),
            "and reads back without one");

        JBro::AssetMetaFile untouched;
        const char* noSprite = "Version: 1\nId: 0123456789abcdeffedcba9876543210\nType: Texture\n";
        // **옵션 블록은 왕복한다**(D-120). 있는 블록만 적히고, 읽으면 같은 값이며, 틀린 값은 파일 전체의 실패다.
        JBro::AssetMetaFile withOptions = meta;
        withOptions.hasTextureOptions = true;
        withOptions.textureOptions.filter = JBro::TextureFilter::Linear;
        withOptions.hasSpriteOptions = true;
        withOptions.spriteOptions.sliceType = JBro::SpriteSliceType::CellCount;
        withOptions.spriteOptions.rowCount = 3;
        withOptions.spriteOptions.pixelsPerUnit = 16.0f;
        const JBro::String optionsText = JBro::FormatAssetMetaFile(withOptions);
        Check(optionsText.find("Texture:") != JBro::String::npos && optionsText.find("filter: Linear") != JBro::String::npos
                && optionsText.find("rowCount: 3") != JBro::String::npos,
            "both option blocks are written when present");
        JBro::AssetMetaFile roundTrip;
        Check(JBro::ParseAssetMetaFile(optionsText.c_str(), optionsText.size(), roundTrip, error)
                && roundTrip.hasTextureOptions && roundTrip.textureOptions.filter == JBro::TextureFilter::Linear
                && roundTrip.hasSpriteOptions && roundTrip.spriteOptions.rowCount == 3
                && roundTrip.spriteOptions.pixelsPerUnit == 16.0f && roundTrip.spriteId == meta.spriteId,
            "and read back as the same values");
        Check(false == read.hasTextureOptions && false == read.hasSpriteOptions,
            "a meta without blocks reports none");
        // 쓰기는 읽는 쪽이 거절할 것을 적지 않는다: 이미지인데 스프라이트 아이디가 비면 글자가 없다.
        JBro::AssetMetaFile noSpriteId = meta;
        noSpriteId.spriteId = {};
        JBro::String refusedText;
        Check(false == JBro::FormatAssetMetaFile(noSpriteId, refusedText) && refusedText.empty()
                && JBro::FormatAssetMetaFile(noSpriteId).empty(),
            "an image meta without a sprite id is not written");
        // 이 엔진이 읽는 판은 1 뿐이다. 새 판을 옛 규칙으로 읽지 않는다.
        const char* futureVersion = "Version: 2\nId: 0123456789abcdeffedcba9876543210\nType: Canvas\n";
        Check(false == JBro::ParseAssetMetaFile(futureVersion, std::strlen(futureVersion), untouched, error)
                && error.message.find("version") != JBro::String::npos,
            "a meta from a newer format version is refused and says so");
        JBro::String badOptions = text;
        badOptions.append("Texture:\n  ImportOptions:\n    filter: Blurry\n");
        Check(false == JBro::ParseAssetMetaFile(badOptions.c_str(), badOptions.size(), untouched, error)
                && error.message.find("ImportOptions") != JBro::String::npos,
            "an option value nobody knows fails the whole file and says where");
        // 오류의 줄은 그 블록이 시작한 줄이다. 본문 세 줄(Version·Id·Type) 과 Sprite 블록 두 줄 뒤에 Texture 블록이 온다.
        Check(error.line == 7, "and names the line of the block that failed");

        Check(false == JBro::ParseAssetMetaFile(noSprite, std::strlen(noSprite), untouched, error),
            "an image without a sprite block is refused");
        const char* badId = "Version: 1\nId: 42\nType: Canvas\n";
        Check(false == JBro::ParseAssetMetaFile(badId, std::strlen(badId), untouched, error), "a bare number is not an id");
        const char* badType = "Version: 1\nId: 0123456789abcdeffedcba9876543210\nType: Thing\n";
        Check(false == JBro::ParseAssetMetaFile(badType, std::strlen(badType), untouched, error), "an unknown type is refused");
        const char* extra = "Version: 1\nId: 0123456789abcdeffedcba9876543210\nType: Canvas\nImportOptions:\n  Foo: 1\n";
        Check(JBro::ParseAssetMetaFile(extra, std::strlen(extra), read, error) && read.type == JBro::AssetType::Canvas,
            "keys this reader does not own are skipped, not refused");
        Check(untouched.id.IsNull() && untouched.type == JBro::AssetType::Unknown, "a refused parse leaves the value alone");
    }

    // **스캔.** 메타가 없는 파일에 메타를 만들고, 이미지는 둘로, 숨김 폴더·무시 패턴·모르는 타입·고아 메타는 건너뛴다.
    void TestScanRegistersWhatItShouldAndSkipsTheRest()
    {
        const fs::path root = MakeProbeDirectory();
        WriteFile(root / "hero.png", "not really a png");
        WriteFile(root / "Scenes" / "level.jcanvas", "Version: 1\n");
        WriteFile(root / "notes.txt", "hello");
        WriteFile(root / ".git" / "config", "[core]");
        WriteFile(root / ".hidden" / "secret.png", "png");
        WriteFile(root / "tmp" / "scratch.tmp", "x");
        WriteFile(root / "old.png.jmeta", "Version: 1\nId: 0123456789abcdeffedcba9876543210\nType: Texture\n");
        WriteFile(root / "broken.png", "png");
        WriteFile(root / "broken.png.jmeta", "Version: 1\nId: nope\nType: Texture\n");

        JBro::String patterns[] = {JBro::String("*.tmp")};
        JBro::AssetScanOptions options;
        options.ignorePatterns = JBro::JArrayView<JBro::String>{patterns, 1};
        options.createMissingMeta = true;

        JBro::WindowsPlatform platform;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        JBro::AssetRegistry registry;
        JBro::AssetScanReport report;
        Check(registry.Scan(platform, Utf8(root).c_str(), options, report), "the folder scans");
        Check(report.registered == 3, "a texture, its sprite and a canvas are registered");
        Check(report.metaCreated == 2, "two files got a meta");
        Check(report.unknownType == 1, "the text file has no type");
        Check(report.ignored == 1, "the .tmp file is ignored by pattern");
        Check(report.orphanMeta == 1, "the meta without a file is an orphan");
        Check(report.invalidMeta == 1, "the unreadable meta is counted and its file not registered");
        Check(registry.GetCount() == 3, "and the registry holds exactly those");
        Check(fs::exists(root / "hero.png.jmeta") && fs::exists(root / "Scenes" / "level.jcanvas.jmeta"),
            "the metas sit next to their files");
        Check(false == fs::exists(root / ".hidden" / "secret.png.jmeta"), "nothing inside a hidden folder is touched");
        Check(false == fs::exists(root / "notes.txt.jmeta"), "an unknown type gets no meta");

        // 주인 색인: 이미지의 Sprite 는 Texture 를 주인으로 둔다. 저장은 임시 파일을 남기지 않는다.
        {
            const JBro::AssetRecord* heroTexture = registry.FindByPath("hero.png");
            JBro::Array<JBro::AssetId> owned;
            registry.CollectOwned(heroTexture->id, owned);
            Check(owned.Size() == 1 && registry.Find(owned[0]) != nullptr && registry.Find(owned[0])->owner == heroTexture->id,
                "the owner index hands back the image's sprite");
            Check(false == fs::exists(root / "hero.png.jmeta.tmp"), "the meta save leaves no scratch file behind");
        }
        // 판번호는 바뀔 때마다 오르고, 아무것도 안 하면 그대로다(에디터 목록이 이것으로 다시 모을지 정한다).
        const std::uint64_t scanned = registry.GetRevision();
        Check(scanned != 0 && registry.GetRevision() == scanned, "a scan moves the revision and reading does not");
        const JBro::AssetRecord* hero = registry.FindByPath("hero.png");
        Check(hero != nullptr && hero->type == JBro::AssetType::Texture, "an image is found by path as its texture");
        Check(hero->id.GetVersion() == 4, "a generated id is a random one");
        const JBro::AssetRecord* level = registry.FindByPath("Scenes/level.jcanvas");
        Check(level != nullptr && level->type == JBro::AssetType::Canvas, "paths use forward slashes");
        Check(registry.FindByPath("broken.png") == nullptr, "the file with the broken meta is not registered");

        std::size_t sprites = 0;
        JBro::AssetId spriteId;
        for (std::size_t index = 0; index < registry.GetCount(); ++index)
        {
            const JBro::AssetRecord& record = registry.GetRecord(index);
            if (record.type == JBro::AssetType::Sprite)
            {
                ++sprites;
                spriteId = record.id;
                Check(record.owner == hero->id && record.relativePath == "hero.png", "the sprite points at its texture");
            }
        }
        Check(sprites == 1, "one image, one sprite");
        JBro::AssetMetadata metadata;
        Check(registry.GetMetadata(spriteId, metadata) && metadata.type == JBro::AssetType::Sprite
                && std::string_view(metadata.sourcePath.data, metadata.sourcePath.size) == "hero.png",
            "a sprite is found by id and summarised");

        // 두 번째 스캔은 메타를 읽는다. 아이디가 같아야 캔버스 파일이 계속 그것을 가리킬 수 있다.
        const JBro::AssetId heroId = hero->id;
        JBro::AssetScanOptions readOnly;
        readOnly.ignorePatterns = options.ignorePatterns;
        Check(registry.Scan(platform, Utf8(root).c_str(), readOnly, report), "a second scan runs");
        Check(report.metaCreated == 0 && report.registered == 3, "nothing new is created");
        Check(registry.Find(heroId) != nullptr && registry.Find(spriteId) != nullptr, "ids are stable across scans");
        Check(report.missingMeta == 0, "every typed file already had its meta");

        // 파일을 옮겨도(메타와 함께) 아이디는 산다 - 메타에 경로가 없기 때문이다.
        fs::create_directories(root / "Art");
        fs::rename(root / "hero.png", root / "Art" / "knight.png");
        fs::rename(root / "hero.png.jmeta", root / "Art" / "knight.png.jmeta");
        Check(registry.Scan(platform, Utf8(root).c_str(), readOnly, report), "a scan after a move runs");
        const JBro::AssetRecord* knight = registry.FindByPath("Art/knight.png");
        Check(knight != nullptr && knight->id == heroId, "the moved file keeps its id");
        Check(registry.Find(spriteId)->relativePath == "Art/knight.png", "and so does its sprite");

        // 메타를 만들지 않는 스캔은 메타 없는 파일을 세기만 한다.
        WriteFile(root / "new.png", "png");
        Check(registry.Scan(platform, Utf8(root).c_str(), readOnly, report), "a read-only scan runs");
        Check(report.missingMeta == 1 && registry.FindByPath("new.png") == nullptr,
            "a file without a meta is counted, not registered, when metas are not created");
        Check(false == fs::exists(root / "new.png.jmeta"), "and no file is written");

        // 같은 아이디를 든 두 메타 - 뒤에 온 것은 거절이다.
        JBro::AssetMetaFile copy;
        JBro::AssetMetaError error;
        Check(JBro::LoadAssetMetaFile(platform, Utf8(root / "Art" / "knight.png.jmeta").c_str(), copy, error), "the meta loads");
        WriteFile(root / "new.png", "png");
        Check(JBro::SaveAssetMetaFile(platform, Utf8(root / "new.png.jmeta").c_str(), copy), "a copied meta saves");
        Check(registry.Scan(platform, Utf8(root).c_str(), readOnly, report), "a scan with a duplicate runs");
        Check(report.duplicateId == 1 && registry.GetCount() == 3, "the duplicate is refused and counted");

        // Sprite 아이디만 남의 것인 이미지 - Texture 가 먼저 서고 Sprite 가 막히면 Texture 도 물려야 한다.
        copy.id = JBro::Uuid::FromName("fresh texture");
        Check(JBro::SaveAssetMetaFile(platform, Utf8(root / "new.png.jmeta").c_str(), copy), "a meta with a fresh texture id saves");
        Check(registry.Scan(platform, Utf8(root).c_str(), readOnly, report), "a scan with a duplicate sprite id runs");
        Check(report.duplicateId == 1 && registry.GetCount() == 3 && registry.FindByPath("new.png") == nullptr,
            "an image whose sprite id is taken is not registered by half");
        Check(registry.Find(copy.id) == nullptr, "its texture record is rolled back");

        Check(false == registry.Scan(platform, Utf8(root / "nowhere").c_str(), readOnly, report), "a missing folder is false");
        platform.Shutdown();
        fs::remove_all(root);
    }

    // **표 조작.** 등록·해제·이름 바꾸기가 두 표를 함께 고치는지 본다 - 끝을 당겨 채우는 지우기가 자리를 어긋내기 쉽다.
    void TestRegisterUnregisterAndRename()
    {
        JBro::AssetRegistry registry;
        JBro::AssetRecord a;
        a.id = JBro::Uuid::FromName("a");
        a.type = JBro::AssetType::Canvas;
        a.relativePath = "a.jcanvas";
        JBro::AssetRecord b = a;
        b.id = JBro::Uuid::FromName("b");
        b.relativePath = "b.jcanvas";
        JBro::AssetRecord c = a;
        c.id = JBro::Uuid::FromName("c");
        c.relativePath = "c.jcanvas";
        Check(registry.Register(a) && registry.Register(b) && registry.Register(c), "three register");
        Check(false == registry.Register(a), "the same id again is refused");
        JBro::AssetRecord samePath = a;
        samePath.id = JBro::Uuid::FromName("d");
        Check(false == registry.Register(samePath), "the same path again is refused");
        JBro::AssetRecord nameless = a;
        nameless.id = JBro::Uuid::FromName("e");
        nameless.relativePath.clear();
        Check(false == registry.Register(nameless), "an empty path is refused");
        JBro::AssetRecord untyped = a;
        untyped.id = JBro::Uuid::FromName("f");
        untyped.relativePath = "f.jcanvas";
        untyped.type = JBro::AssetType::Unknown;
        Check(false == registry.Register(untyped), "an unknown type is refused");

        Check(registry.Unregister(a.id), "the first goes");
        Check(registry.Find(a.id) == nullptr && registry.FindByPath("a.jcanvas") == nullptr, "and is gone from both tables");
        Check(registry.Find(c.id) != nullptr && registry.Find(c.id)->relativePath == "c.jcanvas",
            "the record that moved into its slot is still found by id");
        Check(registry.FindByPath("c.jcanvas") == registry.Find(c.id), "and by path");
        Check(registry.FindByPath("b.jcanvas") == registry.Find(b.id), "and the untouched one too");
        Check(false == registry.Unregister(a.id), "removing it twice is false");

        Check(registry.Rename("b.jcanvas", "Scenes/b.jcanvas"), "a rename moves the path");
        Check(registry.FindByPath("b.jcanvas") == nullptr && registry.FindByPath("Scenes/b.jcanvas") == registry.Find(b.id),
            "under the new key only");
        Check(false == registry.Rename("Scenes/b.jcanvas", "c.jcanvas"), "onto an existing path is refused");
        Check(false == registry.Rename("missing", "x"), "from a missing path is refused");

        // 이미지: Texture 를 빼면 Sprite 도 함께, 이름을 바꾸면 둘 다.
        JBro::AssetRecord texture;
        texture.id = JBro::Uuid::FromName("t");
        texture.type = JBro::AssetType::Texture;
        texture.relativePath = "t.png";
        JBro::AssetRecord sprite;
        sprite.id = JBro::Uuid::FromName("s");
        sprite.type = JBro::AssetType::Sprite;
        sprite.relativePath = "t.png";
        sprite.owner = texture.id;
        Check(registry.Register(texture) && registry.Register(sprite), "a texture and its sprite share a path");
        Check(registry.Rename("t.png", "Art/t.png") && registry.Find(sprite.id)->relativePath == "Art/t.png",
            "renaming the texture renames the sprite");
        Check(registry.Unregister(texture.id) && registry.Find(sprite.id) == nullptr,
            "removing the texture removes the sprite");
        Check(registry.GetCount() == 2, "b and c remain");
        registry.Clear();
        Check(registry.GetCount() == 0 && registry.Find(b.id) == nullptr, "clear empties everything");
    }

    void TestIgnorePatterns()
    {
        JBro::String patterns[] = {JBro::String("*.tmp"), JBro::String("~$*"), JBro::String("Raw/*"), JBro::String("")};
        const JBro::JArrayView<JBro::String> view{patterns, 4};
        Check(JBro::AssetRegistry::MatchesIgnorePattern("a/b/scratch.TMP", view), "an extension pattern matches the file name, any case");
        Check(JBro::AssetRegistry::MatchesIgnorePattern("~$lock.png", view), "a prefix pattern matches");
        Check(JBro::AssetRegistry::MatchesIgnorePattern("Raw/deep/x.png", view), "a path pattern matches the relative path");
        Check(false == JBro::AssetRegistry::MatchesIgnorePattern("hero.png", view), "an ordinary file does not");
        Check(false == JBro::AssetRegistry::MatchesIgnorePattern("tmp.png", view), "and the pattern is anchored");
    }
}

int RunAssetRegistryTests()
{
    TestTypeRules();
    TestMetaFileRoundTrip();
    TestIgnorePatterns();
    TestRegisterUnregisterAndRename();
    TestScanRegistersWhatItShouldAndSkipsTheRest();
    std::cout << "Asset registry tests passed.\n";
    return 0;
}
