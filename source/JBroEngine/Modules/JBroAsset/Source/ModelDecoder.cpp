#include <JBro/Asset/ModelDecoder.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

// cgltf 의 구현은 이 번역 단위 하나에만 켠다(ThirdParty/README). ufbx 는 자기 정적 라이브러리(`ufbx.vcxproj`)다.
// 고치지 않은 외부 소스라 이 include 동안만 경고를 끈다(`fopen`·`strncpy` 의 C4996 따위) - 우리 경고 수준을 강제하면 올릴 때마다 손대야 한다.
#pragma warning(push, 0)
#pragma warning(disable : 4996)
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#pragma warning(pop)
#include <ufbx.h>
// 탄젠트 계산(D-444). 자기 정적 라이브러리(`mikktspace.vcxproj`)이고 이 번역 단위만 부른다.
#include <mikktspace.h>

namespace JBro
{
    namespace
    {
        // **두 디코더가 내놓는 공통 모양**이다. 삼각형마다 꼭짓점 셋을 그대로 늘어놓는다(합치지 않음) - 법선 계산과 감김 뒤집기가 쉽고,
        // 합치기는 마무리(`Finish`)가 한 번에 한다. 감김은 파일의 것(바깥에서 볼 때 반시계)이다.
        struct RawMesh
        {
            String name;
            Array<ModelVertex> corners;
            Int32 material = -1;
            Bool hasNormals = false;
            Bool hasUvs = false;
            Bool hasTangents = false;
            // 스킨 메시면 꼭짓점마다 하나다(D-456). 합치기는 정점과 함께 견준다 - 위치가 같아도 가중치가 다르면 다른 정점이다.
            Array<ModelSkinWeights> skinCorners;
            Int32 skeleton = -1;
        };

        Vector3 PositionOf(const ModelVertex& vertex)
        {
            return Vector3{vertex.position[0], vertex.position[1], vertex.position[2]};
        }

        // **법선을 계산한다**(D-392 `normals`·`smoothingAngle`). 같은 자리의 꼭짓점들 가운데 면 법선이 `smoothingAngle` 안에 드는 면들의 법선을
        // 넓이 가중으로 더한다 - 상자의 모서리는 각지고 공은 매끈하다. 파일의 감김(반시계)으로 잰 면 법선이 바깥쪽이다.
        void CalculateNormals(Array<ModelVertex>& corners, Float smoothingAngleDegrees)
        {
            const std::size_t count = corners.Size() - corners.Size() % 3;
            Array<Vector3> faceNormals;
            faceNormals.Resize(count / 3);
            for (std::size_t face = 0; face < count / 3; ++face)
            {
                const Vector3 a = PositionOf(corners[face * 3]);
                const Vector3 b = PositionOf(corners[face * 3 + 1]);
                const Vector3 c = PositionOf(corners[face * 3 + 2]);
                faceNormals[face] = Cross(Subtract(b, a), Subtract(c, a));
            }
            // 자리가 같은 꼭짓점끼리 모은다. 정확히 같은 수만 같은 자리다 - 파일이 이은 꼭짓점은 같은 수를 쓴다.
            Array<UInt32> order;
            order.Resize(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                order[index] = static_cast<UInt32>(index);
            }
            const auto less = [&](UInt32 left, UInt32 right) {
                return std::memcmp(corners[left.Get()].position, corners[right.Get()].position, sizeof(corners[0].position)) < 0;
            };
            std::sort(order.Data(), order.Data() + order.Size(), less);
            const Float cosine = std::cos(std::clamp(smoothingAngleDegrees.Get(), 0.0f, 180.0f) * 3.14159265f / 180.0f);
            Array<Vector3> normals;
            normals.Resize(count);
            std::size_t start = 0;
            while (start < count)
            {
                std::size_t end = start + 1;
                while (end < count && false == less(order[start], order[end]))
                {
                    ++end;
                }
                for (std::size_t self = start; self < end; ++self)
                {
                    const UInt32 corner = order[self];
                    const Vector3 own = faceNormals[corner.Get() / 3];
                    const Vector3 ownUnit = Length(own) > 0.0f ? Normalize(own) : own;
                    Vector3 sum{0.0f, 0.0f, 0.0f};
                    for (std::size_t other = start; other < end; ++other)
                    {
                        const Vector3 theirs = faceNormals[order[other].Get() / 3];
                        if (Length(theirs) <= 0.0f || Length(own) <= 0.0f || Dot(ownUnit, Normalize(theirs)) >= cosine)
                        {
                            sum = Add(sum, theirs);
                        }
                    }
                    normals[corner.Get()] = Length(sum) > 0.0f ? Normalize(sum) : Vector3{0.0f, 1.0f, 0.0f};
                }
                start = end;
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                corners[index].normal[0] = normals[index].x;
                corners[index].normal[1] = normals[index].y;
                corners[index].normal[2] = normals[index].z;
            }
        }

        // **탄젠트를 계산한다**(D-444, glTF 가 권하는 MikkTSpace). 삼각형마다 꼭짓점 셋이 그대로 늘어선 `corners` 를 그대로 넘긴다.
        // MikkTSpace 는 v 가 위로 느는 UV 를 기대하고 엔진의 UV 는 왼쪽 위가 0 이라(v 가 아래로 는다) v 를 뒤집어 넘긴다 - 그래야 바이탄젠트
        // cross(n, t) x w 가 그림의 위쪽(노멀맵의 +Y)이다. 파일 탄젠트와 같은 규약이다.
        struct TangentSource
        {
            Array<ModelVertex>* corners = nullptr;
            int faces = 0;
        };

        int TangentFaceCount(const SMikkTSpaceContext* context)
        {
            return static_cast<const TangentSource*>(context->m_pUserData)->faces;
        }

        int TangentCornerCount(const SMikkTSpaceContext*, const int)
        {
            return 3;
        }

        ModelVertex& TangentCorner(const SMikkTSpaceContext* context, const int face, const int corner)
        {
            return (*static_cast<const TangentSource*>(context->m_pUserData)->corners)[static_cast<std::size_t>(face) * 3 + static_cast<std::size_t>(corner)];
        }

        void TangentPosition(const SMikkTSpaceContext* context, float position[], const int face, const int corner)
        {
            const ModelVertex& vertex = TangentCorner(context, face, corner);
            position[0] = vertex.position[0].Get();
            position[1] = vertex.position[1].Get();
            position[2] = vertex.position[2].Get();
        }

        void TangentNormal(const SMikkTSpaceContext* context, float normal[], const int face, const int corner)
        {
            const ModelVertex& vertex = TangentCorner(context, face, corner);
            normal[0] = vertex.normal[0].Get();
            normal[1] = vertex.normal[1].Get();
            normal[2] = vertex.normal[2].Get();
        }

        void TangentUv(const SMikkTSpaceContext* context, float uv[], const int face, const int corner)
        {
            const ModelVertex& vertex = TangentCorner(context, face, corner);
            uv[0] = vertex.uv[0].Get();
            uv[1] = 1.0f - vertex.uv[1].Get();
        }

        void TangentWrite(const SMikkTSpaceContext* context, const float tangent[], const float sign, const int face, const int corner)
        {
            ModelVertex& vertex = TangentCorner(context, face, corner);
            vertex.tangent[0] = tangent[0];
            vertex.tangent[1] = tangent[1];
            vertex.tangent[2] = tangent[2];
            vertex.tangent[3] = sign < 0.0f ? -1.0f : 1.0f;
        }

        Bool CalculateTangents(Array<ModelVertex>& corners)
        {
            TangentSource source;
            source.corners = &corners;
            source.faces = static_cast<int>(corners.Size() / 3);
            if (source.faces == 0)
            {
                return false;
            }
            SMikkTSpaceInterface callbacks{};
            callbacks.m_getNumFaces = &TangentFaceCount;
            callbacks.m_getNumVerticesOfFace = &TangentCornerCount;
            callbacks.m_getPosition = &TangentPosition;
            callbacks.m_getNormal = &TangentNormal;
            callbacks.m_getTexCoord = &TangentUv;
            callbacks.m_setTSpaceBasic = &TangentWrite;
            SMikkTSpaceContext context{};
            context.m_pInterface = &callbacks;
            context.m_pUserData = &source;
            return genTangSpaceDefault(&context) != 0;
        }

        // **마무리**: 법선(옵션), 배율, 감김 뒤집기, 정점 합치기. 엔진은 바깥에서 볼 때 **시계** 방향이 앞면이다(빌트인 정육면체와 같은 규약, D-108) -
        // 파일(glTF·FBX·OBJ)은 반시계라 삼각형마다 둘째와 셋째를 바꾼다.
        ModelMesh Finish(RawMesh& raw, const ModelImportOptions& options)
        {
            const Bool calculate = options.normals == ModelNormals::Calculate || false == raw.hasNormals;
            if (calculate)
            {
                CalculateNormals(raw.corners, options.smoothingAngle);
            }
            // 법선을 다시 계산했으면 파일의 탄젠트는 그 법선과 맞지 않는다 - 버리고 새 법선으로 다시 잰다. 파일에 없어도 잰다(D-444).
            // 감김을 뒤집기 전에 잰다 - MikkTSpace 는 감김과 무관하지만 꼭짓점의 차례는 그대로여야 한다(뒤집기는 꼭짓점째 바꾼다).
            Bool tangents = raw.hasTangents && false == calculate;
            if (false == tangents && raw.hasUvs)
            {
                tangents = CalculateTangents(raw.corners);
            }
            const Float scale = std::isfinite(options.scale.Get()) && options.scale > 0.0f ? options.scale : Float(1.0f);
            const std::size_t count = raw.corners.Size() - raw.corners.Size() % 3;
            for (std::size_t index = 0; index < count; ++index)
            {
                for (Int32 axis = 0; axis < 3; ++axis)
                {
                    raw.corners[index].position[axis.Get()] = raw.corners[index].position[axis.Get()] * scale;
                }
            }
            const Bool skinned = raw.skinCorners.Size() == raw.corners.Size() && raw.skeleton >= 0;
            for (std::size_t face = 0; face < count / 3; ++face)
            {
                std::swap(raw.corners[face * 3 + 1], raw.corners[face * 3 + 2]);
                if (skinned)
                {
                    std::swap(raw.skinCorners[face * 3 + 1], raw.skinCorners[face * 3 + 2]);
                }
            }

            // 같은 꼭짓점(위치·법선·UV·탄젠트가 모두 같은 것)을 하나로 합친다. 바이트를 견줘 정렬하고 이웃끼리 묶는다.
            Array<UInt32> order;
            order.Resize(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                order[index] = static_cast<UInt32>(index);
            }
            const auto compare = [&](UInt32 left, UInt32 right) {
                const int vertex = std::memcmp(&raw.corners[left.Get()], &raw.corners[right.Get()], sizeof(ModelVertex));
                if (vertex != 0 || false == skinned)
                {
                    return vertex;
                }
                return std::memcmp(&raw.skinCorners[left.Get()], &raw.skinCorners[right.Get()], sizeof(ModelSkinWeights));
            };
            std::sort(order.Data(), order.Data() + order.Size(), [&](UInt32 left, UInt32 right) { return compare(left, right) < 0; });
            ModelMesh mesh;
            mesh.name = raw.name;
            mesh.material = raw.material;
            mesh.hasUvs = raw.hasUvs;
            mesh.hasTangents = tangents;
            mesh.skeleton = skinned ? raw.skeleton : Int32(-1);
            mesh.indices.Resize(count);
            for (std::size_t at = 0; at < count; ++at)
            {
                if (at == 0 || compare(order[at - 1], order[at]) != 0)
                {
                    mesh.vertices.Add(raw.corners[order[at].Get()]);
                    if (skinned)
                    {
                        mesh.skin.Add(raw.skinCorners[order[at].Get()]);
                    }
                }
                mesh.indices[order[at].Get()] = static_cast<UInt32>(mesh.vertices.Size() - 1);
            }
            return mesh;
        }

