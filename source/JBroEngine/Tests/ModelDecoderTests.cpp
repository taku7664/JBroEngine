#include "TestCases.h"
#include "TestModels.h"
#include <JBro/Asset/ModelDecoder.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

// **모델 디코더**(D-338·D-339, model-import-plan 2 단계). 시험 모델은 내려받지 않고 여기서 짓는다 - glTF(데이터 URI·바깥 `.bin`·`.glb`), OBJ(`.mtl` 과 함께),
// ASCII FBX. 엔진 규약을 본다: 앞면은 바깥에서 볼 때 시계(삼각형의 외적이 법선과 반대), UV 는 왼쪽 위가 0, 단위는 미터.
namespace
{
    using TestModels::Append;
    using TestModels::Base64;
    using TestModels::QuadBuffer;
    using TestModels::QuadGltf;

    void Check(JBro::Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cout << "test failure: " << message << '\n';
            throw std::runtime_error(message);
        }
    }

    JBro::Bool Near(JBro::Float a, JBro::Float b, JBro::Float tolerance = 0.0001f)
    {
        return std::fabs(a.Get() - b.Get()) <= tolerance.Get();
    }

    JBro::JArrayView<std::byte> View(const std::string& text)
    {
        return {reinterpret_cast<const std::byte*>(text.data()), static_cast<JBro::UInt32>(text.size())};
    }

    // 그림 바이트다. 디코더는 그림을 풀지 않으므로 아무 바이트면 된다 - 같은지와 해시만 본다.
    const std::string FakeImage = std::string("\x89PNG\r\n\x1a\nnot really an image", 28);

    // 곁 파일 읽기의 시험용 판이다. 이름 → 바이트 표에서 주고, 물어본 이름을 남긴다.
    struct SideFiles
    {
        std::map<std::string, std::string> files;
        std::string asked;

        static JBro::Bool Read(void* user, const char* path, JBro::Array<std::byte>& bytes)
        {
            SideFiles& self = *static_cast<SideFiles*>(user);
            self.asked += path;
            self.asked += ";";
            const auto found = self.files.find(path);
            if (found == self.files.end())
            {
                return false;
            }
            bytes.Resize(found->second.size());
            std::memcpy(bytes.Data(), found->second.data(), found->second.size());
            return true;
        }

        JBro::ModelFileReader Reader()
        {
            JBro::ModelFileReader reader;
            reader.read = &Read;
            reader.user = this;
            return reader;
        }
    };

    JBro::Vector3 Position(const JBro::ModelVertex& vertex)
    {
        return JBro::Vector3{vertex.position[0], vertex.position[1], vertex.position[2]};
    }

    // **엔진의 앞면은 바깥에서 볼 때 시계다** - 삼각형마다 외적이 정점 법선과 반대를 가리켜야 한다.
    void CheckEngineWinding(const JBro::ModelMesh& mesh, const char* message)
    {
        Check(mesh.indices.Size() > 0 && mesh.indices.Size() % 3 == 0, "a mesh is a list of triangles");
        for (std::size_t face = 0; face < mesh.indices.Size() / 3; ++face)
        {
            const JBro::ModelVertex& a = mesh.vertices[mesh.indices[face * 3].Get()];
            const JBro::ModelVertex& b = mesh.vertices[mesh.indices[face * 3 + 1].Get()];
            const JBro::ModelVertex& c = mesh.vertices[mesh.indices[face * 3 + 2].Get()];
            const JBro::Vector3 cross = JBro::Cross(JBro::Subtract(Position(b), Position(a)), JBro::Subtract(Position(c), Position(a)));
            const JBro::Vector3 normal{a.normal[0] + b.normal[0] + c.normal[0], a.normal[1] + b.normal[1] + c.normal[1], a.normal[2] + b.normal[2] + c.normal[2]};
            Check(JBro::Dot(cross, normal) < 0.0f, message);
        }
    }

    void TestFormatsComeFromTheExtension()
    {
        Check(JBro::ModelFormatFromPath("art/Hero.FBX") == JBro::ModelFormat::Fbx, "the extension picks FBX, ignoring case");
        Check(JBro::ModelFormatFromPath("a.gltf") == JBro::ModelFormat::Gltf && JBro::ModelFormatFromPath("a.glb") == JBro::ModelFormat::Glb,
            "glTF text and binary");
        Check(JBro::ModelFormatFromPath("a.obj") == JBro::ModelFormat::Obj, "OBJ");
        Check(JBro::ModelFormatFromPath("a.png") == JBro::ModelFormat::Unknown && JBro::ModelFormatFromPath("obj") == JBro::ModelFormat::Unknown,
            "anything else is unknown");
        JBro::ModelData model;
        JBro::String error;
        Check(false == JBro::DecodeModel(View("x"), JBro::ModelFormat::Unknown, {}, {}, model, error) && false == error.empty(),
            "an unknown format is refused with a reason");
        Check(false == JBro::DecodeModel({}, JBro::ModelFormat::Gltf, {}, {}, model, error), "an empty file is refused");
        Check(false == JBro::DecodeModel(View("{ not json"), JBro::ModelFormat::Gltf, {}, {}, model, error), "a broken glTF is refused");
        Check(JBro::HashModelBytes(View(FakeImage)) == JBro::HashModelBytes(View(FakeImage)) && JBro::HashModelBytes(View(FakeImage)) != 0,
            "the content hash is stable and never the empty value");
        // 길이가 같고 한 바이트만 다른 그림이다 - 길이만 섞는 해시는 이것을 가르지 못한다(뮤테이션이 처음 시험을 지나갔다).
        std::string changed = FakeImage;
        changed[10] = static_cast<char>(changed[10] ^ 0x20);
        Check(JBro::HashModelBytes(View(FakeImage)) != JBro::HashModelBytes(View(changed)), "and changes with any one byte");
    }

    // **데이터 URI 하나로 된 glTF** - 메시·재질·내장 그림·노드가 다 나온다.
    void TestAGltfWithDataUrisDecodes()
    {
        const std::string buffer = QuadBuffer();
        const std::string json = QuadGltf("data:application/octet-stream;base64," + Base64(buffer), buffer.size(),
            "{\"name\":\"brick.png\",\"uri\":\"data:image/png;base64," + Base64(FakeImage) + "\"}");
        JBro::ModelData model;
        JBro::String error;
        Check(JBro::DecodeModel(View(json), JBro::ModelFormat::Gltf, {}, {}, model, error), "a self-contained glTF decodes");
        Check(model.meshes.Size() == 1 && model.meshes[0].name == "Quad", "its one mesh keeps its name");
        const JBro::ModelMesh& mesh = model.meshes[0];
        Check(mesh.vertices.Size() == 4 && mesh.indices.Size() == 6, "the quad is four vertices and two triangles");
        Check(mesh.hasUvs && false == mesh.hasTangents && mesh.material == 0, "with UVs, no tangents, and its material");
        CheckEngineWinding(mesh, "glTF triangles are turned to the engine's clockwise front");
        for (std::size_t index = 0; index < mesh.vertices.Size(); ++index)
        {
            Check(Near(mesh.vertices[index].normal[1], 1.0f), "the file's normals are kept");
        }
        // glTF 의 UV 는 엔진과 같은 규약이라 그대로다. 위치 (0,0,1) 의 UV 는 (0,1) 이다.
        JBro::Bool foundCorner = false;
        for (std::size_t index = 0; index < mesh.vertices.Size(); ++index)
        {
            const JBro::ModelVertex& vertex = mesh.vertices[index];
            if (Near(vertex.position[2], 1.0f) && Near(vertex.position[0], 0.0f))
            {
                foundCorner = Near(vertex.uv[0], 0.0f) && Near(vertex.uv[1], 1.0f);
            }
        }
        Check(foundCorner, "glTF UVs pass through unchanged");
        Check(model.materials.Size() == 1 && model.materials[0].name == "Brick", "the material keeps its name");
        Check(Near(model.materials[0].baseColor[1], 0.5f) && Near(model.materials[0].metallic, 0.2f) && Near(model.materials[0].roughness, 0.7f),
            "and its factors");
        Check(model.materials[0].images[0] == 0, "and points its base colour at the image");
        Check(model.images.Size() == 1 && model.images[0].path.empty() && model.images[0].embedded.Size() == FakeImage.size()
                && std::memcmp(model.images[0].embedded.Data(), FakeImage.data(), FakeImage.size()) == 0,
            "the data-URI image is embedded byte for byte");
        Check(model.images[0].contentHash == JBro::HashModelBytes(View(FakeImage)), "with its content hash");
        Check(model.images[0].firstUse == JBro::ModelTextureSlot::BaseColor, "and knows it is first used as base colour (sRGB)");
        Check(model.materials[0].emissive[0] == 0.0f && false == model.materials[0].alphaCutout, "a plain material glows nothing and is opaque");
        {
            // 발광 색과 `MASK`(컷아웃)를 읽는다(material-plan 2 단계).
            std::string masked = json;
            const std::string from = "\"materials\":[{\"name\":\"Brick\",";
            masked.replace(masked.find(from), from.size(), from + "\"emissiveFactor\":[0.5,0.25,0],\"alphaMode\":\"MASK\",\"alphaCutoff\":0.3,");
            JBro::ModelData glowing;
            Check(JBro::DecodeModel(View(masked), JBro::ModelFormat::Gltf, {}, {}, glowing, error), "a glTF with a masked, glowing material decodes");
            Check(Near(glowing.materials[0].emissive[0], 0.5f) && Near(glowing.materials[0].emissive[1], 0.25f) && Near(glowing.materials[0].emissive[2], 0.0f),
                "the emissive factor is read");
            Check(glowing.materials[0].alphaCutout && Near(glowing.materials[0].alphaCutoff, 0.3f), "and MASK becomes a cutout with its threshold");
        }
        Check(model.nodes.Size() == 1 && model.nodes[0].name == "Root" && model.nodes[0].parent == -1, "the node comes through as a root");
        Check(Near(model.nodes[0].position.y, 2.0f) && Near(model.nodes[0].scale.x, 2.0f), "with its transform");
        Check(model.nodes[0].meshes.Size() == 1 && model.nodes[0].meshes[0] == 0, "and the mesh it draws");

        // 배율은 정점과 노드 자리에 곱해진다.
        JBro::ModelImportOptions doubled;
        doubled.scale = 2.0f;
        JBro::ModelData scaled;
        Check(JBro::DecodeModel(View(json), JBro::ModelFormat::Gltf, doubled, {}, scaled, error), "the same file decodes with a scale");
        JBro::Float largest = 0.0f;
        for (std::size_t index = 0; index < scaled.meshes[0].vertices.Size(); ++index)
        {
            largest = std::max(largest.Get(), scaled.meshes[0].vertices[index].position[0].Get());
        }
        Check(Near(largest, 2.0f) && Near(scaled.nodes[0].position.y, 4.0f), "scale multiplies the vertices and the node positions");

        // 재질을 가져오지 않으면 재질도 그림도 없다.
        JBro::ModelImportOptions meshOnly;
        meshOnly.importMaterials = false;
        JBro::ModelData bare;
        Check(JBro::DecodeModel(View(json), JBro::ModelFormat::Gltf, meshOnly, {}, bare, error), "the file decodes without materials");
        Check(bare.materials.Size() == 0 && bare.images.Size() == 0 && bare.meshes[0].material == -1,
            "importMaterials off leaves no material or image sub-assets");
    }

    // **바깥 `.bin` 과 바깥 그림** - 디코더는 파일을 열지 않고 곁 파일 읽기로 받는다(D-112). 그림은 경로만 남는다.
    void TestAGltfReadsItsSideFilesThroughTheReader()
    {
        const std::string buffer = QuadBuffer();
        const std::string json = QuadGltf("Quad%20Data.bin", buffer.size(), "{\"uri\":\"textures/brick.png\"}");
        SideFiles side;
        side.files["Quad Data.bin"] = buffer;
        JBro::ModelData model;
        JBro::String error;
        Check(JBro::DecodeModel(View(json), JBro::ModelFormat::Gltf, {}, side.Reader(), model, error), "a glTF with an outside buffer decodes");
        Check(side.asked.find("Quad Data.bin") != std::string::npos, "the buffer is asked for by its decoded name");
        Check(model.meshes.Size() == 1 && model.meshes[0].vertices.Size() == 4, "and its mesh is there");
        Check(model.images.Size() == 1 && model.images[0].path == "textures/brick.png" && model.images[0].embedded.Size() == 0
                && model.images[0].contentHash == 0,
            "an outside image stays a path, with no bytes and no hash");
        SideFiles missing;
        JBro::ModelData nothing;
        Check(false == JBro::DecodeModel(View(json), JBro::ModelFormat::Gltf, {}, missing.Reader(), nothing, error) && false == error.empty(),
            "a missing buffer is refused with a reason");
    }

    // **`.glb`** - JSON 칸과 BIN 칸이다. 그림이 BIN 안의 버퍼 뷰면 내장 그림이다.
    void TestAGlbDecodes()
    {
        std::string bin = QuadBuffer() + FakeImage;
        const std::string json = QuadGltf("", bin.size(), "{\"name\":\"inside\",\"bufferView\":4,\"mimeType\":\"image/png\"}");
        std::string paddedJson = json;
        while (paddedJson.size() % 4 != 0)
        {
            paddedJson += ' ';
        }
        while (bin.size() % 4 != 0)
        {
            bin += '\0';
        }
        std::string glb;
        Append(glb, std::uint32_t{0x46546C67});
        Append(glb, std::uint32_t{2});
        Append(glb, static_cast<std::uint32_t>(12 + 8 + paddedJson.size() + 8 + bin.size()));
        Append(glb, static_cast<std::uint32_t>(paddedJson.size()));
        Append(glb, std::uint32_t{0x4E4F534A});
        glb += paddedJson;
        Append(glb, static_cast<std::uint32_t>(bin.size()));
        Append(glb, std::uint32_t{0x004E4942});
        glb += bin;
        JBro::ModelData model;
        JBro::String error;
        Check(JBro::DecodeModel(View(glb), JBro::ModelFormat::Glb, {}, {}, model, error), "a binary glTF decodes");
        Check(model.meshes.Size() == 1 && model.meshes[0].indices.Size() == 6, "its mesh is read from the BIN chunk");
        CheckEngineWinding(model.meshes[0], "GLB triangles are turned to the engine's clockwise front");
        Check(model.images.Size() == 1 && model.images[0].name == "inside" && model.images[0].embedded.Size() == FakeImage.size()
                && std::memcmp(model.images[0].embedded.Data(), FakeImage.data(), FakeImage.size()) == 0,
            "an image in a buffer view is embedded byte for byte");
    }

    // **OBJ 와 `.mtl`** - 재질 파일은 곁 파일 읽기로 받는다. OBJ 의 UV 는 아래가 0 이라 세로가 뒤집힌다.
    void TestAnObjWithItsMaterialFileDecodes()
    {
        const std::string obj =
            "mtllib quad.mtl\n"
            "v 0 0 0\nv 0 0 1\nv 1 0 1\nv 1 0 0\n"
            "vt 0 0\nvt 0 0.25\nvt 1 0.25\nvt 1 0\n"
            "vn 0 1 0\n"
            "usemtl Red\n"
            "f 1/1/1 2/2/1 3/3/1 4/4/1\n";
        SideFiles side;
        side.files["quad.mtl"] = "newmtl Red\nKd 1 0 0\nmap_Kd textures/red.png\n";
        JBro::ModelData model;
        JBro::String error;
        Check(JBro::DecodeModel(View(obj), JBro::ModelFormat::Obj, {}, side.Reader(), model, error), "an OBJ decodes");
        Check(side.asked.find("quad.mtl") != std::string::npos, "its material file is asked for through the reader");
        Check(model.meshes.Size() == 1, "the quad is one mesh");
        const JBro::ModelMesh& mesh = model.meshes[0];
        Check(mesh.vertices.Size() == 4 && mesh.indices.Size() == 6, "a four-sided face is split into two triangles over four vertices");
        CheckEngineWinding(mesh, "OBJ triangles are turned to the engine's clockwise front");
        JBro::Bool flipped = false;
        for (std::size_t index = 0; index < mesh.vertices.Size(); ++index)
        {
            const JBro::ModelVertex& vertex = mesh.vertices[index];
            if (Near(vertex.position[0], 0.0f) && Near(vertex.position[2], 1.0f))
            {
                flipped = Near(vertex.uv[1], 0.75f);
            }
        }
        Check(flipped, "OBJ UVs are flipped to the engine's top-left origin (0.25 becomes 0.75)");
        Check(model.materials.Size() == 1 && model.materials[0].name == "Red" && Near(model.materials[0].baseColor[0], 1.0f)
                && Near(model.materials[0].baseColor[1], 0.0f),
            "the .mtl material comes through with its colour");
        Check(mesh.material == 0 && model.materials[0].images[0] == 0, "the mesh uses it and it uses the diffuse map");
        Check(model.images.Size() == 1 && model.images[0].path == "textures/red.png", "the map stays a path relative to the model");
    }

    // **법선 계산과 매끈함의 각**(D-339). 모서리 하나를 90 도로 접은 두 삼각형이다 - 60 도면 모서리가 각져 꼭짓점이 갈리고, 120 도면 매끈해 합쳐진다.
    void TestCalculatedNormalsFollowTheSmoothingAngle()
    {
        const std::string fold = "v 0 0 0\nv 0 0 1\nv 1 0 0\nv 0 1 0\nf 1 2 3\nf 1 4 2\n";
        JBro::ModelImportOptions sharp;
        sharp.normals = JBro::ModelNormals::Calculate;
        sharp.smoothingAngle = 60.0f;
        JBro::ModelData sharpModel;
        JBro::String error;
        Check(JBro::DecodeModel(View(fold), JBro::ModelFormat::Obj, sharp, {}, sharpModel, error), "the folded pair decodes");
        Check(sharpModel.meshes[0].vertices.Size() == 6, "at 60 degrees the 90 degree fold is a hard edge - its two corners split");
        CheckEngineWinding(sharpModel.meshes[0], "calculated normals face out of the engine's front");
        JBro::ModelImportOptions smooth = sharp;
        smooth.smoothingAngle = 120.0f;
        JBro::ModelData smoothModel;
        Check(JBro::DecodeModel(View(fold), JBro::ModelFormat::Obj, smooth, {}, smoothModel, error), "the folded pair decodes again");
        Check(smoothModel.meshes[0].vertices.Size() == 4, "at 120 degrees the fold is smooth - the shared corners merge");
        JBro::Bool blended = false;
        for (std::size_t index = 0; index < smoothModel.meshes[0].vertices.Size(); ++index)
        {
            const JBro::ModelVertex& vertex = smoothModel.meshes[0].vertices[index];
            if (Near(vertex.position[0], 0.0f) && Near(vertex.position[1], 0.0f) && Near(vertex.position[2], 0.0f))
            {
                blended = Near(vertex.normal[0], 0.7071f, 0.001f) && Near(vertex.normal[1], 0.7071f, 0.001f);
            }
        }
        Check(blended, "a smooth shared corner points halfway between the two faces");
    }

    // **ASCII FBX 와 단위**. 센티미터로 적힌 50 은 `convertUnits` 면 0.5 미터, 끄면 50 그대로다.
    const char* const CentimetreQuadFbx =
        "; FBX 7.4.0 project file\n"
        "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n}\n"
        "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n"
        "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1\n"
        "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
        "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n"
        "\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n"
        "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
        "\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
        "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",1\n"
        "\t}\n}\n"
        "Objects:  {\n"
        "\tGeometry: 1000, \"Geometry::Floor\", \"Mesh\" {\n"
        "\t\tVertices: *12 {\n\t\t\ta: -50,0,-50,-50,0,50,50,0,50,50,0,-50\n\t\t}\n"
        "\t\tPolygonVertexIndex: *4 {\n\t\t\ta: 0,1,2,-4\n\t\t}\n"
        "\t\tGeometryVersion: 124\n"
        "\t}\n"
        "\tModel: 2000, \"Model::Floor\", \"Mesh\" {\n\t\tVersion: 232\n\t}\n"
        "\tMaterial: 3000, \"Material::Red\", \"\" {\n\t\tVersion: 102\n\t\tShadingModel: \"phong\"\n"
        "\t\tProperties70:  {\n\t\t\tP: \"DiffuseColor\", \"Color\", \"\", \"A\",1,0,0\n\t\t}\n\t}\n"
        "}\n"
        "Connections:  {\n"
        "\tC: \"OO\",1000,2000\n"
        "\tC: \"OO\",2000,0\n"
        "\tC: \"OO\",3000,2000\n"
        "}\n";

    void TestAnFbxDecodesWithItsUnits()
    {
        JBro::ModelData model;
        JBro::String error;
        if (false == JBro::DecodeModel(View(CentimetreQuadFbx), JBro::ModelFormat::Fbx, {}, {}, model, error))
        {
            std::cout << "  fbx error: " << error.c_str() << std::endl;
            Check(false, "an ASCII FBX decodes");
        }
        Check(model.meshes.Size() == 1 && model.meshes[0].name == "Floor", "its mesh takes the node's name when the geometry has none of its own");
        const JBro::ModelMesh& mesh = model.meshes[0];
        Check(mesh.vertices.Size() == 4 && mesh.indices.Size() == 6, "the quad is two triangles over four vertices");
        CheckEngineWinding(mesh, "FBX triangles are turned to the engine's clockwise front");
        JBro::Float largest = 0.0f;
        for (std::size_t index = 0; index < mesh.vertices.Size(); ++index)
        {
            largest = std::max(largest.Get(), std::fabs(mesh.vertices[index].position[0].Get()));
            Check(Near(mesh.vertices[index].normal[1], 1.0f), "the missing normals are generated facing up");
        }
        Check(Near(largest, 0.5f), "centimetres become metres - 50 is 0.5");
        Check(model.materials.Size() == 1 && model.materials[0].name == "Red" && Near(model.materials[0].baseColor[0], 1.0f)
                && Near(model.materials[0].baseColor[1], 0.0f),
            "the phong material comes through with its diffuse colour");
        Check(mesh.material == 0, "and the mesh uses it");
        Check(model.nodes.Size() == 1 && model.nodes[0].name == "Floor" && model.nodes[0].meshes.Size() == 1, "the model node draws the mesh");

        JBro::ModelImportOptions raw;
        raw.convertUnits = false;
        JBro::ModelData unconverted;
        Check(JBro::DecodeModel(View(CentimetreQuadFbx), JBro::ModelFormat::Fbx, raw, {}, unconverted, error), "the same FBX decodes without converting");
        JBro::Float rawLargest = 0.0f;
        for (std::size_t index = 0; index < unconverted.meshes[0].vertices.Size(); ++index)
        {
            rawLargest = std::max(rawLargest.Get(), std::fabs(unconverted.meshes[0].vertices[index].position[0].Get()));
        }
        Check(Near(rawLargest, 50.0f), "convertUnits off keeps the file's numbers");
    }
}

JBro::Int32 RunModelDecoderTests()
{
    JBRO_CASE(TestFormatsComeFromTheExtension());
    JBRO_CASE(TestAGltfWithDataUrisDecodes());
    JBRO_CASE(TestAGltfReadsItsSideFilesThroughTheReader());
    JBRO_CASE(TestAGlbDecodes());
    JBRO_CASE(TestAnObjWithItsMaterialFileDecodes());
    JBRO_CASE(TestCalculatedNormalsFollowTheSmoothingAngle());
    JBRO_CASE(TestAnFbxDecodesWithItsUnits());
    std::cout << "Model decoder tests passed.\n";
    return 0;
}
