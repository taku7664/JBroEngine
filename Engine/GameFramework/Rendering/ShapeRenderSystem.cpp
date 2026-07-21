#include "pch.h"
#include "ShapeRenderSystem.h"

#include "Core/Renderer/Forward2DRenderer.h"
#include "Core/Renderer/IRenderMaterial.h"
#include "Core/Renderer/IRenderMesh.h"
#include "Core/Renderer/IRenderScene.h"
#include "Core/Renderer/RenderResources2D.h"
#include "Core/RHI/IRHIDevice.h"
#include "GameFramework/Component/ShapeRenderers2D.h"
#include "GameFramework/Object/GameObject.h"
#include "GameFramework/Canvas/Canvas.h"
#include "Utillity/Types/FrameLiveness.h"

#include <algorithm>
#include <cmath>

CShapeRenderSystem::CShapeRenderSystem(IRenderScene* renderScene)
	: m_renderScene(renderScene)
{
}

void CShapeRenderSystem::SetRenderScene(IRenderScene* renderScene)
{
	m_renderScene = renderScene;
}

void CShapeRenderSystem::SetDependencies(IRHIDevice* rhiDevice, IRenderer* renderer)
{
	CForward2DRenderer* forwardRenderer = renderer ? renderer->AsForward2DRenderer() : nullptr;
	if (m_rhiDevice != rhiDevice || m_renderer != forwardRenderer)
	{
		ClearMeshCache();
		m_material.Reset();
	}
	m_rhiDevice = rhiDevice;
	m_renderer = forwardRenderer;
}

IRenderScene* CShapeRenderSystem::GetRenderScene() const
{
	return m_renderScene;
}

void CShapeRenderSystem::ClearMeshCache()
{
	m_meshCache.clear();
}

bool CShapeRenderSystem::GeometrySignature::operator==(const GeometrySignature& rhs) const
{
	return Value0 == rhs.Value0
		&& Value1 == rhs.Value1
		&& Value2 == rhs.Value2
		&& OutlineWidth == rhs.OutlineWidth
		&& Count == rhs.Count
		&& FillEnabled == rhs.FillEnabled
		&& OutlineEnabled == rhs.OutlineEnabled;
}

OwnerPtr<IRenderMesh> CShapeRenderSystem::CreateMesh(const ShapeMeshData2D& data) const
{
	if (nullptr == m_rhiDevice || data.IsEmpty())
	{
		return nullptr;
	}

	RHIBufferDesc vertexDesc;
	vertexDesc.SizeInBytes = sizeof(ShapeVertex2D) * data.Vertices.size();
	vertexDesc.Usage = ERHIBufferUsage::Immutable;
	vertexDesc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::VertexBuffer);

	RHIBufferDesc indexDesc;
	indexDesc.SizeInBytes = sizeof(std::uint32_t) * data.Indices.size();
	indexDesc.Usage = ERHIBufferUsage::Immutable;
	indexDesc.BindFlags = static_cast<RHIBindFlags>(ERHIBindFlag::IndexBuffer);

	OwnerPtr<IRHIBuffer> vertexBuffer = m_rhiDevice->CreateBuffer(vertexDesc, data.Vertices.data());
	OwnerPtr<IRHIBuffer> indexBuffer = m_rhiDevice->CreateBuffer(indexDesc, data.Indices.data());
	if (!vertexBuffer || !indexBuffer)
	{
		return nullptr;
	}

	return MakeOwnerPtr<CRenderMesh>(std::move(vertexBuffer), std::move(indexBuffer),
		static_cast<std::uint32_t>(data.Vertices.size()), static_cast<std::uint32_t>(data.Indices.size()));
}

bool CShapeRenderSystem::EnsureMaterial(CForward2DRenderer& renderer)
{
	if (m_material
		&& m_material->GetPipeline().IsValid()
		&& m_material->GetTexture().IsValid()
		&& m_material->GetSampler().IsValid())
	{
		return true;
	}
	m_material.Reset();
	if (false == renderer.GetSpritePipeline().IsValid()
		|| false == renderer.GetWhiteTexture().IsValid()
		|| false == renderer.GetDefaultSampler().IsValid())
	{
		return false;
	}

	m_material = MakeOwnerPtr<CRenderMaterial>(renderer.GetSpritePipeline(), renderer.GetWhiteTexture(),
		renderer.GetDefaultSampler(), ERenderQueue::Transparent);
	return true;
}

