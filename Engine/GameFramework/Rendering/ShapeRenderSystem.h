#pragma once

#include "GameFramework/Rendering/ShapeGeometry2D.h"
#include "GameFramework/System/GameSystem.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <unordered_map>

class CForward2DRenderer;
class Circle2D;
class IRenderMaterial;
class IRenderMesh;
class IRenderer;
class IRHIDevice;
class IRenderScene;
class Polygon2D;
class Square2D;

class CShapeRenderSystem final : public CGameSystem
{
public:
	explicit CShapeRenderSystem(IRenderScene* renderScene = nullptr);

	void SetRenderScene(IRenderScene* renderScene);
	void SetDependencies(IRHIDevice* rhiDevice, IRenderer* renderer);
	IRenderScene* GetRenderScene() const;
	bool ShouldUpdateInEditMode() const override { return true; }
	void ClearMeshCache();

protected:
	void OnUpdate(CGameCanvas& canvas) override;

private:
	struct GeometrySignature
	{
		float Value0 = 0.0f;
		float Value1 = 0.0f;
		float Value2 = 0.0f;
		float OutlineWidth = 0.0f;
		std::uint32_t Count = 0;
		bool FillEnabled = false;
		bool OutlineEnabled = false;

		bool operator==(const GeometrySignature& rhs) const;
	};

	struct CachedShape
	{
		GeometrySignature Signature;
		OwnerPtr<IRenderMesh> FillMesh;
		OwnerPtr<IRenderMesh> OutlineMesh;
		std::uint64_t LastSeenFrame = 0;
	};

	OwnerPtr<IRenderMesh> CreateMesh(const ShapeMeshData2D& data) const;
	bool EnsureMaterial(CForward2DRenderer& renderer);
	void SubmitSquare(Square2D& shape, CForward2DRenderer& renderer);
	void SubmitCircle(Circle2D& shape, CForward2DRenderer& renderer);
	void SubmitPolygon(Polygon2D& shape, CForward2DRenderer& renderer);
	template<typename T, typename TBuilder>
	void SubmitGeometry(T& shape, const GeometrySignature& signature, const Vector2& halfExtents,
		TBuilder&& builder, CForward2DRenderer& renderer);

private:
	IRenderScene* m_renderScene = nullptr;
	IRHIDevice* m_rhiDevice = nullptr;
	CForward2DRenderer* m_renderer = nullptr;
	OwnerPtr<IRenderMaterial> m_material;
	std::unordered_map<const void*, CachedShape> m_meshCache;
	std::uint64_t m_frameStamp = 0;
};
