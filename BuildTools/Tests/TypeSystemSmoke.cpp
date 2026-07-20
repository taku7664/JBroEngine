#include "Utillity/Types/Array.h"
#include "Utillity/Types/Simd128.h"
#include "Utillity/Types/Table.h"
#include "GameFramework/Reflection/ReflectionContainerOps.h"
#include "GameFramework/Object/ObjectPool.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

namespace
{
	struct LifetimeProbe
	{
		LifetimeProbe()
		{
			++Alive;
		}

		explicit LifetimeProbe(int value)
			: Value(value)
		{
			++Alive;
		}

		LifetimeProbe(const LifetimeProbe& other)
			: Value(other.Value)
		{
			++Alive;
		}

		LifetimeProbe(LifetimeProbe&& other) noexcept
			: Value(other.Value)
		{
			other.Value = -1;
			++Alive;
		}

		LifetimeProbe& operator=(LifetimeProbe&& other) noexcept
		{
			Value = other.Value;
			other.Value = -1;
			return *this;
		}

		~LifetimeProbe()
		{
			--Alive;
		}

		int Value = 0;
		inline static int Alive = 0;
	};

	struct CollidingHash
	{
		std::size_t operator()(int value) const noexcept
		{
			return static_cast<std::size_t>(value & 3);
		}
	};

	struct PoolProbe final : EnableSafeFromThis<PoolProbe>
	{
		explicit PoolProbe(int value)
			: Value(value)
		{
			++Alive;
		}

		~PoolProbe()
		{
			--Alive;
		}

		int Value = 0;
		inline static int Alive = 0;
	};


	bool TestArray()
	{
		{
			Array<LifetimeProbe> values;
			for (int value = 0; value < 256; ++value)
			{
				values.Emplace(value);
			}

			if (256 != values.Size() || 256 != LifetimeProbe::Alive)
			{
				return false;
			}

			values.RemoveAt(4);
			values.RemoveAtSwap(8);
			if (254 != values.Size() || 254 != LifetimeProbe::Alive)
			{
				return false;
			}

			const std::size_t capacity = values.Capacity();
			values.Reset();
			if (false == values.IsEmpty() || capacity != values.Capacity())
			{
				return false;
			}
		}

		if (0 != LifetimeProbe::Alive)
		{
			return false;
		}

		Array<std::unique_ptr<int>> owners;
		owners.Emplace(std::make_unique<int>(42));
		owners.Emplace(std::make_unique<int>(7));
		if (42 != *owners.First() || 7 != *owners.Last())
		{
			return false;
		}

		Array<int> selfAppend{ 1, 2, 3, 4 };
		selfAppend.Append(selfAppend.Data() + 1, 2);
		selfAppend.Insert(2, 17);
		if (7 != selfAppend.Size() || 17 != selfAppend[2])
		{
			return false;
		}
		if (1 != selfAppend.RemoveAll([](int value) { return 17 == value; }))
		{
			return false;
		}
		return 6 == selfAppend.Size() && 2 == selfAppend[4] && 3 == selfAppend[5];
	}

	bool TestSimd128()
	{
		alignas(16) std::int8_t bytes[Simd128::ByteWidth] = {};
		for (int seed = 0; seed < 256; ++seed)
		{
			for (std::size_t index = 0; index < Simd128::ByteWidth; ++index)
			{
				bytes[index] = static_cast<std::int8_t>((seed * 37 + index * 13) & 0xFF);
			}

			for (int value = -128; value <= 127; ++value)
			{
				const std::int8_t byteValue = static_cast<std::int8_t>(value);
				if (Simd128::MatchBytesScalar(bytes, byteValue) !=
					Simd128::MatchBytes(bytes, byteValue))
				{
					return false;
				}
			}
		}

		bytes[1] = 7;
		bytes[5] = 7;
		bytes[15] = 7;
		const std::uint16_t mask = Simd128::MatchBytes(bytes, 7);
		return 1 == Simd128::FirstMatchIndex(mask);
	}

	bool TestTable()
	{
		Table<int, std::string, CollidingHash> values;
		for (int key = 0; key < 1000; ++key)
		{
			if (false == values.TryAdd(key, std::to_string(key)))
			{
				return false;
			}
		}

		for (int key = 0; key < 1000; ++key)
		{
			const std::string* value = values.Find(key);
			if (nullptr == value || *value != std::to_string(key))
			{
				return false;
			}
		}

		for (int key = 0; key < 1000; key += 2)
		{
			if (false == values.Remove(key))
			{
				return false;
			}
		}

		for (int key = 0; key < 1000; ++key)
		{
			if ((0 == key % 2) == values.Contains(key))
			{
				return false;
			}
		}

		for (int key = 1000; key < 1500; ++key)
		{
			values.InsertOrAssign(key, std::to_string(key));
		}

		Table<int, std::string, CollidingHash> copy(values);
		return values.Size() == copy.Size() && nullptr != copy.Find(1499);
	}

