#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Component
{
    class MeshRenderer3D;
}

namespace JBro::Handle
{
    // 스크립트가 보는 `MeshRenderer3D` 다(D-302). 데이터는 엔진의 `Component::MeshRenderer3D` 에 있고, 이것은 그 자리를 가리키는 24B 값이다.
    // 메시를 그린다. 에셋은 이번 실행의 자리(`AssetHandle`)로 바꾸고 저장되는 id 는 읽기만 한다.
    // 무효한 핸들의 멤버는 경고를 남기고 아무것도 하지 않으며, 값을 돌려주는 멤버는 그 타입의 기본값을 준다.
    class MeshRenderer3D final : public Internal::ComponentHandle
    {
    public:
        // 데이터 클래스와 같은 글자다 - 그래서 타입 id 가 같고 `GetComponent<MeshRenderer3D>()` 가 이것으로 찾는다.
        static constexpr const char* StaticTypeName()
        {
            return "Component::MeshRenderer3D";
        }

        AssetId GetMeshId() const;
        AssetHandle GetMesh() const;
        void SetMesh(AssetHandle value);
        AssetId GetMaterialId() const;
        AssetHandle GetMaterial() const;
        void SetMaterial(AssetHandle value);
        Color GetTint() const;
        void SetTint(Color value);
        Bool GetVisible() const;
        void SetVisible(Bool value);

    private:
        Component::MeshRenderer3D* Resolve(const char* operation) const;
    };

    static_assert(sizeof(MeshRenderer3D) == 24, "a component handle is a 24-byte script value");
    static_assert(std::is_standard_layout_v<MeshRenderer3D>, "a component handle must stay standard layout");
    static_assert(std::is_trivially_copyable_v<MeshRenderer3D>, "a component handle must stay trivially copyable");
}
