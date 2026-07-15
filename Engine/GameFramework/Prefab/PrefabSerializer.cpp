#include "pch.h"
#include "PrefabSerializer.h"

#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Serialization/ObjectSerializer.h"
#include "Utillity/File/FilePath.h"

#include <fstream>
#include <sstream>
#include <string>

// 서브트리 직렬화는 Serialization(ObjectSerializer/ComponentSerializer) 계층에 전부 위임한다.
// 예전에는 여기서 임시 씬에 서브트리를 복제(CloneHierarchy)한 뒤 그 씬을 통째로 직렬화했는데,
// 그 복제가 InstanceGuid 를 안 옮기고 스크립트를 통째로 흘렸다(리플렉션에서 스크립트는 컴포넌트
// 이름 맵에 없어 조회에 걸리지 않는다). Serialization 계층은 오브젝트/컴포넌트 guid 를 보존하고
// 스크립트를 별도 분기로 처리하므로, 복제 단계를 없애는 것이 곧 그 손실을 없애는 것이다.

EPrefabSerializeResult CPrefabSerializer::SerializePrefabToText(const CGameScene& /*scene*/, const CGameObject* root, std::string& outText) const
{
	if (nullptr == root)
	{
		return EPrefabSerializeResult::InvalidArgument;
	}

	// 자식 서브트리는 Children 키에 중첩된다(씬 직렬화의 평탄 목록 + ParentIndex 와 다른 포맷).
	outText = Serialization::SerializeObject(*root);
	return outText.empty() ? EPrefabSerializeResult::ParseError : EPrefabSerializeResult::Success;
}

EPrefabSerializeResult CPrefabSerializer::DeserializePrefabFromText(CGameScene& scene, const char* text, CGameObject** outRoot) const
{
	if (nullptr == text)
	{
		return EPrefabSerializeResult::InvalidArgument;
	}

	// guid 는 파일 값 그대로 복원된다 — 단 대상 씬에 같은 guid 가 살아 있으면(원본을 남긴 채
	// 붙여넣기·같은 프리팹 2회 인스턴싱) 새로 발급된다. 삭제 undo 는 원본이 이미 사라진
	// 뒤라 충돌이 없어 guid 가 그대로 돌아온다 — 뷰포트 카메라 지목과 Ref 가 되살아난다.
	CGameObject* root = Serialization::DeserializeObject(scene, text);
	if (outRoot)
	{
		*outRoot = root;
	}
	return root ? EPrefabSerializeResult::Success : EPrefabSerializeResult::ParseError;
}

EPrefabSerializeResult CPrefabSerializer::SavePrefabToFile(const CGameScene& scene, const CGameObject* root, const File::Path& path) const
{
	if (path.empty())
	{
		return EPrefabSerializeResult::InvalidArgument;
	}

	std::string text;
	const EPrefabSerializeResult serializeResult = SerializePrefabToText(scene, root, text);
	if (EPrefabSerializeResult::Success != serializeResult)
	{
		return serializeResult;
	}

	std::ofstream file(path, std::ios::out | std::ios::trunc);
	if (false == file.is_open())
	{
		return EPrefabSerializeResult::IoError;
	}

	file << text;
	return EPrefabSerializeResult::Success;
}

EPrefabSerializeResult CPrefabSerializer::LoadPrefabFromFile(CGameScene& scene, const File::Path& path, CGameObject** outRoot) const
{
	if (path.empty())
	{
		return EPrefabSerializeResult::InvalidArgument;
	}

	std::ifstream file(path);
	if (false == file.is_open())
	{
		return EPrefabSerializeResult::IoError;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	return DeserializePrefabFromText(scene, buffer.str().c_str(), outRoot);
}
