#include "pch.h"
#include "TextRenderSystem.h"

#include "Core/Asset/FontAsset.h"
#include "Core/Asset/IAssetManager.h"
#include "Core/Logging/LoggerInternal.h"
#include "Core/Renderer/Forward2DRenderer.h"
#include "Core/Renderer/IRenderMaterial.h"
#include "Core/Renderer/IRenderMesh.h"
#include "Core/Renderer/IRenderScene.h"
#include "Core/Renderer/RenderResources2D.h"
#include "Core/RHI/IRHIBuffer.h"
#include "Core/RHI/IRHIDevice.h"
#include "GameFramework/Component/Text2D.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Scene/Scene.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <unordered_set>

namespace
{
	constexpr std::uint32_t ATLAS_SIZE = 1024;
	constexpr std::uint32_t GLYPH_PIXEL_SIZE = 64;

	void HashCombine(std::size_t& seed, std::size_t value)
	{
		seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
	}

	std::string MaterialKey(const AssetGuid& guid, std::uint32_t page)
	{
		return guid.generic_string() + "#" + std::to_string(page);
	}

	bool IsVisibleColor(bool enabled, const float* color)
	{
		return enabled && color[3] > 0.0f;
	}

	std::uint32_t DecodeOneUtf8(const char* text, std::size_t length, std::size_t& cursor)
	{
		if (cursor >= length) return 0;
		const unsigned char first = static_cast<unsigned char>(text[cursor++]);
		if (first < 0x80) return first;
		std::uint32_t value = 0xfffdu;
		int continuationCount = 0;
		if ((first & 0xe0) == 0xc0) { value = first & 0x1f; continuationCount = 1; }
		else if ((first & 0xf0) == 0xe0) { value = first & 0x0f; continuationCount = 2; }
		else if ((first & 0xf8) == 0xf0) { value = first & 0x07; continuationCount = 3; }
		else return 0xfffdu;
		for (int i = 0; i < continuationCount; ++i)
		{
			if (cursor >= length) return 0xfffdu;
			const unsigned char next = static_cast<unsigned char>(text[cursor]);
			if ((next & 0xc0) != 0x80) return 0xfffdu;
			++cursor;
			value = (value << 6) | (next & 0x3f);
		}
		return value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu) ? 0xfffdu : value;
	}

	bool IsCombiningMark(std::uint32_t codepoint)
	{
		return (codepoint >= 0x0300u && codepoint <= 0x036fu)
			|| (codepoint >= 0x1ab0u && codepoint <= 0x1affu)
			|| (codepoint >= 0x1dc0u && codepoint <= 0x1dffu)
			|| (codepoint >= 0xfe20u && codepoint <= 0xfe2fu);
	}

	bool IsHangulJamo(std::uint32_t codepoint)
	{
		return (codepoint >= 0x1100u && codepoint <= 0x11ffu)
			|| (codepoint >= 0x3130u && codepoint <= 0x318fu)
			|| (codepoint >= 0xa960u && codepoint <= 0xa97fu)
			|| (codepoint >= 0xd7b0u && codepoint <= 0xd7ffu);
	}
}

CTextRenderSystem::CTextRenderSystem(IRenderScene* renderScene) : m_renderScene(renderScene)
{
	InitializeFontLibrary();
}

CTextRenderSystem::~CTextRenderSystem()
{
	ClearCaches();
	FinalizeFontLibrary();
}

void CTextRenderSystem::SetRenderScene(IRenderScene* renderScene) { m_renderScene = renderScene; }

void CTextRenderSystem::SetDependencies(IAssetManager* assetManager, IRHIDevice* rhiDevice, IRenderer* renderer,
	float pixelsPerUnit, const AssetGuid& defaultFamily, const std::vector<AssetGuid>& projectFallbacks)
{
	if (m_assetManager != assetManager || m_rhiDevice != rhiDevice
		|| m_defaultFamily != defaultFamily || m_projectFallbacks != projectFallbacks)
	{
		ClearCaches();
	}
	m_assetManager = assetManager;
	m_rhiDevice = rhiDevice;
	m_renderer = renderer;
	m_pixelsPerUnit = pixelsPerUnit > 0.0f ? pixelsPerUnit : 100.0f;
	m_defaultFamily = defaultFamily;
	m_projectFallbacks = projectFallbacks;
}

