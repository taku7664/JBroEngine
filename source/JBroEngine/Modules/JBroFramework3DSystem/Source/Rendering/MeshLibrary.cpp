#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Asset/MeshFile.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    namespace
    {
        // 한 변이 1 인 정육면체. 면마다 정점 넷(법선이 다르므로 나눈다). 정점은 바깥에서 볼 때 반시계로
        // 놓고 인덱스가 시계 방향으로 감는다 - 세 백엔드가 모두 **화면에서 시계 방향**을 앞면으로 본다
        // (D3D `FrontCounterClockwise = FALSE`, Vulkan 은 뒤집은 뷰포트 + `CLOCKWISE`). 처음에는 반시계로
        // 감아 바깥 면이 전부 컬링되고 안쪽 면(앰비언트만)이 보였다 - 밝기 단언이 잡았다(D-108).
        // UV 는 면마다 바깥에서 본 왼쪽 아래가 (0, 1), 오른쪽 위가 (1, 0) 이다(D-348) - 텍스처의 위가 면의 위다.
        constexpr MeshVertex CubeVertices[24] = {
            // +Z
            {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}, {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
            {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},   {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
            // -Z
            {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}}, {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
            {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}}, {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
            // +X
            {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},  {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
            {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},  {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
            // -X
            {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}}, {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
            {{-0.5f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},   {{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
            // +Y
            {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},  {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
            {{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},  {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
            // -Y
            {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}}, {{0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
            {{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},   {{-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        };

        constexpr UInt32 CubeIndices[36] = {
            0, 2, 1, 0, 3, 2,
            4, 6, 5, 4, 7, 6,
            8, 10, 9, 8, 11, 10,
            12, 14, 13, 12, 15, 14,
            16, 18, 17, 16, 19, 18,
            20, 22, 21, 20, 23, 22};
    }

    AssetId MeshLibrary::BuiltinCubeId()
    {
        return Uuid::FromName("builtin/cube");
    }

    Bool MeshLibrary::Initialize(Renderer* renderer, AssetSystem* assets)
    {
        m_renderer = renderer;
        m_assets = assets;
        m_assetEntries.Clear();
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
            for (const AssetEntry& entry : m_assetEntries)
            {
                m_renderer->UnregisterMesh(entry.renderer);
            }
        }
        m_entries.Clear();
        m_assetEntries.Clear();
        m_assets = nullptr;
        m_renderer = nullptr;
    }

    AssetHandle MeshLibrary::Resolve(AssetId id) const
    {
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            if (m_entries[index].id == id)
            {
                return m_entries[index].handle;
            }
        }
        return {};
    }

    AssetHandle MeshLibrary::ResolveHandle(AssetHandle handle)
    {
        const MeshAssetData* data = m_assets != nullptr ? m_assets->GetMesh(handle) : nullptr;
        if (data == nullptr)
        {
            return handle;
        }
        AssetEntry* found = nullptr;
        for (AssetEntry& entry : m_assetEntries)
        {
            if (entry.asset.index == handle.index && entry.asset.generation == handle.generation)
            {
                found = &entry;
                break;
            }
        }
        if (found != nullptr && found->dataGeneration == data->dataGeneration)
        {
            return found->renderer;
        }
        if (m_renderer == nullptr || false == m_renderer->IsInitialized())
        {
            return {};
        }
        static_assert(sizeof(MeshVertex) == sizeof(Float) * MeshVertexFloats.Get(), "the mesh asset's floats are the renderer's vertices");
        const AssetHandle uploaded = m_renderer->RegisterMesh(
            {reinterpret_cast<const MeshVertex*>(data->vertices.Data()), static_cast<JBro::UInt32>(data->vertices.Size() / MeshVertexFloats.Get())},
            {data->indices.Data(), static_cast<JBro::UInt32>(data->indices.Size())});
        if (found == nullptr)
        {
            found = &m_assetEntries.Add(AssetEntry{handle, 0, {}});
        }
        else if (found->renderer.generation != 0)
        {
            m_renderer->UnregisterMesh(found->renderer);
        }
        found->dataGeneration = data->dataGeneration;
        found->renderer = uploaded;
        return uploaded;
    }

    void MeshLibrary::Register(AssetId id, AssetHandle handle)
    {
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            if (m_entries[index].id == id)
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
