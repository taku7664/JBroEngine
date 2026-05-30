#pragma once

#include "ImporterWindowBase.h"
#include "Engine/Core/Asset/SpriteAsset.h"

class CSpriteImporterWindow final : public CImporterWindowBase
{
public:
	using CImporterWindowBase::CImporterWindowBase;

protected:
	void DrawImportOptions() override;
	bool ExecuteImport(const File::Path& sourcePath,
	                   const File::Path& destFilePath,
	                   std::string& errorOut) override;
	const char* GetSourceExtensionsCsv() const override { return ".png,.jpg,.jpeg,.bmp,.tga"; }
	const char* GetTitleLocKey() const override { return "importer.sprite.title"; }

private:
	SpriteImportOptions m_options;
};
