#include "TestCases.h"
#include "TestModels.h"
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetTypeRules.h>
#include <JBro/Asset/ModelDecoder.h>
#include <JBro/Asset/ModelImport.h>
#include <JBro/Platform/WindowsPlatform.h>

#include <cstring>
#include <string_view>
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
        // 아이디 필드 이름 → 타입(인스펙터의 고르기가 거른다, D-225).
        using JBro::AssetTypeRules::TypeOfIdFieldName;
        Check(TypeOfIdFieldName("spriteId") == JBro::AssetType::Sprite && TypeOfIdFieldName("fontId") == JBro::AssetType::Font,
            "a field named after a type picks that type");
        Check(TypeOfIdFieldName("clipId") == JBro::AssetType::Audio, "clip is the one alias");
        Check(TypeOfIdFieldName("animationClipId") == JBro::AssetType::AnimationClip && TypeOfIdFieldName("skeletonId") == JBro::AssetType::Skeleton,
            "an animation clip and a skeleton are picked by their whole type names, not the audio alias");
        Check(TypeOfIdFieldName("regularFontId") == JBro::AssetType::Font && TypeOfIdFieldName("boldItalicFontId") == JBro::AssetType::Font,
            "a family slot picks fonts by its last word");
        Check(TypeOfIdFieldName("fontFamilyId") == JBro::AssetType::FontFamily, "the whole name wins over the last word");
        Check(TypeOfIdFieldName("targetId") == JBro::AssetType::Unknown && TypeOfIdFieldName("Id") == JBro::AssetType::Unknown,
            "a name that is no type shows every asset");
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
        const JBro::UInt64 scanned = registry.GetRevision();
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

namespace
{
    // 메시 둘(재질이 둘이라 나뉜다)과 재질 둘인 OBJ 다. 재질 파일은 곁에 둔다.
    const char* const TwoPartObj =
        "mtllib parts.mtl\n"
        "v 0 0 0\nv 0 0 1\nv 1 0 1\nv 1 0 0\nv 0 1 0\n"
        "o Crate\n"
        "usemtl Wood\nf 1 2 3\n"
        "usemtl Metal\nf 1 3 4\n";
    const char* const TwoPartMtl = "newmtl Wood\nKd 0.6 0.4 0.2\nnewmtl Metal\nKd 0.7 0.7 0.7\n";

    JBro::AssetId SubId(const JBro::AssetMetaFile& meta, const char* key)
    {
        for (std::size_t index = 0; index < meta.subAssets.Size(); ++index)
        {
            if (meta.subAssets[index].key == key)
            {
                return meta.subAssets[index].id;
            }
        }
        return {};
    }

