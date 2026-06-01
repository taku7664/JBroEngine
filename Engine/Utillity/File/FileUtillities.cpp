#include "pch.h"
#include "FileUtillities.h"

#include <algorithm>
#include <chrono>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include "Utillity/String/StringUtillity.h"
#endif

namespace
{
#if defined(_WIN32)
	template<typename T>
	void SafeRelease(T*& ptr)
	{
		if (ptr)
		{
			ptr->Release();
			ptr = nullptr;
		}
	}

	class ComInitializeScope
	{
	public:
		ComInitializeScope()
		{
			m_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
			m_shouldUninitialize = SUCCEEDED(m_hr);
		}

		~ComInitializeScope()
		{
			if (m_shouldUninitialize)
			{
				CoUninitialize();
			}
		}

		bool CanUseCom() const
		{
			return SUCCEEDED(m_hr);
		}

	private:
		HRESULT m_hr = E_FAIL;
		bool m_shouldUninitialize = false;
	};
	std::vector<COMDLG_FILTERSPEC> BuildFilterSpecs(const std::vector<File::FileDialogFilter>& filters)
	{
		std::vector<COMDLG_FILTERSPEC> result;
		result.reserve(filters.size());
		for (const File::FileDialogFilter& filter : filters)
		{
			result.push_back(COMDLG_FILTERSPEC{
				filter.first ? filter.first : L"",
				filter.second ? filter.second : L"*.*"
			});
		}
		return result;
	}
#endif
}

namespace File
{
	bool CreateGuid(File::Guid& outGuid)
	{
		outGuid = GenerateGuid();
		return false == outGuid.IsNull();
	}

	bool CreateFolder(const File::Path& path)
	{
		std::error_code errorCode;
		return std::filesystem::create_directory(path, errorCode) && false == static_cast<bool>(errorCode);
	}

	bool CreateFolderEx(const File::Path& path, bool processDuplicate)
	{
		if (false == processDuplicate)
		{
			std::error_code errorCode;
			return std::filesystem::create_directories(path, errorCode) && false == static_cast<bool>(errorCode);
		}

		return CreateFolderEx(GenerateUniquePath(path), false);
	}

	bool OpenFile(const File::Path& path)
	{
#if defined(_WIN32)
		return reinterpret_cast<intptr_t>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
		(void)path;
		return false;
#endif
	}

	bool RemoveFile(const File::Path& path)
	{
		std::error_code errorCode;
		return std::filesystem::remove(path, errorCode) && false == static_cast<bool>(errorCode);
	}

