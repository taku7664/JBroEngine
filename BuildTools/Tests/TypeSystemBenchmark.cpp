#include "Utillity/Types/Array.h"
#include "Utillity/Types/Table.h"
#include "Utillity/File/Guid128.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	using Clock = std::chrono::steady_clock;
	volatile std::uint64_t g_sink = 0;

	template<typename Function>
	double MeasureNanoseconds(Function&& function)
	{
		const auto begin = Clock::now();
		function();
		const auto end = Clock::now();
		return std::chrono::duration<double, std::nano>(end - begin).count();
	}

	double Median(std::vector<double> samples)
	{
		std::sort(samples.begin(), samples.end());
		return samples[samples.size() / 2];
	}

	__declspec(noinline) std::uint64_t SumEngineIterator(const Array<std::uint64_t>& values)
	{
		std::uint64_t sum = 0;
		for (std::uint64_t value : values)
		{
			sum += value;
		}
		return sum;
	}

	__declspec(noinline) std::uint64_t SumStandardIterator(const std::vector<std::uint64_t>& values)
	{
		std::uint64_t sum = 0;
		for (std::uint64_t value : values)
		{
			sum += value;
		}
		return sum;
	}

	__declspec(noinline) std::uint64_t SumEnginePointer(const Array<std::uint64_t>& values)
	{
		const std::uint64_t* current = values.Data();
		const std::uint64_t* const end = current + values.Size();
		std::uint64_t sum = 0;
		while (current != end)
		{
			sum += *current++;
		}
		return sum;
	}

	__declspec(noinline) std::uint64_t SumStandardPointer(const std::vector<std::uint64_t>& values)
	{
		const std::uint64_t* current = values.data();
		const std::uint64_t* const end = current + values.size();
		std::uint64_t sum = 0;
		while (current != end)
		{
			sum += *current++;
		}
		return sum;
	}

	void BenchmarkArray(std::size_t count)
	{
		Array<std::uint64_t> engineArray;
		std::vector<std::uint64_t> standardArray;
		engineArray.Reserve(count);
		standardArray.reserve(count);

		std::vector<double> engineAppendSamples;
		std::vector<double> standardAppendSamples;
		for (int trial = 0; trial < 31; ++trial)
		{
			engineArray.Clear();
			standardArray.clear();
			if (0 == (trial & 1))
			{
				engineAppendSamples.push_back(MeasureNanoseconds([&]()
				{
					for (std::size_t index = 0; index < count; ++index)
					{
						engineArray.Add(static_cast<std::uint64_t>(index));
					}
				}));
				standardAppendSamples.push_back(MeasureNanoseconds([&]()
				{
					for (std::size_t index = 0; index < count; ++index)
					{
						standardArray.push_back(static_cast<std::uint64_t>(index));
					}
				}));
			}
			else
			{
				standardAppendSamples.push_back(MeasureNanoseconds([&]()
				{
					for (std::size_t index = 0; index < count; ++index)
					{
						standardArray.push_back(static_cast<std::uint64_t>(index));
					}
				}));
				engineAppendSamples.push_back(MeasureNanoseconds([&]()
				{
					for (std::size_t index = 0; index < count; ++index)
					{
						engineArray.Add(static_cast<std::uint64_t>(index));
					}
				}));
			}
		}

		std::vector<double> engineIteratorSamples;
		std::vector<double> standardIteratorSamples;
		std::vector<double> enginePointerSamples;
		std::vector<double> standardPointerSamples;
		for (int trial = 0; trial < 101; ++trial)
		{
			if (0 == (trial & 1))
			{
				engineIteratorSamples.push_back(MeasureNanoseconds([&]() { g_sink = SumEngineIterator(engineArray); }));
				standardIteratorSamples.push_back(MeasureNanoseconds([&]() { g_sink = SumStandardIterator(standardArray); }));
				enginePointerSamples.push_back(MeasureNanoseconds([&]() { g_sink = SumEnginePointer(engineArray); }));
				standardPointerSamples.push_back(MeasureNanoseconds([&]() { g_sink = SumStandardPointer(standardArray); }));
			}
			else
			{
				standardPointerSamples.push_back(MeasureNanoseconds([&]() { g_sink = SumStandardPointer(standardArray); }));
				enginePointerSamples.push_back(MeasureNanoseconds([&]() { g_sink = SumEnginePointer(engineArray); }));
				standardIteratorSamples.push_back(MeasureNanoseconds([&]() { g_sink = SumStandardIterator(standardArray); }));
				engineIteratorSamples.push_back(MeasureNanoseconds([&]() { g_sink = SumEngineIterator(engineArray); }));
			}
		}

		std::cout
			<< "Array reserved append median ns/element: engine=" << Median(engineAppendSamples) / count
			<< " std=" << Median(standardAppendSamples) / count << '\n'
			<< "Array iterator median ns/element: engine=" << Median(engineIteratorSamples) / count
			<< " std=" << Median(standardIteratorSamples) / count << '\n'
			<< "Array pointer median ns/element: engine=" << Median(enginePointerSamples) / count
			<< " std=" << Median(standardPointerSamples) / count << '\n';
	}

	void BenchmarkArrayRemoval(std::size_t requestedCount)
	{
		const std::size_t count = (std::min)(requestedCount, static_cast<std::size_t>(65'536));
		Array<std::uint64_t> engineOrdered;
		std::vector<std::uint64_t> standardOrdered;
		engineOrdered.Reserve(count);
		standardOrdered.reserve(count);
		for (std::size_t index = 0; index < count; ++index)
		{
			engineOrdered.Add(index);
			standardOrdered.push_back(index);
		}

		const double engineErase = MeasureNanoseconds([&]()
		{
			while (false == engineOrdered.IsEmpty())
			{
				engineOrdered.RemoveAt(0);
			}
		});
		const double standardErase = MeasureNanoseconds([&]()
		{
			while (false == standardOrdered.empty())
			{
				standardOrdered.erase(standardOrdered.begin());
			}
		});

		Array<std::uint64_t> engineCompact;
		std::vector<std::uint64_t> standardCompact;
		engineCompact.Reserve(count);
		standardCompact.reserve(count);
		for (std::size_t index = 0; index < count; ++index)
		{
			engineCompact.Add(index);
			standardCompact.push_back(index);
		}
		const double engineRemoveAll = MeasureNanoseconds([&]()
		{
			engineCompact.RemoveAll([](std::uint64_t value) { return 0 != (value & 1); });
		});
		const double standardRemoveAll = MeasureNanoseconds([&]()
		{
			standardCompact.erase(
				std::remove_if(
					standardCompact.begin(),
					standardCompact.end(),
					[](std::uint64_t value) { return 0 != (value & 1); }),
				standardCompact.end());
		});

		std::cout
			<< "Array ordered front erase ns/removed: engine=" << engineErase / count
			<< " std=" << standardErase / count << '\n'
			<< "Array 50% compact ns/input: engine=" << engineRemoveAll / count
			<< " std=" << standardRemoveAll / count << '\n';
	}

	void BenchmarkTable(std::size_t count)
	{
		std::vector<std::uint64_t> keys(count);
		std::mt19937_64 random(0x4A42524F);
		for (std::uint64_t& key : keys)
		{
			key = random();
		}

		Table<std::uint64_t, std::uint64_t> engineTable;
		std::unordered_map<std::uint64_t, std::uint64_t> standardTable;
		engineTable.Reserve(count);
		standardTable.reserve(count);

		const double engineInsert = MeasureNanoseconds([&]()
		{
			for (std::size_t index = 0; index < count; ++index)
			{
				engineTable.TryAdd(keys[index], static_cast<std::uint64_t>(index));
			}
		});
		const double standardInsert = MeasureNanoseconds([&]()
		{
			for (std::size_t index = 0; index < count; ++index)
			{
				standardTable.emplace(keys[index], static_cast<std::uint64_t>(index));
			}
		});

		std::vector<double> engineHitSamples;
		std::vector<double> standardHitSamples;
		std::vector<double> engineMissSamples;
		std::vector<double> standardMissSamples;
		for (int trial = 0; trial < 31; ++trial)
		{
			engineHitSamples.push_back(MeasureNanoseconds([&]()
			{
				std::uint64_t sum = 0;
				for (std::uint64_t key : keys)
				{
					const std::uint64_t* value = engineTable.Find(key);
					sum += nullptr == value ? 0 : *value;
				}
				g_sink = sum;
			}));
			standardHitSamples.push_back(MeasureNanoseconds([&]()
			{
				std::uint64_t sum = 0;
				for (std::uint64_t key : keys)
				{
					const auto found = standardTable.find(key);
					sum += found == standardTable.end() ? 0 : found->second;
				}
				g_sink = sum;
			}));
			engineMissSamples.push_back(MeasureNanoseconds([&]()
			{
				std::uint64_t sum = 0;
				for (std::uint64_t key : keys)
				{
					sum += engineTable.Contains(key ^ 0x9E3779B97F4A7C15ull) ? 1 : 0;
				}
				g_sink = sum;
			}));
			standardMissSamples.push_back(MeasureNanoseconds([&]()
			{
				std::uint64_t sum = 0;
				for (std::uint64_t key : keys)
				{
					sum += standardTable.contains(key ^ 0x9E3779B97F4A7C15ull) ? 1 : 0;
				}
				g_sink = sum;
			}));
		}

		std::cout
			<< "Table insert ns/element: engine=" << engineInsert / count
			<< " std=" << standardInsert / count << '\n'
			<< "Table hit median ns/lookup: engine=" << Median(engineHitSamples) / count
			<< " std=" << Median(standardHitSamples) / count << '\n'
			<< "Table miss median ns/lookup: engine=" << Median(engineMissSamples) / count
			<< " std=" << Median(standardMissSamples) / count << '\n';
	}

	struct PoolIterationSlot
	{
		std::uint64_t Value = 0;
		std::uint64_t Padding[3] = {};
		bool Occupied = false;
	};

	void BenchmarkPoolIteration(std::size_t requestedCount, std::size_t livePercent)
	{
		const std::size_t count = (std::max)(requestedCount, static_cast<std::size_t>(1024));
		std::vector<PoolIterationSlot> slots(count);
		std::vector<PoolIterationSlot*> dense;
		dense.reserve(count);
		for (std::size_t index = 0; index < count; ++index)
		{
			slots[index].Value = index;
			slots[index].Occupied = (index % 100) < livePercent;
			if (slots[index].Occupied)
			{
				dense.push_back(&slots[index]);
			}
		}

		std::vector<double> slotSamples;
		std::vector<double> denseSamples;
		for (int trial = 0; trial < 101; ++trial)
		{
			slotSamples.push_back(MeasureNanoseconds([&]()
			{
				std::uint64_t sum = 0;
				for (const PoolIterationSlot& slot : slots)
				{
					if (slot.Occupied)
					{
						sum += slot.Value;
					}
				}
				g_sink = sum;
			}));
			denseSamples.push_back(MeasureNanoseconds([&]()
			{
				std::uint64_t sum = 0;
				for (const PoolIterationSlot* slot : dense)
				{
					sum += slot->Value;
				}
				g_sink = sum;
			}));
		}

		std::cout
			<< "Pool iteration " << livePercent << "% live ns/slot: scan="
			<< Median(slotSamples) / count
			<< " dense=" << Median(denseSamples) / count << '\n';
	}

	void BenchmarkGuidTable(std::size_t count)
	{
		std::vector<Guid128> keys(count);
		std::mt19937_64 random(0x47554944313238ull);
		for (Guid128& key : keys)
		{
			key.Hi = random();
			key.Lo = random();
		}

		Table<Guid128, std::uint64_t> engine;
		std::unordered_map<Guid128, std::uint64_t> standard;
		engine.Reserve(count);
		standard.reserve(count);
		for (std::size_t index = 0; index < count; ++index)
		{
			engine.TryAdd(keys[index], index);
			standard.emplace(keys[index], index);
		}

		std::vector<double> engineSamples;
		std::vector<double> standardSamples;
		for (int trial = 0; trial < 31; ++trial)
		{
			engineSamples.push_back(MeasureNanoseconds([&]()
			{
				std::uint64_t sum = 0;
				for (const Guid128& key : keys)
				{
					const std::uint64_t* value = engine.Find(key);
					sum += nullptr == value ? 0 : *value;
				}
				g_sink = sum;
			}));
			standardSamples.push_back(MeasureNanoseconds([&]()
			{
				std::uint64_t sum = 0;
				for (const Guid128& key : keys)
				{
					const auto found = standard.find(key);
					sum += found == standard.end() ? 0 : found->second;
				}
				g_sink = sum;
			}));
		}

		std::cout
			<< "Guid128 Table hit median ns/lookup: engine=" << Median(engineSamples) / count
			<< " std=" << Median(standardSamples) / count << '\n';
	}
}

int main(int argc, char** argv)
{
	const std::size_t count = argc > 1
		? static_cast<std::size_t>(std::stoull(argv[1]))
		: 1'000'000;
	BenchmarkArray(count);
	BenchmarkArrayRemoval(count);
	BenchmarkTable(count);
	BenchmarkPoolIteration(count, 100);
	BenchmarkPoolIteration(count, 50);
	BenchmarkPoolIteration(count, 10);
	BenchmarkGuidTable(count);
	return static_cast<int>(g_sink & 0);
}
