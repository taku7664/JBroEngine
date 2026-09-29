#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
    class IPlatform;

    // 에디터가 스크립트 프로젝트를 빌드하는 데 쓰는 조각들이다(cpp-script-plan §3.4, D-267).
    // 빌드 자체는 자식 프로세스(`IPlatform::StartProcess`)이고 에디터가 프레임마다 끝을 본다. 여기는 무엇을 띄울지와
    // 끝난 뒤 로그에서 무엇을 읽을지만 안다 - 상태는 `EditorApplication` 이 든다.
    namespace ScriptBuild
    {
        // 빌드 로그 한 줄에서 읽은 오류나 경고다.
        struct Diagnostic
        {
            // 절대경로다. 링커·MSBuild 자신의 것처럼 파일이 없는 진단은 빈 글자다.
            String file;
            std::uint32_t line = 0;
            std::uint32_t column = 0;
            bool isError = true;
            // `C2065`·`LNK2019`·`MSB3073` 따위다. 없으면 빈 글자다.
            String code;
            String message;
        };

        // 오류 줄을 무엇으로 열지다(D-267, 사용자 결정 2026-09-29: "편집기를 설정에서 고르게"). 에디터 설정에 저장된다.
        enum class EditorKind : std::uint8_t
        {
            // 셸의 기본 앱. 줄 번호를 넘길 길이 없어 파일만 연다.
            System,
            VisualStudio,
            VisualStudioCode,
            Count
        };

        const char* EditorKindName(EditorKind kind);
        bool ParseEditorKind(const char* text, EditorKind& out);

        // MSBuild 파일 로거의 한 줄을 읽는다(`경로(줄,칸): error C2065: 글 [프로젝트]`). 진단이 아니면 거짓이다.
        bool ParseDiagnosticLine(const char* line, std::size_t length, Diagnostic& out);
        // 로그 전체다. 같은 진단이 두 번 적혀도(병렬 빌드의 요약 따위) 한 번만 담는다.
        void ParseLog(const char* text, std::size_t length, Array<Diagnostic>& out);

        // MSBuild 를 찾는다. 개발자 명령 프롬프트의 `VSINSTALLDIR` 먼저, 그다음 `vswhere`(기존 엔진 `LocateMSBuild` 와 같은 차례).
        // 못 찾으면 빈 글자다. `vswhere` 는 짧게 띄워 끝을 기다린다 - 한 번 찾은 값은 부르는 쪽이 들고 있는다.
        String LocateMSBuild(IPlatform& platform, const char* scratchFolder);
        // Visual Studio 의 `devenv.exe` 다(`vswhere -property productPath`).
        String LocateVisualStudio(IPlatform& platform, const char* scratchFolder);
        // VS Code 의 `code.cmd` 다. 사용자 설치 자리, 그다음 시스템 설치 자리를 본다.
        String LocateVisualStudioCode(IPlatform& platform);

        // `"<msbuild>" "<vcxproj>" ...`. 로그는 `logFile` 에 UTF-8 로 남는다 - 콘솔 출력은 시스템 코드 페이지라 한글 경로가 깨진다.
        String MakeBuildCommand(const char* msbuild, const char* projectFile, const char* configuration, const char* logFile);
        // 그 편집기로 파일의 그 줄을 여는 명령줄이다. `System` 이거나 편집기 경로가 비면 빈 글자다(부르는 쪽이 셸로 연다).
        String MakeOpenAtLineCommand(EditorKind kind, const char* editorPath, const char* file, std::uint32_t line);
    }
}
