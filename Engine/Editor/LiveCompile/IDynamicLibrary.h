#pragma once

class IDynamicLibrary
{
public:
	virtual ~IDynamicLibrary() = default;

public:
	// 한글 등 비-ASCII 사용자 경로를 안전히 처리하기 위해 wide string 으로 받는다.
	// std::filesystem::path::wstring() 결과를 그대로 넘기면 안전.
	virtual bool Load(const wchar_t* path) = 0;
	virtual void Unload() = 0;
	virtual void* GetSymbol(const char* name) const = 0;
	virtual bool IsLoaded() const = 0;
};

