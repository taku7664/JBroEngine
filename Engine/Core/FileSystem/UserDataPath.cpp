#include "pch.h"
#include "UserDataPath.h"

#include <cstdlib>

namespace
{
	// 제품명을 못 받았을 때의 뿌리 이름. 실제 게임 세이브와 섞이지 않게 이름을 구분한다.
	constexpr const char* DEFAULT_PRODUCT_NAME = "JBroEngine-Unnamed";
}

namespace UserData
{
	std::string SanitizeProductName(const char* productName)
	{
		std::string result = (productName && '\0' != productName[0]) ? productName : DEFAULT_PRODUCT_NAME;
		for (char& c : result)
		{
			const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
				|| (c >= '0' && c <= '9') || '-' == c || '_' == c || ' ' == c;
			if (false == allowed)
			{
				c = '_';
			}
		}
		return result;
	}

	File::Path ResolveRoot(const char* productName)
	{
		const std::string sanitized = SanitizeProductName(productName);

#if JBRO_PLATFORM_WEB
		// 웹은 오리진 단위로 IndexedDB 가 갈리므로 제품명을 경로에 넣지 않아도 섞이지 않는다.
		(void)sanitized;
		return File::Path("/UserData");
#elif JBRO_PLATFORM_WINDOWS
		// getenv 는 MSVC 에서 폐기 경고(C4996) 대상이라 _dupenv_s 계열을 쓴다.
		// 와이드 판인 이유는 사용자 이름에 한글이 들어가는 경우다 — 좁은 문자 판은 현재
		// 코드 페이지로 변환되어 경로가 깨진다.
		wchar_t* localAppData = nullptr;
		std::size_t localAppDataLength = 0;
		if (0 != _wdupenv_s(&localAppData, &localAppDataLength, L"LOCALAPPDATA") || nullptr == localAppData)
		{
			return File::Path();
		}
		File::Path root = File::Path(localAppData) / File::FString(sanitized);
		std::free(localAppData);
		return root;
#else
		const char* home = std::getenv("HOME");
		if (nullptr == home || '\0' == home[0])
		{
			return File::Path();
		}
		return File::Path(home) / File::FString(".local") / File::FString("share") / File::FString(sanitized);
#endif
	}
}
