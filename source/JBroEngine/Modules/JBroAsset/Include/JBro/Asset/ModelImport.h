#pragma once

#include <JBro/Asset/AssetMetaFile.h>
#include <JBro/Asset/MaterialFile.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/String.h>

#include <string_view>

namespace JBro
{
    class IPlatform;
    class AssetRegistry;
    struct AssetRecord;
    struct ModelData;
    struct ModelMesh;

    // 모델에서 나오는 하위 에셋 하나다. `index` 는 그 종류의 `ModelData` 배열(`meshes`·`materials`·`images`) 번호다.
    struct ModelSubAsset
    {
        String key;
        AssetType type = AssetType::Unknown;
        UInt32 index = 0;
    };

    // **키를 짓는 곳은 여기 하나다** - 목록을 맞추는 쪽(`RefreshModelSubAssets`)과 하위 에셋을 읽는 쪽(`AssetSystem`)이 같은 이름으로 같은 것을 찾는다.
    // 차례는 메시·재질·내장 그림이고, 겹치는 키는 뒤의 것에 ` (2)` 를 붙인다. 바깥 그림 파일은 하위 에셋이 아니다(그 파일이 이미 Texture 에셋이다).
    void ListModelSubAssets(const ModelData& model, Array<ModelSubAsset>& result);
    // 그 목록에서 하위 레코드의 종류와 이름(`AssetRecord::name`)이 가리키는 것을 찾는다. 없으면 nullptr(메타의 목록이 파일보다 낡음).
    const ModelSubAsset* FindModelSubAsset(const Array<ModelSubAsset>& listed, AssetType type, std::string_view name);

    // 모델 내장 그림을 처음 세울 때의 옵션이다. 일반 그림과 달리 밉맵을 켠다(D-339) - 3D 표면은 멀어지면 밉이 없을 때 반짝인다.
    TextureImportOptions ModelTextureDefaults();

    // **모델의 하위 에셋 아이디를 레지스트리에서 찾는다**(material-plan 2 단계). `ListModelSubAssets` 의 그 종류·번호 항목의 이름과 같은 하위 레코드다. 없으면 빈 아이디.
    AssetId FindModelSubAssetId(const AssetRegistry& registry, const AssetRecord& model, const Array<ModelSubAsset>& listed,
        AssetType type, std::size_t index);
    // 메시가 파일에서 쓰는 재질의 하위 아이디다(D-346 (3) - `materialId` 가 비면 이것이다). 재질이 없거나 `importMaterials` 가 꺼져 있으면 빈 아이디.
    AssetId FindModelMeshMaterialId(const AssetRegistry& registry, const AssetRecord& model, const Array<ModelSubAsset>& listed, const ModelMesh& mesh);
    // 모델의 재질 하나를 재질 값으로 옮긴다. 그림 칸은 내장 그림이면 그 하위 Texture, 바깥 그림이면 모델 폴더 기준 경로의 Texture 에셋이다
    // (레지스트리에 없으면 그 칸은 비고 `missingTextures` 가 오른다). 셰이더는 `Standard` 다.
    MaterialDesc MakeModelMaterialDesc(const AssetRegistry& registry, const AssetRecord& model, const ModelData& data,
        const Array<ModelSubAsset>& listed, std::size_t materialIndex, UInt32& missingTextures);

    // **같은 그림인지 가르는 열쇠**다(model-import-plan §2.5). 원본 바이트의 해시와 실제로 적용되는 옵션(필터·색 공간·밉맵)을 섞는다 -
    // 둘 다 같아야 같은 그림으로 묶고, 한쪽 옵션을 바꾸면 열쇠가 달라져 저절로 갈라진다. 해시가 0 이면 0(묶지 않음)이다.
    UInt64 MakeSharedImageKey(UInt64 contentHash, const TextureImportOptions& options);

    // **에셋 폴더의 모델 파일 하나를 읽는다.** 곁 파일(`.gltf` 의 `.bin`, OBJ 의 `.mtl`)은 모델 폴더 기준으로 플랫폼이 연다(D-112).
    // 목록을 맞추는 쪽(`RefreshModelSubAssets`)과 패키지를 굽는 쪽(`Package::CookAssets`)이 같은 길로 읽는다. 실패하면 `error` 에 영어 한 줄.
    Bool DecodeModelFile(IPlatform& platform, const char* assetRoot, std::string_view relativePath, const ModelImportOptions& options,
        ModelData& model, String& error);

    // **모델의 하위 에셋 목록을 파일에 맞춘다**(D-338, model-import-plan 3·5 단계). 모델을 읽어(`DecodeModel`) `ListModelSubAssets` 의 키를 세우고
    // 메타의 목록과 맞춘다 - **같은 키는 아이디를 지키고**(캔버스의 참조가 끊기지 않는다), 새 키는 새 아이디, 사라진 키는 뺀다.
    // 내장 그림은 해시를 늘 새로 적고, 옵션은 있던 것을 지키며 새 그림은 `ModelTextureDefaults` 로 적는다.
    //
    // 메타의 옵션(`modelOptions`, 없으면 기본값)으로 읽는다. 목록이 달라졌으면 `changed` 가 참이고 `meta.hasSubAssets` 는 늘 참이 된다.
    // 파일은 플랫폼이 연다(D-112) - `.gltf` 의 바깥 버퍼와 OBJ 의 `.mtl` 은 모델 폴더 기준으로 읽는다. 에디터의 임포트 경로이고 게임은 부르지 않는다.
    // 모델을 읽지 못하면 false 이고 메타는 그대로다.
    Bool RefreshModelSubAssets(IPlatform& platform, const char* assetRoot, std::string_view relativePath,
        AssetMetaFile& meta, Bool& changed, String& error);
}
