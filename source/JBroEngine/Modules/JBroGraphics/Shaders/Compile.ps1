# Bakes the built-in shaders listed in ShaderManifest.psd1 (D-329).
# The build does not run this: the blobs are committed so a clone builds
# without a shader compiler. Run it by hand after editing a .hlsl or the
# manifest, then commit the regenerated headers together with the change.
#
#   pwsh Modules\JBroGraphics\Shaders\Compile.ps1
#   pwsh Modules\JBroGraphics\Shaders\Compile.ps1 -SkipSpirv   # no Vulkan SDK here: the *_SPV headers stay as they are
#
# For every stage in the manifest it writes <Symbol>.generated.h (DXIL), <Symbol>_SM5.generated.h (DXBC) and
# <Symbol>_SPV.generated.h (SPIR-V) next to Source/, then the program table: Include/JBro/Graphics/BuiltinShaders.generated.h
# (public, so tests can check it against reflection) and Source/BuiltinShaders.generated.cpp.
# The table only names the stage arrays, so it can be regenerated without any compiler.
#
# Pinned to the same Windows SDK as JBro.Common.props so the committed
# bytes do not drift with whatever SDK happens to be installed.
param(
    [switch]$SkipSpirv
)
$ErrorActionPreference = 'Stop'

$sdk = '10.0.22621.0'
$dxc = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\$sdk\x64\dxc.exe"
if (-not (Test-Path $dxc))
{
    throw "dxc.exe for Windows SDK $sdk not found at $dxc"
}

# D3D11 cannot read DXIL: the same HLSL also goes through fxc to SM 5.0 DXBC (D-107).
# fxc refuses a UTF-8 BOM ('Illegal character'), so .hlsl files are saved without one (the exception to the BOM rule).
$fxc = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\$sdk\x64\fxc.exe"
if (-not (Test-Path $fxc))
{
    throw "fxc.exe for Windows SDK $sdk not found at $fxc"
}

# Vulkan reads SPIR-V. The Windows SDK dxc is built without SPIR-V codegen, so the Vulkan SDK's
# dxc does this one (D-108). JBRO_SPIRV selects the [[vk::push_constant]] spelling of b0, and the
# register shifts keep t# and s# clear of the push block: textures at binding 8+, samplers at 16+,
# storage (u#) at 24+ (D-328). b1..b7 constant buffers stay at binding 1..7.
# Missing it stops the run: unnoticed, the .hlsl and its SPIR-V would be committed out of step. -SkipSpirv says so on purpose.
$vkdxc = "${env:VULKAN_SDK}\Bin\dxc.exe"
if (-not $SkipSpirv -and -not (Test-Path $vkdxc))
{
    throw "dxc.exe of the Vulkan SDK not found at $vkdxc (is VULKAN_SDK set? pass -SkipSpirv to leave the SPIR-V headers alone)"
}

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$out = Join-Path (Split-Path -Parent $here) 'Source'
$manifest = Import-PowerShellDataFile (Join-Path $here 'ShaderManifest.psd1')

$profiles = @{ Vertex = 'vs'; Pixel = 'ps'; Compute = 'cs' }
$stageBits = @{ Vertex = 1; Pixel = 2; Compute = 4 }

foreach ($s in $manifest.Stages)
{
    if (-not $profiles.ContainsKey($s.Stage))
    {
        throw "unknown stage '$($s.Stage)' for $($s.Symbol)"
    }
    $source = Join-Path $here $s.File
    $kind = $profiles[$s.Stage]

    $header = Join-Path $out "$($s.Symbol).generated.h"
    & $dxc -T "${kind}_6_0" -E $s.Entry -Vn "JBro$($s.Symbol)" -Fh $header $source
    if ($LASTEXITCODE -ne 0)
    {
        throw "dxc failed for $($s.Entry) in $($s.File)"
    }

    $header = Join-Path $out "$($s.Symbol)_SM5.generated.h"
    & $fxc /nologo /T "${kind}_5_0" /E $s.Entry /Vn "JBro$($s.Symbol)_SM5" /Fh $header $source | Out-Null
    if ($LASTEXITCODE -ne 0)
    {
        throw "fxc failed for $($s.Entry) in $($s.File)"
    }

    if (-not $SkipSpirv)
    {
        # The Vulkan backend asks every module for VSMain, PSMain or CSMain (VulkanPipeline.cpp). Another entry in the
        # same file (PSLitMain, D-291) is renamed in its own module; D3D bytecode carries no name.
        $entryName = @{ Vertex = 'VSMain'; Pixel = 'PSMain'; Compute = 'CSMain' }[$s.Stage]
        $header = Join-Path $out "$($s.Symbol)_SPV.generated.h"
        & $vkdxc -spirv -T "${kind}_6_0" -E $s.Entry "-fspv-entrypoint-name=$entryName" -D JBRO_SPIRV=1 -fvk-t-shift 8 0 -fvk-s-shift 16 0 -fvk-u-shift 24 0 -Vn "JBro$($s.Symbol)_SPV" -Fh $header $source
        if ($LASTEXITCODE -ne 0)
        {
            throw "dxc -spirv failed for $($s.Entry) in $($s.File)"
        }
    }
    "$($s.Symbol) <- $($s.File) ($($s.Stage) $($s.Entry))"
}

