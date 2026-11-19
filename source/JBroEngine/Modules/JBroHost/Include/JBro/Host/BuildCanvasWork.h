#pragma once

#include <JBro/Types/UInt.h>

namespace JBro
{
    class Canvas;
    class IPlatform;
    struct ProjectFile;

    // 프로젝트의 빌드 캔버스(목록이 비었으면 시작 캔버스)를 따로 세운 캔버스에 하나씩 읽어 `count` 의 값을 더한다(D-419).
    // 물리 스레드의 추천 값이 2D·3D 모두 이 길로 일감을 센다 - 읽는 길은 차원과 무관하고 세는 것만 다르다(D-223 의 2D 코드에서 뗐다).
    // 읽지 못한 캔버스는 건너뛰고 로그에 남긴다. 캔버스를 읽으려면 컴포넌트 타입이 등록돼 있어야 하므로 프레임워크를 연 뒤에 부른다.
    UInt32 SumBuildCanvasWork(IPlatform& platform, const ProjectFile& project, const char* projectFilePath, UInt32 (*count)(Canvas& canvas));
}
