#pragma once

#include "Editor/LiveCompile/IDynamicLibrary.h"

class CWindowsDynamicLibrary final : public IDynamicLibrary
{
public:
	bool Load(const wchar_t* path) override;
	void Unload() override;
	void* GetSymbol(const char* name) const override;
	bool IsLoaded() const override;

private:
	void* m_handle = nullptr;
};

