#include "pch.h"
#include "GameFramework/Serialization/ObjectSerializer.h"

#include "GameFramework/Serialization/ComponentSerializer.h"
#include "GameFramework/Component/Component.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Scene/Scene.h"
#include "yaml-cpp/yaml.h"

namespace Serialization
{
namespace
{
	// Transform 은 컴포넌트가 아니라 오브젝트 멤버 → 여기서 직접 직렬화한다(소량).
	YAML::Node WriteTransform2D(const Transform2D& t)
	{
		YAML::Node pos(YAML::NodeType::Sequence); pos.push_back(t.Position.x); pos.push_back(t.Position.y);
		YAML::Node scl(YAML::NodeType::Sequence); scl.push_back(t.Scale.x);    scl.push_back(t.Scale.y);
		YAML::Node node(YAML::NodeType::Map);
		node["Position"]        = pos;
		node["RotationRadians"] = t.RotationRadians.Value;
		node["Scale"]           = scl;
		return node;
	}

	void ReadTransform2D(const YAML::Node& node, Transform2D& t)
	{
		if (!node || false == node.IsMap()) return;
		if (const YAML::Node p = node["Position"]; p && p.IsSequence() && p.size() >= 2)
		{
			t.Position.x = p[0].as<float>(t.Position.x);
			t.Position.y = p[1].as<float>(t.Position.y);
		}
		t.RotationRadians = node["RotationRadians"].as<float>(t.RotationRadians);
		if (const YAML::Node s = node["Scale"]; s && s.IsSequence() && s.size() >= 2)
		{
			t.Scale.x = s[0].as<float>(t.Scale.x);
			t.Scale.y = s[1].as<float>(t.Scale.y);
		}
	}
}

YAML::Node WriteObject(const CGameObject& object, std::vector<AssetGuid>* referencedAssets,
                       bool includeChildren)
{
	CGameObject& obj = const_cast<CGameObject&>(object);

	YAML::Node node(YAML::NodeType::Map);
	node["Name"]   = obj.Name;
	if (false == obj.InstanceGuid.IsNull())
	{
		node["InstanceGuid"] = obj.InstanceGuid.generic_string();
	}
	node["Active"]      = obj.IsActive;
	if (false == obj.Tag.empty())
	{
		node["Tag"] = obj.Tag;
	}
	if (false == obj.Flags.Empty())
	{
		node["Flags"] = obj.Flags.Get();
	}
	node["Transform2D"] = WriteTransform2D(obj.Local);

	YAML::Node components(YAML::NodeType::Sequence);
	for (const SafePtr<CComponent>& cref : obj.GetComponents())
	{
		CComponent* c = cref.TryGet();
		if (nullptr == c)
		{
			continue;
		}
		YAML::Node cn = WriteComponent(*c, referencedAssets);
		if (cn.IsDefined())
		{
			components.push_back(cn);
		}
	}
	node["Components"] = components;

	if (includeChildren)
	{
		YAML::Node children(YAML::NodeType::Sequence);
		for (const SafePtr<CGameObject>& childRef : obj.GetChildren())
		{
			if (CGameObject* child = childRef.TryGet())
			{
				children.push_back(WriteObject(*child, referencedAssets, true));
			}
		}
		if (children.size() > 0)
		{
			node["Children"] = children;
		}
	}
	return node;
}

CGameObject* ReadObjectInto(CScene& scene, const YAML::Node& node,
                            std::vector<AssetGuid>* referencedAssets)
{
	if (!node || false == node.IsMap())
	{
		return nullptr;
	}

	const std::string name = node["Name"] ? node["Name"].as<std::string>("GameObject") : "GameObject";
	CGameObject* object = scene.CreateGameObject(name.c_str());
	if (nullptr == object)
	{
		return nullptr;
	}

	object->SetActive(node["Active"] ? node["Active"].as<bool>(true) : true);
	if (const YAML::Node t = node["Tag"]; t)
	{
		object->Tag = t.as<std::string>("");
	}
	if (const YAML::Node f = node["Flags"]; f)
	{
		object->Flags.Set(f.as<unsigned int>(0u));
	}
	if (const YAML::Node g = node["InstanceGuid"]; g)
	{
		const std::string guid = g.as<std::string>("");
		if (false == guid.empty())
		{
			object->InstanceGuid = File::Guid(guid);
		}
	}

	if (const YAML::Node transformNode = node["Transform2D"])
	{
		ReadTransform2D(transformNode, object->GetTransform());
	}

	if (const YAML::Node components = node["Components"]; components && components.IsSequence())
	{
		for (const YAML::Node& cn : components)
		{
			ReadComponentInto(*object, cn, referencedAssets);
		}
	}

	// 자식 서브트리(복사 붙여넣기) — 각 자식을 만들어 이 오브젝트의 자식으로 연결한다.
	if (const YAML::Node children = node["Children"]; children && children.IsSequence())
	{
		for (const YAML::Node& childNode : children)
		{
			if (CGameObject* child = ReadObjectInto(scene, childNode, referencedAssets))
			{
				child->SetParent(*object);
			}
		}
	}
	return object;
}

std::string SerializeObject(const CGameObject& object)
{
	// 복사는 자식 서브트리까지 포함한다.
	const YAML::Node node = WriteObject(object, nullptr, /*includeChildren*/ true);
	YAML::Emitter emitter;
	emitter << node;
	return std::string(emitter.c_str());
}

CGameObject* DeserializeObject(CScene& scene, const char* text)
{
	if (nullptr == text)
	{
		return nullptr;
	}
	YAML::Node node;
	try { node = YAML::Load(text); }
	catch (const YAML::Exception&) { return nullptr; }
	return ReadObjectInto(scene, node, nullptr);
}

bool LooksLikeObject(const char* text)
{
	if (nullptr == text)
	{
		return false;
	}
	YAML::Node node;
	try { node = YAML::Load(text); }
	catch (const YAML::Exception&) { return false; }
	// 오브젝트: Components 키(컴포넌트 시퀀스) 보유. 단일 컴포넌트엔 없다.
	return node.IsMap() && node["Components"];
}

} // namespace Serialization
