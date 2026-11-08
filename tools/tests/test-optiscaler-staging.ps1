#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
$native = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('ssmt-component-test-' + [Guid]::NewGuid().ToString('N'))
try {
    $source = Join-Path $testRoot 'release'
    $destination = Join-Path $testRoot 'debug'
    [IO.Directory]::CreateDirectory($source) | Out-Null
    $pin = Get-Content (Join-Path $native 'tools/sources/optiscaler.json') -Raw | ConvertFrom-Json
    $dll = Join-Path $source 'SSMT-OptiScaler.dll'
    [IO.File]::WriteAllText($dll, 'component fixture')
    [IO.File]::WriteAllText((Join-Path $source 'SSMT-OptiScaler-LICENSE.txt'), 'license fixture')
    [IO.File]::WriteAllText((Join-Path $source 'SSMT-OptiScaler-THIRD_PARTY_NOTICES.txt'), 'notices fixture')
    @{ repository = "https://github.com/$($pin.repository)"; revision = $pin.revision; dirty = $false; configuration = 'Release'; size = (Get-Item $dll).Length; sha256 = (Get-FileHash $dll -Algorithm SHA256).Hash.ToLowerInvariant() } |
        ConvertTo-Json | Set-Content (Join-Path $source 'SSMT-OptiScaler-source.json')
    $stage = Join-Path $native 'tools/stage-optiscaler.ps1'
    & $stage -ArtifactDirectory $destination -ComponentDirectory $source
    if ((Get-FileHash (Join-Path $destination 'SSMT-OptiScaler.dll')).Hash -cne (Get-FileHash $dll).Hash) { throw '复制后的组件不一致' }
    [IO.File]::WriteAllText((Join-Path $destination 'SSMT-OptiScaler.dll'), 'corrupted fixture')
    $failed = $false
    try { & $stage -ArtifactDirectory $destination -ComponentDirectory $source } catch { $failed = $_.Exception.Message -like '*校验失败*' }
    if (-not $failed) { throw '应拒绝损坏的目标组件' }
    $failed = $false
    try { & $stage -ArtifactDirectory (Join-Path $testRoot 'empty') -ComponentDirectory (Join-Path $testRoot 'missing') } catch { $failed = $_.Exception.Message -like '*build-optiscaler.ps1*' }
    if (-not $failed) { throw '缺少组件应提示明确的构建入口' }
    Write-Host '固定组件回归通过：跨配置复用、损坏拒绝、缺失提示。'
} finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $parent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    if ($resolved.StartsWith($parent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolved) -match '^ssmt-component-test-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
