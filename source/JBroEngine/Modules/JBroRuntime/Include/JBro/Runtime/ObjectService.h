#pragma once

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/LayerHandle.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/NameTable.h>

namespace JBro::Service
{
    // 스크립트가 오브젝트를 만들고 찾는 길이다(D-301). `GetServiceContext().Objects` 로 부른다.
    //
    //   GameObject enemy = GetServiceContext().Objects.Create("Enemy");
    //   enemy.AddComponent<SpriteRenderer2D>();
    //   Layer ui = GetServiceContext().Objects.GetLayer("UI");
    //   GameObject hpBar = ui["HUD"]["HpBar"];
    //
    // **`Find`·`[]` 는 이름만 본다. 태그는 `FindWithTag` 로만 찾는다.** 지우기는 `handle.Destroy()` 하나다 - 요청한 순간부터 모든 사본이
    // 무효이고 찾기에서 빠진다. 찾기는 문자열을 해시해 캔버스를 훑으므로 매 프레임 부르지 않는다 - `OnStart` 에서 찾아 필드에 든다.
    // 캔버스가 묶이지 않은 자리(에디터 밖 도구·시험)에서는 빈 핸들을 준다. 가상 함수가 없다 - 서비스 컨텍스트 안에 값으로 들어간다.
    class ObjectService
    {
    public:
        // 만든 오브젝트는 곧바로 있다(찾을 수 있고 붙일 수 있다). 차원의 기본 트랜스폼이 붙어 나온다.
        // 레이어를 주지 않으면 부모의 레이어, 부모도 없으면 캔버스의 기본 레이어다.
        Handle::GameObject Create(const char* name, Handle::GameObject parent = {}, Handle::Layer layer = {}) const;

        Handle::GameObject Find(const char* name) const;
        Handle::GameObject Find(NameId name) const;
        // `results` 를 비우고 채운다. 미리 `Reserve` 해 두면 할당하지 않는다.
        void FindAll(const char* name, Array<Handle::GameObject>& results) const;
        Handle::GameObject FindWithTag(const char* tag) const;
        void FindAllWithTag(const char* tag, Array<Handle::GameObject>& results) const;

        // 이름이 같은 레이어다. 없으면 빈 핸들이다.
        Handle::Layer GetLayer(const char* name) const;
    };
}
