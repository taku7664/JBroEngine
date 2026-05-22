#pragma once

#include "Core/RHI/RHIResource.h"
#include "Core/RHI/RHIGraphicsTypes.h"

class IRHIProgram : public IRHIResource
{
public:
	virtual ERHIProgramStage GetStage() const = 0;
	virtual ERHIProgramLanguage GetLanguage() const = 0;
};
