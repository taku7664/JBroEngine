#pragma once

#include <type_traits>

template<typename T>
class SafePtr;

template<typename T>
class OwnerPtr;

template<typename T>
class EnableSafeFromThis;

namespace SafePtrDetail
{
	struct ControlBlock
	{
		void* Ptr;
		bool Alive;
		std::size_t SafeCount;
		void(*Deleter)(void*);

		ControlBlock(void* ptr, void(*deleter)(void*))
			: Ptr(ptr), Alive(ptr != nullptr), SafeCount(0), Deleter(deleter)
		{
		}
	};

	template<typename T>
	inline void DeletePtr(void* ptr)
	{
		delete static_cast<T*>(ptr);
	}

	template<typename TObject>
	inline void BindSafeFromThisControlBlock(TObject* ptr, ControlBlock* block)
	{
		if constexpr (requires(TObject* obj, ControlBlock* cb) { obj->__BindSafeControlBlock(cb); })
		{
			ptr->__BindSafeControlBlock(block);
		}
	}
}

template<typename T>
class EnableSafeFromThis
{
	template<typename>
	friend class OwnerPtr;

public:
	SafePtr<T> SafeFromThis()
	{
		if (nullptr == m_controlBlock)
		{
			return nullptr;
		}

		return SafePtr<T>(m_controlBlock);
	}

	SafePtr<const T> SafeFromThis() const
	{
		if (nullptr == m_controlBlock)
		{
			return nullptr;
		}

		return SafePtr<const T>(m_controlBlock);
	}

private:
	void __BindSafeControlBlock(SafePtrDetail::ControlBlock* block)
	{
		m_controlBlock = block;
	}

private:
	SafePtrDetail::ControlBlock* m_controlBlock = nullptr;
};

template<typename T>
class OwnerPtr final
{
	template<typename>
	friend class OwnerPtr;
	template<typename>
	friend class SafePtr;

public:
	OwnerPtr() : m_controlBlock(nullptr) {}
	OwnerPtr(std::nullptr_t) : m_controlBlock(nullptr) {}
	explicit OwnerPtr(T* ptr)
		: m_controlBlock(ptr ? new SafePtrDetail::ControlBlock(ptr, &SafePtrDetail::DeletePtr<T>) : nullptr)
	{
		if (ptr && m_controlBlock)
		{
			SafePtrDetail::BindSafeFromThisControlBlock(ptr, m_controlBlock);
		}
	}

	OwnerPtr(const OwnerPtr&) = delete;
	OwnerPtr& operator=(const OwnerPtr&) = delete;

	OwnerPtr(OwnerPtr&& rhs) noexcept
		: m_controlBlock(rhs.m_controlBlock)
	{
		rhs.m_controlBlock = nullptr;
	}
	OwnerPtr& operator=(OwnerPtr&& rhs) noexcept
	{
		if (this == &rhs)
			return *this;

		DestroyOwned();
		m_controlBlock = rhs.m_controlBlock;
		rhs.m_controlBlock = nullptr;
		return *this;
	}

	template<typename U>
	requires std::is_convertible_v<U*, T*>
	OwnerPtr(OwnerPtr<U>&& rhs) noexcept
		: m_controlBlock(rhs.m_controlBlock)
	{
		rhs.m_controlBlock = nullptr;
	}

	template<typename U>
	requires std::is_convertible_v<U*, T*>
	OwnerPtr& operator=(OwnerPtr<U>&& rhs) noexcept
	{
		DestroyOwned();
		m_controlBlock = rhs.m_controlBlock;
		rhs.m_controlBlock = nullptr;
		return *this;
	}

	~OwnerPtr()
	{
		DestroyOwned();
	}

	void Reset()
	{
		DestroyOwned();
	}
	void Reset(T* ptr)
	{
		DestroyOwned();
		if (ptr)
		{
			m_controlBlock = new SafePtrDetail::ControlBlock(ptr, &SafePtrDetail::DeletePtr<T>);
			SafePtrDetail::BindSafeFromThisControlBlock(ptr, m_controlBlock);
		}
	}

	T* Get() const
	{
		return (m_controlBlock && m_controlBlock->Alive) ? static_cast<T*>(m_controlBlock->Ptr) : nullptr;
	}

	T& operator*() const { return *Get(); }
	T* operator->() const { return Get(); }
	explicit operator bool() const { return Get() != nullptr; }

