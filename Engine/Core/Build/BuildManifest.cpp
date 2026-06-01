#include "pch.h"
#include "BuildManifest.h"

#include "yaml-cpp/yaml.h"

#include <array>
#include <cstring>
#include <fstream>
#include <vector>

namespace
{
	constexpr const char* MANIFEST_RELATIVE_PATH = "Content/build_manifest.jbmanifest";
	constexpr const char* LEGACY_MANIFEST_RELATIVE_PATH = "Content/build_manifest.yaml";
	constexpr std::array<char, 8> MANIFEST_MAGIC = { 'J', 'B', 'M', 'A', 'N', '1', '\0', '\0' };
	constexpr std::uint32_t MANIFEST_VERSION = 1;
	constexpr std::uint64_t MANIFEST_CRYPT_KEY = 0xC3A5C85C97CB3127ull;
	constexpr std::uint64_t FNV_OFFSET = 14695981039346656037ull;
	constexpr std::uint64_t FNV_PRIME = 1099511628211ull;
	constexpr const char* DEFAULT_PACK_PATH = "Content/game_assets.jbpack";
	constexpr const char* DEFAULT_SCRIPT_MODULE = "GameScript.dll";

	File::Path ToCanonicalPath(const std::filesystem::path& path);

	void SetError(std::string* outError, const char* message)
	{
		if (outError)
		{
			*outError = message ? message : "";
		}
	}

	template<typename T>
	void WritePod(std::ostream& stream, const T& value)
	{
		stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
	}

	template<typename T>
	void WritePod(std::vector<std::uint8_t>& bytes, const T& value)
	{
		const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(&value);
		bytes.insert(bytes.end(), data, data + sizeof(T));
	}

	template<typename T>
	bool ReadPod(std::istream& stream, T& value)
	{
		stream.read(reinterpret_cast<char*>(&value), sizeof(T));
		return static_cast<bool>(stream);
	}

	void WriteString(std::ostream& stream, const std::string& value)
	{
		const std::uint32_t size = static_cast<std::uint32_t>(value.size());
		WritePod(stream, size);
		if (0 != size)
		{
			stream.write(value.data(), size);
		}
	}

	void WriteString(std::vector<std::uint8_t>& bytes, const std::string& value)
	{
		const std::uint32_t size = static_cast<std::uint32_t>(value.size());
		WritePod(bytes, size);
		if (0 != size)
		{
			const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(value.data());
			bytes.insert(bytes.end(), data, data + size);
		}
	}

	std::uint64_t HashBytes(const std::vector<std::uint8_t>& bytes)
	{
		std::uint64_t hash = FNV_OFFSET;
		for (std::uint8_t byte : bytes)
		{
			hash ^= byte;
			hash *= FNV_PRIME;
		}
		return hash;
	}

	void CryptBytes(std::vector<std::uint8_t>& bytes, std::uint64_t seed)
	{
		std::uint64_t state = seed ^ MANIFEST_CRYPT_KEY;
		for (std::size_t i = 0; i < bytes.size(); ++i)
		{
			state ^= state << 13;
			state ^= state >> 7;
			state ^= state << 17;
			bytes[i] ^= static_cast<std::uint8_t>((state >> ((i & 7) * 8)) & 0xFF);
		}
	}

	bool ReadBytes(const std::vector<std::uint8_t>& bytes, std::size_t& cursor, void* outValue, std::size_t size)
	{
		if (cursor > bytes.size() || size > bytes.size() - cursor)
		{
			return false;
		}
		std::memcpy(outValue, bytes.data() + cursor, size);
		cursor += size;
		return true;
	}

	template<typename T>
	bool ReadPod(const std::vector<std::uint8_t>& bytes, std::size_t& cursor, T& value)
	{
		return ReadBytes(bytes, cursor, &value, sizeof(T));
	}

	bool ReadString(const std::vector<std::uint8_t>& bytes, std::size_t& cursor, std::string& value)
	{
		std::uint32_t size = 0;
		if (false == ReadPod(bytes, cursor, size) || size > 4096 || cursor > bytes.size() || size > bytes.size() - cursor)
		{
			return false;
		}
		value.assign(reinterpret_cast<const char*>(bytes.data() + cursor), size);
		cursor += size;
		return true;
	}