template<typename T, typename TBuilder>
void CShapeRenderSystem::SubmitGeometry(T& shape, const GeometrySignature& signature,
	const Vector2& halfExtents, TBuilder&& builder, CForward2DRenderer& renderer)
{
	CGameObject* owner = shape.GetOwner().TryGet();
	if (nullptr == owner)
	{
		return;
	}

	const void* cacheKey = &shape;
	auto found = m_meshCache.find(cacheKey);
	if (found == m_meshCache.end() || false == (found->second.Signature == signature))
	{
		const ShapeGeometry2D geometry = builder();
		CachedShape rebuilt;
		rebuilt.Signature = signature;
		rebuilt.FillMesh = CreateMesh(geometry.Fill);
		rebuilt.OutlineMesh = CreateMesh(geometry.Outline);
		found = m_meshCache.insert_or_assign(cacheKey, std::move(rebuilt)).first;
	}
	found->second.LastSeenFrame = m_frameStamp;

	auto submitPass = [this, &shape, owner, &renderer, &halfExtents](SafePtr<IRenderMesh> mesh, const float (&color)[4])
	{
		if (false == mesh.IsValid() || color[3] <= 0.0f)
		{
			return;
		}

		RenderItem item;
		item.Mesh = mesh;
		item.Material = m_material.GetSafePtr();
		item.Pipeline = renderer.GetSpritePipeline();
		item.Texture = renderer.GetWhiteTexture();
		item.Sampler = renderer.GetDefaultSampler();
		item.Queue = ERenderQueue::Transparent;
		item.LayerIndex = owner->GetLayerIndex();
		item.Transform = Matrix3x2::Transform(shape.Offset, 0.0f, Vector2(1.0f, 1.0f)) * owner->GetWorld().Matrix;
		item.LocalHalfExtents[0] = halfExtents.x;
		item.LocalHalfExtents[1] = halfExtents.y;
		for (int i = 0; i < 4; ++i)
		{
			item.Color[i] = color[i];
		}
		item.SortOrder = shape.SortOrder;
		item.Entity = owner;
		m_renderScene->Submit(item);
	};

	submitPass(found->second.FillMesh.GetSafePtr(), shape.FillColor);
	submitPass(found->second.OutlineMesh.GetSafePtr(), shape.OutlineColor);
}

void CShapeRenderSystem::SubmitSquare(Square2D& shape, CForward2DRenderer& renderer)
{
	GeometrySignature signature;
	signature.Value0 = std::abs(shape.Size.x);
	signature.Value1 = std::abs(shape.Size.y);
	signature.OutlineWidth = std::max(0.0f, shape.OutlineWidth);
	signature.FillEnabled = shape.FillEnabled;
	signature.OutlineEnabled = shape.OutlineEnabled;
	const Vector2 halfExtents(signature.Value0 * 0.5f, signature.Value1 * 0.5f);
	SubmitGeometry(shape, signature, halfExtents, [&shape]()
	{
		return CShapeGeometryBuilder2D::BuildRectangle(shape.Size,
			shape.FillEnabled, shape.OutlineEnabled, shape.OutlineWidth);
	}, renderer);
}

void CShapeRenderSystem::SubmitCircle(Circle2D& shape, CForward2DRenderer& renderer)
{
	const std::uint32_t segments = std::clamp(shape.Segments, 8u, 256u);
	GeometrySignature signature;
	signature.Value0 = std::abs(shape.Radius);
	signature.OutlineWidth = std::max(0.0f, shape.OutlineWidth);
	signature.Count = segments;
	signature.FillEnabled = shape.FillEnabled;
	signature.OutlineEnabled = shape.OutlineEnabled;
	const Vector2 halfExtents(signature.Value0, signature.Value0);
	SubmitGeometry(shape, signature, halfExtents, [&shape, segments]()
	{
		return CShapeGeometryBuilder2D::BuildRegularPolygon(shape.Radius, segments, 0.0f,
			shape.FillEnabled, shape.OutlineEnabled, shape.OutlineWidth);
	}, renderer);
}

void CShapeRenderSystem::SubmitPolygon(Polygon2D& shape, CForward2DRenderer& renderer)
{
	const std::uint32_t vertexCount = std::clamp(shape.VertexCount, 3u, 256u);
	GeometrySignature signature;
	signature.Value0 = std::abs(shape.Radius);
	signature.Value2 = shape.StartAngle;
	signature.OutlineWidth = std::max(0.0f, shape.OutlineWidth);
	signature.Count = vertexCount;
	signature.FillEnabled = shape.FillEnabled;
	signature.OutlineEnabled = shape.OutlineEnabled;
	const Vector2 halfExtents(signature.Value0, signature.Value0);
	SubmitGeometry(shape, signature, halfExtents, [&shape, vertexCount]()
	{
		return CShapeGeometryBuilder2D::BuildRegularPolygon(shape.Radius, vertexCount,
			shape.StartAngle, shape.FillEnabled, shape.OutlineEnabled, shape.OutlineWidth);
	}, renderer);
}

void CShapeRenderSystem::OnUpdate(CGameCanvas& canvas)
{
	if (nullptr == m_renderScene || nullptr == m_rhiDevice)
	{
		return;
	}
	CForward2DRenderer* renderer = m_renderer;
	if (nullptr == renderer || false == EnsureMaterial(*renderer))
	{
		return;
	}

	++m_frameStamp;
	canvas.ForEach<Square2D>([this, renderer](Square2D& shape)
	{
		if (IsActiveComponent(shape))
		{
			SubmitSquare(shape, *renderer);
		}
	});
	canvas.ForEach<Circle2D>([this, renderer](Circle2D& shape)
	{
		if (IsActiveComponent(shape))
		{
			SubmitCircle(shape, *renderer);
		}
	});
	canvas.ForEach<Polygon2D>([this, renderer](Polygon2D& shape)
	{
		if (IsActiveComponent(shape))
		{
			SubmitPolygon(shape, *renderer);
		}
	});

	RemoveStaleEntries(m_meshCache, m_frameStamp,
		[](const CachedShape& cached) { return cached.LastSeenFrame; });
}