# ---- program table ----

$symbols = @{}
foreach ($s in $manifest.Stages)
{
    $symbols[$s.Symbol] = $s.Stage
}

function Get-Mask([string]$stages)
{
    $mask = 0
    foreach ($name in ($stages -split '\|'))
    {
        $name = $name.Trim()
        if (-not $stageBits.ContainsKey($name))
        {
            throw "unknown stage '$name'"
        }
        $mask = $mask -bor $stageBits[$name]
    }
    return $mask
}

function Get-Range($program, [string]$countKey, [string]$stagesKey, [string]$defaultStages)
{
    $count = if ($program.ContainsKey($countKey)) { [int]$program[$countKey] } else { 0 }
    if ($count -eq 0)
    {
        return '{0, 0}'
    }
    $stages = if ($program.ContainsKey($stagesKey)) { $program[$stagesKey] } else { $defaultStages }
    return "{$count, $(Get-Mask $stages)}"
}

function Get-StageCode($program, [string]$stage)
{
    if (-not $program.ContainsKey($stage))
    {
        return '{}'
    }
    $symbol = $program[$stage]
    if ($symbols[$symbol] -ne $stage)
    {
        throw "program $($program.Name): $symbol is not a $stage stage"
    }
    return "{View(JBro$symbol, sizeof(JBro$symbol)), View(Sm5::JBro${symbol}_SM5, sizeof(Sm5::JBro${symbol}_SM5)), View(Spv::JBro${symbol}_SPV, sizeof(Spv::JBro${symbol}_SPV))}"
}

$header = New-Object System.Text.StringBuilder
[void]$header.AppendLine('// Generated by Shaders/Compile.ps1 from Shaders/ShaderManifest.psd1. Do not edit.')
[void]$header.AppendLine('#pragma once')
[void]$header.AppendLine('')
[void]$header.AppendLine('#include <JBro/Asset/ShaderData.h>')
[void]$header.AppendLine('')
[void]$header.AppendLine('#include <cstdint>')
[void]$header.AppendLine('')
[void]$header.AppendLine('namespace JBro')
[void]$header.AppendLine('{')
[void]$header.AppendLine('    // The built-in shader programs, in the order of the manifest''s Programs (D-329).')
[void]$header.AppendLine('    enum class BuiltinShader : std::uint8_t')
[void]$header.AppendLine('    {')
foreach ($p in $manifest.Programs)
{
    [void]$header.AppendLine("        $($p.Name),")
}
[void]$header.AppendLine('        Count')
[void]$header.AppendLine('    };')
[void]$header.AppendLine('')
[void]$header.AppendLine('    const ShaderData& GetBuiltinShader(BuiltinShader shader);')
[void]$header.AppendLine('}')

