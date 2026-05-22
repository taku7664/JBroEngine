#pragma once

class IDynamicLibrary
{
public:
	virtual ~IDynamicLibrary() = default;

public:
	virtual bool Load(const char* path) = 0;
	virtual void Unload() = 0;
	virtual void* GetSymbol(const char* name) const = 0;
	virtual bool IsLoaded() const = 0;
};

