#include "pch.h"
#include "Module.h"
#include "Core/EngineContext.h"

const EngineContext* CModule::GetEngineContext() const
{
	return m_engineContext;
}

void CModule::Initialize(const char* moduleName, const EngineContext& context)
{
	m_moduleName = moduleName;
	m_engineContext = &context;

	OnPreInitialize();
	OnPostInitialize();
}

void CModule::Finalize()
{
	OnPreFinalize();
	OnPostFinalize();

	m_engineContext = nullptr;
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
