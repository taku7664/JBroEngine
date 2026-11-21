#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Math3D.h>

#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class AssetSystem;
    class Canvas;
    namespace Object
    {
        class GameObject;
    }
    namespace Component
    {
        class Collider3D;
        class MeshRenderer3D;
    }

    // 메시 콜라이더가 쓸 메시다(D-418). 물리 어댑터와 캔버스 뷰의 콜라이더 그리기가 같은 길로 찾는다.
    struct ColliderMeshSource
    {
        AssetId id;
        AssetHandle handle;
        // 모델의 메시 자료가 제자리에서 다시 실리면 오른다. 빌트인은 1 이다. 0 이면 아직 쓸 자료가 없다.
        UInt32 dataGeneration = 0;
    };

    // 콜라이더의 `meshId` 가 비었으면 같은 오브젝트의 `MeshRenderer3D` 가 그리는 메시다. 어느 쪽도 없으면 빈 아이디다.
    // `assets` 가 있으면 모델 메시의 자료 세대를 채운다.
    ColliderMeshSource FindColliderMeshSource(Canvas& canvas, Object::GameObject& object, const Component::Collider3D& collider,
        const AssetSystem* assets);

    // 메시 렌더러가 그리는 메시다. `meshId` 가 비었으면 렌더러처럼 빌트인 정육면체다. 캔버스 뷰의 3D 고르기가 쓴다(D-441).
    ColliderMeshSource FindRendererMeshSource(const Component::MeshRenderer3D& renderer, const AssetSystem* assets);

    // 정점 위치(메시 로컬)와 삼각형 인덱스를 채운다. **삼각형은 바깥에서 볼 때 반시계로 감아 돌려준다** - 엔진의 메시는 렌더러의 앞면(시계)으로
    // 감겨 있지만 물리(Jolt)는 반시계 쪽을 앞면으로 막는다. 그대로 넘기면 바닥 윗면이 뒷면이 되어 물체가 뚫고 떨어진다(D-418 에서 겪었다). 빌트인 도형은 메시 라이브러리가 짓고, 모델의 메시는 에셋 시스템의 CPU 자료를 읽는다.
    // 아직 실리지 않았거나 모르는 아이디면 둘을 비우고 거짓이다. 프레임마다 부르지 않는다 - 부르는 쪽이 세대로 사본을 지킨다.
    Bool GatherColliderMesh(const AssetSystem* assets, const ColliderMeshSource& source, Array<Vector3>& points, Array<UInt32>& indices);
}
