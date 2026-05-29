#pragma once

#include <vector>

enum class EFileWatchEventType
{
	Created,
	Modified,
	Deleted,
	Renamed
};

struct FileWatchEvent
{
	EFileWatchEventType Type = EFileWatchEventType::Modified;
	File::Path Path;
	File::Path OldPath;
};

struct FileWatcherDesc
{
	File::Path RootPath;
	bool Recursive = true;
};

