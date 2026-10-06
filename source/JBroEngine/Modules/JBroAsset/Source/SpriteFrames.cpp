#include <JBro/Asset/SpriteFrames.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>

namespace JBro
{
    Bool BuildSpriteFrames(
        UInt32 textureWidth,
        UInt32 textureHeight,
        const SpriteImportOptions& options,
        Array<SpriteFrame>& frames)
    {
        Array<SpriteFrame> built;
        if (textureWidth == 0 || textureHeight == 0)
        {
            return false;
        }

        if (options.sliceType == SpriteSliceType::None)
        {
            SpriteFrame whole;
            whole.width = textureWidth;
            whole.height = textureHeight;
            whole.pivotX = options.pivotX;
            whole.pivotY = options.pivotY;
            built.Add(whole);
            frames = std::move(built);
            return true;
        }

        // 여백을 뺀 안쪽 영역이다. 여백이 이미지보다 크면 자를 것이 없다.
        if (options.marginX * 2 >= textureWidth || options.marginY * 2 >= textureHeight)
        {
            return false;
        }
        const UInt32 innerWidth = textureWidth - options.marginX * 2;
        const UInt32 innerHeight = textureHeight - options.marginY * 2;

        UInt32 cellWidth = 0;
        UInt32 cellHeight = 0;
        UInt32 columns = 0;
        UInt32 rows = 0;
        if (options.sliceType == SpriteSliceType::CellCount)
        {
            columns = options.columnCount;
            rows = options.rowCount;
            if (columns == 0 || rows == 0)
            {
                return false;
            }
            // 간격이 차지하는 폭을 뺀 나머지를 칸 수로 나눈다.
            const UInt32 gapsX = options.gapX * (columns - 1);
            const UInt32 gapsY = options.gapY * (rows - 1);
            if (gapsX >= innerWidth || gapsY >= innerHeight)
            {
                return false;
            }
            cellWidth = (innerWidth - gapsX) / columns;
            cellHeight = (innerHeight - gapsY) / rows;
        }
        else
        {
            cellWidth = options.cellWidth;
            cellHeight = options.cellHeight;
            if (cellWidth == 0 || cellHeight == 0 || cellWidth > innerWidth || cellHeight > innerHeight)
            {
                return false;
            }
            columns = 1 + (innerWidth - cellWidth) / (cellWidth + options.gapX);
            rows = 1 + (innerHeight - cellHeight) / (cellHeight + options.gapY);
        }
        if (cellWidth == 0 || cellHeight == 0)
        {
            return false;
        }

        for (UInt32 row = 0; row < rows; ++row)
        {
            for (UInt32 column = 0; column < columns; ++column)
            {
                SpriteFrame frame;
                frame.x = options.marginX + column * (cellWidth + options.gapX);
                frame.y = options.marginY + row * (cellHeight + options.gapY);
                frame.width = cellWidth;
                frame.height = cellHeight;
                frame.pivotX = options.pivotX;
                frame.pivotY = options.pivotY;
                if (frame.x + frame.width > textureWidth || frame.y + frame.height > textureHeight)
                {
                    continue;
                }
                built.Add(frame);
            }
        }
        if (built.IsEmpty())
        {
            return false;
        }
        frames = std::move(built);
        return true;
    }
}
