#include <JBro/Graphics/EnvironmentMap.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include <JBro/Types/Int.h>

namespace JBro
{
    namespace
    {
        constexpr Float Pi = 3.14159265358979f;

        // 선형 빛 한 장(rgb)이다. 굽는 동안만 산다.
        struct Image
        {
            UInt32 width = 0;
            UInt32 height = 0;
            Array<Float> rgb;

            Float At(UInt32 x, UInt32 y, UInt32 channel) const
            {
                return rgb[((static_cast<std::size_t>(y.Get()) * width.Get() + x.Get()) * 3u + channel.Get())];
            }
        };

        // 양선형으로 읽는다. 가로는 돌아가고(경도), 세로는 끝에서 멈춘다(극).
        void Sample(const Image& image, Float u, Float v, Float (&out)[3])
        {
            const Float fx = u * static_cast<JBro::Float>(image.width.Get()) - 0.5f;
            const Float fy = v * static_cast<JBro::Float>(image.height.Get()) - 0.5f;
            const Float floorX = std::floor(fx.Get());
            const Float floorY = std::floor(fy.Get());
            const Float tx = fx - floorX;
            const Float ty = fy - floorY;
            const Int32 width = static_cast<JBro::Int32>(image.width.Get());
            const Int32 height = static_cast<JBro::Int32>(image.height.Get());
            const auto wrapX = [&](Int32 x) {
                const Int32 wrapped = x % width;
                return static_cast<UInt32>((wrapped < 0 ? wrapped + width : wrapped).Get());
            };
            const auto clampY = [&](Int32 y) {
                return static_cast<UInt32>((y < 0 ? Int32(0) : (y >= height ? height - 1 : y)).Get());
            };
            const Int32 x0 = static_cast<JBro::Int32>(floorX.Get());
            const Int32 y0 = static_cast<JBro::Int32>(floorY.Get());
            const UInt32 ax = wrapX(x0);
            const UInt32 bx = wrapX(x0 + 1);
            const UInt32 ay = clampY(y0);
            const UInt32 by = clampY(y0 + 1);
            for (UInt32 channel = 0; channel < 3; ++channel)
            {
                const Float top = image.At(ax, ay, channel) * (1.0f - tx) + image.At(bx, ay, channel) * tx;
                const Float bottom = image.At(ax, by, channel) * (1.0f - tx) + image.At(bx, by, channel) * tx;
                out[channel.Get()] = top * (1.0f - ty) + bottom * ty;
            }
        }

        // 원본을 한 크기로 옮긴다. 줄일 때는 덮는 원본 텍셀의 평균이고, 늘릴 때(덮는 텍셀이 없을 때)는 가운데를 양선형으로 읽는다.
        void Resample(const Image& source, UInt32 width, UInt32 height, Image& result)
        {
            result.width = width;
            result.height = height;
            result.rgb.Resize(static_cast<std::size_t>(width.Get()) * height.Get() * 3u);
            for (UInt32 y = 0; y < height; ++y)
            {
                const UInt32 fromY = static_cast<UInt32>((static_cast<std::uint64_t>(y.Get()) * source.height.Get() / height.Get()));
                UInt32 toY = static_cast<UInt32>((static_cast<std::uint64_t>(y.Get() + 1) * source.height.Get() / height.Get()));
                for (UInt32 x = 0; x < width; ++x)
                {
                    const UInt32 fromX = static_cast<UInt32>((static_cast<std::uint64_t>(x.Get()) * source.width.Get() / width.Get()));
                    UInt32 toX = static_cast<UInt32>((static_cast<std::uint64_t>(x.Get() + 1) * source.width.Get() / width.Get()));
                    Float sum[3] = {0.0f, 0.0f, 0.0f};
                    if (toX > fromX && toY > fromY)
                    {
                        Float count = 0.0f;
                        for (UInt32 sy = fromY; sy < toY; ++sy)
                        {
                            for (UInt32 sx = fromX; sx < toX; ++sx)
                            {
                                for (UInt32 channel = 0; channel < 3; ++channel)
                                {
                                    sum[channel.Get()] += source.At(sx, sy, channel);
                                }
                                count += 1.0f;
                            }
                        }
                        for (Float& value : sum)
                        {
                            value = value / count;
                        }
                    }
                    else
                    {
                        const Float u = (static_cast<JBro::Float>(x.Get()) + 0.5f) / static_cast<JBro::Float>(width.Get());
                        const Float v = (static_cast<JBro::Float>(y.Get()) + 0.5f) / static_cast<JBro::Float>(height.Get());
                        Sample(source, u, v, sum);
                    }
                    for (UInt32 channel = 0; channel < 3; ++channel)
                    {
                        result.rgb[((static_cast<std::size_t>(y.Get()) * width.Get() + x.Get()) * 3u + channel.Get())] = sum[channel.Get()];
                    }
                }
            }
        }

