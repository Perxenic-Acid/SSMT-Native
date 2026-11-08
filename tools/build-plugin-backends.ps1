#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('player-tweaks', 'hoyoshade', 'dlss5')]
    [string[]]$Backends = @('player-tweaks', 'hoyoshade', 'dlss5'),
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\dist\Release'),
    [switch]$Release
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$arguments = @('build', '--offline', '--manifest-path', (Join-Path $repo 'Cargo.toml'))
foreach ($backend in $Backends) { $arguments += @('-p', "ssmt-$backend-backend") }
if ($Release) { $arguments += '--release' }
& cargo @arguments
if ($LASTEXITCODE -ne 0) { throw '插件后台编译失败' }
$profile = if ($Release) { 'release' } else { 'debug' }
$destination = [IO.Path]::GetFullPath($ArtifactDirectory)
[IO.Directory]::CreateDirectory($destination) | Out-Null
foreach ($backend in $Backends) {
    $name = "ssmt-$backend-backend.exe"
    Copy-Item -LiteralPath (Join-Path $repo "target\$profile\$name") -Destination (Join-Path $destination $name)
}
if ('dlss5' -in $Backends) {
    & (Join-Path $PSScriptRoot 'build-dlss5-ui.ps1') -ArtifactDirectory $destination
    if (-not $?) { throw '插件页面构建失败' }
}
Write-Output "BACKEND_ARTIFACT=$destination"
