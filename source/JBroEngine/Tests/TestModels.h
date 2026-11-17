#pragma once

#include <cstdint>
#include <string>

// **시험이 짓는 모델 파일**이다. 내려받지 않고 바이트로 짓는다 - 디코더 시험(`ModelDecoder`)과 에셋·에디터 시험(내장 그림)이 같이 쓴다.
namespace TestModels
{
    inline std::string Base64(const std::string& bytes)
    {
        static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        std::size_t index = 0;
        while (index + 2 < bytes.size())
        {
            const std::uint32_t chunk = (static_cast<std::uint8_t>(bytes[index]) << 16) | (static_cast<std::uint8_t>(bytes[index + 1]) << 8)
                | static_cast<std::uint8_t>(bytes[index + 2]);
            out += alphabet[(chunk >> 18) & 63];
            out += alphabet[(chunk >> 12) & 63];
            out += alphabet[(chunk >> 6) & 63];
            out += alphabet[chunk & 63];
            index += 3;
        }
        const std::size_t rest = bytes.size() - index;
        if (rest == 1)
        {
            const std::uint32_t chunk = static_cast<std::uint8_t>(bytes[index]) << 16;
            out += alphabet[(chunk >> 18) & 63];
            out += alphabet[(chunk >> 12) & 63];
            out += "==";
        }
        else if (rest == 2)
        {
            const std::uint32_t chunk = (static_cast<std::uint8_t>(bytes[index]) << 16) | (static_cast<std::uint8_t>(bytes[index + 1]) << 8);
            out += alphabet[(chunk >> 18) & 63];
            out += alphabet[(chunk >> 12) & 63];
            out += alphabet[(chunk >> 6) & 63];
            out += '=';
        }
        return out;
    }

    template <typename T>
    inline void Append(std::string& bytes, const T& value)
    {
        bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    // y=0 평면의 1 x 1 사각형이다. 위(+Y)에서 볼 때 반시계로 감겨 있다(glTF 규약). 위치 48 바이트, 법선 48, UV 32, 인덱스 12.
    inline std::string QuadBuffer()
    {
        std::string bytes;
        const float positions[12] = {0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0};
        for (float value : positions)
        {
            Append(bytes, value);
        }
        for (int corner = 0; corner < 4; ++corner)
        {
            Append(bytes, 0.0f);
            Append(bytes, 1.0f);
            Append(bytes, 0.0f);
        }
        const float uvs[8] = {0, 0, 0, 1, 1, 1, 1, 0};
        for (float value : uvs)
        {
            Append(bytes, value);
        }
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        for (std::uint16_t value : indices)
        {
            Append(bytes, value);
        }
        return bytes;
    }

    // 사각형 하나의 glTF 다. `bufferUri` 가 비면 `.glb` 의 BIN 칸, `imageSource` 는 그림 칸의 JSON 조각이다.
    inline std::string QuadGltf(const std::string& bufferUri, std::size_t bufferLength, const std::string& imageSource)
    {
        std::string json = "{\"asset\":{\"version\":\"2.0\"},";
        json += "\"buffers\":[{" + (bufferUri.empty() ? std::string() : "\"uri\":\"" + bufferUri + "\",") + "\"byteLength\":" + std::to_string(bufferLength) + "}],";
        json += "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48},"
                "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":32},{\"buffer\":0,\"byteOffset\":128,\"byteLength\":12}";
        if (bufferLength > 140)
        {
            json += ",{\"buffer\":0,\"byteOffset\":140,\"byteLength\":" + std::to_string(bufferLength - 140) + "}";
        }
        json += "],";
        json += "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,0,1]},"
                "{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
                "{\"bufferView\":2,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"},"
                "{\"bufferView\":3,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}],";
        json += "\"images\":[" + imageSource + "],\"textures\":[{\"source\":0}],";
        json += "\"materials\":[{\"name\":\"Brick\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0.5,0.25,1],\"metallicFactor\":0.2,"
                "\"roughnessFactor\":0.7,\"baseColorTexture\":{\"index\":0}}}],";
        json += "\"meshes\":[{\"name\":\"Quad\",\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,\"material\":0}]}],";
        json += "\"nodes\":[{\"name\":\"Root\",\"translation\":[1,2,3],\"scale\":[2,2,2],\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";
        return json;
    }

    // 2x2 RGBA PNG 다. 왼쪽 위 빨강, 오른쪽 위 초록, 왼쪽 아래 파랑, 오른쪽 아래 반투명(알파 약 0.5) 흰색.
    inline std::string TinyPng()
    {
        static const unsigned char bytes[] =
        {
            0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d,
            0x24, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
            0x1f, 0x0c, 0x81, 0x34, 0x08, 0x34, 0x00, 0x00, 0x49, 0x49, 0x09, 0x78, 0x9c, 0x51, 0x17, 0x92,
            0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
        };
        return std::string(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    }

    // 한 가지 색으로 칠한 2x2 TGA 다(비압축 32 비트, 왼쪽 위 원점). PNG 를 짓지 않고 가운데 회색 같은 값을 그림으로 줄 때 쓴다.
    inline std::string SolidTga(unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha)
    {
        std::string bytes(18, '\0');
        bytes[2] = 2;
        bytes[12] = 2;
        bytes[14] = 2;
        bytes[16] = 32;
        bytes[17] = 0x28;
        for (int texel = 0; texel < 4; ++texel)
        {
            bytes += static_cast<char>(blue);
            bytes += static_cast<char>(green);
            bytes += static_cast<char>(red);
            bytes += static_cast<char>(alpha);
        }
        return bytes;
    }

    // 그림 하나가 내장된 사각형 glTF 하나다. 버퍼와 그림이 모두 데이터 URI 라 곁 파일이 없다. 그림의 이름이 하위 Texture 의 이름이 된다.
    inline std::string SelfContainedQuadGltf(const std::string& imageName, const std::string& imageBytes)
    {
        const std::string buffer = QuadBuffer();
        return QuadGltf("data:application/octet-stream;base64," + Base64(buffer), buffer.size(),
            "{\"name\":\"" + imageName + "\",\"uri\":\"data:image/png;base64," + Base64(imageBytes) + "\"}");
    }
}
