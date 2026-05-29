#pragma once

#include "Utillity/SafePtr.h"
#include "Utillity/Vector2T.h"

#include <mutex>
#include <random>

class CRandomService final : public EnableSafeFromThis<CRandomService>
{
public:
	CRandomService();

	void SetSeed(std::uint32_t seed);
	std::uint32_t GetSeed() const;
	int RangeInt(int minInclusive, int maxInclusive);
	float RangeFloat(float minInclusive, float maxInclusive);
	Vector2<float> UnitVector2();

private:
	mutable std::mutex m_mutex;
	std::mt19937 m_engine;
	std::uint32_t m_seed = 0;
};
