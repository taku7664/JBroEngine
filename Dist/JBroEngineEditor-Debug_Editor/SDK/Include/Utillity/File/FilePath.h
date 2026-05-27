#pragma once

#include <filesystem>
#include <functional>

namespace File
{
	namespace fs = std::filesystem;

	class Path;
	class Guid;
	using FString = fs::path;

	using GuidToPathResolver = const Path& (*)(const Guid&);
	using PathToGuidResolver = const Guid& (*)(const Path&);

	class Guid : public FString
	{
	public:
		Guid();
		Guid(const FString& id);
		using FString::FString;

	public:
		const File::Path& ToPath() const;
		bool IsNull() const;

	public:
		operator const File::Path&() const;
	};

	class Path : public FString
	{
	public:
		Path();
		Path(const FString& path);
		using FString::FString;

	public:
		const File::Guid& ToGuid() const;
		bool IsNull() const;

	public:
		operator const File::Guid&() const;
		File::Path operator+(const File::FString& v) const;
		File::Path operator/(const File::FString& v) const;
	};

	inline static const File::Guid NULL_GUID = L"";
	inline static const File::Path NULL_PATH = L"";

	void SetPathGuidResolver(GuidToPathResolver guidToPath, PathToGuidResolver pathToGuid);
	const File::Path& ResolvePath(const File::Guid& guid);
	const File::Guid& ResolveGuid(const File::Path& path);
	File::Guid GenerateGuid();
}

namespace std
{
	template <>
	struct hash<File::Guid>
	{
		size_t operator()(const File::Guid& guid) const
		{
			return hash<File::FString>()(guid);
		}
	};

	template <>
	struct hash<File::Path>
	{
		size_t operator()(const File::Path& path) const
		{
			return hash<File::FString>()(path);
		}
	};
}