        // 한 파일 안에서 하위 에셋 이름이 겹치지 않게 한다. 겹치면 뒤에 ` (2)` 를 붙인다 - `.jmeta` 의 하위 아이디가 이름으로 붙는다(D-391).
        String UniqueName(const Array<RawMesh>& meshes, const String& wanted)
        {
            const auto taken = [&](const String& name) {
                for (std::size_t index = 0; index < meshes.Size(); ++index)
                {
                    if (meshes[index].name == name)
                    {
                        return true;
                    }
                }
                return false;
            };
            if (false == taken(wanted))
            {
                return wanted;
            }
            for (UInt32 suffix = 2;; ++suffix)
            {
                String candidate = wanted;
                candidate += " (";
                candidate += String(std::to_string(suffix.Get()).c_str());
                candidate += ")";
                if (false == taken(candidate))
                {
                    return candidate;
                }
            }
        }

        String SlashPath(const char* path, std::size_t length)
        {
            String result(path != nullptr ? std::string_view(path, length) : std::string_view());
            for (std::size_t index = 0; index < result.size(); ++index)
            {
                if (result[index] == '\\')
                {
                    result[index] = '/';
                }
            }
            return result;
        }

        void NoteImageUse(ModelData& model, Int32 image, ModelTextureSlot slot, Array<Bool>& used)
        {
            if (image < 0 || static_cast<std::size_t>(image.Get()) >= model.images.Size())
            {
                return;
            }
            if (false == used[static_cast<std::size_t>(image.Get())])
            {
                used[static_cast<std::size_t>(image.Get())] = true;
                model.images[static_cast<std::size_t>(image.Get())].firstUse = slot;
            }
        }

        // ── glTF (cgltf) ─────────────────────────────────────────────

        cgltf_result ReadSideFile(const cgltf_memory_options*, const cgltf_file_options* file, const char* path, cgltf_size* size, void** data)
        {
            const ModelFileReader* reader = static_cast<const ModelFileReader*>(file->user_data);
            Array<std::byte> bytes;
            if (reader == nullptr || reader->read == nullptr || false == reader->read(reader->user, path, bytes))
            {
                return cgltf_result_file_not_found;
            }
            void* copy = std::malloc(bytes.Size() > 0 ? bytes.Size() : 1);
            if (copy == nullptr)
            {
                return cgltf_result_out_of_memory;
            }
            if (bytes.Size() > 0)
            {
                std::memcpy(copy, bytes.Data(), bytes.Size());
            }
            *size = bytes.Size();
            *data = copy;
            return cgltf_result_success;
        }

        void ReleaseSideFile(const cgltf_memory_options*, const cgltf_file_options*, void* data)
        {
            std::free(data);
        }

        Int32 ImageOf(const cgltf_data* data, const cgltf_texture_view& view)
        {
            if (view.texture == nullptr || view.texture->image == nullptr)
            {
                return -1;
            }
            return static_cast<Int32>(view.texture->image - data->images);
        }

