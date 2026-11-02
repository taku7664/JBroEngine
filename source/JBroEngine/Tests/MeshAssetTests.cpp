#include <JBro/Asset/Asset.h>
#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Asset/MeshFile.h>
#include <JBro/Package/PackageAssetSource.h>
#include <JBro/Package/PackageCollect.h>
#include <JBro/Package/PackageCook.h>
#include <JBro/Package/PackageReader.h>
#include <JBro/Package/PackageWriter.h>
#include <JBro/Platform/WindowsPlatform.h>

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

// **메시 에셋**(D-366): glTF 읽기(임베드·GLB·바깥 버퍼·노드 변환·감김·법선 만들기·거절), 에셋 시스템의 싣기와 다시 읽기, 패키지의 푼 메시.
// 렌더러에 올려 그리는 것은 `EditorApplication` 의 3D 시나리오가 본다.
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

    JBro::Bool Near(JBro::Float a, JBro::Float b)
    {
        return std::fabs(a - b) < 1e-4f;
    }

    // 바닥 없는 사각형 하나의 버퍼: 정점 넷의 위치(48 B), UV(32 B), 인덱스 여섯(12 B, UInt16).
    std::string QuadBuffer()
    {
        const float positions[12] = {-1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f};
        const float uvs[8] = {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        std::string out;
        out.append(reinterpret_cast<const char*>(positions), sizeof(positions));
        out.append(reinterpret_cast<const char*>(uvs), sizeof(uvs));
        out.append(reinterpret_cast<const char*>(indices), sizeof(indices));
        return out;
    }

    std::string Base64(const std::string& bytes)
    {
        static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        std::size_t at = 0;
        for (; at + 2 < bytes.size(); at += 3)
        {
            const unsigned value = (static_cast<unsigned char>(bytes[at]) << 16) | (static_cast<unsigned char>(bytes[at + 1]) << 8) | static_cast<unsigned char>(bytes[at + 2]);
            out.push_back(table[(value >> 18) & 63]);
            out.push_back(table[(value >> 12) & 63]);
            out.push_back(table[(value >> 6) & 63]);
            out.push_back(table[value & 63]);
        }
        if (at + 1 == bytes.size())
        {
            const unsigned value = static_cast<unsigned char>(bytes[at]) << 16;
            out.push_back(table[(value >> 18) & 63]);
            out.push_back(table[(value >> 12) & 63]);
            out += "==";
        }
        else if (at + 2 == bytes.size())
        {
            const unsigned value = (static_cast<unsigned char>(bytes[at]) << 16) | (static_cast<unsigned char>(bytes[at + 1]) << 8);
            out.push_back(table[(value >> 18) & 63]);
            out.push_back(table[(value >> 12) & 63]);
            out.push_back(table[(value >> 6) & 63]);
            out += "=";
        }
        return out;
    }

    // 사각형 glTF 의 JSON 이다. `buffer` 는 buffers 칸의 글자, `node` 는 노드에 더할 글자다.
    std::string QuadJson(const std::string& buffer, const std::string& node = "", JBro::Bool withUv = true)
    {
        const std::string attributes = withUv ? "\"POSITION\":0,\"TEXCOORD_0\":1" : "\"POSITION\":0";
        return "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
               "\"nodes\":[{\"mesh\":0" + node + "}],"
               "\"meshes\":[{\"primitives\":[{\"attributes\":{" + attributes + "},\"indices\":2}]}],"
               "\"accessors\":["
               "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
               "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"},"
               "{\"bufferView\":2,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}],"
               "\"bufferViews\":["
               "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48},"
               "{\"buffer\":0,\"byteOffset\":48,\"byteLength\":32},"
               "{\"buffer\":0,\"byteOffset\":80,\"byteLength\":12}],"
               "\"buffers\":[" + buffer + "]}";
    }

    std::string EmbeddedBuffer()
    {
        const std::string bytes = QuadBuffer();
        return "{\"byteLength\":" + std::to_string(bytes.size()) + ",\"uri\":\"data:application/octet-stream;base64," + Base64(bytes) + "\"}";
    }

    JBro::Bool Parse(const std::string& text, MeshAssetData& mesh, String* error = nullptr)
    {
        return ParseGltfMesh(reinterpret_cast<const std::byte*>(text.data()), text.size(), nullptr, nullptr, mesh, error);
    }

    void TestAnEmbeddedQuadReads()
    {
        MeshAssetData mesh;
        String error;
        Check(Parse(QuadJson(EmbeddedBuffer()), mesh, &error), "an embedded glTF reads");
        Check(mesh.vertices.Size() == 4 * 8 && mesh.indices.Size() == 6, "four vertices and two triangles");
        Check(Near(mesh.vertices[0], -1.0f) && Near(mesh.vertices[8 + 1], -1.0f) && Near(mesh.vertices[6], 0.0f) && Near(mesh.vertices[7], 1.0f),
            "positions and UVs come through");
        // 엔진의 앞면(빌트인 정육면체의 0, 2, 1 / 0, 3, 2 와 같은 감김)이다. glTF 의 0, 1, 2 / 0, 2, 3 을 뒤집는다.
        Check(mesh.indices[0] == 0 && mesh.indices[1] == 2 && mesh.indices[2] == 1 && mesh.indices[3] == 0 && mesh.indices[4] == 3 && mesh.indices[5] == 2,
            "the winding becomes the engine's front face");
        // 법선이 없으면 면에서 만든다 - 이 사각형은 +Z 를 본다.
        Check(Near(mesh.vertices[3], 0.0f) && Near(mesh.vertices[4], 0.0f) && Near(mesh.vertices[5], 1.0f), "a missing normal is made from the face");

        MeshAssetData plain;
        Check(Parse(QuadJson(EmbeddedBuffer(), "", false), plain), "a mesh without UVs reads");
        Check(Near(plain.vertices[6], 0.0f) && Near(plain.vertices[7], 0.0f), "and its UVs are zero");
    }

    void TestNodeTransformsAreBaked()
    {
        MeshAssetData moved;
        Check(Parse(QuadJson(EmbeddedBuffer(), ",\"translation\":[10,0,0],\"scale\":[2,2,2]"), moved), "a moved node reads");
        Check(Near(moved.vertices[0], 8.0f) && Near(moved.vertices[1], -2.0f), "translation and scale are baked into the vertices");
        Check(Near(moved.vertices[5], 1.0f), "the normal stays unit length");

        // 음의 크기는 거울이다. 감김을 한 번 더 뒤집어 앞면이 바깥을 유지한다.
        MeshAssetData mirrored;
        Check(Parse(QuadJson(EmbeddedBuffer(), ",\"scale\":[-1,1,1]"), mirrored), "a mirrored node reads");
        Check(Near(mirrored.vertices[0], 1.0f) && mirrored.indices[1] == 1 && mirrored.indices[2] == 2, "a mirrored node flips the winding back");

        // 90 도 돌린 노드(+Y 축): +Z 를 보던 면이 +X 를 본다.
        MeshAssetData turned;
        Check(Parse(QuadJson(EmbeddedBuffer(), ",\"rotation\":[0,0.7071068,0,0.7071068]"), turned), "a rotated node reads");
        Check(Near(turned.vertices[3], 1.0f) && Near(turned.vertices[5], 0.0f), "the made normal follows the rotation");
    }

    void TestAGlbReads()
    {
        const std::string binary = QuadBuffer();
        std::string json = QuadJson("{\"byteLength\":" + std::to_string(binary.size()) + "}");
        while (json.size() % 4 != 0)
        {
            json.push_back(' ');
        }
        const auto u32 = [](std::string& out, std::uint32_t value) { out.append(reinterpret_cast<const char*>(&value), 4); };
        std::string glb;
        u32(glb, 0x46546C67u);
        u32(glb, 2u);
        u32(glb, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + binary.size()));
        u32(glb, static_cast<std::uint32_t>(json.size()));
        u32(glb, 0x4E4F534Au);
        glb += json;
        u32(glb, static_cast<std::uint32_t>(binary.size()));
        u32(glb, 0x004E4942u);
        glb += binary;
        MeshAssetData mesh;
        Check(Parse(glb, mesh), "a GLB reads");
        Check(mesh.vertices.Size() == 32 && mesh.indices.Size() == 6, "with its binary chunk");
        Check(false == Parse(glb.substr(0, 40), mesh), "a cut GLB is refused");
    }

    void TestBrokenFilesAreRefused()
    {
        const auto refuses = [](const std::string& text, const char* expected, const char* message) {
            MeshAssetData mesh;
            String error;
            const JBro::Bool read = Parse(text, mesh, &error);
            if (read || false == error.Contains(expected))
            {
                std::cout << "  reader said: " << error.c_str() << '\n';
            }
            Check(false == read && error.Contains(expected), message);
        };
        refuses("{not json", "broken", "broken JSON is refused");
        refuses(QuadJson("{\"byteLength\":92,\"uri\":\"missing.bin\"}"), "buffer file", "a missing buffer file is refused");
        refuses(QuadJson("{\"byteLength\":4,\"uri\":\"data:application/octet-stream;base64,AAAAAA==\"}"), "past", "an accessor past its buffer is refused");
        refuses("{\"asset\":{\"version\":\"2.0\"},\"meshes\":[]}", "no triangles", "a file without triangles is refused");
        refuses("{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{}}]}]}", "POSITION", "a primitive without positions is refused");
        refuses("{\"asset\":{\"version\":\"2.0\"},\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":\"x\"}}]}]}", "wrong type", "a value of the wrong type is refused");
        // 고리 달린 노드는 멈춘다.
        refuses("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"children\":[0]}]}", "node", "a node cycle is refused");
    }

    void TestTheCookedMeshRoundTrips()
    {
        MeshAssetData mesh;
        Check(Parse(QuadJson(EmbeddedBuffer()), mesh), "the quad reads");
        Array<std::byte> blob;
        WriteCookedMesh(mesh, blob);
        MeshAssetData back;
        Check(ReadCookedMesh(blob.Data(), blob.Size(), back) && back.vertices.Size() == 32 && back.indices.Size() == 6 && Near(back.vertices[8], 1.0f),
            "a cooked mesh reads back");
        Array<std::byte> cut = blob;
        cut.Resize(cut.Size() - 1);
        Check(false == ReadCookedMesh(cut.Data(), cut.Size(), back), "a cut blob is refused");
        Array<std::byte> wild = blob;
        const std::uint32_t far = 100000;
        std::memcpy(wild.Data() + wild.Size() - 4, &far, 4);
        Check(false == ReadCookedMesh(wild.Data(), wild.Size(), back), "an index past the vertices is refused");
    }

    void WriteFile(const fs::path& path, const std::string& bytes)
    {
        fs::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        Check(file.good(), "the test writes its own file");
    }

    // **에셋 시스템이 메시를 싣는다.** 바깥 버퍼(`Quad.bin`)가 옆 폴더에 있어도 읽고, 폴더 밖을 가리키는 URI 는 거절한다. 파일을 고쳐 다시 읽으면 세대가 오른다.
    // 패키지는 푼 메시만 싣는다 - 원본과 바깥 버퍼를 지워도 게임이 읽는다.
    void TestTheAssetSystemLoadsAndShipsAMesh()
    {
        WindowsPlatform platform;
        JMemoryContext memory;
        Check(platform.Initialize(memory), "the platform initializes");
        const fs::path root = fs::temp_directory_path() / ("JBroMeshAsset" + std::to_string(_getpid()));
        std::error_code ignored;
        fs::remove_all(root, ignored);
        const fs::path assets = root / "Assets";
        WriteFile(assets / "Models" / "Quad.gltf", QuadJson("{\"byteLength\":92,\"uri\":\"Quad%20Data.bin\"}"));
        WriteFile(assets / "Models" / "Quad Data.bin", QuadBuffer());
        WriteFile(assets / "Models" / "Escape.gltf", QuadJson("{\"byteLength\":92,\"uri\":\"../../Secret.bin\"}"));
        WriteFile(root / "Secret.bin", QuadBuffer());
        AssetRegistry registry;
        AssetScanOptions options;
        options.createMissingMeta = true;
        AssetScanReport report;
        const std::u8string text = assets.generic_u8string();
        const String assetRoot(reinterpret_cast<const char*>(text.data()), text.size());
        Check(registry.Scan(platform, assetRoot.c_str(), options, report), "the folder scans");
        const AssetRecord* record = registry.FindByPath("Models/Quad.gltf");
        Check(record != nullptr && record->type == AssetType::Mesh, "a .gltf file is a mesh asset");
        const AssetId meshId = record->id;
        const AssetId escapeId = registry.FindByPath("Models/Escape.gltf")->id;
        {
            AssetSystem loose;
            Check(loose.Initialize(memory), "the asset system initializes");
            loose.Bind(platform, registry, assetRoot.c_str());
            const AssetHandle handle = loose.Load(meshId);
            const MeshAssetData* mesh = loose.GetMesh(handle);
            Check(mesh != nullptr && mesh->vertices.Size() == 32, "a mesh loads through a sibling buffer with an escaped name");
            Check(loose.GetReferenceCount(handle) == 1 && loose.Load(meshId).index == handle.index && loose.GetReferenceCount(handle) == 2,
                "a second load shares the slot");
            Check(loose.Load(escapeId).generation == 0, "a buffer outside the asset folder is refused");
            const UInt32 generation = mesh->dataGeneration;
            WriteFile(assets / "Models" / "Quad.gltf", QuadJson("{\"byteLength\":92,\"uri\":\"Quad%20Data.bin\"}", ",\"translation\":[5,0,0]"));
            Check(loose.ReloadInPlace(meshId), "the mesh reloads in place");
            mesh = loose.GetMesh(handle);
            Check(mesh != nullptr && mesh->dataGeneration == generation + 1 && Near(mesh->vertices[0], 4.0f), "with the new vertices and a new generation");
            loose.Release(handle);
            loose.Release(handle);
            Check(loose.CollectUnused() >= 1 && loose.GetMesh(handle) == nullptr, "an unused mesh is freed");
            loose.Shutdown();
        }

        Array<AssetId> ids;
        ids.Add(meshId);
        Package::PackageWriter writer(0x5EEDull);
        Package::CookReport cooked;
        const JBro::Bool ok = Package::CookAssets(platform, registry, assetRoot.c_str(), ArrayView<const AssetId>(ids.Data(), ids.Size()), writer, cooked);
        for (const String& failure : cooked.failures)
        {
            std::cout << "  cook said: " << failure.c_str() << '\n';
        }
        Check(ok, "the mesh cooks");
        const std::u8string packageText = (root / "game.jpak").generic_u8string();
        const String packagePath(reinterpret_cast<const char*>(packageText.data()), packageText.size());
        String error;
        Check(writer.Save(platform, packagePath.c_str(), error), "the package is written");
        fs::remove_all(assets, ignored);

        Package::PackageReader package;
        Check(package.Open(platform, packagePath.c_str(), error), "the package opens");
        Check(package.Find(meshId, Package::BlobKind::CookedMesh) != nullptr && package.Find(meshId, Package::BlobKind::Source) == nullptr,
            "the mesh ships cooked, not as its text");
        AssetRegistry packed;
        Check(Package::FillRegistry(package, packed) == 1, "the registry is filled from the index");
        Package::PackageAssetSource source(package);
        AssetSystem game;
        Check(game.Initialize(memory), "the packaged asset system initializes");
        game.Bind(platform, packed, source);
        const MeshAssetData* shipped = game.GetMesh(game.Load(meshId));
        Check(shipped != nullptr && shipped->indices.Size() == 6, "the game reads the cooked mesh with the source files gone");
        game.Shutdown();
        package.Close();
        platform.Shutdown();
        fs::remove_all(root, ignored);
    }
}

JBro::Int32 RunMeshAssetTests()
{
    TestAnEmbeddedQuadReads();
    TestNodeTransformsAreBaked();
    TestAGlbReads();
    TestBrokenFilesAreRefused();
    TestTheCookedMeshRoundTrips();
    TestTheAssetSystemLoadsAndShipsAMesh();
    std::cout << "Mesh asset tests passed." << std::endl;
    return 0;
}