	bool TestArrayReflectionOps()
	{
		const ReflectTypeDesc& arrayDescriptor =
			GetArrayReflectTypeDesc<int, EReflectPropertyType::Int32>();
		const ReflectTypeDesc& integerDescriptor =
			GetScalarReflectTypeDesc<int, EReflectPropertyType::Int32>();
		if (EReflectPropertyType::Array != arrayDescriptor.Type ||
			nullptr == arrayDescriptor.ArrayOps ||
			&integerDescriptor != arrayDescriptor.Element)
		{
			return false;
		}

		Array<int> values{ 10, 20 };
		arrayDescriptor.ArrayOps->AddDefault(&values);
		if (3 != arrayDescriptor.ArrayOps->GetSize(&values))
		{
			return false;
		}

		int* last = static_cast<int*>(arrayDescriptor.ArrayOps->GetElement(&values, 2));
		if (nullptr == last || 0 != *last)
		{
			return false;
		}
		*last = 30;
		arrayDescriptor.ArrayOps->Move(&values, 2, 0);
		if (30 != values[0] || 10 != values[1] || 20 != values[2])
		{
			return false;
		}

		arrayDescriptor.ArrayOps->RemoveAt(&values, 1);
		return 2 == values.Size() && 30 == values[0] && 20 == values[1];
	}

	bool TestTableReflectionOps()
	{
		const ReflectTypeDesc& tableDescriptor = GetTableReflectTypeDesc<
			int, EReflectPropertyType::Int32,
			float, EReflectPropertyType::Float>();
		if (EReflectPropertyType::Table != tableDescriptor.Type ||
			nullptr == tableDescriptor.TableOps ||
			nullptr == tableDescriptor.Key ||
			nullptr == tableDescriptor.Value)
		{
			return false;
		}

		const ReflectTableOps& ops = *tableDescriptor.TableOps;
		Table<int, float> values;
		for (int key = 0; key < 64; ++key)
		{
			const int stored = key;
			if (false == ops.InsertDefault(&values, &stored))
			{
				return false;
			}
			values.InsertOrAssign(key, static_cast<float>(key));
		}

		// 중복 키는 실패해야 한다(기존 값을 덮지 않는다).
		const int duplicate = 7;
		if (ops.InsertDefault(&values, &duplicate) || 64 != ops.GetSize(&values))
		{
			return false;
		}

		// 슬롯 커서로 전부 훑어 키/값이 짝을 유지하는지 본다.
		// 슬롯은 조밀하지 않으므로 방문 횟수와 Size() 가 같아야 통과다.
		std::size_t visited = 0;
		bool seen[64] = {};
		for (std::size_t slot = ops.BeginSlot(&values);
			InvalidTableSlot != slot;
			slot = ops.NextSlot(&values, slot))
		{
			const int   key   = *static_cast<const int*>(ops.GetKeyAt(&values, slot));
			const float value = *static_cast<const float*>(ops.GetConstValueAt(&values, slot));
			if (key < 0 || key >= 64 || seen[key] || value != static_cast<float>(key))
			{
				return false;
			}
			seen[key] = true;
			++visited;
		}
		if (64 != visited)
		{
			return false;
		}

		const int removed = 13;
		if (false == ops.RemoveKey(&values, &removed) || ops.ContainsKey(&values, &removed))
		{
			return false;
		}

		ops.Clear(&values);
		return 0 == ops.GetSize(&values) && InvalidTableSlot == ops.BeginSlot(&values);
	}

	bool TestObjectPoolDenseIndex()
	{
		TObjectPool<PoolProbe> pool;
		pool.Reserve(96);
		std::vector<PoolProbe*> objects;
		objects.reserve(96);
		for (int value = 0; value < 96; ++value)
		{
			PoolProbe* object = pool.Allocate(value);
			if (nullptr == object)
			{
				return false;
			}
			objects.push_back(object);
		}

		SafePtr<PoolProbe> retained = objects[17]->SafeFromThis();
		for (std::size_t index = 0; index < objects.size(); index += 3)
		{
			pool.Free(objects[index]);
		}

		std::size_t visited = 0;
		int sum = 0;
		bool seen[96] = {};
		bool validVisit = true;
		pool.ForEachLive([&](const PoolProbe& object)
		{
			if (object.Value < 0 || object.Value >= 96 || 0 == object.Value % 3 || seen[object.Value])
			{
				validVisit = false;
				return;
			}
			seen[object.Value] = true;
			++visited;
			sum += object.Value;
		});
		if (false == validVisit || 64 != visited || 64 != pool.GetLiveCount() || 3072 != sum)
		{
			return false;
		}
		for (int value = 0; value < 96; ++value)
		{
			if ((0 != value % 3) != seen[value])
			{
				return false;
			}
		}

		for (int value = 96; value < 128; ++value)
		{
			if (nullptr == pool.Allocate(value))
			{
				return false;
			}
		}
		if (96 != pool.GetLiveCount() || 96 != PoolProbe::Alive)
		{
			return false;
		}

		pool.Clear();
		return 0 == pool.GetLiveCount() && 0 == PoolProbe::Alive && false == retained.IsValid();
	}
}

int main()
{
	if (false == TestArray())
	{
		std::cerr << "Array smoke test failed.\n";
		return 1;
	}

	if (false == TestSimd128())
	{
		std::cerr << "SIMD128 smoke test failed.\n";
		return 2;
	}

	if (false == TestTable())
	{
		std::cerr << "Table smoke test failed.\n";
		return 3;
	}

	if (false == TestArrayReflectionOps())
	{
		std::cerr << "Array reflection ops smoke test failed.\n";
		return 4;
	}

	if (false == TestTableReflectionOps())
	{
		std::cerr << "Table reflection ops smoke test failed.\n";
		return 6;
	}

	if (false == TestObjectPoolDenseIndex())
	{
		std::cerr << "Object pool dense-index smoke test failed.\n";
		return 5;
	}

	std::cout << "Type system smoke test passed.\n";
	return 0;
}
