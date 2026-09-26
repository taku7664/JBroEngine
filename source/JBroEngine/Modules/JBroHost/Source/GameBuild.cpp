#include <JBro/Host/GameBuild.h>

#include <JBro/Asset/AssetRegistry.h>
#include <JBro/Package/PackageCollect.h>
#include <JBro/Package/PackageCook.h>
#include <JBro/Package/PackageWriter.h>
#include <JBro/Types/Uuid.h>

namespace JBro
{
    namespace
    {
        String Slashed(String path)
        {
            for (char& value : path)
            {
                if (value == '\\')
                {
                    value = '/';
                }
            }
            return path;
        }

        String Join(const String& folder, const char* name)
        {
            String path = folder;
            if (false == path.empty() && path.back() != '/')
            {
                path.push_back('/');
            }
            path.append(name);
            return path;
        }

        bool CopyWholeFile(IPlatform& platform, const String& from, const String& to)
        {
            Array<std::byte> bytes;
            if (false == platform.ReadWholeFile(from.c_str(), bytes) || bytes.Size() > 0xFFFFFFFFull)
            {
                return false;
            }
            JArrayView<std::byte> view;
            view.data = bytes.Data();
            view.size = static_cast<std::uint32_t>(bytes.Size());
            return platform.WriteWholeFile(to.c_str(), view);
        }

        bool Fail(GameBuildReport& report, const char* why)
        {
            report.error = why;
            return false;
        }
    }

    String ToAssetRelativePath(const ProjectFile& project, const char* projectFilePath, const String& projectRelative)
    {
        if (projectRelative.empty())
        {
            return String();
        }
        String root = Slashed(ResolveProjectRelativePath(project.assetDirectory.c_str(), projectFilePath));
        if (false == root.empty() && root.back() != '/')
        {
            root.push_back('/');
        }
        const String absolute = Slashed(ResolveProjectRelativePath(projectRelative.c_str(), projectFilePath));
        if (root.empty() || absolute.size() <= root.size() || absolute.compare(0, root.size(), root) != 0)
        {
            return String();
        }
        return String(absolute.c_str() + root.size());
    }

