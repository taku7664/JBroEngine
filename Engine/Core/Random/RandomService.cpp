#include "pch.h"
#include "RandomService.h"

CRandomService::CRandomService()
	: m_seed(std::random_device{}())
	, m_engine(m_seed)
{
}

void CRandomService::SetSeed(std::uint32_t seed)
{
	std::lock_guard lock(m_mutex);
	m_seed = seed;
	m_engine.seed(seed);
}

std::uint32_t CRandomService::GetSeed() const
{
	std::lock_guard lock(m_mutex);
	return m_seed;
}

int CRandomService::RangeInt(int minInclusive, int maxInclusive)
{
	std::lock_guard lock(m_mutex);
	if (maxInclusive < minInclusive)
	{
		std::swap(minInclusive, maxInclusive);
	}
	std::uniform_int_distribution<int> distribution(minInclusive, maxInclusive);
	return distribution(m_engine);
}

float CRandomService::RangeFloat(float minInclusive, float maxInclusive)
{
	std::lock_guard lock(m_mutex);
	if (maxInclusive < minInclusive)
	{
		std::swap(minInclusive, maxInclusive);
	}
	std::uniform_real_distribution<float> distribution(minInclusive, maxInclusive);
	return distribution(m_engine);
}

Vector2 CRandomService::UnitVector2()
{
	const float angle = RangeFloat(0.0f, 6.28318530718f);
	return Vector2{ std::cos(angle), std::sin(angle) };
}
