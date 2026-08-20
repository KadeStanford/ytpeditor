param(
    [string]$BuildDirectory = 'build\ucrt-debug',
    [string]$UcrtRoot = 'C:\msys64\ucrt64',
    [string[]]$EditorArguments = @(),
    [switch]$Wait
)

$ErrorActionPreference = 'Stop'

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$runtimeBin = Join-Path $UcrtRoot 'bin'
$msysBin = Join-Path (Split-Path -Parent $UcrtRoot) 'usr\bin'
$editorPath = [IO.Path]::GetFullPath((Join-Path $projectRoot (Join-Path $BuildDirectory 'ytp_editor.exe')))

if (-not (Test-Path -LiteralPath $editorPath)) {
    throw "YTP Editor has not been built at $editorPath. Run .\tools\run-dev.ps1 first."
}
if (-not (Test-Path -LiteralPath (Join-Path $runtimeBin 'Qt6Core.dll'))) {
    throw "The UCRT64 Qt runtime was not found under $runtimeBin."
}

# Start-Process inherits this environment. Keeping all runtime setup here means
# direct shells, Codex sessions, and double-click launches behave identically.
$env:PATH = "$runtimeBin;$msysBin;$env:PATH"
$env:MLT_REPOSITORY = Join-Path $UcrtRoot 'lib\mlt'
$env:MLT_PROFILES_PATH = Join-Path $UcrtRoot 'share\mlt\profiles'
$env:MLT_PRESETS_PATH = Join-Path $UcrtRoot 'share\mlt\presets'
$env:MLT_DATA = Join-Path $UcrtRoot 'share\mlt'

$launchParameters = @{
    FilePath = $editorPath
    WorkingDirectory = Split-Path -Parent $editorPath
    PassThru = $true
}
if ($EditorArguments.Count -gt 0) {
    $launchParameters.ArgumentList = $EditorArguments
}

$editorProcess = Start-Process @launchParameters
Start-Sleep -Milliseconds 750
$editorProcess.Refresh()
if ($editorProcess.HasExited) {
    throw "YTP Editor exited during startup with code $($editorProcess.ExitCode)."
}

Write-Host "YTP Editor launched (PID $($editorProcess.Id))."
if ($Wait) {
    $editorProcess.WaitForExit()
    exit $editorProcess.ExitCode
}
