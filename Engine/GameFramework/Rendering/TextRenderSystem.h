#pragma once

#include "Core/Asset/AssetRef.h"
#include "Core/Asset/AssetTypes.h"
#include "GameFramework/System/GameSystem.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// 해석 함수가 스타일만 받으므로 필요하다. 정의는 Core/Asset/FontAsset.h (cpp 에서 포함).
enum class EFontStyle : std::uint8_t;

class CForward2DRenderer;
class CFontFaceAsset;
class CFontFamilyAsset;
class IAsset;
class IAssetManager;
class IRenderMaterial;
class IRenderMesh;
class IRenderer;
class IRHIDevice;
class IRHITexture;
class IRenderScene;
class Text2D;

class CTextRenderSystem final : public CGameSystem
{
public:
	explicit CTextRenderSystem(IRenderScene* renderScene = nullptr);
	~CTextRenderSystem() override;

	void SetRenderScene(IRenderScene* renderScene);
	void SetDependencies(IAssetManager* assetManager, IRHIDevice* rhiDevice, IRenderer* renderer,
		float pixelsPerUnit, const AssetGuid& defaultFamily, const std::vector<AssetGuid>& projectFallbacks);
	bool ShouldUpdateInEditMode() const override { return true; }
	void ClearCaches();
	bool TryGetLocalBounds(const Text2D& text, float& outCenterX, float& outCenterY, float& outWidth, float& outHeight) const;

protected:
	void OnUpdate(CGameCanvas& canvas) override;

private:
	struct GlyphInfo
	{
		std::uint32_t Page = 0;
		float U0 = 0.0f;
		float V0 = 0.0f;
		float U1 = 0.0f;
		float V1 = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;
		float BearingX = 0.0f;
		float BearingY = 0.0f;
	};

	struct AtlasPage
	{
		OwnerPtr<IRHITexture> Texture;
		std::uint32_t CursorX = 1;
		std::uint32_t CursorY = 1;
		std::uint32_t RowHeight = 0;
	};

	struct FaceCache
	{
		AssetRef<IAsset> Asset;
		void* FreeTypeFace = nullptr;
		void* HarfBuzzFont = nullptr;
		std::uint32_t Generation = 0;
		std::unordered_map<std::uint32_t, GlyphInfo> Glyphs;
		std::vector<AtlasPage> Pages;
	};

	struct CachedMesh
	{
		std::uint32_t Page = 0;
		AssetGuid FaceGuid = INVALID_ASSET_GUID;
		OwnerPtr<IRenderMesh> Mesh;
	};

	struct CachedText
	{
		std::size_t Signature = 0;
		std::vector<CachedMesh> Meshes;
		float Width = 0.0f;
		float Height = 0.0f;
		float CenterX = 0.0f;
		float CenterY = 0.0f;
		std::uint64_t LastSeenFrame = 0;
	};

	struct PositionedGlyph
	{
		AssetGuid FaceGuid = INVALID_ASSET_GUID;
		std::uint32_t GlyphId = 0;
		float X = 0.0f;
		float Y = 0.0f;
		float Advance = 0.0f;
		std::uint32_t Line = 0;
	};