	bool ReadString(std::istream& stream, std::string& value)
	{
		std::uint32_t size = 0;
		if (false == ReadPod(stream, size) || size > 4096)
		{
			return false;
		}
		value.clear();
		value.resize(size);
		if (0 != size)
		{
			stream.read(value.data(), size);
		}
		return static_cast<bool>(stream);
	}

	void ApplyRuntimeDefaults(BuildManifest& manifest)
	{
		manifest.AssetMounts.clear();
		BuildAssetMount mount;
		mount.Type = EBuildAssetMountType::Pack;
		mount.Path = File::Path(DEFAULT_PACK_PATH);
		mount.Required = true;
		manifest.AssetMounts.push_back(std::move(mount));

		manifest.ScriptMode = "DynamicLibrary";
		manifest.ScriptModule = DEFAULT_SCRIPT_MODULE;
	}

	bool LoadBinaryManifest(const File::Path& manifestPath, BuildManifest& outManifest, std::string* outError)
	{
		std::ifstream file(manifestPath, std::ios::binary);
		if (false == file.is_open())
		{
			SetError(outError, "Failed to open binary build manifest.");
			return false;
		}

		std::array<char, 8> magic = {};
		file.read(magic.data(), magic.size());
		std::uint32_t version = 0;
		if (false == static_cast<bool>(file) || magic != MANIFEST_MAGIC || false == ReadPod(file, version) || version != MANIFEST_VERSION)
		{
			SetError(outError, "Binary build manifest header is invalid.");
			return false;
		}

		std::uint32_t payloadSize = 0;
		std::uint64_t payloadHash = 0;
		if (false == ReadPod(file, payloadSize) || false == ReadPod(file, payloadHash) || payloadSize > 1024 * 1024)
		{
			SetError(outError, "Binary build manifest payload header is invalid.");
			return false;
		}

		std::vector<std::uint8_t> payload(payloadSize);
		if (payloadSize > 0)
		{
			file.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
		}
		if (false == static_cast<bool>(file) && false == file.eof())
		{
			SetError(outError, "Binary build manifest payload read failed.");
			return false;
		}
		CryptBytes(payload, payloadHash ^ version);
		if (HashBytes(payload) != payloadHash)
		{
			SetError(outError, "Binary build manifest payload hash is invalid.");
			return false;
		}

		std::size_t cursor = 0;
		outManifest = {};
		outManifest.Version = static_cast<int>(version);
		if (false == ReadPod(payload, cursor, outManifest.ResolutionWidth)
			|| false == ReadPod(payload, cursor, outManifest.ResolutionHeight)
			|| false == ReadString(payload, cursor, outManifest.StartupSceneGuid))
		{
			SetError(outError, "Binary build manifest payload is invalid.");
			return false;
		}

		const std::filesystem::path absoluteManifestPath = std::filesystem::path(ToCanonicalPath(manifestPath));
		const std::filesystem::path contentRootPath = absoluteManifestPath.parent_path();
		const std::filesystem::path packageRootPath = contentRootPath.parent_path();
		outManifest.ManifestPath = File::Path(absoluteManifestPath.generic_string());
		outManifest.ContentRootPath = File::Path(contentRootPath.generic_string());
		outManifest.PackageRootPath = File::Path(packageRootPath.generic_string());
		ApplyRuntimeDefaults(outManifest);

		if (outManifest.StartupSceneGuid.empty())
		{
			SetError(outError, "Binary build manifest has no startup scene GUID.");
			return false;
		}

		SetError(outError, "");
		return true;
	}

	File::Path ToCanonicalPath(const std::filesystem::path& path)
	{
		std::error_code errorCode;
		std::filesystem::path canonical = std::filesystem::weakly_canonical(path, errorCode);
		if (errorCode)
		{
			canonical = std::filesystem::absolute(path, errorCode);
		}
		if (errorCode)
		{
			canonical = path;
		}
		return File::Path(canonical.generic_string());
	}

