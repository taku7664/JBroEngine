#include "pch.h"
#include "GameSystem.h"

void CGameSystem::Initialize(CGameCanvas& canvas)
{
	if (m_isInitialized)
	{
		return;
	}

	OnInitialize(canvas);
	m_isInitialized = true;
}

void CGameSystem::Update(CGameCanvas& canvas)
{
	if (false == m_isInitialized)
	{
		return;
	}

	OnUpdate(canvas);
}

void CGameSystem::FixedUpdate(CGameCanvas& canvas)
{
	if (false == m_isInitialized)
	{
		return;
	}

	OnFixedUpdate(canvas);
}

void CGameSystem::Finalize(CGameCanvas& canvas)
{
	if (false == m_isInitialized)
	{
		return;
	}

	OnFinalize(canvas);
	m_isInitialized = false;
}

void CGameSystem::SimulationStop(CGameCanvas& canvas)
{
	if (false == m_isInitialized)
	{
		return;
	}

	OnSimulationStop(canvas);
}

bool CGameSystem::IsInitialized() const
{
	return m_isInitialized;
}

