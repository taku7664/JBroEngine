#pragma once

#include "Core/Asset/FileAsset.h"

// ─────────────────────────────────────────────────────────────────────────────
//  CPrefabAsset — 프리팹 파일(`.jprefab`) 자산.
//
//  CCanvasAsset 과 같은 이유로 존재한다 — **신원**이다. 내용은 CFileAsset 과 똑같은
//  파일 바이트지만(GetText 로 YAML 을 꺼낸다), 별개 타입이라야 `Ref<CPrefabAsset>` 이
//  프리팹만 가리킬 수 있다. Ref 의 에셋 해석은 StaticAssetType() 짝맞춤이라(Ref.h),
//  CFileAsset 으로 두면 `.jlayer`·셰이더·스크립트가 전부 같은 타입이라 해석이 통과한다.
//
//  스크립트가 스폰 대상을 지목하는 방법이기도 하다. `Asset`(File::Guid = fs::path 파생)
//  으로 들면 호스트↔게임 DLL 경계 POD 규칙을 깨지만, RefBase 는 고정 크기 char 버퍼라
//  경계를 넘어도 안전하다.
//
//  파싱은 여기서 하지 않는다. 프리팹 YAML 을 읽는 건 Serialization 계층의 일이고, 자산
//  계층은 바이트만 나른다(로더가 yaml-cpp 를 끌어오면 게임 DLL 링크가 깨진다).
// ─────────────────────────────────────────────────────────────────────────────
class CPrefabAsset final : public CFileAsset
{
public:
	using CFileAsset::CFileAsset;

	// Ref<T> 가 RTTI 없이 타입을 확인하는 컴파일타임 짝 — 로더 등록이 지키는
	// "이 타입은 이 클래스" 계약을 코드로 적어 둔 것이다.
	static constexpr EAssetType StaticAssetType() { return EAssetType::Prefab; }
};

// 파일 읽기는 CFileAssetLoader 그대로 쓰고 만들 타입만 바꾼다.
class CPrefabAssetLoader final : public CFileAssetLoader
{
public:
	CPrefabAssetLoader();

protected:
	OwnerPtr<IAsset> CreateAsset(const AssetMetaData& metaData, std::vector<std::uint8_t>&& data) const override;
};
