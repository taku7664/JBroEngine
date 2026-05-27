#pragma once

#include "FilePath.h"

#include <ctime>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace File
{
	enum class EventType
	{
		Unknown = 0,
		Added,
		Removed,
		Modified,
		Renamed,
		Moved,
	};

	using FileDialogFilter = std::pair<const wchar_t*, const wchar_t*>;
	using FileDialogOwnerHandle = void*;

	enum DirectoryDialogFlags
	{
		DIRECTORY_DIALOG_FLAG_NONE = 0,
		DIRECTORY_DIALOG_FLAG_SAVE_FILE = 1 << 0,
		DIRECTORY_DIALOG_FLAG_OPEN_FILE = 1 << 1,
		DIRECTORY_DIALOG_FLAG_ALLOW_MULTISELECT = 1 << 2,
		DIRECTORY_DIALOG_FLAG_PICK_FOLDER = 1 << 3,
	};

	struct FileDialogDesc
	{
		FileDialogOwnerHandle Owner = nullptr;
		const wchar_t* Title = L"";
		const wchar_t* InitialDirectory = L"";
		const wchar_t* DefaultFileName = L"";
		std::vector<FileDialogFilter> Filters;
		std::uint32_t Flags = DIRECTORY_DIALOG_FLAG_NONE;
	};

	bool CreateGuid(File::Guid& outGuid);
	bool CreateFolder(const File::Path& path);
	bool CreateFolderEx(const File::Path& path, bool processDuplicate = false);
	bool OpenFile(const File::Path& path);
	bool RemoveFile(const File::Path& path);
	bool CopyFileFromTo(const File::Path& from, const File::Path& to);
	bool CopyStrToClipBoard(std::string_view text);
	bool CopyPathToClipBoard(const File::Path& path);
	File::Path GenerateUniquePath(const File::Path& path, int maxIndex = 999);

	bool ShowOpenFileDialog(
		FileDialogOwnerHandle owner,
		const wchar_t* title,
		const wchar_t* initialDirectory,
		std::vector<FileDialogFilter> filters,
		File::Path& out);

	bool ShowOpenFileDialog(
		FileDialogOwnerHandle owner,
		const wchar_t* title,
		const wchar_t* initialDirectory,
		std::vector<FileDialogFilter> filters,
		std::vector<File::Path>& out);

	bool ShowOpenFileDialog(
		FileDialogOwnerHandle owner,
		const wchar_t* title,
		const wchar_t* initialDirectory,
		std::vector<FileDialogFilter> filters,
		bool allowMultiSelect,
		std::vector<File::Path>& out);

	bool ShowSaveFileDialog(
		FileDialogOwnerHandle owner,
		const wchar_t* title,
		const wchar_t* initialDirectory,
		const wchar_t* defaultName,
		const std::vector<FileDialogFilter>& filters,
		File::Path& out);

	bool ShowOpenFolderDialog(
		FileDialogOwnerHandle owner,
		const wchar_t* title,
		const wchar_t* initialDirectory,
		File::Path& out);

	bool ShowFileDialogEx(const FileDialogDesc& desc, std::vector<File::Path>& out);

	std::wstring GetDesktopPath();
	void SetClipboardText(std::wstring_view text);
	std::wstring GetClipboardText();
	std::time_t GetFileLastWriteTime(const fs::directory_entry& entry);

	class Compare
	{
	public:
		enum SortFlags
		{
			FLAGS_SORT_BY_NONE = 0,
			FLAGS_SORT_BY_TYPE = 1 << 1,
			FLAGS_SORT_BY_NAME = 1 << 2,
			FLAGS_SORT_BY_DATE = 1 << 3,
		};

	public:
		explicit Compare(int sortFlags = 0);
		bool operator()(const fs::directory_entry& a, const fs::directory_entry& b) const;

	private:
		int m_flags = 0;
	};
}