	std::filesystem::path GetExecutableDirectory()
	{
#if JBRO_PLATFORM_WINDOWS
		std::wstring buffer;
		buffer.resize(MAX_PATH);
		DWORD length = 0;
		while (true)
		{
			length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (0 == length)
			{
				return {};
			}
			if (length < buffer.size() - 1)
			{
				buffer.resize(length);
				break;
			}
			buffer.resize(buffer.size() * 2);
		}
		return std::filesystem::path(buffer).parent_path();
#else
		return {};
#endif
	}

	EBuildAssetMountType ParseMountType(const std::string& value)
	{
		if (value == "Loose") return EBuildAssetMountType::Loose;
		if (value == "Pack") return EBuildAssetMountType::Pack;
		return EBuildAssetMountType::Unknown;
	}

	template<typename T>
	T ReadValueOr(const YAML::Node& node, const char* key, const T& fallback)
	{
		if (!node || !key)
		{
			return fallback;
		}

		const YAML::Node value = node[key];
		if (!value)
		{
			return fallback;
		}

		try
		{
			return value.as<T>(fallback);
		}
		catch (const YAML::Exception&)
		{
			return fallback;
		}
	}
}

bool CBuildManifestLoader::FindDefaultManifest(File::Path& outManifestPath)
{
	std::vector<std::filesystem::path> candidates;
	candidates.push_back(std::filesystem::current_path() / MANIFEST_RELATIVE_PATH);
	candidates.push_back(std::filesystem::current_path() / LEGACY_MANIFEST_RELATIVE_PATH);

	const std::filesystem::path executableDir = GetExecutableDirectory();
	if (false == executableDir.empty())
	{
		candidates.push_back(executableDir / MANIFEST_RELATIVE_PATH);
		candidates.push_back(executableDir / LEGACY_MANIFEST_RELATIVE_PATH);
	}

	for (const std::filesystem::path& candidate : candidates)
	{
		std::error_code errorCode;
		if (std::filesystem::exists(candidate, errorCode) && std::filesystem::is_regular_file(candidate, errorCode))
		{
			outManifestPath = ToCanonicalPath(candidate);
			return true;
		}
	}

	outManifestPath = File::NULL_PATH;
	return false;
}

bool CBuildManifestLoader::LoadFromFile(const File::Path& manifestPath, BuildManifest& outManifest, std::string* outError)
{
	outManifest = {};
	if (manifestPath.empty())
	{
		SetError(outError, "Manifest path is empty.");
		return false;
	}

	{
		std::ifstream probe(manifestPath, std::ios::binary);
		std::array<char, 8> magic = {};
		probe.read(magic.data(), magic.size());
		if (static_cast<bool>(probe) && magic == MANIFEST_MAGIC)
		{
			return LoadBinaryManifest(manifestPath, outManifest, outError);
		}
	}

	YAML::Node root;
	try
	{
		root = YAML::LoadFile(manifestPath.string());
	}
	catch (const YAML::Exception&)
	{
		SetError(outError, "Failed to parse build manifest.");
		return false;
	}

	if (!root || false == root.IsMap())
	{
		SetError(outError, "Build manifest root must be a map.");
		return false;
	}

	outManifest.Version = ReadValueOr<int>(root, "version", 0);
	if (outManifest.Version <= 0)
	{
		SetError(outError, "Build manifest version is invalid.");
		return false;
	}

	const std::filesystem::path absoluteManifestPath = std::filesystem::path(ToCanonicalPath(manifestPath));
	const std::filesystem::path contentRootPath = absoluteManifestPath.parent_path();
	const std::filesystem::path packageRootPath = contentRootPath.parent_path();

	outManifest.ManifestPath = File::Path(absoluteManifestPath.generic_string());
	outManifest.ContentRootPath = File::Path(contentRootPath.generic_string());
	outManifest.PackageRootPath = File::Path(packageRootPath.generic_string());
	outManifest.ProductName = ReadValueOr<std::string>(root, "productName", "");
	outManifest.TargetPlatform = ReadValueOr<std::string>(root, "targetPlatform", "");
	outManifest.Configuration = ReadValueOr<std::string>(root, "configuration", "");
	outManifest.StartupScene = ReadValueOr<std::string>(root, "startupScene", "");
	outManifest.StartupSceneGuid = ReadValueOr<std::string>(root, "startupSceneGuid", "");
	outManifest.ScriptMode = ReadValueOr<std::string>(root, "scriptMode", "");
	outManifest.ScriptModule = ReadValueOr<std::string>(root, "scriptModule", "");
	outManifest.EngineVersion = ReadValueOr<std::string>(root, "engineVersion", "");
	outManifest.BuildTimeUtc = ReadValueOr<std::string>(root, "buildTimeUtc", "");

	if (const YAML::Node scenes = root["buildScenes"]; scenes && scenes.IsSequence())
	{
		for (const YAML::Node& scene : scenes)
		{
			try
			{
				std::string value = scene.as<std::string>("");
				if (false == value.empty())
				{
					outManifest.BuildScenes.push_back(std::move(value));
				}
			}
			catch (const YAML::Exception&)
			{
			}
		}
	}

	if (const YAML::Node resolution = root["resolution"]; resolution && resolution.IsMap())
	{
		outManifest.ResolutionWidth = ReadValueOr<int>(resolution, "width", 0);
		outManifest.ResolutionHeight = ReadValueOr<int>(resolution, "height", 0);
	}

	if (const YAML::Node mounts = root["assetMounts"]; mounts && mounts.IsSequence())
	{
		for (const YAML::Node& mountNode : mounts)
		{
			if (!mountNode || false == mountNode.IsMap())
			{
				continue;
			}

			BuildAssetMount mount;
			mount.Type = ParseMountType(ReadValueOr<std::string>(mountNode, "type", ""));
			mount.Path = File::Path(ReadValueOr<std::string>(mountNode, "path", ""));
			mount.Required = ReadValueOr<bool>(mountNode, "required", true);
			if (EBuildAssetMountType::Unknown != mount.Type && false == mount.Path.empty())
			{
				outManifest.AssetMounts.push_back(std::move(mount));
			}
		}
	}

	if (outManifest.StartupScene.empty())
	{
		SetError(outError, "Build manifest has no startup scene.");
		return false;
	}
	if (outManifest.AssetMounts.empty())
	{
		SetError(outError, "Build manifest has no asset mounts.");
		return false;
	}

	SetError(outError, "");
	return true;
}

