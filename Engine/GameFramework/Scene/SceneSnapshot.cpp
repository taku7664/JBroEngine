#include "pch.h"
#include "SceneSnapshot.h"

void SceneSnapshot::Clear()
{
	Objects.clear();
}

std::size_t SceneSnapshot::GetObjectCount() const
{
	return Objects.size();
}