        // 반정밀 실수다. 너무 크면 가장 큰 유한값(65504)이고, 아주 작은 것은 0 이다 - 빛의 값이라 음수·무한은 오지 않는다.
        std::uint16_t ToHalf(Float value)
        {
            Float clamped = value;
            if (false == std::isfinite(clamped.Get()) || clamped < 0.0f)
            {
                clamped = 0.0f;
            }
            if (clamped > 65504.0f)
            {
                clamped = 65504.0f;
            }
            if (clamped < 6.1035156e-05f)
            {
                return 0;
            }
            float raw = clamped.Get();
            std::uint32_t bits = 0;
            std::memcpy(&bits, &raw, sizeof(bits));
            const std::uint32_t exponent = ((bits >> 23) & 0xFFu) - 127u + 15u;
            std::uint32_t half = (exponent << 10) | ((bits >> 13) & 0x3FFu);
            if ((bits & 0x1FFFu) > 0x1000u || ((bits & 0x1FFFu) == 0x1000u && (half & 1u) != 0))
            {
                ++half;
            }
            return static_cast<std::uint16_t>(half);
        }

        void Pack(const Image& image, EnvironmentMapLevel& level)
        {
            level.width = image.width;
            level.height = image.height;
            const std::size_t texels = static_cast<std::size_t>(image.width.Get()) * image.height.Get();
            level.rgba16f.Resize(texels * 8u);
            for (std::size_t at = 0; at < texels; ++at)
            {
                const std::uint16_t texel[4] = {ToHalf(image.rgb[at * 3u]), ToHalf(image.rgb[at * 3u + 1u]), ToHalf(image.rgb[at * 3u + 2u]), 0x3C00u};
                std::memcpy(level.rgba16f.Data() + at * 8u, texel, sizeof(texel));
            }
        }

        // 반지름 역(radical inverse)으로 고른 표본 자리다(Hammersley).
        Float RadicalInverse(std::uint32_t bits)
        {
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            return static_cast<JBro::Float>(static_cast<double>(bits) * 2.3283064365386963e-10);
        }

