#pragma once

#include "Core/Asset/FileAsset.h"

// ─────────────────────────────────────────────────────────────────────────────
//  CLayerAsset — 레이어 파일(`.jlayer`) 자산. CCanvasAsset 의 짝이다.
//
//  내용은 CFileAsset 과 똑같이 파일 바이트 그대로다(GetText 로 YAML 을 꺼내 쓴다).
//  존재 이유는 **신원**이다 — 별개 타입이라야 `Ref<CLayerAsset>` 이 레이어만 가리킬 수
//  있다. CFileAsset 은 Prefab/Layer/Shader/Script 가 공유하므로 StaticAssetType 이 없고,
//  그래서 `Ref<CFileAsset>` 은 아예 컴파일되지 않는다(아무 자산이나 그 타입으로 내리는 걸
//  막는 계약). 스크립트가 "이 레이어를 런타임에 로드해라"를 저작하려면 이 타입이 필요하다 —
//  경로 문자열로 지목하면 파일을 옮기거나 이름만 바꿔도 조용히 깨진다(전환의 CCanvasAsset 과
//  같은 논리다).
//
//  파싱은 여기서 하지 않는다. 레이어 YAML 을 읽는 건 LayerSerializer 의 일이고, 자산 계층은
//  바이트만 나른다(로더가 yaml-cpp 를 끌어오면 게임 DLL 링크가 깨진다).
// ─────────────────────────────────────────────────────────────────────────────
class CLayerAsset final : public CFileAsset
{
public:
	using CFileAsset::CFileAsset;

	// Ref<T> 가 RTTI 없이 타입을 확인하는 컴파일타임 짝 — 로더 등록이 지키는
	// "이 타입은 이 클래스" 계약을 코드로 적어 둔 것이다.
	static constexpr EAssetType StaticAssetType() { return EAssetType::Layer; }
};

// 파일 읽기는 CFileAssetLoader 그대로 쓰고 만들 타입만 바꾼다.
class CLayerAssetLoader final : public CFileAssetLoader
{
public:
	CLayerAssetLoader();

protected:
	OwnerPtr<IAsset> CreateAsset(const AssetMetaData& metaData, std::vector<std::uint8_t>&& data) const override;
};
