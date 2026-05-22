#pragma once

#include "Core/RHI/RHIResource.h"
#include "Core/RHI/RHIGraphicsTypes.h"

class IRHISampler : public IRHIResource
{
public:
	virtual const RHISamplerDesc& GetDesc() const = 0;
};
