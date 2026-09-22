#include "EditorThumbnails.h"

#include <JBro/Asset/Asset.h>
#include <JBro/Core/Log.h>

namespace JBro
{
    void EditorThumbnails::Initialize(IRHIDevice& device, AssetSystem& assets)
    {
        m_device = &device;
        m_assets = &assets;
    }

    void EditorThumbnails::Shutdown()
    {
        Clear();
        m_device = nullptr;
        m_assets = nullptr;
    }

    void EditorThumbnails::BeginFrame()
    {
        m_budget = MaxNewPerFrame;
    }

    void EditorThumbnails::Clear()
    {
        if (m_device != nullptr)
        {
            for (std::size_t index = 0; index < m_entries.Size(); ++index)
            {
                if (m_entries[index].texture.IsValid())
                {
                    m_device->DestroyTexture(m_entries[index].texture);
                }
            }
        }
        m_entries.Clear();
    }

    EditorThumbnails::Entry* EditorThumbnails::Find(AssetId asset, std::uint32_t maxSide)
    {
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            if (m_entries[index].asset == asset && m_entries[index].maxSide == maxSide)
            {
                return &m_entries[index];
            }
        }
        return nullptr;
    }

    void EditorThumbnails::Invalidate(AssetId asset)
    {
        // 크기마다 따로 들고 있으니 **그 에셋의 것을 모두** 버린다.
        for (std::size_t index = 0; index < m_entries.Size();)
        {
            if (false == (m_entries[index].asset == asset))
            {
                ++index;
                continue;
            }
            if (m_entries[index].texture.IsValid() && m_device != nullptr)
            {
                m_device->DestroyTexture(m_entries[index].texture);
            }
            m_entries.RemoveAt(index);
        }
    }

    bool EditorThumbnails::GetSourceSize(
        AssetId asset, std::uint32_t& width, std::uint32_t& height) const
    {
        for (std::size_t index = 0; index < m_entries.Size(); ++index)
        {
            const Entry& entry = m_entries[index];
            if (entry.asset == asset && false == entry.failed && entry.sourceWidth != 0)
            {
                width = entry.sourceWidth;
                height = entry.sourceHeight;
                return true;
            }
        }
        return false;
    }

    bool EditorThumbnails::Build(AssetId asset, Entry& entry)
    {
        // **에셋을 잠깐만 든다.** 그림을 만들고 나면 픽셀은 GPU 에 있으므로, 들고 있을 이유가
        // 없다 - 계속 잡고 있으면 `CollectUnused` 가 영영 내리지 못한다.
        const AssetHandle handle = m_assets->Load(asset);
        if (false == m_assets->IsLoaded(handle))
        {
            return false;
        }
        const TextureData* data = m_assets->GetTexture(handle);
        if (data == nullptr || data->width == 0 || data->height == 0
            || data->pixels.Size() < static_cast<std::size_t>(data->width) * data->height * 4)
        {
            m_assets->Release(handle);
            return false;
        }

        // 긴 변이 `MaxSide` 를 넘으면 정수 배로 건너뛴다. 가중 평균이 아니라 건너뛰기다 -
        // 픽셀 아트의 또렷한 가장자리가 평균에 뭉개지면 그림을 알아보기 어려워진다.
        const std::uint32_t longest = data->width > data->height ? data->width : data->height;
        entry.sourceWidth = data->width;
        entry.sourceHeight = data->height;
        std::uint32_t step = 1;
        while (longest / step > entry.maxSide)
        {
            ++step;
        }
        const std::uint32_t width = data->width / step > 0 ? data->width / step : 1;
        const std::uint32_t height = data->height / step > 0 ? data->height / step : 1;

        Array<std::byte> pixels;
        pixels.Resize(static_cast<std::size_t>(width) * height * 4);
        const std::byte* source = data->pixels.Data();
        for (std::uint32_t y = 0; y < height; ++y)
        {
            const std::size_t sourceRow = static_cast<std::size_t>(y) * step * data->width;
            const std::size_t targetRow = static_cast<std::size_t>(y) * width;
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::size_t from = (sourceRow + static_cast<std::size_t>(x) * step) * 4;
                const std::size_t to = (targetRow + x) * 4;
                pixels[to + 0] = source[from + 0];
                pixels[to + 1] = source[from + 1];
                pixels[to + 2] = source[from + 2];
                pixels[to + 3] = source[from + 3];
            }
        }
        entry.pixelGeneration = data->pixelGeneration;
        m_assets->Release(handle);

        TextureDesc desc;
        desc.extent = {width, height};
        desc.format = TextureFormat::RGBA8Unorm;
        desc.usage = TextureUsage::Sampled;
        const TextureHandle texture = m_device->CreateTexture(desc);
        if (false == texture.IsValid())
        {
            return false;
        }
        const JArrayView<std::byte> bytes{
            pixels.Data(), static_cast<std::uint32_t>(pixels.Size())};
        if (false == m_device->WriteTexture(texture, 0, bytes))
        {
            // **올리지 못한 텍스처는 버린다.** 빈 텍스처를 들고 있으면 화면에 쓰레기가 뜬다.
            m_device->DestroyTexture(texture);
            return false;
        }
        entry.texture = texture;
        return true;
    }

    TextureHandle EditorThumbnails::Get(AssetId asset, std::uint32_t maxSide)
    {
        if (m_device == nullptr || m_assets == nullptr || asset.IsNull())
        {
            return TextureHandle{};
        }
        if (Entry* found = Find(asset, maxSide))
        {
            if (found->failed)
            {
                return TextureHandle{};
            }
            // 제자리 재로드로 픽셀이 바뀌었으면 다시 만든다.
            if (const AssetHandle loaded = m_assets->Find(asset);
                m_assets->IsLoaded(loaded))
            {
                const TextureData* data = m_assets->GetTexture(loaded);
                if (data != nullptr && data->pixelGeneration != found->pixelGeneration)
                {
                    Invalidate(asset);
                    return Get(asset, maxSide);
                }
            }
            return found->texture;
        }
        if (m_budget == 0)
        {
            // 이번 프레임의 몫을 다 썼다. 다음 프레임에 만들어진다 - 기다리는 칸은 비어 있다.
            return TextureHandle{};
        }
        --m_budget;

        Entry entry;
        entry.asset = asset;
        entry.maxSide = maxSide > 0 ? maxSide : MaxSide;
        if (false == Build(asset, entry))
        {
            // **실패도 기억한다.** 기억하지 않으면 프레임마다 같은 파일을 다시 읽는다.
            entry.failed = true;
            entry.texture = TextureHandle{};
            m_entries.Add(entry);
            return TextureHandle{};
        }
        m_entries.Add(entry);
        return entry.texture;
    }
}
