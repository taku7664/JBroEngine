#include "pch.h"
#include "Application.h"

bool CApplication::InitializeApplication()
{
	if (m_isInitialized)
	{
		return true;
	}

	std::setlocale(LC_ALL, ".UTF-8");

	m_engine = MakeOwnerPtr<CEngine>();
	if (!m_engine)
	{
		return false;
	}

	OnPreInitialize();
	if (false == m_engine->Initialize())
	{
		return false;
	}
	OnPostInitialize();

	m_isRunning = true;
	m_isInitialized = true;
	return true;
}

bool CApplication::TickApplication()
{
	if (false == m_isRunning)
	{
		return false;
	}

	if (!m_engine || false == m_engine->Update())
	{
		Quit();
		return false;
	}

	return true;
}

void CApplication::FinalizeApplication()
{
	if (false == m_isInitialized)
	{
		return;
	}

	OnPreFinalize();
	if (m_engine)
	{
		m_engine->Finalize();
		m_engine.Reset();
	}
	OnPostFinalize();

	m_isRunning = false;
	m_isInitialized = false;
}

void CApplication::Run()
{
	RunNative();
}

void CApplication::RunNative()
{
	if (InitializeApplication())
	{
		while (TickApplication())
		{
		}
	}

	FinalizeApplication();
}

void CApplication::Quit()
{
	m_isRunning = false;
}

CEngine* CApplication::GetEngine()
{
	return m_engine.Get();
}

const CEngine* CApplication::GetEngine() const
{
	return m_engine.Get();
}
