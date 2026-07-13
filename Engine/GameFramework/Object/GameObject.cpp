#include "pch.h"
#include "GameObject.h"

#include "GameFramework/Scene/Scene.h"
bool CGameObject::MoveComponent(const File::Guid& instanceGuid, std::size_t newIndex)
{
	auto it = std::find_if(m_components.begin(), m_components.end(), [&](const SafePtr<CComponent>& component)
	{
		const CComponent* value = component.TryGet();
		return value && value->InstanceGuid == instanceGuid;
	});
	if (it == m_components.end()) return false;
	SafePtr<CComponent> moved = *it;
	m_components.erase(it);
	newIndex = std::min(newIndex, m_components.size());
	m_components.insert(m_components.begin() + static_cast<std::ptrdiff_t>(newIndex), std::move(moved));
	return true;
}

bool CGameObject::SetParent(CGameObject& parent)
{
	if (this == &parent || parent.IsDescendantOf(*this))
	{
		return false;
	}

	ClearParent();
	m_parent = parent.SafeFromThis();
	parent.AddChildInternal(SafeFromThis());
	return true;
}

void CGameObject::ClearParent()
{
	if (CGameObject* parent = m_parent.TryGet())
	{
		parent->RemoveChildInternal(this);
	}
	m_parent.Reset();
}

bool CGameObject::IsDescendantOf(const CGameObject& possibleAncestor) const
{
	const CGameObject* current = m_parent.TryGet();
	while (nullptr != current)
	{
		if (current == &possibleAncestor)
		{
			return true;
		}
		current = current->m_parent.TryGet();
	}
	return false;
}

void CGameObject::Destroy()
{
	if (m_scene)
	{
		m_scene->DestroyGameObject(this);
	}
}