	bool CopyFileFromTo(const File::Path& from, const File::Path& to)
	{
		std::error_code errorCode;
		return std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, errorCode) && false == static_cast<bool>(errorCode);
	}

	bool CopyStrToClipBoard(std::string_view text)
	{
#if defined(_WIN32)
		SetClipboardText(Utillity::U8ToWString(text));
		return true;
#else
		(void)text;
		return false;
#endif
	}

	bool CopyPathToClipBoard(const File::Path& path)
	{
		SetClipboardText(path.wstring());
		return true;
	}

	File::Path GenerateUniquePath(const File::Path& path, int maxIndex)
	{
		if (false == std::filesystem::exists(path))
		{
			return path;
		}

		const std::filesystem::path parent = path.parent_path();
		const std::wstring stem = path.stem().wstring();
		const std::wstring extension = path.extension().wstring();
		for (int index = 1; index <= maxIndex; ++index)
		{
			File::Path candidate = parent / (stem + L"_" + std::to_wstring(index) + extension);
			if (false == std::filesystem::exists(candidate))
			{
				return candidate;
			}
		}

		return path;
	}

	bool ShowOpenFileDialog(FileDialogOwnerHandle owner, const wchar_t* title, const wchar_t* initialDirectory, std::vector<FileDialogFilter> filters, File::Path& out)
	{
		std::vector<File::Path> results;
		if (false == ShowOpenFileDialog(owner, title, initialDirectory, std::move(filters), false, results) || results.empty())
		{
			return false;
		}

		out = results.front();
		return true;
	}

	bool ShowOpenFileDialog(FileDialogOwnerHandle owner, const wchar_t* title, const wchar_t* initialDirectory, std::vector<FileDialogFilter> filters, std::vector<File::Path>& out)
	{
		return ShowOpenFileDialog(owner, title, initialDirectory, std::move(filters), true, out);
	}

	bool ShowOpenFileDialog(FileDialogOwnerHandle owner, const wchar_t* title, const wchar_t* initialDirectory, std::vector<FileDialogFilter> filters, bool allowMultiSelect, std::vector<File::Path>& out)
	{
		FileDialogDesc desc;
		desc.Owner = owner;
		desc.Title = title;
		desc.InitialDirectory = initialDirectory;
		desc.Filters = std::move(filters);
		desc.Flags = DIRECTORY_DIALOG_FLAG_OPEN_FILE | (allowMultiSelect ? DIRECTORY_DIALOG_FLAG_ALLOW_MULTISELECT : DIRECTORY_DIALOG_FLAG_NONE);
		return ShowFileDialogEx(desc, out);
	}

	bool ShowSaveFileDialog(FileDialogOwnerHandle owner, const wchar_t* title, const wchar_t* initialDirectory, const wchar_t* defaultName, const std::vector<FileDialogFilter>& filters, File::Path& out)
	{
		FileDialogDesc desc;
		desc.Owner = owner;
		desc.Title = title;
		desc.InitialDirectory = initialDirectory;
		desc.DefaultFileName = defaultName;
		desc.Filters = filters;
		desc.Flags = DIRECTORY_DIALOG_FLAG_SAVE_FILE;

		std::vector<File::Path> results;
		if (false == ShowFileDialogEx(desc, results) || results.empty())
		{
			return false;
		}

		out = results.front();
		return true;
	}

	bool ShowOpenFolderDialog(FileDialogOwnerHandle owner, const wchar_t* title, const wchar_t* initialDirectory, File::Path& out)
	{
		FileDialogDesc desc;
		desc.Owner = owner;
		desc.Title = title;
		desc.InitialDirectory = initialDirectory;
		desc.Flags = DIRECTORY_DIALOG_FLAG_PICK_FOLDER;

		std::vector<File::Path> results;
		if (false == ShowFileDialogEx(desc, results) || results.empty())
		{
			return false;
		}

		out = results.front();
		return true;
	}

	bool ShowFileDialogEx(const FileDialogDesc& desc, std::vector<File::Path>& out)
	{
		out.clear();
#if defined(_WIN32)
		const bool pickFolder = 0 != (desc.Flags & DIRECTORY_DIALOG_FLAG_PICK_FOLDER);
		const bool saveFile = 0 != (desc.Flags & DIRECTORY_DIALOG_FLAG_SAVE_FILE);
		const bool allowMultiSelect = 0 != (desc.Flags & DIRECTORY_DIALOG_FLAG_ALLOW_MULTISELECT);

		ComInitializeScope comScope;
		if (false == comScope.CanUseCom())
		{
			return false;
		}

		IFileDialog* dialog = nullptr;
		HRESULT hr = CoCreateInstance(
			saveFile ? CLSID_FileSaveDialog : CLSID_FileOpenDialog,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&dialog));
		if (FAILED(hr) || nullptr == dialog)
		{
			return false;
		}

		DWORD options = 0;
		if (SUCCEEDED(dialog->GetOptions(&options)))
		{
			options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
			if (pickFolder)
			{
				options |= FOS_PICKFOLDERS | FOS_PATHMUSTEXIST;
			}
			else if (saveFile)
			{
				options |= FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST;
			}
			else
			{
				options |= FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
				if (allowMultiSelect)
				{
					options |= FOS_ALLOWMULTISELECT;
				}
			}
			dialog->SetOptions(options);
		}

		if (desc.Title && desc.Title[0] != L'\0')
		{
			dialog->SetTitle(desc.Title);
		}
		if (desc.InitialDirectory && desc.InitialDirectory[0] != L'\0')
		{
			IShellItem* initialFolder = nullptr;
			if (SUCCEEDED(SHCreateItemFromParsingName(desc.InitialDirectory, nullptr, IID_PPV_ARGS(&initialFolder)))
				&& nullptr != initialFolder)
			{
				dialog->SetFolder(initialFolder);
				SafeRelease(initialFolder);
			}
		}
		if (desc.DefaultFileName && desc.DefaultFileName[0] != L'\0')
		{
			dialog->SetFileName(desc.DefaultFileName);
		}
		if (false == pickFolder && false == desc.Filters.empty())
		{
			std::vector<COMDLG_FILTERSPEC> filterSpecs = BuildFilterSpecs(desc.Filters);
			dialog->SetFileTypes(static_cast<UINT>(filterSpecs.size()), filterSpecs.data());
			dialog->SetFileTypeIndex(1);
		}

		HWND owner = static_cast<HWND>(desc.Owner);
		if (nullptr == owner)
		{
			owner = GetActiveWindow();
		}
		hr = dialog->Show(owner);
		if (FAILED(hr))
		{
			SafeRelease(dialog);
			return false;
		}

		if (allowMultiSelect && false == saveFile && false == pickFolder)
		{
			IFileOpenDialog* openDialog = nullptr;
			hr = dialog->QueryInterface(IID_PPV_ARGS(&openDialog));
			if (FAILED(hr) || nullptr == openDialog)
			{
				SafeRelease(dialog);
				return false;
			}

			IShellItemArray* results = nullptr;
			hr = openDialog->GetResults(&results);
			if (FAILED(hr) || nullptr == results)
			{
				SafeRelease(openDialog);
				SafeRelease(dialog);
				return false;
			}

			DWORD count = 0;
			results->GetCount(&count);
			for (DWORD i = 0; i < count; ++i)
			{
				IShellItem* item = nullptr;
				if (SUCCEEDED(results->GetItemAt(i, &item)) && nullptr != item)
				{
					PWSTR path = nullptr;
					if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && nullptr != path)
					{
						out.emplace_back(path);
						CoTaskMemFree(path);
					}
					SafeRelease(item);
				}
			}

			SafeRelease(results);
			SafeRelease(openDialog);
			SafeRelease(dialog);
			return false == out.empty();
		}

		IShellItem* result = nullptr;
		hr = dialog->GetResult(&result);
		if (FAILED(hr) || nullptr == result)
		{
			SafeRelease(dialog);
			return false;
		}

		PWSTR path = nullptr;
		hr = result->GetDisplayName(SIGDN_FILESYSPATH, &path);
		if (SUCCEEDED(hr) && nullptr != path)
		{
			out.emplace_back(path);
			CoTaskMemFree(path);
		}

		SafeRelease(result);
		SafeRelease(dialog);
		return false == out.empty();
