#pragma once

#include "ImporterWindowBase.h"
#include "Engine/Core/Asset/AudioAsset.h"

class CAudioImporterWindow final : public CImporterWindowBase
{
public:
	using CImporterWindowBase::CImporterWindowBase;

protected:
	void DrawImportOptions() override;
	bool ExecuteImport(const File::Path& sourcePath,
	                   const File::Path& destFilePath,
	                   std::string& errorOut) override;
	const char* GetSourceExtensionsCsv() const override { return ".wav,.mp3,.flac,.ogg"; }
	const char* GetTitleLocKey() const override { return "importer.audio.title"; }

private:
	AudioImportOptions m_options;
};
