#include "pch.h"
#include "AssetTypeRules.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string_view>

namespace
{
	struct ExtensionList
	{
		const std::string_view* Values = nullptr;
		std::size_t Count = 0;
	};

	struct AssetTypeRule
	{
		EAssetType Type = EAssetType::Unknown;
		std::string_view Name;
		ExtensionList Extensions;
	};

	template <std::size_t Count>
	constexpr ExtensionList MakeExtensionList(const std::string_view (&values)[Count])
	{
		return { values, Count };
	}

	constexpr std::string_view SPRITE_EXTENSIONS[]      = { ".png", ".jpg", ".jpeg", ".bmp", ".tga" };
	constexpr std::string_view MESH_EXTENSIONS[]        = { ".obj", ".fbx", ".gltf", ".glb" };
	constexpr std::string_view MATERIAL_EXTENSIONS[]    = { ".jmat" };
	constexpr std::string_view SHADER_EXTENSIONS[]      = { ".hlsl", ".wgsl", ".glsl", ".shader" };
	constexpr std::string_view SCENE_EXTENSIONS[]       = { ".jcanvas" };
	constexpr std::string_view PREFAB_EXTENSIONS[]      = { ".jprefab" };
	constexpr std::string_view SCRIPT_EXTENSIONS[]      = { ".cpp", ".h", ".hpp" };
	constexpr std::string_view AUDIO_EXTENSIONS[]       = { ".wav", ".mp3", ".flac", ".ogg" };
	constexpr std::string_view AUDIO_EFFECT_EXTENSIONS[] = { ".jfx" };
	constexpr std::string_view FONT_FACE_EXTENSIONS[]   = { ".ttf", ".otf" };
	constexpr std::string_view FONT_FAMILY_EXTENSIONS[] = { ".jfontfamily" };
	constexpr std::string_view LAYER_EXTENSIONS[]      = { ".jlayer" };

	constexpr AssetTypeRule RULES[] = {
		{ EAssetType::Sprite,     "Sprite",      MakeExtensionList(SPRITE_EXTENSIONS) },
		{ EAssetType::Mesh,       "Mesh",        MakeExtensionList(MESH_EXTENSIONS) },
		{ EAssetType::Material,   "Material",    MakeExtensionList(MATERIAL_EXTENSIONS) },
		{ EAssetType::Shader,     "Shader",      MakeExtensionList(SHADER_EXTENSIONS) },
		{ EAssetType::Scene,      "Scene",       MakeExtensionList(SCENE_EXTENSIONS) },
		{ EAssetType::Prefab,     "Prefab",      MakeExtensionList(PREFAB_EXTENSIONS) },
		{ EAssetType::Script,     "Script",      MakeExtensionList(SCRIPT_EXTENSIONS) },
		{ EAssetType::Audio,      "Audio",       MakeExtensionList(AUDIO_EXTENSIONS) },
		{ EAssetType::AudioEffect,"AudioEffect", MakeExtensionList(AUDIO_EFFECT_EXTENSIONS) },
		{ EAssetType::FontFace,   "FontFace",    MakeExtensionList(FONT_FACE_EXTENSIONS) },
		{ EAssetType::FontFamily, "FontFamily",  MakeExtensionList(FONT_FAMILY_EXTENSIONS) },
		{ EAssetType::Layer,      "Layer",       MakeExtensionList(LAYER_EXTENSIONS) },
	};

	std::string NormalizeExtension(const File::Path& path)
	{
		std::string extension = path.extension().generic_string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		return extension;
	}

	const AssetTypeRule* FindRule(EAssetType type)
	{
		for (const AssetTypeRule& rule : RULES)
		{
			if (rule.Type == type)
			{
				return &rule;
			}
		}
		return nullptr;
	}

	bool HasExtension(const AssetTypeRule& rule, std::string_view extension)
	{
		for (std::size_t i = 0; i < rule.Extensions.Count; ++i)
		{
			const std::string_view allowed = rule.Extensions.Values[i];
			if (allowed == extension)
			{
				return true;
			}
		}
		return false;
	}

