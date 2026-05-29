#pragma once

#include "Editor/Project/ProjectTypes.h"

#include <filesystem>
#include <string>

class CGameScriptProjectGenerator
{
public:
	bool EnsureProject(const ProjectInfo& projectInfo) const;

private:
	bool WriteFileIfMissing(const std::filesystem::path& path, const std::string& content) const;
	bool WriteGeneratedFile(const std::filesystem::path& path, const std::string& content) const;
	void RemoveStaleGeneratedFiles(const std::filesystem::path& contentPath) const;
	bool WriteTextFile(const std::filesystem::path& path, const std::string& content) const;
	std::string BuildProjectFile(const ProjectInfo& projectInfo) const;
	std::string BuildSolutionFile(const ProjectInfo& projectInfo) const;
	std::string BuildPchHeader() const;
	std::string BuildPchSource() const;
	std::string BuildGameScriptApiHeader() const;
	std::string BuildGameModuleEntryHeader() const;
	std::string BuildGameModuleSource() const;
	std::string BuildGeneratedRegistryHeader() const;
	std::string BuildGeneratedRegistrySource(const ProjectInfo& projectInfo) const;
	std::string BuildDefaultScriptHeader() const;
	std::string BuildDefaultScriptSource() const;
};
