#pragma once

#include "Utillity/Pointer/SafePtr.h"

class IRenderResource : public EnableSafeFromThis<IRenderResource>
{
public:
	virtual ~IRenderResource() = default;
};

