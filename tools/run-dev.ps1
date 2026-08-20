param(
    [string[]]$EditorArguments = @()
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$cmake = 'C:\msys64\ucrt64\bin\cmake.exe'

if (-not (Test-Path -LiteralPath $cmake)) {
    throw 'The UCRT64 CMake toolchain is not installed at C:\msys64\ucrt64.'
}

Push-Location $projectRoot
try {
    # Windows locks a running executable, so an old development instance must
    # exit before the linker can replace it. Close only the editor from this
    # build directory; never touch installed or unrelated copies.
    $editorPath = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build\ucrt-debug\ytp_editor.exe'))
    $runningEditors = @(Get-Process -Name 'ytp_editor' -ErrorAction SilentlyContinue | Where-Object {
        try { [string]::Equals($_.Path, $editorPath, [StringComparison]::OrdinalIgnoreCase) } catch { $false }
    })
    foreach ($editor in $runningEditors) {
        [void]$editor.CloseMainWindow()
        if (-not $editor.WaitForExit(5000)) {
            throw "The existing development editor (PID $($editor.Id)) did not close. Save or discard its open project, close it, and run this command again."
        }
    }

    & $cmake --preset windows-ucrt-debug
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $cmake --build --preset windows-ucrt-debug --target ytp_editor
    if ($LASTEXITCODE -ne 0) { throw 'YTP Editor build failed.' }

    & (Join-Path $PSScriptRoot 'launch-dev.ps1') -EditorArguments $EditorArguments
} finally {
    Pop-Location
}
