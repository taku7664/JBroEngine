#pragma once

#include "Core/Input/InputTypes.h"

class IInputMessageHandler
{
public:
	virtual ~IInputMessageHandler() = default;

public:
	// Return true to continue propagation to lower layers, false to block it.
	virtual bool OnReceiveInputMessage(const InputMessage& message) = 0;
};