        // 거칠기 `roughness` 의 GGX 로 한 방향을 거른다. 반사 방향이 법선이고 보는 방향이다(N = V = R, 미리 거르기의 가정).
        // 표본마다 그 표본이 덮는 입체각에 맞는 원본 밉에서 읽는다(Colbert·Křivánek 2007).
        void Prefilter(const Array<Image>& pyramid, Float roughness, Float nx, Float ny, Float nz, Float (&out)[3])
        {
            constexpr UInt32 SampleCount = 64;
            // 법선 둘레의 직교 기저다.
            const Bool nearPole = std::fabs(ny.Get()) > 0.999f;
            Float tx = nearPole ? Float(1.0f) : Float(nz);
            Float ty = 0.0f;
            Float tz = nearPole ? Float(0.0f) : Float(-nx);
            const Float tangentLength = std::sqrt((tx * tx + ty * ty + tz * tz).Get());
            tx = tx / tangentLength;
            ty = ty / tangentLength;
            tz = tz / tangentLength;
            const Float bx = ny * tz - nz * ty;
            const Float by = nz * tx - nx * tz;
            const Float bz = nx * ty - ny * tx;

            const Float alpha = roughness * roughness;
            const Float alphaSquared = alpha * alpha;
            const Float texelSolidAngle = 4.0f * Pi / static_cast<JBro::Float>((pyramid[0].width * pyramid[0].height).Get());
            Float sum[3] = {0.0f, 0.0f, 0.0f};
            Float weight = 0.0f;
            for (UInt32 index = 0; index < SampleCount; ++index)
            {
                const Float e1 = (static_cast<JBro::Float>(index.Get()) + 0.5f) / static_cast<JBro::Float>(SampleCount.Get());
                const Float e2 = RadicalInverse(index.Get());
                const Float phi = 2.0f * Pi * e1;
                const Float cosTheta = std::sqrt(((1.0f - e2) / (1.0f + (alphaSquared - 1.0f) * e2)).Get());
                const Float sinTheta = std::sqrt((Float(1.0f) - cosTheta * cosTheta).Get());
                // 반쪽 벡터 H 를 법선의 기저로 옮긴다.
                const Float hx = tx * (sinTheta * std::cos(phi.Get())) + bx * (sinTheta * std::sin(phi.Get())) + nx * cosTheta;
                const Float hy = ty * (sinTheta * std::cos(phi.Get())) + by * (sinTheta * std::sin(phi.Get())) + ny * cosTheta;
                const Float hz = tz * (sinTheta * std::cos(phi.Get())) + bz * (sinTheta * std::sin(phi.Get())) + nz * cosTheta;
                const Float vDotH = nx * hx + ny * hy + nz * hz;
                const Float lx = hx * (2.0f * vDotH) - nx;
                const Float ly = hy * (2.0f * vDotH) - ny;
                const Float lz = hz * (2.0f * vDotH) - nz;
                const Float nDotL = nx * lx + ny * ly + nz * lz;
                if (false == (nDotL > 0.0f))
                {
                    continue;
                }
                // GGX 의 확률 밀도(N = V 라 D / 4)와 그 표본이 덮는 입체각으로 읽을 밉을 고른다.
                const Float denominator = cosTheta * cosTheta * (alphaSquared - 1.0f) + 1.0f;
                const Float distribution = alphaSquared / (Pi * denominator * denominator);
                const Float pdf = distribution * 0.25f;
                const Float sampleSolidAngle = 1.0f / (static_cast<JBro::Float>(SampleCount.Get()) * pdf + 0.0001f);
                Float level = roughness > 0.0f ? Float(0.5f * std::log2((sampleSolidAngle / texelSolidAngle).Get()) + 1.0f) : Float(0.0f);
                const Float deepest = static_cast<JBro::Float>(pyramid.Size() - 1);
                level = level < 0.0f ? Float(0.0f) : (level > deepest ? deepest : level);
                const std::size_t lower = static_cast<std::size_t>(std::floor(level.Get()));
                const std::size_t upper = lower + 1 < pyramid.Size() ? lower + 1 : lower;
                const Float blend = level - static_cast<JBro::Float>(lower);
                Float u = 0.0f;
                Float v = 0.0f;
                EnvironmentUv(lx, ly, lz, u, v);
                Float a[3];
                Float b[3];
                Sample(pyramid[lower], u, v, a);
                Sample(pyramid[upper], u, v, b);
                for (Int32 channel = 0; channel < 3; ++channel)
                {
                    sum[channel.Get()] += (a[channel.Get()] * (1.0f - blend) + b[channel.Get()] * blend) * nDotL;
                }
                weight += nDotL;
            }
            for (Int32 channel = 0; channel < 3; ++channel)
            {
                out[channel.Get()] = weight > 0.0f ? Float(sum[channel.Get()] / weight) : Float(0.0f);
            }
        }

