#pragma once

#include <filesystem>

namespace Build
{
	// 설치된 Visual Studio 의 MSBuild.exe 절대경로를 해석한다.
	// 해석 순서:
	//   1) VSINSTALLDIR (개발자 명령 프롬프트 안에서 실행된 경우 즉시 해석)
	//   2) vswhere (-prerelease 포함 — Preview/Insiders/BuildTools/미래 버전 모두 대응)
	//   3) 실패 시 빈 경로
	// 빈 경로는 "MSBuild 를 찾지 못함" 을 뜻한다. 호출측은 반드시 이를 명시적으로
	// 처리해야 하며, bare "MSBuild.exe" 같은 실패 예정 명령을 만들면 안 된다.
	std::filesystem::path LocateMSBuild();
}
