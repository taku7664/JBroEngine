#pragma once

#include "Core/Asset/FileAsset.h"

// ─────────────────────────────────────────────────────────────────────────────
//  CCanvasAsset — 캔버스 파일(`.jcanvas`) 자산.
//
//  내용은 CFileAsset 과 똑같이 파일 바이트 그대로다(GetText 로 YAML 을 꺼내 쓴다).
//  존재 이유는 **신원**이다 — 별개 타입이라야 `Ref<CCanvasAsset>` 이 캔버스만 가리킬 수
//  있다. Ref 의 에셋 해석은 `dynamic_cast<T*>` 라(Ref.h), CFileAsset 으로 두면 `.jlayer`
//  든 셰이더든 전부 같은 타입이라 해석이 통과해 버린다.
//
//  파싱은 여기서 하지 않는다. 캔버스 YAML 을 읽는 건 CCanvasSerializer 의 일이고, 자산
//  계층은 바이트만 나른다(로더가 yaml-cpp 를 끌어오면 게임 DLL 링크가 깨진다).
// ─────────────────────────────────────────────────────────────────────────────
class CCanvasAsset final : public CFileAsset
{
public:
	using CFileAsset::CFileAsset;

	// Ref<T> 가 RTTI 없이 타입을 확인하는 컴파일타임 짝 — 로더 등록이 지키는
	// "이 타입은 이 클래스" 계약을 코드로 적어 둔 것이다.
	static constexpr EAssetType StaticAssetType() { return EAssetType::Canvas; }
};

// 파일 읽기는 CFileAssetLoader 그대로 쓰고 만들 타입만 바꾼다.
class CCanvasAssetLoader final : public CFileAssetLoader
{
public:
	CCanvasAssetLoader();

protected:
	OwnerPtr<IAsset> CreateAsset(const AssetMetaData& metaData, std::vector<std::uint8_t>&& data) const override;
};
