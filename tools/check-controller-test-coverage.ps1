param([string]$Root = (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference = 'Stop'
$headers = @(
    'src/ui/project_controller.h',
    'src/ui/timeline_controller.h',
    'src/ui/export_controller.h'
)
$testFiles = Get-ChildItem -LiteralPath (Join-Path $Root 'tests') -File -Filter '*.cpp'
$missing = @()

foreach ($relativeHeader in $headers) {
    $header = Join-Path $Root $relativeHeader
    $source = Get-Content -Raw -LiteralPath $header
    $methods = [regex]::Matches($source, 'Q_INVOKABLE\s+(?:\[\[nodiscard\]\]\s+)?(?:[\w:<>]+\s+)+(?<name>\w+)\s*\(')
    foreach ($method in $methods) {
        $name = $method.Groups['name'].Value
        $pattern = "\b$([regex]::Escape($name))\s*\("
        $references = $testFiles | Select-String -Pattern $pattern
        if (-not $references) {
            $missing += "$relativeHeader::$name"
        }
    }
}

if ($missing.Count -gt 0) {
    Write-Error ("UI-facing controller methods without a direct test reference:`n" + ($missing -join "`n"))
}

Write-Output 'Every Q_INVOKABLE Project, Timeline, and Export controller method has a direct automated-test reference.'
