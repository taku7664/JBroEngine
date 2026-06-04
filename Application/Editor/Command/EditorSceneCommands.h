#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Command/EditorCommandManager.h"
#include "Engine/GameFramework/Component/Transform2D.h"
#include "Engine/GameFramework/Reflection/ReflectionTypes.h"
#include "Utillity/File/FilePath.h" // File::Guid (오브젝트 안정 식별자 = InstanceGuid)

#include <vector>

class CScene;
class CGameObject;

// 명령은 오브젝트를 InstanceGuid 로 보관한다(포인터/정수 id 아님).
// 파괴→undo→redo 로 오브젝트가 재생성돼 주소가 바뀌어도 guid 로 안전하게 재해석된다.

class CAddComponentCommand final : public IEditorCommand
{
public:
	CAddComponentCommand(SafePtr<CScene> scene, CGameObject* object, TypeId componentTypeId);
	~CAddComponentCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	SafePtr<CScene> m_scene;
	File::Guid m_objectGuid;
	TypeId m_componentTypeId = INVALID_TYPE_ID;
	bool m_added = false;
};

class CAddScriptComponentCommand final : public IEditorCommand
{
public:
	CAddScriptComponentCommand(SafePtr<CScene> scene, CGameObject* object, TypeId scriptTypeId);
	~CAddScriptComponentCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	SafePtr<CScene> m_scene;
	File::Guid m_objectGuid;
	TypeId m_scriptTypeId = INVALID_TYPE_ID;
	TypeId m_scriptComponentTypeId = INVALID_TYPE_ID;
	void* m_addedComponent = nullptr;
	bool m_added = false;
};

class CSetScriptTypeCommand final : public IEditorCommand
{
public:
	CSetScriptTypeCommand(SafePtr<CScene> scene, CGameObject* object, std::size_t instanceIndex, TypeId newScriptTypeId);
	~CSetScriptTypeCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	bool Apply(TypeId scriptTypeId);

private:
	SafePtr<CScene> m_scene;
	File::Guid m_objectGuid;
	std::size_t m_instanceIndex = 0;
	TypeId m_oldScriptTypeId = INVALID_TYPE_ID;
	TypeId m_newScriptTypeId = INVALID_TYPE_ID;
	TypeId m_scriptComponentTypeId = INVALID_TYPE_ID;
	bool m_executed = false;
};

class CCreateGameObjectCommand final : public IEditorCommand
{
public:
	// parent == nullptr 이면 루트에 생성, 그 외에는 parent 의 자식으로 생성.
	CCreateGameObjectCommand(SafePtr<CScene> scene, const char* name,
	                         CGameObject* parent = nullptr);
	~CCreateGameObjectCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
	// 생성된 오브젝트(없으면 nullptr). 호출자가 선택 처리에 사용.
	CGameObject* GetEntity() const;

private:
	SafePtr<CScene> m_scene;
	std::string m_name;
	File::Guid m_parentGuid; // null = 루트
	File::Guid m_objectGuid; // 생성된 오브젝트의 안정 식별자(redo 시 동일 guid 강제)
	bool     m_created = false;
};

class CSetComponentPropertyCommand final : public IEditorCommand
{
public:
	CSetComponentPropertyCommand(
		SafePtr<CScene> scene,
		CGameObject* object,
		TypeId componentTypeId,
		std::size_t propertyOffset,
		std::vector<std::uint8_t> oldValue,
		std::vector<std::uint8_t> newValue,
		std::size_t instanceIndex = 0);
	~CSetComponentPropertyCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
	// 같은 (오브젝트·컴포넌트·프로퍼티·인스턴스) 편집이면 newer 의 새 값만 흡수(드래그 묶기).
	bool TryMerge(const IEditorCommand& newer) override;

private:
	bool WriteValue(const std::vector<std::uint8_t>& value);

private:
	SafePtr<CScene> m_scene;
	File::Guid m_objectGuid;
	TypeId m_componentTypeId = INVALID_TYPE_ID;
	std::size_t m_propertyOffset = 0;
	std::size_t m_instanceIndex = 0;
	std::vector<std::uint8_t> m_oldValue;
	std::vector<std::uint8_t> m_newValue;
};

