#pragma once

#if JBRO_PLATFORM_WINDOWS
namespace Error
{
	void ThrowWithHResult(HRESULT hr, const std::source_location& location = std::source_location::current());
	void ThrowWithLastError(const std::source_location& location = std::source_location::current());
}
#endif
