#include <JBro/Framework3DSystem/Rendering/RenderWorld3D.h>

#include <new>

namespace JBro
{
    bool RenderWorld3D::ReserveMeshes(std::size_t capacity)
    {
        try
        {
            m_meshes.Reserve(capacity);
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        return true;
    }

    void RenderWorld3D::BeginFrame()
    {
        m_hasCamera = false;
        m_meshes.Clear();
        m_droppedMeshCount = 0;
    }

    void RenderWorld3D::SetCamera(const RenderCamera3D& camera)
    {
        m_camera = camera;
        m_hasCamera = true;
    }

    bool RenderWorld3D::SubmitMesh(const MeshRenderItem& item)
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