// 오브젝트 Transform(Local) 편집. Transform 은 컴포넌트가 아니라 CGameObject 멤버라
// 컴포넌트 프로퍼티 커맨드 경로를 못 탄다 → 전용 커맨드. 드래그는 TryMerge 로 묶인다.
class CSetObjectTransformCommand final : public IEditorCommand
{
public:
	CSetObjectTransformCommand(SafePtr<CScene> scene, CGameObject* object,
	                           const Transform2D& oldTransform, const Transform2D& newTransform);
	~CSetObjectTransformCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
	bool TryMerge(const IEditorCommand& newer) override;

private:
	bool Apply(const Transform2D& transform);

	SafePtr<CScene> m_scene;
	File::Guid  m_objectGuid;
	Transform2D m_old;
	Transform2D m_new;
};

// 오브젝트(+서브트리) 삭제. Undo 는 직렬화 스냅샷(프리팹 텍스트)으로 복원.
class CDeleteGameObjectCommand final : public IEditorCommand
{
public:
	CDeleteGameObjectCommand(SafePtr<CScene> scene, CGameObject* object);
	~CDeleteGameObjectCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	SafePtr<CScene> m_scene;
	File::Guid  m_objectGuid;  // 삭제 대상(redo 재삭제·undo 후 재해석 키)
	File::Guid  m_parentGuid;  // 복원 시 재부모(null = 루트)
	std::string m_snapshot;    // 서브트리 직렬화(undo 복원용)
	bool        m_deleted = false;
};

// 컴포넌트 1개 제거. Undo 는 직렬화 스냅샷으로 재부착+값 복원.
class CRemoveComponentCommand final : public IEditorCommand
{
public:
	CRemoveComponentCommand(SafePtr<CScene> scene, CGameObject* object, TypeId componentTypeId);
	~CRemoveComponentCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	bool RemoveNow();

	SafePtr<CScene> m_scene;
	File::Guid  m_objectGuid;
	TypeId      m_componentTypeId = INVALID_TYPE_ID;
	std::string m_snapshot;     // 제거 전 컴포넌트 직렬화(undo 복원용)
	bool        m_removed = false;
};

// SetParent 와 WorldStay(로컬 Transform 자동 보정)를 함께 처리하는 커맨드.
// Undo: 이전 부모 관계 + 이전 로컬 Transform 복원.
// Redo: 새 부모 관계 + 계산된 새 로컬 Transform 재적용.
class CSetParentCommand final : public IEditorCommand
{
public:
	// newParent = nullptr 이면 부모 해제(루트로 이동).
	CSetParentCommand(SafePtr<CScene> scene, CGameObject* child, CGameObject* newParent);
	~CSetParentCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	SafePtr<CScene> m_scene;
	File::Guid      m_childGuid;
	File::Guid      m_oldParentGuid; // null = 루트
	File::Guid      m_newParentGuid; // null = 루트
	Transform2D     m_oldLocalTransform;
	Transform2D     m_newLocalTransform;
	bool            m_executed       = false;
};

// PolygonCollider2D 의 버텍스 목록을 통째로 교체하는 커맨드.
// Execute / Redo: newPoints 를 LocalPoints 에 적용 + dirty 캐시 마킹.
// Undo         : 이전 LocalPoints 복원 + dirty 캐시 마킹.
class CModifyPolygonVerticesCommand final : public IEditorCommand
{
public:
	CModifyPolygonVerticesCommand(
		SafePtr<CScene>                   scene,
		CGameObject*                      object,
		std::vector<Vector2>       newPoints);
	~CModifyPolygonVerticesCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo()    override;
	void Redo()    override;

private:
	bool Apply(const std::vector<Vector2>& points);

private:
	SafePtr<CScene>               m_scene;
	File::Guid                    m_objectGuid;
	std::vector<Vector2>   m_oldPoints;
	std::vector<Vector2>   m_newPoints;
	bool                          m_executed = false;
};

#endif