        void ShBasis(Float x, Float y, Float z, Float (&basis)[9])
        {
            basis[0] = 0.282095f;
            basis[1] = 0.488603f * y;
            basis[2] = 0.488603f * z;
            basis[3] = 0.488603f * x;
            basis[4] = 1.092548f * x * y;
            basis[5] = 1.092548f * y * z;
            basis[6] = 0.315392f * (3.0f * z * z - 1.0f);
            basis[7] = 1.092548f * x * z;
            basis[8] = 0.546274f * (x * x - y * y);
        }
    }

    void EnvironmentDirection(Float u, Float v, Float& x, Float& y, Float& z)
    {
        const Float phi = (u - 0.5f) * 2.0f * Pi;
        const Float theta = v * Pi;
        const Float sinTheta = std::sin(theta.Get());
        x = sinTheta * std::sin(phi.Get());
        y = std::cos(theta.Get());
        z = -(sinTheta * std::cos(phi.Get()));
    }

    void EnvironmentUv(Float x, Float y, Float z, Float& u, Float& v)
    {
        u = std::atan2(x.Get(), -z.Get()) / (2.0f * Pi) + 0.5f;
        const Float clamped = y < -1.0f ? Float(-1.0f) : (y > 1.0f ? Float(1.0f) : y);
        v = std::acos(clamped.Get()) / Pi;
    }

