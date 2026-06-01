#include "pch.h"
#include "Utillity/String/StringUtillity.h"

namespace Utillity
{
	std::string WCharToString(const wchar_t* wstr)
	{
		if (!wstr)
			return {};

		int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
		if (size_needed <= 0)
			return {};

		std::string str;
		str.resize(size_needed); // Excludes null.

		if (!str.empty())
		{
			int converted = WideCharToMultiByte(
				CP_UTF8,
				0,
				wstr,
				-1,
				&str[0],
				size_needed,
				nullptr,
				nullptr
			);

			if (converted <= 0)
				return {};
		}

		return str;
	}

	bool WCharToString(const wchar_t* wstr, std::string& out)
	{
		out.clear();

		if (nullptr == wstr)
		{
			return false;
		}

		int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
		if (size_needed <= 0)
		{
			return false;
		}

		if (out.length() < size_needed)
		{
			out.resize(size_needed);
		}

		if (!out.empty())
		{
			int converted = WideCharToMultiByte(
				CP_UTF8,
				0,
				wstr,
				-1,
				&out[0],
				size_needed,
				nullptr,
				nullptr
			);

			if (converted <= 0)
			{
				out.clear();
				return false;
			}
		}

		return true;
	}

	std::wstring CharToWString(const char* str)
	{
		if (!str)
			return {};

		int size_needed = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
		if (size_needed <= 0)
			return {};

		std::wstring wstr;
		wstr.resize(size_needed); // Excludes null.
		if (!wstr.empty())
		{
			int converted = MultiByteToWideChar(
				CP_UTF8,
				0,
				str,
				-1,
				&wstr[0],
				size_needed
			);

			if (converted <= 0)
				return {};
		}

		return wstr;
	}

	bool CharToWString(const char* str, std::wstring& out)
	{
		out.clear();

		if (nullptr == str)
		{
			return false;
		}

		int size_needed = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
		if (size_needed <= 0)
		{
			return false;
		}

		if (out.length() < size_needed)
		{
			out.resize(size_needed);
		}

		if (!out.empty())
		{
			int converted = MultiByteToWideChar(
				CP_UTF8,
				0,
				str,
				-1,
				&out[0],
				size_needed
			);

			if (converted <= 0)
			{
				out.clear();
				return false;
			}
		}

		return true;
	}

	std::wstring U8ToWString(std::string_view utf8_str)
	{
		if (utf8_str.empty()) return std::wstring();

		int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8_str.data(), -1, nullptr, 0);
		std::wstring wstr(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, utf8_str.data(), -1, &wstr[0], size_needed);
		wstr.pop_back();
		return wstr;
	}

	std::string WStringToU8(std::wstring_view wstring)
	{
		if (wstring.empty()) return std::string();

		int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstring.data(), -1, nullptr, 0, nullptr, nullptr);
		std::string utf8_str(size_needed, 0);
		WideCharToMultiByte(CP_UTF8, 0, wstring.data(), -1, &utf8_str[0], size_needed, nullptr, nullptr);

		utf8_str.pop_back();
		return utf8_str;
	}

}
