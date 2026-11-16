#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>

#include <JBro/Core/StableTypeId.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Types/UInt.h>

#include <cmath>

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

        constexpr UInt32 CubeIndices[36] = {
            0, 2, 1, 0, 3, 2,
            4, 6, 5, 4, 7, 6,
            8, 10, 9, 8, 11, 10,
            12, 14, 13, 12, 15, 14,
            16, 18, 17, 16, 19, 18,
            20, 22, 21, 20, 23, 22};

        // **둥근 도형은 고리를 돌려 만든다.** 고리 하나는 높이·반지름과 그 고리의 법선(위 성분·바깥 성분)이다. 이웃한 고리를
        // 사각형 띠로 잇는다. 반지름 0 인 고리는 극(꼭짓점)이다. 크기는 Unity 의 기본 도형과 같다 - 구 지름 1, 원기둥·캡슐 높이 2·
        // 반지름 0.5, 평면 10 x 10. `[가정]` (D-335 우선순위 2)
        struct Ring
        {
            Float y;
            Float radius;
            Float normalY;
            Float normalRadial;
        };

        constexpr UInt32 RoundSegments = 32;
        constexpr UInt32 SphereRows = 16;
        constexpr Float Pi = 3.14159265f;

        class MeshBuilder
        {
        public:
            UInt32 AddVertex(const Vector3& position, const Vector3& normal)
            {
                MeshVertex vertex;
                vertex.position[0] = position.x;
                vertex.position[1] = position.y;
                vertex.position[2] = position.z;
                vertex.normal[0] = normal.x;
                vertex.normal[1] = normal.y;
                vertex.normal[2] = normal.z;
                m_vertices.Add(vertex);
                return static_cast<UInt32>(m_vertices.Size() - 1);
            }

            // **감김은 여기서 정한다.** 세 점을 어느 차례로 받든 법선 쪽에서 볼 때 화면 시계가 되게 넣는다(정육면체와 같은 규약).
            // 손으로 차례를 맞추면 도형마다 한 번씩 틀릴 자리라, 면의 외적과 정점 법선의 합을 견준다. 넓이가 없는 삼각형(극)은 버린다.
            void AddTriangle(UInt32 a, UInt32 b, UInt32 c)
            {
                const Vector3 pa = PositionOf(a);
                const Vector3 face = Cross(Subtract(PositionOf(b), pa), Subtract(PositionOf(c), pa));
                if (Length(face) < 0.0000001f)
                {
                    return;
                }
                const Vector3 outward = Add(NormalOf(a), Add(NormalOf(b), NormalOf(c)));
                const Bool counterClockwiseFromOutside = Dot(face, outward) > 0.0f;
                m_indices.Add(a);
                m_indices.Add(counterClockwiseFromOutside ? c : b);
                m_indices.Add(counterClockwiseFromOutside ? b : c);
            }

            void AddQuad(UInt32 a, UInt32 b, UInt32 c, UInt32 d)
            {
                AddTriangle(a, b, c);
                AddTriangle(a, c, d);
            }

            // 고리마다 `RoundSegments + 1` 개의 정점을 둔다(이음매의 첫·끝 정점은 자리가 같다).
            void AddLathe(const Ring* rings, UInt32 count)
            {
                const UInt32 first = static_cast<UInt32>(m_vertices.Size());
                for (UInt32 ring = 0; ring < count; ++ring)
                {
                    for (UInt32 segment = 0; segment <= RoundSegments; ++segment)
                    {
                        const Float angle = 2.0f * Pi * static_cast<Float>(segment) / static_cast<Float>(RoundSegments);
                        const Float cosine = std::cos(angle);
                        const Float sine = std::sin(angle);
                        const Ring& at = rings[ring];
                        const Vector3 normal = Normalize(Vector3{at.normalRadial * cosine, at.normalY, at.normalRadial * sine});
                        AddVertex(Vector3{at.radius * cosine, at.y, at.radius * sine}, normal);
                    }
                }
                const UInt32 stride = RoundSegments + 1;
                for (UInt32 ring = 0; ring + 1 < count; ++ring)
                {
                    const UInt32 upper = first + ring * stride;
                    const UInt32 lower = upper + stride;
                    for (UInt32 segment = 0; segment < RoundSegments; ++segment)
                    {
                        AddQuad(upper + segment, upper + segment + 1, lower + segment + 1, lower + segment);
                    }
                }
            }

            AssetHandle Upload(Renderer& renderer) const
            {
                const JArrayView<MeshVertex> vertices{m_vertices.Data(), static_cast<UInt32>(m_vertices.Size())};
                const JArrayView<UInt32> indices{m_indices.Data(), static_cast<UInt32>(m_indices.Size())};
                return renderer.RegisterMesh(vertices, indices);
            }

        private:
            Vector3 PositionOf(UInt32 index) const
            {
                const MeshVertex& vertex = m_vertices[index];
                return Vector3{vertex.position[0], vertex.position[1], vertex.position[2]};
            }

            Vector3 NormalOf(UInt32 index) const
            {
                const MeshVertex& vertex = m_vertices[index];
                return Vector3{vertex.normal[0], vertex.normal[1], vertex.normal[2]};
            }

            Array<MeshVertex> m_vertices;
            Array<UInt32> m_indices;
        };

        // 위 극에서 아래 극까지 `SphereRows` 칸이다. `shiftAbove`·`shiftBelow` 는 적도 위·아래 반구를 띄우는 높이다 - 캡슐은 두 반구를
        // ±0.5 로 벌리고 적도 고리를 두 번 두어, 그 사이가 원기둥 띠가 된다. 구는 둘 다 0 이다.
        void AddRoundRows(MeshBuilder& builder, Float shiftAbove, Float shiftBelow)
        {
            Ring rings[SphereRows + 2] = {};
            UInt32 count = 0;
            for (UInt32 row = 0; row <= SphereRows; ++row)
            {
                const Float polar = Pi * static_cast<Float>(row) / static_cast<Float>(SphereRows);
                const Float up = std::cos(polar);
                const Float out = std::sin(polar);
                const Bool equator = row * 2 == SphereRows;
                if (equator && shiftAbove != shiftBelow)
                {
                    rings[count++] = Ring{shiftAbove, 0.5f, 0.0f, 1.0f};
                    rings[count++] = Ring{shiftBelow, 0.5f, 0.0f, 1.0f};
                    continue;
                }
                const Float shift = row * 2 < SphereRows ? shiftAbove : shiftBelow;
                rings[count++] = Ring{shift + 0.5f * up, 0.5f * out, up, out};
            }
            builder.AddLathe(rings, count);
        }

        AssetHandle UploadSphere(Renderer& renderer)
        {
            MeshBuilder builder;
            AddRoundRows(builder, 0.0f, 0.0f);
            return builder.Upload(renderer);
        }

        AssetHandle UploadCapsule(Renderer& renderer)
        {
            MeshBuilder builder;
            AddRoundRows(builder, 0.5f, -0.5f);
            return builder.Upload(renderer);
        }

        AssetHandle UploadCylinder(Renderer& renderer)
        {
            MeshBuilder builder;
            const Ring side[2] = {{1.0f, 0.5f, 0.0f, 1.0f}, {-1.0f, 0.5f, 0.0f, 1.0f}};
            builder.AddLathe(side, 2);
            // 뚜껑은 법선이 위·아래로 따로라 옆면과 정점을 나누지 않는다.
            const Ring top[2] = {{1.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 0.5f, 1.0f, 0.0f}};
            builder.AddLathe(top, 2);
            const Ring bottom[2] = {{-1.0f, 0.5f, -1.0f, 0.0f}, {-1.0f, 0.0f, -1.0f, 0.0f}};
            builder.AddLathe(bottom, 2);
            return builder.Upload(renderer);
        }

        AssetHandle UploadPlane(Renderer& renderer)
        {
            MeshBuilder builder;
            const Vector3 up{0.0f, 1.0f, 0.0f};
            const UInt32 a = builder.AddVertex(Vector3{-5.0f, 0.0f, -5.0f}, up);
            const UInt32 b = builder.AddVertex(Vector3{5.0f, 0.0f, -5.0f}, up);
            const UInt32 c = builder.AddVertex(Vector3{5.0f, 0.0f, 5.0f}, up);
            const UInt32 d = builder.AddVertex(Vector3{-5.0f, 0.0f, 5.0f}, up);
            builder.AddQuad(a, b, c, d);
            return builder.Upload(renderer);
        }

        AssetHandle UploadCube(Renderer& renderer)
        {
            return renderer.RegisterMesh({CubeVertices, 24}, {CubeIndices, 36});
        }

        struct BuiltinEntry
        {
            const char* name;
            const char* uuidName;
            AssetHandle (*Upload)(Renderer& renderer);
        };

        // 이름은 번역하지 않는다 - 컴포넌트 이름처럼 그 자리에서 무엇인지 가리키는 이름이다. 차례가 메뉴와 목록의 차례다.
        constexpr BuiltinEntry BuiltinEntries[] = {
            {"Cube", "builtin/cube", &UploadCube},
            {"Sphere", "builtin/sphere", &UploadSphere},
            {"Capsule", "builtin/capsule", &UploadCapsule},
            {"Cylinder", "builtin/cylinder", &UploadCylinder},
            {"Plane", "builtin/plane", &UploadPlane},
        };
        constexpr UInt32 BuiltinEntryCount = static_cast<UInt32>(sizeof(BuiltinEntries) / sizeof(BuiltinEntries[0]));
    }

    AssetId MeshLibrary::BuiltinCubeId()
    {
        return Uuid::FromName("builtin/cube");
    }

    UInt32 MeshLibrary::GetBuiltinCount()
    {
        return BuiltinEntryCount;
    }

    MeshLibrary::BuiltinMesh MeshLibrary::GetBuiltin(UInt32 index)
    {
        if (index >= BuiltinEntryCount)
        {
            return {};
        }
        const BuiltinEntry& entry = BuiltinEntries[index.Get()];
        return BuiltinMesh{entry.name, Uuid::FromName(entry.uuidName)};
    }

    Bool MeshLibrary::Initialize(Renderer* renderer)
    {
        m_renderer = renderer;
        m_entries.Clear();
        if (renderer == nullptr || false == renderer->IsInitialized())
        {
            return true;
        }
        for (UInt32 index = 0; index < BuiltinEntryCount; ++index)
        {
            const BuiltinEntry& entry = BuiltinEntries[index.Get()];
            const AssetHandle mesh = entry.Upload(*renderer);
            if (mesh.generation == 0)
            {
                return false;
            }
            Register(Uuid::FromName(entry.uuidName), mesh);
        }
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
            if (m_entries[index].id == id)
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
