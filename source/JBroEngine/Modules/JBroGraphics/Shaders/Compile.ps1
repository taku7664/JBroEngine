# Regenerates the checked-in DXIL headers next to Source/.
# The build does not run this: the blobs are committed so a clone builds
# without a shader compiler. Run it by hand after editing a .hlsl, then
# commit the regenerated headers together with the shader.
#
#   pwsh Modules\JBroGraphics\Shaders\Compile.ps1
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

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$out = Join-Path (Split-Path -Parent $here) 'Source'

$targets = @(
    @{ File = 'BuiltinSprite.hlsl'; Entry = 'VSMain'; Profile = 'vs_6_0'; Name = 'JBroBuiltinSpriteVS'; Header = 'BuiltinSpriteVS.generated.h' },
    @{ File = 'BuiltinSprite.hlsl'; Entry = 'PSMain'; Profile = 'ps_6_0'; Name = 'JBroBuiltinSpritePS'; Header = 'BuiltinSpritePS.generated.h' },
    @{ File = 'BuiltinMesh.hlsl'; Entry = 'VSMain'; Profile = 'vs_6_0'; Name = 'JBroBuiltinMeshVS'; Header = 'BuiltinMeshVS.generated.h' },
    @{ File = 'BuiltinMesh.hlsl'; Entry = 'PSMain'; Profile = 'ps_6_0'; Name = 'JBroBuiltinMeshPS'; Header = 'BuiltinMeshPS.generated.h' }
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
