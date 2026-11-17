#include <JBro/Asset/MaterialFile.h>

#include <JBro/Core/Yaml.h>
#include <JBro/Reflection/ReflectedYaml.h>
#include <JBro/Types/Int.h>

namespace JBro
{
    Bool ParseMaterialFile(const char* text, std::size_t length, MaterialDesc& result, String& error)
    {
        YamlDocument document;
        YamlError yamlError;
        if (false == document.Parse(text, length, yamlError))
        {
            error = yamlError.message;
            return false;
        }
        const UInt32 root = document.GetRoot();
        if (root == YamlDocument::InvalidNode || document.GetKind(root) != YamlKind::Map)
        {
            error = "a material file is a map";
            return false;
        }
        Int64 version = 1;
        if (document.Find(root, "Version") != YamlDocument::InvalidNode && false == document.FindInt(root, "Version", version))
        {
            error = "Version must be a whole number";
            return false;
        }
        if (version != 1)
        {
            error = "this material file version is not one this engine reads";
            return false;
        }
        const UInt32 block = document.Find(root, "Material");
        if (block == YamlDocument::InvalidNode)
        {
            error = "a material file needs a Material block";
            return false;
        }
        // 빈 값에서 시작해 파일의 키를 덮는다 - 적히지 않은 키는 기본값이다(새 칸이 늘어도 옛 파일이 읽힌다). 모르는 키는 실패다.
        MaterialDesc parsed;
        ReflectedYamlError reflected;
        if (false == ReadReflectedValue(document, block, TypeDescriptorOf<MaterialDesc>::Get(), &parsed, reflected))
        {
            error = "the Material block could not be read";
            if (false == reflected.fieldName.empty())
            {
                error += " at ";
                error += reflected.fieldName;
            }
            if (false == reflected.message.empty())
            {
                error += ": ";
                error += reflected.message;
            }
            return false;
        }
        result = parsed;
        return true;
    }

    Bool FormatMaterialFile(const MaterialDesc& desc, String& text)
    {
        YamlWriter writer;
        writer.WriteInt("Version", 1);
        ReflectedYamlError error;
        if (false == WriteReflectedValue(writer, "Material", TypeDescriptorOf<MaterialDesc>::Get(), &desc, error))
        {
            return false;
        }
        text = writer.GetText();
        return true;
    }
}
