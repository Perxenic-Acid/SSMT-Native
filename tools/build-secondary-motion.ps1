#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDirectory,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath($SourceDirectory)
$output = [IO.Path]::GetFullPath($OutputDirectory)
$buildScript = Join-Path $source 'build.bat'
$sourceFile = Join-Path $source 'src/sbm.cpp'
if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf) -or
    -not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
    throw "Secondary Motion source is incomplete: $source"
}

Push-Location $source
try {
    & cmd.exe /d /s /c build.bat
    if ($LASTEXITCODE -ne 0) {
        throw "Secondary Motion build failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

$builtDll = Join-Path $source 'bin/sbm.dll'
if (-not (Test-Path -LiteralPath $builtDll -PathType Leaf)) {
    throw "Secondary Motion build did not produce bin/sbm.dll"
}
[IO.Directory]::CreateDirectory($output) | Out-Null
$target = Join-Path $output 'sbm.dll'
Copy-Item -LiteralPath $builtDll -Destination $target -Force
$hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Output "SECONDARY_MOTION_DLL=$target"
Write-Output "SECONDARY_MOTION_SHA256=$hash"
