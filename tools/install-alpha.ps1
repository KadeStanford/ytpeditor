param([Parameter(Mandatory=$true)][string]$PayloadPath)
$ErrorActionPreference = 'Stop'
$installRoot = Join-Path $env:LOCALAPPDATA 'Programs\YTPEditor'
$staging = Join-Path $env:TEMP ('ytp-editor-install-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $staging | Out-Null
try {
    Expand-Archive -LiteralPath $PayloadPath -DestinationPath $staging -Force
    New-Item -ItemType Directory -Force -Path $installRoot | Out-Null
    Copy-Item -Path (Join-Path $staging 'YTPEditor-portable\*') -Destination $installRoot -Recurse -Force
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Programs')) 'YTP Editor.lnk'))
    $shortcut.TargetPath = Join-Path $installRoot 'ytp_editor.exe'
    $shortcut.WorkingDirectory = $installRoot
    $shortcut.Description = 'YTP Editor first usable alpha'
    $shortcut.Save()
    $desktop = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'YTP Editor.lnk'))
    $desktop.TargetPath = Join-Path $installRoot 'ytp_editor.exe'
    $desktop.WorkingDirectory = $installRoot
    $desktop.Description = 'YTP Editor first usable alpha'
    $desktop.Save()
    Start-Process -FilePath (Join-Path $installRoot 'ytp_editor.exe') -WindowStyle Normal
} finally {
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}
