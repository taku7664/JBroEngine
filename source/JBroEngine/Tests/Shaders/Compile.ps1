# 테스트용 DXIL 헤더를 다시 만든다.
# 빌드는 이것을 돌리지 않는다 — 결과를 커밋해 두므로 클론 하나로 빌드된다.
# `.hlsl` 을 고친 뒤 손으로 돌리고, 생성된 헤더를 셰이더와 함께 커밋한다.
#
#   pwsh Tests\Shaders\Compile.ps1
#
# Modules/JBroGraphics/Shaders/Compile.ps1 과 같은 방식이고 같은 SDK 에 고정한다.
$ErrorActionPreference = 'Stop'

$sdk = '10.0.22621.0'
$dxc = "${env:ProgramFiles(x86)}\Windows Kits\10\bin\$sdk\x64\dxc.exe"
if (-not (Test-Path $dxc))
{
    throw "dxc.exe for Windows SDK $sdk not found at $dxc"
}

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$out = Split-Path -Parent $here

$targets = @(
    @{ File = 'TexturedQuad.hlsl'; Entry = 'VSMain'; Profile = 'vs_6_0'; Name = 'JBroTestTexturedQuadVS'; Header = 'TexturedQuadVS.generated.h' },
    @{ File = 'TexturedQuad.hlsl'; Entry = 'PSMain'; Profile = 'ps_6_0'; Name = 'JBroTestTexturedQuadPS'; Header = 'TexturedQuadPS.generated.h' }
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
