#include "CppProjectOutput.h"
#include <JBro/Types/Table.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <Windows.h>

namespace JBro::CompilerOutput
{
    namespace
    {
        namespace fs = std::filesystem;
        constexpr std::string_view Marker = "JBro generated files v1\n";

        fs::path Path(std::string_view name)
        {
            return fs::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size()));
        }

        String Read(const fs::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            return String(std::string(std::istreambuf_iterator<char>(input), {}));
        }

        bool OwnedName(std::string_view name)
        {
            if (name == "jbro_gen_module.cpp" || name == "JBroGenerated.h")
            {
                return true;
            }
            if (!(name.starts_with("jbro_gen_source_") || name.starts_with("jbro_gen_type_")))
            {
                return false;
            }
            const std::size_t start = name.starts_with("jbro_gen_source_") ? 16 : 14;
            if (name.size() <= start + 16)
            {
                return false;
            }
            for (std::size_t index = start; index < start + 16; ++index)
            {
                const char ch = name[index];
                if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
                {
                    return false;
                }
            }
            const auto extension = name.substr(start + 16);
            return extension == ".h" || extension == ".fwd.h" || extension == ".cpp";
        }

        bool SafeTarget(const fs::path& root, const fs::path& file)
        {
            std::error_code error;
            const fs::path resolved = fs::weakly_canonical(file, error);
            return !error && resolved.parent_path() == root;
        }

        bool Replace(const fs::path& file, const String& code, String& error)
        {
            if (fs::exists(file) && Read(file) == code)
            {
                return true;
            }
            const fs::path temporary = fs::path(file.native() + L".tmp");
            if (fs::exists(temporary))
            {
                error = "temporary output already exists; preserve and inspect it before retrying";
                return false;
            }
            {
                std::ofstream output(temporary, std::ios::binary);
                output.write(code.data(), static_cast<std::streamsize>(code.size()));
                if (!output.good())
                {
                    error = "cannot write temporary project output";
                    return false;
                }
            }
            if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                error = "cannot replace project output";
                return false;
            }
            return true;
        }
    }

    Bool WriteProject(const std::filesystem::path& directory,
        const ScriptCompiler::CppProjectResult& project, String& error)
    {
        std::error_code filesystemError;
        fs::create_directories(directory, filesystemError);
        const fs::path root = fs::weakly_canonical(directory, filesystemError);
        if (filesystemError || !fs::is_directory(root))
        {
            error = "cannot create project output directory";
            return false;
        }
        const fs::path manifest = root / L"JBroGenerated.files";
        const fs::path items = root / L"JBroGenerated.items";
        if (!SafeTarget(root, manifest) || !SafeTarget(root, items))
        {
            error = "generated metadata must stay inside the output directory";
            return false;
        }
        Table<String, bool> previous;
        if (fs::exists(manifest))
        {
            const String contents = Read(manifest);
            if (!contents.View().starts_with(Marker))
            {
                error = "unrecognized generated output manifest";
                return false;
            }
            std::string_view remaining = contents.View().substr(Marker.size());
            while (!remaining.empty())
            {
                const auto newline = remaining.find('\n');
                const auto name = remaining.substr(0, newline);
                if (!OwnedName(name) || !SafeTarget(root, root / Path(name)))
                {
                    error = "invalid or escaped generated artifact in manifest";
                    return false;
                }
                previous[String(name)] = true;
                if (newline == std::string_view::npos)
                {
                    break;
                }
                remaining.remove_prefix(newline + 1);
            }
        }
        else if (fs::exists(items))
        {
            error = "existing item list has no generated ownership manifest";
            return false;
        }
        Table<String, bool> next;
        Array<String> allNames;
        Array<String> cppNames;
        for (const auto& file : project.Files)
        {
            const fs::path path = root / Path(file.FileName.View());
            if (!OwnedName(file.FileName.View()) || !SafeTarget(root, path) || next.Contains(file.FileName)
                || (fs::exists(path) && !previous.Contains(file.FileName)))
            {
                error = "generated output collides with an unowned or escaped file";
                return false;
            }
            next[file.FileName] = true;
            allNames.Add(file.FileName);
            if (path.extension() == L".cpp")
            {
                cppNames.Add(file.FileName);
            }
        }
        // Reserve every name before writing, so an interrupted run can be retried
        // without claiming any unrelated file in this directory.
        for (const auto& entry : previous)
        {
            if (!next.Contains(entry.KeyValue))
            {
                allNames.Add(entry.KeyValue);
            }
        }
        std::sort(allNames.begin(), allNames.end());
        String reserved(Marker);
        for (const String& name : allNames)
        {
            reserved += name + '\n';
        }
        if (!Replace(manifest, reserved, error))
        {
            return false;
        }
        for (const auto& file : project.Files)
        {
            if (!Replace(root / Path(file.FileName.View()), file.Code, error))
            {
                return false;
            }
        }
        for (const auto& entry : previous)
        {
            if (next.Contains(entry.KeyValue))
            {
                continue;
            }
            fs::remove(root / Path(entry.KeyValue.View()), filesystemError);
            if (filesystemError)
            {
                error = "cannot remove obsolete generated artifact";
                return false;
            }
        }
        String finalManifest(Marker);
        for (const String& name : allNames)
        {
            if (next.Contains(name))
            {
                finalManifest += name + '\n';
            }
        }
        std::sort(cppNames.begin(), cppNames.end());
        String itemList;
        for (const String& name : cppNames)
        {
            itemList += name + '\n';
        }
        return Replace(items, itemList, error) && Replace(manifest, finalManifest, error);
    }
}
