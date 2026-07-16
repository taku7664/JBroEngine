param(
    [ValidateRange(1, 1000)]
    [int]$Iterations = 20,

    [ValidateRange(1, 120)]
    [int]$TimeoutSeconds = 20,

    [string]$ApplicationPath
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if ([string]::IsNullOrWhiteSpace($ApplicationPath))
{
    $ApplicationPath = Join-Path $repositoryRoot 'Build\Release_Editor\Application.exe'
}

$ApplicationPath = (Resolve-Path -LiteralPath $ApplicationPath).Path

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class AudioEditorShutdownNativeMethods
{
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PostMessage(
        IntPtr windowHandle,
        uint message,
        IntPtr wordParameter,
        IntPtr longParameter);
}
'@

$windowCloseMessage = 0x0010

for ($iteration = 1; $iteration -le $Iterations; ++$iteration)
{
    $process = $null
    try
    {
        # A hidden Win32 window does not reliably expose MainWindowHandle. Keep the
        # window visible so this test exercises the editor's normal WM_CLOSE path.
        $process = Start-Process `
            -FilePath $ApplicationPath `
            -WorkingDirectory $repositoryRoot `
            -PassThru

        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        $windowHandle = [IntPtr]::Zero
        do
        {
            Start-Sleep -Milliseconds 100
            $process.Refresh()
            if ($process.HasExited)
            {
                break
            }

            $windowHandle = $process.MainWindowHandle
        }
        while ($windowHandle -eq [IntPtr]::Zero -and (Get-Date) -lt $deadline)

        if ($process.HasExited -or $windowHandle -eq [IntPtr]::Zero)
        {
            throw "Iteration $iteration did not create a live main window."
        }

        if (-not [AudioEditorShutdownNativeMethods]::PostMessage(
            $windowHandle,
            $windowCloseMessage,
            [IntPtr]::Zero,
            [IntPtr]::Zero))
        {
            throw "Iteration $iteration failed to post WM_CLOSE."
        }

        if (-not $process.WaitForExit($TimeoutSeconds * 1000))
        {
            throw "Iteration $iteration did not exit normally within the timeout."
        }

        if ($process.ExitCode -ne 0)
        {
            throw "Iteration $iteration exited with code $($process.ExitCode)."
        }
    }
    finally
    {
        if ($null -ne $process -and -not $process.HasExited)
        {
            $process.Kill()
            $process.WaitForExit()
        }
    }
}

Write-Host "$Iterations/$Iterations normal editor shutdowns passed."