bool CTextRenderSystem::InitializeFontLibrary()
{
	if (m_freeTypeLibrary) return true;
	FT_Library library = nullptr;
	if (0 != FT_Init_FreeType(&library)) return false;
	m_freeTypeLibrary = library;
	return true;
}

void CTextRenderSystem::FinalizeFontLibrary()
{
	if (m_freeTypeLibrary)
	{
		FT_Done_FreeType(static_cast<FT_Library>(m_freeTypeLibrary));
		m_freeTypeLibrary = nullptr;
	}
}

void CTextRenderSystem::ClearCaches()
{
	m_materials.clear();
	m_warnedMissingFonts.clear();
	m_textCache.clear();
	for (auto& pair : m_faces)
	{
		FaceCache& cache = pair.second;
		if (cache.HarfBuzzFont) hb_font_destroy(static_cast<hb_font_t*>(cache.HarfBuzzFont));
		if (cache.FreeTypeFace) FT_Done_Face(static_cast<FT_Face>(cache.FreeTypeFace));
	}
	m_faces.clear();
}

bool CTextRenderSystem::TryGetLocalBounds(const Text2D& text, float& outCenterX, float& outCenterY, float& outWidth, float& outHeight) const
{
	const auto found = m_textCache.find(&text);
	if (found == m_textCache.end()) return false;
	outCenterX = found->second.CenterX;
	outCenterY = found->second.CenterY;
	outWidth = found->second.Width;
	outHeight = found->second.Height;
	return true;
}

CTextRenderSystem::FaceCache* CTextRenderSystem::AcquireFace(const AssetGuid& guid)
{
	if (guid.IsNull() || nullptr == m_assetManager || false == InitializeFontLibrary()) return nullptr;
	auto found = m_faces.find(guid);
	if (found != m_faces.end()) return &found->second;
	AssetRef<IAsset> asset = m_assetManager->LoadAsset(guid);
	if (false == asset.IsValid() || EAssetType::FontFace != asset->GetAssetType()) return nullptr;
	CFontFaceAsset* fontAsset = static_cast<CFontFaceAsset*>(asset.Get());
	const std::vector<std::uint8_t>& bytes = fontAsset->GetBytes();
	FT_Face face = nullptr;
	if (bytes.empty() || 0 != FT_New_Memory_Face(static_cast<FT_Library>(m_freeTypeLibrary), bytes.data(),
		static_cast<FT_Long>(bytes.size()), static_cast<FT_Long>(fontAsset->GetFaceIndex()), &face))
	{
		return nullptr;
	}
	FT_Select_Charmap(face, FT_ENCODING_UNICODE);
	FT_Set_Pixel_Sizes(face, 0, GLYPH_PIXEL_SIZE);
	hb_font_t* hbFont = hb_ft_font_create_referenced(face);
	if (nullptr == hbFont)
	{
		FT_Done_Face(face);
		return nullptr;
	}
	hb_font_set_scale(hbFont, GLYPH_PIXEL_SIZE * 64, GLYPH_PIXEL_SIZE * 64);
	FaceCache cache;
	cache.Asset = std::move(asset);
	cache.FreeTypeFace = face;
	cache.HarfBuzzFont = hbFont;
	cache.Generation = fontAsset->GetGeneration();
	auto inserted = m_faces.emplace(guid, std::move(cache));
	return &inserted.first->second;
}

AssetGuid CTextRenderSystem::GetEffectiveFamily(const Text2D& text) const
{
	return text.FontFamilyGuid.IsNull() ? m_defaultFamily : text.FontFamilyGuid;
}

