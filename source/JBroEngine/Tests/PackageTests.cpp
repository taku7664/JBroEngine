#include "TestCases.h"
#include "TestModels.h"
#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/AssetSource.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Canvas/CanvasFile.h>
#include <JBro/D3D12RHI/D3D12RHI.h>
#include <JBro/AudioTypes/Component/AudioSource.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2DSystem/Framework2D.h>
#include <JBro/Host/EngineInstance.h>
#include <JBro/Host/GameBuild.h>
#include <JBro/Host/GameHostArguments.h>
#include <JBro/Host/ProjectFile.h>
#include <JBro/Package/PackageAssetSource.h>
#include <JBro/Package/PackageCollect.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Package/PackageCook.h>
#include <JBro/Package/PackageReader.h>
#include <JBro/Package/PackageWriter.h>
#include <JBro/Platform/WindowsPlatform.h>

#include "TestFontNotoSansKRLatin.generated.h"

#include <cstring>
#include <filesystem>
#include <string>

#include <process.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// 에셋 패키지 `.jpak`(D-232, package-plan 1 단계): 왕복·정렬·난독화·깨진 파일 거절·창 스트림.
namespace
{
    namespace fs = std::filesystem;
    using namespace JBro;
    using namespace JBro::Package;

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

    // 시험 폴더는 프로세스마다 다르다. 여러 세션의 시험이 한 기계에서 함께 돌면 같은 이름의 임시 폴더를 서로 지우고 덮어썼다(메타를 못 읽는
    // 실패가 운으로 났다).
    fs::path ProcessTempFolder(const wchar_t* name)
    {
        std::wstring folder(name);
        folder += L"-";
        folder += std::to_wstring(_getpid());
        return fs::temp_directory_path() / folder;
    }

    ArrayView<const std::byte> Bytes(const char* text)
    {
        return ArrayView<const std::byte>(reinterpret_cast<const std::byte*>(text), std::strlen(text));
    }

    Array<std::byte> ReadFile(IPlatform& platform, const fs::path& path)
    {
        Array<std::byte> bytes;
        Check(platform.ReadWholeFile(Utf8(path).c_str(), bytes), "the package file reads back");
        return bytes;
    }

