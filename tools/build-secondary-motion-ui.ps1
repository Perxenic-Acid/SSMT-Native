$ErrorActionPreference = 'Stop'
$uiRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\secondary-motion-ui'))
$bun = Get-Command bun -ErrorAction Stop

Push-Location $uiRoot
try {
    & $bun.Source run build
    if ($LASTEXITCODE -ne 0) { throw "Secondary Motion UI build failed with exit code $LASTEXITCODE" }
}
finally {
    Pop-Location
}

$output = Join-Path $PSScriptRoot '..\official-plugin-packages\ssmt.secondary-motion\3.3.4\ui'
if (-not (Test-Path -LiteralPath (Join-Path $output 'index.html') -PathType Leaf)) {
    throw "Secondary Motion UI output is incomplete: $output"
}
Write-Output $output
