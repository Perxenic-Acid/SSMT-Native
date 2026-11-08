$ErrorActionPreference = 'Stop'
$uiRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\liquid-gravity-probe-ui'))
$bun = Get-Command bun -ErrorAction Stop

Push-Location $uiRoot
try {
    & $bun.Source run build
    if ($LASTEXITCODE -ne 0) { throw "Liquid Gravity Probe UI build failed with exit code $LASTEXITCODE" }
}
finally {
    Pop-Location
}

$output = Join-Path $PSScriptRoot '..\official-plugin-packages\ssmt.liquid-gravity-probe\0.2.33\ui'
if (-not (Test-Path -LiteralPath (Join-Path $output 'index.html') -PathType Leaf)) {
    throw "Liquid Gravity Probe UI output is incomplete: $output"
}
Write-Output $output
