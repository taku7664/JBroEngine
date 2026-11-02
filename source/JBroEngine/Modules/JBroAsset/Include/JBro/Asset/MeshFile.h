#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>

#include <cstddef>

namespace JBro
{
    // 정점 하나의 실수 수다: 위치 3, 법선 3, UV 2. 렌더러의 `MeshVertex` 와 같은 차례다 - 에셋 모듈은 렌더러를 모르므로 실수로 든다(D-365).
    inline constexpr UInt32 MeshVertexFloats = 8;

    // 로드된 메시(`.gltf`·`.glb`, D-365)다. 파일의 메시를 노드 변환까지 구워 한 덩어리로 합쳤다. 렌더러에 올리는 것은 `MeshLibrary` 다.
    struct MeshAssetData
    {
        Array<Float> vertices;
        Array<UInt32> indices;
        UInt32 dataGeneration = 1;
    };

    // glTF 의 바깥 버퍼 파일(`uri`)을 읽는다. 없으면 거짓이다.
    using MeshBufferReader = Bool (*)(const char* uri, Array<std::byte>& out, void* user);

    // **glTF 2.0 을 읽는다**(`.gltf`·`.glb`, D-365). 기본 장면(없으면 모든 메시)의 삼각형 프리미티브를 노드 변환까지 구워 합친다. 법선이 없으면
    // 면에서 만들고 UV 가 없으면 0 이다. 감김은 엔진의 앞면(glTF 와 반대)으로 뒤집고, 음의 크기를 가진 노드는 한 번 더 뒤집는다. 임베드(`data:`)·GLB
    // 바이너리·바깥 파일을 읽는다. 재질·스킨·애니메이션은 읽지 않는다. 깨졌으면 거짓이고 까닭은 `error` 다.
    Bool ParseGltfMesh(const std::byte* bytes, std::size_t size, MeshBufferReader reader, void* user, MeshAssetData& out, String* error = nullptr);

    // 게임 패키지의 `CookedMesh` 블롭이다(`JMSH` 와 다른 머리 `JMES`, 판 1). 빌드가 glTF 를 풀어 싣고, 게임은 glTF 를 읽지 않는다.
    void WriteCookedMesh(const MeshAssetData& mesh, Array<std::byte>& out);
    Bool ReadCookedMesh(const std::byte* bytes, std::size_t size, MeshAssetData& out);
}
