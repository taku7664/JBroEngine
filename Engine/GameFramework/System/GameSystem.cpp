#include "pch.h"
#include "GameSystem.h"

void CGameSystem::Initialize(CScene& scene)
{
	if (m_isInitialized)
	{
		return;
	}

	OnInitialize(scene);
	m_isInitialized = true;
}

void CGameSystem::Update(CScene& scene)
{
	if (false == m_isInitialized)
	{
		return;
	}

	OnUpdate(scene);
}

void CGameSystem::Finalize(CScene& scene)
{
	if (false == m_isInitialized)
	{
		return;
	}

	OnFinalize(scene);
	m_isInitialized = false;
}

bool CGameSystem::IsInitialized() const
{
	return m_isInitialized;
}