#else
		(void)desc;
		return false;
#endif
	}

	std::wstring GetDesktopPath()
	{
#if defined(_WIN32)
		PWSTR path = nullptr;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path)) && path)
		{
			std::wstring result(path);
			CoTaskMemFree(path);
			return result;
		}
#endif
		return L"C:";
	}

	void SetClipboardText(std::wstring_view text)
	{
#if defined(_WIN32)
		if (FALSE == OpenClipboard(nullptr))
		{
			return;
		}

		EmptyClipboard();
		const SIZE_T byteSize = (text.size() + 1) * sizeof(wchar_t);
		HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byteSize);
		if (memory)
		{
			void* data = GlobalLock(memory);
			if (data)
			{
				std::memcpy(data, text.data(), text.size() * sizeof(wchar_t));
				static_cast<wchar_t*>(data)[text.size()] = L'\0';
				GlobalUnlock(memory);
				SetClipboardData(CF_UNICODETEXT, memory);
			}
		}

		CloseClipboard();
#else
		(void)text;
#endif
	}

	std::wstring GetClipboardText()
	{
#if defined(_WIN32)
		if (FALSE == OpenClipboard(nullptr))
		{
			return {};
		}

		std::wstring result;
		HANDLE data = GetClipboardData(CF_UNICODETEXT);
		if (data)
		{
			const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data));
			if (text)
			{
				result = text;
				GlobalUnlock(data);
			}
		}
		CloseClipboard();
		return result;
#else
		return {};
#endif
	}

	std::time_t GetFileLastWriteTime(const fs::directory_entry& entry)
	{
		const auto fileTime = entry.last_write_time();
		const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
			fileTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
		return std::chrono::system_clock::to_time_t(systemTime);
	}

	Compare::Compare(int sortFlags)
		: m_flags(sortFlags)
	{
	}

	bool Compare::operator()(const fs::directory_entry& a, const fs::directory_entry& b) const
	{
		const bool isADirectory = a.is_directory();
		const bool isBDirectory = b.is_directory();
		if (isADirectory != isBDirectory)
		{
			return isADirectory;
		}

		if (0 != (m_flags & FLAGS_SORT_BY_DATE))
		{
			return a.last_write_time() < b.last_write_time();
		}
		if (0 != (m_flags & FLAGS_SORT_BY_NAME))
		{
			return a.path().filename().wstring() < b.path().filename().wstring();
		}
		if (0 != (m_flags & FLAGS_SORT_BY_TYPE))
		{
			return a.path().extension().wstring() < b.path().extension().wstring();
		}
		return false;
	}
}
