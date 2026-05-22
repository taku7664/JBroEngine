#pragma once

#include "Core/Platform/PlatformDefines.h"
#include "Core/RHI/IRHIProgram.h"

#if JBRO_PLATFORM_WINDOWS
#include <d3dcommon.h>
#endif

class CD3D11Program final : public IRHIProgram
{
public:
	CD3D11Program(ERHIProgramStage stage, ERHIProgramLanguage language);
	~CD3D11Program() override;

	ERHIProgramStage GetStage() const override;
	ERHIProgramLanguage GetLanguage() const override;

#if JBRO_PLATFORM_WINDOWS
	void BindNativeBlob(ID3DBlob* bytecode);
	ID3DBlob* GetBytecode() const;
#endif

private:
	ERHIProgramStage m_stage = ERHIProgramStage::Vertex;
	ERHIProgramLanguage m_language = ERHIProgramLanguage::Unknown;
#if JBRO_PLATFORM_WINDOWS
	ID3DBlob* m_bytecode = nullptr;
#endif
};
