#include "pch.h"
#include "FilePath.h"

#include <iomanip>
#include <random>
#include <sstream>

namespace
{
	File::GuidToPathResolver GGuidToPathResolver = nullptr;
	File::PathToGuidResolver GPathToGuidResolver = nullptr;
}

namespace File
{
	Guid::Guid()
		: FString()
	{
	}

	Guid::Guid(const FString& id)
		: FString(id)
	{
	}

	const File::Path& Guid::ToPath() const
	{
		return ResolvePath(*this);
	}

	bool Guid::IsNull() const
	{
		return empty();
	}

	Guid::operator const File::Path&() const
	{
		return ToPath();
	}

	Path::Path()
		: FString()
	{
	}

	Path::Path(const FString& path)
		: FString(path)
	{
	}

	const File::Guid& Path::ToGuid() const
	{
		return ResolveGuid(*this);
	}

	bool Path::IsNull() const
	{
		return empty();
	}

	Path::operator const File::Guid&() const
	{
		return ToGuid();
	}

	File::Path Path::operator+(const File::FString& v) const
	{
		File::FString result = static_cast<const File::FString&>(*this);
		result += v;
		return File::Path(result);
	}

	File::Path Path::operator/(const File::FString& v) const
	{
		return File::Path(static_cast<const File::FString&>(*this) / v);
	}

	void SetPathGuidResolver(GuidToPathResolver guidToPath, PathToGuidResolver pathToGuid)
	{
		GGuidToPathResolver = guidToPath;
		GPathToGuidResolver = pathToGuid;
	}

	const File::Path& ResolvePath(const File::Guid& guid)
	{
		if (nullptr == GGuidToPathResolver)
		{
			return NULL_PATH;
		}

		return GGuidToPathResolver(guid);
	}

	const File::Guid& ResolveGuid(const File::Path& path)
	{
		if (nullptr == GPathToGuidResolver)
		{
			return NULL_GUID;
		}

		return GPathToGuidResolver(path);
	}

	File::Guid GenerateGuid()
	{
		std::random_device randomDevice;
		std::mt19937_64 engine(randomDevice());
		std::uniform_int_distribution<std::uint64_t> distribution;

		const std::uint64_t high = distribution(engine);
		const std::uint64_t low = distribution(engine);

		std::ostringstream stream;
		stream << std::hex << std::setfill('0')
			<< std::setw(16) << high
			<< std::setw(16) << low;
		return File::Guid(stream.str());
	}
}
