#include <JBro/ShaderCompiler/ShaderCompiler.h>

#include <JBro/Core/Log.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <dxcapi.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace JBro
{
    namespace
    {
        using D3DReflectFunction = HRESULT(WINAPI*)(LPCVOID, SIZE_T, REFIID, void**);

        std::wstring Widen(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }
            const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring result(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
            return result;
        }

        // 머리글 하나만 준다. dxc 는 include 를 소스 파일의 폴더 기준으로 묻는다(`./Shaders/JBro/Material.hlsli`) - 끝이 머리글 이름이고
        // 그 앞이 경로 구분자(또는 처음)면 머리글이다.
        Bool IsPrelude(std::string_view name)
        {
            const std::string_view expected = MaterialPreludeName;
            if (name.size() < expected.size())
            {
                return false;
            }
            const std::size_t start = name.size() - expected.size();
            if (start != 0 && name[start - 1] != '/' && name[start - 1] != '\\')
            {
                return false;
            }
            for (std::size_t at = 0; at < expected.size(); ++at)
            {
                const char left = name[start + at] == '\\' ? '/' : name[start + at];
                if (left != expected[at])
                {
                    return false;
                }
            }
            return true;
        }

        class FxcInclude final : public ID3DInclude
        {
        public:
            HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR fileName, LPCVOID, LPCVOID* data, UINT* bytes) override
            {
                if (fileName == nullptr || false == IsPrelude(fileName))
                {
                    return E_FAIL;
                }
                const char* prelude = GetMaterialPrelude();
                *data = prelude;
                *bytes = static_cast<UINT>(std::strlen(prelude));
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE Close(LPCVOID) override
            {
                return S_OK;
            }
        };

        // dxc 의 include 손잡이다. 스택에 사는 것이라 참조 수는 세지 않는다.
        class DxcInclude final : public IDxcIncludeHandler
        {
        public:
            explicit DxcInclude(IDxcUtils* utils) : m_utils(utils)
            {
            }
            HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR fileName, IDxcBlob** source) override
            {
                *source = nullptr;
                if (fileName == nullptr)
                {
                    return E_FAIL;
                }
                const int length = WideCharToMultiByte(CP_UTF8, 0, fileName, -1, nullptr, 0, nullptr, nullptr);
                String narrow(static_cast<std::size_t>(length > 0 ? length - 1 : 0), '\0');
                WideCharToMultiByte(CP_UTF8, 0, fileName, -1, narrow.data(), length, nullptr, nullptr);
                if (false == IsPrelude(narrow))
                {
                    return E_FAIL;
                }
                const char* prelude = GetMaterialPrelude();
                IDxcBlobEncoding* blob = nullptr;
                const HRESULT result = m_utils->CreateBlob(prelude, static_cast<UINT32>(std::strlen(prelude)), DXC_CP_UTF8, &blob);
                *source = blob;
                return result;
            }
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
            {
                if (id == __uuidof(IDxcIncludeHandler) || id == __uuidof(IUnknown))
                {
                    *object = this;
                    return S_OK;
                }
                *object = nullptr;
                return E_NOINTERFACE;
            }
            ULONG STDMETHODCALLTYPE AddRef() override
            {
                return 1;
            }
            ULONG STDMETHODCALLTYPE Release() override
            {
                return 1;
            }

        private:
            IDxcUtils* m_utils = nullptr;
        };

        template <typename T>
        void ReleaseCom(T*& object)
        {
            if (object != nullptr)
            {
                object->Release();
                object = nullptr;
            }
        }

        Bool EndsWithColor(const String& name)
        {
            const std::string_view view = name.View();
            if (view.size() < 5)
            {
                return false;
            }
            const std::string_view tail = view.substr(view.size() - 5);
            return tail == "color" || tail == "Color" || tail == "COLOR";
        }

        const char* DomainDefine(MaterialDomain domain)
        {
            switch (domain)
            {
            case MaterialDomain::Sprite:
                return "JBRO_MATERIAL_SPRITE";
            case MaterialDomain::Mesh:
                return "JBRO_MATERIAL_MESH";
            case MaterialDomain::PostProcess:
                break;
            }
            return "JBRO_MATERIAL_POSTPROCESS";
        }

        // t0 자리의 입력 이름이다. 메시는 입력이 없다(빈 값).
        const char* InputName(MaterialDomain domain)
        {
            switch (domain)
            {
            case MaterialDomain::Sprite:
                return "JBroSpriteTexture";
            case MaterialDomain::Mesh:
                return nullptr;
            case MaterialDomain::PostProcess:
                break;
            }
            return "JBroSource";
        }

        void Append(String& error, const char* text)
        {
            if (false == error.empty())
            {
                error.append("\n");
            }
            error.append(text);
        }
    }

    struct ShaderCompiler::Impl
    {
        HMODULE fxcModule = nullptr;
        pD3DCompile compile = nullptr;
        D3DReflectFunction reflect = nullptr;
        HMODULE dxcModule = nullptr;
        IDxcCompiler3* dxc = nullptr;
        IDxcUtils* utils = nullptr;
        Bool spirv = false;

        ~Impl()
        {
            ReleaseCom(dxc);
            ReleaseCom(utils);
            if (dxcModule != nullptr)
            {
                FreeLibrary(dxcModule);
            }
            if (fxcModule != nullptr)
            {
                FreeLibrary(fxcModule);
            }
        }

        // dxc 로 굽는다. `spirvTarget` 이면 Vulkan 의 자리 이동(`Compile.ps1` 과 같다)을 붙인다.
        Bool CompileDxc(std::string_view source, const char* sourceName, MaterialDomain domain, Bool spirvTarget, Array<std::byte>& out,
            String& error)
        {
            if (dxc == nullptr)
            {
                return false;
            }
            const std::wstring name = Widen(sourceName != nullptr ? sourceName : "material.hlsl");
            const std::wstring define = Widen(DomainDefine(domain)) + L"=1";
            const std::wstring entry = Widen(GetMaterialEntryPoint(domain));
            const wchar_t* arguments[24] = {};
            UINT32 count = 0;
            arguments[count++] = name.c_str();
            arguments[count++] = L"-T";
            arguments[count++] = L"ps_6_0";
            arguments[count++] = L"-E";
            arguments[count++] = entry.c_str();
            arguments[count++] = L"-D";
            arguments[count++] = define.c_str();
            if (spirvTarget)
            {
                arguments[count++] = L"-spirv";
                arguments[count++] = L"-D";
                arguments[count++] = L"JBRO_SPIRV=1";
                arguments[count++] = L"-fvk-t-shift";
                arguments[count++] = L"8";
                arguments[count++] = L"0";
                arguments[count++] = L"-fvk-s-shift";
                arguments[count++] = L"16";
                arguments[count++] = L"0";
                arguments[count++] = L"-fvk-u-shift";
                arguments[count++] = L"24";
                arguments[count++] = L"0";
                // Vulkan 백엔드는 픽셀 단계를 `PSMain` 으로 부른다. 자리의 입구가 다르면(스프라이트의 `JBroSpriteMain`, D-348) SPIR-V 의 이름만 맞춘다.
                arguments[count++] = L"-fspv-entrypoint-name=PSMain";
            }
            DxcBuffer buffer = {};
            buffer.Ptr = source.data();
            buffer.Size = source.size();
            buffer.Encoding = DXC_CP_UTF8;
            DxcInclude include(utils);
            IDxcResult* result = nullptr;
            if (FAILED(dxc->Compile(&buffer, arguments, count, &include, IID_PPV_ARGS(&result))) || result == nullptr)
            {
                Append(error, "dxc could not run");
                return false;
            }
            HRESULT status = E_FAIL;
            result->GetStatus(&status);
            IDxcBlobUtf8* errors = nullptr;
            result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
            if (FAILED(status))
            {
                Append(error, errors != nullptr && errors->GetStringLength() != 0 ? errors->GetStringPointer() : "dxc failed");
                ReleaseCom(errors);
                ReleaseCom(result);
                return false;
            }
            ReleaseCom(errors);
            IDxcBlob* object = nullptr;
            result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
            const Bool good = object != nullptr && object->GetBufferSize() != 0;
            if (good)
            {
                out.Resize(object->GetBufferSize());
                std::memcpy(out.Data(), object->GetBufferPointer(), object->GetBufferSize());
            }
            ReleaseCom(object);
            ReleaseCom(result);
            return good;
        }

        Bool Reflect(const Array<std::byte>& dxbc, MaterialDomain domain, CompiledMaterialShader& out, String& error)
        {
            ID3D11ShaderReflection* reflection = nullptr;
            if (FAILED(reflect(dxbc.Data(), dxbc.Size(), __uuidof(ID3D11ShaderReflection), reinterpret_cast<void**>(&reflection))))
            {
                Append(error, "the shader could not be reflected");
                return false;
            }
            D3D11_SHADER_DESC desc = {};
            reflection->GetDesc(&desc);
            Bool good = true;
            char line[256] = {};
            for (UINT index = 0; index < desc.BoundResources && good; ++index)
            {
                D3D11_SHADER_INPUT_BIND_DESC bind = {};
                reflection->GetResourceBindingDesc(index, &bind);
                switch (bind.Type)
                {
                case D3D_SIT_CBUFFER:
                    if (std::strcmp(bind.Name, "JBroEngine") == 0 && bind.BindPoint == 0)
                    {
                        break;
                    }
                    if (std::strcmp(bind.Name, "Material") == 0 && bind.BindPoint == 1)
                    {
                        break;
                    }
                    std::snprintf(line, sizeof(line), "cbuffer %s: a material reads only cbuffer Material at b1", bind.Name);
                    Append(error, line);
                    good = false;
                    break;
                case D3D_SIT_TEXTURE:
                    if (bind.BindPoint == 0)
                    {
                        const char* input = InputName(domain);
                        if (input == nullptr)
                        {
                            std::snprintf(line, sizeof(line), "%s: a mesh material has no input at t0; its textures start at t1", bind.Name);
                            Append(error, line);
                            good = false;
                        }
                        else if (std::strcmp(bind.Name, input) != 0)
                        {
                            std::snprintf(line, sizeof(line), "%s: t0 is the domain's input (%s)", bind.Name, input);
                            Append(error, line);
                            good = false;
                        }
                        break;
                    }
                    // 스프라이트의 t7 은 엔진의 라이트맵이다(머리글이 선언하고 입구가 읽는다).
                    if (domain == MaterialDomain::Sprite && bind.BindPoint == MaterialLightMapSlot.Get()
                        && std::strcmp(bind.Name, "JBroLightMap") == 0)
                    {
                        break;
                    }
                    // 배열은 상수로 읽으면 원소마다 따로(`layers[0]`) 반사된다 - 이름으로 고를 수 없으니 받지 않는다.
                    if (bind.Dimension != D3D_SRV_DIMENSION_TEXTURE2D || bind.BindCount != 1 || bind.BindPoint > MaxMaterialTextures.Get()
                        || std::strchr(bind.Name, '[') != nullptr)
                    {
                        std::snprintf(line, sizeof(line), "%s: a material texture is one Texture2D at t1..t%u", bind.Name,
                            MaxMaterialTextures.Get());
                        Append(error, line);
                        good = false;
                        break;
                    }
                    {
                        MaterialTextureInfo texture;
                        texture.name = bind.Name;
                        texture.slot = bind.BindPoint;
                        out.textures.Add(std::move(texture));
                    }
                    break;
                case D3D_SIT_SAMPLER:
                    if (bind.BindPoint + bind.BindCount > MaterialSamplerCount.Get())
                    {
                        std::snprintf(line, sizeof(line), "%s: a material uses the samplers s0..s3 of the prelude", bind.Name);
                        Append(error, line);
                        good = false;
                    }
                    break;
                default:
                    std::snprintf(line, sizeof(line), "%s: a material reads only textures, samplers and cbuffer Material", bind.Name);
                    Append(error, line);
                    good = false;
                    break;
                }
            }
            ID3D11ShaderReflectionConstantBuffer* material = good ? reflection->GetConstantBufferByName("Material") : nullptr;
            D3D11_SHADER_BUFFER_DESC buffer = {};
            if (material != nullptr && SUCCEEDED(material->GetDesc(&buffer)))
            {
                if (buffer.Size > MaxMaterialParameterBytes.Get())
                {
                    std::snprintf(line, sizeof(line), "cbuffer Material: %u bytes is more than %u", buffer.Size, MaxMaterialParameterBytes.Get());
                    Append(error, line);
                    good = false;
                }
                out.parameterBytes = buffer.Size;
                for (UINT index = 0; index < buffer.Variables && good; ++index)
                {
                    ID3D11ShaderReflectionVariable* variable = material->GetVariableByIndex(index);
                    D3D11_SHADER_VARIABLE_DESC variableDesc = {};
                    D3D11_SHADER_TYPE_DESC typeDesc = {};
                    variable->GetDesc(&variableDesc);
                    variable->GetType()->GetDesc(&typeDesc);
                    MaterialParameterInfo parameter;
                    parameter.name = variableDesc.Name;
                    parameter.offset = variableDesc.StartOffset;
                    const Bool shape = typeDesc.Elements == 0 && typeDesc.Rows == 1
                        && (typeDesc.Class == D3D_SVC_SCALAR || typeDesc.Class == D3D_SVC_VECTOR);
                    if (shape && typeDesc.Type == D3D_SVT_FLOAT && typeDesc.Columns >= 1 && typeDesc.Columns <= 4)
                    {
                        constexpr MaterialParameterType Floats[] = {MaterialParameterType::Float, MaterialParameterType::Float2,
                            MaterialParameterType::Float3, MaterialParameterType::Float4};
                        parameter.type = Floats[typeDesc.Columns - 1];
                    }
                    else if (shape && typeDesc.Columns == 1 && typeDesc.Type == D3D_SVT_INT)
                    {
                        parameter.type = MaterialParameterType::Int;
                    }
                    else if (shape && typeDesc.Columns == 1 && typeDesc.Type == D3D_SVT_UINT)
                    {
                        parameter.type = MaterialParameterType::UInt;
                    }
                    else if (shape && typeDesc.Columns == 1 && typeDesc.Type == D3D_SVT_BOOL)
                    {
                        parameter.type = MaterialParameterType::Bool;
                    }
                    else
                    {
                        std::snprintf(line, sizeof(line), "%s: a material parameter is a float, float2..4, int, uint or bool", variableDesc.Name);
                        Append(error, line);
                        good = false;
                        break;
                    }
                    const UInt32 components = GetMaterialParameterComponents(parameter.type);
                    if (variableDesc.DefaultValue != nullptr)
                    {
                        const auto* bytes = static_cast<const std::uint8_t*>(variableDesc.DefaultValue);
                        for (UInt32 at = 0; at < components; ++at)
                        {
                            if (parameter.type == MaterialParameterType::Int)
                            {
                                std::int32_t value = 0;
                                std::memcpy(&value, bytes + at.Get() * 4, 4);
                                parameter.defaults[at.Get()] = static_cast<float>(value);
                            }
                            else if (parameter.type == MaterialParameterType::UInt || parameter.type == MaterialParameterType::Bool)
                            {
                                std::uint32_t value = 0;
                                std::memcpy(&value, bytes + at.Get() * 4, 4);
                                parameter.defaults[at.Get()] = static_cast<float>(value);
                            }
                            else
                            {
                                float value = 0.0f;
                                std::memcpy(&value, bytes + at.Get() * 4, 4);
                                parameter.defaults[at.Get()] = value;
                            }
                        }
                    }
                    parameter.color = components >= 3 && EndsWithColor(parameter.name);
                    out.parameters.Add(std::move(parameter));
                }
            }
            reflection->Release();
            return good;
        }
    };

    ShaderCompiler::~ShaderCompiler()
    {
        Shutdown();
    }

    Bool ShaderCompiler::Initialize(std::string_view executableFolder)
    {
        Shutdown();
        m_impl = MakeOwnerPtr<Impl>();
        Impl& impl = *m_impl;
        impl.fxcModule = LoadLibraryW(L"d3dcompiler_47.dll");
        if (impl.fxcModule != nullptr)
        {
            impl.compile = reinterpret_cast<pD3DCompile>(GetProcAddress(impl.fxcModule, "D3DCompile"));
            impl.reflect = reinterpret_cast<D3DReflectFunction>(GetProcAddress(impl.fxcModule, "D3DReflect"));
        }
        if (impl.compile == nullptr || impl.reflect == nullptr)
        {
            Log::Write(LogLevel::Warning, "shader", "d3dcompiler_47.dll could not be opened: material shaders cannot be compiled");
            m_impl = nullptr;
            return false;
        }
        // 실행 파일 옆의 것을 먼저 - 빌드가 Vulkan SDK 의 것을 옮겨 둔다. 없으면 시스템 검색 경로다.
        if (false == executableFolder.empty())
        {
            std::wstring path = Widen(executableFolder);
            path += L"\\dxcompiler.dll";
            impl.dxcModule = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        }
        if (impl.dxcModule == nullptr)
        {
            impl.dxcModule = LoadLibraryW(L"dxcompiler.dll");
        }
        const auto create = impl.dxcModule != nullptr
            ? reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(impl.dxcModule, "DxcCreateInstance"))
            : nullptr;
        if (create == nullptr || FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&impl.dxc)))
            || FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&impl.utils))))
        {
            ReleaseCom(impl.dxc);
            ReleaseCom(impl.utils);
            Log::Write(LogLevel::Warning, "shader", "dxcompiler.dll could not be opened: material shaders have no D3D12 or Vulkan code");
            return true;
        }
        // SPIR-V 를 굽는지 한 번 재 본다 - Windows SDK 의 dxc 는 SPIR-V 를 모른다.
        Array<std::byte> probe;
        String ignored;
        const char* source = "#include \"JBro/Material.hlsli\"\nfloat4 PSMain(JBroPostProcessInput input) : SV_TARGET { return JBroLoadSource(input); }\n";
        impl.spirv = impl.CompileDxc(source, "probe.hlsl", MaterialDomain::PostProcess, true, probe, ignored);
        if (false == impl.spirv)
        {
            Log::Write(LogLevel::Info, "shader", "this dxcompiler.dll cannot compile SPIR-V: material shaders have no Vulkan code");
        }
        return true;
    }

    void ShaderCompiler::Shutdown()
    {
        m_impl = nullptr;
    }

    Bool ShaderCompiler::HasFormat(ShaderCodeFormat format) const
    {
        if (m_impl.Get() == nullptr)
        {
            return false;
        }
        switch (format)
        {
        case ShaderCodeFormat::Dxbc:
            return true;
        case ShaderCodeFormat::Dxil:
            return m_impl->dxc != nullptr;
        case ShaderCodeFormat::SpirV:
            return m_impl->spirv;
        }
        return false;
    }

    Bool ShaderCompiler::Compile(std::string_view source, const char* sourceName, MaterialDomain domain, CompiledMaterialShader& out,
        String& error)
    {
        out = {};
        out.domain = domain;
        error.clear();
        if (m_impl.Get() == nullptr)
        {
            error = "the shader compiler is not open";
            return false;
        }
        Impl& impl = *m_impl;
        // 자리의 맺음말(스프라이트의 입구)을 사용자 글자 뒤에 붙인다. 맺음말은 제 `#line` 을 달아 사용자 줄 번호가 그대로다.
        String full(source);
        full.append(GetMaterialEpilogue(domain));
        source = full.View();
        const D3D_SHADER_MACRO defines[] = {{DomainDefine(domain), "1"}, {nullptr, nullptr}};
        FxcInclude include;
        ID3DBlob* code = nullptr;
        ID3DBlob* messages = nullptr;
        const HRESULT result = impl.compile(source.data(), source.size(), sourceName, defines, &include, GetMaterialEntryPoint(domain),
            "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &messages);
        if (FAILED(result) || code == nullptr)
        {
            if (messages != nullptr && messages->GetBufferSize() != 0)
            {
                error.assign(static_cast<const char*>(messages->GetBufferPointer()), messages->GetBufferSize());
                while (false == error.empty() && (error.back() == '\0' || error.back() == '\n'))
                {
                    error.pop_back();
                }
                // fxc 는 상대 이름 앞에 작업 폴더를 붙인다 - 그 폴더는 에셋 폴더가 아니라 엉뚱한 곳을 가리킨다. 떼어 에셋 경로만 남긴다.
                char folder[MAX_PATH] = {};
                const DWORD length = GetCurrentDirectoryA(MAX_PATH, folder);
                if (length != 0 && length < MAX_PATH)
                {
                    String prefix(folder, length);
                    prefix.append("\\");
                    error.ReplaceAll(prefix.View(), "");
                }
            }
            else
            {
                error = "fxc failed";
            }
            ReleaseCom(code);
            ReleaseCom(messages);
            return false;
        }
        ReleaseCom(messages);
        Array<std::byte>& dxbc = out.pixel[static_cast<std::size_t>(ShaderCodeFormat::Dxbc)];
        dxbc.Resize(code->GetBufferSize());
        std::memcpy(dxbc.Data(), code->GetBufferPointer(), code->GetBufferSize());
        ReleaseCom(code);
        if (false == impl.Reflect(dxbc, domain, out, error))
        {
            out.pixel[static_cast<std::size_t>(ShaderCodeFormat::Dxbc)].Clear();
            return false;
        }
        if (impl.dxc != nullptr
            && false == impl.CompileDxc(source, sourceName, domain, false, out.pixel[static_cast<std::size_t>(ShaderCodeFormat::Dxil)], error))
        {
            return false;
        }
        if (impl.spirv
            && false == impl.CompileDxc(source, sourceName, domain, true, out.pixel[static_cast<std::size_t>(ShaderCodeFormat::SpirV)], error))
        {
            return false;
        }
        return true;
    }
}
