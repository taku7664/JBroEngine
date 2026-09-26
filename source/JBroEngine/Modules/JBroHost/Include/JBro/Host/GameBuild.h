#pragma once

#include <JBro/Host/ProjectFile.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

#include <cstdint>

namespace JBro
{
    struct GameBuildOptions
    {
        // 복사할 게임 호스트 실행 파일(UTF-8 절대경로)이다. 차원에 맞는 것을 부르는 쪽(에디터)이 고른다. 비면 실행 파일 없이 패키지와 프로젝트만 낸다.
        String gameHostPath;
        // 0 이상이면 내놓는 프로젝트의 물리 워커 수로 적는다(`Auto` 를 빌드 때 정한다, D-223). 게임은 패키지에서 캔버스를 세어 볼 수 없다.
        std::int32_t physicsWorkers = -1;
    };

    struct GameBuildReport
    {
        // 내놓은 폴더(UTF-8 절대경로)다.
        String outputFolder;
        std::uint32_t assets = 0;
        std::uint32_t cookedTextures = 0;
        std::uint64_t packageBytes = 0;
        // 빌드를 멈추지 않는 것(레지스트리에 없는 아이디 따위)이다. 영어, 로그용이다.
        Array<String> warnings;
        // 실패하면 까닭(영어)이다.
        String error;
    };

    // **게임을 빌드한다**(D-227, package-plan §2.4). 에셋 폴더를 다시 훑고, 시작 캔버스·빌드 캔버스·프로젝트 폰트·모든 문자열 표에서 참조를 따라가
    // 게임이 쓰는 에셋을 모아 굽고, `<OutputDirectory>/<ProductName>/` 에 다음을 낸다:
    //
    //     <ProductName>.exe          게임 호스트의 사본(있으면)
    //     <ProductName>.jproject     `AssetPackage` 를 적은 프로젝트 사본. 캔버스 경로는 에셋 폴더 기준이다
    //     GameScript.dll             스크립트 DLL(있으면, `Build.ScriptOutputLibraryPath` 이름)
    //     Content/game.jpak          에셋 패키지
    //
    // 원본 프로젝트는 건드리지 않는다. 파일은 플랫폼이 쓴다(D-112).
    bool BuildGame(IPlatform& platform, const ProjectFile& project, const char* projectFilePath, const GameBuildOptions& options,
        GameBuildReport& report);

    // 프로젝트 기준 경로(`Build.StartupCanvas` 따위)를 에셋 폴더 기준으로 바꾼다. 에셋 폴더 밖이면 빈 글자다.
    String ToAssetRelativePath(const ProjectFile& project, const char* projectFilePath, const String& projectRelative);
}