bool CTextRenderSystem::AppendFamilyFaces(const AssetGuid& familyGuid, const Text2D& text,
	std::vector<AssetGuid>& outFaces, std::vector<AssetGuid>& visited)
{
	if (familyGuid.IsNull() || nullptr == m_assetManager || std::find(visited.begin(), visited.end(), familyGuid) != visited.end()) return false;
	visited.push_back(familyGuid);
	AssetRef<IAsset> asset = m_assetManager->LoadAsset(familyGuid);
	if (false == asset.IsValid() || EAssetType::FontFamily != asset->GetAssetType()) return false;
	const CFontFamilyAsset& family = *static_cast<CFontFamilyAsset*>(asset.Get());
	bool regularFallback = false;
	const AssetGuid faceGuid = family.ResolveFace(text.FontStyle, regularFallback);
	if (false == faceGuid.IsNull() && std::find(outFaces.begin(), outFaces.end(), faceGuid) == outFaces.end()) outFaces.push_back(faceGuid);
	for (const AssetGuid& fallback : family.GetData().FallbackFamilies) AppendFamilyFaces(fallback, text, outFaces, visited);
	if (family.GetData().UseProjectFallbacks)
	{
		for (const AssetGuid& fallback : m_projectFallbacks) AppendFamilyFaces(fallback, text, outFaces, visited);
	}
	return false == outFaces.empty();
}

bool CTextRenderSystem::ResolveFamilyFaces(const Text2D& text, std::vector<AssetGuid>& outFaces)
{
	outFaces.clear();
	std::vector<AssetGuid> visited;
	return AppendFamilyFaces(GetEffectiveFamily(text), text, outFaces, visited);
}

bool CTextRenderSystem::ShapeRun(const char* utf8, std::size_t length, const AssetGuid& faceGuid,
	std::uint32_t line, float scale, float& penX, float penY, std::vector<PositionedGlyph>& outGlyphs)
{
	FaceCache* face = AcquireFace(faceGuid);
	if (nullptr == face || nullptr == face->HarfBuzzFont || 0 == length) return false;
	hb_buffer_t* buffer = hb_buffer_create();
	hb_buffer_add_utf8(buffer, utf8, static_cast<int>(length), 0, static_cast<int>(length));
	hb_buffer_guess_segment_properties(buffer);
	hb_shape(static_cast<hb_font_t*>(face->HarfBuzzFont), buffer, nullptr, 0);
	unsigned int count = 0;
	hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
	hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
	for (unsigned int i = 0; i < count; ++i)
	{
		PositionedGlyph glyph;
		glyph.FaceGuid = faceGuid;
		glyph.GlyphId = infos[i].codepoint;
		glyph.X = penX + static_cast<float>(positions[i].x_offset) / 64.0f * scale;
		glyph.Y = penY + static_cast<float>(positions[i].y_offset) / 64.0f * scale;
		glyph.Advance = static_cast<float>(positions[i].x_advance) / 64.0f * scale;
		glyph.Line = line;
		outGlyphs.push_back(glyph);
		penX += glyph.Advance;
	}
	hb_buffer_destroy(buffer);
	return true;
}

