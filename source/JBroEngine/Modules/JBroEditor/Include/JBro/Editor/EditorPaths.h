#pragma once

#include <JBro/Types/String.h>

namespace JBro::EditorPaths
{
    // **경로 조각을 다루는 한 벌**이다(D-173, 기존 `Path/EditorPathUtils`).
    //
    // 같은 세 줄이 `EditorApplication` 과 에셋 브라우저에 따로 있었고, 인스펙터가 셋째 벌을
    // 쓸 뻔했다. 구분자는 `/` 와 `\` 를 둘 다 받는다 - 프로젝트 파일에는 `/` 로 적지만
    // 사용자가 고른 경로는 윈도우 표기로 온다.

    // 마지막 구분자 앞까지. 구분자가 없으면 빈 글자다.
    String FolderOf(const char* path);

    // 마지막 구분자 뒤. **입력 안을 가리킨다** - 복사하지 않으므로 그 글자가 사는 동안만 쓴다.
    // 널이면 널이다.
    const char* LeafOfPath(const char* path);
    inline const char* LeafOfPath(const String& path)
    {
        return LeafOfPath(path.c_str());
    }

    // `root` 와 `relative` 를 `/` 하나로 잇는다. 한쪽이 비어 있으면 나머지 그대로다.
    String JoinPath(const char* root, const char* relative);
}