    // **모델의 하위 에셋**(D-391, model-import-plan 3 단계). 에디터 스캔이 모델을 읽어 `.jmeta` 의 `Model.SubAssets` 를 채우고, 메시·재질이
    // `owner` 가 모델인 하위 레코드로 이름과 함께 선다. 다시 훑으면 같은 아이디이고, 게임(메타를 만들지 않는 스캔)은 메타의 목록만으로 같은 것을 세운다.
    void TestModelsRegisterTheirSubAssets()
    {
        Check(JBro::AssetTypeRules::DetectTypeFromPath("art/Hero.FBX") == JBro::AssetType::Model
                && JBro::AssetTypeRules::DetectTypeFromPath("a.glb") == JBro::AssetType::Model
                && JBro::AssetTypeRules::DetectTypeFromPath("a.obj") == JBro::AssetType::Model,
            "model files are Model, not Mesh");
        Check(JBro::AssetTypeRules::ParseTypeName("Model") == JBro::AssetType::Model, "and the type has its name");

        const fs::path root = MakeProbeDirectory();
        WriteFile(root / "props" / "crate.obj", TwoPartObj);
        WriteFile(root / "props" / "parts.mtl", TwoPartMtl);
        WriteFile(root / "props" / "broken.fbx", "this is not a model");
        JBro::WindowsPlatform platform;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        JBro::AssetScanOptions editor;
        editor.createMissingMeta = true;
        JBro::AssetRegistry registry;
        JBro::AssetScanReport report;
        Check(registry.Scan(platform, Utf8(root).c_str(), editor, report), "the folder scans");
        Check(report.unreadableModel == 1, "the broken model is counted as unreadable");
        Check(registry.FindByPath("props/broken.fbx") != nullptr, "but it is still registered, so the user can see and fix it");

        const JBro::AssetRecord* crate = registry.FindByPath("props/crate.obj");
        Check(crate != nullptr && crate->type == JBro::AssetType::Model, "the OBJ is a Model record");
        const JBro::AssetId crateId = crate->id;
        JBro::Array<JBro::AssetId> owned;
        registry.CollectOwned(crateId, owned);
        Check(owned.Size() == 4, "two meshes and two materials hang under the model");
        JBro::Int32 meshes = 0;
        JBro::Int32 materials = 0;
        JBro::Bool named = true;
        for (std::size_t index = 0; index < owned.Size(); ++index)
        {
            const JBro::AssetRecord* child = registry.Find(owned[index]);
            Check(child != nullptr && child->owner == crateId && child->relativePath == "props/crate.obj", "each sub-asset shares the model's path");
            meshes += child->type == JBro::AssetType::Mesh ? 1 : 0;
            materials += child->type == JBro::AssetType::Material ? 1 : 0;
            named = named && false == child->name.empty();
        }
        Check(meshes == 2 && materials == 2 && named, "two named meshes and two named materials");

        JBro::AssetMetaFile meta;
        JBro::AssetMetaError metaError;
        Check(JBro::LoadAssetMetaFile(platform, Utf8(root / "props" / "crate.obj.jmeta").c_str(), meta, metaError), "the model's meta reads back");
        Check(meta.type == JBro::AssetType::Model && meta.hasSubAssets && meta.subAssets.Size() == 4, "and lists its sub-assets");
        const JBro::AssetId woodId = SubId(meta, "Material/Wood");
        Check(false == woodId.IsNull() && registry.Find(woodId) != nullptr && registry.Find(woodId)->name == "Wood",
            "a material's key is Material/<name> and its record carries the name");

        // 다시 훑어도 아이디는 그대로다. 게임(메타를 만들지 않는 스캔)도 메타의 목록만으로 같은 레코드를 세운다.
        JBro::AssetScanOptions game;
        JBro::AssetRegistry again;
        Check(again.Scan(platform, Utf8(root).c_str(), game, report), "a read-only scan runs");
        Check(again.Find(woodId) != nullptr && again.Find(woodId)->owner == crateId, "the read-only scan stands the same sub-assets from the meta");

        // 옵션을 바꿔 다시 맞추면 같은 키는 아이디를 지키고, 재질을 끄면 재질 키만 빠진다.
        const JBro::String firstMeshKey = meta.subAssets[0].key;
        const JBro::AssetId firstMeshId = meta.subAssets[0].id;
        meta.hasModelOptions = true;
        meta.modelOptions.importMaterials = false;
        JBro::Bool changed = false;
        JBro::String error;
        Check(JBro::RefreshModelSubAssets(platform, Utf8(root).c_str(), "props/crate.obj", meta, changed, error), "the model refreshes");
        Check(changed && meta.subAssets.Size() == 2 && SubId(meta, "Material/Wood").IsNull(), "importMaterials off drops the material keys");
        Check(SubId(meta, firstMeshKey.c_str()) == firstMeshId, "the mesh keys stay with the same ids - a canvas that uses them keeps working");
        meta.modelOptions.importMaterials = true;
        Check(JBro::RefreshModelSubAssets(platform, Utf8(root).c_str(), "props/crate.obj", meta, changed, error), "the model refreshes again");
        Check(changed && meta.subAssets.Size() == 4, "turning materials back on brings their keys back");
        JBro::Bool unchanged = false;
        Check(JBro::RefreshModelSubAssets(platform, Utf8(root).c_str(), "props/crate.obj", meta, unchanged, error) && false == unchanged,
            "refreshing with nothing changed reports no change");

        // 메타의 모델 블록은 왕복한다.
        JBro::AssetMetaFile reread;
        const JBro::String text = JBro::FormatAssetMetaFile(meta);
        Check(false == text.empty() && JBro::ParseAssetMetaFile(text.c_str(), text.size(), reread, metaError), "a model meta formats and parses");
        Check(reread.hasModelOptions && reread.modelOptions.importMaterials && reread.subAssets.Size() == 4
                && reread.subAssets[1].key == meta.subAssets[1].key && reread.subAssets[1].id == meta.subAssets[1].id,
            "with its options and its sub-asset list");
        const char* clash = "Version: 1\nId: 0123456789abcdeffedcba9876543210\nType: Model\nModel:\n  SubAssets:\n"
                            "    - Key: Mesh/A\n      Id: 0123456789abcdeffedcba9876543211\n"
                            "    - Key: Mesh/A\n      Id: 0123456789abcdeffedcba9876543212\n";
        Check(false == JBro::ParseAssetMetaFile(clash, std::strlen(clash), reread, metaError), "two sub-assets may not share a key");
        const char* badKey = "Version: 1\nId: 0123456789abcdeffedcba9876543210\nType: Model\nModel:\n  SubAssets:\n"
                             "    - Key: Canvas/A\n      Id: 0123456789abcdeffedcba9876543211\n";
        Check(false == JBro::ParseAssetMetaFile(badKey, std::strlen(badKey), reread, metaError), "a sub-asset is a mesh, a material or a texture");
        platform.Shutdown();
        fs::remove_all(root);
    }
    const JBro::AssetSubEntry* SubEntry(const JBro::AssetMetaFile& meta, const char* key)
    {
        for (std::size_t index = 0; index < meta.subAssets.Size(); ++index)
        {
            if (meta.subAssets[index].key == key)
            {
                return &meta.subAssets[index];
            }
        }
        return nullptr;
    }