bool CTextRenderSystem::BuildLayout(const Text2D& text, const std::vector<AssetGuid>& faces,
	std::vector<PositionedGlyph>& outGlyphs, float& outWidth, float& outHeight, float fontSizePixels)
{
	outGlyphs.clear(); outWidth = 0.0f; outHeight = 0.0f;
	if (faces.empty()) return false;
	const std::string normalized = [&]()
	{
		std::string result;
		result.reserve(text.Text.size());
		for (std::size_t i = 0; i < text.Text.size(); ++i)
		{
			if ('\r' == text.Text[i])
			{
				if (i + 1 < text.Text.size() && '\n' == text.Text[i + 1]) ++i;
				result.push_back('\n');
			}
			else result.push_back(text.Text[i]);
		}
		return result;
	}();
	const float scale = fontSizePixels / static_cast<float>(GLYPH_PIXEL_SIZE);
	const float lineHeight = fontSizePixels * std::max(0.01f, text.LineSpacing);
	const float maxWidth = text.WidthPixels > 0.0f ? text.WidthPixels : std::numeric_limits<float>::max();
	float penX = 0.0f;
	float penY = 0.0f;
	std::uint32_t line = 0;
	std::vector<float> lineWidths(1, 0.0f);
	std::size_t cursor = 0;
	while (cursor < normalized.size())
	{
		if ('\n' == normalized[cursor])
		{
			lineWidths[line] = penX;
			penX = 0.0f; penY -= lineHeight; ++line; lineWidths.push_back(0.0f); ++cursor; continue;
		}
		const std::size_t codepointStart = cursor;
		const std::uint32_t codepoint = DecodeOneUtf8(normalized.data(), normalized.size(), cursor);
		AssetGuid selectedFace = faces.front();
		for (const AssetGuid& candidate : faces)
		{
			FaceCache* face = AcquireFace(candidate);
			if (face && 0 != FT_Get_Char_Index(static_cast<FT_Face>(face->FreeTypeFace), codepoint))
			{
				selectedFace = candidate;
				break;
			}
		}
		// HarfBuzz must see a grapheme sequence together. In particular, decomposed Latin
		// marks and Hangul Jamo cannot be shaped one codepoint at a time.
		while (cursor < normalized.size() && normalized[cursor] != '\n')
		{
			std::size_t nextCursor = cursor;
			const std::uint32_t nextCodepoint = DecodeOneUtf8(normalized.data(), normalized.size(), nextCursor);
			if (false == IsCombiningMark(nextCodepoint) && false == (IsHangulJamo(codepoint) && IsHangulJamo(nextCodepoint))) break;
			FaceCache* selected = AcquireFace(selectedFace);
			if (!selected || 0 == FT_Get_Char_Index(static_cast<FT_Face>(selected->FreeTypeFace), nextCodepoint)) break;
			cursor = nextCursor;
		}
		float probeX = penX;
		std::vector<PositionedGlyph> shaped;
		ShapeRun(normalized.data() + codepointStart, cursor - codepointStart, selectedFace, line, scale, probeX, penY, shaped);
		const float advance = probeX - penX + text.LetterSpacingPixels;
		if (ETextOverflowMode::Overflow != text.OverflowMode && penX > 0.0f && penX + advance > maxWidth)
		{
			lineWidths[line] = penX;
			penX = 0.0f; penY -= lineHeight; ++line; lineWidths.push_back(0.0f);
			shaped.clear();
			probeX = 0.0f;
			ShapeRun(normalized.data() + codepointStart, cursor - codepointStart, selectedFace, line, scale, probeX, penY, shaped);
		}
		for (PositionedGlyph& glyph : shaped) outGlyphs.push_back(glyph);
		penX = probeX + text.LetterSpacingPixels;
		lineWidths[line] = penX;
	}
	outWidth = 0.0f;
	for (float width : lineWidths) outWidth = std::max(outWidth, width);
	outHeight = lineHeight * static_cast<float>(line + 1);
	for (PositionedGlyph& glyph : outGlyphs)
	{
		const float lineWidth = lineWidths[glyph.Line];
		if (ETextHorizontalAlignment::Center == text.HorizontalAlignment) glyph.X += (outWidth - lineWidth) * 0.5f;
		else if (ETextHorizontalAlignment::Right == text.HorizontalAlignment) glyph.X += outWidth - lineWidth;
	}
	return true;
}

bool CTextRenderSystem::AllocateAtlasRect(FaceCache& face, std::uint32_t width, std::uint32_t height,
	std::uint32_t& outPage, std::uint32_t& outX, std::uint32_t& outY)
{
	if (width + 2 > ATLAS_SIZE || height + 2 > ATLAS_SIZE || nullptr == m_rhiDevice) return false;
	for (std::uint32_t pageIndex = 0; pageIndex <= face.Pages.size(); ++pageIndex)
	{
		if (pageIndex == face.Pages.size())
		{
			std::vector<std::uint8_t> clear(static_cast<std::size_t>(ATLAS_SIZE) * ATLAS_SIZE * 4, 0);
			RHITexture2DDesc desc; desc.Width = ATLAS_SIZE; desc.Height = ATLAS_SIZE;
			AtlasPage page; page.Texture = m_rhiDevice->CreateTexture2D(desc, clear.data());
			if (!page.Texture) return false;
			face.Pages.push_back(std::move(page));
		}
		AtlasPage& page = face.Pages[pageIndex];
		if (page.CursorX + width + 1 > ATLAS_SIZE)
		{
			page.CursorX = 1; page.CursorY += page.RowHeight + 1; page.RowHeight = 0;
		}
		if (page.CursorY + height + 1 > ATLAS_SIZE) continue;
		outPage = pageIndex; outX = page.CursorX; outY = page.CursorY;
		page.CursorX += width + 1; page.RowHeight = std::max(page.RowHeight, height);
		return true;
	}
	return false;
}

