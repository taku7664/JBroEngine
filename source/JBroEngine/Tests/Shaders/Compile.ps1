# 테스트용 셰이더 헤더를 다시 만든다.
# 빌드는 이것을 돌리지 않는다 — 결과를 커밋해 두므로 클론 하나로 빌드된다.
# `.hlsl` 을 고친 뒤 손으로 돌리고, 생성된 헤더를 셰이더와 함께 커밋한다.
#
#   pwsh Tests\Shaders\Compile.ps1
#   pwsh Tests\Shaders\Compile.ps1 -SkipSpirv   # Vulkan SDK 가 없는 기계. SPIR-V 헤더는 그대로 둔다
#
# Modules/JBroGraphics/Shaders/Compile.ps1 과 같은 방식이고 같은 SDK 에 고정한다.
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

# D3D11 은 DXIL 을 읽지 못한다. 같은 HLSL 을 fxc 로 SM 5.0 DXBC 로도 굽는다(D-107).
$fxc = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\$sdk\x64\fxc.exe"
if (-not (Test-Path $fxc))
{
    throw "fxc.exe for Windows SDK $sdk not found at $fxc"
}

# Vulkan 은 SPIR-V 를 읽는다(D-108). Windows SDK 의 dxc 는 SPIR-V 를 못 굽는다 - Vulkan SDK 의 것이 한다.
# 없으면 멈춘다. 모르고 지나가면 .hlsl 과 SPIR-V 헤더가 어긋난 채 커밋된다 - 일부러 건너뛸 때만 -SkipSpirv 를 준다.
$vkdxc = "${env:VULKAN_SDK}\Bin\dxc.exe"
if (-not $SkipSpirv -and -not (Test-Path $vkdxc))
{
    throw "dxc.exe of the Vulkan SDK not found at $vkdxc (is VULKAN_SDK set? pass -SkipSpirv to leave the SPIR-V headers alone)"
}

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$out = Split-Path -Parent $here

$targets = @(
    @{ File = 'TexturedQuad.hlsl'; Entry = 'VSMain'; Profile = 'vs_6_0'; Name = 'JBroTestTexturedQuadVS'; Header = 'TexturedQuadVS.generated.h' },
    @{ File = 'TexturedQuad.hlsl'; Entry = 'PSMain'; Profile = 'ps_6_0'; Name = 'JBroTestTexturedQuadPS'; Header = 'TexturedQuadPS.generated.h' },
    @{ File = 'ConstantProbe.hlsl'; Entry = 'VSMain'; Profile = 'vs_6_0'; Name = 'JBroTestConstantProbeVS'; Header = 'ConstantProbeVS.generated.h' },
    @{ File = 'ConstantProbe.hlsl'; Entry = 'PSMain'; Profile = 'ps_6_0'; Name = 'JBroTestConstantProbePS'; Header = 'ConstantProbePS.generated.h' },
    @{ File = 'SubresourceProbe.hlsl'; Entry = 'VSMain'; Profile = 'vs_6_0'; Name = 'JBroTestSubresourceProbeVS'; Header = 'SubresourceProbeVS.generated.h' },
    @{ File = 'SubresourceProbe.hlsl'; Entry = 'PSTint'; Profile = 'ps_6_0'; Name = 'JBroTestSubresourceProbeTintPS'; Header = 'SubresourceProbeTintPS.generated.h' },
    @{ File = 'SubresourceProbe.hlsl'; Entry = 'PSSample'; Profile = 'ps_6_0'; Name = 'JBroTestSubresourceProbeSamplePS'; Header = 'SubresourceProbeSamplePS.generated.h' },
    @{ File = 'SubresourceProbe.hlsl'; Entry = 'PSDepth'; Profile = 'ps_6_0'; Name = 'JBroTestSubresourceProbeDepthPS'; Header = 'SubresourceProbeDepthPS.generated.h' }
)

