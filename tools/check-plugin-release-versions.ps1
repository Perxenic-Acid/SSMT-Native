#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._/-]*$')]
    [string]$BaseRef,
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..')
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath($RepositoryRoot)
function Read-Git([string[]]$Arguments) {
    $result = & git -C $repo @Arguments
    if ($LASTEXITCODE -ne 0) { throw "无法读取发布基线：git $($Arguments -join ' ')" }
    return $result
}

# 基线必须来自已公开的 Native Release，而不是本机最新提交或 Actions 构建。
Read-Git @('rev-parse', '--verify', "$BaseRef^{commit}") | Out-Null
$baseFiles = @(Read-Git @('ls-tree', '-r', '--name-only', $BaseRef, '--', 'official-plugin-packages'))
$previous = @{}
foreach ($path in $baseFiles | Where-Object { $_ -match '^official-plugin-packages/[^/]+/[^/]+/ssmt-plugin\.json$' }) {
    $statusPath = $path -replace 'ssmt-plugin\.json$', 'release-status.json'
    if ($statusPath -notin $baseFiles) { continue }
    $status = (Read-Git @('show', "${BaseRef}:$statusPath")) -join "`n" | ConvertFrom-Json
    if ($status.publish -ne $true) { continue }
    $manifest = (Read-Git @('show', "${BaseRef}:$path")) -join "`n" | ConvertFrom-Json
    $version = [semver]$manifest.version
    if (-not $previous.ContainsKey($manifest.id) -or $version -gt $previous[$manifest.id].version) {
        $previous[$manifest.id] = @{ version = $version; status = $status }
    }
}

$latest = @{}
foreach ($idDir in Get-ChildItem -LiteralPath (Join-Path $repo 'official-plugin-packages') -Directory) {
    foreach ($versionDir in Get-ChildItem -LiteralPath $idDir.FullName -Directory) {
        $statusPath = Join-Path $versionDir.FullName 'release-status.json'
        if (-not (Test-Path -LiteralPath $statusPath -PathType Leaf)) { continue }
        $status = Get-Content -LiteralPath $statusPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($status.publish -ne $true) { continue }
        $manifest = Get-Content -LiteralPath (Join-Path $versionDir.FullName 'ssmt-plugin.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($manifest.id -cne $idDir.Name -or $manifest.version -cne $versionDir.Name) {
            throw "插件清单与目录不一致：$($versionDir.FullName)"
        }
        $version = [semver]$manifest.version
        if (-not $latest.ContainsKey($manifest.id) -or $version -gt $latest[$manifest.id].version) {
            $latest[$manifest.id] = @{ version = $version; status = $status }
        }
    }
}

foreach ($id in $latest.Keys | Sort-Object) {
    $current = $latest[$id]
    $base = $previous[$id]
    $paths = @($current.status.sourcePaths | Where-Object { $null -ne $_ })
    $oldPaths = @($base.status.sourcePaths | Where-Object { $null -ne $_ })
    if (-not $paths.Count -and -not $oldPaths.Count) { continue }
    if (-not $paths.Count) { throw "不得移除已有插件的 sourcePaths 发布检查：$id" }
    foreach ($path in @($paths + $oldPaths)) {
        if ($path -isnot [string] -or $path -notmatch '^[A-Za-z0-9][A-Za-z0-9._/-]*$' -or
            $path -match '(^|/)\.\.(/|$)') { throw "无效的插件源码路径：$id / $path" }
    }
    if ($null -eq $base) { continue }
    if ($current.version -lt $base.version) { throw "插件版本不得低于已发布版本：$id $($current.version) < $($base.version)" }
    if ($current.version -gt $base.version) {
        Write-Host "插件版本检查通过：$id $($base.version) -> $($current.version)"
        continue
    }
    $changed = @(Read-Git (@('diff', '--name-only', $BaseRef, '--', "official-plugin-packages/$id") + $paths + $oldPaths))
    if ($changed.Count) {
        throw "插件源码或清单已改变，但版本仍为 $id $($current.version)。请新增版本清单后发布；Native Release 标签变化不能代替插件版本升级。改动：$($changed -join ', ')"
    }
    Write-Host "插件版本检查通过：$id $($current.version)（源码未变）"
}
