#pragma once

#include <JBro/Core/Core.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/NameTable.h>
#include <JBro/Types/UInt.h>

namespace JBro::System
{
    // 스크립트의 오브젝트 만들기·찾기가 캔버스에 닿는 길이다(D-301 (7)). 캔버스를 가진 프레임워크가 구현을 들고,
    // 스크립트 컨텍스트를 묶을 때 공통 시스템 컨텍스트(`SystemContext::Objects`)에 꽂는다. 가상 함수 표는 스크립트 DLL 과의
    // ABI 다 - 바꾸면 `SystemContextAbiVersion` 을 올린다.
    //
    // 레이어는 캔버스의 영속 번호(`LayerId`, 재사용하지 않는다)를 `UInt32` 로 주고받는다 - `Layer` 는 캔버스 모듈의 타입이라 여기서 보지 못한다.
    // **파괴를 요청한 오브젝트는 찾기에서 빠진다**(D-301 (6)). 메인 스레드 전용이다.
    class IObjectSystem
    {
    public:
        virtual ~IObjectSystem() = default;

        // 오브젝트를 만들고 차원의 기본 트랜스폼을 붙인다. `parent` 가 유효하면 그 자식이고, `layer` 가 없으면 부모의 레이어, 부모도 없으면 기본 레이어다.
        virtual Handle::GameObject Create(const char* name, const Handle::GameObject& parent, UInt32 layer) = 0;
        // 이름(`byTag` 면 태그)이 같은 첫 오브젝트. 차례는 레이어 차례 → 계층 패널의 차례(깊이 우선)다.
        virtual Handle::GameObject Find(NameId name, Bool byTag) = 0;
        // 같은 차례로 전부. `results` 를 비우고 채운다.
        virtual void FindAll(NameId name, Bool byTag, Array<Handle::GameObject>& results) = 0;
        // 레이어 `layer` 의 뿌리 오브젝트 가운데 이름이 같은 첫 것. 한 단계만 본다.
        virtual Handle::GameObject FindRoot(UInt32 layer, NameId name) = 0;
        // 이름이 같은 레이어의 번호. 없으면 `InvalidLayer` 다.
        virtual UInt32 FindLayer(NameId name) = 0;
        virtual Bool IsLayerAlive(UInt32 layer) = 0;
        // 없는 레이어면 빈 글자다.
        virtual const char* GetLayerName(UInt32 layer) = 0;
        // 빌트인 컴포넌트를 타입으로 붙인다. 못 붙이면(모르는 타입·무효 오브젝트) 빈 참조다.
        virtual InstanceRef AddComponent(const Handle::GameObject& object, ComponentTypeId type) = 0;
        // 스크립트를 타입 이름으로 붙인다. 등록되지 않은 타입이면 빈 참조다.
        virtual InstanceRef AddScript(const Handle::GameObject& object, const char* typeName) = 0;

        static constexpr UInt32 InvalidLayer = 0xFFFFFFFFu;
    };
}