	bool InitializeFontLibrary();
	void FinalizeFontLibrary();
	// 캐시된 face 중 자산 generation 이 재임포트로 바뀐 것이 있는지(폰트 핫리로드 감지).
	bool AnyFaceGenerationStale() const;
	FaceCache* AcquireFace(const AssetGuid& guid);
	// 해석은 (패밀리, 스타일) 만의 함수다 — Text2D 의 다른 필드는 쓰지 않는다.
	// 시그니처에서 Text2D 를 걷어낸 이유가 그것이고, 프레임 memo 가 성립하는 근거이기도 하다.
	bool ResolveFamilyFaces(const AssetGuid& family, EFontStyle style, std::vector<AssetGuid>& outFaces);
	bool AppendFamilyFaces(const AssetGuid& familyGuid, EFontStyle style, std::vector<AssetGuid>& outFaces,
		std::vector<AssetGuid>& visited);
	// 프레임 memo 를 거쳐 해석 결과를 얻는다. 해석 실패면 nullptr.
	// ⚠ 반환 포인터는 다음 AcquireResolvedFaces 호출까지만 유효하다(memo 벡터가 자랄 수 있다).
	const std::vector<AssetGuid>* AcquireResolvedFaces(const AssetGuid& family, EFontStyle style);
	bool BuildLayout(const Text2D& text, const std::vector<AssetGuid>& faces,
		std::vector<PositionedGlyph>& outGlyphs, float& outWidth, float& outHeight, float fontSizePixels);
	bool ShapeRun(const char* utf8, std::size_t length, const AssetGuid& faceGuid, std::uint32_t line,
		float scale, float& penX, float penY, std::vector<PositionedGlyph>& outGlyphs);
	bool EnsureGlyph(FaceCache& face, std::uint32_t glyphId, GlyphInfo& outGlyph);
	bool AllocateAtlasRect(FaceCache& face, std::uint32_t width, std::uint32_t height,
		std::uint32_t& outPage, std::uint32_t& outX, std::uint32_t& outY);
	bool RebuildText(Text2D& text, const std::vector<AssetGuid>& faces, CachedText& cache,
		CForward2DRenderer& renderer);
	OwnerPtr<IRenderMesh> CreateMesh(const std::vector<float>& vertices, const std::vector<std::uint32_t>& indices) const;
	std::size_t BuildSignature(const Text2D& text, const AssetGuid& effectiveFamily) const;
	AssetGuid GetEffectiveFamily(const Text2D& text) const;
	OwnerPtr<IRenderMaterial>& AcquireMaterial(const AssetGuid& faceGuid, std::uint32_t page, CForward2DRenderer& renderer);

private:
	IRenderScene* m_renderScene = nullptr;
	IAssetManager* m_assetManager = nullptr;
	IRHIDevice* m_rhiDevice = nullptr;
	CForward2DRenderer* m_renderer = nullptr;
	float m_pixelsPerUnit = 100.0f;
	AssetGuid m_defaultFamily = INVALID_ASSET_GUID;
	std::vector<AssetGuid> m_projectFallbacks;
	void* m_freeTypeLibrary = nullptr;
	std::unordered_map<AssetGuid, FaceCache> m_faces;
	std::unordered_map<const void*, CachedText> m_textCache;
	// 값 = 마지막으로 본 프레임 스탬프. 집합이 아니라 맵인 이유는 m_textCache 와 같은 방식으로
	// 청소하기 위해서다(별도 seen 집합을 매 프레임 만들지 않으려고).
	std::unordered_map<const void*, std::uint64_t> m_warnedMissingFonts;
	std::unordered_map<std::string, OwnerPtr<IRenderMaterial>> m_materials;
	std::uint64_t m_frameStamp = 0;
	// 매 프레임 텍스트마다 vector 를 새로 만들지 않으려고 재사용하는 작업 버퍼.
	// 용량을 유지한 채 clear 만 하므로 워밍업 이후 할당이 없다.
	std::vector<AssetGuid> m_faceVisitedScratch;

	// 한 프레임 안에서 (패밀리, 스타일) → 해석된 face 목록을 기억한다.
	// 폰트 해석은 패밀리마다 자산 매니저를 타는데(폴백 체인까지 전부, 호출마다 뮤텍스),
	// 보통 한 캔버스의 텍스트가 전부 같은 패밀리를 쓴다. 그래서 텍스트 수만큼 반복하던 것이
	// 서로 다른 패밀리 수만큼으로 줄어든다.
	//
	// 항목이 한두 개에 그치므로 해시맵이 아니라 선형 탐색이 맞다. 프레임마다 비우는 대신
	// m_familyResolveCount 를 0 으로 되돌려 슬롯과 그 안의 vector 용량을 재사용한다.
	struct ResolvedFamily
	{
		AssetGuid Family = INVALID_ASSET_GUID;
		EFontStyle Style{};
		bool Resolved = false;
		std::vector<AssetGuid> Faces;
	};
	std::vector<ResolvedFamily> m_familyResolveMemo;
	std::size_t m_familyResolveCount = 0;
};