    void WriteFile(const fs::path& path, const Array<std::byte>& bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.Data()), static_cast<std::streamsize>(bytes.Size()));
    }

    JBro::Bool Contains(const Array<std::byte>& haystack, const char* needle)
    {
        const std::size_t length = std::strlen(needle);
        for (std::size_t at = 0; at + length <= haystack.Size(); ++at)
        {
            if (std::memcmp(haystack.Data() + at, needle, length) == 0)
            {
                return true;
            }
        }
        return false;
    }

    Entry Make(AssetId id, AssetType type, BlobKind kind, const char* path, AssetId owner = {})
    {
        Entry entry;
        entry.id = id;
        entry.type = type;
        entry.kind = kind;
        entry.path = path;
        entry.owner = owner;
        return entry;
    }

    constexpr unsigned char TinyPng[] =
    {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d,
        0x24, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
        0x1f, 0x0c, 0x81, 0x34, 0x08, 0x34, 0x00, 0x00, 0x49, 0x49, 0x09, 0x78, 0x9c, 0x51, 0x17, 0x92,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
    };

    void WriteRaw(const fs::path& path, const void* data, std::size_t size)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    // 16 비트 모노 WAV, 0 부터 오르는 표본이다.
    Array<std::byte> MakeWav(JBro::UInt32 frames)
    {
        Array<std::byte> bytes;
        const JBro::UInt32 dataBytes = frames * 2;
        bytes.Resize(44 + dataBytes);
        const auto put32 = [&](std::size_t at, JBro::UInt32 value) { std::memcpy(bytes.Data() + at, &value, 4); };
        const auto put16 = [&](std::size_t at, std::uint16_t value) { std::memcpy(bytes.Data() + at, &value, 2); };
        std::memcpy(bytes.Data(), "RIFF", 4);
        put32(4, 36 + dataBytes);
        std::memcpy(bytes.Data() + 8, "WAVEfmt ", 8);
        put32(16, 16);
        put16(20, 1);
        put16(22, 1);
        put32(24, 48000);
        put32(28, 48000 * 2);
        put16(32, 2);
        put16(34, 16);
        std::memcpy(bytes.Data() + 36, "data", 4);
        put32(40, dataBytes);
        for (JBro::UInt32 frame = 0; frame < frames; ++frame)
        {
            const auto sample = static_cast<std::int16_t>(frame * 3);
            std::memcpy(bytes.Data() + 44 + frame * 2, &sample, 2);
        }
        return bytes;
    }

    // 패키지의 색인을 풀어 `change` 로 고치고 해시를 다시 잰 뒤 섞어 넣는다. 색인 해시가 맞는 채로 내용이 틀린 패키지를 만든다.
    template <typename TChange>
    Array<std::byte> RewriteIndex(const Array<std::byte>& file, TChange change)
    {
        Array<std::byte> copy = file;
        JBro::UInt64 indexOffset = 0;
        JBro::UInt64 indexSize = 0;
        JBro::UInt64 key = 0;
        std::memcpy(&indexOffset, copy.Data() + 24, 8);
        std::memcpy(&indexSize, copy.Data() + 32, 8);
        std::memcpy(&key, copy.Data() + 48, 8);
        std::byte* index = copy.Data() + indexOffset;
        Obfuscate(key, indexOffset, index, static_cast<std::size_t>(indexSize));
        change(index);
        const JBro::UInt64 hash = JBro::Package::Hash(index, static_cast<std::size_t>(indexSize));
        std::memcpy(copy.Data() + 40, &hash, 8);
        Obfuscate(key, indexOffset, index, static_cast<std::size_t>(indexSize));
        return copy;
    }

    // **패키지에서 싣기**(package-plan 2 단계). 원본 폴더를 구운 뒤 지우고, 패키지로 연 에셋 시스템이 느슨한 파일과 같은 자료를 내는지 본다.
    void TestAssetsLoadTheSameFromAPackage()
    {
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform initializes");
        const fs::path root = ProcessTempFolder(L"JBroPackageAssets·에셋");
        fs::remove_all(root);
        const fs::path assetsFolder = root / "Assets";
        WriteRaw(assetsFolder / "Art" / "hero.png", TinyPng, sizeof(TinyPng));
        WriteRaw(assetsFolder / "Fonts" / "latin.otf", TestFontNotoSansKRLatin, sizeof(TestFontNotoSansKRLatin));
        const char* table = "menu.start: Start\n";
        WriteRaw(assetsFolder / "Text" / "ui.en-US.jstrings", table, std::strlen(table));
        const Array<std::byte> wav = MakeWav(4800);
        WriteRaw(assetsFolder / "Sound" / "theme.wav", wav.Data(), wav.Size());

        AssetRegistry loose;
        AssetScanOptions options;
        options.createMissingMeta = true;
        AssetScanReport report;
        const String assetRoot = Utf8(assetsFolder);
        Check(loose.Scan(platform, assetRoot.c_str(), options, report), "the loose folder scans");
        const AssetRecord* textureRecord = loose.FindByPath("Art/hero.png");
        const AssetRecord* fontRecord = loose.FindByPath("Fonts/latin.otf");
        const AssetRecord* tableRecord = loose.FindByPath("Text/ui.en-US.jstrings");
        const AssetRecord* soundRecord = loose.FindByPath("Sound/theme.wav");
        Check(textureRecord != nullptr && fontRecord != nullptr && tableRecord != nullptr && soundRecord != nullptr, "every file registers");
        const AssetId textureId = textureRecord->id;
        const AssetId fontId = fontRecord->id;
        const AssetId tableId = tableRecord->id;
        const AssetId soundId = soundRecord->id;
        Array<AssetId> owned;
        loose.CollectOwned(textureId, owned);
        Check(owned.Size() == 1, "the image has its sprite");
        const AssetId spriteId = owned[0];
        {
            // 소리는 디스크에서 흘려 읽는다 - 패키지에서는 창 스트림이다.
            const String metaPath = Utf8(assetsFolder / "Sound" / "theme.wav.jmeta");
            AssetMetaFile meta;
            AssetMetaError error;
            Check(LoadAssetMetaFile(platform, metaPath.c_str(), meta, error), "the sound meta reads");
            meta.hasAudioOptions = true;
            meta.audioOptions.mode = AudioImportMode::StreamFromDisk;
            Check(SaveAssetMetaFile(platform, metaPath.c_str(), meta), "the sound meta saves");
        }

        // 느슨한 파일로 읽은 것이 기준이다.
        AssetSystem looseAssets;
        Check(looseAssets.Initialize(memory), "the loose asset system initializes");
        looseAssets.Bind(platform, loose, assetRoot.c_str());
        const TextureData looseTexture = *looseAssets.GetTexture(looseAssets.Load(textureId));
        const SpriteData looseSprite = *looseAssets.GetSprite(looseAssets.Load(spriteId));
        const Array<std::byte> looseFont = looseAssets.GetFont(looseAssets.Load(fontId))->bytes;
        const AudioData looseSound = *looseAssets.GetAudio(looseAssets.Load(soundId));
        looseAssets.Shutdown();

        // 참조 따라가기: 이미지의 Texture 와 Sprite 는 어느 쪽에서 가도 한 짝으로 간다.
        {
            Array<AssetId> seeds;
            seeds.Add(textureId);
            Array<AssetId> collected;
            CollectReport collectReport;
            CollectAssets(platform, loose, assetRoot.c_str(), ArrayView<const AssetId>(seeds.Data(), seeds.Size()), collected, collectReport);
            Check(collected.Size() == 2 && collected[0] == textureId && collected[1] == spriteId && collectReport.warnings.IsEmpty(),
                "a texture takes its sprite along");
            seeds.Clear();
            seeds.Add(spriteId);
            seeds.Add(Uuid::Generate());
            CollectAssets(platform, loose, assetRoot.c_str(), ArrayView<const AssetId>(seeds.Data(), seeds.Size()), collected, collectReport);
            Check(collected.Size() == 2 && collected[0] == spriteId && collected[1] == textureId && collectReport.warnings.Size() == 1,
                "a sprite takes its texture along, and a seed that is not in the project is a warning");
        }
        Array<AssetId> ids;
        ids.Add(textureId);
        ids.Add(spriteId);
        ids.Add(fontId);
        ids.Add(tableId);
        ids.Add(soundId);
        PackageWriter writer(0x5EED5EED5EED5EEDull);
        CookReport cooked;
        Check(CookAssets(platform, loose, assetRoot.c_str(), ArrayView<const AssetId>(ids.Data(), ids.Size()), writer, cooked),
            "every asset cooks");
        Check(cooked.assets == 5 && cooked.cookedTextures == 1 && cooked.failures.IsEmpty(), "the report counts what was cooked");
        Array<AssetId> absent;
        absent.Add(Uuid::FromName("not in the project"));
        PackageWriter spare(1);
        CookReport refused;
        Check(false == CookAssets(platform, loose, assetRoot.c_str(), ArrayView<const AssetId>(absent.Data(), absent.Size()), spare, refused)
                && refused.failures.Size() == 1,
            "an id the registry does not know is a failure");
        const String packagePath = Utf8(root / "game.jpak");
        String error;
        Check(writer.Save(platform, packagePath.c_str(), error), "the package is written");
        // 원본을 지운다 - 아래는 패키지만 본다.
        fs::remove_all(assetsFolder);

        PackageReader package;
        Check(package.Open(platform, packagePath.c_str(), error), "the package opens");
        const Entry* textureMeta = package.Find(textureId, BlobKind::Meta);
        Check(textureMeta != nullptr && package.Find(textureId, BlobKind::CookedTexture) != nullptr
                && package.Find(textureId, BlobKind::Source) == nullptr,
            "a texture ships as its meta and its decoded pixels, not the PNG");
        Check(package.Find(spriteId, BlobKind::Record) != nullptr && package.Find(spriteId, BlobKind::Meta) == nullptr,
            "a sprite ships as a record under its texture");
        AssetRegistry packed;
        Check(FillRegistry(package, packed) == 5, "the registry is filled from the index");
        const AssetRecord* packedSprite = packed.Find(spriteId);
        Check(packedSprite != nullptr && packedSprite->owner == textureId && packed.FindByPath("Art/hero.png")->id == textureId,
            "with the paths, types and owners of the loose registry");
        PackageAssetSource source(package);
        AssetSystem assets;
        Check(assets.Initialize(memory), "the packaged asset system initializes");
        assets.Bind(platform, packed, source);
        Check(assets.GetAssetRoot().empty() && assets.GetSource() == &source, "a packaged asset system has no folder");

        const TextureData* texture = assets.GetTexture(assets.Load(textureId));
        Check(texture != nullptr && texture->width == looseTexture.width && texture->height == looseTexture.height
                && texture->pixels.Size() == looseTexture.pixels.Size()
                && std::memcmp(texture->pixels.Data(), looseTexture.pixels.Data(), texture->pixels.Size()) == 0
                && texture->filter == looseTexture.filter,
            "the cooked texture has the loose texture's pixels");
        const SpriteData* sprite = assets.GetSprite(assets.Load(spriteId));
        Check(sprite != nullptr && sprite->frames.Size() == looseSprite.frames.Size() && sprite->frames[0].width == looseSprite.frames[0].width
                && sprite->options.pixelsPerUnit == looseSprite.options.pixelsPerUnit,
            "the sprite reads its texture's meta");
        const FontData* font = assets.GetFont(assets.Load(fontId));
        Check(font != nullptr && font->bytes.Size() == looseFont.Size() && std::memcmp(font->bytes.Data(), looseFont.Data(), looseFont.Size()) == 0,
            "the font ships its bytes");
        const StringTableData* strings = assets.GetStringTable(assets.Load(tableId));
        const String* start = strings != nullptr ? strings->entries.Find(String("menu.start")) : nullptr;
        Check(start != nullptr && *start == "Start", "the string table reads");
        Array<std::byte> canvasBytes;
        Check(assets.ReadSourceByPath("Text/ui.en-US.jstrings", canvasBytes) && canvasBytes.Size() == std::strlen(table),
            "a source reads by its path, as the game host reads its startup canvas");
        Check(false == assets.ReadSourceByPath("Art/hero.png", canvasBytes), "a cooked texture has no source to read");

        const AudioData* sound = assets.GetAudio(assets.Load(soundId));
        Check(sound != nullptr && sound->frameCount == looseSound.frameCount && sound->sampleRate == looseSound.sampleRate
                && sound->streamPath.rfind("jpak:", 0) == 0,
            "a streamed sound opens from the package");
        OwnerPtr<IFileStream> stream = source.OpenStream(sound->streamPath.c_str());
        Array<std::byte> streamed;
        streamed.Resize(wav.Size());
        Check(stream.Get() != nullptr && stream->Read(streamed.Data(), streamed.Size()) == wav.Size()
                && std::memcmp(streamed.Data(), wav.Data(), wav.Size()) == 0,
            "and its stream reads the original file");
        Check(source.OpenStream("jpak:0123") .Get() == nullptr && source.OpenStream("C:/theme.wav").Get() == nullptr,
            "a stream name that is not a packaged source opens nothing");
        Array<JBro::Float> peaks;
        Check(assets.ComputeAudioPeaks(assets.Find(soundId), 8, peaks) && peaks.Size() == 8, "peaks stream from the package too");
        assets.Shutdown();
        package.Close();
        fs::remove_all(root);
        platform.Shutdown();
    }

    // **모델은 꺼낸 메시와 그림으로 실린다**(model-import-plan 6 단계). 패키지에 모델 원본·곁 파일은 없고, 하위 메시는 엔진이 바로 읽는 메시(`CookedMesh`),
    // 내장 그림은 디코드한 픽셀(`CookedTexture`)이다. 원본 폴더를 지운 뒤에도 게임은 느슨한 파일로 읽은 것과 같은 메시·픽셀을 받는다.
    void TestAModelShipsAsCookedMeshesAndImages()
    {
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform initializes");
        const fs::path root = ProcessTempFolder(L"JBroPackageModels·모델");
        fs::remove_all(root);
        const fs::path assetsFolder = root / "Assets";
        // 버퍼는 곁 파일(`crate.bin`)이고 그림은 내장이다 - 게임이 곁 파일을 찾지 않는지 함께 본다.
        const std::string buffer = TestModels::QuadBuffer();
        const std::string png(reinterpret_cast<const char*>(TinyPng), sizeof(TinyPng));
        const std::string gltf = TestModels::QuadGltf("crate.bin", buffer.size(),
            "{\"name\":\"Bricks\",\"uri\":\"data:image/png;base64," + TestModels::Base64(png) + "\"}");
        WriteRaw(assetsFolder / "Props" / "crate.gltf", gltf.data(), gltf.size());
        WriteRaw(assetsFolder / "Props" / "crate.bin", buffer.data(), buffer.size());

        AssetRegistry loose;
        AssetScanOptions options;
        options.createMissingMeta = true;
        AssetScanReport report;
        const String assetRoot = Utf8(assetsFolder);
        Check(loose.Scan(platform, assetRoot.c_str(), options, report), "the loose folder scans");
        const AssetRecord* model = loose.FindByPath("Props/crate.gltf");
        Check(model != nullptr && model->type == AssetType::Model, "the glTF is a model");
        const AssetId modelId = model->id;
        Array<AssetId> owned;
        loose.CollectOwned(modelId, owned);
        AssetId meshId;
        AssetId imageId;
        AssetId materialId;
        for (const AssetId& id : owned)
        {
            const AssetType type = loose.Find(id)->type;
            meshId = type == AssetType::Mesh ? id : meshId;
            imageId = type == AssetType::Texture ? id : imageId;
            materialId = type == AssetType::Material ? id : materialId;
        }
        Check(false == meshId.IsNull() && false == imageId.IsNull() && false == materialId.IsNull(), "the model has a mesh, an image and a material");

        // 느슨한 파일로 읽은 것이 기준이다.
        AssetSystem looseAssets;
        Check(looseAssets.Initialize(memory), "the loose asset system initializes");
        looseAssets.Bind(platform, loose, assetRoot.c_str());
        const MeshData looseMesh = *looseAssets.GetMesh(looseAssets.Load(meshId));
        const MaterialData looseMaterial = *looseAssets.GetMaterial(looseAssets.Load(materialId));
        Check(looseMesh.material == materialId && looseMaterial.desc.baseColorTextureId == imageId, "loose, the mesh knows its material and the material its picture");
        const TextureData looseImage = *looseAssets.GetTexture(looseAssets.Load(imageId));
        looseAssets.Shutdown();

        // 캔버스가 메시 하나만 가리켜도 모델과 그 하위 에셋이 모두 따라간다.
        Array<AssetId> seeds;
        seeds.Add(meshId);
        Array<AssetId> ids;
        CollectReport collectReport;
        CollectAssets(platform, loose, assetRoot.c_str(), ArrayView<const AssetId>(seeds.Data(), seeds.Size()), ids, collectReport);
        Check(ids.Size() == 4 && collectReport.warnings.IsEmpty(), "a mesh takes its model and the model's other sub-assets along");
        PackageWriter writer(0x0DE10DE10DE10DE1ull);
        CookReport cooked;
        Check(CookAssets(platform, loose, assetRoot.c_str(), ArrayView<const AssetId>(ids.Data(), ids.Size()), writer, cooked),
            "the model cooks");
        Check(cooked.cookedMeshes == 1 && cooked.cookedTextures == 1 && cooked.cookedMaterials == 1 && cooked.failures.IsEmpty(),
            "one cooked mesh, one cooked image and one cooked material");
        const String packagePath = Utf8(root / "game.jpak");
        String error;
        Check(writer.Save(platform, packagePath.c_str(), error), "the package is written");
        fs::remove_all(assetsFolder);

        PackageReader package;
        Check(package.Open(platform, packagePath.c_str(), error), "the package opens");
        Check(package.Find(modelId, BlobKind::Meta) != nullptr && package.Find(modelId, BlobKind::Source) == nullptr,
            "the model ships as its meta only - no model file for the game to decode");
        Check(package.Find(meshId, BlobKind::CookedMesh) != nullptr, "its mesh ships cooked");
        Check(package.Find(imageId, BlobKind::CookedTexture) != nullptr, "its embedded image ships as pixels");
        Check(package.Find(materialId, BlobKind::CookedMaterial) != nullptr, "its material ships as a material file with its pictures resolved");
        AssetRegistry packed;
        Check(FillRegistry(package, packed) == 4, "the registry is filled from the index");
        Check(packed.Find(meshId) != nullptr && packed.Find(meshId)->owner == modelId, "the mesh keeps its model as owner");
        PackageAssetSource source(package);
        AssetSystem assets;
        Check(assets.Initialize(memory), "the packaged asset system initializes");
        assets.Bind(platform, packed, source);
        const MeshData* mesh = assets.GetMesh(assets.Load(meshId));
        Check(mesh != nullptr && mesh->vertices.Size() == looseMesh.vertices.Size() && mesh->indices.Size() == looseMesh.indices.Size()
                && mesh->hasUvs == looseMesh.hasUvs
                && std::memcmp(mesh->vertices.Data(), looseMesh.vertices.Data(), mesh->vertices.Size() * sizeof(ModelVertex)) == 0
                && std::memcmp(mesh->indices.Data(), looseMesh.indices.Data(), mesh->indices.Size() * sizeof(JBro::UInt32)) == 0,
            "the cooked mesh is the loose mesh, vertex for vertex");
        Check(mesh->material == materialId, "and still knows its material");
        const MaterialData* packedMaterial = assets.GetMaterial(assets.Load(materialId));
        Check(packedMaterial != nullptr && packedMaterial->desc.baseColorTextureId == imageId
                && packedMaterial->desc.baseColor == looseMaterial.desc.baseColor && packedMaterial->desc.roughness == looseMaterial.desc.roughness,
            "the packaged material is the loose material, picture slot included, without reading the model");
        const TextureData* image = assets.GetTexture(assets.Load(imageId));
        Check(image != nullptr && image->width == looseImage.width && image->height == looseImage.height
                && std::memcmp(image->pixels.Data(), looseImage.pixels.Data(), image->pixels.Size()) == 0
                && image->options.mipmaps == looseImage.options.mipmaps,
            "the cooked image is the loose image, with its own options from the model's meta");
        assets.Shutdown();
        package.Close();

        // 깨진 메시 블롭은 읽지 않는다.
        CookedMesh written;
        written.vertices.Resize(3);
        written.indices.Add(0);
        written.indices.Add(1);
        written.indices.Add(2);
        written.hasUvs = true;
        written.hasTangents = true;
        Array<std::byte> blob;
        WriteCookedMesh(written, blob);
        CookedMesh read;
        Check(ReadCookedMesh(blob, read) && read.vertices.Size() == 3 && read.indices.Size() == 3 && read.hasUvs && read.hasTangents && read.skin.IsEmpty(),
            "a cooked mesh round-trips");
        Array<std::byte> shortBlob = blob;
        shortBlob.Resize(blob.Size() - 1);
        Check(false == ReadCookedMesh(shortBlob, read), "a short blob is refused");
        written.indices[2] = 3;
        WriteCookedMesh(written, blob);
        Check(false == ReadCookedMesh(blob, read), "an index past the vertices is refused");
        fs::remove_all(root);
        platform.Shutdown();
    }

    // **스킨 모델은 뼈대·클립과 함께 구워진다**(D-466). 게임 빌드가 스킨 메시의 뼈·가중치를 버리고 뼈대·클립을 레코드만 두면 패키지로 연 게임의 캐릭터는
    // 쉬는 자세로 굳는다. 모델 메타의 소켓(뼈대·메시)·뼈 마스크·클립 설정(구간·반복·이벤트)·나눈 클립까지 적은 뒤 굽고, 원본 폴더를 지운 게임이
    // 느슨한 파일로 읽은 것과 같은 메시·스킨·뼈대·트랙·설정을 받는지 본다.
    void TestASkinnedModelShipsWithItsSkeletonAndClips()
    {
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform initializes");
        const fs::path root = ProcessTempFolder(L"JBroPackageSkins·스킨");
        fs::remove_all(root);
        const fs::path assetsFolder = root / "Assets";
        const std::string gltf = TestModels::SkinnedGltf();
        WriteRaw(assetsFolder / "Chars" / "walker.gltf", gltf.data(), gltf.size());

        AssetRegistry loose;
        AssetScanOptions options;
        options.createMissingMeta = true;
        AssetScanReport report;
        const String assetRoot = Utf8(assetsFolder);
        Check(loose.Scan(platform, assetRoot.c_str(), options, report), "the loose folder scans");
        const AssetRecord* model = loose.FindByPath("Chars/walker.gltf");
        Check(model != nullptr && model->type == AssetType::Model, "the skinned glTF is a model");
        const AssetId modelId = model->id;
        Array<AssetId> owned;
        loose.CollectOwned(modelId, owned);
        AssetId meshId;
        AssetId skeletonId;
        AssetId clipId;
        for (const AssetId& id : owned)
        {
            const AssetType type = loose.Find(id)->type;
            meshId = type == AssetType::Mesh ? id : meshId;
            skeletonId = type == AssetType::Skeleton ? id : skeletonId;
            clipId = type == AssetType::AnimationClip ? id : clipId;
        }
        Check(false == meshId.IsNull() && false == skeletonId.IsNull() && false == clipId.IsNull(), "the model has a mesh, a skeleton and a clip");

        // 메타에 소켓·마스크·클립 설정·나눈 클립을 적는다.
        const String metaPath = Utf8(assetsFolder / "Chars" / "walker.gltf.jmeta");
        AssetMetaFile meta;
        AssetMetaError metaError;
        Check(LoadAssetMetaFile(platform, metaPath.c_str(), meta, metaError), "the model meta reads");
        AssetSubEntry split;
        split.key = "AnimationClip/Wave 2";
        split.id = Uuid::Generate();
        split.hasClip = true;
        split.clip.source = "Wave";
        split.clip.start = 0.5f;
        for (AssetSubEntry& entry : meta.subAssets)
        {
            if (entry.id == skeletonId)
            {
                ModelSocket tip;
                tip.name = "Tip";
                tip.bone = "Spine";
                tip.position = {0.0f, 0.5f, 0.0f};
                entry.sockets.Add(tip);
                ModelBoneMask upper;
                upper.name = "Upper";
                ModelMaskBone spine;
                spine.bone = "Spine";
                spine.weight = 0.5f;
                upper.bones.Add(spine);
                entry.masks.Add(upper);
            }
            if (entry.id == meshId)
            {
                ModelSocket grip;
                grip.name = "Grip";
                grip.bone = "Hip";
                grip.position = {1.0f, 0.0f, 0.0f};
                entry.sockets.Add(grip);
            }
            if (entry.id == clipId)
            {
                entry.hasClip = true;
                entry.clip.end = 0.75f;
                entry.clip.loop = false;
                ModelClipEvent step;
                step.name = "Step";
                step.time = 0.25f;
                entry.clip.events.Add(step);
            }
        }
        meta.subAssets.Add(split);
        Check(SaveAssetMetaFile(platform, metaPath.c_str(), meta), "the meta with sockets, a mask and clip settings is written");
        Check(loose.Scan(platform, assetRoot.c_str(), options, report) && loose.Find(split.id) != nullptr, "the split clip is a sub-asset after a scan");

        // 느슨한 파일로 읽은 것이 기준이다.
        AssetSystem looseAssets;
        Check(looseAssets.Initialize(memory), "the loose asset system initializes");
        looseAssets.Bind(platform, loose, assetRoot.c_str());
        const MeshData looseMesh = *looseAssets.GetMesh(looseAssets.Load(meshId));
        const SkeletonData looseSkeleton = *looseAssets.GetSkeleton(looseAssets.Load(skeletonId));
        const AnimationClipData looseClip = *looseAssets.GetAnimationClip(looseAssets.Load(clipId));
        const AnimationClipData looseSplit = *looseAssets.GetAnimationClip(looseAssets.Load(split.id));
        looseAssets.Shutdown();
        Check(looseMesh.skin.Size() == looseMesh.vertices.Size() && looseMesh.skeleton == skeletonId && looseMesh.sockets.Size() == 1
                && looseSkeleton.sockets.Size() == 1 && looseSkeleton.masks.Size() == 1 && false == looseClip.loop && looseClip.events.Size() == 1
                && looseSplit.animation.name == "Wave 2",
            "loose, the mesh is skinned and the meta's sockets, mask and clip settings are read");

        // 캔버스가 메시 하나만 가리켜도 모델과 뼈대·두 클립이 따라간다.
        Array<AssetId> seeds;
        seeds.Add(meshId);
        Array<AssetId> ids;
        CollectReport collectReport;
        CollectAssets(platform, loose, assetRoot.c_str(), ArrayView<const AssetId>(seeds.Data(), seeds.Size()), ids, collectReport);
        Check(ids.Size() == 5 && collectReport.warnings.IsEmpty(), "a mesh takes its model, its skeleton and both clips along");
        PackageWriter writer(0x5C1A5C1A5C1A5C1Aull);
        CookReport cooked;
        Check(CookAssets(platform, loose, assetRoot.c_str(), ArrayView<const AssetId>(ids.Data(), ids.Size()), writer, cooked), "the skinned model cooks");
        Check(cooked.cookedMeshes == 1 && cooked.cookedSkeletons == 1 && cooked.cookedClips == 2 && cooked.failures.IsEmpty(),
            "one mesh, one skeleton and two clips - the split clip cooks its source");
        const String packagePath = Utf8(root / "game.jpak");
        String error;
        Check(writer.Save(platform, packagePath.c_str(), error), "the package is written");
        fs::remove_all(assetsFolder);

        PackageReader package;
        Check(package.Open(platform, packagePath.c_str(), error), "the package opens");
        Check(package.Find(modelId, BlobKind::Source) == nullptr && package.Find(skeletonId, BlobKind::CookedSkeleton) != nullptr
                && package.Find(clipId, BlobKind::CookedAnimationClip) != nullptr && package.Find(split.id, BlobKind::CookedAnimationClip) != nullptr,
            "the skeleton and both clips ship cooked, without the model file");
        AssetRegistry packed;
        Check(FillRegistry(package, packed) == 5, "the registry is filled from the index");
        PackageAssetSource source(package);
        AssetSystem assets;
        Check(assets.Initialize(memory), "the packaged asset system initializes");
        assets.Bind(platform, packed, source);

        const MeshData* mesh = assets.GetMesh(assets.Load(meshId));
        Check(mesh != nullptr && mesh->vertices.Size() == looseMesh.vertices.Size() && mesh->skin.Size() == looseMesh.skin.Size()
                && std::memcmp(mesh->skin.Data(), looseMesh.skin.Data(), mesh->skin.Size() * sizeof(ModelSkinWeights)) == 0,
            "the cooked mesh keeps its bones and weights, vertex for vertex");
        Check(mesh->skeleton == skeletonId && assets.GetSkeleton(mesh->skeletonAsset) != nullptr, "and the id of its skeleton, which it loads along");
        Check(mesh->sockets.Size() == 1 && mesh->sockets[0].name == "Grip" && mesh->sockets[0].bone == "Hip", "and its own socket from the meta");

        const SkeletonData* skeleton = assets.GetSkeleton(assets.Load(skeletonId));
        Check(skeleton != nullptr && skeleton->skeleton.bones.Size() == looseSkeleton.skeleton.bones.Size(), "the cooked skeleton has the loose bones");
        for (std::size_t index = 0; index < skeleton->skeleton.bones.Size(); ++index)
        {
            const ModelBone& bone = skeleton->skeleton.bones[index];
            const ModelBone& expected = looseSkeleton.skeleton.bones[index];
            Check(bone.name == expected.name && bone.parent == expected.parent && bone.position.y == expected.position.y
                    && bone.rotation.w == expected.rotation.w && bone.scale.x == expected.scale.x
                    && std::memcmp(&bone.inverseBind, &expected.inverseBind, sizeof(Matrix4x4)) == 0,
                "each bone keeps its name, parent, rest pose and inverse bind");
        }
        Check(std::memcmp(&skeleton->skeleton.rootParent, &looseSkeleton.skeleton.rootParent, sizeof(Matrix4x4)) == 0,
            "and the root's parent matrix (the armature) too");
        Check(skeleton->sockets.Size() == 1 && skeleton->sockets[0].name == "Tip" && skeleton->masks.Size() == 1 && skeleton->masks[0].name == "Upper"
                && skeleton->masks[0].bones.Size() == 1 && skeleton->masks[0].bones[0].weight == 0.5f,
            "the skeleton's sockets and masks come from the shipped meta");

        const auto sameTracks = [](const ModelAnimation& left, const ModelAnimation& right) {
            if (left.tracks.Size() != right.tracks.Size() || left.duration != right.duration)
            {
                return false;
            }
            for (std::size_t index = 0; index < left.tracks.Size(); ++index)
            {
                const ModelAnimationTrack& a = left.tracks[index];
                const ModelAnimationTrack& b = right.tracks[index];
                if (a.bone != b.bone || a.path != b.path || a.interpolation != b.interpolation || a.times.Size() != b.times.Size()
                    || a.values.Size() != b.values.Size()
                    || std::memcmp(a.times.Data(), b.times.Data(), a.times.Size() * sizeof(Float)) != 0
                    || std::memcmp(a.values.Data(), b.values.Data(), a.values.Size() * sizeof(Float)) != 0)
                {
                    return false;
                }
            }
            return true;
        };
        const AnimationClipData* clip = assets.GetAnimationClip(assets.Load(clipId));
        Check(clip != nullptr && sameTracks(clip->animation, looseClip.animation) && clip->animation.name == "Wave",
            "the cooked clip has the loose tracks after its range is cut, and its name");
        Check(false == clip->loop && clip->events.Size() == 1 && clip->events[0].name == "Step" && clip->events[0].time == looseClip.events[0].time
                && clip->sourceDuration == looseClip.sourceDuration,
            "and the clip settings from the meta - no loop, the event, the source length");
        const AnimationClipData* splitClip = assets.GetAnimationClip(assets.Load(split.id));
        Check(splitClip != nullptr && sameTracks(splitClip->animation, looseSplit.animation) && splitClip->animation.name == "Wave 2",
            "the split clip reads the second half of its source and keeps its own name");
        assets.Shutdown();
        package.Close();

        // 깨진 블롭은 읽지 않는다.
        ModelSkeleton bones;
        bones.name = "Rig";
        bones.bones.Resize(2);
        bones.bones[0].name = "Root";
        bones.bones[1].name = "Arm";
        bones.bones[1].parent = 0;
        Array<std::byte> blob;
        WriteCookedSkeleton(bones, blob);
        ModelSkeleton readBones;
        Check(ReadCookedSkeleton(blob, readBones) && readBones.bones.Size() == 2 && readBones.bones[1].name == "Arm" && readBones.name == "Rig",
            "a cooked skeleton round-trips");
        Array<std::byte> broken = blob;
        broken.Resize(blob.Size() - 1);
        Check(false == ReadCookedSkeleton(broken, readBones), "a short skeleton is refused");
        bones.bones[1].parent = 1;
        WriteCookedSkeleton(bones, blob);
        Check(false == ReadCookedSkeleton(blob, readBones), "a bone that is its own parent is refused");
        ModelAnimation wave;
        wave.name = "Wave";
        wave.duration = 1.0f;
        ModelAnimationTrack turn;
        turn.bone = "Arm";
        turn.path = ModelTrackPath::Rotation;
        turn.times.Add(0.0f);
        turn.values.Add(0.0f);
        turn.values.Add(0.0f);
        turn.values.Add(0.0f);
        turn.values.Add(1.0f);
        wave.tracks.Add(turn);
        WriteCookedAnimation(wave, blob);
        ModelAnimation readWave;
        Check(ReadCookedAnimation(blob, readWave) && readWave.tracks.Size() == 1 && readWave.tracks[0].values.Size() == 4, "a cooked animation round-trips");
        wave.tracks[0].values.Pop();
        WriteCookedAnimation(wave, blob);
        Check(false == ReadCookedAnimation(blob, readWave), "a rotation key without four values is refused");
        CookedMesh skinned;
        skinned.vertices.Resize(3);
        skinned.indices.Add(0);
        skinned.indices.Add(1);
        skinned.indices.Add(2);
        skinned.skin.Resize(3);
        skinned.skin[2].bones[0] = 1;
        skinned.skin[2].weights[0] = 1.0f;
        skinned.skeleton = skeletonId;
        WriteCookedMesh(skinned, blob);
        CookedMesh readSkinned;
        Check(ReadCookedMesh(blob, readSkinned) && readSkinned.skin.Size() == 3 && readSkinned.skin[2].bones[0] == 1 && readSkinned.skeleton == skeletonId,
            "a cooked skinned mesh round-trips");
        broken = blob;
        broken.Resize(blob.Size() - 1);
        Check(false == ReadCookedMesh(broken, readSkinned), "a skinned mesh short of its weights is refused");
        fs::remove_all(root);
        platform.Shutdown();
    }

    // **참조 따라가기**: 글자 안의 32 자리 아이디만 읽는다.
    void TestIdsAreFoundInText()
    {
        const char* text = "spriteId: 0123456789abcdef0123456789ABCDEF\nshort: 0123456789abcdef\n"
                           "long: 0123456789abcdef0123456789abcdef00\nlast:fedcba9876543210fedcba9876543210";
        Array<AssetId> ids;
        FindIdsInText(text, std::strlen(text), ids);
        AssetId first;
        AssetId last;
        Check(Uuid::Parse("0123456789abcdef0123456789abcdef", 32, first) && Uuid::Parse("fedcba9876543210fedcba9876543210", 32, last),
            "the probe ids parse");
        Check(ids.Size() == 2 && ids[0] == first && ids[1] == last, "only whole 32-digit words are ids, whatever their case");
    }

    // **게임 빌드**(package-plan 3 단계). 캔버스가 쓰는 스프라이트·프로젝트 폰트·문자열 표만 싸 가고, 내놓은 프로젝트를 원본 없이 연 엔진이
    // 캔버스를 패키지에서 읽고 스프라이트를 푼 픽셀로 싣는다.
    void TestAGameBuildRunsWithoutTheSourceProject()
    {
        JMemoryContext memory;
        WindowsPlatform platform;
        D3D12RHIModule rhi;
        Check(platform.Initialize(memory), "the platform initializes");
        if (false == rhi.Initialize(memory))
        {
            std::cout << "  [skip] no D3D12 device; the game build was not run end to end" << std::endl;
            platform.Shutdown();
            return;
        }
        const fs::path root = ProcessTempFolder(L"JBroGameBuildProbe·빌드");
        fs::remove_all(root);
        const fs::path source = root / "Source";
        WriteRaw(source / "Assets" / "Art" / "hero.png", TinyPng, sizeof(TinyPng));
        WriteRaw(source / "Assets" / "Art" / "unused.png", TinyPng, sizeof(TinyPng));
        WriteRaw(source / "Assets" / "Fonts" / "latin.otf", TestFontNotoSansKRLatin, sizeof(TestFontNotoSansKRLatin));
        const char* table = "menu.start: Start\n";
        WriteRaw(source / "Assets" / "Text" / "ui.en-US.jstrings", table, std::strlen(table));
        const Array<std::byte> wav = MakeWav(4800);
        WriteRaw(source / "Assets" / "Sound" / "theme.wav", wav.Data(), wav.Size());
        const fs::path projectPath = source / "Probe.jproject";
        const auto writeProject = [&](const char* extra) {
            std::ofstream file(projectPath, std::ios::binary | std::ios::trunc);
            file << "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\nRootPath: .\nAssetDirectory: Assets\nScriptOutputLibraryPath: \"\"\n"
                 << extra << "Build:\n  ProductName: Probe Game\n  OutputDirectory: ../Out\n  StartupCanvas: Assets/Canvases/Main.jcanvas\n";
        };
        writeProject("");
        const String projectUtf8 = Utf8(projectPath);

        EngineConfig config;
        config.window.visible = false;
        config.window.width = 64;
        config.window.height = 64;
        config.createMissingAssetMeta = true;
        config.audioEnabled = false;
        EngineInstance engine;
        Check(engine.Initialize(config, platform, rhi), "the engine initializes");
        AssetId heroId;
        AssetId spriteId;
        AssetId unusedId;
        AssetId fontId;
        AssetId soundId;
        {
            Framework2D framework;
            ProjectFileError error;
            Check(engine.OpenProjectFile(framework, projectUtf8.c_str(), error), "the source project opens");
            const AssetRegistry& registry = engine.GetAssetRegistry();
            heroId = registry.FindByPath("Art/hero.png")->id;
            unusedId = registry.FindByPath("Art/unused.png")->id;
            fontId = registry.FindByPath("Fonts/latin.otf")->id;
            soundId = registry.FindByPath("Sound/theme.wav")->id;
            {
                // 소리는 디스크에서 흘려 읽는다 - 패키지로 연 게임은 패키지의 창 스트림이다.
                const String metaPath = Utf8(source / "Assets" / "Sound" / "theme.wav.jmeta");
                AssetMetaFile meta;
                AssetMetaError metaError;
                Check(LoadAssetMetaFile(platform, metaPath.c_str(), meta, metaError), "the sound meta reads");
                meta.hasAudioOptions = true;
                meta.audioOptions.mode = AudioImportMode::StreamFromDisk;
                Check(SaveAssetMetaFile(platform, metaPath.c_str(), meta), "the sound meta saves");
            }
            Array<AssetId> owned;
            registry.CollectOwned(heroId, owned);
            spriteId = owned[0];
            // 시작 캔버스: 스프라이트 하나를 그린다.
            Canvas* canvas = framework.GetCanvas();
            Object::GameObject* hero = canvas->CreateObject("hero");
            canvas->AttachComponent<Component::Transform2D>(hero);
            canvas->AttachComponent<Component::SpriteRenderer2D>(hero)->spriteId = spriteId;
            canvas->AttachComponent<Component::AudioSource>(hero)->clipId = soundId;
            String text;
            CanvasFileError canvasError;
            Check(WriteCanvasText(*canvas, text, canvasError), "the canvas writes");
            WriteRaw(source / "Assets" / "Canvases" / "Main.jcanvas", text.data(), text.size());
            engine.CloseProject();
        }
        // 폰트를 프로젝트 폰트로 둔다. 캔버스가 쓰지 않아도 간다.
        {
            char font[Uuid::TextCapacity] = {};
            fontId.ToText(font, sizeof(font));
            String fonts("Fonts:\n  - ");
            fonts.append(font);
            fonts.append("\n");
            writeProject(fonts.c_str());
        }
        {
            Framework2D framework;
            ProjectFileError error;
            Check(engine.OpenProjectFile(framework, projectUtf8.c_str(), error), "the source project opens with its canvas");
            // 가짜 게임 호스트를 복사해 본다 - 실행 파일의 바이트가 그대로 가는지만 본다.
            const char* fakeHost = "MZ fake host";
            WriteRaw(root / "FakeHost.exe", fakeHost, std::strlen(fakeHost));
            GameBuildOptions options;
            options.gameHostPath = Utf8(root / "FakeHost.exe");
            options.physicsWorkers = 0;
            GameBuildReport report;
            const JBro::Bool built = BuildGame(platform, engine.GetProjectFile(), projectUtf8.c_str(), options, report);
            if (false == built)
            {
                std::cout << "  build error: " << report.error.c_str() << '\n';
            }
            for (const String& warning : report.warnings)
            {
                std::cout << "  build warning: " << warning.c_str() << '\n';
            }
            Check(built, "the game builds");
            Check(report.warnings.IsEmpty(), "a clean project builds without warnings");
            // 캔버스·텍스처·스프라이트·소리·폰트·문자열 표. 쓰지 않은 그림은 가지 않는다.
            Check(report.assets == 6 && report.cookedTextures == 1, "only what the game uses is packed");
            engine.CloseProject();
        }
        const fs::path output = root / "Out" / "Probe Game";
        Check(fs::exists(output / "Probe Game.exe") && fs::file_size(output / "Probe Game.exe") == 12, "the game host is copied under the product name");
        Check(fs::exists(output / "Content" / "game.jpak") && fs::exists(output / "Probe Game.jproject"), "the package and the project copy are written");
        ProjectFile exported;
        ProjectFileError exportError;
        Check(LoadProjectFile(platform, Utf8(output / "Probe Game.jproject").c_str(), exported, exportError), "the exported project reads");
        if (exported.assetPackage != "Content/game.jpak" || exported.build.startupCanvas != "Canvases/Main.jcanvas"
            || exported.build.physicsThreadMode != PhysicsThreadMode::Single || false == exported.scriptOutputLibraryPath.empty())
        {
            std::cout << "  exported: package=" << exported.assetPackage.c_str() << " canvas=" << exported.build.startupCanvas.c_str()
                      << " physics=" << static_cast<JBro::Int32>(exported.build.physicsThreadMode) << " script=" << exported.scriptOutputLibraryPath.c_str()
                      << '\n';
        }
        Check(exported.assetPackage == "Content/game.jpak" && exported.build.startupCanvas == "Canvases/Main.jcanvas"
                && exported.build.physicsThreadMode == PhysicsThreadMode::Single && exported.scriptOutputLibraryPath.empty(),
            "the copy names the package, the canvas under the asset folder, the physics workers and no script");
        {
            PackageReader package;
            String error;
            Check(package.Open(platform, Utf8(output / "Content" / "game.jpak").c_str(), error), "the built package opens");
            Check(package.Find(unusedId, BlobKind::Meta) == nullptr && package.Find(heroId, BlobKind::CookedTexture) != nullptr
                    && package.Find(spriteId, BlobKind::Record) != nullptr && package.Find(fontId, BlobKind::Source) != nullptr,
                "the unused image stays out and the used one is cooked");
        }

        // 원본 프로젝트를 지우고 내놓은 폴더만으로 연다.
        fs::remove_all(source);
        {
            Framework2D framework;
            ProjectFileError error;
            const String exportedPath = Utf8(output / "Probe Game.jproject");
            Check(engine.OpenProjectFile(framework, exportedPath.c_str(), error), "the exported project opens from its package alone");
            Check(engine.IsRunningFromPackage() && false == engine.RescanAssets(), "it runs from the package and does not rescan");
            AssetSystem* assets = engine.GetAssetSystem();
            GameHostArguments arguments;
            const String canvasPath = ResolvePackagedStartupCanvas(arguments, engine.GetProjectFile());
            Array<std::byte> text;
            Check(canvasPath == "Canvases/Main.jcanvas" && assets->ReadSourceByPath(canvasPath, text), "the startup canvas reads from the package");
            CanvasFileError canvasError;
            Check(ReadCanvasText(*framework.GetCanvas(), reinterpret_cast<const char*>(text.Data()), text.Size(), canvasError),
                "the packaged canvas loads");
            framework.BindCanvasAssets();
            const Component::SpriteRenderer2D* sprite = nullptr;
            framework.GetCanvas()->ForEach<Component::SpriteRenderer2D>([&](Component::SpriteRenderer2D& renderer) {
                sprite = &renderer;
            });
            Check(sprite != nullptr && sprite->sprite.generation != 0, "the sprite resolves from the package");
            const SpriteData* data = assets->GetSprite(sprite->sprite);
            const TextureData* texture = data != nullptr ? assets->GetTexture(data->texture) : nullptr;
            Check(texture != nullptr && texture->width == 2 && texture->height == 2, "with its texture decoded at build time");
            const AudioData* sound = assets->GetAudio(assets->Load(soundId));
            Check(sound != nullptr && sound->streamPath.rfind("jpak:", 0) == 0, "the streamed sound points into the package");
            OwnerPtr<IFileStream> stream = engine.OpenAudioStream(sound->streamPath.c_str());
            Array<std::byte> streamed;
            streamed.Resize(wav.Size());
            Check(stream.Get() != nullptr && stream->Read(streamed.Data(), streamed.Size()) == wav.Size()
                    && std::memcmp(streamed.Data(), wav.Data(), wav.Size()) == 0,
                "the engine opens the mixer's stream from the package");
            stream.Reset();
            Check(engine.Tick(1.0f / 60.0f), "the packaged game ticks");
            engine.CloseProject();
        }
        // 패키지가 없으면 열지 않는다(조용히 빈 게임으로 뜨지 않는다).
        {
            fs::remove(output / "Content" / "game.jpak");
            Framework2D framework;
            ProjectFileError error;
            Check(false == engine.OpenProjectFile(framework, Utf8(output / "Probe Game.jproject").c_str(), error)
                    && error.message.find("asset package") != String::npos,
                "a project whose package is missing is refused");
        }
        engine.Shutdown();
        rhi.Shutdown();
        platform.Shutdown();
        fs::remove_all(root);
    }

    void TestAPackageRoundTrips()
    {
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform initializes");
        const fs::path root = ProcessTempFolder(L"JBroPackageProbe·패키지");
        fs::remove_all(root);
        fs::create_directories(root);
        const String path = Utf8(root / "game.jpak");

        const AssetId canvas = Uuid::FromName("probe canvas");
        const AssetId texture = Uuid::FromName("probe texture");
        const AssetId sprite = Uuid::FromName("probe sprite");
        const char* canvasText = "Objects:\n  - Name: hero secret-marker-canvas\n";
        const char* metaText = "Version: 1\nType: Texture\n";
        Array<std::byte> pixels;
        pixels.Resize(4 * 4 * 4);
        for (std::size_t index = 0; index < pixels.Size(); ++index)
        {
            pixels[index] = static_cast<std::byte>(index * 7 + 1);
        }

        PackageWriter writer(0x1234ABCD5678EF01ull);
        // 차례를 흩어 더한다. 파일에는 (id, kind) 차례로 놓인다.
        Check(writer.Add(Make(texture, AssetType::Texture, BlobKind::CookedTexture, "Art/hero.png"),
                  ArrayView<const std::byte>(pixels.Data(), pixels.Size())),
            "a cooked texture is added");
        Check(writer.Add(Make(canvas, AssetType::Canvas, BlobKind::Source, "Canvases/Main.jcanvas"), Bytes(canvasText)),
            "a canvas source is added");
        Check(writer.Add(Make(texture, AssetType::Texture, BlobKind::Meta, "Art/hero.png"), Bytes(metaText)), "a meta is added");
        Check(writer.Add(Make(sprite, AssetType::Sprite, BlobKind::Record, "Art/hero.png", texture), {}), "a record-only sprite is added");
        Check(writer.Add(Make(canvas, AssetType::Canvas, BlobKind::Meta, "Canvases/Main.jcanvas"), Bytes("")), "an empty blob is added");
        Check(false == writer.Add(Make(texture, AssetType::Texture, BlobKind::Meta, "Art/hero.png"), Bytes("again")),
            "the same asset and kind are refused twice");
        Check(false == writer.Add(Make(AssetId{}, AssetType::Texture, BlobKind::Meta, "x"), Bytes("x")), "an empty id is refused");
        String tooLong;
        tooLong.assign(MaxPathBytes + 1, 'a');
        Check(false == writer.Add(Make(Uuid::FromName("long"), AssetType::Texture, BlobKind::Meta, tooLong.c_str()), Bytes("x")),
            "a path longer than the limit is refused");
        String error;
        Check(writer.Save(platform, path.c_str(), error), "the package is written");

        // 겉으로는 원문이 보이지 않는다(일반 도구로 열리지 않는다).
        const Array<std::byte> file = ReadFile(platform, root / "game.jpak");
        Check(false == Contains(file, "secret-marker-canvas") && false == Contains(file, "Canvases/Main.jcanvas")
                && false == Contains(file, "Type: Texture"),
            "neither the blobs nor the paths are readable in the file");

        PackageReader reader;
        Check(reader.Open(platform, path.c_str(), error), "the package opens");
        Check(reader.GetEntryCount() == 5, "every record is in the index");
        for (JBro::UInt32 row = 1; row < reader.GetEntryCount(); ++row)
        {
            Check(EntryLess(reader.GetEntry(row - 1), reader.GetEntry(row)), "the index is in id and kind order");
        }
        for (JBro::UInt32 row = 0; row < reader.GetEntryCount(); ++row)
        {
            const Entry& entry = reader.GetEntry(row);
            Check(entry.kind == BlobKind::Record || entry.offset % BlobAlignment == 0, "every blob starts on 16 bytes");
        }
        const Entry* cooked = reader.Find(texture, BlobKind::CookedTexture);
        Check(cooked != nullptr && cooked->path == "Art/hero.png" && cooked->type == AssetType::Texture, "a blob is found by id and kind");
        Array<std::byte> read;
        Check(reader.ReadBlob(*cooked, read) && read.Size() == pixels.Size() && std::memcmp(read.Data(), pixels.Data(), read.Size()) == 0,
            "the cooked pixels come back byte for byte");
        const Entry* source = reader.Find(canvas, BlobKind::Source);
        Check(source != nullptr && reader.ReadBlob(*source, read) && read.Size() == std::strlen(canvasText)
                && std::memcmp(read.Data(), canvasText, read.Size()) == 0,
            "the canvas source comes back");
        const Entry* empty = reader.Find(canvas, BlobKind::Meta);
        Check(empty != nullptr && reader.ReadBlob(*empty, read) && read.Size() == 0, "an empty blob reads as empty");
        const Entry* record = reader.Find(sprite, BlobKind::Record);
        Check(record != nullptr && record->owner == texture && false == reader.ReadBlob(*record, read), "a record has an owner and no blob");
        Check(reader.Find(sprite, BlobKind::Meta) == nullptr && reader.Find(Uuid::FromName("absent"), BlobKind::Meta) == nullptr,
            "a missing blob is not found");

        // 창 스트림: 블롭 안만 보이고, 어디서든 읽기 시작할 수 있다.
        OwnerPtr<IFileStream> stream = reader.OpenBlobStream(*source);
        Check(stream.Get() != nullptr && stream->GetSize() == static_cast<JBro::Int64>(std::strlen(canvasText)), "a blob stream has the blob's size");
        char part[8] = {};
        Check(stream->Seek(3, FileSeekOrigin::Begin) && stream->Read(part, 5) == 5 && std::memcmp(part, canvasText + 3, 5) == 0,
            "a blob stream reads from the middle");
        Check(stream->Tell() == 8, "and moves on");
        char tail[256] = {};
        Check(stream->Seek(-4, FileSeekOrigin::End) && stream->Read(tail, sizeof(tail)) == 4, "reading past the end stops at the blob's end");
        Check(stream->Read(tail, sizeof(tail)) == 0, "and then reads nothing");
        Check(false == stream->Seek(1, FileSeekOrigin::End) && false == stream->Seek(-1, FileSeekOrigin::Begin),
            "a blob stream refuses to seek outside the blob");
        stream.Reset();
        reader.Close();
        Check(false == reader.IsOpen(), "a closed reader is closed");

        // 블롭 한 바이트를 바꾸면 그 블롭만 읽히지 않는다.
        {
            Check(reader.Open(platform, path.c_str(), error), "the package opens again");
            const JBro::UInt64 at = reader.Find(texture, BlobKind::CookedTexture)->offset;
            reader.Close();
            Array<std::byte> damaged = file;
            damaged[static_cast<std::size_t>(at + 5)] ^= std::byte{0x40};
            WriteFile(root / "damaged.jpak", damaged);
            const String damagedPath = Utf8(root / "damaged.jpak");
            Check(reader.Open(platform, damagedPath.c_str(), error), "a package with a damaged blob still opens");
            Check(false == reader.ReadBlob(*reader.Find(texture, BlobKind::CookedTexture), read) && read.IsEmpty(),
                "the damaged blob is refused");
            Check(reader.ReadBlob(*reader.Find(canvas, BlobKind::Source), read), "the other blobs still read");
            reader.Close();
        }
        // 색인 한 바이트·표지·판·잘린 파일은 열지 않는다.
        {
            Array<std::byte> damaged = file;
            damaged[damaged.Size() - 3] ^= std::byte{0x01};
            WriteFile(root / "index.jpak", damaged);
            const String indexPath = Utf8(root / "index.jpak");
            Check(false == reader.Open(platform, indexPath.c_str(), error) && false == error.empty(), "a damaged index is refused");

            Array<std::byte> foreign = file;
            foreign[0] = std::byte{'X'};
            WriteFile(root / "foreign.jpak", foreign);
            const String foreignPath = Utf8(root / "foreign.jpak");
            Check(false == reader.Open(platform, foreignPath.c_str(), error), "a file that is not a package is refused");

            Array<std::byte> newer = file;
            newer[8] = std::byte{2};
            WriteFile(root / "newer.jpak", newer);
            const String newerPath = Utf8(root / "newer.jpak");
            Check(false == reader.Open(platform, newerPath.c_str(), error), "another format version is refused");

            Array<std::byte> cut = file;
            cut.Resize(cut.Size() - 10);
            WriteFile(root / "cut.jpak", cut);
            const String cutPath = Utf8(root / "cut.jpak");
            Check(false == reader.Open(platform, cutPath.c_str(), error), "a cut file is refused");
            Check(false == reader.IsOpen(), "a refused package leaves the reader closed");
        }
        // 색인 해시가 맞아도 내용이 틀리면 열지 않는다: 같은 (id, kind) 둘, 블롭이 색인을 넘는 것.
        {
            const Array<std::byte> repeated = RewriteIndex(file, [](std::byte* index) {
                // 둘째 줄의 아이디와 종류를 첫 줄의 것으로 덮는다(아이디 16 바이트, 종류는 18 번째).
                const JBro::UInt32 firstPath = [&] { JBro::UInt32 length = 0; std::memcpy(&length, index + 20, 4); return length; }();
                std::byte* second = index + RecordFixedSize + firstPath;
                std::memcpy(second, index, 16);
                second[18] = index[18];
            });
            WriteFile(root / "repeated.jpak", repeated);
            Check(false == reader.Open(platform, Utf8(root / "repeated.jpak").c_str(), error), "an index that repeats a record is refused");
            const Array<std::byte> overflowing = RewriteIndex(file, [](std::byte* index) {
                // 첫 줄은 (id, kind) 가 가장 작은 것이다. 블롭이 있는 줄을 찾아 크기를 파일보다 크게 한다.
                std::byte* row = index;
                for (;;)
                {
                    JBro::UInt32 length = 0;
                    std::memcpy(&length, row + 20, 4);
                    if (row[18] != std::byte{ 0 })
                    {
                        const JBro::UInt64 huge = 1ull << 40;
                        std::memcpy(row + 48, &huge, 8);
                        return;
                    }
                    row += RecordFixedSize + length;
                }
            });
            WriteFile(root / "overflowing.jpak", overflowing);
            Check(false == reader.Open(platform, Utf8(root / "overflowing.jpak").c_str(), error), "a blob that runs past the index is refused");
        }
        // 섞기는 자리마다 다르다. 0 으로 채운 블롭이 8 바이트마다 되풀이되면 한 낱말로 섞은 것이다(풀기 쉽다).
        {
            PackageWriter zeros(0x77ull);
            Array<std::byte> blank;
            blank.Resize(64);
            Check(zeros.Add(Make(canvas, AssetType::Canvas, BlobKind::Source, "z"), ArrayView<const std::byte>(blank.Data(), blank.Size())), "added");
            Array<std::byte> built;
            zeros.Build(built);
            Check(std::memcmp(built.Data() + HeaderSize, built.Data() + HeaderSize + 8, 8) != 0
                    && std::memcmp(built.Data() + HeaderSize, built.Data() + HeaderSize + 32, 8) != 0,
                "the scrambling changes with the position in the file");
        }
        // 키가 다르면 같은 내용도 다른 바이트다.
        {
            PackageWriter other(0x0BADF00Dull);
            Check(other.Add(Make(canvas, AssetType::Canvas, BlobKind::Source, "Canvases/Main.jcanvas"), Bytes(canvasText)), "added");
            PackageWriter same(0x1234ABCD5678EF01ull);
            Check(same.Add(Make(canvas, AssetType::Canvas, BlobKind::Source, "Canvases/Main.jcanvas"), Bytes(canvasText)), "added");
            Array<std::byte> a;
            Array<std::byte> b;
            other.Build(a);
            same.Build(b);
            Check(a.Size() == b.Size() && std::memcmp(a.Data() + HeaderSize, b.Data() + HeaderSize, 16) != 0,
                "another build key scrambles the same blob differently");
        }
        fs::remove_all(root);
        platform.Shutdown();
    }
}

JBro::Int32 RunPackageTests()
{
    try
    {
        JBRO_CASE(TestAPackageRoundTrips());
        JBRO_CASE(TestAssetsLoadTheSameFromAPackage());
        JBRO_CASE(TestAModelShipsAsCookedMeshesAndImages());
        JBRO_CASE(TestASkinnedModelShipsWithItsSkeletonAndClips());
        JBRO_CASE(TestIdsAreFoundInText());
        JBRO_CASE(TestAGameBuildRunsWithoutTheSourceProject());
    }
    catch (const std::exception&)
    {
        return 1;
    }
    std::cout << "package tests passed\n";
    return 0;
}