bool CTextRenderSystem::EnsureGlyph(FaceCache& face, std::uint32_t glyphId, GlyphInfo& outGlyph)
{
	if (auto found = face.Glyphs.find(glyphId); found != face.Glyphs.end()) { outGlyph = found->second; return true; }
	FT_Face ftFace = static_cast<FT_Face>(face.FreeTypeFace);
	if (nullptr == ftFace || 0 != FT_Load_Glyph(ftFace, glyphId, FT_LOAD_DEFAULT)) return false;
	if (0 != FT_Render_Glyph(ftFace->glyph, FT_RENDER_MODE_SDF)) return false;
	const FT_Bitmap& bitmap = ftFace->glyph->bitmap;
	if (0 == bitmap.width || 0 == bitmap.rows)
	{
		GlyphInfo empty; face.Glyphs.emplace(glyphId, empty); outGlyph = empty; return true;
	}
	std::uint32_t pageIndex = 0, x = 0, y = 0;
	if (false == AllocateAtlasRect(face, bitmap.width, bitmap.rows, pageIndex, x, y)) return false;
	std::vector<std::uint8_t> rgba(static_cast<std::size_t>(bitmap.width) * bitmap.rows * 4);
	for (std::uint32_t row = 0; row < bitmap.rows; ++row)
	{
		const std::uint8_t* source = bitmap.buffer + static_cast<std::ptrdiff_t>(row) * bitmap.pitch;
		for (std::uint32_t column = 0; column < bitmap.width; ++column)
		{
			const std::uint8_t value = source[column];
			const std::size_t dst = (static_cast<std::size_t>(row) * bitmap.width + column) * 4;
			rgba[dst] = value; rgba[dst + 1] = value; rgba[dst + 2] = value; rgba[dst + 3] = 255;
		}
	}
	AtlasPage& page = face.Pages[pageIndex];
	if (false == m_rhiDevice->UpdateTexture2D(page.Texture.GetSafePtr(), x, y, bitmap.width, bitmap.rows, rgba.data(), bitmap.width * 4)) return false;
	GlyphInfo info;
	info.Page = pageIndex;
	info.U0 = static_cast<float>(x) / ATLAS_SIZE; info.V0 = static_cast<float>(y) / ATLAS_SIZE;
	info.U1 = static_cast<float>(x + bitmap.width) / ATLAS_SIZE; info.V1 = static_cast<float>(y + bitmap.rows) / ATLAS_SIZE;
	info.Width = static_cast<float>(bitmap.width); info.Height = static_cast<float>(bitmap.rows);
	info.BearingX = static_cast<float>(ftFace->glyph->bitmap_left);
	info.BearingY = static_cast<float>(ftFace->glyph->bitmap_top);
	face.Glyphs.emplace(glyphId, info); outGlyph = info; return true;
}

OwnerPtr<IRenderMesh> CTextRenderSystem::CreateMesh(const std::vector<float>& vertices, const std::vector<std::uint32_t>& indices) const
{
	if (nullptr == m_rhiDevice || vertices.empty() || indices.empty()) return nullptr;
	RHIBufferDesc vertexDesc; vertexDesc.SizeInBytes = vertices.size() * sizeof(float); vertexDesc.Usage = ERHIBufferUsage::Immutable;
	vertexDesc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::VertexBuffer);
	RHIBufferDesc indexDesc; indexDesc.SizeInBytes = indices.size() * sizeof(std::uint32_t); indexDesc.Usage = ERHIBufferUsage::Immutable;
	indexDesc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::IndexBuffer);
	OwnerPtr<IRHIBuffer> vb = m_rhiDevice->CreateBuffer(vertexDesc, vertices.data());
	OwnerPtr<IRHIBuffer> ib = m_rhiDevice->CreateBuffer(indexDesc, indices.data());
	if (!vb || !ib) return nullptr;
	return MakeOwnerPtr<CRenderMesh>(std::move(vb), std::move(ib), static_cast<std::uint32_t>(vertices.size() / 4), static_cast<std::uint32_t>(indices.size()));
}

