#pragma once

#include <cstddef>
#include <cstdint>

struct GameObject
{
	static constexpr std::size_t MAX_NAME_LENGTH = 63;

	GameObject();
	explicit GameObject(const char* name);

	void SetName(const char* name);
	void CopyNameTo(char* buffer, std::size_t bufferLength) const;

	char Name[MAX_NAME_LENGTH + 1];
	bool IsActive = true;
	std::uint32_t Layer = 0;
};
