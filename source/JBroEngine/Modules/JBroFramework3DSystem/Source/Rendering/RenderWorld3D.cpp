#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>

#include <new>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    Bool RenderWorld3D::ReserveMeshes(std::size_t capacity)
    {
        try
        {
            m_meshes.Reserve(capacity);
            m_skinPalettes.Reserve(capacity);

            m_layerOrders.Reserve(m_meshes.Capacity() + m_texts.Capacity());
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        return true;
    }

    Bool RenderWorld3D::ReserveTexts(std::size_t capacity)
    {
        try
        {
            m_texts.Reserve(capacity);
            m_textOrder.Reserve(capacity);
            m_layerOrders.Reserve(m_meshes.Capacity() + m_texts.Capacity());
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        return true;
    }

    Array<UInt32>& RenderWorld3D::GetSkinPaletteScratch() const
    {
        return m_skinPalettes;
    }

    UInt64& RenderWorld3D::GetSkinPaletteFrame() const
    {
        return m_skinPaletteFrame;
    }

    void RenderWorld3D::BeginFrame()
    {
        m_hasCamera = false;
        m_meshes.Clear();
        m_droppedMeshCount = 0;
        m_lights.Clear();
        m_texts.Clear();
        m_droppedTextCount = 0;
        m_lights.Clear();
        m_droppedLightCount = 0;
    }

    Bool RenderWorld3D::ReserveLights(std::size_t capacity)
    {
        try
        {
            m_lights.Reserve(capacity);
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        return true;
    }

    Bool RenderWorld3D::SubmitLight(const Light3DRenderItem& item)
    {
        if (m_lights.Size() >= m_lights.Capacity())
        {
            ++m_droppedLightCount;
            return false;
        }
        m_lights.Add(item);
        return true;
    }

    std::size_t RenderWorld3D::GetLightCount() const
    {
        return m_lights.Size();
    }

    std::size_t RenderWorld3D::GetDroppedLightCount() const
    {
        return m_droppedLightCount;
    }

    const Light3DRenderItem& RenderWorld3D::GetLight(std::size_t index) const
    {
        return m_lights[index];
    }

    Bool RenderWorld3D::SubmitText(const WorldTextRenderItem& item)
    {
        if (m_texts.Size() >= m_texts.Capacity())
        {
            ++m_droppedTextCount;
            return false;
        }
        m_texts.Add(item);
        return true;
    }

    std::size_t RenderWorld3D::GetTextCount() const
    {
        return m_texts.Size();
    }

    std::size_t RenderWorld3D::GetDroppedTextCount() const
    {
        return m_droppedTextCount;
    }

    const WorldTextRenderItem& RenderWorld3D::GetText(std::size_t index) const
    {
        return m_texts[index];
    }

    Array<UInt32>& RenderWorld3D::GetTextOrderScratch() const
    {
        return m_textOrder;
    }

    Array<std::uint16_t>& RenderWorld3D::GetLayerOrderScratch() const
    {
        return m_layerOrders;
    }

    void RenderWorld3D::SetCamera(const RenderCamera3D& camera)
    {
        m_camera = camera;
        m_hasCamera = true;
    }

    void RenderWorld3D::ClearCameras()
    {
        m_cameras.Clear();
    }

    void RenderWorld3D::AddCamera(const RenderCamera3D& camera)
    {
        // 들어온 차례가 그리는 차례다(D-438). 용량은 한 번 자라면 남는다.
        m_cameras.Add(camera);
    }

    ArrayView<const RenderCamera3D> RenderWorld3D::GetCameras() const
    {
        return ArrayView<const RenderCamera3D>(m_cameras.Data(), m_cameras.Size());
    }

    Bool RenderWorld3D::SubmitMesh(const MeshRenderItem& item)
    {
        // 용량은 초기화 때 렌더러의 한도로 잡았다. 넘치면 세기만 하고 버린다 -
        // 매 프레임 경로에서 힙을 늘리지 않는다(§9).
        if (m_meshes.Size() >= m_meshes.Capacity())
        {
            ++m_droppedMeshCount;
            return false;
        }
        m_meshes.Add(item);
        return true;
    }

    void RenderWorld3D::EndFrame()
    {
    }

    const RenderCamera3D* RenderWorld3D::GetCamera() const
    {
        return m_hasCamera ? &m_camera : nullptr;
    }

    std::size_t RenderWorld3D::GetMeshCount() const
    {
        return m_meshes.Size();
    }

    std::size_t RenderWorld3D::GetMeshCapacity() const
    {
        return m_meshes.Capacity();
    }

    std::size_t RenderWorld3D::GetDroppedMeshCount() const
    {
        return m_droppedMeshCount;
    }

    const MeshRenderItem& RenderWorld3D::GetMesh(std::size_t index) const
    {
        return m_meshes[index];
    }
}
