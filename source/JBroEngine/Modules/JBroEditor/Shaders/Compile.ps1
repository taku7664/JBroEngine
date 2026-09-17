# Regenerates the checked-in DXIL headers next to Source/.
# The build does not run this: the blobs are committed so a clone builds
# without a shader compiler. Run it by hand after editing a .hlsl, then
# commit the regenerated headers together with the shader.
#
#   pwsh Modules\JBroEditor\Shaders\Compile.ps1
#
# Pinned to the same Windows SDK as JBro.Common.props so the committed
# bytes do not drift with whatever SDK happens to be installed.
$ErrorActionPreference = 'Stop'

$sdk = '10.0.22621.0'
$dxc = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\$sdk\x64\dxc.exe"
if (-not (Test-Path $dxc))
{
    throw "dxc.exe for Windows SDK $sdk not found at $dxc"
}

# D3D11 은 DXIL 을 읽지 못한다. 같은 HLSL 을 fxc 로 SM 5.0 DXBC 로도 굽는다(D-107).
# fxc 는 UTF-8 BOM 을 'Illegal character' 로 거절한다. .hlsl 은 BOM 없이 저장한다 - 한글 주석 파일의 BOM 규칙(§14)의 예외다.
$fxc = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\$sdk\x64\fxc.exe"
if (-not (Test-Path $fxc))
{
    throw "fxc.exe for Windows SDK $sdk not found at $fxc"
}

# Vulkan reads SPIR-V. The Windows SDK dxc is built without SPIR-V codegen, so the Vulkan SDK's
# dxc does this one (D-108). JBRO_SPIRV selects the [[vk::push_constant]] spelling of b0, and the
# register shifts keep t# and s# clear of the push block: textures at binding 8+, samplers at 16+.
$vkdxc = "${env:VULKAN_SDK}\Bin\dxc.exe"
if (-not (Test-Path $vkdxc))
{
    throw "dxc.exe of the Vulkan SDK not found at $vkdxc (is VULKAN_SDK set?)"
}

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$out = Join-Path (Split-Path -Parent $here) 'Source'

$targets = @(
    @{ File = 'EditorUI.hlsl'; Entry = 'VSMain'; Profile = 'vs_6_0'; Name = 'JBroEditorUIVS'; Header = 'EditorUIVS.generated.h' },
    @{ File = 'EditorUI.hlsl'; Entry = 'PSMain'; Profile = 'ps_6_0'; Name = 'JBroEditorUIPS'; Header = 'EditorUIPS.generated.h' }
)

$sm5Targets = @(
    @{ File = 'EditorUI.hlsl'; Entry = 'VSMain'; Profile = 'vs_5_0'; Name = 'JBroEditorUIVS_SM5'; Header = 'EditorUIVS_SM5.generated.h' },
    @{ File = 'EditorUI.hlsl'; Entry = 'PSMain'; Profile = 'ps_5_0'; Name = 'JBroEditorUIPS_SM5'; Header = 'EditorUIPS_SM5.generated.h' }
)

$spirvTargets = @(
    @{ File = 'EditorUI.hlsl'; Entry = 'VSMain'; Profile = 'vs_6_0'; Name = 'JBroEditorUIVS_SPV'; Header = 'EditorUIVS_SPV.generated.h' },
    @{ File = 'EditorUI.hlsl'; Entry = 'PSMain'; Profile = 'ps_6_0'; Name = 'JBroEditorUIPS_SPV'; Header = 'EditorUIPS_SPV.generated.h' }
)

foreach ($t in $targets)
{
    $source = Join-Path $here $t.File
    $header = Join-Path $out $t.Header
    & $dxc -T $t.Profile -E $t.Entry -Vn $t.Name -Fh $header $source
    if ($LASTEXITCODE -ne 0)
    {
        throw "dxc failed for $($t.Entry) in $($t.File)"
    }
    "$($t.Header) <- $($t.File) ($($t.Profile) $($t.Entry))"
}

foreach ($t in $sm5Targets)
{
    $source = Join-Path $here $t.File
    $header = Join-Path $out $t.Header
    & $fxc /nologo /T $t.Profile /E $t.Entry /Vn $t.Name /Fh $header $source
    if ($LASTEXITCODE -ne 0)
    {
        throw "fxc failed for $($t.Entry) in $($t.File)"
    }
    "$($t.Header) <- $($t.File) ($($t.Profile) $($t.Entry))"
}

foreach ($t in $spirvTargets)
{
    $source = Join-Path $here $t.File
    $header = Join-Path $out $t.Header
    & $vkdxc -spirv -T $t.Profile -E $t.Entry -D JBRO_SPIRV=1 -fvk-t-shift 8 0 -fvk-s-shift 16 0 -Vn $t.Name -Fh $header $source
    if ($LASTEXITCODE -ne 0)
    {
        throw "dxc -spirv failed for $($t.Entry) in $($t.File)"
    }
    "$($t.Header) <- $($t.File) (spirv $($t.Profile) $($t.Entry))"
}
