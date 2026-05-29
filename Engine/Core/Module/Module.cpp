#include "pch.h"
#include "Module.h"
#include "Core/EngineCore.h"

const EngineCore* CModule::GetEngineCore() const
{
	return m_engineCore;
}

void CModule::Initialize(const char* moduleName, const EngineCore& engineCore)
{
	m_moduleName = moduleName;
	m_engineCore = &engineCore;

	OnPreInitialize();
	OnPostInitialize();
}

void CModule::Finalize()
{
	OnPreFinalize();
	OnPostFinalize();

	m_engineCore = nullptr;
	m_moduleName.clear();
}

void CModule::BeginFrame()
{
	OnBeginFrame();
}

void CModule::Update()
{
	OnUpdate();
}

void CModule::PrepareRender()
{
	OnPrepareRender();
}

void CModule::Render()
{
	OnRender();
}

void CModule::EndFrame()
{
	OnEndFrame();
}