	std::string NormalizeTypeName(std::string value)
	{
		if (value.rfind("class ", 0) == 0)
		{
			value.erase(0, 6);
		}
		if (value.rfind("struct ", 0) == 0)
		{
			value.erase(0, 7);
		}
		const std::size_t namespacePos = value.find_last_of(':');
		if (namespacePos != std::string::npos)
		{
			value.erase(0, namespacePos + 1);
		}
		if (value.size() > 1 && value.front() == 'C' && std::isupper(static_cast<unsigned char>(value[1])))
		{
			value.erase(value.begin());
		}
		constexpr std::string_view ASSET_SUFFIX = "Asset";
		if (value.size() > ASSET_SUFFIX.size()
			&& value.compare(value.size() - ASSET_SUFFIX.size(), ASSET_SUFFIX.size(), ASSET_SUFFIX) == 0)
		{
			value.resize(value.size() - ASSET_SUFFIX.size());
		}
		return value;
	}
}

EAssetType CAssetTypeRules::DetectTypeFromPath(const File::Path& path)
{
	const std::string extension = NormalizeExtension(path);
	if (extension.empty())
	{
		return EAssetType::Unknown;
	}

	for (const AssetTypeRule& rule : RULES)
	{
		if (HasExtension(rule, extension))
		{
			return rule.Type;
		}
	}

	return EAssetType::Custom;
}

EAssetType CAssetTypeRules::ResolveType(EAssetType declaredType, const File::Path& path)
{
	if (EAssetType::Unknown != declaredType)
	{
		return declaredType;
	}
	return DetectTypeFromPath(path);
}

bool CAssetTypeRules::IsAssignableTo(EAssetType expectedType, EAssetType declaredType, const File::Path& path)
{
	if (EAssetType::Unknown == expectedType)
	{
		return true;
	}
	return ResolveType(declaredType, path) == expectedType;
}

bool CAssetTypeRules::IsExtensionAllowed(EAssetType type, const File::Path& path)
{
	if (EAssetType::Unknown == type)
	{
		return true;
	}

	const AssetTypeRule* rule = FindRule(type);
	if (nullptr == rule)
	{
		return EAssetType::Custom == type;
	}

	const std::string extension = NormalizeExtension(path);
	return false == extension.empty() && HasExtension(*rule, extension);
}

std::string CAssetTypeRules::GetAllowedExtensionsText(EAssetType type)
{
	const AssetTypeRule* rule = FindRule(type);
	if (nullptr == rule)
	{
		return {};
	}

	std::string result;
	for (std::size_t i = 0; i < rule->Extensions.Count; ++i)
	{
		if (false == result.empty())
		{
			result += ", ";
		}
		const std::string_view extension = rule->Extensions.Values[i];
		result += extension;
	}
	return result;
}

EAssetType CAssetTypeRules::ParseTypeName(const std::string& name)
{
	const std::string normalized = NormalizeTypeName(name);
	if (normalized == "Any" || normalized == "Unknown")
	{
		return EAssetType::Unknown;
	}
	for (const AssetTypeRule& rule : RULES)
	{
		if (rule.Name == normalized)
		{
			return rule.Type;
		}
	}
	if (normalized == "Custom")
	{
		return EAssetType::Custom;
	}
	// `Ref<CCanvasAsset>` 이 여기로 온다 — NormalizeTypeName 이 C 접두와 Asset 접미를 떼어
	// "Canvas" 가 되는데, 규칙 이름은 아직 "Scene" 이다. 캔버스-레이어 개편의 코어 리네임
	// (Type: Scene → Canvas)이 끝나면 이 별칭은 지워도 된다.
	if (normalized == "Canvas")
	{
		return EAssetType::Scene;
	}
	return EAssetType::Unknown;
}

const char* CAssetTypeRules::GetTypeName(EAssetType type)
{
	if (EAssetType::Unknown == type)
	{
		return "Any";
	}
	if (EAssetType::Custom == type)
	{
		return "Custom";
	}

	const AssetTypeRule* rule = FindRule(type);
	return nullptr != rule ? rule->Name.data() : "Unknown";
}
