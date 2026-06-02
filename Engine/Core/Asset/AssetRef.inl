#pragma once

// AssetRef<T> 의 구현. IAssetManager 의 use-count API (AcquireAssetUseCount/ReleaseAssetUseCount)
// 가 완전 타입으로 보여야 하므로 이 inl 은 IAssetManager.h 를 include 한 다음 include 한다.

#include "Core/Asset/AssetRef.h"
#include "Core/Asset/IAssetManager.h"

template<typename T>
AssetRef<T>::AssetRef(SafePtr<IAssetManager> manager, const AssetGuid& guid, T* asset)
	: m_manager(manager)
	, m_guid(guid)
	, m_asset(asset)
{
	AcquireUseCount();
}

template<typename T>
AssetRef<T>::AssetRef(const AssetRef& rhs)
	: m_manager(rhs.m_manager)
	, m_guid(rhs.m_guid)
	, m_asset(rhs.m_asset)
{
	AcquireUseCount();
}

template<typename T>
AssetRef<T>& AssetRef<T>::operator=(const AssetRef& rhs)
{
	if (this == &rhs) return *this;
	ReleaseUseCount();
	m_manager = rhs.m_manager;
	m_guid    = rhs.m_guid;
	m_asset   = rhs.m_asset;
	AcquireUseCount();
	return *this;
}

template<typename T>
AssetRef<T>::AssetRef(AssetRef&& rhs) noexcept
	: m_manager(std::move(rhs.m_manager))
	, m_guid(std::move(rhs.m_guid))
	, m_asset(rhs.m_asset)
{
	rhs.m_asset = nullptr;
	// rhs.m_manager / m_guid 는 비워둠 — ReleaseUseCount 가 m_asset 비었으면 no-op.
}

template<typename T>
AssetRef<T>& AssetRef<T>::operator=(AssetRef&& rhs) noexcept
{
	if (this == &rhs) return *this;
	ReleaseUseCount();
	m_manager = std::move(rhs.m_manager);
	m_guid    = std::move(rhs.m_guid);
	m_asset   = rhs.m_asset;
	rhs.m_asset = nullptr;
	return *this;
}

template<typename T>
template<typename U>
requires std::is_convertible_v<U*, T*>
AssetRef<T>::AssetRef(const AssetRef<U>& rhs)
	: m_manager(rhs.m_manager)
	, m_guid(rhs.m_guid)
	, m_asset(rhs.m_asset)
{
	AcquireUseCount();
}

template<typename T>
template<typename U>
requires std::is_convertible_v<U*, T*>
AssetRef<T>& AssetRef<T>::operator=(const AssetRef<U>& rhs)
{
	ReleaseUseCount();
	m_manager = rhs.m_manager;
	m_guid    = rhs.m_guid;
	m_asset   = rhs.m_asset;
	AcquireUseCount();
	return *this;
}

template<typename T>
AssetRef<T>::~AssetRef()
{
	ReleaseUseCount();
}

template<typename T>
void AssetRef<T>::Reset() noexcept
{
	ReleaseUseCount();
	m_manager = nullptr;
	m_guid    = AssetGuid{};
	m_asset   = nullptr;
}

template<typename T>
void AssetRef<T>::AcquireUseCount()
{
	if (nullptr == m_asset) return;
	if (false == m_manager.IsValid()) return;
	m_manager->AcquireAssetUseCount(m_guid);
}

template<typename T>
void AssetRef<T>::ReleaseUseCount()
{
	if (nullptr == m_asset) return;
	if (m_manager.IsValid())
	{
		m_manager->ReleaseAssetUseCount(m_guid);
	}
	m_asset = nullptr;
}

template<typename T>
AssetRef<T> StaticAssetRefCast(const AssetRef<IAsset>& base)
{
	if (false == base.IsValid()) return AssetRef<T>();
	return AssetRef<T>(base.m_manager, base.m_guid, static_cast<T*>(base.m_asset));
}