bool CBuildManifestLoader::WriteBinaryFile(const File::Path& manifestPath, const BuildManifest& manifest, std::string* outError)
{
	if (manifestPath.empty())
	{
		SetError(outError, "Manifest path is empty.");
		return false;
	}

	std::filesystem::path path(manifestPath);
	if (path.has_parent_path())
	{
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec)
		{
			SetError(outError, "Failed to create manifest directory.");
			return false;
		}
	}

	std::ofstream file(manifestPath, std::ios::binary | std::ios::trunc);
	if (false == file.is_open())
	{
		SetError(outError, "Failed to write binary build manifest.");
		return false;
	}

	std::vector<std::uint8_t> payload;
	const int width = manifest.ResolutionWidth > 0 ? manifest.ResolutionWidth : 1280;
	const int height = manifest.ResolutionHeight > 0 ? manifest.ResolutionHeight : 720;
	WritePod(payload, width);
	WritePod(payload, height);
	WriteString(payload, manifest.StartupSceneGuid);

	const std::uint32_t payloadSize = static_cast<std::uint32_t>(payload.size());
	const std::uint64_t payloadHash = HashBytes(payload);
	CryptBytes(payload, payloadHash ^ MANIFEST_VERSION);

	file.write(MANIFEST_MAGIC.data(), MANIFEST_MAGIC.size());
	WritePod(file, MANIFEST_VERSION);
	WritePod(file, payloadSize);
	WritePod(file, payloadHash);
	if (false == payload.empty())
	{
		file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
	}

	if (false == static_cast<bool>(file))
	{
		SetError(outError, "Failed to finish binary build manifest.");
		return false;
	}

	SetError(outError, "");
	return true;
}

File::Path CBuildManifestLoader::ResolvePackagePath(const BuildManifest& manifest, const File::Path& path)
{
	if (path.empty())
	{
		return File::NULL_PATH;
	}

	std::filesystem::path resolvedPath(path);
	if (false == resolvedPath.is_absolute())
	{
		resolvedPath = std::filesystem::path(manifest.PackageRootPath) / resolvedPath;
	}

	return ToCanonicalPath(resolvedPath);
}
