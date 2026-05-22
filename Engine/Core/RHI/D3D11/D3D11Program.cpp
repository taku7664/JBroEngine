#include "pch.h"
#include "D3D11Program.h"

#if JBRO_PLATFORM_WINDOWS
#include <d3dcompiler.h>
#endif

CD3D11Program::CD3D11Program(ERHIProgramStage stage, ERHIProgramLanguage language)
	: m_stage(stage)
	, m_language(language)
{
}

CD3D11Program::~CD3D11Program()
{
#if JBRO_PLATFORM_WINDOWS
	if (m_bytecode)
	{
		m_bytecode->Release();
		m_bytecode = nullptr;
	}
#endif
}

ERHIProgramStage CD3D11Program::GetStage() const
{
	return m_stage;
}

ERHIProgramLanguage CD3D11Program::GetLanguage() const
{
	return m_language;
}

#if JBRO_PLATFORM_WINDOWS
void CD3D11Program::BindNativeBlob(ID3DBlob* bytecode)
{
	m_bytecode = bytecode;
}

ID3DBlob* CD3D11Program::GetBytecode() const
{
	return m_bytecode;
}
#endif
