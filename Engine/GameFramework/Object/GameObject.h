#pragma once

#include "GameFramework/Component/Component.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/WorldTransform2D.h"
#include "Utillity/File/FilePath.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <string>
#include <vector>

class CScene;

// ─────────────────────────────────────────────────────────────────────────────
//  CGameObject — 씬의 실체 객체(다형성 컴포넌트 구조).
//
//  · 고정 크기 → CScene 의 TObjectPool<CGameObject> 에 거주(메모리 소유=풀).
//  · Transform(Local) / WorldTransform(World) / 계층(Parent·Children)을 멤버로 직접
//    보유한다 — 컴포넌트가 아니다.
//  · 컴포넌트는 타입별 풀에 살고, 여기서는 SafePtr 로 참조만 들고 lifetime 을
//    결정한다(Destroy 시 scene 을 통해 각 풀에서 해제).
//  · 외부 공유는 SafeFromThis()/SafePtr<CGameObject>.
//
//  템플릿 메서드 AddComponent/RemoveComponent 는 CScene 완전형이 필요하므로
//  본체는 Scene.h 하단에 정의한다(GetComponent 류는 scene 불필요 → 여기 인라인).
// ─────────────────────────────────────────────────────────────────────────────
class CGameObject final : public EnableSafeFromThis<CGameObject>
{
public:
	CGameObject() = default;
	CGameObject(CScene& scene, const char* name, const File::Guid& instanceGuid)
		: Name(name ? name : "GameObject")
		, InstanceGuid(instanceGuid)
		, m_scene(&scene)
	{
	}

	CGameObject(const CGameObject&) = delete;
	CGameObject& operator=(const CGameObject&) = delete;

	// ── 기본 속성 ────────────────────────────────────────────────────────────
	std::string   Name;
	bool          IsActive = true;
	std::uint32_t Layer = 0;
	File::Guid    InstanceGuid;

	// ── Transform (컴포넌트 아님 — 멤버) ──────────────────────────────────────
	Transform2D      Local;
	WorldTransform2D World;

	// 렌더러 픽킹 등 프레임 내 불투명 식별자. 풀 슬롯 주소가 안정적이므로 객체
	// 수명 동안 유일·불변(직렬화 키는 InstanceGuid, 이건 런타임 전용).
	std::uint64_t GetId() const { return reinterpret_cast<std::uintptr_t>(this); }

	const char* GetName() const { return Name.c_str(); }
	void        SetName(const char* name) { Name = name ? name : ""; }
	bool        IsActiveSelf() const { return IsActive; }
	void        SetActive(bool active) { IsActive = active; }

	Transform2D&            GetTransform()       { return Local; }
	const Transform2D&      GetTransform() const { return Local; }
	WorldTransform2D&       GetWorld()           { return World; }
	const WorldTransform2D& GetWorld()     const { return World; }

	CScene* GetScene() const { return m_scene; }

	// ── 컴포넌트 ──────────────────────────────────────────────────────────────
	// AddComponent/RemoveComponent: Scene.h 하단 정의.
	template<typename T, typename... Args> T* AddComponent(Args&&... args);
	template<typename T>                    void RemoveComponent();

	template<typename T>
	T* GetComponent()
	{
		for (const SafePtr<CComponent>& c : m_components)
		{
			if (T* typed = dynamic_cast<T*>(c.TryGet()))
			{
				return typed;
			}
		}
		return nullptr;
	}

	template<typename T>
	const T* GetComponent() const
	{
		for (const SafePtr<CComponent>& c : m_components)
		{
			if (const T* typed = dynamic_cast<const T*>(c.TryGet()))
			{
				return typed;
			}
		}
		return nullptr;
	}

	template<typename T>
	bool HasComponent() const { return nullptr != GetComponent<T>(); }

	const std::vector<SafePtr<CComponent>>& GetComponents() const { return m_components; }

	// scene 의 AddComponent/RemoveComponent 가 호출하는 부착/탈착 훅.
	void AttachComponent(const SafePtr<CComponent>& component) { m_components.push_back(component); }
	void DetachComponent(CComponent* component)
	{
		for (std::size_t i = 0; i < m_components.size(); ++i)
		{
			if (m_components[i].TryGet() == component)
			{
				m_components.erase(m_components.begin() + i);
				return;
			}
		}
	}

	// ── 계층 ──────────────────────────────────────────────────────────────────
	SafePtr<CGameObject> GetParent() const { return m_parent; }
	const std::vector<SafePtr<CGameObject>>& GetChildren() const { return m_children; }

	bool SetParent(CGameObject& parent);
	void ClearParent();
	bool IsDescendantOf(const CGameObject& possibleAncestor) const;

	// scene 이 사용하는 계층 링크 조작(외부 호출 금지).
	void __AddChild(const SafePtr<CGameObject>& child) { m_children.push_back(child); }
	void __RemoveChild(CGameObject* child)
	{
		for (std::size_t i = 0; i < m_children.size(); ++i)
		{
			if (m_children[i].TryGet() == child)
			{
				m_children.erase(m_children.begin() + i);
				return;
			}
		}
	}
	void __SetParentRef(const SafePtr<CGameObject>& parent) { m_parent = parent; }

	// ── 파괴 ──────────────────────────────────────────────────────────────────
	void Destroy();

private:
	CScene*                          m_scene = nullptr;
	SafePtr<CGameObject>             m_parent;
	std::vector<SafePtr<CGameObject>> m_children;
	std::vector<SafePtr<CComponent>>  m_components;
};
