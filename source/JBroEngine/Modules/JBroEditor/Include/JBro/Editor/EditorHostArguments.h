#pragma once

#include <JBro/Editor/EditorControlPort.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/String.h>

namespace JBro
{
    class IPlatform;
    struct FileTypeAssociation;

    // 에디터 실행 파일(`JBroEditorHost`)의 인자다(D-97). 시험이 실행 파일을 띄우지 않고 해석을 볼 수 있게 모듈에 둔다(D-389).
    //
    //   [<경로>] / --project <경로>   열 `.jproject`. 탐색기에서 두 번 누르면 이름 없는 인자로 온다(D-389).
    //   --content-root <경로>          로컬라이징 표와 아이콘 글꼴을 찾을 기준 폴더
    //   --frames <수>                  그만큼 돌고 끝낸다
    //   --control-port <번호>          제어 포트(D-270). 0 이면 열지 않는다
    //   --register-file-type           `.jproject` 를 이 실행 파일과 잇고 끝낸다(D-389)
    //   --unregister-file-type         그 잇기를 지우고 끝낸다
    //
    // 경로는 UTF-8 이다. 모르는 옵션과 값 없는 옵션은 오류다.
    struct EditorHostArguments
    {
        String projectFile;
        String contentRoot;
        Int64 frameLimit = 0;
        Int64 controlPort = EditorControlPort::DefaultPort;
        Bool showHelp = false;
        Bool registerFileType = false;
        Bool unregisterFileType = false;
        String error;

        Bool IsValid() const
        {
            return error.empty();
        }
    };

    EditorHostArguments ParseEditorHostArguments(Int32 argumentCount, const char* const* arguments);

    // **`.jproject` 의 연결**(D-389). 두 번 누르면 `"<실행 파일>" "%1"` 이 돈다. 탐색기는 작업 폴더를 프로젝트 쪽으로 주지만
    // 에디터는 글자 표와 아이콘 글꼴을 실행 파일 옆에서 먼저 찾으므로(D-176) 기준 폴더를 적지 않는다. 실행 파일 경로를 모르면 거짓이다.
    Bool MakeProjectFileType(const IPlatform& platform, FileTypeAssociation& desc);
}
