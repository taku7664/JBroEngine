#pragma once
namespace Utillity
{
	// wchar_t* to std::string (UTF-8)
	std::string WCharToString(const wchar_t* wstr);
	bool WCharToString(const wchar_t* wstr, std::string& out);

	// char* to std::wstring (UTF-8)
	std::wstring CharToWString(const char* str);
	bool CharToWString(const char* str, std::wstring& out);

	std::wstring U8ToWString(std::string_view utf8_str);

	std::string WStringToU8(std::wstring_view wstring);

	inline const char* U8(const char8_t* text)
	{
		return reinterpret_cast<const char*>(text);
	}

    // Returns the length of a string literal at compile time.
    template <std::size_t N>
    constexpr std::size_t Strlen(const char(&str)[N])
    {
        return N - 1;
    }

    // Returns a compile-time slice of a string literal.
    template <std::size_t N, std::size_t START, std::size_t COUNT>
    constexpr std::array<char, COUNT + 1> SliceLiteral(const char(&str)[N])
    {
        static_assert(START + COUNT <= N - 1, "string size overrflow");
        std::array<char, COUNT + 1> result{};
        for (std::size_t i = 0; i < COUNT; ++i)
        {
            result[i] = str[i + START];
        }
        result[COUNT] = '\0';
        return result;
    };
}
