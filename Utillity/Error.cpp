#include "pch.h"
#include "Error.h"

void Error::ThrowWithHResult(HRESULT hr, const std::source_location & location)
{
    if (FAILED(hr))
    {
        ::std::string msg = ::std::format("HRESULT failed to code 0x{:08X}\nat func: {} line: {}",
            static_cast<std::uint32_t>(hr),
            location.function_name(),
            location.line()
        );
        throw ::std::runtime_error(msg);
    }
}

void Error::ThrowWithLastError(const std::source_location& location)
{
    DWORD errorCode = GetLastError();
    ::std::string msg = ::std::format("Throw with Last Error code 0x{:08X}\nat func: {} line: {}",
        errorCode,
        location.function_name(),
        location.line()
    );
    throw ::std::runtime_error(msg);
}