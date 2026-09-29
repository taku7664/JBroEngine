#pragma once

#include <JBro/Editor/EditorPopup.h>
#include <JBro/Editor/ScriptProject.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

namespace JBro
{
    // **새 스크립트의 이름과 필드를 받는다**(cpp-script-plan §3.3, D-266, 기존 `CAssetBrowserTool::ShowNewScriptPopup`).
    // 에셋 브라우저의 스크립트 폴더에서 우클릭하면 뜬다. 필드는 `고급 옵션` 을 펼쳐야 보인다 - 이름만 넣고 만드는 일이 대부분이다.
    //
    // 기존과 다른 것: 이름이 겹치면 **번호를 붙여 피하지 않고 막는다**. 이 이름이 캔버스 파일에 저장되는 타입 이름이라,
    // 조용히 `Player2` 가 되면 사용자가 모르는 이름으로 저장된다. 만들지 못하면 닫지 않고 까닭을 그 자리에 보인다.
    class NewScriptPopup final : public EditorPopup
    {
    public:
        // `folder` 는 스크립트 자리(`Scripts`) 기준 상대경로다. 빈 글자면 그 뿌리다.
        explicit NewScriptPopup(const char* folder);

        const char* GetTitle() const override;
        const char* GetId() const override;
        float GetInitialWidth() const override;
        void OnDraw(EditorApplication& editor) override;

    private:
        String m_folder;
        String m_name = "NewScript";
        Array<ScriptProject::FieldSpec> m_fields;
        String m_error;
    };
}
