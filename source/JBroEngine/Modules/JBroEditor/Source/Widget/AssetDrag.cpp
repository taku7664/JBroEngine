#include <JBro/Editor/Widget/AssetDrag.h>

#include <JBro/Types/Array.h>

#include <imgui.h>

#include <cstring>

namespace JBro::Widget
{
    void SetAssetDragPayload(AssetId primary, AssetId paired, const String& paths)
    {
        // 머리 뒤에 경로 묶음을 잇는다. ImGui 가 꾸러미를 복사해 들므로 여기 버퍼는 잠깐이다.
        AssetDragHeader header;
        header.primary = primary;
        header.paired = paired;
        header.pathBytes = static_cast<std::uint32_t>(paths.size() + 1);
        Array<std::byte> buffer;
        buffer.Resize(sizeof(AssetDragHeader) + header.pathBytes);
        std::memcpy(buffer.Data(), &header, sizeof(header));
        std::memcpy(buffer.Data() + sizeof(header), paths.c_str(), header.pathBytes);
        ImGui::SetDragDropPayload(AssetDragPayloadType, buffer.Data(), buffer.Size());
    }

    bool AcceptAssetDrop(AssetDragHeader& header, String* paths)
    {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragPayloadType);
        if (payload == nullptr || payload->Data == nullptr
            || payload->DataSize < static_cast<int>(sizeof(AssetDragHeader)))
        {
            return false;
        }
        std::memcpy(&header, payload->Data, sizeof(header));
        // 길이가 맞지 않는 꾸러미는 받지 않는다. 믿고 읽으면 남의 기억을 경로로 읽는다.
        if (static_cast<std::size_t>(payload->DataSize) != sizeof(header) + header.pathBytes
            || header.pathBytes == 0)
        {
            return false;
        }
        if (paths != nullptr)
        {
            const char* text = static_cast<const char*>(payload->Data) + sizeof(header);
            *paths = String(text, header.pathBytes - 1);
        }
        return true;
    }
}