    void ProjectEnvironmentSh(const Float* rgb, UInt32 width, UInt32 height, Float (&sh)[9][3])
    {
        for (auto& coefficient : sh)
        {
            coefficient[0] = 0.0f;
            coefficient[1] = 0.0f;
            coefficient[2] = 0.0f;
        }
        if (rgb == nullptr || width == 0 || height == 0)
        {
            return;
        }
        // 텍셀마다 입체각(dφ dθ sinθ)으로 무게를 준다. 조도 / π 는 띠마다 1·2/3·1/4 를 곱한 것이다.
        const Float bandScale[9] = {1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
        const Float cell = (2.0f * Pi / static_cast<JBro::Float>(width.Get())) * (Pi / static_cast<JBro::Float>(height.Get()));
        for (UInt32 row = 0; row < height; ++row)
        {
            const Float v = (static_cast<JBro::Float>(row.Get()) + 0.5f) / static_cast<JBro::Float>(height.Get());
            const Float solidAngle = cell * std::sin((v * Pi).Get());
            for (UInt32 column = 0; column < width; ++column)
            {
                const Float u = (static_cast<JBro::Float>(column.Get()) + 0.5f) / static_cast<JBro::Float>(width.Get());
                Float x = 0.0f;
                Float y = 0.0f;
                Float z = 0.0f;
                EnvironmentDirection(u, v, x, y, z);
                Float basis[9];
                ShBasis(x, y, z, basis);
                const Float* texel = rgb + (static_cast<std::size_t>(row.Get()) * width.Get() + column.Get()) * 3u;
                for (Int32 at = 0; at < 9; ++at)
                {
                    for (Int32 channel = 0; channel < 3; ++channel)
                    {
                        sh[at.Get()][channel.Get()] += texel[channel.Get()] * basis[at.Get()] * solidAngle;
                    }
                }
            }
        }
        for (Int32 at = 0; at < 9; ++at)
        {
            for (Int32 channel = 0; channel < 3; ++channel)
            {
                sh[at.Get()][channel.Get()] = sh[at.Get()][channel.Get()] * bandScale[at.Get()];
            }
        }
    }

    void EvaluateEnvironmentSh(const Float (&sh)[9][3], Float x, Float y, Float z, Float (&rgb)[3])
    {
        Float basis[9];
        ShBasis(x, y, z, basis);
        for (Int32 channel = 0; channel < 3; ++channel)
        {
            Float value = 0.0f;
            for (Int32 at = 0; at < 9; ++at)
            {
                value += sh[at.Get()][channel.Get()] * basis[at.Get()];
            }
            rgb[channel.Get()] = value > 0.0f ? value : Float(0.0f);
        }
    }

    Bool BuildEnvironmentMap(const Float* rgb, UInt32 width, UInt32 height, EnvironmentMapBuild& result, const EnvironmentMapCancel& cancel)
    {
        const auto cancelled = [&cancel]() -> Bool {
            return cancel.requested != nullptr && cancel.requested(cancel.context);
        };
        if (rgb == nullptr || width == 0 || height == 0)
        {
            return false;
        }
        Image source;
        source.width = width;
        source.height = height;
        source.rgb.Resize(static_cast<std::size_t>(width.Get()) * height.Get() * 3u);
        // 빛은 0 이상의 유한값이다. 잘리거나 깨진 파일의 쓰레기 값(무한·NaN·음수)은 0 으로 둔다 - 한 텍셀이 SH 와 거친 단계 전체를 더럽힌다.
        for (std::size_t at = 0; at < source.rgb.Size(); ++at)
        {
            const Float value = rgb[at];
            source.rgb[at] = std::isfinite(value.Get()) && value > 0.0f ? (value < 65504.0f ? value : Float(65504.0f)) : Float(0.0f);
        }

        // 원본 피라미드: 밉 0 크기로 맞춘 것에서 반씩 줄인다. 미리 거르기가 표본의 입체각에 맞는 단계를 읽는다.
        // 16 x 8 에서 멈춘다 - 그보다 거친 단계의 텍셀은 하늘의 반을 덮어, 거친 표본이 읽으면 반대편의 빛이 새어 든다(시험에서 0.03).
        Array<Image> pyramid;
        if (cancelled())
        {
            return false;
        }
        pyramid.Add(Image{});
        Resample(source, EnvironmentMapWidth, EnvironmentMapHeight, pyramid[0]);
        while (pyramid[pyramid.Size() - 1].width > 16 && pyramid[pyramid.Size() - 1].height > 8)
        {
            if (cancelled())
            {
                return false;
            }
            const Image& last = pyramid[pyramid.Size() - 1];
            Image smaller;
            Resample(last, last.width / 2, last.height / 2, smaller);
            pyramid.Add(std::move(smaller));
        }

        EnvironmentMapBuild built;
        ProjectEnvironmentSh(pyramid[0].rgb.Data(), pyramid[0].width, pyramid[0].height, built.sh);
        built.levels.Resize(EnvironmentMapLevels.Get());
        Pack(pyramid[0], built.levels[0]);
        for (UInt32 levelIndex = 1; levelIndex < EnvironmentMapLevels; ++levelIndex)
        {
            const Float roughness = static_cast<JBro::Float>(levelIndex.Get()) / static_cast<JBro::Float>((EnvironmentMapLevels - 1).Get());
            Image level;
            level.width = EnvironmentMapWidth.Get() >> levelIndex.Get();
            level.height = EnvironmentMapHeight.Get() >> levelIndex.Get();
            level.rgb.Resize(static_cast<std::size_t>(level.width.Get()) * level.height.Get() * 3u);
            for (UInt32 y = 0; y < level.height; ++y)
            {
                // 줄마다 묻는다. 가장 무거운 단계의 한 줄도 디버그에서 몇십 ms 안이다.
                if (cancelled())
                {
                    return false;
                }
                const Float v = (static_cast<JBro::Float>(y.Get()) + 0.5f) / static_cast<JBro::Float>(level.height.Get());
                for (UInt32 x = 0; x < level.width; ++x)
                {
                    const Float u = (static_cast<JBro::Float>(x.Get()) + 0.5f) / static_cast<JBro::Float>(level.width.Get());
                    Float nx = 0.0f;
                    Float ny = 0.0f;
                    Float nz = 0.0f;
                    EnvironmentDirection(u, v, nx, ny, nz);
                    Float filtered[3];
                    Prefilter(pyramid, roughness, nx, ny, nz, filtered);
                    for (Int32 channel = 0; channel < 3; ++channel)
                    {
                        level.rgb[((static_cast<std::size_t>(y.Get()) * level.width.Get() + x.Get()) * 3u + channel.Get())] = filtered[channel.Get()];
                    }
                }
            }
            Pack(level, built.levels[levelIndex.Get()]);
        }
        result = std::move(built);
        return true;
    }
}
