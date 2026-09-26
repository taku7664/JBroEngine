#include <JBro/SaveTypes/Service/SaveService.h>

#include <JBro/SaveTypes/Internal/SystemContext.h>

namespace JBro::Service
{
    bool SaveService::IsReady() const
    {
        const System::ISaveStorage* storage = GetSaveSystems().Storage;
        return storage != nullptr && storage->IsReady();
    }

    bool SaveService::WriteBytes(const char* slot, const void* data, std::size_t size) const
    {
        System::ISaveStorage* storage = GetSaveSystems().Storage;
        return storage != nullptr && storage->Write(slot, data, size);
    }

    bool SaveService::WriteText(const char* slot, const String& text) const
    {
        return WriteBytes(slot, text.data(), text.size());
    }

    bool SaveService::ReadBytes(const char* slot, Array<std::byte>& out) const
    {
        out.Clear();
        const System::ISaveStorage* storage = GetSaveSystems().Storage;
        std::size_t size = 0;
        if (storage == nullptr || false == storage->GetSize(slot, size))
        {
            return false;
        }
        // 버퍼는 이 사본(게임 DLL)이 키운다. 호스트는 받은 버퍼에 쓰기만 한다.
        out.Resize(size);
        std::size_t read = 0;
        if (false == storage->Read(slot, out.Data(), out.Size(), read))
        {
            out.Clear();
            return false;
        }
        out.Resize(read);
        return true;
    }

    bool SaveService::ReadText(const char* slot, String& out) const
    {
        out.clear();
        Array<std::byte> bytes;
        if (false == ReadBytes(slot, bytes))
        {
            return false;
        }
        out.assign(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
        return true;
    }

    bool SaveService::Exists(const char* slot) const
    {
        const System::ISaveStorage* storage = GetSaveSystems().Storage;
        return storage != nullptr && storage->Exists(slot);
    }

    bool SaveService::Remove(const char* slot) const
    {
        System::ISaveStorage* storage = GetSaveSystems().Storage;
        return storage != nullptr && storage->Remove(slot);
    }

    bool SaveService::Flush() const
    {
        System::ISaveStorage* storage = GetSaveSystems().Storage;
        return storage != nullptr && storage->Flush();
    }
}