    // **모델의 내장 그림과 같은 그림 묶기**(model-import-plan 5 단계·§2.5). 내장 그림은 `Texture/이름` 하위 에셋이고 `.jmeta` 에 원본 해시와 제 옵션을 적는다.
    // 다른 모델에 든 같은 바이트는 같은 묶음 열쇠를 갖고, 한쪽 옵션을 바꾸면 갈라진다. 바깥 그림 파일을 가리키는 그림은 하위 에셋이 아니다.
    void TestEmbeddedImagesAreSubAssetsGroupedByContent()
    {
        const std::string bricks = std::string("\x89PNG\r\n\x1a\nbrick bytes", 19);
        const std::string stones = std::string("\x89PNG\r\n\x1a\nstone bytes", 19);
        const fs::path root = MakeProbeDirectory();
        WriteFile(root / "crate.gltf", TestModels::SelfContainedQuadGltf("Bricks", bricks).c_str());
        WriteFile(root / "barrel.gltf", TestModels::SelfContainedQuadGltf("Bricks", bricks).c_str());
        WriteFile(root / "wall.gltf", TestModels::SelfContainedQuadGltf("Bricks", stones).c_str());
        const std::string buffer = TestModels::QuadBuffer();
        WriteFile(root / "outside.gltf", TestModels::QuadGltf("data:application/octet-stream;base64," + TestModels::Base64(buffer), buffer.size(),
            "{\"uri\":\"bricks.png\"}").c_str());
        JBro::WindowsPlatform platform;
        JBro::JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform must initialize");
        JBro::AssetScanOptions editor;
        editor.createMissingMeta = true;
        JBro::AssetRegistry registry;
        JBro::AssetScanReport report;
        Check(registry.Scan(platform, Utf8(root).c_str(), editor, report), "the folder scans");

        const auto metaOf = [&](const char* name) {
            JBro::AssetMetaFile meta;
            JBro::AssetMetaError error;
            Check(JBro::LoadAssetMetaFile(platform, Utf8(root / (std::string(name) + ".jmeta")).c_str(), meta, error), "a model's meta reads");
            return meta;
        };
        const JBro::AssetMetaFile crate = metaOf("crate.gltf");
        const JBro::AssetSubEntry* crateImage = SubEntry(crate, "Texture/Bricks");
        Check(crateImage != nullptr, "the embedded image is a Texture/<name> sub-asset");
        Check(crateImage->contentHash == JBro::HashModelBytes({reinterpret_cast<const std::byte*>(bricks.data()), static_cast<JBro::UInt32>(bricks.size())}),
            "its meta entry records the hash of the image's bytes");
        Check(crateImage->hasTextureOptions && crateImage->textureOptions.mipmaps, "and its own options, with mipmaps on for a model's image");
        Check(SubEntry(crate, "Mesh/Quad") != nullptr && SubEntry(crate, "Mesh/Quad")->contentHash == 0, "a mesh entry has no hash");
        const JBro::AssetMetaFile outside = metaOf("outside.gltf");
        Check(outside.subAssets.Size() == 2 && SubEntry(outside, "Texture/bricks.png") == nullptr,
            "an image the model only points at is not a sub-asset - that file is its own Texture");

        const JBro::AssetRecord* crateRecord = registry.Find(crateImage->id);
        const JBro::AssetRecord* barrelRecord = registry.Find(SubEntry(metaOf("barrel.gltf"), "Texture/Bricks")->id);
        const JBro::AssetRecord* wallRecord = registry.Find(SubEntry(metaOf("wall.gltf"), "Texture/Bricks")->id);
        Check(crateRecord != nullptr && barrelRecord != nullptr && wallRecord != nullptr, "every embedded image has a record");
        Check(crateRecord->sharedImageKey != 0 && crateRecord->sharedImageKey == barrelRecord->sharedImageKey,
            "the same bytes in two models share one key");
        Check(wallRecord->sharedImageKey != crateRecord->sharedImageKey, "other bytes under the same name do not");
        Check(registry.Find(SubEntry(crate, "Mesh/Quad")->id)->sharedImageKey == 0, "meshes are never grouped");

        // 한쪽 옵션을 바꾸면 갈라진다. 모델을 다시 맞춰도 고친 옵션은 남고 해시는 다시 적힌다.
        JBro::AssetMetaFile barrel = metaOf("barrel.gltf");
        JBro::AssetSubEntry* barrelImage = const_cast<JBro::AssetSubEntry*>(SubEntry(barrel, "Texture/Bricks"));
        barrelImage->textureOptions.colorSpace = JBro::TextureColorSpace::Linear;
        barrelImage->contentHash = 1;
        JBro::Bool changed = false;
        JBro::String error;
        Check(JBro::RefreshModelSubAssets(platform, Utf8(root).c_str(), "barrel.gltf", barrel, changed, error), "the model refreshes");
        barrelImage = const_cast<JBro::AssetSubEntry*>(SubEntry(barrel, "Texture/Bricks"));
        Check(changed && barrelImage->contentHash == crateImage->contentHash, "the refresh writes the hash from the file again");
        Check(barrelImage->textureOptions.colorSpace == JBro::TextureColorSpace::Linear, "and keeps the options the user set");
        Check(JBro::SaveAssetMetaFile(platform, Utf8(root / "barrel.gltf.jmeta").c_str(), barrel), "the meta writes");
        JBro::AssetRegistry again;
        Check(again.Scan(platform, Utf8(root).c_str(), editor, report), "the folder scans again");
        Check(again.Find(crateImage->id)->sharedImageKey != again.Find(barrelImage->id)->sharedImageKey,
            "different options split the same bytes into two images");

        // 열쇠는 바이트와 옵션 세 칸에 달렸다.
        JBro::TextureImportOptions options = JBro::ModelTextureDefaults();
        const JBro::UInt64 base = JBro::MakeSharedImageKey(7, options);
        Check(JBro::MakeSharedImageKey(0, options) == 0 && base != 0, "no hash, no key");
        Check(JBro::MakeSharedImageKey(8, options) != base, "the bytes change the key");
        options.filter = JBro::TextureFilter::Linear;
        Check(JBro::MakeSharedImageKey(7, options) != base, "the filter changes the key");
        options = JBro::ModelTextureDefaults();
        options.mipmaps = false;
        Check(JBro::MakeSharedImageKey(7, options) != base, "mipmaps change the key");

        // 메타: 해시와 옵션은 그림 항목만 갖는다. 틀린 해시는 파일 전체의 실패다.
        JBro::AssetMetaFile reread;
        JBro::AssetMetaError metaError;
        const JBro::String text = JBro::FormatAssetMetaFile(barrel);
        Check(JBro::ParseAssetMetaFile(text.c_str(), text.size(), reread, metaError), "a meta with an image entry round-trips");
        const JBro::AssetSubEntry* rereadImage = SubEntry(reread, "Texture/Bricks");
        Check(rereadImage != nullptr && rereadImage->contentHash == barrelImage->contentHash && rereadImage->hasTextureOptions
                && rereadImage->textureOptions.colorSpace == JBro::TextureColorSpace::Linear,
            "with the image's hash and options");
        const char* meshHash = "Version: 1\nId: 0123456789abcdeffedcba9876543210\nType: Model\nModel:\n  SubAssets:\n"
                               "    - Key: Mesh/A\n      Id: 0123456789abcdeffedcba9876543211\n      Hash: 0123456789abcdef\n";
        Check(false == JBro::ParseAssetMetaFile(meshHash, std::strlen(meshHash), reread, metaError), "a mesh may not carry a hash");
        const char* materialOptions = "Version: 1\nId: 0123456789abcdeffedcba9876543210\nType: Model\nModel:\n  SubAssets:\n"
                                      "    - Key: Material/A\n      Id: 0123456789abcdeffedcba9876543211\n      ImportOptions:\n        mipmaps: true\n";
        Check(false == JBro::ParseAssetMetaFile(materialOptions, std::strlen(materialOptions), reread, metaError), "nor a material import options");
        const char* shortHash = "Version: 1\nId: 0123456789abcdeffedcba9876543210\nType: Model\nModel:\n  SubAssets:\n"
                                "    - Key: Texture/A\n      Id: 0123456789abcdeffedcba9876543211\n      Hash: 12ab\n";
        Check(false == JBro::ParseAssetMetaFile(shortHash, std::strlen(shortHash), reread, metaError), "a hash is sixteen hex digits");
        JBro::AssetMetaFile meshWithHash = barrel;
        const_cast<JBro::AssetSubEntry*>(SubEntry(meshWithHash, "Mesh/Quad"))->contentHash = 5;
        JBro::String refused;
        Check(false == JBro::FormatAssetMetaFile(meshWithHash, refused), "and the writer refuses what the reader would refuse");
        platform.Shutdown();
        fs::remove_all(root);
    }
}

JBro::Int32 RunAssetRegistryTests()
{
    JBRO_CASE(TestTypeRules());
    JBRO_CASE(TestModelsRegisterTheirSubAssets());
    JBRO_CASE(TestEmbeddedImagesAreSubAssetsGroupedByContent());
    JBRO_CASE(TestMetaFileRoundTrip());
    JBRO_CASE(TestIgnorePatterns());
    JBRO_CASE(TestRegisterUnregisterAndRename());
    JBRO_CASE(TestScanRegistersWhatItShouldAndSkipsTheRest());
    std::cout << "Asset registry tests passed.\n";
    return 0;
}
