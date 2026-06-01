#pragma once

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class String : public std::string
{
public:
	using Base = std::string;
	using Base::Base;
	using Base::operator=;

	String() = default;
	String(const Base& value) : Base(value) {}
	String(Base&& value) noexcept : Base(std::move(value)) {}
	String(std::string_view value) : Base(value) {}

	String& operator=(std::string_view value)
	{
		assign(value.data(), value.size());
		return *this;
	}

	std::string_view View() const noexcept { return std::string_view(data(), size()); }
	const Base& Std() const noexcept { return *this; }
	Base& Std() noexcept { return *this; }

	bool IsEmpty() const noexcept { return empty(); }
	bool IsNotEmpty() const noexcept { return !empty(); }

	static std::size_t LocalCapacity()
	{
		return Base().capacity();
	}

	bool FitsInLocalBuffer() const
	{
		return size() <= LocalCapacity();
	}

	void Reserve(std::size_t capacity)
	{
		reserve(capacity);
	}

	void ReserveAdditional(std::size_t additional)
	{
		if (additional > capacity() - size())
		{
			reserve(size() + additional);
		}
	}

	void Clear(bool keepCapacity = true)
	{
		if (keepCapacity)
		{
			clear();
			return;
		}
		Base().swap(*this);
	}

	void Reset()
	{
		Base().swap(*this);
	}

	void Shrink()
	{
		shrink_to_fit();
	}

	bool StartsWith(std::string_view prefix) const noexcept
	{
		const std::string_view self = View();
		return self.size() >= prefix.size() && self.substr(0, prefix.size()) == prefix;
	}

	bool EndsWith(std::string_view suffix) const noexcept
	{
		const std::string_view self = View();
		return self.size() >= suffix.size() && self.substr(self.size() - suffix.size()) == suffix;
	}

	bool Contains(std::string_view text) const noexcept
	{
		return View().find(text) != std::string_view::npos;
	}

	String Substr(std::size_t offset, std::size_t count = npos) const
	{
		return String(Base::substr(offset, count));
	}

	String& Append(std::string_view text)
	{
		append(text.data(), text.size());
		return *this;
	}

	String& ReplaceAll(std::string_view from, std::string_view to)
	{
		if (from.empty())
		{
			return *this;
		}

		std::size_t pos = 0;
		while ((pos = find(from.data(), pos, from.size())) != npos)
		{
			replace(pos, from.size(), to.data(), to.size());
			pos += to.size();
		}
		return *this;
	}

	String& TrimLeft()
	{
		const auto it = std::find_if_not(begin(), end(), [](unsigned char c) { return 0 != std::isspace(c); });
		erase(begin(), it);
		return *this;
	}

	String& TrimRight()
	{
		const auto it = std::find_if_not(rbegin(), rend(), [](unsigned char c) { return 0 != std::isspace(c); });
		erase(it.base(), end());
		return *this;
	}

	String& Trim()
	{
		return TrimLeft().TrimRight();
	}

	String Trimmed() const
	{
		String copy(*this);
		copy.Trim();
		return copy;
	}

	String& ToLower()
	{
		std::transform(begin(), end(), begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return *this;
	}

	String& ToUpper()
	{
		std::transform(begin(), end(), begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		return *this;
	}

	String ToLowerCopy() const
	{
		String copy(*this);
		copy.ToLower();
		return copy;
	}

	String ToUpperCopy() const
	{
		String copy(*this);
		copy.ToUpper();
		return copy;
	}

	std::vector<String> Split(char delimiter, bool skipEmpty = false) const
	{
		std::vector<String> result;
		std::size_t start = 0;
		while (start <= size())
		{
			const std::size_t end = find(delimiter, start);
			const std::size_t count = (npos == end) ? npos : end - start;
			if (0 != count || false == skipEmpty)
			{
				result.emplace_back(substr(start, count));
			}
			if (npos == end)
			{
				break;
			}
			start = end + 1;
		}
		return result;
	}
};

static_assert(sizeof(String) == sizeof(std::string));

namespace std
{
	template <>
	struct hash<String>
	{
		std::size_t operator()(const String& value) const noexcept
		{
			return hash<std::string_view>()(value.View());
		}
	};
}
