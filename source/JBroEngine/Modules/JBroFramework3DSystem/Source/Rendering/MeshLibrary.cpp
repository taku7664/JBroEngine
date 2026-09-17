#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>

#include <JBro/Core/StableTypeId.h>
#include <JBro/Graphics/Renderer.h>

namespace JBro
{
    namespace
    {
        // 한 변이 1 인 정육면체. 면마다 정점 넷(법선이 다르므로 나눈다), 바깥에서 볼 때 반시계다 -
        // D3D 의 기본(`FrontCounterClockwise = FALSE`)에서 화면 y 가 아래로 뒤집히며 그것이 앞면이 된다.
        constexpr MeshVertex CubeVertices[24] = {
            // +Z
            {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}, {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
            {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},   {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
            // -Z
            {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}}, {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}},
            {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}}, {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}},
            // +X
            {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}},  {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
            {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},  {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}},
            // -X
            {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}}, {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}},
            {{-0.5f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}},   {{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}},
            // +Y
            {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},  {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
            {{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},  {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
            // -Y
            {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}}, {{0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}},
            {{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}},   {{-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}},
        };

        constexpr std::uint32_t CubeIndices[36] = {
            0, 1, 2, 0, 2, 3,
            4, 5, 6, 4, 6, 7,
            8, 9, 10, 8, 10, 11,
            12, 13, 14, 12, 14, 15,
            16, 17, 18, 16, 18, 19,
            20, 21, 22, 20, 22, 23};
    }

    AssetId MeshLibrary::BuiltinCubeId()
    {
        return AssetId{MakeStableTypeId("builtin/cube")};
    }

    bool MeshLibrary::Initialize(Renderer* renderer)
    {
        m_renderer = renderer;
        m_entries.Clear();
        if (renderer == nullptr || false == renderer->IsInitialized())
        {
            return true;
        }
        const AssetHandle cube = renderer->RegisterMesh({CubeVertices, 24}, {CubeIndices, 36});
        if (cube.generation == 0)
        {
            return false;
        }
        Register(BuiltinCubeId(), cube);
        return true;
    }

    void MeshLibrary::Shutdown()
    {
        if (m_renderer != nullptr && m_renderer->IsInitialized())
        {
            for (std::size_t index = 0; index < m_entries.Size(); ++index)
            {
                m_renderer->UnregisterMesh(m_entries[index].handle);
            }
        }
        m_entries.Clear();
        m_renderer = nullptr;
    }

    AssetHandle MeshLibrary::Resolve(AssetId id) const
    {
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            if (m_entries[index].id.value == id.value)
            {
                return m_entries[index].handle;
            }
        }
        return {};
    }

    void MeshLibrary::Register(AssetId id, AssetHandle handle)
    {
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            if (m_entries[index].id.value == id.value)
            {
                m_entries[index].handle = handle;
                return;
            }
        }
        m_entries.Add(Entry{id, handle});
    }

    std::size_t MeshLibrary::GetCount() const
    {
        return m_entries.Size();
    }
}
