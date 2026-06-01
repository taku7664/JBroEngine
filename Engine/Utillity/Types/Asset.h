#pragma once

#include "Utillity/File/FilePath.h"

#include <functional>
#include <utility>

class Asset : public File::Guid
{
public:
	using File::Guid::Guid;

	Asset() = default;
	Asset(const File::Guid& guid) : File::Guid(guid) {}
	Asset(File::Guid&& guid) : File::Guid(std::move(guid)) {}

	const File::Guid& GetGuid() const noexcept { return *this; }
	File::Guid& GetGuid() noexcept { return *this; }
	void SetGuid(const File::Guid& guid) { File::Guid::operator=(guid); }
	void Clear() { File::Guid::operator=(File::NULL_GUID); }
	bool IsValid() const { return !IsNull(); }
	explicit operator bool() const { return IsValid(); }
};

namespace std
{
	template <>
	struct hash<Asset>
	{
		std::size_t operator()(const Asset& value) const
		{
			return hash<File::Guid>()(value.GetGuid());
		}
	};
}
