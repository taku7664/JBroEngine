#pragma once

#include <JBro/Editor/Widget/Common.h>

#include <JBro/Types/String.h>

namespace JBro::Widget
{
    // 글자 칸이다. `String` 과 ImGui 의 `char` 버퍼 사이를 옮겨 담는다.
    //
    // **편집 중에는 원본이 버퍼를 덮지 않는다.** 매 프레임 최신 값을 넣어도
    // 타자가 지워지지 않아야 하기 때문이다 - 기존 엔진 `ImInputText::SetSourceText`
    // 가 같은 이유로 있었다. 값을 언제 반영할지는 `commitOnEnter` 가 정한다.
    class TextField
    {
    public:
        TextField(const char* id, String& text);

        TextField& Hint(const char* text);
        TextField& MaxLength(std::size_t length);
        TextField& Multiline(bool multiline = true, float lines = 4.5f);
        // 참이면 Enter 를 눌러야 값이 반영된다. 거짓이면 글자마다 반영한다.
        TextField& CommitOnEnter(bool commit = true);
        // 참이면 **편집이 끝날 때 한 번** 참을 돌려준다(포커스를 잃거나 Enter). 글자는 치는
        // 대로 들어가되 부르는 쪽이 값을 확정하는 시점만 미뤄진다.
        //
        // 값을 커맨드로 남기는 자리가 이것을 쓴다. 글자마다 커맨드를 만들면 되돌리기가
        // 글자 수만큼 필요해지고, 커맨드 병합은 마우스를 누른 채일 때만 일어나므로
        // 타이핑에는 걸리지 않는다.
        TextField& CommitOnFinish(bool commit = true);
        TextField& Invalid(bool invalid = true);
        TextField& Width(float width);

        bool Draw() const;
        bool operator()() const;

    private:
        const char* m_id = nullptr;
        String& m_text;
        const char* m_hint = nullptr;
        std::size_t m_maxLength = 0;
        float m_lines = 4.5f;
        float m_width = 0.0f;
        bool m_multiline = false;
        bool m_commitOnEnter = false;
        bool m_commitOnFinish = false;
        bool m_invalid = false;
    };
}
