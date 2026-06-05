#include "pch.h"
#include "Module.h"

void CModule::Initialize(const char* moduleName)
{
	m_moduleName = moduleName;

	OnPreInitialize();
	OnPostInitialize();
}

void CModule::Finalize()
{
	OnPreFinalize();
	OnPostFinalize();

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
