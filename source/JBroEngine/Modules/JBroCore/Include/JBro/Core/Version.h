#pragma once

// 엔진 버전이다(D-101·D-160). 원본은 `JBro.Common.props` 의 세 줄이고, 빌드가 `JBRO_VERSION_*` 로 넘긴다.
// 여기서는 글자로 바꾸기만 한다 - 값을 코드에 옮겨 적으면 올릴 때 두 곳이 갈린다.
#if !defined(JBRO_VERSION_MAJOR) || !defined(JBRO_VERSION_MINOR) || !defined(JBRO_VERSION_PATCH)
#error "JBRO_VERSION_* must come from JBro.Common.props"
#endif

#define JBRO_VERSION_TEXT_INNER(value) #value
#define JBRO_VERSION_TEXT(value) JBRO_VERSION_TEXT_INNER(value)

namespace JBro
{
    // "0.1.0" 모양이다. 프로젝트 파일의 `EngineVersion` 과 같은 글자다.
    inline constexpr const char EngineVersionText[] =
        JBRO_VERSION_TEXT(JBRO_VERSION_MAJOR) "." JBRO_VERSION_TEXT(JBRO_VERSION_MINOR) "."
        JBRO_VERSION_TEXT(JBRO_VERSION_PATCH);
}
