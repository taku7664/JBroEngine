#include "pch.h"
#include "GameSystem.h"

void CGameSystem::Initialize(CGameCanvas& scene)
{
	if (m_isInitialized)
	{
		return;
	}

	OnInitialize(scene);
	m_isInitialized = true;
}

void CGameSystem::Update(CGameCanvas& scene)
{
	if (false == m_isInitialized)
	{
		return;
	}

	OnUpdate(scene);
}

void CGameSystem::FixedUpdate(CGameCanvas& scene)
{
	if (false == m_isInitialized)
	{
		return;
	}

	OnFixedUpdate(scene);
}

void CGameSystem::Finalize(CGameCanvas& scene)
{
	if (false == m_isInitialized)
	{
		return;
	}

	OnFinalize(scene);
	m_isInitialized = false;
}

void CGameSystem::SimulationStop(CGameCanvas& scene)
{
	if (false == m_isInitialized)
	{
		return;
	}

	OnSimulationStop(scene);
}

bool CGameSystem::IsInitialized() const
{
	return m_isInitialized;
}

