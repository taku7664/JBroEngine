#include <JBro/Asset/MeshFile.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace JBro
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::uint32_t CookedMagic = 0x53454D4Au; // "JMES"
        constexpr std::uint32_t CookedVersion = 1;
        constexpr std::uint32_t GlbMagic = 0x46546C67u;    // "glTF"
        constexpr std::uint32_t GlbJson = 0x4E4F534Au;     // "JSON"
        constexpr std::uint32_t GlbBinary = 0x004E4942u;   // "BIN"

        struct Matrix
        {
            // 열 우선(glTF 그대로)이다.
            double m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        };

        Matrix Multiply(const Matrix& a, const Matrix& b)
        {
            Matrix out;
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    double sum = 0.0;
                    for (int k = 0; k < 4; ++k)
                    {
                        sum += a.m[k * 4 + row] * b.m[column * 4 + k];
                    }
                    out.m[column * 4 + row] = sum;
                }
            }
            return out;
        }

        Matrix NodeMatrix(const Json& node)
        {
            Matrix out;
            const auto matrix = node.find("matrix");
            if (matrix != node.end() && matrix->is_array() && matrix->size() == 16)
            {
                for (int at = 0; at < 16; ++at)
                {
                    out.m[at] = (*matrix)[static_cast<std::size_t>(at)].is_number() ? (*matrix)[static_cast<std::size_t>(at)].get<double>() : out.m[at];
                }
                return out;
            }
            const auto vector = [&](const char* name, double* values, int count) {
                const auto found = node.find(name);
                if (found != node.end() && found->is_array() && found->size() == static_cast<std::size_t>(count))
                {
                    for (int at = 0; at < count; ++at)
                    {
                        values[at] = (*found)[static_cast<std::size_t>(at)].is_number() ? (*found)[static_cast<std::size_t>(at)].get<double>() : values[at];
                    }
                }
            };
            double t[3] = {0, 0, 0};
            double r[4] = {0, 0, 0, 1};
            double s[3] = {1, 1, 1};
            vector("translation", t, 3);
            vector("rotation", r, 4);
            vector("scale", s, 3);
            const double x = r[0];
            const double y = r[1];
            const double z = r[2];
            const double w = r[3];
            out.m[0] = (1 - 2 * (y * y + z * z)) * s[0];
            out.m[1] = (2 * (x * y + z * w)) * s[0];
            out.m[2] = (2 * (x * z - y * w)) * s[0];
            out.m[4] = (2 * (x * y - z * w)) * s[1];
            out.m[5] = (1 - 2 * (x * x + z * z)) * s[1];
            out.m[6] = (2 * (y * z + x * w)) * s[1];
            out.m[8] = (2 * (x * z + y * w)) * s[2];
            out.m[9] = (2 * (y * z - x * w)) * s[2];
            out.m[10] = (1 - 2 * (x * x + y * y)) * s[2];
            out.m[12] = t[0];
            out.m[13] = t[1];
            out.m[14] = t[2];
            return out;
        }

        double Determinant3(const Matrix& a)
        {
            return a.m[0] * (a.m[5] * a.m[10] - a.m[9] * a.m[6]) - a.m[4] * (a.m[1] * a.m[10] - a.m[9] * a.m[2])
                + a.m[8] * (a.m[1] * a.m[6] - a.m[5] * a.m[2]);
        }

        Bool DecodeBase64(std::string_view text, Array<std::byte>& out)
        {
            const auto value = [](char c) -> int {
                if (c >= 'A' && c <= 'Z')
                {
                    return c - 'A';
                }
                if (c >= 'a' && c <= 'z')
                {
                    return c - 'a' + 26;
                }
                if (c >= '0' && c <= '9')
                {
                    return c - '0' + 52;
                }
                return c == '+' ? 62 : c == '/' ? 63 : -1;
            };
            out.Clear();
            out.Reserve(text.size() / 4 * 3);
            std::uint32_t bits = 0;
            int count = 0;
            for (const char c : text)
            {
                if (c == '=')
                {
                    break;
                }
                const int digit = value(c);
                if (digit < 0)
                {
                    return false;
                }
                bits = (bits << 6) | static_cast<std::uint32_t>(digit);
                count += 6;
                if (count >= 8)
                {
                    count -= 8;
                    out.Add(static_cast<std::byte>((bits >> count) & 0xFFu));
                }
            }
            return true;
        }

        struct Reader
        {
            const Json& root;
            Array<Array<std::byte>> buffers;
            String* error = nullptr;

            Bool Fail(const char* why)
            {
                if (error != nullptr)
                {
                    *error = why;
                }
                return false;
            }

            // 접근자 하나의 원소를 실수로 읽는다(`normalized` 정수 포함). 인덱스는 `ReadIndices` 다.
            Bool ReadFloats(std::size_t accessorIndex, int wanted, Array<float>& out)
            {
                const Json& accessors = root["accessors"];
                if (false == accessors.is_array() || accessorIndex >= accessors.size())
                {
                    return Fail("a primitive names an accessor that does not exist");
                }
                const Json& accessor = accessors[accessorIndex];
                const std::string type = accessor.value("type", std::string());
                const int components = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 0;
                if (components < wanted || accessor.contains("sparse"))
                {
                    return Fail("an accessor has a shape this reader does not take");
                }
                const std::size_t count = accessor.value("count", std::size_t{0});
                const int componentType = accessor.value("componentType", 0);
                const int size = componentType == 5126 ? 4 : componentType == 5123 || componentType == 5122 ? 2 : componentType == 5121 || componentType == 5120 ? 1 : 0;
                if (size == 0)
                {
                    return Fail("an accessor has a component type this reader does not take");
                }
                const std::byte* data = nullptr;
                std::size_t stride = 0;
                if (false == Locate(accessor, static_cast<std::size_t>(components) * size, count, data, stride))
                {
                    return false;
                }
                out.Resize(count * static_cast<std::size_t>(wanted));
                for (std::size_t at = 0; at < count; ++at)
                {
                    const std::byte* element = data + at * stride;
                    for (int component = 0; component < wanted; ++component)
                    {
                        const std::byte* cell = element + static_cast<std::size_t>(component) * size;
                        float value = 0.0f;
                        if (componentType == 5126)
                        {
                            std::memcpy(&value, cell, 4);
                        }
                        else if (componentType == 5123)
                        {
                            std::uint16_t raw = 0;
                            std::memcpy(&raw, cell, 2);
                            value = static_cast<float>(raw) / 65535.0f;
                        }
                        else if (componentType == 5121)
                        {
                            value = static_cast<float>(static_cast<std::uint8_t>(*cell)) / 255.0f;
                        }
                        else if (componentType == 5122)
                        {
                            std::int16_t raw = 0;
                            std::memcpy(&raw, cell, 2);
                            value = static_cast<float>(raw) / 32767.0f;
                        }
                        else
                        {
                            value = static_cast<float>(static_cast<std::int8_t>(*cell)) / 127.0f;
                        }
                        out[at * static_cast<std::size_t>(wanted) + static_cast<std::size_t>(component)] = value;
                    }
                }
                return true;
            }

            Bool ReadIndices(std::size_t accessorIndex, Array<std::uint32_t>& out)
            {
                const Json& accessors = root["accessors"];
                if (false == accessors.is_array() || accessorIndex >= accessors.size())
                {
                    return Fail("a primitive names an index accessor that does not exist");
                }
                const Json& accessor = accessors[accessorIndex];
                const int componentType = accessor.value("componentType", 0);
                const int size = componentType == 5125 ? 4 : componentType == 5123 ? 2 : componentType == 5121 ? 1 : 0;
                const std::size_t count = accessor.value("count", std::size_t{0});
                if (size == 0)
                {
                    return Fail("an index accessor has a component type this reader does not take");
                }
                const std::byte* data = nullptr;
                std::size_t stride = 0;
                if (false == Locate(accessor, static_cast<std::size_t>(size), count, data, stride))
                {
                    return false;
                }
                out.Resize(count);
                for (std::size_t at = 0; at < count; ++at)
                {
                    std::uint32_t value = 0;
                    std::memcpy(&value, data + at * stride, static_cast<std::size_t>(size));
                    out[at] = value;
                }
                return true;
            }

            // 접근자의 첫 원소 자리와 보폭을 찾고 범위를 잰다.
            Bool Locate(const Json& accessor, std::size_t elementBytes, std::size_t count, const std::byte*& data, std::size_t& stride)
            {
                const Json& views = root["bufferViews"];
                const std::size_t viewIndex = accessor.value("bufferView", static_cast<std::size_t>(-1));
                if (false == views.is_array() || viewIndex >= views.size())
                {
                    return Fail("an accessor without a buffer view is not taken");
                }
                const Json& view = views[viewIndex];
                const std::size_t bufferIndex = view.value("buffer", static_cast<std::size_t>(-1));
                if (bufferIndex >= buffers.Size())
                {
                    return Fail("a buffer view names a buffer that does not exist");
                }
                const std::size_t viewOffset = view.value("byteOffset", std::size_t{0});
                const std::size_t viewLength = view.value("byteLength", std::size_t{0});
                const std::size_t offset = accessor.value("byteOffset", std::size_t{0});
                stride = view.value("byteStride", std::size_t{0});
                if (stride == 0)
                {
                    stride = elementBytes;
                }
                const Array<std::byte>& buffer = buffers[bufferIndex];
                if (stride < elementBytes || viewOffset > buffer.Size() || viewLength > buffer.Size() - viewOffset)
                {
                    return Fail("a buffer view runs past its buffer");
                }
                if (count != 0 && (offset > viewLength || elementBytes > viewLength - offset || (count - 1) > (viewLength - offset - elementBytes) / stride))
                {
                    return Fail("an accessor runs past its buffer view");
                }
                data = buffer.Data() + viewOffset + offset;
                return true;
            }
        };

        Bool AppendPrimitive(Reader& reader, const Json& primitive, const Matrix& transform, MeshAssetData& out)
        {
            if (primitive.value("mode", 4) != 4)
            {
                return true; // 선·점은 메시가 아니다. 삼각형만 든다.
            }
            const auto attributes = primitive.find("attributes");
            if (attributes == primitive.end() || false == attributes->is_object() || false == attributes->contains("POSITION"))
            {
                return reader.Fail("a primitive has no POSITION");
            }
            Array<float> positions;
            if (false == reader.ReadFloats((*attributes)["POSITION"].get<std::size_t>(), 3, positions))
            {
                return false;
            }
            const std::size_t vertexCount = positions.Size() / 3;
            Array<float> normals;
            if (attributes->contains("NORMAL") && false == reader.ReadFloats((*attributes)["NORMAL"].get<std::size_t>(), 3, normals))
            {
                return false;
            }
            Array<float> uvs;
            if (attributes->contains("TEXCOORD_0") && false == reader.ReadFloats((*attributes)["TEXCOORD_0"].get<std::size_t>(), 2, uvs))
            {
                return false;
            }
            if ((false == normals.IsEmpty() && normals.Size() != vertexCount * 3) || (false == uvs.IsEmpty() && uvs.Size() != vertexCount * 2))
            {
                return reader.Fail("a primitive's attributes differ in length");
            }
            Array<std::uint32_t> indices;
            if (primitive.contains("indices"))
            {
                if (false == reader.ReadIndices(primitive["indices"].get<std::size_t>(), indices))
                {
                    return false;
                }
            }
            else
            {
                indices.Resize(vertexCount);
                for (std::size_t at = 0; at < vertexCount; ++at)
                {
                    indices[at] = static_cast<std::uint32_t>(at);
                }
            }
            if (indices.Size() % 3 != 0)
            {
                return reader.Fail("a triangle primitive's index count is not a multiple of three");
            }
            for (const std::uint32_t index : indices)
            {
                if (index >= vertexCount)
                {
                    return reader.Fail("an index points past the vertices");
                }
            }
            const std::size_t firstVertex = out.vertices.Size() / MeshVertexFloats.Get();
            out.vertices.Resize(out.vertices.Size() + vertexCount * MeshVertexFloats.Get());
            Float* written = out.vertices.Data() + firstVertex * MeshVertexFloats.Get();
            // 법선은 위 3x3 의 여인수 행렬로 옮긴다(크기가 고르지 않아도 면에 수직이다).
            const double* m = transform.m;
            const double normalMatrix[9] = {
                m[5] * m[10] - m[9] * m[6], m[8] * m[6] - m[4] * m[10], m[4] * m[9] - m[8] * m[5],
                m[9] * m[2] - m[1] * m[10], m[0] * m[10] - m[8] * m[2], m[8] * m[1] - m[0] * m[9],
                m[1] * m[6] - m[5] * m[2], m[4] * m[2] - m[0] * m[6], m[0] * m[5] - m[4] * m[1]};
            for (std::size_t at = 0; at < vertexCount; ++at)
            {
                const double x = positions[at * 3];
                const double y = positions[at * 3 + 1];
                const double z = positions[at * 3 + 2];
                Float* vertex = written + at * MeshVertexFloats.Get();
                vertex[0] = static_cast<float>(m[0] * x + m[4] * y + m[8] * z + m[12]);
                vertex[1] = static_cast<float>(m[1] * x + m[5] * y + m[9] * z + m[13]);
                vertex[2] = static_cast<float>(m[2] * x + m[6] * y + m[10] * z + m[14]);
                if (false == normals.IsEmpty())
                {
                    const double nx = normals[at * 3];
                    const double ny = normals[at * 3 + 1];
                    const double nz = normals[at * 3 + 2];
                    double ox = normalMatrix[0] * nx + normalMatrix[1] * ny + normalMatrix[2] * nz;
                    double oy = normalMatrix[3] * nx + normalMatrix[4] * ny + normalMatrix[5] * nz;
                    double oz = normalMatrix[6] * nx + normalMatrix[7] * ny + normalMatrix[8] * nz;
                    const double length = std::sqrt(ox * ox + oy * oy + oz * oz);
                    const double sign = Determinant3(transform) < 0.0 ? -1.0 : 1.0;
                    if (length > 0.0)
                    {
                        ox = ox / length * sign;
                        oy = oy / length * sign;
                        oz = oz / length * sign;
                    }
                    vertex[3] = static_cast<float>(ox);
                    vertex[4] = static_cast<float>(oy);
                    vertex[5] = static_cast<float>(oz);
                }
                if (false == uvs.IsEmpty())
                {
                    vertex[6] = uvs[at * 2];
                    vertex[7] = uvs[at * 2 + 1];
                }
            }
            // glTF 의 앞면은 반시계이고 엔진의 앞면은 인덱스 시계 방향이다. 음의 크기는 감김을 한 번 더 뒤집는다.
            const Bool mirrored = Determinant3(transform) < 0.0;
            const std::size_t firstIndex = out.indices.Size();
            out.indices.Resize(firstIndex + indices.Size());
            for (std::size_t at = 0; at + 2 < indices.Size(); at += 3)
            {
                const std::uint32_t a = static_cast<std::uint32_t>(firstVertex) + indices[at];
                const std::uint32_t b = static_cast<std::uint32_t>(firstVertex) + indices[at + 1];
                const std::uint32_t c = static_cast<std::uint32_t>(firstVertex) + indices[at + 2];
                out.indices[firstIndex + at] = a;
                out.indices[firstIndex + at + 1] = mirrored ? b : c;
                out.indices[firstIndex + at + 2] = mirrored ? c : b;
            }
            // 법선이 없으면 면에서 만든다(면적 가중 평균). 이 프리미티브의 정점만 고친다.
            if (normals.IsEmpty())
            {
                for (std::size_t at = 0; at + 2 < indices.Size(); at += 3)
                {
                    const std::size_t ia = indices[at];
                    const std::size_t ib = indices[at + 1];
                    const std::size_t ic = indices[at + 2];
                    const Float* a = written + ia * MeshVertexFloats.Get();
                    const Float* b = written + ib * MeshVertexFloats.Get();
                    const Float* c = written + ic * MeshVertexFloats.Get();
                    const float ux = b[0] - a[0];
                    const float uy = b[1] - a[1];
                    const float uz = b[2] - a[2];
                    const float vx = c[0] - a[0];
                    const float vy = c[1] - a[1];
                    const float vz = c[2] - a[2];
                    const float sign = mirrored ? -1.0f : 1.0f;
                    const float nx = (uy * vz - uz * vy) * sign;
                    const float ny = (uz * vx - ux * vz) * sign;
                    const float nz = (ux * vy - uy * vx) * sign;
                    for (const std::size_t corner : {ia, ib, ic})
                    {
                        Float* vertex = written + corner * MeshVertexFloats.Get();
                        vertex[3] = vertex[3] + nx;
                        vertex[4] = vertex[4] + ny;
                        vertex[5] = vertex[5] + nz;
                    }
                }
                for (std::size_t at = 0; at < vertexCount; ++at)
                {
                    Float* vertex = written + at * MeshVertexFloats.Get();
                    const float x = vertex[3];
                    const float y = vertex[4];
                    const float z = vertex[5];
                    const float length = std::sqrt(x * x + y * y + z * z);
                    if (length > 0.0f)
                    {
                        vertex[3] = x / length;
                        vertex[4] = y / length;
                        vertex[5] = z / length;
                    }
                }
            }
            return true;
        }

        Bool AppendMesh(Reader& reader, std::size_t meshIndex, const Matrix& transform, MeshAssetData& out)
        {
            const Json& meshes = reader.root["meshes"];
            if (false == meshes.is_array() || meshIndex >= meshes.size())
            {
                return reader.Fail("a node names a mesh that does not exist");
            }
            const auto primitives = meshes[meshIndex].find("primitives");
            if (primitives == meshes[meshIndex].end() || false == primitives->is_array())
            {
                return true;
            }
            for (const Json& primitive : *primitives)
            {
                if (false == AppendPrimitive(reader, primitive, transform, out))
                {
                    return false;
                }
            }
            return true;
        }

        // 노드 나무를 내려간다. `depth` 는 고리 달린 깨진 파일을 멈춘다.
        Bool VisitNode(Reader& reader, std::size_t nodeIndex, const Matrix& parent, std::uint32_t depth, MeshAssetData& out)
        {
            const Json& nodes = reader.root["nodes"];
            if (false == nodes.is_array() || nodeIndex >= nodes.size() || depth > 256u)
            {
                return reader.Fail("a node reference is broken");
            }
            const Json& node = nodes[nodeIndex];
            const Matrix world = Multiply(parent, NodeMatrix(node));
            if (node.contains("mesh") && false == AppendMesh(reader, node["mesh"].get<std::size_t>(), world, out))
            {
                return false;
            }
            const auto children = node.find("children");
            if (children != node.end() && children->is_array())
            {
                for (const Json& child : *children)
                {
                    if (false == VisitNode(reader, child.get<std::size_t>(), world, depth + 1u, out))
                    {
                        return false;
                    }
                }
            }
            return true;
        }
    }

    Bool ParseGltfMesh(const std::byte* bytes, std::size_t size, MeshBufferReader bufferReader, void* user, MeshAssetData& out, String* error)
    {
        out.vertices.Clear();
        out.indices.Clear();
        const auto fail = [&](const char* why) {
            if (error != nullptr)
            {
                *error = why;
            }
            return false;
        };
        if (bytes == nullptr || size == 0)
        {
            return fail("the file is empty");
        }
        // GLB 면 JSON 조각과 BIN 조각으로 나눈다.
        const char* jsonBegin = reinterpret_cast<const char*>(bytes);
        std::size_t jsonSize = size;
        Array<std::byte> binary;
        Bool hasBinary = false;
        std::uint32_t magic = 0;
        if (size >= 12)
        {
            std::memcpy(&magic, bytes, 4);
        }
        if (magic == GlbMagic)
        {
            std::size_t cursor = 12;
            jsonBegin = nullptr;
            while (cursor + 8 <= size)
            {
                std::uint32_t length = 0;
                std::uint32_t kind = 0;
                std::memcpy(&length, bytes + cursor, 4);
                std::memcpy(&kind, bytes + cursor + 4, 4);
                cursor += 8;
                if (length > size - cursor)
                {
                    return fail("a GLB chunk runs past the file");
                }
                if (kind == GlbJson && jsonBegin == nullptr)
                {
                    jsonBegin = reinterpret_cast<const char*>(bytes + cursor);
                    jsonSize = length;
                }
                else if (kind == GlbBinary && false == hasBinary)
                {
                    binary.Resize(length);
                    if (length != 0)
                    {
                        std::memcpy(binary.Data(), bytes + cursor, length);
                    }
                    hasBinary = true;
                }
                cursor += length;
            }
            if (jsonBegin == nullptr)
            {
                return fail("the GLB has no JSON chunk");
            }
        }
        const Json root = Json::parse(jsonBegin, jsonBegin + jsonSize, nullptr, false);
        if (root.is_discarded() || false == root.is_object())
        {
            return fail("the glTF JSON is broken");
        }
        Reader reader{root, {}, error};
        // 값의 형이 틀리면 json 이 예외를 던진다(엔진은 json 을 예외 켠 채 쓴다) - 깨진 파일이다.
        try
        {
            const auto buffers = root.find("buffers");
            if (buffers != root.end() && buffers->is_array())
            {
                for (std::size_t at = 0; at < buffers->size(); ++at)
                {
                    const Json& buffer = (*buffers)[at];
                    Array<std::byte> data;
                    if (buffer.contains("uri"))
                    {
                        const std::string uri = buffer["uri"].get<std::string>();
                        if (uri.rfind("data:", 0) == 0)
                        {
                            const std::size_t comma = uri.find(";base64,");
                            if (comma == std::string::npos || false == DecodeBase64(std::string_view(uri).substr(comma + 8), data))
                            {
                                return fail("a data buffer is not base64");
                            }
                        }
                        else if (bufferReader == nullptr || false == bufferReader(uri.c_str(), data, user))
                        {
                            return fail("a buffer file could not be read");
                        }
                    }
                    else if (at == 0 && hasBinary)
                    {
                        data = std::move(binary);
                    }
                    else
                    {
                        return fail("a buffer has neither a file nor a GLB chunk");
                    }
                    reader.buffers.Add(std::move(data));
                }
            }
            const Matrix identity;
            const auto scenes = root.find("scenes");
            const auto nodes = root.find("nodes");
            if (scenes != root.end() && scenes->is_array() && false == scenes->empty() && nodes != root.end())
            {
                const std::size_t scene = root.value("scene", std::size_t{0});
                if (scene >= scenes->size())
                {
                    return fail("the default scene does not exist");
                }
                const auto sceneNodes = (*scenes)[scene].find("nodes");
                if (sceneNodes != (*scenes)[scene].end() && sceneNodes->is_array())
                {
                    for (const Json& node : *sceneNodes)
                    {
                        if (false == VisitNode(reader, node.get<std::size_t>(), identity, 0, out))
                        {
                            return false;
                        }
                    }
                }
            }
            else
            {
                const auto meshes = root.find("meshes");
                const std::size_t count = meshes != root.end() && meshes->is_array() ? meshes->size() : 0;
                for (std::size_t at = 0; at < count; ++at)
                {
                    if (false == AppendMesh(reader, at, identity, out))
                    {
                        return false;
                    }
                }
            }
        }
        catch (const Json::exception&)
        {
            return fail("a glTF value has the wrong type");
        }
        if (out.indices.IsEmpty())
        {
            return fail("the file has no triangles");
        }
        return true;
    }

    void WriteCookedMesh(const MeshAssetData& mesh, Array<std::byte>& out)
    {
        const std::uint32_t head[4] = {CookedMagic, CookedVersion, static_cast<std::uint32_t>(mesh.vertices.Size()), static_cast<std::uint32_t>(mesh.indices.Size())};
        const std::size_t vertexBytes = mesh.vertices.Size() * sizeof(Float);
        const std::size_t indexBytes = mesh.indices.Size() * sizeof(UInt32);
        out.Resize(sizeof(head) + vertexBytes + indexBytes);
        std::memcpy(out.Data(), head, sizeof(head));
        if (vertexBytes != 0)
        {
            std::memcpy(out.Data() + sizeof(head), mesh.vertices.Data(), vertexBytes);
        }
        if (indexBytes != 0)
        {
            std::memcpy(out.Data() + sizeof(head) + vertexBytes, mesh.indices.Data(), indexBytes);
        }
    }

    Bool ReadCookedMesh(const std::byte* bytes, std::size_t size, MeshAssetData& out)
    {
        std::uint32_t head[4] = {};
        if (bytes == nullptr || size < sizeof(head))
        {
            return false;
        }
        std::memcpy(head, bytes, sizeof(head));
        const std::uint64_t vertexBytes = static_cast<std::uint64_t>(head[2]) * sizeof(Float);
        const std::uint64_t indexBytes = static_cast<std::uint64_t>(head[3]) * sizeof(UInt32);
        if (head[0] != CookedMagic || head[1] != CookedVersion || head[2] % MeshVertexFloats.Get() != 0 || head[3] % 3 != 0
            || sizeof(head) + vertexBytes + indexBytes != size)
        {
            return false;
        }
        out.vertices.Resize(head[2]);
        out.indices.Resize(head[3]);
        if (vertexBytes != 0)
        {
            std::memcpy(out.vertices.Data(), bytes + sizeof(head), static_cast<std::size_t>(vertexBytes));
        }
        if (indexBytes != 0)
        {
            std::memcpy(out.indices.Data(), bytes + sizeof(head) + vertexBytes, static_cast<std::size_t>(indexBytes));
        }
        const std::uint32_t vertexCount = head[2] / MeshVertexFloats.Get();
        for (const UInt32 index : out.indices)
        {
            if (index >= vertexCount)
            {
                return false;
            }
        }
        return true;
    }
}
