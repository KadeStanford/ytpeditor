$ErrorActionPreference = 'Stop'
$installRoot = Join-Path $env:LOCALAPPDATA 'Programs\YTPEditor'
Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('Programs')) 'YTP Editor.lnk') -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('Desktop')) 'YTP Editor.lnk') -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $installRoot) {
    $cleanup = "Start-Sleep -Seconds 2; Remove-Item -LiteralPath '$($installRoot.Replace("'", "''"))' -Recurse -Force"
    Start-Process powershell.exe -ArgumentList '-NoProfile','-WindowStyle','Hidden','-Command',$cleanup -WindowStyle Hidden
}
