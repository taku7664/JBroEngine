#pragma once

#include <JBro/ScreenUi/UiHost.h>

namespace JBro::System
{
    // 화면 UI 가 3D 스크립트의 훅을 부르는 자리다(D-363). 3D 스크립트는 모두 `GameScript3D` 다 - 등록(`RegisterScriptType3D`)이 컴파일할 때 검사한다.
    class UiHookDispatcher3D final : public IUiHookDispatcher
    {
    public:
        void Call(GameScriptBase& script, UiHook hook) override;
    };
}
