#pragma once

#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/GameObject.h"
#include "GameFramework/Scene/Scene.h"

// CGameObject is a lightweight handle over an EntityId. The actual object data
// is stored as GameObject/Transform2D components inside CScene-owned pools.
// Do not persist this handle across live-compile DLL reload boundaries.
class CGameObject final
{
public:
	CGameObject() = default;
	CGameObject(CScene& scene, EntityId entity);

public:
	bool IsValid() const;
	void Destroy();

	EntityId GetEntityId() const;
	CScene* GetScene() const;

	const char* GetName() const;
	void SetName(const char* name);

	bool IsActive() const;
	void SetActive(bool isActive);

	std::uint32_t GetLayer() const;
	void SetLayer(std::uint32_t layer);

	Transform2D* GetTransform();
	const Transform2D* GetTransform() const;

	bool SetParent(const CGameObject& parent);
	bool ClearParent();
	EntityId GetParent() const;
	EntityId GetFirstChild() const;
	EntityId GetNextSibling() const;
	EntityId GetPrevSibling() const;

	explicit operator bool() const;

	template<typename T, typename... Args>
	T* AddComponent(Args&&... args)
	{
		if (false == IsValid())
		{
			return nullptr;
		}

		return m_scene->AddComponent<T>(m_entity, std::forward<Args>(args)...);
	}

	// Always creates a new instance (use for AllowDuplicates component types).
	template<typename T, typename... Args>
	T* AddNewComponent(Args&&... args)
	{
		if (false == IsValid())
		{
			return nullptr;
		}

		return m_scene->AddNewComponent<T>(m_entity, std::forward<Args>(args)...);
	}

	template<typename T>
	void RemoveComponent()
	{
		if (IsValid())
		{
			m_scene->RemoveComponent<T>(m_entity);
		}
	}

	template<typename T>
	T* GetComponent()
	{
		if (false == IsValid())
		{
			return nullptr;
		}

		return m_scene->GetComponent<T>(m_entity);
	}

	template<typename T>
	const T* GetComponent() const
	{
		if (false == IsValid())
		{
			return nullptr;
		}

		return m_scene->GetComponent<T>(m_entity);
	}

	template<typename T>
	bool HasComponent() const
	{
		return nullptr != GetComponent<T>();
	}

private:
	CScene* m_scene = nullptr;
	EntityId m_entity = INVALID_ENTITY_ID;
};