    bool BuildGame(IPlatform& platform, const ProjectFile& project, const char* projectFilePath, const GameBuildOptions& options,
        GameBuildReport& report)
    {
        report = {};
        const String assetRoot = ResolveProjectRelativePath(project.assetDirectory.c_str(), projectFilePath);
        AssetRegistry registry;
        AssetScanOptions scan;
        scan.ignorePatterns.data = project.assetIgnorePatterns.Data();
        scan.ignorePatterns.size = static_cast<std::uint32_t>(project.assetIgnorePatterns.Size());
        AssetScanReport scanned;
        if (false == registry.Scan(platform, assetRoot.c_str(), scan, scanned))
        {
            return Fail(report, "the asset folder could not be read");
        }

        // 씨: 시작 캔버스·빌드 캔버스(경로)·프로젝트 폰트·모든 문자열 표(키는 실행 중에 찾는다).
        ProjectFile exported = project;
        exported.build.startupCanvas = ToAssetRelativePath(project, projectFilePath, project.build.startupCanvas);
        if (false == project.build.startupCanvas.empty() && exported.build.startupCanvas.empty())
        {
            return Fail(report, "the startup canvas is not inside the asset folder");
        }
        exported.build.buildCanvases.Clear();
        Array<AssetId> seeds;
        const auto seedCanvas = [&](const String& assetRelative) {
            const AssetRecord* record = registry.FindByPath(assetRelative);
            if (record == nullptr)
            {
                String line("the build settings name a canvas that is not in the project: ");
                line.append(assetRelative);
                report.warnings.Add(std::move(line));
                return;
            }
            seeds.Add(record->id);
        };
        if (false == exported.build.startupCanvas.empty())
        {
            seedCanvas(exported.build.startupCanvas);
        }
        for (const String& canvas : project.build.buildCanvases)
        {
            const String relative = ToAssetRelativePath(project, projectFilePath, canvas);
            if (relative.empty())
            {
                String line("a build canvas is not inside the asset folder: ");
                line.append(canvas);
                report.warnings.Add(std::move(line));
                continue;
            }
            exported.build.buildCanvases.Add(relative);
            seedCanvas(relative);
        }
        for (const AssetId& font : project.fonts)
        {
            seeds.Add(font);
        }
        for (std::size_t index = 0; index < registry.GetCount(); ++index)
        {
            if (registry.GetRecord(index).type == AssetType::StringTable)
            {
                seeds.Add(registry.GetRecord(index).id);
            }
        }

        Array<AssetId> included;
        Package::CollectReport collected;
        Package::CollectAssets(platform, registry, assetRoot.c_str(), ArrayView<const AssetId>(seeds.Data(), seeds.Size()), included, collected);
        for (String& warning : collected.warnings)
        {
            report.warnings.Add(std::move(warning));
        }
        // 키는 빌드마다 뽑는다(D-232 §2.3). 0 은 피한다 - 섞지 않은 것과 헷갈린다.
        const Uuid random = Uuid::Generate();
        const std::uint64_t key = (random.high ^ random.low) | 1ull;
        Package::PackageWriter writer(key);
        Package::CookReport cooked;
        if (false == Package::CookAssets(platform, registry, assetRoot.c_str(), ArrayView<const AssetId>(included.Data(), included.Size()),
                writer, cooked))
        {
            report.error = "an asset could not be cooked: ";
            report.error.append(cooked.failures.IsEmpty() ? String() : cooked.failures[0]);
            return false;
        }
        report.assets = cooked.assets;
        report.cookedTextures = cooked.cookedTextures;

        const String productName = project.build.productName.empty() ? String("Game") : project.build.productName;
        const String output = Join(Slashed(ResolveProjectRelativePath(project.build.outputDirectory.c_str(), projectFilePath)),
            productName.c_str());
        report.outputFolder = output;
        const String content = Join(output, "Content");
        if (false == platform.CreateDirectoryAt(content.c_str()))
        {
            return Fail(report, "the output folder could not be made");
        }
        Array<std::byte> packageBytes;
        writer.Build(packageBytes);
        report.packageBytes = packageBytes.Size();
        JArrayView<std::byte> packageView;
        packageView.data = packageBytes.Data();
        packageView.size = static_cast<std::uint32_t>(packageBytes.Size());
        if (packageBytes.Size() > 0xFFFFFFFFull || false == platform.WriteWholeFile(Join(content, "game.jpak").c_str(), packageView))
        {
            return Fail(report, "the package could not be written");
        }

        // 스크립트 DLL 은 있을 때만 싸 간다(스크립트 없는 게임도 있다, D-98).
        const String script = ResolveScriptModulePath(project, projectFilePath);
        exported.scriptOutputLibraryPath.clear();
        if (false == script.empty() && platform.FileExists(script.c_str()))
        {
            const String name = project.build.scriptOutputLibraryPath.empty() ? String("GameScript.dll") : project.build.scriptOutputLibraryPath;
            if (false == CopyWholeFile(platform, script, Join(output, name.c_str())))
            {
                return Fail(report, "the script library could not be copied");
            }
            exported.scriptOutputLibraryPath = name;
        }
        if (false == options.gameHostPath.empty())
        {
            String exe(productName);
            exe.append(".exe");
            if (false == CopyWholeFile(platform, options.gameHostPath, Join(output, exe.c_str())))
            {
                return Fail(report, "the game host could not be copied");
            }
        }

        // 프로젝트 사본: 패키지를 가리키고, 편집기의 자리 기억은 버린다.
        exported.assetPackage = "Content/game.jpak";
        exported.lastOpenedCanvasPath.clear();
        exported.canvasViewCameraX = 0.0f;
        exported.canvasViewCameraY = 0.0f;
        exported.canvasViewCameraSize = 0.0f;
        if (options.physicsWorkers >= 0)
        {
            exported.build.physicsThreadMode = options.physicsWorkers == 0 ? PhysicsThreadMode::Single : PhysicsThreadMode::Workers;
            exported.build.physicsWorkers = static_cast<std::uint32_t>(options.physicsWorkers);
        }
        String projectName(productName);
        projectName.append(".jproject");
        const String exportedPath = Join(output, projectName.c_str());
        // 새로 적는다(빈 원문에서). 전 빌드의 사본이나 원본을 고쳐 쓰면 편집기만 아는 키와 모르는 키가 따라간다.
        // `BuildCanvases` 는 적히지 않는다 - 게임은 그것을 물리 워커 수를 셀 때만 썼고, 그 수는 위에서 정해 적었다.
        ProjectFileError projectError;
        String projectText;
        if (false == WriteProjectFileText(exported, "", 0, projectText, projectError))
        {
            report.error = "the project copy could not be written: ";
            report.error.append(projectError.message);
            return false;
        }
        JArrayView<std::byte> projectView;
        projectView.data = reinterpret_cast<const std::byte*>(projectText.data());
        projectView.size = static_cast<std::uint32_t>(projectText.size());
        if (false == platform.WriteWholeFile(exportedPath.c_str(), projectView))
        {
            return Fail(report, "the project copy could not be written");
        }
        return true;
    }
}
