param([switch]$SkipBuild, [switch]$StageAlongside, [string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$release = if ($BuildDirectory) { [IO.Path]::GetFullPath((Join-Path $repo $BuildDirectory)) } else { Join-Path $repo 'build\ucrt-release' }
$dist = Join-Path $repo 'dist'
$stageRoot = if ($StageAlongside) { Join-Path $dist '.package-staging' } else { $dist }
$portable = Join-Path $stageRoot 'YTPEditor-portable'
if (-not $SkipBuild) {
    $env:PATH = 'C:\msys64\ucrt64\bin;C:\msys64\usr\bin;' + $env:PATH
    if ($BuildDirectory) {
        & cmake -S $repo -B $release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64 -DPKG_CONFIG_EXECUTABLE=C:/msys64/ucrt64/bin/pkg-config.exe -DYTP_BUILD_TESTS=OFF -DYTP_ENABLE_QT_UI=ON -DYTP_ENABLE_MLT=ON
    } else {
        & cmake --preset windows-ucrt-release
    }
    if ($LASTEXITCODE -ne 0) { throw 'Release configuration failed.' }
    if ($BuildDirectory) { & cmake --build $release -j 4 } else { & cmake --build --preset windows-ucrt-release -j 4 }
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
}
$resolvedPortable = [IO.Path]::GetFullPath($portable)
$resolvedDist = [IO.Path]::GetFullPath($dist)
if (-not $resolvedPortable.StartsWith($resolvedDist, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe portable output path.' }
if ($StageAlongside -and (Test-Path -LiteralPath $stageRoot)) { Remove-Item -LiteralPath $stageRoot -Recurse -Force }
if (Test-Path -LiteralPath $portable) { Remove-Item -LiteralPath $portable -Recurse -Force }
New-Item -ItemType Directory -Force -Path $portable | Out-Null
Copy-Item -LiteralPath (Join-Path $release 'ytp_editor.exe') -Destination $portable
Copy-Item -LiteralPath (Join-Path $repo 'tools\qt.conf') -Destination $portable
& 'C:\msys64\ucrt64\bin\windeployqt6.exe' --release --no-quick-import --no-translations (Join-Path $portable 'ytp_editor.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
Copy-Item -LiteralPath 'C:\msys64\ucrt64\share\qt6\plugins\platforms\qoffscreen.dll' -Destination (Join-Path $portable 'platforms') -Force
Copy-Item -LiteralPath 'C:\msys64\ucrt64\share\qt6\plugins\multimedia' -Destination $portable -Recurse -Force
$qmlTarget = Join-Path $portable 'qml'
New-Item -ItemType Directory -Force -Path $qmlTarget | Out-Null
foreach ($module in @('QtQuick','QtMultimedia','QtCore')) {
    Copy-Item -LiteralPath (Join-Path 'C:\msys64\ucrt64\share\qt6\qml' $module) -Destination $qmlTarget -Recurse -Force
}

$binaryQueue = [Collections.Generic.Queue[string]]::new()
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($name in @('ffmpeg.exe','ffprobe.exe')) {
    $source = Join-Path 'C:\msys64\ucrt64\bin' $name
    Copy-Item -LiteralPath $source -Destination $portable
    $binaryQueue.Enqueue((Join-Path $portable $name))
}
Copy-Item -LiteralPath 'C:\msys64\ucrt64\lib\frei0r-1' -Destination $portable -Recurse -Force
Get-ChildItem -LiteralPath $portable -Recurse -File | Where-Object { $_.Extension -in @('.exe','.dll') } | ForEach-Object { $binaryQueue.Enqueue($_.FullName) }
while ($binaryQueue.Count -gt 0) {
    $binary = $binaryQueue.Dequeue()
    $dependencies = & 'C:\msys64\ucrt64\bin\objdump.exe' -p $binary 2>$null | Select-String 'DLL Name:' | ForEach-Object { ($_ -split 'DLL Name:')[1].Trim() }
    foreach ($dependency in $dependencies) {
        if (-not $seen.Add($dependency)) { continue }
        $source = Join-Path 'C:\msys64\ucrt64\bin' $dependency
        $target = Join-Path $portable $dependency
        if ((Test-Path -LiteralPath $source) -and -not (Test-Path -LiteralPath $target)) {
            Copy-Item -LiteralPath $source -Destination $target
            $binaryQueue.Enqueue($target)
        }
    }
}
Copy-Item -LiteralPath (Join-Path $repo 'README.md'),(Join-Path $repo 'THIRD_PARTY_NOTICES.md'),(Join-Path $repo 'docs\USER_GUIDE.md'),(Join-Path $repo 'docs\RELEASE_CHECKLIST.md'),(Join-Path $repo 'tools\uninstall-alpha.ps1') -Destination $portable
$portableZip = Join-Path $dist 'YTPEditor-0.5.0-alpha-portable.zip'
if (Test-Path -LiteralPath $portableZip) { Remove-Item -LiteralPath $portableZip -Force }
Compress-Archive -LiteralPath $portable -DestinationPath $portableZip -CompressionLevel Optimal
Copy-Item -LiteralPath $portableZip -Destination (Join-Path $dist 'payload.zip') -Force

$sed = Join-Path $dist 'alpha-installer.sed'
$setup = Join-Path $dist 'YTPEditor-0.5.0-alpha-setup.exe'
if (Test-Path -LiteralPath $setup) { Remove-Item -LiteralPath $setup -Force }
$sedText = @"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=0
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=Install YTP Editor 0.5.0 alpha?
DisplayLicense=
FinishMessage=YTP Editor was installed for the current user.
TargetName=$setup
FriendlyName=YTP Editor 0.5.0 Alpha Setup
AppLaunched=install-alpha.cmd
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
SourceFiles=SourceFiles
[SourceFiles]
SourceFiles0=$dist\
SourceFiles1=$repo\tools\
[SourceFiles0]
payload.zip=
[SourceFiles1]
install-alpha.cmd=
install-alpha.ps1=
"@
Set-Content -LiteralPath $sed -Value $sedText -Encoding Ascii
& "$env:WINDIR\System32\iexpress.exe" /N $sed
# IExpress returns 1 on some current Windows builds even after emitting a valid package.
for ($attempt = 0; $attempt -lt 180; $attempt++) {
    $builderRunning = Get-Process iexpress -ErrorAction SilentlyContinue
    if ((Test-Path -LiteralPath $setup) -and (Get-Item -LiteralPath $setup).Length -ge 1MB -and -not $builderRunning) { break }
    Start-Sleep -Seconds 1
}
if (-not (Test-Path -LiteralPath $setup) -or (Get-Item -LiteralPath $setup).Length -lt 1MB) { throw 'IExpress installer creation failed.' }
Remove-Item -LiteralPath (Join-Path $dist 'payload.zip') -Force
$checksums = Join-Path $dist 'SHA256SUMS.txt'
$portableHash = (Get-FileHash -LiteralPath $portableZip -Algorithm SHA256).Hash
$setupHash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash
Set-Content -LiteralPath $checksums -Encoding Ascii -Value @(
    "$portableHash  $([IO.Path]::GetFileName($portableZip))",
    "$setupHash  $([IO.Path]::GetFileName($setup))"
)
if ($StageAlongside -and (Test-Path -LiteralPath $stageRoot)) { Remove-Item -LiteralPath $stageRoot -Recurse -Force }
Write-Host "Portable: $portableZip"
Write-Host "Installer: $setup"
Write-Host "Checksums: $checksums"
