#pragma once

#include <JBro/Types/String.h>

namespace JBro
{
    struct ProjectFile;

    // 게임 호스트의 실행 인자다(D-115). 에디터의 규약(D-97)과 같은 모양이고 키는 둘이다.
    //
    //   --project <경로>   열 `.jproject`. 없으면 빈 프로젝트로 뜬다(지금까지의 동작).
    //   --canvas <경로>    처음 읽을 캔버스. 없으면 프로젝트 파일의 `Build.StartupCanvas` 다.
    //
    // 경로는 UTF-8 이다. 모르는 키나 값 없는 키는 오류다 - 조용히 무시하면 오타가 빈 프로젝트로 뜬다.
    struct GameHostArguments
    {
        String projectFile;
        String canvasFile;
        String error;

        bool IsValid() const
        {
            return error.empty();
        }
    };

    GameHostArguments ParseGameHostArguments(int argumentCount, const char* const* arguments);

    // 처음 읽을 캔버스의 경로다. `--canvas` 가 있으면 그것(상대경로는 프로젝트 폴더 기준), 없으면 `Build.StartupCanvas`.
    // 둘 다 없으면 빈 문자열이다.
    String ResolveStartupCanvasPath(const GameHostArguments& arguments, const ProjectFile& project, const char* projectFilePath);
}
