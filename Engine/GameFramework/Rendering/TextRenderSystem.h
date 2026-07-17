#pragma once

#include "Core/Asset/AssetRef.h"
#include "Core/Asset/AssetTypes.h"
#include "GameFramework/System/GameSystem.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
	void OnUpdate(CGameCanvas& scene) override;

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
	bool ResolveFamilyFaces(const Text2D& text, std::vector<AssetGuid>& outFaces);
	bool AppendFamilyFaces(const AssetGuid& familyGuid, const Text2D& text, std::vector<AssetGuid>& outFaces,
		std::vector<AssetGuid>& visited);
	bool BuildLayout(const Text2D& text, const std::vector<AssetGuid>& faces,
		std::vector<PositionedGlyph>& outGlyphs, float& outWidth, float& outHeight, float fontSizePixels);
	bool ShapeRun(const char* utf8, std::size_t length, const AssetGuid& faceGuid, std::uint32_t line,
		float scale, float& penX, float penY, std::vector<PositionedGlyph>& outGlyphs);
	bool EnsureGlyph(FaceCache& face, std::uint32_t glyphId, GlyphInfo& outGlyph);
	bool AllocateAtlasRect(FaceCache& face, std::uint32_t width, std::uint32_t height,
		std::uint32_t& outPage, std::uint32_t& outX, std::uint32_t& outY);
	bool RebuildText(Text2D& text, CachedText& cache, CForward2DRenderer& renderer);
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
	std::unordered_set<const void*> m_warnedMissingFonts;
	std::unordered_map<std::string, OwnerPtr<IRenderMaterial>> m_materials;
};