std::size_t CTextRenderSystem::BuildSignature(const Text2D& text, const AssetGuid& effectiveFamily) const
{
	std::size_t hash = std::hash<std::string>{}(text.Text);
	HashCombine(hash, std::hash<AssetGuid>{}(effectiveFamily));
	HashCombine(hash, static_cast<std::size_t>(text.FontStyle));
	auto hf = [&hash](float value) { HashCombine(hash, std::hash<float>{}(value)); };
	hf(text.FontSizePixels); hf(text.WidthPixels); hf(text.HeightPixels); hf(text.LineSpacing); hf(text.LetterSpacingPixels);
	HashCombine(hash, static_cast<std::size_t>(text.OverflowMode));
	HashCombine(hash, static_cast<std::size_t>(text.HorizontalAlignment));
	HashCombine(hash, static_cast<std::size_t>(text.VerticalAlignment));
	HashCombine(hash, text.AutoSizeEnabled ? 1u : 0u); hf(text.MinFontSizePixels); hf(text.MaxFontSizePixels);
	return hash;
}

bool CTextRenderSystem::RebuildText(Text2D& text, CachedText& cache, CForward2DRenderer& renderer)
{
	std::vector<AssetGuid> faces;
	if (false == ResolveFamilyFaces(text, faces)) return false;
	float fontSize = std::max(1.0f, text.FontSizePixels);
	std::vector<PositionedGlyph> glyphs;
	float width = 0.0f, height = 0.0f;
	BuildLayout(text, faces, glyphs, width, height, fontSize);
	if (text.AutoSizeEnabled && ETextOverflowMode::Clip == text.OverflowMode && text.WidthPixels > 0.0f && text.HeightPixels > 0.0f)
	{
		float low = std::max(1.0f, text.MinFontSizePixels);
		float high = std::max(low, text.MaxFontSizePixels);
		for (int iteration = 0; iteration < 8; ++iteration)
		{
			const float candidate = (low + high) * 0.5f;
			std::vector<PositionedGlyph> candidateGlyphs; float candidateWidth = 0.0f, candidateHeight = 0.0f;
			BuildLayout(text, faces, candidateGlyphs, candidateWidth, candidateHeight, candidate);
			if (candidateWidth <= text.WidthPixels && candidateHeight <= text.HeightPixels)
			{
				low = candidate; glyphs = std::move(candidateGlyphs); width = candidateWidth; height = candidateHeight; fontSize = candidate;
			}
			else high = candidate;
		}
	}
	struct Buffers { std::vector<float> Vertices; std::vector<std::uint32_t> Indices; AssetGuid Face; std::uint32_t Page = 0; };
	std::unordered_map<std::string, Buffers> buffers;
	const float glyphScale = fontSize / static_cast<float>(GLYPH_PIXEL_SIZE);
	for (const PositionedGlyph& positioned : glyphs)
	{
		FaceCache* face = AcquireFace(positioned.FaceGuid); if (!face) continue;
		GlyphInfo glyph; if (false == EnsureGlyph(*face, positioned.GlyphId, glyph) || glyph.Width <= 0.0f) continue;
		float left = positioned.X + glyph.BearingX * glyphScale;
		float top = positioned.Y + glyph.BearingY * glyphScale;
		float right = left + glyph.Width * glyphScale;
		float bottom = top - glyph.Height * glyphScale;
		float u0 = glyph.U0, v0 = glyph.V0, u1 = glyph.U1, v1 = glyph.V1;
		if (ETextOverflowMode::Clip == text.OverflowMode)
		{
			const float clipTop = fontSize;
			const float clipBottom = fontSize - text.HeightPixels;
			if (right <= 0.0f || left >= text.WidthPixels || top <= clipBottom || bottom >= clipTop) continue;
			if (left < 0.0f) { u0 += (u1-u0) * ((0.0f-left)/(right-left)); left = 0.0f; }
			if (right > text.WidthPixels) { u1 -= (u1-u0) * ((right-text.WidthPixels)/(right-left)); right = text.WidthPixels; }
			if (top > clipTop) { v0 += (v1-v0) * ((top-clipTop)/(top-bottom)); top = clipTop; }
			if (bottom < clipBottom) { v1 -= (v1-v0) * ((clipBottom-bottom)/(top-bottom)); bottom = clipBottom; }
		}
		const std::string key = MaterialKey(positioned.FaceGuid, glyph.Page);
		Buffers& target = buffers[key]; target.Face = positioned.FaceGuid; target.Page = glyph.Page;
		const std::uint32_t base = static_cast<std::uint32_t>(target.Vertices.size() / 4);
		const float ppu = m_pixelsPerUnit;
		const float quad[] = { left/ppu, top/ppu, u0,v0, right/ppu,top/ppu,u1,v0,
			right/ppu,bottom/ppu,u1,v1, left/ppu,bottom/ppu,u0,v1 };
		target.Vertices.insert(target.Vertices.end(), std::begin(quad), std::end(quad));
		const std::uint32_t inds[] = { base,base+1,base+2,base,base+2,base+3 };
		target.Indices.insert(target.Indices.end(), std::begin(inds), std::end(inds));
	}
	float minX = std::numeric_limits<float>::max(), minY = std::numeric_limits<float>::max();
	float maxX = -std::numeric_limits<float>::max(), maxY = -std::numeric_limits<float>::max();
	for (auto& pair : buffers)
	{
		for (std::size_t index = 0; index + 3 < pair.second.Vertices.size(); index += 4)
		{
			minX = std::min(minX, pair.second.Vertices[index]); maxX = std::max(maxX, pair.second.Vertices[index]);
			minY = std::min(minY, pair.second.Vertices[index + 1]); maxY = std::max(maxY, pair.second.Vertices[index + 1]);
		}
	}
	if (buffers.empty()) minX = minY = maxX = maxY = 0.0f;
	float anchorX = minX;
	if (ETextHorizontalAlignment::Center == text.HorizontalAlignment) anchorX = (minX + maxX) * 0.5f;
	else if (ETextHorizontalAlignment::Right == text.HorizontalAlignment) anchorX = maxX;
	float anchorY = 0.0f;
	if (ETextVerticalAlignment::Top == text.VerticalAlignment) anchorY = maxY;
	else if (ETextVerticalAlignment::Middle == text.VerticalAlignment) anchorY = (minY + maxY) * 0.5f;
	else if (ETextVerticalAlignment::Bottom == text.VerticalAlignment) anchorY = minY;
	minX -= anchorX; maxX -= anchorX; minY -= anchorY; maxY -= anchorY;
	const float centerX = (minX + maxX) * 0.5f;
	const float centerY = (minY + maxY) * 0.5f;
	for (auto& pair : buffers)
	{
		for (std::size_t index = 0; index + 3 < pair.second.Vertices.size(); index += 4)
		{
			pair.second.Vertices[index] -= anchorX + centerX;
			pair.second.Vertices[index + 1] -= anchorY + centerY;
		}
	}
	cache.Meshes.clear();
	for (auto& pair : buffers)
	{
		CachedMesh mesh; mesh.FaceGuid = pair.second.Face; mesh.Page = pair.second.Page;
		mesh.Mesh = CreateMesh(pair.second.Vertices, pair.second.Indices);
		if (mesh.Mesh) cache.Meshes.push_back(std::move(mesh));
	}
	cache.Width = maxX - minX; cache.Height = maxY - minY;
	cache.CenterX = centerX; cache.CenterY = centerY;
	cache.Signature = BuildSignature(text, GetEffectiveFamily(text));
	return true;
}

