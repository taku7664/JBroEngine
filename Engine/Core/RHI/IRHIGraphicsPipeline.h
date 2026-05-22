#pragma once

#include "Core/RHI/RHIResource.h"
#include "Core/RHI/RHIGraphicsTypes.h"

class IRHIGraphicsPipeline : public IRHIResource
{
public:
	virtual const RHIGraphicsPipelineDesc& GetDesc() const = 0;
};
