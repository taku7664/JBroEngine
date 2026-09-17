#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>

#include <JBro/Core/StableTypeId.h>
#include <JBro/Graphics/Renderer.h>

namespace JBro
{
    namespace
    {
        // 한 변이 1 인 정육면체. 면마다 정점 넷(법선이 다르므로 나눈다). 정점은 바깥에서 볼 때 반시계로
        // 놓고 인덱스가 시계 방향으로 감는다 - 세 백엔드가 모두 **화면에서 시계 방향**을 앞면으로 본다
        // (D3D `FrontCounterClockwise = FALSE`, Vulkan 은 뒤집은 뷰포트 + `CLOCKWISE`). 처음에는 반시계로
        // 감아 바깥 면이 전부 컬링되고 안쪽 면(앰비언트만)이 보였다 - 밝기 단언이 잡았다(D-108).
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
            0, 2, 1, 0, 3, 2,
            4, 6, 5, 4, 7, 6,
            8, 10, 9, 8, 11, 10,
            12, 14, 13, 12, 15, 14,
            16, 18, 17, 16, 19, 18,
            20, 22, 21, 20, 23, 22};
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
