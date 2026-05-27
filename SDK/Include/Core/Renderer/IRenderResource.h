#pragma once

#include "Utillity/SafePtr.h"

class IRenderResource : public EnableSafeFromThis<IRenderResource>
{
public:
	virtual ~IRenderResource() = default;
};