	SafePtr<T> GetSafePtr() const
	{
		return SafePtr<T>(m_controlBlock);
	}
	operator SafePtr<T>() const { return GetSafePtr(); }

private:
	void DestroyOwned()
	{
		if (!m_controlBlock)
			return;

		if (m_controlBlock->Alive && m_controlBlock->Ptr)
		{
			m_controlBlock->Deleter(m_controlBlock->Ptr);
			m_controlBlock->Ptr = nullptr;
			m_controlBlock->Alive = false;
		}

		if (m_controlBlock->SafeCount == 0)
		{
			delete m_controlBlock;
		}

		m_controlBlock = nullptr;
	}

private:
	SafePtrDetail::ControlBlock* m_controlBlock;
};

template<typename T>
class SafePtr final
{
	template<typename>
	friend class SafePtr;
	template<typename>
	friend class OwnerPtr;
	template<typename>
	friend class EnableSafeFromThis;

	template<typename To, typename From>
	friend SafePtr<To> StaticSafePtrCast(const SafePtr<From>& ptr);

	template<typename To, typename From>
	friend SafePtr<To> DynamicSafePtrCast(const SafePtr<From>& ptr);

public:
	SafePtr() : m_controlBlock(nullptr) {}
	SafePtr(std::nullptr_t) : m_controlBlock(nullptr) {}

	SafePtr(const SafePtr& rhs)
		: m_controlBlock(rhs.m_controlBlock)
	{
		AddRef();
	}
	SafePtr& operator=(const SafePtr& rhs)
	{
		if (this == &rhs)
			return *this;

		ReleaseRef();
		m_controlBlock = rhs.m_controlBlock;
		AddRef();
		return *this;
	}

	template<typename U>
	requires std::is_convertible_v<U*, T*>
	SafePtr(const SafePtr<U>& rhs)
		: m_controlBlock(rhs.m_controlBlock)
	{
		AddRef();
	}
	template<typename U>
	requires std::is_convertible_v<U*, T*>
	SafePtr& operator=(const SafePtr<U>& rhs)
	{
		if (m_controlBlock == rhs.m_controlBlock)
			return *this;

		ReleaseRef();
		m_controlBlock = rhs.m_controlBlock;
		AddRef();
		return *this;
	}

	SafePtr(SafePtr&& rhs) noexcept
		: m_controlBlock(rhs.m_controlBlock)
	{
		rhs.m_controlBlock = nullptr;
	}
	SafePtr& operator=(SafePtr&& rhs) noexcept
	{
		if (this == &rhs)
			return *this;

		ReleaseRef();
		m_controlBlock = rhs.m_controlBlock;
		rhs.m_controlBlock = nullptr;
		return *this;
	}

	~SafePtr()
	{
		ReleaseRef();
	}

	void Reset()
	{
		ReleaseRef();
		m_controlBlock = nullptr;
	}

	bool IsValid() const
	{
		return m_controlBlock != nullptr && m_controlBlock->Alive;
	}

	T* TryGet() const
	{
		return IsValid() ? static_cast<T*>(m_controlBlock->Ptr) : nullptr;
	}

	T& operator*() const { return *TryGet(); }
	T* operator->() const { return TryGet(); }
	explicit operator bool() const { return IsValid(); }

private:
	explicit SafePtr(SafePtrDetail::ControlBlock* block)
		: m_controlBlock(block)
	{
		AddRef();
	}

	void AddRef()
	{
		if (m_controlBlock)
			++m_controlBlock->SafeCount;
	}

	void ReleaseRef()
	{
		if (!m_controlBlock)
			return;

		--m_controlBlock->SafeCount;

		if (m_controlBlock->SafeCount == 0 && !m_controlBlock->Alive)
		{
			delete m_controlBlock;
		}

		m_controlBlock = nullptr;
	}

private:
	SafePtrDetail::ControlBlock* m_controlBlock;
};

template<typename T, typename... Args>
OwnerPtr<T> MakeOwnerPtr(Args&&... args)
{
	return OwnerPtr<T>(new T(std::forward<Args>(args)...));
}

template<typename To, typename From>
SafePtr<To> StaticSafePtrCast(const SafePtr<From>& ptr)
{
	if (!ptr.IsValid())
	{
		return nullptr;
	}

	static_cast<To*>(ptr.TryGet());

	return SafePtr<To>(ptr.m_controlBlock);
}

template<typename To, typename From>
SafePtr<To> DynamicSafePtrCast(const SafePtr<From>& ptr)
{
	if (!ptr.IsValid())
	{
		return nullptr;
	}

	To* casted = dynamic_cast<To*>(ptr.TryGet());
	if (nullptr == casted)
	{
		return nullptr;
	}

	return SafePtr<To>(ptr.m_controlBlock);
}
