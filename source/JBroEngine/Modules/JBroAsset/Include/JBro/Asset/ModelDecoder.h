#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Core/Core.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>

#include <cstddef>
#include <string_view>

namespace JBro
{
    // **모델 파일을 읽은 CPU 자료**다(D-338, model-import-plan §2.2). 디코더는 ufbx(FBX·OBJ)와 cgltf(glTF)이고 결과는 엔진 값 타입이다 -
    // 서드파티의 타입은 이 헤더 밖으로 나가지 않는다. 좌표는 엔진의 것이다(오른손, Y 위, 미터, 카메라는 -Z 를 본다). UV 는 D3D 규약(왼쪽 위가 0)이다.
    // 임포트 경로의 일이고 프레임 경로가 아니다.

    struct ModelVertex
    {
        Float position[3] = {0.0f, 0.0f, 0.0f};
        Float normal[3] = {0.0f, 0.0f, 1.0f};
        Float uv[2] = {0.0f, 0.0f};
        // 파일에 탄젠트가 있을 때만 채운다(`ModelMesh::hasTangents`). w 는 바이탄젠트의 방향(±1)이다. 계산은 남은 일이다(D-339).
        Float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    };

    // 메시 하나 = 재질 하나다. 파일의 메시가 재질 여럿으로 나뉘면(glTF 의 프리미티브, FBX 의 재질 부분) 그만큼의 `ModelMesh` 가 된다 -
    // 렌더러가 메시마다 재질 하나를 그리기 때문이다. 정점은 메시 자신의 좌표이고 노드의 변환은 굽지 않는다(노드는 `ModelNode` 에 따로).
    struct ModelMesh
    {
        // 하위 에셋의 이름이다. 파일의 메시 이름(없으면 노드 이름)이고, 나뉘었으면 뒤에 재질 이름이 붙는다. 한 파일 안에서 겹치지 않는다.
        String name;
        Array<ModelVertex> vertices;
        // 삼각형 목록이다. 정점은 합쳐져 있다(같은 위치·법선·UV 는 한 정점).
        Array<UInt32> indices;
        // `ModelData::materials` 의 번호다. 없으면 -1.
        Int32 material = -1;
        Bool hasUvs = false;
        Bool hasTangents = false;
    };

    // 재질의 그림 칸이다. 색 공간의 `Auto` 가 이것을 본다(D-339) - 기본색·발광은 sRGB, 나머지는 선형이다.
    enum class ModelTextureSlot : std::uint8_t
    {
        BaseColor,
        Normal,
        MetallicRoughness,
        Occlusion,
        Emissive
    };
    inline constexpr UInt32 ModelTextureSlotCount = 5;

    struct ModelMaterial
    {
        String name;
        Float baseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        Float metallic = 0.0f;
        Float roughness = 1.0f;
        // 칸마다 `ModelData::images` 의 번호다. 없으면 -1.
        Int32 images[ModelTextureSlotCount] = {-1, -1, -1, -1, -1};
    };

    // 재질이 쓰는 그림 하나다. 바깥 파일을 가리키거나(`path`) 모델 안에 들어 있다(`embedded`). 둘 중 하나다.
    struct ModelImage
    {
        String name;
        // 모델 파일의 폴더 기준 경로다. 구분자는 `/` 다. 내장이면 비어 있다.
        String path;
        // 내장 그림의 원래 바이트(PNG·JPEG 따위)다. 디코드는 이미지 디코더가 한다.
        Array<std::byte> embedded;
        // 내장 바이트의 64 비트 FNV-1a 다. 다른 모델에 든 같은 그림을 묶는 열쇠의 한 쪽이다(D-338 (4)). 바깥 그림이면 0.
        UInt64 contentHash = 0;
        // 처음 쓰인 칸이다. `colorSpace` 의 `Auto` 가 이것으로 정한다.
        ModelTextureSlot firstUse = ModelTextureSlot::BaseColor;
    };

    // 노드 하나다. 첫 판은 쓰지 않고 들고만 있다 - 모델을 노드 트리째 놓는 것(프리팹)이 서면 쓴다(model-import-plan §4).
    struct ModelNode
    {
        String name;
        // 부모 노드의 번호다. 뿌리면 -1.
        Int32 parent = -1;
        Vector3 position;
        Quaternion rotation;
        Vector3 scale{1.0f, 1.0f, 1.0f};
        // 이 노드가 그리는 메시들(`ModelData::meshes` 의 번호)이다.
        Array<UInt32> meshes;
    };

    struct ModelData
    {
        Array<ModelMesh> meshes;
        Array<ModelMaterial> materials;
        Array<ModelImage> images;
        Array<ModelNode> nodes;
    };

    enum class ModelFormat : std::uint8_t
    {
        Unknown,
        Gltf,
        Glb,
        Fbx,
        Obj
    };

    // 확장자로 형식을 고른다(대소문자 무관). 모르면 `Unknown`.
    ModelFormat ModelFormatFromPath(std::string_view path);

    // **곁 파일을 읽는 길**이다. 디코더는 파일을 열지 않는다(D-112) - `.gltf` 의 바깥 버퍼(`.bin`)와 `.obj` 의 재질 파일(`.mtl`)을 이것으로 받는다.
    // `relativePath` 는 모델 파일의 폴더 기준이고 구분자는 `/` 다. 못 읽으면 false.
    struct ModelFileReader
    {
        Bool (*read)(void* user, const char* relativePath, Array<std::byte>& bytes) = nullptr;
        void* user = nullptr;
    };

    // 모델 파일의 바이트를 읽는다. 실패하면 `result` 를 손대지 않고 false 이며 `error` 에 영어 한 줄을 적는다(로그용, 사용자 글은 에디터가 짓는다).
    Bool DecodeModel(JArrayView<std::byte> bytes, ModelFormat format, const ModelImportOptions& options,
        const ModelFileReader& reader, ModelData& result, String& error);

    // 64 비트 FNV-1a 다. 내장 그림의 `contentHash` 가 쓴다. 0 은 "없음" 이라 내지 않는다(0 이 나오면 1).
    UInt64 HashModelBytes(JArrayView<std::byte> bytes);
}
