#pragma once

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/UInt.h>

#include <type_traits>

namespace JBro::Handle
{
    // 스크립트가 보는 캔버스 레이어다(D-301, D-302 (8) - 옛 이름 `LayerHandle`). 레이어 번호 하나(4B)이고, 번호는 재사용하지 않는
    // 영속 번호라 이것으로 충분하다. 레이어 자체(`JBro::Layer`)는 캔버스 모듈에 있어 스크립트가 보지 못한다 - 멤버는 오브젝트 서비스의
    // 시스템을 거쳐 캔버스에 닿는다. 무효한 핸들의 멤버는 경고를 남기고 빈 값을 준다.
    class Layer final
    {
    public:
        Layer() = default;

        Bool IsValid() const;
        explicit operator bool() const;

        const char* GetName() const;
        // 이 레이어의 뿌리 오브젝트(부모 없음) 가운데 이름이 같은 첫 것이다. **한 단계만 본다** - `ui["HUD"]["HpBar"]` 처럼 잇는다.
        // 못 찾으면 경고 없이 빈 핸들이다. 문자열은 한 번 해시해 정수로 견준다. 매 프레임 부르지 않는다 - `OnStart` 에서 찾아 든다.
        GameObject operator[](const char* name) const;
        GameObject operator[](NameId name) const;

        UInt32 GetId() const;
        Bool operator==(const Layer& other) const;

    private:
        friend class GameObject;
        friend struct LayerAccess;

        static void ReportInvalidAccess(const char* operation, UInt32 id);

        UInt32 m_id = 0xFFFFFFFFu;
    };

    // 호스트 코드(오브젝트 시스템·시험)가 번호로 핸들을 짓는 창구다. 스크립트 API 가 아니다.
    struct LayerAccess
    {
        static Layer FromId(UInt32 id)
        {
            Layer layer;
            layer.m_id = id;
            return layer;
        }
    };

    static_assert(sizeof(Layer) == 4, "Handle::Layer is a 4-byte script value");
    static_assert(std::is_standard_layout_v<Layer>, "Handle::Layer must stay standard layout");
    static_assert(std::is_trivially_copyable_v<Layer>, "Handle::Layer must stay trivially copyable");
}
