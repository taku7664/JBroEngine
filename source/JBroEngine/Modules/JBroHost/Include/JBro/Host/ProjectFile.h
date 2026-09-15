#pragma once

#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <cstdint>

namespace JBro
{
    // 프로젝트 파일이다. 기존 엔진과 같은 `.jproject` 확장자와 같은 키 이름을 쓴다 —
    // 그 쪽 프로젝트를 그대로 열 수 있어야 하고, 이름을 새로 지을 이유도 없다.
    //
    // 형식은 YAML 이되 **읽는 것은 그 부분집합**이다. 아래가 전부다. **모르는 키와 그 아래
    // 블록은 조용히 건너뛴다** - 기존 엔진이 쓴 키를 다 알지 못해도 그 프로젝트를 열어야
    // 하고, 새 엔진이 나중에 키를 더해도 옛 엔진이 열 수 있어야 한다. 실패하는 것은
    // **형식이 틀린 것**이다: 탭 들여쓰기, 두 칸이 아닌 들여쓰기, 키 없는 시퀀스 항목,
    // 닫히지 않은 따옴표, `key: value` 가 아닌 줄, 아는 키에 타입이 맞지 않는 값. 그때는
    // 추측하지 않고 줄 번호와 함께 실패한다. 부분집합은 다음과 같다.
    //
    //   Key: value            최상위 스칼라
    //   Key:                  중첩 맵(들여쓰기 2칸) — `Build:` 하나만 안다
    //     Inner: value
    //   Key:                  문자열 시퀀스
    //     - item
    //   Key: []               빈 시퀀스
    //   # 주석                 줄 전체 주석
    //
    // 앵커, 플로우 맵, 여러 줄 스칼라는 읽지 않는다. 아는 키의 값으로 오면 타입이 맞지
    // 않아 거절되고, 모르는 키의 값으로 오면 그 키와 함께 건너뛴다.
    struct ProjectBuildSettings
    {
        String        productName;
        bool          enableWindows = true;
        bool          enableWeb = false;
        bool          enableAndroid = false;
        bool          enableIOS = false;
        String        outputDirectory = "Dist/Games";
        String        startupCanvas;
        Array<String> buildCanvases;
        // 익스포트한 게임 옆에 놓이는 스크립트 DLL 이름이다.
        String        scriptOutputLibraryPath = "GameScript.dll";
    };

    struct ProjectFile
    {
        std::uint32_t version = 1;
        String        rootPath = ".";
        std::uint32_t resolutionWidth = 1920;
        std::uint32_t resolutionHeight = 1080;
        float         pixelsPerUnit = 100.0f;
        bool          debugModeEnabled = false;
        // 에디터가 스크립트를 빌드해 내놓는 자리다. 프로젝트 루트 기준 상대경로다.
        String        scriptSourceDirectory = "Contents";
        String        scriptOutputLibraryPath = "x64/Debug/GameScript.dll";
        String        lastOpenedCanvasPath;
        ProjectBuildSettings build;
    };

    struct ProjectFileError
    {
        // 0 이면 파일 자체를 열지 못한 것이다.
        std::uint32_t line = 0;
        String        message;
    };

    // 파일에서 읽는다. 실패하면 result 는 손대지 않고 error 를 채운다.
    bool LoadProjectFile(const char* path, ProjectFile& result, ProjectFileError& error);
    // 이미 읽어 둔 내용에서 읽는다. 테스트와 에디터의 미리보기가 쓴다.
    bool ParseProjectFile(
        const char* text,
        std::size_t length,
        ProjectFile& result,
        ProjectFileError& error);

    // 프로젝트 루트와 합쳐 실제로 로드할 스크립트 DLL 경로를 만든다.
    // 절대경로면 그대로 두고, 상대경로면 프로젝트 파일이 있는 폴더 기준으로 붙인다.
    String ResolveScriptModulePath(const ProjectFile& project, const char* projectFilePath);
}