OwnerPtr<IRenderMaterial>& CTextRenderSystem::AcquireMaterial(const AssetGuid& faceGuid, std::uint32_t page, CForward2DRenderer& renderer)
{
	const std::string key = MaterialKey(faceGuid, page);
	auto found = m_materials.find(key);
	if (found != m_materials.end()) return found->second;
	FaceCache* face = AcquireFace(faceGuid);
	OwnerPtr<IRenderMaterial> material;
	if (face && page < face->Pages.size())
	{
		material = MakeOwnerPtr<CRenderMaterial>(renderer.GetTextPipeline(), face->Pages[page].Texture.GetSafePtr(),
			renderer.GetDefaultSampler(), ERenderQueue::Transparent);
	}
	auto inserted = m_materials.emplace(key, std::move(material));
	return inserted.first->second;
}

void CTextRenderSystem::OnUpdate(CGameScene& scene)
{
	CForward2DRenderer* renderer = dynamic_cast<CForward2DRenderer*>(m_renderer);
	if (nullptr == renderer || nullptr == m_renderScene || false == renderer->GetTextPipeline().IsValid()) return;
	std::unordered_set<const void*> seen;
	scene.ForEach<Text2D>([&](Text2D& text)
	{
		if (false == text.IsEnabled() || nullptr == text.GetOwner() || text.Text.empty()) return;
		if (false == IsVisibleColor(text.FillEnabled, text.FillColor) && false == IsVisibleColor(text.OutlineEnabled, text.OutlineColor)) return;
		const void* key = &text; seen.insert(key);
		const AssetGuid family = GetEffectiveFamily(text);
		std::vector<AssetGuid> resolvedFaces;
		if (family.IsNull() || false == ResolveFamilyFaces(text, resolvedFaces))
		{
			if (m_warnedMissingFonts.insert(key).second)
			{
				CSystemLog::Warning("Text2D was not rendered because no usable Font Family was configured.");
			}
			return;
		}
		m_warnedMissingFonts.erase(key);
		CachedText& cache = m_textCache[key];
		const std::size_t signature = BuildSignature(text, family);
		if (cache.Signature != signature && false == RebuildText(text, cache, *renderer)) return;
		for (CachedMesh& mesh : cache.Meshes)
		{
			OwnerPtr<IRenderMaterial>& material = AcquireMaterial(mesh.FaceGuid, mesh.Page, *renderer);
			if (!mesh.Mesh || !material) continue;
			RenderItem item;
			item.Mesh = mesh.Mesh.GetSafePtr(); item.Material = material.GetSafePtr(); item.Pipeline = renderer->GetTextPipeline();
			item.Texture = material->GetTexture(); item.Sampler = material->GetSampler(); item.Queue = ERenderQueue::Transparent;
			item.LayerMask = text.LayerMask; item.SortOrder = text.SortOrder; item.Entity = text.GetOwner().TryGet();
			item.Transform = Matrix3x2::Transform(text.Offset + Vector2(cache.CenterX, cache.CenterY), 0.0f, Vector2(1.0f, 1.0f)) * text.GetOwner()->GetWorld().Matrix;
			if (text.PixelSnap && std::abs(item.Transform.M12) < 0.00001f && std::abs(item.Transform.M21) < 0.00001f)
			{
				item.Transform.Dx = std::round(item.Transform.Dx * m_pixelsPerUnit) / m_pixelsPerUnit;
				item.Transform.Dy = std::round(item.Transform.Dy * m_pixelsPerUnit) / m_pixelsPerUnit;
			}
			item.LocalHalfExtents[0] = std::max(cache.Width * 0.5f, 0.001f); item.LocalHalfExtents[1] = std::max(cache.Height * 0.5f, 0.001f);
			for (int c = 0; c < 4; ++c)
			{
				item.Color[c] = text.FillEnabled ? text.FillColor[c] : 0.0f;
				item.SecondaryColor[c] = text.OutlineEnabled ? text.OutlineColor[c] : 0.0f;
			}
			item.ShaderParams[0] = text.OutlineEnabled ? std::clamp(text.OutlineWidthPixels, 0.0f, 64.0f) : 0.0f;
			item.ShaderParams[1] = 0.0f;
			m_renderScene->Submit(item);
		}
	});
	for (auto it = m_textCache.begin(); it != m_textCache.end(); )
	{
		if (seen.find(it->first) == seen.end()) it = m_textCache.erase(it); else ++it;
	}
	for (auto it = m_warnedMissingFonts.begin(); it != m_warnedMissingFonts.end(); )
	{
		if (seen.find(*it) == seen.end()) it = m_warnedMissingFonts.erase(it); else ++it;
	}
}
