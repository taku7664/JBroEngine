#pragma once

#include "Utillity/SafePtr.h"

class IRHIResource : public EnableSafeFromThis<IRHIResource>
{
public:
	virtual ~IRHIResource() = default;
};
