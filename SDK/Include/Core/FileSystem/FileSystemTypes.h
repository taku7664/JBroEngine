#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class EFileSystemAccess
{
	ReadOnly,
	ReadWrite
};

struct FileInfo
{
	std::string Path;
	std::uint64_t Size = 0;
	bool Exists = false;
	bool IsDirectory = false;
};

struct FileReadResult
{
	std::vector<std::uint8_t> Bytes;
	bool Success = false;
};
