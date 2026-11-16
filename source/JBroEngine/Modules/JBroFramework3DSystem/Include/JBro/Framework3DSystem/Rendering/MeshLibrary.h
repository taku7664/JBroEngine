#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    class Renderer;

    // `AssetId → AssetHandle` 표다. 렌더러에 올린 메시를 아이디로 판다(framework3d-plan §2.3).
    // `[가정]` `AssetSystem` 이 실제로 파일에서 메시를 로드하게 되면 이 표는 그쪽으로 옮긴다 -
    // 그때까지는 빌트인 도형만 있다.
    class MeshLibrary
    {
    public:
        // 빌트인 정육면체의 아이디. `MeshRenderer3D::meshId` 에 넣으면 그 도형이 그려진다.
        static AssetId BuiltinCubeId();

        // **빌트인 도형 표**(D-335 우선순위 2). 정육면체·구·캡슐·원기둥·평면이고, 아이디는 `Uuid::FromName("builtin/<이름>")` 이다.
        // 레지스트리에 없는 아이디라 에디터의 메시 칸·만들기 메뉴가 이 표에서 이름을 얻는다. 이름은 번역하지 않는다.
        struct BuiltinMesh
        {
            const char* name = nullptr;
            AssetId id;
        };
        static UInt32 GetBuiltinCount();
        // 범위 밖이면 이름이 널이다.
        static BuiltinMesh GetBuiltin(UInt32 index);

        // 빌트인 도형을 렌더러에 올린다. 렌더러가 없으면(테스트) 표만 비어 있고 참이다.
        Bool Initialize(Renderer* renderer);
        void Shutdown();

        // 아이디로 핸들을 찾는다. 모르면 빈 핸들이다.
        AssetHandle Resolve(AssetId id) const;
        // 표에 더한다. 같은 아이디가 있으면 덮는다.
        void Register(AssetId id, AssetHandle handle);
        std::size_t GetCount() const;

    private:
        struct Entry
        {
            AssetId id;
            AssetHandle handle;
        };
        Array<Entry> m_entries;
        Renderer* m_renderer = nullptr;
    };
}
