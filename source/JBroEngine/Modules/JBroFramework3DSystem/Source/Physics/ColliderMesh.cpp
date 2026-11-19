#include <JBro/Framework3DSystem/Physics/ColliderMesh.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Physics3D.h>
#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Runtime/GameObject.h>

namespace JBro
{
    namespace
    {
        // 렌더러의 감기(바깥에서 시계)를 물리의 감기(반시계)로. 삼각형마다 둘째와 셋째를 바꾼다.
        void FlipWinding(Array<UInt32>& indices)
        {
            for (std::size_t index = 0; index + 2 < indices.Size(); index += 3)
            {
                const UInt32 second = indices[index + 1];
                indices[index + 1] = indices[index + 2];
                indices[index + 2] = second;
            }
        }

        // 빌트인 도형 표의 자리. 없으면 범위 밖 값이다.
        UInt32 FindBuiltin(AssetId id)
        {
            const UInt32 count = MeshLibrary::GetBuiltinCount();
            for (UInt32 index = 0; index < count; ++index)
            {
                if (MeshLibrary::GetBuiltin(index).id == id)
                {
                    return index;
                }
            }
            return count;
        }
    }

    ColliderMeshSource FindColliderMeshSource(Canvas& canvas, Object::GameObject& object, const Component::Collider3D& collider,
        const AssetSystem* assets)
    {
        ColliderMeshSource source;
        if (false == collider.meshId.IsNull())
        {
            source.id = collider.meshId;
            source.handle = collider.mesh;
        }
        else if (const Component::MeshRenderer3D* renderer = canvas.FindComponentRaw<Component::MeshRenderer3D>(&object))
        {
            source.id = renderer->meshId;
            source.handle = renderer->mesh;
        }
        if (source.id.IsNull())
        {
            return source;
        }
        if (FindBuiltin(source.id) < MeshLibrary::GetBuiltinCount())
        {
            source.dataGeneration = 1;
            return source;
        }
        const MeshData* mesh = assets != nullptr && source.handle.generation != 0 ? assets->GetMesh(source.handle) : nullptr;
        source.dataGeneration = mesh != nullptr ? mesh->dataGeneration : UInt32(0);
        return source;
    }

    Bool GatherColliderMesh(const AssetSystem* assets, const ColliderMeshSource& source, Array<Vector3>& points, Array<UInt32>& indices)
    {
        points.Clear();
        indices.Clear();
        if (source.id.IsNull())
        {
            return false;
        }
        const UInt32 builtin = FindBuiltin(source.id);
        if (builtin < MeshLibrary::GetBuiltinCount())
        {
            Array<MeshVertex> vertices;
            if (false == MeshLibrary::BuildBuiltin(builtin, vertices, indices))
            {
                return false;
            }
            points.Reserve(vertices.Size());
            for (const MeshVertex& vertex : vertices)
            {
                points.Add(Vector3{vertex.position[0], vertex.position[1], vertex.position[2]});
            }
            FlipWinding(indices);
            return points.Size() != 0 && indices.Size() != 0;
        }
        const MeshData* mesh = assets != nullptr && source.handle.generation != 0 ? assets->GetMesh(source.handle) : nullptr;
        if (mesh == nullptr || mesh->vertices.Size() == 0 || mesh->indices.Size() == 0)
        {
            return false;
        }
        points.Reserve(mesh->vertices.Size());
        for (const ModelVertex& vertex : mesh->vertices)
        {
            points.Add(Vector3{vertex.position[0], vertex.position[1], vertex.position[2]});
        }
        indices.Reserve(mesh->indices.Size());
        for (const UInt32 index : mesh->indices)
        {
            indices.Add(index);
        }
        FlipWinding(indices);
        return true;
    }
}
