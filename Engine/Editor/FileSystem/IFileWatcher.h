#pragma once

#include "Engine/Editor/FileSystem/FileWatcherTypes.h"

class IFileWatcher
{
public:
	virtual ~IFileWatcher() = default;

public:
	virtual bool Watch(const FileWatcherDesc& desc) = 0;
	virtual void Stop() = 0;
	virtual void Poll() = 0;
	virtual bool TakeEvents(std::vector<FileWatchEvent>& outEvents) = 0;
	virtual bool IsWatching() const = 0;
};

