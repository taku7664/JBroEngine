#include <JBro/Framework3DSystem/Rendering/MeshLibrary.h>

#include <JBro/Asset/Asset.h>
#include <JBro/Types/Int.h>

#include <JBro/Core/StableTypeId.h>
#include <JBro/Graphics/Renderer.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Math3D.h>
#include <JBro/Types/UInt.h>

#include <cmath>
#include <cstddef>
#include <cstring>

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

        // 면마다 네 꼭짓점은 바깥에서 볼 때 왼쪽 아래·오른쪽 아래·오른쪽 위·왼쪽 위 차례다. 그 차례의 UV(왼쪽 위가 0)다.
        constexpr Float CubeCornerUvs[4][2] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};

        constexpr UInt32 CubeIndices[36] = {
            0, 2, 1, 0, 3, 2,
            4, 6, 5, 4, 7, 6,
            8, 10, 9, 8, 11, 10,
            12, 14, 13, 12, 15, 14,
            16, 18, 17, 16, 19, 18,
            20, 22, 21, 20, 23, 22};

        // **둥근 도형은 고리를 돌려 만든다.** 고리 하나는 높이·반지름과 그 고리의 법선(위 성분·바깥 성분)이다. 이웃한 고리를
        // 사각형 띠로 잇는다. 반지름 0 인 고리는 극(꼭짓점)이다. 크기는 Unity 의 기본 도형과 같다 - 구 지름 1, 원기둥·캡슐 높이 2·
        // 반지름 0.5, 평면 10 x 10. `[가정]` (D-388 우선순위 2)
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
            UInt32 AddVertex(const Vector3& position, const Vector3& normal, Float u, Float v)
            {
                MeshVertex vertex;
                vertex.position[0] = position.x;
                vertex.position[1] = position.y;
                vertex.position[2] = position.z;
                vertex.normal[0] = normal.x;
                vertex.normal[1] = normal.y;
                vertex.normal[2] = normal.z;
                vertex.uv[0] = u;
                vertex.uv[1] = v;
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

            // 고리마다 `RoundSegments + 1` 개의 정점을 둔다(이음매의 첫·끝 정점은 자리가 같고 u 가 0 과 1 이다).
            // `planar` 가 거짓이면 원통 감기(u 는 둘레, v 는 첫 고리의 높이에서 마지막 고리의 높이로 0..1), 참이면 위에서 내려다본 평면 투영(뚜껑)이다.
            void AddLathe(const Ring* rings, UInt32 count, Bool planar)
            {
                const UInt32 first = static_cast<UInt32>(m_vertices.Size());
                const Float top = rings[0].y;
                const Float span = rings[count - 1].y - top;
                for (UInt32 ring = 0; ring < count; ++ring)
                {
                    for (UInt32 segment = 0; segment <= RoundSegments; ++segment)
                    {
                        const Float angle = 2.0f * Pi * static_cast<Float>(segment) / static_cast<Float>(RoundSegments);
                        const Float cosine = std::cos(angle);
                        const Float sine = std::sin(angle);
                        const Ring& at = rings[ring];
                        const Vector3 normal = Normalize(Vector3{at.normalRadial * cosine, at.normalY, at.normalRadial * sine});
                        const Vector3 position{at.radius * cosine, at.y, at.radius * sine};
                        const Float u = planar ? Float(0.5f + position.x) : Float(static_cast<Float>(segment) / static_cast<Float>(RoundSegments));
                        const Float v = planar ? Float(0.5f + position.z) : (span != 0.0f ? Float((at.y - top) / span) : Float(0.0f));
                        AddVertex(position, normal, u, v);
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

            // **탄젠트는 여기 한 곳에서 계산한다.** 삼각형마다 위치의 차이를 UV 의 차이로 풀어 +u 방향(T)과 +v 방향(B)을 얻고 정점에 더한 뒤,
            // 법선에 수직이 되게 깎아 정규화한다. w 는 cross(N, T) 가 B 와 같은 쪽이면 +1 이다. UV 가 퇴화한 삼각형(극의 한 점)은 더하지 않는다.
            void ComputeTangents()
            {
                Array<Vector3> tangents;
                Array<Vector3> bitangents;
                tangents.Resize(m_vertices.Size());
                bitangents.Resize(m_vertices.Size());
                for (std::size_t index = 0; index + 2 < m_indices.Size(); index += 3)
                {
                    const UInt32 corner[3] = {m_indices[index], m_indices[index + 1], m_indices[index + 2]};
                    const MeshVertex& a = m_vertices[corner[0].Get()];
                    const MeshVertex& b = m_vertices[corner[1].Get()];
                    const MeshVertex& c = m_vertices[corner[2].Get()];
                    const Vector3 edge1 = Subtract(PositionOf(corner[1]), PositionOf(corner[0]));
                    const Vector3 edge2 = Subtract(PositionOf(corner[2]), PositionOf(corner[0]));
                    const Float du1 = b.uv[0] - a.uv[0];
                    const Float dv1 = b.uv[1] - a.uv[1];
                    const Float du2 = c.uv[0] - a.uv[0];
                    const Float dv2 = c.uv[1] - a.uv[1];
                    const Float determinant = du1 * dv2 - du2 * dv1;
                    if (std::fabs(determinant.Get()) < 0.0000001f)
                    {
                        continue;
                    }
                    const Float inverse = 1.0f / determinant;
                    const Vector3 tangent = Scale(Subtract(Scale(edge1, dv2), Scale(edge2, dv1)), inverse);
                    const Vector3 bitangent = Scale(Subtract(Scale(edge2, du1), Scale(edge1, du2)), inverse);
                    for (const UInt32 at : corner)
                    {
                        tangents[at.Get()] = Add(tangents[at.Get()], tangent);
                        bitangents[at.Get()] = Add(bitangents[at.Get()], bitangent);
                    }
                }
                for (std::size_t index = 0; index < m_vertices.Size(); ++index)
                {
                    const Vector3 normal = NormalOf(static_cast<UInt32>(index));
                    Vector3 tangent = Subtract(tangents[index], Scale(normal, Dot(normal, tangents[index])));
                    if (Length(tangent) < 0.000001f)
                    {
                        // 쓸 삼각형이 없던 정점(극)은 법선에 수직인 아무 방향이다 - 그 자리는 넓이가 없어 보이지 않는다.
                        tangent = std::fabs(normal.y.Get()) < 0.99f ? Cross(Vector3{0.0f, 1.0f, 0.0f}, normal) : Vector3{1.0f, 0.0f, 0.0f};
                    }
                    tangent = Normalize(tangent);
                    MeshVertex& vertex = m_vertices[index];
                    vertex.tangent[0] = tangent.x;
                    vertex.tangent[1] = tangent.y;
                    vertex.tangent[2] = tangent.z;
                    vertex.tangent[3] = Dot(Cross(normal, tangent), bitangents[index]) < 0.0f ? -1.0f : 1.0f;
                }
            }

            void Take(Array<MeshVertex>& vertices, Array<UInt32>& indices)
            {
                ComputeTangents();
                vertices = std::move(m_vertices);
                indices = std::move(m_indices);
                m_vertices.Clear();
                m_indices.Clear();
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
            builder.AddLathe(rings, count, false);
        }

        void BuildSphere(MeshBuilder& builder)
        {
            AddRoundRows(builder, 0.0f, 0.0f);
        }

        void BuildCapsule(MeshBuilder& builder)
        {
            AddRoundRows(builder, 0.5f, -0.5f);
        }

        void BuildCylinder(MeshBuilder& builder)
        {
            const Ring side[2] = {{1.0f, 0.5f, 0.0f, 1.0f}, {-1.0f, 0.5f, 0.0f, 1.0f}};
            builder.AddLathe(side, 2, false);
            // 뚜껑은 법선이 위·아래로 따로라 옆면과 정점을 나누지 않는다.
            const Ring top[2] = {{1.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 0.5f, 1.0f, 0.0f}};
            builder.AddLathe(top, 2, true);
            const Ring bottom[2] = {{-1.0f, 0.5f, -1.0f, 0.0f}, {-1.0f, 0.0f, -1.0f, 0.0f}};
            builder.AddLathe(bottom, 2, true);
        }

        // 위에서 내려다볼 때 -Z 가 화면 위다 - u 는 x 를, v 는 z 를 따라 0..1 이다.
        void BuildPlane(MeshBuilder& builder)
        {
            const Vector3 up{0.0f, 1.0f, 0.0f};
            const UInt32 a = builder.AddVertex(Vector3{-5.0f, 0.0f, -5.0f}, up, 0.0f, 0.0f);
            const UInt32 b = builder.AddVertex(Vector3{5.0f, 0.0f, -5.0f}, up, 1.0f, 0.0f);
            const UInt32 c = builder.AddVertex(Vector3{5.0f, 0.0f, 5.0f}, up, 1.0f, 1.0f);
            const UInt32 d = builder.AddVertex(Vector3{-5.0f, 0.0f, 5.0f}, up, 0.0f, 1.0f);
            builder.AddQuad(a, b, c, d);
        }

        void BuildCube(MeshBuilder& builder)
        {
            for (UInt32 index = 0; index < 24; ++index)
            {
                const MeshVertex& corner = CubeVertices[index.Get()];
                const Float* uv = CubeCornerUvs[(index % 4).Get()];
                builder.AddVertex(Vector3{corner.position[0], corner.position[1], corner.position[2]},
                    Vector3{corner.normal[0], corner.normal[1], corner.normal[2]}, uv[0], uv[1]);
            }
            for (UInt32 index = 0; index < 36; index += 3)
            {
                builder.AddTriangle(CubeIndices[index.Get()], CubeIndices[(index + 1).Get()], CubeIndices[(index + 2).Get()]);
            }
        }

        struct BuiltinEntry
        {
            const char* name;
            const char* uuidName;
            void (*Build)(MeshBuilder& builder);
        };

        // 이름은 번역하지 않는다 - 컴포넌트 이름처럼 그 자리에서 무엇인지 가리키는 이름이다. 차례가 메뉴와 목록의 차례다.
        constexpr BuiltinEntry BuiltinEntries[] = {
            {"Cube", "builtin/cube", &BuildCube},
            {"Sphere", "builtin/sphere", &BuildSphere},
            {"Capsule", "builtin/capsule", &BuildCapsule},
            {"Cylinder", "builtin/cylinder", &BuildCylinder},
            {"Plane", "builtin/plane", &BuildPlane},
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

    Bool MeshLibrary::BuildBuiltin(UInt32 index, Array<MeshVertex>& vertices, Array<UInt32>& indices)
    {
        if (index >= BuiltinEntryCount)
        {
            return false;
        }
        MeshBuilder builder;
        BuiltinEntries[index.Get()].Build(builder);
        builder.Take(vertices, indices);
        return true;
    }

    Bool MeshLibrary::Initialize(Renderer* renderer, AssetSystem* assets)
    {
        m_renderer = renderer;
        m_assets = assets;
        m_entries.Clear();
        m_assetEntries.Clear();
        if (renderer == nullptr || false == renderer->IsInitialized())
        {
            return true;
        }
        for (UInt32 index = 0; index < BuiltinEntryCount; ++index)
        {
            const BuiltinEntry& entry = BuiltinEntries[index.Get()];
            Array<MeshVertex> vertices;
            Array<UInt32> indices;
            BuildBuiltin(index, vertices, indices);
            const AssetHandle mesh = renderer->RegisterMesh({vertices.Data(), static_cast<UInt32>(vertices.Size())},
                {indices.Data(), static_cast<UInt32>(indices.Size())}, true);
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
            for (std::size_t index = 0; index < m_assetEntries.Size(); ++index)
            {
                if (m_assetEntries[index].rendererMesh.generation != 0)
                {
                    m_renderer->UnregisterMesh(m_assetEntries[index].rendererMesh);
                }
            }
        }
        m_entries.Clear();
        m_assetEntries.Clear();
        m_renderer = nullptr;
        m_assets = nullptr;
    }

    AssetHandle MeshLibrary::ResolveAsset(AssetHandle meshAsset)
    {
        if (m_assets == nullptr || m_renderer == nullptr || false == m_renderer->IsInitialized() || meshAsset.generation == 0)
        {
            return {};
        }
        const MeshData* mesh = m_assets->GetMesh(meshAsset);
        if (mesh == nullptr || mesh->vertices.Size() == 0 || mesh->indices.Size() == 0)
        {
            return {};
        }
        // 풀의 슬롯 번호로 자리를 판다(위 8 비트는 타입, D-322 (2)). 표는 첫 만남 때만 자란다 - 프레임마다가 아니다.
        const UInt32 slot = meshAsset.index & ((1u << 24) - 1);
        if (slot >= m_assetEntries.Size())
        {
            m_assetEntries.Resize(static_cast<std::size_t>(slot) + 1);
        }
        AssetEntry& entry = m_assetEntries[slot.Get()];
        const Bool sameAsset = entry.asset.index == meshAsset.index && entry.asset.generation == meshAsset.generation;
        if (sameAsset && entry.rendererMesh.generation != 0 && entry.dataGeneration == mesh->dataGeneration)
        {
            return entry.rendererMesh;
        }
        if (sameAsset && entry.rendererMesh.generation == 0 && entry.failedGeneration == mesh->dataGeneration)
        {
            return {};
        }
        // 처음 만났거나, 슬롯을 다른 메시가 다시 썼거나, 같은 메시가 다시 읽혔다(세대가 올랐다). 옛것을 내리고 새로 올린다.
        if (entry.rendererMesh.generation != 0)
        {
            m_renderer->UnregisterMesh(entry.rendererMesh);
            entry.rendererMesh = {};
        }
        // 렌더러의 정점은 모델의 CPU 정점과 같은 모양이다(위치·법선·UV·탄젠트, material-plan 1 단계) - 통째로 옮긴다.
        static_assert(sizeof(MeshVertex) == sizeof(ModelVertex) && offsetof(MeshVertex, normal) == offsetof(ModelVertex, normal)
                && offsetof(MeshVertex, uv) == offsetof(ModelVertex, uv) && offsetof(MeshVertex, tangent) == offsetof(ModelVertex, tangent),
            "the renderer's mesh vertex and the model vertex must keep one layout");
        Array<MeshVertex> vertices;
        vertices.Resize(mesh->vertices.Size());
        std::memcpy(vertices.Data(), mesh->vertices.Data(), vertices.Size() * sizeof(MeshVertex));
        const JArrayView<MeshVertex> vertexView{vertices.Data(), static_cast<UInt32>(vertices.Size())};
        const JArrayView<UInt32> indexView{mesh->indices.Data(), static_cast<UInt32>(mesh->indices.Size())};
        entry.asset = meshAsset;
        // 스킨 메시(D-458)는 뼈 번호·가중치 흐름을 함께 올린다. 뼈 번호는 정수를 그대로 든 실수다(셰이더가 반올림해 읽는다).
        if (mesh->skin.Size() == mesh->vertices.Size() && false == mesh->skin.IsEmpty())
        {
            Array<MeshSkinVertex> skin;
            skin.Resize(mesh->skin.Size());
            for (std::size_t at = 0; at < skin.Size(); ++at)
            {
                for (UInt32 slot = 0; slot < 4; ++slot)
                {
                    skin[at].bones[slot.Get()] = static_cast<JBro::Float>(mesh->skin[at].bones[slot.Get()].Get());
                    skin[at].weights[slot.Get()] = mesh->skin[at].weights[slot.Get()];
                }
            }
            entry.rendererMesh = m_renderer->RegisterSkinnedMesh(vertexView, indexView, mesh->hasTangents,
                {skin.Data(), static_cast<UInt32>(skin.Size())});
        }
        else
        {
            entry.rendererMesh = m_renderer->RegisterMesh(vertexView, indexView, mesh->hasTangents);
        }
        if (entry.rendererMesh.generation == 0)
        {
            entry.failedGeneration = mesh->dataGeneration;
            return {};
        }
        entry.dataGeneration = mesh->dataGeneration;
        return entry.rendererMesh;
    }

    std::size_t MeshLibrary::GetUploadedAssetCount() const
    {
        std::size_t count = 0;
        for (std::size_t index = 0; index < m_assetEntries.Size(); ++index)
        {
            count += m_assetEntries[index].rendererMesh.generation != 0 ? 1 : 0;
        }
        return count;
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