$source = New-Object System.Text.StringBuilder
[void]$source.AppendLine('// Generated by Shaders/Compile.ps1 from Shaders/ShaderManifest.psd1. Do not edit.')
[void]$source.AppendLine('#include <JBro/Graphics/BuiltinShaders.generated.h>')
[void]$source.AppendLine('')
[void]$source.AppendLine('#include <cstddef>')
[void]$source.AppendLine('')
foreach ($s in $manifest.Stages)
{
    [void]$source.AppendLine("#include `"$($s.Symbol).generated.h`"")
}
[void]$source.AppendLine('')
[void]$source.AppendLine('// fxc names its bytes BYTE. Give it the name without pulling in Windows.h.')
[void]$source.AppendLine('namespace JBro::BuiltinShaderSm5')
[void]$source.AppendLine('{')
[void]$source.AppendLine('    using BYTE = unsigned char;')
foreach ($s in $manifest.Stages)
{
    [void]$source.AppendLine("#include `"$($s.Symbol)_SM5.generated.h`"")
}
[void]$source.AppendLine('}')
[void]$source.AppendLine('')
[void]$source.AppendLine('namespace JBro::BuiltinShaderSpv')
[void]$source.AppendLine('{')
foreach ($s in $manifest.Stages)
{
    [void]$source.AppendLine("#include `"$($s.Symbol)_SPV.generated.h`"")
}
[void]$source.AppendLine('}')
[void]$source.AppendLine('')
[void]$source.AppendLine('namespace JBro')
[void]$source.AppendLine('{')
[void]$source.AppendLine('    namespace')
[void]$source.AppendLine('    {')
[void]$source.AppendLine('        namespace Sm5 = BuiltinShaderSm5;')
[void]$source.AppendLine('        namespace Spv = BuiltinShaderSpv;')
[void]$source.AppendLine('')
[void]$source.AppendLine('        JArrayView<std::byte> View(const unsigned char* data, std::size_t size)')
[void]$source.AppendLine('        {')
[void]$source.AppendLine('            return {reinterpret_cast<const std::byte*>(data), static_cast<UInt32>(size)};')
[void]$source.AppendLine('        }')
[void]$source.AppendLine('')
[void]$source.AppendLine('        // [stage][format]: Vertex, Pixel, Compute x DXIL, DXBC, SPIR-V.')
[void]$source.AppendLine('        const ShaderData Programs[] = {')
foreach ($p in $manifest.Programs)
{
    $push = if ($p.ContainsKey('PushConstantBytes')) { [int]$p.PushConstantBytes } else { 0 }
    $pushMask = if ($push -ne 0) { Get-Mask $p.PushConstantStages } else { 0 }
    $vertex = Get-StageCode $p 'Vertex'
    $pixel = Get-StageCode $p 'Pixel'
    $compute = Get-StageCode $p 'Compute'
    $constants = Get-Range $p 'ConstantBuffers' 'ConstantBufferStages' 'Vertex|Pixel'
    $textures = Get-Range $p 'SampledTextures' 'SampledTextureStages' 'Pixel'
    $samplers = Get-Range $p 'Samplers' 'SamplerStages' 'Pixel'
    $storageTextures = Get-Range $p 'StorageTextures' 'StorageTextureStages' 'Compute'
    $storageBuffers = Get-Range $p 'StorageBuffers' 'StorageBufferStages' 'Compute'
    [void]$source.AppendLine("            {`"$($p.Name)`",")
    [void]$source.AppendLine("                {$vertex,")
    [void]$source.AppendLine("                    $pixel,")
    [void]$source.AppendLine("                    $compute},")
    [void]$source.AppendLine("                $push, $pushMask, $constants, $textures, $samplers, $storageTextures, $storageBuffers},")
}
[void]$source.AppendLine('        };')
[void]$source.AppendLine('')
[void]$source.AppendLine('        static_assert(sizeof(Programs) / sizeof(Programs[0]) == static_cast<std::size_t>(BuiltinShader::Count),')
[void]$source.AppendLine('            "the table and the enum come from the same manifest");')
[void]$source.AppendLine('    }')
[void]$source.AppendLine('')
[void]$source.AppendLine('    const ShaderData& GetBuiltinShader(BuiltinShader shader)')
[void]$source.AppendLine('    {')
[void]$source.AppendLine('        return Programs[static_cast<std::uint8_t>(shader)];')
[void]$source.AppendLine('    }')
[void]$source.AppendLine('}')

$publicHeader = Join-Path (Split-Path -Parent $here) 'Include\JBro\Graphics\BuiltinShaders.generated.h'
[System.IO.File]::WriteAllText($publicHeader, $header.ToString())
[System.IO.File]::WriteAllText((Join-Path $out 'BuiltinShaders.generated.cpp'), $source.ToString())
"BuiltinShaders.generated.h/.cpp <- ShaderManifest.psd1 ($($manifest.Programs.Count) programs, $($manifest.Stages.Count) stages)"
