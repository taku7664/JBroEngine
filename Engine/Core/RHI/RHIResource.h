#pragma once

#include "Utillity/Pointer/SafePtr.h"

class IRHIResource : public EnableSafeFromThis<IRHIResource>
{
public:
	virtual ~IRHIResource() = default;
};
