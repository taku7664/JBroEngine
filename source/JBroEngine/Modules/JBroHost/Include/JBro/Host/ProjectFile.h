#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <cstdint>

namespace JBro
{
    class IPlatform;
}

namespace JBro
{
    // 프로젝트 파일이다. 기존 엔진과 같은 `.jproject` 확장자와 같은 키 이름을 쓴다 —
    // 이름을 새로 지을 이유가 없기 때문이다. **다만 기존 엔진이 쓴 파일을 그대로 여는 것은
    // 더 이상 목표가 아니다**(D-99). 이 엔진은 `EngineVersion` 과 `Framework` 를 요구하고
    // 기존 엔진의 파일에는 그 두 키가 없다. 옮겨 올 프로젝트가 없어서 내린 결정이다.
    //
    // 형식은 YAML 이되 **읽는 것은 그 부분집합**이다. 아래가 전부다. **모르는 키와 그 아래
    // 블록은 조용히 건너뛴다** - 기존 엔진이 쓴 키를 다 알지 못해도 그 프로젝트를 열어야
    // 하고, 새 엔진이 나중에 키를 더해도 옛 엔진이 열 수 있어야 한다. 실패하는 것은
    // **형식이 틀린 것**이다: 탭 들여쓰기, 두 칸이 아닌 들여쓰기, 키 없는 시퀀스 항목,
    // 닫히지 않은 따옴표, `key: value` 가 아닌 줄, 아는 키에 타입이 맞지 않는 값,
    // 그리고 **`EngineVersion` 이나 `Framework` 가 없는 것**이다. 그때는 추측하지 않고
    // 줄 번호와 함께 실패한다. 부분집합은 다음과 같다.
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
    // 프로젝트가 어느 프레임워크 위에서 도는지다. 에디터와 게임 호스트가 이 값으로
    // 어느 프레임워크를 만들지 고른다. **프로젝트 파일이 이것을 적는 자리**이므로
    // 여기에 둔다(D-99). 에디터 빌드는 2D 와 3D 를 둘 다 포함한다(ProjectRule §4).
    enum class FrameworkKind : std::uint8_t
    {
        Framework2D,
        Framework3D
    };

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
        // 이 프로젝트를 여는 엔진 버전이다(`EngineVersion`). **없으면 파일이 거절된다**(D-99) -
        // 런처가 이 값으로 어느 엔진 설치를 띄울지 고르므로, 비어 있으면 고를 수가 없다.
        String        engineVersion;
        // 2D 인지 3D 인지다(`Framework`). **없으면 파일이 거절된다**(D-99) - 기본값을
        // 정해 두면 3D 프로젝트가 조용히 2D 로 열리고, 그 화면은 비어 있다.
        FrameworkKind framework = FrameworkKind::Framework2D;
        String        rootPath = ".";
        std::uint32_t resolutionWidth = 1920;
        std::uint32_t resolutionHeight = 1080;
        // 텍스처 샘플링의 프로젝트 기본(`TextureFilter: Nearest|Linear`, D-117). 텍스처의 임포트 옵션이 덮어쓴다.
        // 2D 픽셀 아트가 기본 대상이라 `Nearest` 다. `Default` 는 파일에 적을 수 없다.
        TextureFilter textureFilter = TextureFilter::Nearest;
        bool          debugModeEnabled = false;
        // 에디터가 스크립트를 빌드해 내놓는 자리다. 프로젝트 루트 기준 상대경로다.
        String        scriptSourceDirectory = "Contents";
        String        scriptOutputLibraryPath = "x64/Debug/GameScript.dll";
        String        lastOpenedCanvasPath;
        // **에디터가 다시 열릴 때의 자리다**(D-146). 기존 엔진도 이 값들을 프로젝트에 적었다 -
        // 프로젝트를 다시 열었을 때 보던 캔버스와 보던 자리가 그대로여야 이어서 일할 수 있다.
        // 에디터 언어도 여기 있다: 사람마다 다른 값이지만, 프로젝트를 옮겨 다니며 쓰는 것이
        // 한 사람이라 프로젝트에 두어도 어긋나지 않고, 둘 곳을 따로 만들면 그 파일의 자리를
        // 또 정해야 한다.
        String        editorLocale;
        float         canvasViewCameraX = 0.0f;
        float         canvasViewCameraY = 0.0f;
        // 0 이면 적힌 적이 없다는 뜻이다. 화면 세로 절반이 담는 월드 길이라 0 일 수 없다.
        float         canvasViewCameraSize = 0.0f;
        // 에셋 폴더다(`AssetDirectory`). 프로젝트 루트 기준 상대경로이고 레지스트리가 이 아래를 스캔한다(D-111).
        // 기존 엔진에는 이 키가 없었다 - 코드 기본값 `Assets` 였다.
        String        assetDirectory = "Contents/Assets";
        // 스캔과 파일 감시가 건너뛸 이름 패턴이다(`AssetIgnorePatterns`, `*`·`?`). 숨김 폴더는 패턴과 무관하게 건너뛴다.
        Array<String> assetIgnorePatterns;
        ProjectBuildSettings build;
    };

    struct ProjectFileError
    {
        // 0 이면 파일 자체를 열지 못한 것이다.
        std::uint32_t line = 0;
        String        message;
    };

    // 파일에서 읽는다. 실패하면 result 는 손대지 않고 error 를 채운다.
    bool LoadProjectFile(IPlatform& platform, const char* utf8Path, ProjectFile& result, ProjectFileError& error);
    // 이미 읽어 둔 내용에서 읽는다. 테스트와 에디터의 미리보기가 쓴다.
    bool ParseProjectFile(
        const char* text,
        std::size_t length,
        ProjectFile& result,
        ProjectFileError& error);

    // **읽은 글자를 고쳐 쓴다**(D-137). 새로 지어내지 않고 원문의 줄을 타고 가며 아는 키의
    // 값만 바꾼다 - 주석도, 우리가 모르는 키도, 시퀀스도 그 자리에 그대로 남는다.
    //
    // 통째로 다시 쓰면 **모르는 키가 사라진다**. 파서는 모르는 키를 조용히 건너뛰도록
    // 되어 있으므로(위 설명), 읽고 다시 쓰는 것만으로 남의 설정이 없어진다.
    // 에셋 메타에서 같은 자리를 이미 한 번 겪었다(D-123·D-124).
    //
    // 아는 키가 원문에 없으면 **맨 뒤에 더한다**. `Build:` 아래의 키는 그 블록 끝에 더하고,
    // 블록 자체가 없으면 블록째 더한다. 시퀀스 키(`AssetIgnorePatterns`·`BuildCanvases`)는
    // 손대지 않는다 - 값이 여러 줄이라 한 줄 바꿔치기로는 다룰 수 없다.
    bool WriteProjectFileText(
        const ProjectFile& project,
        const char* originalText,
        std::size_t originalLength,
        String& result,
        ProjectFileError& error);

    // 위의 글자를 파일에 쓴다. **바꿔치기다**(D-124) - `<파일>.tmp` 에 먼저 쓰고 옮긴다.
    // 쓰다 만 파일로 프로젝트를 잃지 않는다.
    bool SaveProjectFile(IPlatform& platform, const char* utf8Path, const ProjectFile& project,
        ProjectFileError& error);

    // 프로젝트 루트와 합쳐 실제로 로드할 스크립트 DLL 경로를 만든다.
    // 절대경로면 그대로 두고, 상대경로면 프로젝트 파일이 있는 폴더 기준으로 붙인다.
    String ResolveScriptModulePath(const ProjectFile& project, const char* projectFilePath);
    // 프로젝트 파일이 있는 폴더 기준의 상대경로를 푼다. 절대경로면 그대로다. 에셋 폴더(`AssetDirectory`)가 이것을 쓴다.
    String ResolveProjectRelativePath(const char* relative, const char* projectFilePath);
}
