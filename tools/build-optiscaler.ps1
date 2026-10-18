param(
    [Parameter(Mandatory = $true)][string]$SourceDirectory,
    [string]$NativeArtifactDirectory = (Join-Path $PSScriptRoot '../dist/Release')
)
$ErrorActionPreference = 'Stop'
$pin = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'sources/optiscaler.json') -Raw | ConvertFrom-Json
$source = (Resolve-Path -LiteralPath $SourceDirectory).Path
if ($pin.repository -cne 'Perxenic-Acid/OptiScaler_DLSSNR' -or $pin.revision -notmatch '^[0-9a-f]{40}$') {
    throw 'OptiScaler 固定来源声明无效'
}
$revision = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -cne $pin.revision) { throw "OptiScaler 源码必须位于固定提交 $($pin.revision)" }
$dirty = & git -C $source status --porcelain
if ($LASTEXITCODE -ne 0 -or $dirty) { throw 'OptiScaler 源码有未提交修改，拒绝作为插件发布组件' }

# 该入口只编译独立组件并执行 CPU 界面回归，不运行上游游戏目录安装器。
& (Join-Path $source 'scripts/build-ssmt.ps1')
if (-not $?) { throw 'OptiScaler 组件构建失败' }
$metadataPath = Join-Path $source 'build/ssmt/artifact.json'
$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
if ($metadata.dirty -or $metadata.revision -cne $pin.revision -or $metadata.configuration -cne 'Release') {
    throw 'OptiScaler 产物来源与固定构建要求不一致'
}
$dll = Join-Path $source 'x64/Release/a/OptiScaler.dll'
if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant() -cne $metadata.sha256 -or
    (Get-Item -LiteralPath $dll).Length -ne $metadata.size) { throw 'OptiScaler 构建产物校验失败' }
$destination = [IO.Path]::GetFullPath($NativeArtifactDirectory)
[IO.Directory]::CreateDirectory($destination) | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $destination 'SSMT-OptiScaler.dll') -Force
Copy-Item -LiteralPath (Join-Path $source 'LICENSE') -Destination (Join-Path $destination 'SSMT-OptiScaler-LICENSE.txt') -Force
Copy-Item -LiteralPath $metadataPath -Destination (Join-Path $destination 'SSMT-OptiScaler-source.json') -Force
Copy-Item -LiteralPath (Join-Path $source 'build/ssmt/THIRD_PARTY_NOTICES.txt') -Destination (Join-Path $destination 'SSMT-OptiScaler-THIRD_PARTY_NOTICES.txt') -Force
Write-Host "OptiScaler 插件组件已准备：$destination"
