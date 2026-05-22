#include "pch.h"
#include "GameObject.h"

GameObject::GameObject()
{
	Name[0] = '\0';
}

GameObject::GameObject(const char* name)
{
	SetName(name);
}

void GameObject::SetName(const char* name)
{
	if (nullptr == name)
	{
		Name[0] = '\0';
		return;
	}

	std::size_t index = 0;
	for (; index < MAX_NAME_LENGTH && '\0' != name[index]; ++index)
	{
		Name[index] = name[index];
	}

	Name[index] = '\0';
}

void GameObject::CopyNameTo(char* buffer, std::size_t bufferLength) const
{
	if (nullptr == buffer || 0 == bufferLength)
	{
		return;
	}

	std::size_t index = 0;
	for (; index + 1 < bufferLength && '\0' != Name[index]; ++index)
	{
		buffer[index] = Name[index];
	}

	buffer[index] = '\0';
}
