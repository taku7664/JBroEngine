#pragma once

#include "Core/RHI/RHIResource.h"
#include "Core/RHI/RHIGraphicsTypes.h"

class IRHIBuffer : public IRHIResource
{
public:
	virtual const RHIBufferDesc& GetDesc() const = 0;
};
