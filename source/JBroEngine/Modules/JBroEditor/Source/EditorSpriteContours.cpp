#include <JBro/Editor/EditorSpriteContours.h>

#include <JBro/Asset/Asset.h>

namespace JBro
{
    void EditorSpriteContours::Initialize(AssetSystem& assets)
    {
        m_assets = &assets;
    }

    void EditorSpriteContours::Shutdown()
    {
        Clear();
        m_assets = nullptr;
    }

    void EditorSpriteContours::BeginFrame()
    {
        m_budget = MaxNewPerFrame;
    }

    void EditorSpriteContours::Clear()
    {
        m_entries.Clear();
    }

    EditorSpriteContours::Entry* EditorSpriteContours::Find(
        AssetHandle texture, const SpriteFrame& frame)
    {
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            Entry& entry = m_entries[index];
            if (entry.texture.index == texture.index
                && entry.texture.generation == texture.generation && entry.x == frame.x && entry.y == frame.y
                && entry.width == frame.width && entry.height == frame.height)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    bool EditorSpriteContours::Build(
        AssetHandle texture, const SpriteFrame& frame, Entry& entry)
    {
        if (frame.width == 0 || frame.height == 0)
        {
            return false;
        }
        // 참조 수를 올리지 않는다. 부르는 쪽의 스프라이트가 이 텍스처를 잡고 있다.
        const TextureData* data = m_assets->GetTexture(texture);
        if (data == nullptr || data->width == 0 || data->height == 0
            || data->pixels.Size() < static_cast<std::size_t>(data->width) * data->height * 4)
        {
            return false;
        }
        // 칸이 그림 밖으로 나가면 잴 수 없다. 메타가 그림보다 큰 칸을 적어 둔 경우다.
        if (frame.x + frame.width > data->width || frame.y + frame.height > data->height)
        {
            return false;
        }

        const std::uint32_t longest =
            frame.width > frame.height ? frame.width : frame.height;
        std::uint32_t step = 1;
        while (longest / step > MaxSide)
        {
            ++step;
        }
        const std::uint32_t columns = frame.width / step > 0 ? frame.width / step : 1;
        const std::uint32_t rows = frame.height / step > 0 ? frame.height / step : 1;

        // 줄인 칸의 알파 마스크다. 한 칸이라도 불투명한 픽셀을 담고 있으면 그 칸은 있는 것이다 -
        // 가운데 픽셀만 보면 가는 선이 통째로 사라진다.
        Array<std::uint8_t> mask;
        mask.Resize(static_cast<std::size_t>(columns) * rows);
        const std::byte* pixels = data->pixels.Data();
        for (std::uint32_t row = 0; row < rows; ++row)
        {
            for (std::uint32_t column = 0; column < columns; ++column)
            {
                std::uint8_t present = 0;
                for (std::uint32_t inner = 0; inner < step && present == 0; ++inner)
                {
                    const std::uint32_t sourceY = frame.y + row * step + inner;
                    if (sourceY >= frame.y + frame.height)
                    {
                        break;
                    }
                    for (std::uint32_t across = 0; across < step; ++across)
                    {
                        const std::uint32_t sourceX = frame.x + column * step + across;
                        if (sourceX >= frame.x + frame.width)
                        {
                            break;
                        }
                        const std::size_t at =
                            (static_cast<std::size_t>(sourceY) * data->width + sourceX) * 4 + 3;
                        if (static_cast<std::uint8_t>(pixels[at]) >= AlphaThreshold)
                        {
                            present = 1;
                            break;
                        }
                    }
                }
                mask[static_cast<std::size_t>(row) * columns + column] = present;
            }
        }
        entry.pixelGeneration = data->pixelGeneration;

        // 불투명한 칸의 네 변 중 **이웃이 없는 쪽**만 남긴다. 칸 밖은 없는 것으로 본다 -
        // 그림이 칸 가장자리까지 차 있으면 그 변이 곧 경계다.
        const float cellWidth = 1.0f / static_cast<float>(columns);
        const float cellHeight = 1.0f / static_cast<float>(rows);
        const auto filled = [&](std::int64_t column, std::int64_t row) {
            if (column < 0 || row < 0 || column >= static_cast<std::int64_t>(columns)
                || row >= static_cast<std::int64_t>(rows))
            {
                return false;
            }
            return mask[static_cast<std::size_t>(row) * columns
                + static_cast<std::size_t>(column)] != 0;
        };
        for (std::uint32_t row = 0; row < rows; ++row)
        {
            for (std::uint32_t column = 0; column < columns; ++column)
            {
                if (false == filled(column, row))
                {
                    continue;
                }
                const float left = static_cast<float>(column) * cellWidth;
                const float right = left + cellWidth;
                const float top = static_cast<float>(row) * cellHeight;
                const float bottom = top + cellHeight;
                if (false == filled(column, static_cast<std::int64_t>(row) - 1))
                {
                    entry.segments.Add(Segment{left, top, right, top});
                }
                if (false == filled(column, static_cast<std::int64_t>(row) + 1))
                {
                    entry.segments.Add(Segment{left, bottom, right, bottom});
                }
                if (false == filled(static_cast<std::int64_t>(column) - 1, row))
                {
                    entry.segments.Add(Segment{left, top, left, bottom});
                }
                if (false == filled(static_cast<std::int64_t>(column) + 1, row))
                {
                    entry.segments.Add(Segment{right, top, right, bottom});
                }
            }
        }
        if (entry.segments.IsEmpty())
        {
            // 칸이 통째로 비어 있다(전부 투명). 그래도 무엇을 골랐는지는 보여야 하므로
            // 칸의 네 변을 준다 - 아무것도 그리지 않으면 선택이 사라진 것처럼 보인다.
            entry.segments.Add(Segment{0.0f, 0.0f, 1.0f, 0.0f});
            entry.segments.Add(Segment{1.0f, 0.0f, 1.0f, 1.0f});
            entry.segments.Add(Segment{1.0f, 1.0f, 0.0f, 1.0f});
            entry.segments.Add(Segment{0.0f, 1.0f, 0.0f, 0.0f});
        }
        return true;
    }

    const Array<EditorSpriteContours::Segment>* EditorSpriteContours::Get(
        AssetHandle texture, const SpriteFrame& frame)
    {
        if (m_assets == nullptr || false == m_assets->IsLoaded(texture))
        {
            return nullptr;
        }
        if (Entry* found = Find(texture, frame))
        {
            if (found->failed)
            {
                return nullptr;
            }
            {
                const TextureData* data = m_assets->GetTexture(texture);
                if (data != nullptr && data->pixelGeneration != found->pixelGeneration)
                {
                    // 제자리 재로드로 그림이 바뀌었다. 다시 잰다.
                    for (std::size_t index = 0; index < m_entries.Size(); ++index)
                    {
                        if (&m_entries[index] == found)
                        {
                            m_entries.RemoveAt(index);
                            break;
                        }
                    }
                    return Get(texture, frame);
                }
            }
            return &found->segments;
        }
        if (m_budget == 0)
        {
            return nullptr;
        }
        --m_budget;

        Entry entry;
        entry.texture = texture;
        entry.x = frame.x;
        entry.y = frame.y;
        entry.width = frame.width;
        entry.height = frame.height;
        if (false == Build(texture, frame, entry))
        {
            // 실패도 기억한다. 기억하지 않으면 프레임마다 같은 그림을 다시 읽는다.
            entry.failed = true;
            entry.segments.Clear();
            m_entries.Add(std::move(entry));
            return nullptr;
        }
        m_entries.Add(std::move(entry));
        return &m_entries[m_entries.Size() - 1].segments;
    }
}
