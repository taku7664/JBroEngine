#pragma once
namespace Error
{
	void ThrowWithHResult(HRESULT hr, const std::source_location& location = std::source_location::current());
	void ThrowWithLastError(const std::source_location& location = std::source_location::current());
}