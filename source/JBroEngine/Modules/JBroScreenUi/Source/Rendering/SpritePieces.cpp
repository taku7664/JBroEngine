#include <JBro/ScreenUi/Rendering/SpritePieces.h>

#include <JBro/ScreenUi/Rendering/RenderWorld2D.h>

#include <cmath>

namespace JBro
{
    namespace
    {
        // 양쪽 테두리의 합이 길이를 넘으면 비례로 줄인다.
        void FitBorders(Float& first, Float& second, Float length)
        {
            first = Float(first > 0.0f ? first.Get() : 0.0f);
            second = Float(second > 0.0f ? second.Get() : 0.0f);
            const Float sum = first + second;
            if (sum > length && sum > 0.0f)
            {
                first = first * length / sum;
                second = second * length / sum;
            }
        }

        Float Fraction(Float part, Float whole)
        {
            return Float(whole > 0.0f ? Float::Clamp(part / whole, 0.0f, 1.0f) : 0.0f);
        }
    }

    UInt32 BuildSpritePieces(const SpriteRenderItem& item, SpritePiece* out, UInt32 capacity)
    {
        if (out == nullptr || capacity == 0)
        {
            return 0;
        }
        const Float width = std::fabs(item.size.x);
        const Float height = std::fabs(item.size.y);
        const Bool known = width > 0.0f && height > 0.0f && item.tileWidth > 0.0f && item.tileHeight > 0.0f
            && std::isfinite(width.Get()) && std::isfinite(height.Get());
        switch (item.drawMode)
        {
        case Component::SpriteDrawMode::Sliced:
        {
            if (false == known)
            {
                out[0] = WholeSpritePiece();
                return 1;
            }
            Float left = item.border[0];
            Float top = item.border[1];
            Float right = item.border[2];
            Float bottom = item.border[3];
            FitBorders(left, right, width);
            FitBorders(bottom, top, height);
            // 그려지는 비율은 사각형 크기로, 텍스처 비율은 칸 크기로 잰다 - 그래서 모서리는 그대로이고 변·가운데만 늘어난다.
            const Float gx[4] = { 0.0f, Fraction(left, width), 1.0f - Fraction(right, width), 1.0f };
            const Float gy[4] = { 0.0f, Fraction(bottom, height), 1.0f - Fraction(top, height), 1.0f };
            const Float tx[4] = { 0.0f, Fraction(left, item.tileWidth), 1.0f - Fraction(right, item.tileWidth), 1.0f };
            const Float ty[4] = { 0.0f, Fraction(bottom, item.tileHeight), 1.0f - Fraction(top, item.tileHeight), 1.0f };
            UInt32 count = 0;
            for (UInt32 row = 0; row < 3 && count < capacity; ++row)
            {
                for (UInt32 column = 0; column < 3 && count < capacity; ++column)
                {
                    const Float gu0 = gx[column];
                    const Float gu1 = gx[column + 1];
                    const Float gv0 = gy[row];
                    const Float gv1 = gy[row + 1];
                    if (gu1 <= gu0 || gv1 <= gv0)
                    {
                        continue;
                    }
                    out[count] = { gu0, gu1, gv0, gv1, tx[column], tx[column + 1], ty[row], ty[row + 1] };
                    ++count;
                }
            }
            return count;
        }
        case Component::SpriteDrawMode::Tiled:
        {
            if (false == known)
            {
                out[0] = WholeSpritePiece();
                return 1;
            }
            const Float tileU = item.tileWidth / width;
            const Float tileV = item.tileHeight / height;
            UInt32 count = 0;
            for (Float v0 = 0.0f; v0 < 1.0f && count < capacity; v0 += tileV)
            {
                const Float v1 = Float(v0 + tileV < 1.0f ? (v0 + tileV).Get() : 1.0f);
                for (Float u0 = 0.0f; u0 < 1.0f && count < capacity; u0 += tileU)
                {
                    const Float u1 = Float(u0 + tileU < 1.0f ? (u0 + tileU).Get() : 1.0f);
                    // 마지막 타일은 남은 만큼만 - 쿼드와 텍스처를 같은 비율로 자른다.
                    out[count] = { u0, u1, v0, v1, 0.0f, (u1 - u0) / tileU, 0.0f, (v1 - v0) / tileV };
                    ++count;
                }
            }
            return count;
        }
        case Component::SpriteDrawMode::Filled:
        {
            const Float amount = Float(std::isfinite(item.fillAmount.Get()) ? Float::Clamp(item.fillAmount, 0.0f, 1.0f) : 0.0f);
            if (amount <= 0.0f)
            {
                return 0;
            }
            SpritePiece piece = WholeSpritePiece();
            switch (item.fillOrigin)
            {
            case Component::SpriteFillOrigin::Right:
                piece.gu0 = 1.0f - amount;
                piece.tu0 = 1.0f - amount;
                break;
            case Component::SpriteFillOrigin::Bottom:
                piece.gv1 = amount;
                piece.tv1 = amount;
                break;
            case Component::SpriteFillOrigin::Top:
                piece.gv0 = 1.0f - amount;
                piece.tv0 = 1.0f - amount;
                break;
            case Component::SpriteFillOrigin::Left:
            default:
                piece.gu1 = amount;
                piece.tu1 = amount;
                break;
            }
            out[0] = piece;
            return 1;
        }
        case Component::SpriteDrawMode::Simple:
        default:
            out[0] = WholeSpritePiece();
            return 1;
        }
    }
}
