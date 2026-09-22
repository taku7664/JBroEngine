#pragma once

#include <JBro/Editor/Widget/Common.h>

#include <JBro/Types/String.h>

namespace JBro::Widget
{
    // **경로 칸과 "찾아보기" 단추다**(D-164, 기존 `ImPathField` + `DrawReadOnlyPathWithFolderBrowse`).
    //
    // 글자로도 고칠 수 있고 단추로 고를 수도 있다. 단추는 **알리기만 한다** - 대화상자는 막히는 호출이라 프레임 안에서
    // 열 수 없다(D-93). 부르는 쪽이 `EditorApplication::RequestBrowsePath` 로 넘기면 프레임이 끝난 뒤 열리고,
    // 고른 경로가 돌아온다. 칸의 폭은 단추 자리를 뺀 나머지다 - 기존의 `ReserveTrailingWidth` 가 하던 일이다.
    struct PathFieldResult
    {
        // 글자를 고쳤다.
        bool edited = false;
        // "찾아보기" 를 눌렀다.
        bool browse = false;
    };

    class PathField
    {
    public:
        PathField(const char* id, String& path);

        PathField& Hint(const char* text);
        PathField& Invalid(bool invalid = true);

        PathFieldResult Draw() const;

    private:
        const char* m_id = nullptr;
        String& m_path;
        const char* m_hint = nullptr;
        bool m_invalid = false;
    };
}
