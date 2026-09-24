#pragma once

#include <JBro/Editor/EditorPopup.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Types/String.h>

namespace JBro
{
    // **되돌릴 수 없는 일 앞에서 묻는 모달**이다(D-174). 단추는 최대 셋이고, 고른 번호가
    // 콜백으로 온다. 창을 닫거나 Esc 를 누르면 `Cancelled` 다 - 묻는 자리에서 아무것도
    // 고르지 않은 것은 "그만두기" 로 친다.
    //
    // `MessagePopup` 은 확인 단추 하나뿐이라 고를 것이 있는 자리에는 쓸 수 없다.
    // 제목·글·단추 이름은 **이미 번역된 글자**를 받는다(§11.2) - 어디에 쓰이는지는 부르는 쪽이 안다.
    class ConfirmPopup final : public EditorPopup
    {
    public:
        static constexpr int Cancelled = -1;

        using Answer = void (*)(EditorApplication& editor, int choice, void* user);

        // `second`·`third` 는 널이면 그리지 않는다. `id` 가 널이면 제목이 곧 아이디다.
        ConfirmPopup(const char* title, const char* message,
            const char* first, const char* second, const char* third,
            Answer answer, void* user, const char* id = nullptr);


        const char* GetTitle() const override;
        const char* GetId() const override;
        void OnDraw(EditorApplication& editor) override;
        void OnExit(EditorApplication& editor) override;

    private:
        void Choose(EditorApplication& editor, int choice);

        String m_title;
        String m_message;
        String m_labels[3];
        // **단추의 차례가 곧 무게다**(D-190): 첫째가 하기, 둘째가 저장하지 않고 버리기,
        // 셋째가 그만두기다. 이 차례를 따르지 않는 창은 이 클래스를 그대로 쓰지 않는다 -
        // 버리는 쪽이 아닌 단추가 붉게 서면 색이 거짓말을 한다.
        static constexpr Widget::Severity Weights[3] = {
            Widget::Severity::Success, Widget::Severity::Error, Widget::Severity::Info};
        String m_id;
        Answer m_answer = nullptr;
        void* m_user = nullptr;
        bool m_answered = false;
    };
}