        // 꼭짓점을 행렬(열 우선)에서 나눠 낸다. 비균등 크기와 회전만 있다고 본다(전단은 버린다).
        void Decompose(const cgltf_float matrix[16], ModelNode& node)
        {
            node.position = Vector3{matrix[12], matrix[13], matrix[14]};
            const Vector3 axisX{matrix[0], matrix[1], matrix[2]};
            const Vector3 axisY{matrix[4], matrix[5], matrix[6]};
            const Vector3 axisZ{matrix[8], matrix[9], matrix[10]};
            node.scale = Vector3{Length(axisX), Length(axisY), Length(axisZ)};
            const Vector3 x = node.scale.x > 0.0f ? Scale(axisX, 1.0f / node.scale.x) : Vector3{1.0f, 0.0f, 0.0f};
            const Vector3 y = node.scale.y > 0.0f ? Scale(axisY, 1.0f / node.scale.y) : Vector3{0.0f, 1.0f, 0.0f};
            const Vector3 z = node.scale.z > 0.0f ? Scale(axisZ, 1.0f / node.scale.z) : Vector3{0.0f, 0.0f, 1.0f};
            const Float trace = x.x + y.y + z.z;
            Quaternion q;
            if (trace > 0.0f)
            {
                const Float s = std::sqrt(trace.Get() + 1.0f) * 2.0f;
                q = Quaternion{(y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s};
            }
            else if (x.x > y.y && x.x > z.z)
            {
                const Float s = std::sqrt(1.0f + x.x.Get() - y.y.Get() - z.z.Get()) * 2.0f;
                q = Quaternion{0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s};
            }
            else if (y.y > z.z)
            {
                const Float s = std::sqrt(1.0f + y.y.Get() - x.x.Get() - z.z.Get()) * 2.0f;
                q = Quaternion{(y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s};
            }
            else
            {
                const Float s = std::sqrt(1.0f + z.z.Get() - x.x.Get() - y.y.Get()) * 2.0f;
                q = Quaternion{(z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s};
            }
            node.rotation = Normalize(q);
        }

        // glTF 의 열 우선 행렬을 엔진의 `values[row * 4 + column]` 로 옮긴다. 위치는 임포트 배율로 늘린다(균일 배율이라 회전·크기는 그대로다).
        Matrix4x4 FromGltfMatrix(const cgltf_float source[16], Float scale)
        {
            Matrix4x4 result;
            for (UInt32 row = 0; row < 4; ++row)
            {
                for (UInt32 column = 0; column < 4; ++column)
                {
                    result.values[row.Get() * 4 + column.Get()] = source[column.Get() * 4 + row.Get()];
                }
            }
            for (UInt32 row = 0; row < 3; ++row)
            {
                result.values[row.Get() * 4 + 3] = result.values[row.Get() * 4 + 3] * scale;
            }
            return result;
        }

        // 아핀 행렬의 역이다(마지막 행이 0 0 0 1). 3x3 의 역과 옮김의 역이다.
        Bool InvertAffine(const cgltf_float source[16], cgltf_float result[16])
        {
            // 열 우선: m(row, column) = source[column * 4 + row].
            const auto m = [&](int row, int column) { return static_cast<double>(source[column * 4 + row]); };
            const double a = m(0, 0), b = m(0, 1), c = m(0, 2);
            const double d = m(1, 0), e = m(1, 1), f = m(1, 2);
            const double g = m(2, 0), h = m(2, 1), i = m(2, 2);
            const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
            if (std::abs(determinant) < 1e-20)
            {
                return false;
            }
            const double inverse[3][3] = {
                {(e * i - f * h) / determinant, (c * h - b * i) / determinant, (b * f - c * e) / determinant},
                {(f * g - d * i) / determinant, (a * i - c * g) / determinant, (c * d - a * f) / determinant},
                {(d * h - e * g) / determinant, (b * g - a * h) / determinant, (a * e - b * d) / determinant},
            };
            const double t[3] = {m(0, 3), m(1, 3), m(2, 3)};
            for (int row = 0; row < 3; ++row)
            {
                for (int column = 0; column < 3; ++column)
                {
                    result[column * 4 + row] = static_cast<cgltf_float>(inverse[row][column]);
                }
                result[12 + row] = static_cast<cgltf_float>(-(inverse[row][0] * t[0] + inverse[row][1] * t[1] + inverse[row][2] * t[2]));
                result[row * 4 + 3] = 0.0f;
            }
            result[15] = 1.0f;
            return true;
        }

        // 열 우선 두 행렬의 곱(left x right)이다.
        void MultiplyGltf(const cgltf_float left[16], const cgltf_float right[16], cgltf_float result[16])
        {
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    cgltf_float value = 0.0f;
                    for (int at = 0; at < 4; ++at)
                    {
                        value += left[at * 4 + row] * right[column * 4 + at];
                    }
                    result[column * 4 + row] = value;
                }
            }
        }

        // glTF 의 스킨을 뼈대로 옮긴다(D-456). 뼈는 스킨의 `joints` 차례이고 부모는 가장 가까운 조상 뼈다. 사이에 뼈가 아닌 노드가 끼면
        // 그 노드들까지 접은 상대 자세를 쓴다. 이름은 뼈대 안에서 겹치지 않게 하고, 노드 → 뼈 이름 표를 `boneNameOfNode` 에 남긴다(트랙이 쓴다).
        // `boneOfJoint` 는 스킨의 `joints` 번호 → 뼈 번호다(부모가 앞에 오게 다시 늘어놓으므로 다르다). 메시의 `JOINTS_0` 이 이것으로 옮겨진다.
        ModelSkeleton ReadGltfSkeleton(const cgltf_data* data, const cgltf_skin& skin, Float scale, Array<String>& boneNameOfNode,
            Array<UInt32>& boneOfJoint)
        {
            ModelSkeleton skeleton;
            skeleton.name = skin.name != nullptr ? String(skin.name) : String("Skeleton");
            const auto jointIndexOf = [&](const cgltf_node* node) -> Int32 {
                for (cgltf_size at = 0; at < skin.joints_count; ++at)
                {
                    if (skin.joints[at] == node)
                    {
                        return static_cast<Int32>(at);
                    }
                }
                return -1;
            };
            Array<String> taken;
            Bool rootParentSet = false;
            for (cgltf_size at = 0; at < skin.joints_count; ++at)
            {
                const cgltf_node* node = skin.joints[at];
                ModelBone bone;
                String wanted = node->name != nullptr ? String(node->name) : String("Bone");
                String name = wanted;
                for (Int32 suffix = 2; taken.Contains(name); ++suffix)
                {
                    name = wanted + " (" + String(std::to_string(suffix.Get()).c_str()) + ")";
                }
                taken.Add(name);
                bone.name = name;
                boneNameOfNode[static_cast<std::size_t>(node - data->nodes)] = name;
                // 가장 가까운 조상 뼈와, 그 사이의 뼈가 아닌 노드들.
                const cgltf_node* ancestor = node->parent;
                while (ancestor != nullptr && jointIndexOf(ancestor) < 0)
                {
                    ancestor = ancestor->parent;
                }
                bone.parent = ancestor != nullptr ? jointIndexOf(ancestor) : Int32(-1);
                cgltf_float relative[16];
                if (ancestor == nullptr || ancestor == node->parent)
                {
                    cgltf_node_transform_local(node, relative);
                }
                else
                {
                    cgltf_float nodeWorld[16];
                    cgltf_float ancestorWorld[16];
                    cgltf_float ancestorInverse[16];
                    cgltf_node_transform_world(node, nodeWorld);
                    cgltf_node_transform_world(ancestor, ancestorWorld);
                    if (false == InvertAffine(ancestorWorld, ancestorInverse))
                    {
                        cgltf_node_transform_local(node, relative);
                    }
                    else
                    {
                        MultiplyGltf(ancestorInverse, nodeWorld, relative);
                    }
                }
                ModelNode decomposed;
                Decompose(relative, decomposed);
                bone.position = Scale(decomposed.position, scale);
                bone.rotation = decomposed.rotation;
                bone.scale = decomposed.scale;
                if (skin.inverse_bind_matrices != nullptr)
                {
                    cgltf_float inverse[16];
                    if (cgltf_accessor_read_float(skin.inverse_bind_matrices, at, inverse, 16))
                    {
                        bone.inverseBind = FromGltfMatrix(inverse, scale);
                    }
                }
                if (bone.parent < 0 && node->parent != nullptr && false == rootParentSet)
                {
                    rootParentSet = true;
                    cgltf_float parentWorld[16];
                    cgltf_node_transform_world(node->parent, parentWorld);
                    skeleton.rootParent = FromGltfMatrix(parentWorld, scale);
                }
                skeleton.bones.Add(std::move(bone));
            }
            // 부모가 늘 앞에 오게 한다 - 런타임은 앞에서부터 한 번 훑어 월드 자세를 짓는다. 스킨의 차례가 그렇지 않으면 다시 늘어놓고 번호를 고친다.
            const std::size_t count = skeleton.bones.Size();
            Array<Int32> placed;
            placed.Resize(count);
            for (std::size_t at = 0; at < count; ++at)
            {
                placed[at] = -1;
            }
            Array<UInt32> order;
            while (order.Size() < count)
            {
                const std::size_t before = order.Size();
                for (std::size_t at = 0; at < count; ++at)
                {
                    const Int32 parent = skeleton.bones[at].parent;
                    if (placed[at] < 0 && (parent < 0 || placed[static_cast<std::size_t>(parent.Get())] >= 0))
                    {
                        placed[at] = static_cast<Int32>(order.Size());
                        order.Add(static_cast<UInt32>(at));
                    }
                }
                // 부모 고리가 있는 깨진 파일이다. 남은 것은 뿌리로 둔다.
                if (order.Size() == before)
                {
                    for (std::size_t at = 0; at < count; ++at)
                    {
                        if (placed[at] < 0)
                        {
                            skeleton.bones[at].parent = -1;
                            placed[at] = static_cast<Int32>(order.Size());
                            order.Add(static_cast<UInt32>(at));
                        }
                    }
                }
            }
            Array<ModelBone> sorted;
            sorted.Reserve(count);
            for (std::size_t at = 0; at < count; ++at)
            {
                ModelBone bone = std::move(skeleton.bones[order[at].Get()]);
                if (bone.parent >= 0)
                {
                    bone.parent = placed[static_cast<std::size_t>(bone.parent.Get())];
                }
                sorted.Add(std::move(bone));
            }
            skeleton.bones = std::move(sorted);
            boneOfJoint.Resize(count);
            for (std::size_t at = 0; at < count; ++at)
            {
                boneOfJoint[at] = static_cast<UInt32>(placed[at].Get());
            }
            return skeleton;
        }

        Bool DecodeGltf(JArrayView<std::byte> bytes, const ModelImportOptions& options, const ModelFileReader& reader,
            ModelData& model, String& error)
        {
            cgltf_options gltfOptions = {};
            gltfOptions.file.read = &ReadSideFile;
            gltfOptions.file.release = &ReleaseSideFile;
            gltfOptions.file.user_data = const_cast<ModelFileReader*>(&reader);
            cgltf_data* data = nullptr;
            if (cgltf_parse(&gltfOptions, bytes.data, bytes.size.Get(), &data) != cgltf_result_success)
            {
                error = "the glTF could not be parsed";
                return false;
            }
            struct Guard
            {
                cgltf_data* data;
                ~Guard() { cgltf_free(data); }
            } guard{data};
            // 바깥 버퍼(`.bin`)는 곁 파일 읽기로, `data:` 버퍼는 cgltf 가 푼다. 경로의 기준은 비워 둔다 - 읽는 쪽이 모델 폴더 기준으로 받는다.
            if (cgltf_load_buffers(&gltfOptions, data, "") != cgltf_result_success)
            {
                error = "a glTF buffer could not be read";
                return false;
            }

            if (options.importMaterials)
            {
                for (cgltf_size index = 0; index < data->images_count; ++index)
                {
                    const cgltf_image& source = data->images[index];
                    ModelImage image;
                    image.name = source.name != nullptr ? String(source.name) : String();
                    if (source.buffer_view != nullptr && source.buffer_view->buffer != nullptr && source.buffer_view->buffer->data != nullptr)
                    {
                        const std::byte* start = static_cast<const std::byte*>(source.buffer_view->buffer->data) + source.buffer_view->offset;
                        image.embedded.Resize(source.buffer_view->size);
                        std::memcpy(image.embedded.Data(), start, source.buffer_view->size);
                    }
                    else if (source.uri != nullptr && std::strncmp(source.uri, "data:", 5) == 0)
                    {
                        const char* comma = std::strchr(source.uri, ',');
                        if (comma != nullptr)
                        {
                            const char* base64 = comma + 1;
                            const std::size_t length = std::strlen(base64);
                            std::size_t padding = 0;
                            while (padding < length && base64[length - 1 - padding] == '=')
                            {
                                ++padding;
                            }
                            const cgltf_size size = length / 4 * 3 - padding;
                            void* decoded = nullptr;
                            if (cgltf_load_buffer_base64(&gltfOptions, size, base64, &decoded) == cgltf_result_success)
                            {
                                image.embedded.Resize(size);
                                std::memcpy(image.embedded.Data(), decoded, size);
                                std::free(decoded);
                            }
                        }
                    }
                    else if (source.uri != nullptr)
                    {
                        Array<char> uri;
                        uri.Resize(std::strlen(source.uri) + 1);
                        std::memcpy(uri.Data(), source.uri, uri.Size());
                        cgltf_decode_uri(uri.Data());
                        image.path = SlashPath(uri.Data(), std::strlen(uri.Data()));
                    }
                    if (image.embedded.Size() > 0)
                    {
                        image.contentHash = HashModelBytes({image.embedded.Data(), static_cast<UInt32>(image.embedded.Size())});
                    }
                    if (image.name.empty())
                    {
                        image.name = image.path.empty() ? String("Image") : image.path;
                    }
                    model.images.Add(std::move(image));
                }
                Array<Bool> used;
                used.Resize(model.images.Size());
                for (std::size_t index = 0; index < used.Size(); ++index)
                {
                    used[index] = false;
                }
                for (cgltf_size index = 0; index < data->materials_count; ++index)
                {
                    const cgltf_material& source = data->materials[index];
                    ModelMaterial material;
                    material.name = source.name != nullptr ? String(source.name) : String("Material");
                    if (source.has_pbr_metallic_roughness)
                    {
                        const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
                        for (Int32 channel = 0; channel < 4; ++channel)
                        {
                            material.baseColor[channel.Get()] = pbr.base_color_factor[channel.Get()];
                        }
                        material.metallic = pbr.metallic_factor;
                        material.roughness = pbr.roughness_factor;
                        material.images[static_cast<UInt32>(ModelTextureSlot::BaseColor).Get()] = ImageOf(data, pbr.base_color_texture);
                        material.images[static_cast<UInt32>(ModelTextureSlot::MetallicRoughness).Get()] = ImageOf(data, pbr.metallic_roughness_texture);
                    }
                    material.images[static_cast<UInt32>(ModelTextureSlot::Normal).Get()] = ImageOf(data, source.normal_texture);
                    material.images[static_cast<UInt32>(ModelTextureSlot::Occlusion).Get()] = ImageOf(data, source.occlusion_texture);
                    material.images[static_cast<UInt32>(ModelTextureSlot::Emissive).Get()] = ImageOf(data, source.emissive_texture);
                    for (Int32 channel = 0; channel < 3; ++channel)
                    {
                        material.emissive[channel.Get()] = source.emissive_factor[channel.Get()];
                    }
                    if (source.alpha_mode == cgltf_alpha_mode_mask)
                    {
                        material.alphaCutout = true;
                        material.alphaCutoff = source.alpha_cutoff;
                    }
                    material.alphaBlend = source.alpha_mode == cgltf_alpha_mode_blend;
                    material.doubleSided = source.double_sided != 0;
                    for (UInt32 slot = 0; slot < ModelTextureSlotCount; ++slot)
                    {
                        NoteImageUse(model, material.images[slot.Get()], static_cast<ModelTextureSlot>(slot.Get()), used);
                    }
                    model.materials.Add(std::move(material));
                }
            }

            // 스킨마다 뼈대 하나다(D-456). 노드 → 뼈 이름 표는 트랙이, 조인트 → 뼈 번호 표는 메시의 가중치가 쓴다.
            const Float importScale = std::isfinite(options.scale.Get()) && options.scale > 0.0f ? options.scale : Float(1.0f);
            Array<String> boneNameOfNode;
            boneNameOfNode.Resize(data->nodes_count);
            Array<Array<UInt32>> boneOfJointPerSkin;
            Array<String> skeletonNames;
            for (cgltf_size index = 0; index < data->skins_count; ++index)
            {
                Array<UInt32> boneOfJoint;
                ModelSkeleton skeleton = ReadGltfSkeleton(data, data->skins[index], importScale, boneNameOfNode, boneOfJoint);
                String name = skeleton.name;
                for (Int32 suffix = 2; skeletonNames.Contains(name); ++suffix)
                {
                    name = skeleton.name + " (" + String(std::to_string(suffix.Get()).c_str()) + ")";
                }
                skeletonNames.Add(name);
                skeleton.name = name;
                model.skeletons.Add(std::move(skeleton));
                boneOfJointPerSkin.Add(std::move(boneOfJoint));
            }
            // 메시를 그리는 노드가 스킨을 가리키면 그 메시는 그 뼈대의 스킨 메시다. 노드 여럿이 다른 스킨으로 한 메시를 쓰면 첫 것이다.
            Array<Int32> skinOfMesh;
            skinOfMesh.Resize(data->meshes_count);
            for (cgltf_size index = 0; index < data->meshes_count; ++index)
            {
                skinOfMesh[index] = -1;
            }
            for (cgltf_size index = 0; index < data->nodes_count; ++index)
            {
                const cgltf_node& node = data->nodes[index];
                if (node.mesh != nullptr && node.skin != nullptr && skinOfMesh[static_cast<std::size_t>(node.mesh - data->meshes)] < 0)
                {
                    skinOfMesh[static_cast<std::size_t>(node.mesh - data->meshes)] = static_cast<Int32>(node.skin - data->skins);
                }
            }

            // 메시마다 프리미티브(재질 하나)가 `ModelMesh` 하나다. 노드가 가리킬 수 있게 메시 → 결과 번호들을 남긴다.
            Array<RawMesh> raws;
            Array<Array<UInt32>> meshOutputs;
            meshOutputs.Resize(data->meshes_count);
            for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex)
            {
                const cgltf_mesh& source = data->meshes[meshIndex];
                const String baseName = source.name != nullptr ? String(source.name) : String("Mesh");
                for (cgltf_size primitiveIndex = 0; primitiveIndex < source.primitives_count; ++primitiveIndex)
                {
                    const cgltf_primitive& primitive = source.primitives[primitiveIndex];
                    if (primitive.type != cgltf_primitive_type_triangles)
                    {
                        continue;
                    }
                    const cgltf_accessor* positions = nullptr;
                    const cgltf_accessor* normals = nullptr;
                    const cgltf_accessor* uvs = nullptr;
                    const cgltf_accessor* tangents = nullptr;
                    const cgltf_accessor* joints = nullptr;
                    const cgltf_accessor* weights = nullptr;
                    for (cgltf_size attribute = 0; attribute < primitive.attributes_count; ++attribute)
                    {
                        const cgltf_attribute& entry = primitive.attributes[attribute];
                        if (entry.type == cgltf_attribute_type_joints && entry.index == 0)
                        {
                            joints = entry.data;
                        }
                        else if (entry.type == cgltf_attribute_type_weights && entry.index == 0)
                        {
                            weights = entry.data;
                        }
                        if (entry.type == cgltf_attribute_type_position)
                        {
                            positions = entry.data;
                        }
                        else if (entry.type == cgltf_attribute_type_normal)
                        {
                            normals = entry.data;
                        }
                        else if (entry.type == cgltf_attribute_type_texcoord && entry.index == 0)
                        {
                            uvs = entry.data;
                        }
                        else if (entry.type == cgltf_attribute_type_tangent)
                        {
                            tangents = entry.data;
                        }
                    }
                    if (positions == nullptr)
                    {
                        continue;
                    }
                    RawMesh raw;
                    raw.hasNormals = normals != nullptr;
                    raw.hasUvs = uvs != nullptr;
                    raw.hasTangents = tangents != nullptr;
                    raw.material = (options.importMaterials && primitive.material != nullptr)
                        ? static_cast<Int32>(primitive.material - data->materials) : Int32(-1);
                    String name = baseName;
                    if (source.primitives_count > 1)
                    {
                        name += "/";
                        name += primitive.material != nullptr && primitive.material->name != nullptr
                            ? String(primitive.material->name) : String(std::to_string(primitiveIndex).c_str());
                    }
                    raw.name = UniqueName(raws, name);
                    const cgltf_size cornerCount = primitive.indices != nullptr ? primitive.indices->count : positions->count;
                    raw.corners.Resize(cornerCount - cornerCount % 3);
                    const Int32 skinIndex = skinOfMesh[meshIndex];
                    if (skinIndex >= 0 && joints != nullptr && weights != nullptr)
                    {
                        raw.skeleton = skinIndex;
                        raw.skinCorners.Resize(raw.corners.Size());
                    }
                    for (cgltf_size corner = 0; corner < raw.corners.Size(); ++corner)
                    {
                        const cgltf_size vertex = primitive.indices != nullptr ? cgltf_accessor_read_index(primitive.indices, corner) : corner;
                        ModelVertex& out = raw.corners[corner];
                        // 뼈 넷과 가중치. 조인트 번호는 뼈대의 뼈 번호로 옮기고, 가중치는 합이 1 이 되게 다시 나눈다(파일이 정규화하지 않았어도).
                        if (raw.skeleton >= 0)
                        {
                            cgltf_uint jointValues[4] = {0, 0, 0, 0};
                            float weightValues[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                            cgltf_accessor_read_uint(joints, vertex, jointValues, 4);
                            cgltf_accessor_read_float(weights, vertex, weightValues, 4);
                            const Array<UInt32>& boneOfJoint = boneOfJointPerSkin[static_cast<std::size_t>(skinIndex.Get())];
                            ModelSkinWeights& skinOut = raw.skinCorners[corner];
                            Float sum = 0.0f;
                            for (Int32 slot = 0; slot < 4; ++slot)
                            {
                                const Float weight = weightValues[slot.Get()] > 0.0f ? Float(weightValues[slot.Get()]) : Float(0.0f);
                                const cgltf_uint joint = jointValues[slot.Get()];
                                const Bool known = joint < boneOfJoint.Size();
                                skinOut.bones[slot.Get()] = known ? boneOfJoint[joint] : UInt32(0);
                                skinOut.weights[slot.Get()] = known ? weight : Float(0.0f);
                                sum += skinOut.weights[slot.Get()];
                            }
                            for (Int32 slot = 0; slot < 4; ++slot)
                            {
                                skinOut.weights[slot.Get()] = sum > 0.0f ? Float(skinOut.weights[slot.Get()] / sum) : Float(slot == 0 ? 1.0f : 0.0f);
                                // 쓰지 않는 칸은 뼈 0 으로 둔다 - 같은 정점이 칸의 쓰레기 번호로 갈라지지 않게.
                                if (skinOut.weights[slot.Get()] == 0.0f && slot > 0)
                                {
                                    skinOut.bones[slot.Get()] = 0;
                                }
                            }
                        }
                        float values[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                        cgltf_accessor_read_float(positions, vertex, values, 3);
                        for (Int32 axis = 0; axis < 3; ++axis)
                        {
                            out.position[axis.Get()] = values[axis.Get()];
                        }
                        if (normals != nullptr && cgltf_accessor_read_float(normals, vertex, values, 3))
                        {
                            for (Int32 axis = 0; axis < 3; ++axis)
                            {
                                out.normal[axis.Get()] = values[axis.Get()];
                            }
                        }
                        // glTF 의 UV 는 왼쪽 위가 0 이라 엔진(D3D)과 같다.
                        if (uvs != nullptr && cgltf_accessor_read_float(uvs, vertex, values, 2))
                        {
                            out.uv[0] = values[0];
                            out.uv[1] = values[1];
                        }
                        if (tangents != nullptr && cgltf_accessor_read_float(tangents, vertex, values, 4))
                        {
                            for (Int32 axis = 0; axis < 4; ++axis)
                            {
                                out.tangent[axis.Get()] = values[axis.Get()];
                            }
                        }
                    }
                    meshOutputs[meshIndex].Add(static_cast<UInt32>(raws.Size()));
                    raws.Add(std::move(raw));
                }
            }
            for (std::size_t index = 0; index < raws.Size(); ++index)
            {
                model.meshes.Add(Finish(raws[index], options));
            }

            for (cgltf_size index = 0; index < data->nodes_count; ++index)
            {
                const cgltf_node& source = data->nodes[index];
                ModelNode node;
                node.name = source.name != nullptr ? String(source.name) : String("Node");
                node.parent = source.parent != nullptr ? static_cast<Int32>(source.parent - data->nodes) : Int32(-1);
                if (source.has_matrix)
                {
                    Decompose(source.matrix, node);
                }
                else
                {
                    if (source.has_translation)
                    {
                        node.position = Vector3{source.translation[0], source.translation[1], source.translation[2]};
                    }
                    if (source.has_rotation)
                    {
                        node.rotation = Quaternion{source.rotation[0], source.rotation[1], source.rotation[2], source.rotation[3]};
                    }
                    if (source.has_scale)
                    {
                        node.scale = Vector3{source.scale[0], source.scale[1], source.scale[2]};
                    }
                }
                const Float scale = std::isfinite(options.scale.Get()) && options.scale > 0.0f ? options.scale : Float(1.0f);
                node.position = Scale(node.position, scale);
                if (source.mesh != nullptr)
                {
                    node.meshes = meshOutputs[static_cast<std::size_t>(source.mesh - data->meshes)];
                }
                model.nodes.Add(std::move(node));
            }

            // 애니메이션마다 클립 하나다(D-456). 트랙은 노드의 뼈 이름(뼈가 아닌 노드면 노드 이름)을 가리킨다. 모프 가중치(`weights`)는 블렌드셰이프 단계다.
            Array<String> animationNames;
            for (cgltf_size index = 0; index < data->animations_count; ++index)
            {
                const cgltf_animation& source = data->animations[index];
                ModelAnimation animation;
                const String wanted = source.name != nullptr ? String(source.name) : String("Animation");
                animation.name = wanted;
                for (Int32 suffix = 2; animationNames.Contains(animation.name); ++suffix)
                {
                    animation.name = wanted + " (" + String(std::to_string(suffix.Get()).c_str()) + ")";
                }
                animationNames.Add(animation.name);
                for (cgltf_size channelIndex = 0; channelIndex < source.channels_count; ++channelIndex)
                {
                    const cgltf_animation_channel& channel = source.channels[channelIndex];
                    if (channel.target_node == nullptr || channel.sampler == nullptr || channel.sampler->input == nullptr || channel.sampler->output == nullptr)
                    {
                        continue;
                    }
                    ModelAnimationTrack track;
                    UInt32 components = 3;
                    if (channel.target_path == cgltf_animation_path_type_translation)
                    {
                        track.path = ModelTrackPath::Position;
                    }
                    else if (channel.target_path == cgltf_animation_path_type_rotation)
                    {
                        track.path = ModelTrackPath::Rotation;
                        components = 4;
                    }
                    else if (channel.target_path == cgltf_animation_path_type_scale)
                    {
                        track.path = ModelTrackPath::Scale;
                    }
                    else
                    {
                        continue;
                    }
                    const std::size_t nodeIndex = static_cast<std::size_t>(channel.target_node - data->nodes);
                    track.bone = false == boneNameOfNode[nodeIndex].empty() ? boneNameOfNode[nodeIndex]
                        : (channel.target_node->name != nullptr ? String(channel.target_node->name) : String("Node"));
                    const cgltf_animation_sampler& sampler = *channel.sampler;
                    track.interpolation = sampler.interpolation == cgltf_interpolation_type_step ? ModelInterpolation::Step : ModelInterpolation::Linear;
                    const Bool cubic = sampler.interpolation == cgltf_interpolation_type_cubic_spline;
                    const cgltf_size keys = sampler.input->count;
                    track.times.Resize(keys);
                    track.values.Resize(keys * components.Get());
                    for (cgltf_size key = 0; key < keys; ++key)
                    {
                        float time = 0.0f;
                        cgltf_accessor_read_float(sampler.input, key, &time, 1);
                        track.times[key] = time;
                        animation.duration = time > animation.duration ? Float(time) : animation.duration;
                        float value[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                        // 큐빅 스플라인은 키마다 (들어오는 접선, 값, 나가는 접선)이다 - 값만 읽는다.
                        cgltf_accessor_read_float(sampler.output, cubic ? key * 3 + 1 : key, value, components.Get());
                        for (UInt32 component = 0; component < components; ++component)
                        {
                            Float written = value[component.Get()];
                            if (track.path == ModelTrackPath::Position)
                            {
                                written = written * importScale;
                            }
                            track.values[key * components.Get() + component.Get()] = written;
                        }
                    }
                    animation.tracks.Add(std::move(track));
                }
                model.animations.Add(std::move(animation));
            }
            return true;
        }

        // ── FBX·OBJ (ufbx) ───────────────────────────────────────────

        bool OpenSideFile(void* user, ufbx_stream* stream, const char* path, size_t pathLength, const ufbx_open_file_info*)
        {
            const ModelFileReader* reader = static_cast<const ModelFileReader*>(user);
            if (reader == nullptr || reader->read == nullptr)
            {
                return false;
            }
            const String relative = SlashPath(path, pathLength);
            Array<std::byte> bytes;
            if (false == reader->read(reader->user, relative.c_str(), bytes))
            {
                return false;
            }
            // 복사한다(`no_copy` 끔) - `bytes` 는 이 함수가 끝나면 사라진다.
            ufbx_open_memory_opts memoryOptions = {};
            return ufbx_open_memory(stream, bytes.Data(), bytes.Size(), &memoryOptions, nullptr);
        }

        // ufbx 의 3x4 열 행렬을 엔진의 `values[row * 4 + column]` 로 옮긴다. 위치는 임포트 배율로 늘린다(`FromGltfMatrix` 와 같다).
        Matrix4x4 FromUfbxMatrix(const ufbx_matrix& source, Float scale)
        {
            Matrix4x4 result;
            for (UInt32 row = 0; row < 3; ++row)
            {
                for (UInt32 column = 0; column < 4; ++column)
                {
                    result.values[row.Get() * 4 + column.Get()] = static_cast<float>(source.cols[column.Get()].v[row.Get()]);
                }
                result.values[row.Get() * 4 + 3] = result.values[row.Get() * 4 + 3] * scale;
            }
            result.values[12] = 0.0f;
            result.values[13] = 0.0f;
            result.values[14] = 0.0f;
            result.values[15] = 1.0f;
            return result;
        }

        // 스킨 메시가 놓인 자리다 - 첫 인스턴스 노드의 `geometry_to_world`(변환된 공간)이고, 노드가 없으면 단위 행렬이다.
        ufbx_matrix FbxMeshPlace(const ufbx_mesh* mesh)
        {
            return mesh != nullptr && mesh->instances.count > 0 ? mesh->instances.data[0]->geometry_to_world : ufbx_identity_matrix;
        }

        Vector3 FromUfbxVector(const ufbx_vec3& value)
        {
            return Vector3{static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
        }

        // **FBX 의 뼈대**(D-467)다. 뼈는 스킨 클러스터가 가리키는 노드와, 그 위로 이어지는 뼈 노드(ufbx 의 `bone` 이 있는 LimbNode 따위)다 -
        // 가중치가 없는 중간 뼈도 자세가 이어지게 넣는다. 뿌리 뼈가 같은 뼈대가 하나이고, 한 스킨이 두 뿌리를 가리키면 두 뼈대를 합친다(메시 하나 = 뼈대 하나).
        struct FbxSkeletons
        {
            // 노드(typed_id)마다다. 뼈가 아니면 -1 이다.
            Array<Int32> skeletonOfNode;
            Array<Int32> boneOfNode;
            // 뼈의 쉬는 자세가 굽힌 애니메이션의 로컬 값과 같은 기준인가다 - 부모 뼈가 바로 위 노드일 때(뿌리는 뼈대의 뿌리 부모가 바로 위 노드일 때)다.
            // 사이에 뼈가 아닌 노드가 끼어 접은 뼈는 그 노드의 움직임을 모르므로 트랙을 받지 않는다.
            Array<Bool> animatable;
            // 스킨(typed_id)마다의 뼈대다.
            Array<Int32> skeletonOfSkin;
        };

        Bool IsFbxBone(const Array<Bool>& isBone, const ufbx_node* node)
        {
            return node != nullptr && false == node->is_root && node->typed_id < isBone.Size() && isBone[node->typed_id];
        }

        const ufbx_node* FbxBoneParent(const Array<Bool>& isBone, const ufbx_node* node)
        {
            const ufbx_node* above = node->parent;
            while (above != nullptr && false == above->is_root && false == IsFbxBone(isBone, above))
            {
                above = above->parent;
            }
            return IsFbxBone(isBone, above) ? above : nullptr;
        }

        UInt32 FbxDepth(const ufbx_node* node)
        {
            UInt32 depth = 0;
            for (const ufbx_node* above = node->parent; above != nullptr; above = above->parent)
            {
                ++depth;
            }
            return depth;
        }

        void ReadFbxSkeletons(const ufbx_scene* scene, Float scale, ModelData& model, FbxSkeletons& out)
        {
            const std::size_t nodeCount = scene->nodes.count;
            Array<Bool> isBone;
            isBone.Resize(nodeCount);
            out.skeletonOfNode.Resize(nodeCount);
            out.boneOfNode.Resize(nodeCount);
            out.animatable.Resize(nodeCount);
            for (std::size_t index = 0; index < nodeCount; ++index)
            {
                isBone[index] = false;
                out.skeletonOfNode[index] = -1;
                out.boneOfNode[index] = -1;
                out.animatable[index] = false;
            }
            out.skeletonOfSkin.Resize(scene->skin_deformers.count);
            for (std::size_t index = 0; index < out.skeletonOfSkin.Size(); ++index)
            {
                out.skeletonOfSkin[index] = -1;
            }
            for (std::size_t skinIndex = 0; skinIndex < scene->skin_deformers.count; ++skinIndex)
            {
                const ufbx_skin_deformer* skin = scene->skin_deformers.data[skinIndex];
                for (std::size_t at = 0; at < skin->clusters.count; ++at)
                {
                    const ufbx_node* node = skin->clusters.data[at]->bone_node;
                    if (node == nullptr || node->is_root || node->typed_id >= nodeCount)
                    {
                        continue;
                    }
                    isBone[node->typed_id] = true;
                    for (const ufbx_node* above = node->parent; above != nullptr && false == above->is_root && above->bone != nullptr
                         && false == isBone[above->typed_id]; above = above->parent)
                    {
                        isBone[above->typed_id] = true;
                    }
                }
            }

            // 뿌리 뼈마다 무리 하나로 시작해, 같은 스킨이 가리키는 뿌리끼리 합친다.
            Array<Int32> group;
            group.Resize(nodeCount);
            for (std::size_t index = 0; index < nodeCount; ++index)
            {
                group[index] = static_cast<Int32>(index);
            }
            const auto find = [&](Int32 at) {
                while (group[static_cast<std::size_t>(at.Get())] != at)
                {
                    at = group[static_cast<std::size_t>(at.Get())];
                }
                return at;
            };
            const auto topOf = [&](const ufbx_node* node) {
                const ufbx_node* top = node;
                for (const ufbx_node* above = FbxBoneParent(isBone, top); above != nullptr; above = FbxBoneParent(isBone, top))
                {
                    top = above;
                }
                return static_cast<Int32>(top->typed_id);
            };
            for (std::size_t skinIndex = 0; skinIndex < scene->skin_deformers.count; ++skinIndex)
            {
                const ufbx_skin_deformer* skin = scene->skin_deformers.data[skinIndex];
                Int32 first = -1;
                for (std::size_t at = 0; at < skin->clusters.count; ++at)
                {
                    const ufbx_node* node = skin->clusters.data[at]->bone_node;
                    if (false == IsFbxBone(isBone, node))
                    {
                        continue;
                    }
                    const Int32 root = find(topOf(node));
                    if (first < 0)
                    {
                        first = root;
                    }
                    else if (root != first)
                    {
                        group[static_cast<std::size_t>(root.Get())] = first;
                    }
                }
            }

            // 무리마다 뼈대 하나다. 뼈는 깊이 차례라 부모가 늘 앞에 온다(같은 깊이는 파일의 차례).
            Array<Int32> skeletonOfGroup;
            skeletonOfGroup.Resize(nodeCount);
            for (std::size_t index = 0; index < nodeCount; ++index)
            {
                skeletonOfGroup[index] = -1;
            }
            Array<UInt32> bones;
            for (std::size_t index = 0; index < nodeCount; ++index)
            {
                if (isBone[index])
                {
                    bones.Add(static_cast<UInt32>(index));
                }
            }
            Array<UInt32> depth;
            depth.Resize(nodeCount);
            for (const UInt32 index : bones)
            {
                depth[index.Get()] = FbxDepth(scene->nodes.data[index.Get()]);
            }
            std::stable_sort(bones.Data(), bones.Data() + bones.Size(), [&](UInt32 left, UInt32 right) { return depth[left.Get()] < depth[right.Get()]; });
            Array<ufbx_matrix> rootParentOf;
            Array<const ufbx_node*> rootParentNodeOf;
            Array<Array<String>> taken;
            for (const UInt32 index : bones)
            {
                const ufbx_node* node = scene->nodes.data[index.Get()];
                const Int32 owner = find(topOf(node));
                Int32& skeletonIndex = skeletonOfGroup[static_cast<std::size_t>(owner.Get())];
                if (skeletonIndex < 0)
                {
                    // 첫 뿌리의 바로 위 노드(뼈가 아닌 `Armature` 따위)가 뿌리 부모다. 파일의 뿌리 노드 밑이면 단위 행렬이다.
                    skeletonIndex = static_cast<Int32>(model.skeletons.Size());
                    ModelSkeleton skeleton;
                    skeleton.name = "Skeleton";
                    const ufbx_node* parent = node->parent != nullptr && false == node->parent->is_root ? node->parent : nullptr;
                    const ufbx_matrix rootParent = parent != nullptr ? parent->node_to_world : ufbx_identity_matrix;
                    skeleton.rootParent = FromUfbxMatrix(rootParent, scale);
                    model.skeletons.Add(std::move(skeleton));
                    rootParentOf.Add(rootParent);
                    rootParentNodeOf.Add(parent);
                    taken.Add(Array<String>{});
                }
                ModelSkeleton& skeleton = model.skeletons[static_cast<std::size_t>(skeletonIndex.Get())];
                ModelBone bone;
                const String wanted = node->name.length > 0 ? String(std::string_view(node->name.data, node->name.length)) : String("Bone");
                String name = wanted;
                Array<String>& names = taken[static_cast<std::size_t>(skeletonIndex.Get())];
                for (Int32 suffix = 2; names.Contains(name); ++suffix)
                {
                    name = wanted + " (" + String(std::to_string(suffix.Get()).c_str()) + ")";
                }
                names.Add(name);
                bone.name = name;
                const ufbx_node* parentBone = FbxBoneParent(isBone, node);
                const ufbx_node* direct = node->parent != nullptr && false == node->parent->is_root ? node->parent : nullptr;
                ufbx_transform local = node->local_transform;
                Bool animatable = true;
                if (parentBone != nullptr)
                {
                    bone.parent = out.boneOfNode[parentBone->typed_id];
                    if (parentBone != direct)
                    {
                        const ufbx_matrix inverse = ufbx_matrix_invert(&parentBone->node_to_world);
                        const ufbx_matrix relative = ufbx_matrix_mul(&inverse, &node->node_to_world);
                        local = ufbx_matrix_to_transform(&relative);
                        animatable = false;
                    }
                }
                else if (direct != rootParentNodeOf[static_cast<std::size_t>(skeletonIndex.Get())])
                {
                    // 합친 뼈대의 다른 뿌리다. 첫 뿌리의 부모를 기준으로 쉬는 자세를 둔다.
                    const ufbx_matrix inverse = ufbx_matrix_invert(&rootParentOf[static_cast<std::size_t>(skeletonIndex.Get())]);
                    const ufbx_matrix relative = ufbx_matrix_mul(&inverse, &node->node_to_world);
                    local = ufbx_matrix_to_transform(&relative);
                    animatable = false;
                }
                bone.position = Scale(FromUfbxVector(local.translation), scale);
                bone.rotation = Quaternion{static_cast<float>(local.rotation.x), static_cast<float>(local.rotation.y), static_cast<float>(local.rotation.z),
                    static_cast<float>(local.rotation.w)};
                bone.scale = FromUfbxVector(local.scale);
                // 역바인드는 클러스터의 `geometry_to_bone` 에 그 메시 자리의 역을 곱한 것이다 - 정점을 메시 자리로 옮겨 두므로(아래 메시 읽기) 스킨 행렬은
                // ufbx 의 `geometry_to_world` 와 같은 `뼈 자세 x geometry_to_bone` 이 되고, 같은 뼈대를 쓰는 메시들이 역바인드 하나를 함께 쓴다.
                // 클러스터의 `bind_to_world` 는 쓰지 않는다 - ufbx 문서가 스킨에는 `geometry_to_bone` 을 권하고, 그러면 ufbx 자신의 스킨 셈과 같은 값이 나온다.
                // 클러스터가 없는 중간 뼈는 쉬는 자세의 역이다.
                ufbx_matrix inverseBind = ufbx_matrix_invert(&node->node_to_world);
                Bool found = false;
                for (std::size_t meshIndex = 0; meshIndex < scene->meshes.count && false == found; ++meshIndex)
                {
                    const ufbx_mesh* mesh = scene->meshes.data[meshIndex];
                    const ufbx_skin_deformer* skin = mesh->skin_deformers.count > 0 ? mesh->skin_deformers.data[0] : nullptr;
                    for (std::size_t at = 0; skin != nullptr && at < skin->clusters.count && false == found; ++at)
                    {
                        if (skin->clusters.data[at]->bone_node == node)
                        {
                            const ufbx_matrix place = FbxMeshPlace(mesh);
                            const ufbx_matrix placeInverse = ufbx_matrix_invert(&place);
                            inverseBind = ufbx_matrix_mul(&skin->clusters.data[at]->geometry_to_bone, &placeInverse);
                            found = true;
                        }
                    }
                }
                bone.inverseBind = FromUfbxMatrix(inverseBind, scale);
                out.skeletonOfNode[index.Get()] = skeletonIndex;
                out.boneOfNode[index.Get()] = static_cast<Int32>(skeleton.bones.Size());
                out.animatable[index.Get()] = animatable;
                skeleton.bones.Add(std::move(bone));
            }
            for (std::size_t skinIndex = 0; skinIndex < scene->skin_deformers.count; ++skinIndex)
            {
                const ufbx_skin_deformer* skin = scene->skin_deformers.data[skinIndex];
                for (std::size_t at = 0; at < skin->clusters.count; ++at)
                {
                    const ufbx_node* node = skin->clusters.data[at]->bone_node;
                    if (IsFbxBone(isBone, node))
                    {
                        out.skeletonOfSkin[skinIndex] = out.skeletonOfNode[node->typed_id];
                        break;
                    }
                }
            }
        }

        // 정점 하나의 가중치다. ufbx 는 큰 것부터 늘어놓으므로 앞의 넷을 받고, glTF 와 같이 합이 1 이 되게 다시 나눈다(쓰지 않는 칸은 뼈 0).
        // 가중치가 없는 정점은 뼈 0 에 온전히 걸린다.
        ModelSkinWeights FbxSkinWeights(const ufbx_skin_deformer& skin, uint32_t vertex, const FbxSkeletons& skeletons)
        {
            ModelSkinWeights result;
            Float sum = 0.0f;
            if (vertex < skin.vertices.count)
            {
                const ufbx_skin_vertex& source = skin.vertices.data[vertex];
                UInt32 slot = 0;
                for (uint32_t at = 0; at < source.num_weights && slot < 4; ++at)
                {
                    const ufbx_skin_weight& weight = skin.weights.data[source.weight_begin + at];
                    const ufbx_node* node = weight.cluster_index < skin.clusters.count ? skin.clusters.data[weight.cluster_index]->bone_node : nullptr;
                    const Int32 bone = node != nullptr && node->typed_id < skeletons.boneOfNode.Size() ? skeletons.boneOfNode[node->typed_id] : Int32(-1);
                    if (bone < 0 || false == (weight.weight > 0.0))
                    {
                        continue;
                    }
                    result.bones[slot.Get()] = static_cast<UInt32>(bone.Get());
                    result.weights[slot.Get()] = static_cast<float>(weight.weight);
                    sum += result.weights[slot.Get()];
                    ++slot;
                }
            }
            for (UInt32 slot = 0; slot < 4; ++slot)
            {
                result.weights[slot.Get()] = sum > 0.0f ? Float(result.weights[slot.Get()] / sum) : Float(slot == 0 ? 1.0f : 0.0f);
                if (result.weights[slot.Get()] == 0.0f && slot > 0)
                {
                    result.bones[slot.Get()] = 0;
                }
            }
            return result;
        }

        // **FBX 의 애니메이션**(D-467)이다. 스택마다 `ufbx_bake_anim` 으로 굽어 선형 키로 받고, 뼈 노드의 위치·회전·크기를 트랙으로 둔다.
        // 시간은 스택의 시작이 0 이다. 바뀌지 않는 채널은 쉬는 자세와 다를 때만 둔다(내보내기가 모든 칸에 상수 키를 넣곤 한다).
        void ReadFbxAnimations(const ufbx_scene* scene, Float scale, const FbxSkeletons& skeletons, ModelData& model)
        {
            Array<String> taken;
            for (std::size_t stackIndex = 0; stackIndex < scene->anim_stacks.count; ++stackIndex)
            {
                const ufbx_anim_stack* stack = scene->anim_stacks.data[stackIndex];
                ufbx_bake_opts bakeOptions = {};
                bakeOptions.trim_start_time = true;
                ufbx_error bakeError = {};
                ufbx_baked_anim* baked = ufbx_bake_anim(scene, stack->anim, &bakeOptions, &bakeError);
                if (baked == nullptr)
                {
                    continue;
                }
                ModelAnimation animation;
                const String wanted = stack->name.length > 0 ? String(std::string_view(stack->name.data, stack->name.length)) : String("Animation");
                String name = wanted;
                for (Int32 suffix = 2; taken.Contains(name); ++suffix)
                {
                    name = wanted + " (" + String(std::to_string(suffix.Get()).c_str()) + ")";
                }
                animation.name = name;
                Float last = 0.0f;
                for (std::size_t at = 0; at < baked->nodes.count; ++at)
                {
                    const ufbx_baked_node& source = baked->nodes.data[at];
                    if (source.typed_id >= skeletons.boneOfNode.Size() || skeletons.boneOfNode[source.typed_id] < 0 || false == skeletons.animatable[source.typed_id])
                    {
                        continue;
                    }
                    const ModelBone& bone = model.skeletons[static_cast<std::size_t>(skeletons.skeletonOfNode[source.typed_id].Get())]
                                                .bones[static_cast<std::size_t>(skeletons.boneOfNode[source.typed_id].Get())];
                    const auto addVectors = [&](const ufbx_baked_vec3_list& keys, Bool constant, ModelTrackPath path, Vector3 rest, Float factor) {
                        if (keys.count == 0)
                        {
                            return;
                        }
                        const Vector3 first = Scale(FromUfbxVector(keys.data[0].value), factor);
                        const Bool same = std::fabs((first.x - rest.x).Get()) < 1.0e-5f && std::fabs((first.y - rest.y).Get()) < 1.0e-5f
                            && std::fabs((first.z - rest.z).Get()) < 1.0e-5f;
                        if (constant && same)
                        {
                            return;
                        }
                        ModelAnimationTrack track;
                        track.bone = bone.name;
                        track.path = path;
                        for (std::size_t key = 0; key < keys.count; ++key)
                        {
                            const Vector3 value = Scale(FromUfbxVector(keys.data[key].value), factor);
                            track.times.Add(static_cast<float>(keys.data[key].time));
                            track.values.Add(value.x);
                            track.values.Add(value.y);
                            track.values.Add(value.z);
                            last = std::max(last.Get(), static_cast<float>(keys.data[key].time));
                        }
                        animation.tracks.Add(std::move(track));
                    };
                    addVectors(source.translation_keys, source.constant_translation, ModelTrackPath::Position, bone.position, scale);
                    if (source.rotation_keys.count > 0)
                    {
                        const ufbx_quat first = source.rotation_keys.data[0].value;
                        const Float dot = static_cast<float>(first.x) * bone.rotation.x + static_cast<float>(first.y) * bone.rotation.y
                            + static_cast<float>(first.z) * bone.rotation.z + static_cast<float>(first.w) * bone.rotation.w;
                        if (false == source.constant_rotation || std::fabs(dot.Get()) < 1.0f - 1.0e-6f)
                        {
                            ModelAnimationTrack track;
                            track.bone = bone.name;
                            track.path = ModelTrackPath::Rotation;
                            for (std::size_t key = 0; key < source.rotation_keys.count; ++key)
                            {
                                const ufbx_quat value = source.rotation_keys.data[key].value;
                                track.times.Add(static_cast<float>(source.rotation_keys.data[key].time));
                                track.values.Add(static_cast<float>(value.x));
                                track.values.Add(static_cast<float>(value.y));
                                track.values.Add(static_cast<float>(value.z));
                                track.values.Add(static_cast<float>(value.w));
                                last = std::max(last.Get(), static_cast<float>(source.rotation_keys.data[key].time));
                            }
                            animation.tracks.Add(std::move(track));
                        }
                    }
                    addVectors(source.scale_keys, source.constant_scale, ModelTrackPath::Scale, bone.scale, 1.0f);
                }
                animation.duration = baked->playback_duration > 0.0 ? Float(static_cast<float>(baked->playback_duration)) : last;
                ufbx_free_baked_anim(baked);
                // 뼈를 움직이지 않는 스택(카메라·조명만)은 클립이 아니다.
                if (animation.tracks.IsEmpty())
                {
                    continue;
                }
                taken.Add(animation.name);
                model.animations.Add(std::move(animation));
            }
        }

        Int32 TextureImage(const ufbx_texture* texture, const Array<Int32>& imageOfTexture)
        {
            if (texture == nullptr || texture->typed_id >= imageOfTexture.Size())
            {
                return -1;
            }
            return imageOfTexture[texture->typed_id];
        }

        Bool DecodeUfbx(JArrayView<std::byte> bytes, ModelFormat format, const ModelImportOptions& options, const ModelFileReader& reader,
            ModelData& model, String& error)
        {
            ufbx_load_opts loadOptions = {};
            loadOptions.target_axes = ufbx_axes_right_handed_y_up;
            // 단위는 `convertUnits` 일 때만 미터로 바꾼다. 0 이면 바꾸지 않는다(ufbx 의 약속).
            loadOptions.target_unit_meters = options.convertUnits ? 1.0f : 0.0f;
            // 축·단위 변환과 FBX 의 기하 변환을 **정점에 굽는다** - 메시가 노드 없이 홀로 쓰이기 때문이다(메시 칸에 하나만 고른다).
            loadOptions.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
            loadOptions.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
            loadOptions.generate_missing_normals = true;
            loadOptions.open_file_cb.fn = &OpenSideFile;
            loadOptions.open_file_cb.user = const_cast<ModelFileReader*>(&reader);
            loadOptions.file_format = format == ModelFormat::Obj ? UFBX_FILE_FORMAT_OBJ : UFBX_FILE_FORMAT_FBX;
            // OBJ 의 `mtllib` 은 모델 폴더 기준이다. 파일 이름은 기준 폴더를 비우는 데만 쓴다.
            loadOptions.filename.data = format == ModelFormat::Obj ? "model.obj" : "model.fbx";
            loadOptions.filename.length = std::strlen(loadOptions.filename.data);
            loadOptions.obj_search_mtl_by_filename = true;
            // 곁 파일(OBJ 의 `.mtl`)은 켜야 읽는다. 없어도 모델은 연다 - 재질만 빠진다(ufbx 가 경고로 남긴다).
            loadOptions.load_external_files = true;
            loadOptions.ignore_missing_external_files = true;
            ufbx_error ufbxError = {};
            ufbx_scene* scene = ufbx_load_memory(bytes.data, bytes.size.Get(), &loadOptions, &ufbxError);
            if (scene == nullptr)
            {
                error = "the model could not be parsed: ";
                error += String(std::string_view(ufbxError.description.data, ufbxError.description.length));
                return false;
            }
            struct Guard
            {
                ufbx_scene* scene;
                ~Guard() { ufbx_free_scene(scene); }
            } guard{scene};

            Array<Int32> imageOfTexture;
            imageOfTexture.Resize(scene->textures.count);
            for (std::size_t index = 0; index < imageOfTexture.Size(); ++index)
            {
                imageOfTexture[index] = -1;
            }
            if (options.importMaterials)
            {
                // 같은 파일·같은 내장 바이트의 텍스처는 그림 하나다(ufbx 가 이미 텍스처 객체를 묶어 준다).
                for (std::size_t index = 0; index < scene->textures.count; ++index)
                {
                    const ufbx_texture* texture = scene->textures.data[index];
                    if (texture->type != UFBX_TEXTURE_FILE)
                    {
                        continue;
                    }
                    ModelImage image;
                    image.name = String(std::string_view(texture->name.data, texture->name.length));
                    if (texture->content.size > 0)
                    {
                        image.embedded.Resize(texture->content.size);
                        std::memcpy(image.embedded.Data(), texture->content.data, texture->content.size);
                        image.contentHash = HashModelBytes({image.embedded.Data(), static_cast<UInt32>(image.embedded.Size())});
                    }
                    else
                    {
                        image.path = SlashPath(texture->relative_filename.data, texture->relative_filename.length);
                    }
                    if (image.name.empty())
                    {
                        image.name = image.path.empty() ? String("Image") : image.path;
                    }
                    imageOfTexture[index] = static_cast<Int32>(model.images.Size());
                    model.images.Add(std::move(image));
                }
                Array<Bool> used;
                used.Resize(model.images.Size());
                for (std::size_t index = 0; index < used.Size(); ++index)
                {
                    used[index] = false;
                }
                for (std::size_t index = 0; index < scene->materials.count; ++index)
                {
                    const ufbx_material* source = scene->materials.data[index];
                    ModelMaterial material;
                    material.name = String(std::string_view(source->name.data, source->name.length));
                    if (material.name.empty())
                    {
                        material.name = "Material";
                    }
                    // ufbx 는 FBX 의 고전 재질(phong 따위)도 PBR 칸으로 옮겨 준다.
                    const ufbx_vec4 color = source->pbr.base_color.value_vec4;
                    const ufbx_real factor = source->pbr.base_factor.has_value ? source->pbr.base_factor.value_real : 1.0;
                    material.baseColor[0] = static_cast<float>(color.x * factor);
                    material.baseColor[1] = static_cast<float>(color.y * factor);
                    material.baseColor[2] = static_cast<float>(color.z * factor);
                    material.baseColor[3] = source->pbr.base_color.value_components >= 4 ? static_cast<float>(color.w) : 1.0f;
                    material.metallic = static_cast<float>(source->pbr.metalness.value_real);
                    material.roughness = static_cast<float>(source->pbr.roughness.value_real);
                    material.images[static_cast<UInt32>(ModelTextureSlot::BaseColor).Get()] = TextureImage(source->pbr.base_color.texture, imageOfTexture);
                    material.images[static_cast<UInt32>(ModelTextureSlot::Normal).Get()] = TextureImage(source->pbr.normal_map.texture, imageOfTexture);
                    material.images[static_cast<UInt32>(ModelTextureSlot::MetallicRoughness).Get()] = TextureImage(source->pbr.roughness.texture, imageOfTexture);
                    material.images[static_cast<UInt32>(ModelTextureSlot::Occlusion).Get()] = TextureImage(source->pbr.ambient_occlusion.texture, imageOfTexture);
                    material.images[static_cast<UInt32>(ModelTextureSlot::Emissive).Get()] = TextureImage(source->pbr.emission_color.texture, imageOfTexture);
                    const ufbx_vec4 emission = source->pbr.emission_color.value_vec4;
                    const ufbx_real emissionFactor = source->pbr.emission_factor.has_value ? source->pbr.emission_factor.value_real : 1.0;
                    material.emissive[0] = static_cast<float>(emission.x * emissionFactor);
                    material.emissive[1] = static_cast<float>(emission.y * emissionFactor);
                    material.emissive[2] = static_cast<float>(emission.z * emissionFactor);
                    for (UInt32 slot = 0; slot < ModelTextureSlotCount; ++slot)
                    {
                        NoteImageUse(model, material.images[slot.Get()], static_cast<ModelTextureSlot>(slot.Get()), used);
                    }
                    model.materials.Add(std::move(material));
                }
            }

            // 뼈대가 먼저다 - 스킨 메시가 뼈 번호를 본다(D-467).
            const Float importScale = std::isfinite(options.scale.Get()) && options.scale > 0.0f ? options.scale : Float(1.0f);
            FbxSkeletons skeletons;
            ReadFbxSkeletons(scene, importScale, model, skeletons);

            // 메시마다 재질 부분 하나가 `ModelMesh` 하나다. 노드가 가리킬 수 있게 메시 → 결과 번호들을 남긴다.
            Array<RawMesh> raws;
            Array<Array<UInt32>> meshOutputs;
            meshOutputs.Resize(scene->meshes.count);
            Array<uint32_t> triangle;
            for (std::size_t meshIndex = 0; meshIndex < scene->meshes.count; ++meshIndex)
            {
                const ufbx_mesh* mesh = scene->meshes.data[meshIndex];
                triangle.Resize(mesh->max_face_triangles * 3);
                // 스킨 메시(D-467)는 첫 스킨만 본다. 정점을 메시가 놓인 자리(`FbxMeshPlace`)로 옮긴다 - 역바인드가 뼈마다 하나라 같은 뼈대를 쓰는 메시들이 함께 쓴다.
                // glTF 의 스킨 메시가 노드 자리를 보지 않는 것과 같은 뜻이다.
                const ufbx_skin_deformer* skin = mesh->skin_deformers.count > 0 ? mesh->skin_deformers.data[0] : nullptr;
                const Int32 skinSkeleton = skin != nullptr && skin->typed_id < skeletons.skeletonOfSkin.Size() ? skeletons.skeletonOfSkin[skin->typed_id] : Int32(-1);
                const ufbx_matrix bindPlace = skinSkeleton >= 0 ? FbxMeshPlace(mesh) : ufbx_identity_matrix;
                const ufbx_matrix bindNormals = ufbx_matrix_for_normals(&bindPlace);
                String baseName(std::string_view(mesh->name.data, mesh->name.length));
                if (baseName.empty() && mesh->instances.count > 0)
                {
                    baseName = String(std::string_view(mesh->instances.data[0]->name.data, mesh->instances.data[0]->name.length));
                }
                if (baseName.empty())
                {
                    baseName = "Mesh";
                }
                const std::size_t partCount = mesh->material_parts.count;
                for (std::size_t part = 0; part < (partCount > 0 ? partCount : 1); ++part)
                {
                    const ufbx_mesh_part* source = partCount > 0 ? &mesh->material_parts.data[part] : nullptr;
                    const std::size_t faceCount = source != nullptr ? source->face_indices.count : mesh->faces.count;
                    if (faceCount == 0)
                    {
                        continue;
                    }
                    RawMesh raw;
                    raw.hasNormals = mesh->vertex_normal.exists;
                    raw.hasUvs = mesh->vertex_uv.exists;
                    raw.hasTangents = mesh->vertex_tangent.exists && mesh->vertex_bitangent.exists;
                    const ufbx_material* material = (source != nullptr && source->index < mesh->materials.count)
                        ? mesh->materials.data[source->index] : nullptr;
                    raw.material = (options.importMaterials && material != nullptr) ? static_cast<Int32>(material->typed_id) : Int32(-1);
                    String name = baseName;
                    if (partCount > 1)
                    {
                        name += "/";
                        name += material != nullptr && material->name.length > 0
                            ? String(std::string_view(material->name.data, material->name.length)) : String(std::to_string(part).c_str());
                    }
                    raw.name = UniqueName(raws, name);
                    if (skinSkeleton >= 0)
                    {
                        raw.skeleton = skinSkeleton;
                    }
                    for (std::size_t faceAt = 0; faceAt < faceCount; ++faceAt)
                    {
                        const uint32_t faceIndex = source != nullptr ? source->face_indices.data[faceAt] : static_cast<uint32_t>(faceAt);
                        const uint32_t triangles = ufbx_triangulate_face(triangle.Data(), triangle.Size(), mesh, mesh->faces.data[faceIndex]);
                        for (uint32_t corner = 0; corner < triangles * 3; ++corner)
                        {
                            const uint32_t index = triangle[corner];
                            ModelVertex out;
                            ufbx_vec3 position = ufbx_get_vertex_vec3(&mesh->vertex_position, index);
                            if (skinSkeleton >= 0)
                            {
                                position = ufbx_transform_position(&bindPlace, position);
                            }
                            out.position[0] = static_cast<float>(position.x);
                            out.position[1] = static_cast<float>(position.y);
                            out.position[2] = static_cast<float>(position.z);
                            if (mesh->vertex_normal.exists)
                            {
                                ufbx_vec3 normal = ufbx_get_vertex_vec3(&mesh->vertex_normal, index);
                                if (skinSkeleton >= 0)
                                {
                                    normal = ufbx_vec3_normalize(ufbx_transform_direction(&bindNormals, normal));
                                }
                                out.normal[0] = static_cast<float>(normal.x);
                                out.normal[1] = static_cast<float>(normal.y);
                                out.normal[2] = static_cast<float>(normal.z);
                            }
                            if (mesh->vertex_uv.exists)
                            {
                                // FBX·OBJ 의 UV 는 왼쪽 **아래**가 0 이다. 엔진(D3D)에 맞춰 세로를 뒤집는다.
                                const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, index);
                                out.uv[0] = static_cast<float>(uv.x);
                                out.uv[1] = static_cast<float>(1.0 - uv.y);
                            }
                            if (raw.hasTangents)
                            {
                                ufbx_vec3 t = ufbx_get_vertex_vec3(&mesh->vertex_tangent, index);
                                ufbx_vec3 b = ufbx_get_vertex_vec3(&mesh->vertex_bitangent, index);
                                if (skinSkeleton >= 0)
                                {
                                    t = ufbx_vec3_normalize(ufbx_transform_direction(&bindPlace, t));
                                    b = ufbx_vec3_normalize(ufbx_transform_direction(&bindPlace, b));
                                }
                                const Vector3 tangent{static_cast<float>(t.x), static_cast<float>(t.y), static_cast<float>(t.z)};
                                const Vector3 bitangent{static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.z)};
                                const Vector3 normal{out.normal[0], out.normal[1], out.normal[2]};
                                out.tangent[0] = tangent.x;
                                out.tangent[1] = tangent.y;
                                out.tangent[2] = tangent.z;
                                out.tangent[3] = Dot(Cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
                            }
                            raw.corners.Add(out);
                            if (skinSkeleton >= 0)
                            {
                                raw.skinCorners.Add(FbxSkinWeights(*skin, mesh->vertex_indices.data[index], skeletons));
                            }
                        }
                    }
                    meshOutputs[meshIndex].Add(static_cast<UInt32>(raws.Size()));
                    raws.Add(std::move(raw));
                }
            }
            for (std::size_t index = 0; index < raws.Size(); ++index)
            {
                model.meshes.Add(Finish(raws[index], options));
            }

            // 노드다. ufbx 의 뿌리 노드는 파일에 없는 것이라 빼고, 그 자식을 뿌리로 둔다.
            Array<Int32> nodeOf;
            nodeOf.Resize(scene->nodes.count);
            for (std::size_t index = 0; index < scene->nodes.count; ++index)
            {
                nodeOf[index] = -1;
            }
            const Float scale = importScale;
            for (std::size_t index = 0; index < scene->nodes.count; ++index)
            {
                const ufbx_node* source = scene->nodes.data[index];
                if (source->is_root)
                {
                    continue;
                }
                ModelNode node;
                node.name = String(std::string_view(source->name.data, source->name.length));
                const ufbx_transform& local = source->local_transform;
                node.position = Scale(Vector3{static_cast<float>(local.translation.x), static_cast<float>(local.translation.y),
                    static_cast<float>(local.translation.z)}, scale);
                node.rotation = Quaternion{static_cast<float>(local.rotation.x), static_cast<float>(local.rotation.y),
                    static_cast<float>(local.rotation.z), static_cast<float>(local.rotation.w)};
                node.scale = Vector3{static_cast<float>(local.scale.x), static_cast<float>(local.scale.y), static_cast<float>(local.scale.z)};
                if (source->parent != nullptr && false == source->parent->is_root && source->parent->typed_id < nodeOf.Size())
                {
                    node.parent = nodeOf[source->parent->typed_id];
                }
                if (source->mesh != nullptr && source->mesh->typed_id < meshOutputs.Size())
                {
                    node.meshes = meshOutputs[source->mesh->typed_id];
                }
                nodeOf[index] = static_cast<Int32>(model.nodes.Size());
                model.nodes.Add(std::move(node));
            }
            ReadFbxAnimations(scene, importScale, skeletons, model);
            return true;
        }

        Bool EndsWith(std::string_view text, std::string_view suffix)
        {
            if (text.size() < suffix.size())
            {
                return false;
            }
            for (std::size_t index = 0; index < suffix.size(); ++index)
            {
                char letter = text[text.size() - suffix.size() + index];
                if (letter >= 'A' && letter <= 'Z')
                {
                    letter = static_cast<char>(letter - 'A' + 'a');
                }
                if (letter != suffix[index])
                {
                    return false;
                }
            }
            return true;
        }
    }

    ModelFormat ModelFormatFromPath(std::string_view path)
    {
        if (EndsWith(path, ".gltf"))
        {
            return ModelFormat::Gltf;
        }
        if (EndsWith(path, ".glb"))
        {
            return ModelFormat::Glb;
        }
        if (EndsWith(path, ".fbx"))
        {
            return ModelFormat::Fbx;
        }
        if (EndsWith(path, ".obj"))
        {
            return ModelFormat::Obj;
        }
        return ModelFormat::Unknown;
    }

    Bool DecodeModel(JArrayView<std::byte> bytes, ModelFormat format, const ModelImportOptions& options,
        const ModelFileReader& reader, ModelData& result, String& error)
    {
        if (bytes.data == nullptr || bytes.size == 0)
        {
            error = "the model file is empty";
            return false;
        }
        ModelData model;
        Bool decoded = false;
        switch (format)
        {
        case ModelFormat::Gltf:
        case ModelFormat::Glb:
            decoded = DecodeGltf(bytes, options, reader, model, error);
            break;
        case ModelFormat::Fbx:
        case ModelFormat::Obj:
            decoded = DecodeUfbx(bytes, format, options, reader, model, error);
            break;
        default:
            error = "the model format is not known";
            return false;
        }
        if (false == decoded)
        {
            return false;
        }
        result = std::move(model);
        return true;
    }

    UInt64 HashModelBytes(JArrayView<std::byte> bytes)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (std::uint32_t index = 0; index < bytes.size.Get(); ++index)
        {
            hash ^= static_cast<std::uint64_t>(bytes.data[index]);
            hash *= 1099511628211ull;
        }
        return hash == 0 ? UInt64(1) : UInt64(hash);
    }
}
