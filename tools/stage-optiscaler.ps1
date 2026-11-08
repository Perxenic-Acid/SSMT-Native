#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ArtifactDirectory,
    [string]$ComponentDirectory = (Join-Path $PSScriptRoot '../dist/Release')
)
$ErrorActionPreference = 'Stop'
$destination = [IO.Path]::GetFullPath($ArtifactDirectory)
$names = @('SSMT-OptiScaler.dll', 'SSMT-OptiScaler-LICENSE.txt', 'SSMT-OptiScaler-source.json', 'SSMT-OptiScaler-THIRD_PARTY_NOTICES.txt')
# 固定组件始终为 Release 构建；Debug 后台同样复用经过来源和内容校验的组件。
$source = if (@($names | Where-Object { -not (Test-Path -LiteralPath (Join-Path $destination $_) -PathType Leaf) }).Count -eq 0) { $destination } else { [IO.Path]::GetFullPath($ComponentDirectory) }
foreach ($name in $names) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $name) -PathType Leaf)) {
        throw "缺少固定组件 $name。请先执行 native/tools/build-optiscaler.ps1 -SourceDirectory <固定提交的源码目录>，再重新构建。"
    }
}
$pin = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'sources/optiscaler.json') -Raw | ConvertFrom-Json
$metadata = Get-Content -LiteralPath (Join-Path $source 'SSMT-OptiScaler-source.json') -Raw | ConvertFrom-Json
$dll = Join-Path $source 'SSMT-OptiScaler.dll'
if ($metadata.repository -cne "https://github.com/$($pin.repository)" -or $metadata.revision -cne $pin.revision -or
    $metadata.dirty -isnot [bool] -or $metadata.dirty -or $metadata.configuration -cne 'Release' -or
    $metadata.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
    (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant() -cne $metadata.sha256 -or
    (Get-Item -LiteralPath $dll).Length -ne $metadata.size) {
    throw '固定 OptiScaler 组件的来源、版本或哈希校验失败，请通过 build-optiscaler.ps1 重新构建。'
}
[IO.Directory]::CreateDirectory($destination) | Out-Null
if (-not $source.Equals($destination, [StringComparison]::OrdinalIgnoreCase)) {
    foreach ($name in $names) { Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $destination $name) -Force }
}
Write-Host "固定 OptiScaler 组件已校验并就绪：$destination"