$sm5Targets = @(
    @{ File = 'ConstantProbe.hlsl'; Entry = 'VSMain'; Profile = 'vs_5_0'; Name = 'JBroTestConstantProbeVS_SM5'; Header = 'ConstantProbeVS_SM5.generated.h' },
    @{ File = 'ConstantProbe.hlsl'; Entry = 'PSMain'; Profile = 'ps_5_0'; Name = 'JBroTestConstantProbePS_SM5'; Header = 'ConstantProbePS_SM5.generated.h' },
    @{ File = 'SubresourceProbe.hlsl'; Entry = 'VSMain'; Profile = 'vs_5_0'; Name = 'JBroTestSubresourceProbeVS_SM5'; Header = 'SubresourceProbeVS_SM5.generated.h' },
    @{ File = 'SubresourceProbe.hlsl'; Entry = 'PSTint'; Profile = 'ps_5_0'; Name = 'JBroTestSubresourceProbeTintPS_SM5'; Header = 'SubresourceProbeTintPS_SM5.generated.h' },
    @{ File = 'SubresourceProbe.hlsl'; Entry = 'PSSample'; Profile = 'ps_5_0'; Name = 'JBroTestSubresourceProbeSamplePS_SM5'; Header = 'SubresourceProbeSamplePS_SM5.generated.h' },
    @{ File = 'SubresourceProbe.hlsl'; Entry = 'PSDepth'; Profile = 'ps_5_0'; Name = 'JBroTestSubresourceProbeDepthPS_SM5'; Header = 'SubresourceProbeDepthPS_SM5.generated.h' }
)

$spirvTargets = @(
    @{ File = 'TexturedQuad.hlsl'; Entry = 'VSMain'; Profile = 'vs_6_0'; Name = 'JBroTestTexturedQuadVS_SPV'; Header = 'TexturedQuadVS_SPV.generated.h' },
    @{ File = 'TexturedQuad.hlsl'; Entry = 'PSMain'; Profile = 'ps_6_0'; Name = 'JBroTestTexturedQuadPS_SPV'; Header = 'TexturedQuadPS_SPV.generated.h' },
    @{ File = 'ConstantProbe.hlsl'; Entry = 'VSMain'; Profile = 'vs_6_0'; Name = 'JBroTestConstantProbeVS_SPV'; Header = 'ConstantProbeVS_SPV.generated.h' },
    @{ File = 'ConstantProbe.hlsl'; Entry = 'PSMain'; Profile = 'ps_6_0'; Name = 'JBroTestConstantProbePS_SPV'; Header = 'ConstantProbePS_SPV.generated.h' },
    @{ File = 'SubresourceProbe.hlsl'; Entry = 'VSMain'; Profile = 'vs_6_0'; Name = 'JBroTestSubresourceProbeVS_SPV'; Header = 'SubresourceProbeVS_SPV.generated.h' },
    @{ File = 'SubresourceProbe.hlsl'; Entry = 'PSTint'; Profile = 'ps_6_0'; Name = 'JBroTestSubresourceProbeTintPS_SPV'; Header = 'SubresourceProbeTintPS_SPV.generated.h' },
    @{ File = 'SubresourceProbe.hlsl'; Entry = 'PSSample'; Profile = 'ps_6_0'; Name = 'JBroTestSubresourceProbeSamplePS_SPV'; Header = 'SubresourceProbeSamplePS_SPV.generated.h' },
    @{ File = 'SubresourceProbe.hlsl'; Entry = 'PSDepth'; Profile = 'ps_6_0'; Name = 'JBroTestSubresourceProbeDepthPS_SPV'; Header = 'SubresourceProbeDepthPS_SPV.generated.h' }
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

if ($SkipSpirv)
{
    'SPIR-V skipped (-SkipSpirv): the *_SPV.generated.h headers were not touched'
    return
}

# 상수 버퍼(b#)는 옮기지 않는다 - b1..b7 이 그대로 binding 1..7 이다. b0 은 푸시 상수라 binding 이 없다.
foreach ($t in $spirvTargets)
{
    $source = Join-Path $here $t.File
    $header = Join-Path $out $t.Header
    # Vulkan 백엔드는 모듈마다 진입점 이름 VSMain·PSMain 을 찾는다(VulkanPipeline.cpp). 다른 이름의 픽셀 진입점은 PSMain 으로 바꿔 굽는다 -
    # D3D 바이트코드는 이름을 싣지 않는다(엔진의 Compile.ps1 과 같다).
    $entryName = if ($t.Profile.StartsWith('vs')) { 'VSMain' } else { 'PSMain' }
    & $vkdxc -spirv -T $t.Profile -E $t.Entry "-fspv-entrypoint-name=$entryName" -D JBRO_SPIRV=1 -fvk-t-shift 8 0 -fvk-s-shift 16 0 -Vn $t.Name -Fh $header $source
    if ($LASTEXITCODE -ne 0)
    {
        throw "dxc -spirv failed for $($t.Entry) in $($t.File)"
    }
    "$($t.Header) <- $($t.File) (spirv $($t.Profile) $($t.Entry))"
}
