#pragma once

#include <JBro/Editor/EditorPopup.h>
#include <JBro/Types/String.h>

namespace JBro
{
    // 제목과 글 한 덩이, 확인 단추 하나짜리 모달이다. 저장 실패처럼 사용자가 알아야 하는
    // 결과를 로그에만 남기지 않기 위한 것이다(기존 엔진 `EditorMessagePopup`).
    // 제목은 이미 번역된 글자를 받는다 - 어디에 쓰이는지는 부르는 쪽이 안다(§11.2).
    class MessagePopup final : public EditorPopup
    {
    public:
        MessagePopup(const char* title, const char* message, const char* id = nullptr);

        const char* GetTitle() const override;
        const char* GetId() const override;
        void OnDraw(EditorApplication& editor) override;

    private:
        String m_title;
        String m_message;
        String m_id;
    };
}
